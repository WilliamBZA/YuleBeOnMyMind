using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading.Channels;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Options;
using Yule.Server.Configuration;
using Yule.Server.Data;

namespace Yule.Server.Broker;

/// <summary>What a device reported on its status topic. Fields a message does not carry are null.</summary>
public sealed record StatusUpdate(string State, string? FirmwareVersion, string? Board, string? Name, string? OtaResult);

/// <summary>Reading <c>yule/v1/devices/&lt;id&gt;/status</c> messages (firmware <c>status_payload</c>): the online report and the broker's Last Will.</summary>
public static class StatusMessages
{
    public static string Filter(string root) => $"{root}/devices/+/status";

    /// <summary>Gets the device id out of <c>&lt;root&gt;/devices/&lt;id&gt;/status</c>; false for any other topic.</summary>
    public static bool TryParseTopic(string root, string topic, out string deviceId)
    {
        deviceId = "";
        var prefix = root + "/devices/";
        const string suffix = "/status";
        if (!topic.StartsWith(prefix, StringComparison.Ordinal) || !topic.EndsWith(suffix, StringComparison.Ordinal)
            || topic.Length <= prefix.Length + suffix.Length)
        {
            return false;
        }

        var id = topic[prefix.Length..^suffix.Length];
        if (!Identifiers.IsValidDeviceId(id))
        {
            return false;
        }

        deviceId = id;
        return true;
    }

    /// <summary>Reads the payload, ignoring unknown fields (newer firmware). False unless it is a JSON object with a state of online or offline.</summary>
    public static bool TryParsePayload(string payload, out StatusUpdate update)
    {
        update = null!;
        JsonNode? node;
        try
        {
            node = JsonNode.Parse(payload);
        }
        catch (JsonException)
        {
            return false;
        }

        if (node is not JsonObject obj)
        {
            return false;
        }

        var state = Text(obj, "state");
        if (state is not ("online" or "offline"))
        {
            return false;
        }

        update = new StatusUpdate(state, Text(obj, "fw"), Text(obj, "board"), Text(obj, "name"), Text(obj, "ota"));
        return true;
    }

    private static string? Text(JsonObject obj, string key) =>
        obj[key] is JsonValue value && value.TryGetValue<string>(out var text) ? text : null;
}

/// <summary>
/// Records each registered device's reported state in <see cref="DeviceStatus"/>. The broker keeps the latest
/// status of every device (retained), so subscribing at start-up, and again after a reconnection, refreshes them all.
/// </summary>
public sealed class StatusListener : BackgroundService
{
    private readonly IBrokerConnection _connection;
    private readonly IServiceScopeFactory _scopes;
    private readonly string _root;
    private readonly ILogger<StatusListener> _logger;
    private readonly Channel<(string DeviceId, StatusUpdate Update)> _queue = Channel.CreateUnbounded<(string, StatusUpdate)>();

    public StatusListener(IBrokerConnection connection, IServiceScopeFactory scopes, IOptions<YuleOptions> options, ILogger<StatusListener> logger)
    {
        _connection = connection;
        _scopes = scopes;
        _root = options.Value.TopicRoot;
        _logger = logger;
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        _connection.MessageReceived += OnMessage;
        try
        {
            // Registered now and applied after every (re)connection, whenever the connection comes up.
            await _connection.SubscribeAsync(StatusMessages.Filter(_root), stoppingToken);

            await foreach (var (deviceId, update) in _queue.Reader.ReadAllAsync(stoppingToken))
            {
                try
                {
                    await RecordAsync(deviceId, update, stoppingToken);
                }
                catch (Exception ex) when (ex is not OperationCanceledException)
                {
                    _logger.LogError(ex, "Could not record the status of {Device}", deviceId);
                }
            }
        }
        catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested)
        {
        }
        finally
        {
            _connection.MessageReceived -= OnMessage;
        }
    }

    private void OnMessage(BrokerMessage message)
    {
        if (!StatusMessages.TryParseTopic(_root, message.Topic, out var deviceId))
        {
            return;
        }

        // A cleared retained message (empty payload) means there is no status to record.
        if (message.Payload.Length == 0)
        {
            return;
        }

        if (!StatusMessages.TryParsePayload(message.Payload, out var update))
        {
            _logger.LogWarning("Ignoring a malformed status message from {Device}", deviceId);
            return;
        }

        _queue.Writer.TryWrite((deviceId, update));
    }

    private async Task RecordAsync(string deviceId, StatusUpdate update, CancellationToken cancellationToken)
    {
        await using var scope = _scopes.CreateAsyncScope();
        var db = scope.ServiceProvider.GetRequiredService<YuleDbContext>();

        // Only registered devices have a status row; the broker would not accept anyone else's status anyway.
        if (!await db.Devices.AnyAsync(d => d.Id == deviceId, cancellationToken))
        {
            return;
        }

        var status = await db.DeviceStatuses.FindAsync([deviceId], cancellationToken);
        if (status is null)
        {
            status = new DeviceStatus { DeviceId = deviceId };
            db.DeviceStatuses.Add(status);
        }

        status.State = update.State;
        if (update.State == "online")
        {
            // An online report is the device's complete description of itself; replace what was there.
            status.FirmwareVersion = update.FirmwareVersion;
            status.Board = update.Board;
            status.Name = update.Name;
            status.OtaResult = update.OtaResult;
        }

        // For the Last Will this is when the broker noticed the device had gone, close enough to when it was last seen.
        status.LastSeenUtc = DateTime.UtcNow;
        await db.SaveChangesAsync(cancellationToken);
    }
}
