using System.Text.Json.Nodes;
using Yule.Server.Broker;

namespace Yule.Server.Tests;

public class DynsecClientTests
{
    private const string Root = "yule/v1";

    private static (DynsecClient Client, FakeBrokerConnection Connection, FakeDynsecBroker Broker) Create(TimeSpan? timeout = null)
    {
        var connection = new FakeBrokerConnection();
        var broker = new FakeDynsecBroker();
        broker.Attach(connection);
        return (new DynsecClient(connection) { RequestTimeout = timeout ?? TimeSpan.FromSeconds(5) }, connection, broker);
    }

    private static DesiredRole RoleFor(string deviceId, params string[] groupIds) =>
        BrokerProjection.Project([new DeviceModel(deviceId, "x", "stable", "pw", groupIds.Select(g => new GroupInfo(g, g)).ToList())], Root).Roles.Single();

    [Fact]
    public async Task It_subscribes_to_the_response_topic_before_sending()
    {
        var (client, connection, _) = Create();

        await client.ExecuteAsync([DynsecCommands.ListClients(10, 0)], CancellationToken.None);

        Assert.Contains(DynsecCommands.ResponseTopic, connection.Subscriptions);
    }

    [Fact]
    public async Task Each_reply_goes_to_the_request_it_answers_even_when_answered_in_reverse_order()
    {
        var connection = new FakeBrokerConnection();
        var requests = new List<JsonObject>();
        connection.OnPublish = message =>
        {
            requests.Add((JsonObject)JsonNode.Parse(message.Payload)!);
            if (requests.Count == 2)
            {
                foreach (var request in Enumerable.Reverse(requests))
                {
                    var command = request["commands"]![0]!;
                    var reply = new JsonObject
                    {
                        ["responses"] = new JsonArray(new JsonObject
                        {
                            ["command"] = "getClient",
                            ["data"] = new JsonObject { ["echo"] = command["username"]!.GetValue<string>() },
                            ["correlationData"] = command["correlationData"]!.GetValue<string>(),
                        }),
                    };
                    connection.Receive(new BrokerMessage(DynsecCommands.ResponseTopic, reply.ToJsonString(), false));
                }
            }

            return Task.CompletedTask;
        };
        var client = new DynsecClient(connection);

        var first = client.SendAsync([DynsecCommands.GetClient("first")], CancellationToken.None);
        var second = client.SendAsync([DynsecCommands.GetClient("second")], CancellationToken.None);

        Assert.Equal("first", (await first)[0].Data!["echo"]!.GetValue<string>());
        Assert.Equal("second", (await second)[0].Data!["echo"]!.GetValue<string>());
    }

    [Fact]
    public async Task A_command_the_broker_rejects_is_reported_by_send_and_thrown_by_execute()
    {
        var (client, _, _) = Create();
        var bad = DynsecCommands.GetRole("device-d-ffffffffffff");

        var response = Assert.Single(await client.SendAsync([bad], CancellationToken.None));
        Assert.Equal("Role not found", response.Error);

        var thrown = await Assert.ThrowsAsync<DynsecException>(() => client.ExecuteAsync([DynsecCommands.ListClients(10, 0), bad], CancellationToken.None));
        Assert.Contains("getRole", thrown.Message);
        Assert.Contains("Role not found", thrown.Message);
        Assert.Contains("device-d-ffffffffffff", thrown.Message);
    }

    [Fact]
    public async Task A_batch_with_one_failure_still_runs_the_other_commands_and_returns_every_response_in_order()
    {
        var (client, _, broker) = Create();

        var responses = await client.SendAsync(
            [DynsecCommands.CreateRole("device-d-000000000001"), DynsecCommands.GetRole("nope"), DynsecCommands.CreateRole("device-d-000000000002")],
            CancellationToken.None);

        Assert.Equal([null, "Role not found", null], responses.Select(r => r.Error));
        Assert.Contains("device-d-000000000002", broker.Roles.Keys);
    }

    [Fact]
    public async Task Long_command_lists_are_split_so_no_request_reaches_the_brokers_packet_limit()
    {
        var (client, _, broker) = Create();
        var groups = Enumerable.Range(0, 32).Select(i => $"g-{i:x8}").ToArray();
        var role = RoleFor("d-000000000001", groups);

        var created = await client.EnsureRoleAsync(role, CancellationToken.None);

        Assert.True(created);
        Assert.Equal(7 + 32 * 3, role.Acls.Count);
        Assert.Equal(role.Acls.Count, broker.Roles["device-d-000000000001"].Count);
        Assert.True(broker.RequestSizes.Count > 1, "expected the commands to be split across requests");
        Assert.All(broker.RequestSizes, size => Assert.True(size < 4096, $"request of {size} bytes"));
    }

    [Fact]
    public async Task No_answer_is_a_timeout_that_names_the_command()
    {
        var connection = new FakeBrokerConnection();
        var client = new DynsecClient(connection) { RequestTimeout = TimeSpan.FromMilliseconds(150) };

        var thrown = await Assert.ThrowsAsync<TimeoutException>(() => client.SendAsync([DynsecCommands.ListClients(10, 0)], CancellationToken.None));

        Assert.Contains("listClients", thrown.Message);
    }

    [Fact]
    public async Task Unrelated_and_unmatched_messages_are_ignored()
    {
        var (client, connection, _) = Create();

        connection.Receive(new BrokerMessage("yule/v1/devices/d-000000000001/status", "{}", true));
        connection.Receive(new BrokerMessage(DynsecCommands.ResponseTopic, "not json", false));
        connection.Receive(new BrokerMessage(DynsecCommands.ResponseTopic, """{"responses":[{"command":"x","correlationData":"y999999"}]}""", false));
        connection.Receive(new BrokerMessage(DynsecCommands.ResponseTopic, """{"nothing":true}""", false));

        Assert.Single(await client.SendAsync([DynsecCommands.ListClients(10, 0)], CancellationToken.None));
    }

    [Fact]
    public async Task Reading_the_state_returns_every_device_across_pages_and_ignores_other_accounts()
    {
        var (client, _, broker) = Create();
        const int devices = 250; // more than two pages of 100
        for (var i = 1; i <= devices; i++)
        {
            var id = $"d-{i:x12}";
            var role = RoleFor(id, "g-aaaaaaaa");
            await client.EnsureRoleAsync(role, CancellationToken.None);
            await client.EnsureClientAsync(new DesiredClient(id, id, "pw", role.Name), CancellationToken.None);
        }

        var state = await client.ReadStateAsync(CancellationToken.None);

        Assert.Equal(devices, state.Clients.Count);
        Assert.Equal(devices, state.Roles.Count);
        Assert.DoesNotContain(state.Clients, c => c.Username is "admin" or "yule-server");
        Assert.DoesNotContain(state.Roles.Keys, r => r is "admin" or "yule-server");
        var one = state.Clients.Single(c => c.Username == "d-0000000000fa");
        Assert.Equal("d-0000000000fa", one.ClientId);
        Assert.Equal(["device-d-0000000000fa"], one.Roles);
        Assert.Equal(10, state.Roles["device-d-0000000000fa"].Count);
        Assert.Equal(devices, broker.DeviceClients.Count());
    }

    [Fact]
    public async Task Ensuring_a_role_and_a_client_twice_changes_nothing_the_second_time()
    {
        var (client, _, broker) = Create();
        var role = RoleFor("d-000000000001", "g-aaaaaaaa", "g-bbbbbbbb");
        var login = new DesiredClient("d-000000000001", "d-000000000001", "the-password", role.Name);

        Assert.True(await client.EnsureRoleAsync(role, CancellationToken.None));
        Assert.True(await client.EnsureClientAsync(login, CancellationToken.None));
        var changesAfterFirst = broker.Changes.Count;

        Assert.False(await client.EnsureRoleAsync(role, CancellationToken.None));
        Assert.False(await client.EnsureClientAsync(login, CancellationToken.None));

        Assert.Equal(changesAfterFirst, broker.Changes.Count);
        Assert.Equal("the-password", broker.Clients["d-000000000001"].Password);
    }

    [Fact]
    public async Task Ensuring_a_changed_role_sends_only_the_difference()
    {
        var (client, _, broker) = Create();
        await client.EnsureRoleAsync(RoleFor("d-000000000001", "g-aaaaaaaa", "g-bbbbbbbb"), CancellationToken.None);
        broker.Changes.Clear();

        var changed = await client.EnsureRoleAsync(RoleFor("d-000000000001", "g-aaaaaaaa"), CancellationToken.None);

        Assert.True(changed);
        Assert.Equal(3, broker.Changes.Count);
        Assert.All(broker.Changes, c => Assert.StartsWith("removeRoleACL device-d-000000000001", c));
        Assert.All(broker.Changes, c => Assert.Contains("g-bbbbbbbb", c));
    }

    [Fact]
    public async Task Ensuring_a_client_whose_role_was_swapped_repairs_it_and_keeps_its_password()
    {
        var (client, _, broker) = Create();
        var role = RoleFor("d-000000000001", "g-aaaaaaaa");
        await client.EnsureRoleAsync(role, CancellationToken.None);
        await client.ExecuteAsync([DynsecCommands.CreateRole("other")], CancellationToken.None);
        var login = new DesiredClient("d-000000000001", "d-000000000001", "the-password", role.Name);
        await client.EnsureClientAsync(login, CancellationToken.None);
        broker.Clients["d-000000000001"].Roles.Clear();
        broker.Clients["d-000000000001"].Roles.Add("other");

        var changed = await client.EnsureClientAsync(login, CancellationToken.None);

        Assert.True(changed);
        Assert.Equal([role.Name], broker.Clients["d-000000000001"].Roles);
        Assert.Equal("the-password", broker.Clients["d-000000000001"].Password);
    }
}
