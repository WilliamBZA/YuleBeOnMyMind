using System.Text;
using System.Text.Json.Nodes;
using Yule.Server.Broker;

namespace Yule.Server.Tests;

internal sealed record PublishedMessage(string Topic, string Payload, bool Retain);

/// <summary>An <see cref="IBrokerConnection"/> that records what is published and lets a test play the broker.</summary>
internal sealed class FakeBrokerConnection : IBrokerConnection
{
    public List<PublishedMessage> Published { get; } = [];

    public List<string> Subscriptions { get; } = [];

    /// <summary>Called for every publish, after it is recorded. This is where a fake broker answers.</summary>
    public Func<PublishedMessage, Task>? OnPublish { get; set; }

    public bool IsConnected => true;

    public event Action<BrokerMessage>? MessageReceived;

    public event Action? Connected;

    public void RaiseConnected() => Connected?.Invoke();

    public void Receive(BrokerMessage message) => MessageReceived?.Invoke(message);

    public Task WaitUntilConnectedAsync(CancellationToken cancellationToken) => Task.CompletedTask;

    public async Task PublishAsync(string topic, string payload, bool retain, CancellationToken cancellationToken)
    {
        var message = new PublishedMessage(topic, payload, retain);
        lock (Published)
        {
            Published.Add(message);
        }

        if (OnPublish is not null)
        {
            await OnPublish(message);
        }
    }

    public Task SubscribeAsync(string topicFilter, CancellationToken cancellationToken)
    {
        Subscriptions.Add(topicFilter);
        return Task.CompletedTask;
    }
}

/// <summary>
/// A small in-memory stand-in for Mosquitto's dynamic-security plugin: enough of the command set, with the same
/// request and response shapes (checked against a real broker), to test the server's logic without Docker.
/// Mosquitto itself is exercised by the integration tests.
/// </summary>
internal sealed class FakeDynsecBroker
{
    public sealed class Client(string username)
    {
        public string Username { get; } = username;

        public string? ClientId { get; set; }

        public string? Password { get; set; }

        public List<string> Roles { get; } = [];
    }

    public Dictionary<string, Client> Clients { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, List<AclEntry>> Roles { get; } = new(StringComparer.Ordinal);

    /// <summary>Every command that changed something, as <c>name target</c>, in the order received.</summary>
    public List<string> Changes { get; } = [];

    /// <summary>The UTF-8 size of every request received; the real broker disconnects a client that sends more than 4096 bytes.</summary>
    public List<int> RequestSizes { get; } = [];

    public FakeDynsecBroker()
    {
        // The operator and the server's own account, which the server must never touch.
        Clients["admin"] = new Client("admin") { Password = "admin-pw" };
        Clients["admin"].Roles.Add("admin");
        Roles["admin"] = [new AclEntry(AclTypes.PublishClientSend, "$CONTROL/dynamic-security/#")];
        Clients["yule-server"] = new Client("yule-server") { Password = "server-pw" };
        Clients["yule-server"].Roles.Add("yule-server");
        Roles["yule-server"] = [new AclEntry(AclTypes.PublishClientSend, "yule/v1/devices/+/config")];
    }

    public IEnumerable<string> DeviceClients => Clients.Keys.Where(BrokerProjection.IsManagedClient).Order(StringComparer.Ordinal);

    public IEnumerable<string> DeviceRoles => Roles.Keys.Where(BrokerProjection.IsManagedRole).Order(StringComparer.Ordinal);

    public void Attach(FakeBrokerConnection connection)
    {
        connection.OnPublish = message =>
        {
            if (message.Topic == DynsecCommands.ControlTopic)
            {
                RequestSizes.Add(Encoding.UTF8.GetByteCount(message.Payload));
                connection.Receive(new BrokerMessage(DynsecCommands.ResponseTopic, Handle(message.Payload), false));
            }

            return Task.CompletedTask;
        };
    }

    /// <summary>Removes every device account and role, as if the broker's dynamic-security file had been lost.</summary>
    public void LoseDeviceState()
    {
        foreach (var name in DeviceClients.ToList())
        {
            Clients.Remove(name);
        }

        foreach (var name in DeviceRoles.ToList())
        {
            Roles.Remove(name);
        }
    }

    private string Handle(string requestJson)
    {
        var responses = new JsonArray();
        foreach (var command in JsonNode.Parse(requestJson)!["commands"]!.AsArray())
        {
            responses.Add(Execute((JsonObject)command!));
        }

        return new JsonObject { ["responses"] = responses }.ToJsonString();
    }

    private JsonObject Execute(JsonObject c)
    {
        var name = c["command"]!.GetValue<string>();
        var response = new JsonObject { ["command"] = name };
        try
        {
            response["data"] = Run(name, c);
        }
        catch (FakeError error)
        {
            response["error"] = error.Message;
        }

        if (response["data"] is null)
        {
            response.Remove("data");
        }

        response["correlationData"] = c["correlationData"]!.GetValue<string>();
        return response;
    }

    private JsonNode? Run(string name, JsonObject c)
    {
        string Str(string key) => c[key]!.GetValue<string>();

        switch (name)
        {
            case "listClients":
                return Page(Clients.Keys, "clients", c);
            case "listRoles":
                return Page(Roles.Keys, "roles", c);
            case "getClient":
            {
                var client = Clients.GetValueOrDefault(Str("username")) ?? throw new FakeError("Client not found");
                var json = new JsonObject { ["username"] = client.Username };
                if (client.ClientId is not null)
                {
                    json["clientid"] = client.ClientId;
                }

                json["roles"] = new JsonArray(client.Roles.Select(r => (JsonNode)new JsonObject { ["rolename"] = r, ["priority"] = 1 }).ToArray());
                json["groups"] = new JsonArray();
                return new JsonObject { ["client"] = json };
            }

            case "getRole":
            {
                var acls = Roles.GetValueOrDefault(Str("rolename")) ?? throw new FakeError("Role not found");
                return new JsonObject
                {
                    ["role"] = new JsonObject
                    {
                        ["rolename"] = Str("rolename"),
                        ["acls"] = new JsonArray(acls.Select(a => (JsonNode)new JsonObject
                        {
                            ["acltype"] = a.AclType, ["topic"] = a.Topic, ["priority"] = a.Priority, ["allow"] = a.Allow,
                        }).ToArray()),
                    },
                };
            }

            case "createRole":
                if (!Roles.TryAdd(Str("rolename"), [])) throw new FakeError("Role already exists");
                Changes.Add("createRole " + Str("rolename"));
                return null;
            case "deleteRole":
                if (!Roles.Remove(Str("rolename"))) throw new FakeError("Role not found");
                foreach (var client in Clients.Values) client.Roles.Remove(Str("rolename"));
                Changes.Add("deleteRole " + Str("rolename"));
                return null;
            case "addRoleACL":
            {
                var acls = Roles.GetValueOrDefault(Str("rolename")) ?? throw new FakeError("Role not found");
                if (acls.Any(a => a.AclType == Str("acltype") && a.Topic == Str("topic"))) throw new FakeError("ACL with this topic already exists");
                acls.Add(new AclEntry(Str("acltype"), Str("topic"), c["allow"]?.GetValue<bool>() ?? true, c["priority"]?.GetValue<int>() ?? 0));
                Changes.Add($"addRoleACL {Str("rolename")} {Str("acltype")} {Str("topic")}");
                return null;
            }

            case "removeRoleACL":
            {
                var acls = Roles.GetValueOrDefault(Str("rolename")) ?? throw new FakeError("Role not found");
                if (acls.RemoveAll(a => a.AclType == Str("acltype") && a.Topic == Str("topic")) == 0) throw new FakeError("ACL not found");
                Changes.Add($"removeRoleACL {Str("rolename")} {Str("acltype")} {Str("topic")}");
                return null;
            }

            case "createClient":
            {
                if (Clients.ContainsKey(Str("username"))) throw new FakeError("Client already exists");

                // Real Mosquitto 2.0.22 corrupts memory when a role is given this way (see DynsecCommands.CreateClient).
                if (c["roles"] is not null) throw new FakeError("FAKE: createClient must not carry inline roles; use addClientRole");
                var client = new Client(Str("username")) { ClientId = c["clientid"]?.GetValue<string>(), Password = c["password"]?.GetValue<string>() };
                Clients[client.Username] = client;
                Changes.Add("createClient " + client.Username);
                return null;
            }

            case "addClientRole":
            {
                var client = Clients.GetValueOrDefault(Str("username")) ?? throw new FakeError("Client not found");
                if (!Roles.ContainsKey(Str("rolename"))) throw new FakeError("Role not found");
                if (client.Roles.Contains(Str("rolename"))) throw new FakeError("Client is already in this role");
                client.Roles.Add(Str("rolename"));
                Changes.Add($"addClientRole {client.Username} {Str("rolename")}");
                return null;
            }

            case "removeClientRole":
            {
                var client = Clients.GetValueOrDefault(Str("username")) ?? throw new FakeError("Client not found");
                if (!client.Roles.Remove(Str("rolename"))) throw new FakeError("Client is not in this role");
                Changes.Add($"removeClientRole {client.Username} {Str("rolename")}");
                return null;
            }

            case "setClientId":
            {
                var client = Clients.GetValueOrDefault(Str("username")) ?? throw new FakeError("Client not found");
                client.ClientId = Str("clientid");
                Changes.Add($"setClientId {client.Username} {client.ClientId}");
                return null;
            }

            case "deleteClient":
                if (!Clients.Remove(Str("username"))) throw new FakeError("Client not found");
                Changes.Add("deleteClient " + Str("username"));
                return null;
            default:
                throw new FakeError("Unknown command");
        }
    }

    private static JsonNode Page(IEnumerable<string> names, string arrayName, JsonObject c)
    {
        var all = names.Order(StringComparer.Ordinal).ToList();
        var count = c["count"]?.GetValue<int>() ?? -1;
        var offset = c["offset"]?.GetValue<int>() ?? 0;
        var page = all.Skip(offset).Take(count < 0 ? int.MaxValue : count);
        return new JsonObject
        {
            ["totalCount"] = all.Count,
            [arrayName] = new JsonArray(page.Select(n => (JsonNode)n).ToArray()),
        };
    }

    private sealed class FakeError(string message) : Exception(message);
}
