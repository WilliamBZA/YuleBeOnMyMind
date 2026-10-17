using System.Collections.Concurrent;
using System.Text;
using System.Text.Json.Nodes;

namespace Yule.Server.Broker;

/// <summary>The broker's answer to one dynamic-security command. <see cref="Error"/> is null on success.</summary>
public sealed record DynsecResponse(string Command, string? Error, JsonNode? Data);

public sealed class DynsecException(string message) : Exception(message);

/// <summary>
/// Talks to Mosquitto's dynamic-security plugin over MQTT: publishes commands to <c>$CONTROL/dynamic-security/v1</c>
/// and matches each answer on the response topic to its request by <c>correlationData</c>. Safe for concurrent use.
/// </summary>
public sealed class DynsecClient
{
    // The broker drops any packet over max_packet_size (4096) and disconnects the sender, so requests are split well below it.
    private const int MaxRequestBytes = 3000;
    private const int PageSize = 100;

    private readonly IBrokerConnection _connection;
    private readonly ConcurrentDictionary<string, TaskCompletionSource<DynsecResponse>> _pending = new();
    private long _nextId;

    public DynsecClient(IBrokerConnection connection)
    {
        _connection = connection;
        _connection.MessageReceived += OnMessage;
    }

    /// <summary>How long to wait for the broker to answer one request.</summary>
    public TimeSpan RequestTimeout { get; init; } = TimeSpan.FromSeconds(10);

    /// <summary>Sends the commands in order, split across as many requests as the broker's packet limit needs, and returns one response per command. Command errors are returned, not thrown.</summary>
    public async Task<IReadOnlyList<DynsecResponse>> SendAsync(IReadOnlyList<DynsecCommand> commands, CancellationToken cancellationToken)
    {
        var responses = new List<DynsecResponse>(commands.Count);
        foreach (var chunk in Chunk(commands))
        {
            responses.AddRange(await SendRequestAsync(chunk, cancellationToken));
        }

        return responses;
    }

    /// <summary>Like <see cref="SendAsync"/> but throws a <see cref="DynsecException"/> naming every command the broker rejected.</summary>
    public async Task<IReadOnlyList<DynsecResponse>> ExecuteAsync(IReadOnlyList<DynsecCommand> commands, CancellationToken cancellationToken)
    {
        var responses = await SendAsync(commands, cancellationToken);
        var failures = responses.Zip(commands, (response, command) => (response, command))
            .Where(pair => pair.response.Error is not null)
            .Select(pair => $"{pair.command.Name}: {pair.response.Error} ({pair.command})")
            .ToList();
        if (failures.Count > 0)
        {
            throw new DynsecException("The broker rejected dynamic-security commands: " + string.Join("; ", failures));
        }

        return responses;
    }

    /// <summary>Reads every device client and device role the broker holds. Accounts that are not devices (the operator and this server) are ignored.</summary>
    public async Task<ActualBrokerState> ReadStateAsync(CancellationToken cancellationToken)
    {
        var clientNames = (await ListNamesAsync(DynsecCommands.ListClients, "clients", "username", cancellationToken))
            .Where(BrokerProjection.IsManagedClient).ToList();
        var roleNames = (await ListNamesAsync(DynsecCommands.ListRoles, "roles", "rolename", cancellationToken))
            .Where(BrokerProjection.IsManagedRole).ToList();

        var clientResponses = await ExecuteAsync(clientNames.Select(DynsecCommands.GetClient).ToList(), cancellationToken);
        var roleResponses = await ExecuteAsync(roleNames.Select(DynsecCommands.GetRole).ToList(), cancellationToken);

        var clients = clientResponses.Select(r => ParseClient(r.Data!["client"]!)).ToList();
        var roles = new Dictionary<string, IReadOnlyList<AclEntry>>(StringComparer.Ordinal);
        foreach (var response in roleResponses)
        {
            var role = response.Data!["role"]!;
            roles[role["rolename"]!.GetValue<string>()] = role["acls"]!.AsArray().Select(ParseAcl).ToList();
        }

        return new ActualBrokerState(clients, roles);
    }

    /// <summary>Creates the role, or adds and removes only the ACLs that differ. Returns whether the broker was changed; applying the same role again returns false and sends nothing.</summary>
    public async Task<bool> EnsureRoleAsync(DesiredRole role, CancellationToken cancellationToken)
    {
        var existing = await TryGetAsync(DynsecCommands.GetRole(role.Name), cancellationToken);
        var actual = existing?["role"]!["acls"]!.AsArray().Select(ParseAcl).ToList();
        var commands = BrokerPlanner.PlanRole(role, actual);
        if (commands.Count > 0)
        {
            await ExecuteAsync(commands, cancellationToken);
        }

        return commands.Count > 0;
    }

    /// <summary>Creates the client, or fixes its client id or role if they differ; never touches an existing client's password. Returns whether the broker was changed.</summary>
    public async Task<bool> EnsureClientAsync(DesiredClient client, CancellationToken cancellationToken)
    {
        var existing = await TryGetAsync(DynsecCommands.GetClient(client.Username), cancellationToken);
        var commands = BrokerPlanner.PlanClient(client, existing is null ? null : ParseClient(existing["client"]!));
        if (commands.Count > 0)
        {
            await ExecuteAsync(commands, cancellationToken);
        }

        return commands.Count > 0;
    }

    private async Task<JsonNode?> TryGetAsync(DynsecCommand get, CancellationToken cancellationToken)
    {
        var response = (await SendAsync([get], cancellationToken))[0];
        if (response.Error is null)
        {
            return response.Data;
        }

        // Mosquitto has no error codes; "not found" is how it says the client or role does not exist.
        if (response.Error.Contains("not found", StringComparison.OrdinalIgnoreCase))
        {
            return null;
        }

        throw new DynsecException($"{get.Name} failed: {response.Error}");
    }

    private async Task<List<string>> ListNamesAsync(Func<int, int, DynsecCommand> list, string arrayName, string nameField, CancellationToken cancellationToken)
    {
        var names = new List<string>();
        while (true)
        {
            var data = (await ExecuteAsync([list(PageSize, names.Count)], cancellationToken))[0].Data!;
            var page = data[arrayName]!.AsArray()
                .Select(item => item is JsonObject o ? o[nameField]!.GetValue<string>() : item!.GetValue<string>())
                .ToList();
            names.AddRange(page);
            if (page.Count == 0 || names.Count >= data["totalCount"]!.GetValue<int>())
            {
                return names;
            }
        }
    }

    private static ActualClient ParseClient(JsonNode client) => new(
        client["username"]!.GetValue<string>(),
        client["clientid"]?.GetValue<string>(),
        client["roles"]!.AsArray().Select(r => r!["rolename"]!.GetValue<string>()).ToList());

    private static AclEntry ParseAcl(JsonNode? acl) => new(
        acl!["acltype"]!.GetValue<string>(),
        acl["topic"]!.GetValue<string>(),
        acl["allow"]?.GetValue<bool>() ?? true,
        acl["priority"]?.GetValue<int>() ?? 0);

    private async Task<DynsecResponse[]> SendRequestAsync(List<DynsecCommand> commands, CancellationToken cancellationToken)
    {
        await _connection.WaitUntilConnectedAsync(cancellationToken);
        await _connection.SubscribeAsync(DynsecCommands.ResponseTopic, cancellationToken);

        var ids = new string[commands.Count];
        var waiting = new TaskCompletionSource<DynsecResponse>[commands.Count];
        var array = new JsonArray();
        for (var i = 0; i < commands.Count; i++)
        {
            ids[i] = "y" + Interlocked.Increment(ref _nextId);
            waiting[i] = new TaskCompletionSource<DynsecResponse>(TaskCreationOptions.RunContinuationsAsynchronously);
            _pending[ids[i]] = waiting[i];

            var body = (JsonObject)commands[i].Body.DeepClone();
            body["correlationData"] = ids[i];
            array.Add(body);
        }

        try
        {
            var request = new JsonObject { ["commands"] = array }.ToJsonString();
            await _connection.PublishAsync(DynsecCommands.ControlTopic, request, retain: false, cancellationToken);

            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeout.CancelAfter(RequestTimeout);
            var responses = new DynsecResponse[commands.Count];
            try
            {
                for (var i = 0; i < responses.Length; i++)
                {
                    responses[i] = await waiting[i].Task.WaitAsync(timeout.Token);
                }
            }
            catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
            {
                throw new TimeoutException($"The broker did not answer {commands[0].Name} within {RequestTimeout.TotalSeconds:0} s. Is the dynamic-security plugin loaded and the yule-server role in place?");
            }

            return responses;
        }
        finally
        {
            foreach (var id in ids)
            {
                _pending.TryRemove(id, out _);
            }
        }
    }

    private void OnMessage(BrokerMessage message)
    {
        if (message.Topic != DynsecCommands.ResponseTopic)
        {
            return;
        }

        JsonNode? parsed;
        try
        {
            parsed = JsonNode.Parse(message.Payload);
        }
        catch (System.Text.Json.JsonException)
        {
            return;
        }

        if (parsed?["responses"] is not JsonArray responses)
        {
            return;
        }

        foreach (var item in responses)
        {
            var id = item?["correlationData"]?.GetValue<string>();
            if (id is not null && _pending.TryGetValue(id, out var waiter))
            {
                waiter.TrySetResult(new DynsecResponse(
                    item!["command"]?.GetValue<string>() ?? "",
                    item["error"]?.GetValue<string>(),
                    item["data"]));
            }
        }
    }

    private static List<List<DynsecCommand>> Chunk(IReadOnlyList<DynsecCommand> commands)
    {
        var chunks = new List<List<DynsecCommand>>();
        var current = new List<DynsecCommand>();
        var size = 0;
        foreach (var command in commands)
        {
            var bytes = Encoding.UTF8.GetByteCount(command.Body.ToJsonString()) + 40; // 40: the correlationData member and the comma
            if (current.Count > 0 && size + bytes > MaxRequestBytes)
            {
                chunks.Add(current);
                current = [];
                size = 0;
            }

            current.Add(command);
            size += bytes;
        }

        if (current.Count > 0)
        {
            chunks.Add(current);
        }

        return chunks;
    }
}
