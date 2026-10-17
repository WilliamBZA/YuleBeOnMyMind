using System.Diagnostics;
using System.Text.Json.Nodes;
using MQTTnet.Formatter;
using Yule.Server.Broker;

namespace Yule.Server.IntegrationTests;

/// <summary>
/// Group isolation, live membership changes and self-repair, against a real Mosquitto with dynamic security.
/// Every test uses its own device and group ids: persistent sessions are keyed by client id and outlive a test.
/// </summary>
public class BrokerSyncIntegrationTests(MosquittoFixture broker) : IClassFixture<MosquittoFixture>, IAsyncLifetime
{
    private static int _next;

    private BrokerHarness _server = null!;
    private readonly List<TestDevice> _devices = [];

    public async Task InitializeAsync() => _server = await BrokerHarness.StartAsync(broker);

    public async Task DisposeAsync()
    {
        foreach (var device in _devices)
        {
            await device.DisposeAsync();
        }

        await _server.DisposeAsync();
    }

    private static string NewDeviceId() => $"d-{Interlocked.Increment(ref _next):x12}";

    private async Task<string> NewGroupAsync(string name)
    {
        var id = $"g-{Interlocked.Increment(ref _next):x8}";
        await _server.World.AddGroupAsync(id, name);
        return id;
    }

    /// <summary>Registers a device the way the Devices page will, and applies it to the broker.</summary>
    private async Task<(string Id, string Password)> RegisterAsync(string channel = "stable", params string[] groups)
    {
        var id = NewDeviceId();
        var password = await _server.World.AddDeviceAsync(id, channel, groups);
        await _server.Sync.ApplyDevicesAsync([id]);
        return (id, password);
    }

    /// <summary>Connects like the firmware does and subscribes to its config topic and to the press topic of each group given.</summary>
    private async Task<TestDevice> ConnectAsync(
        string id, string password, IEnumerable<string> subscribeToGroups, MqttProtocolVersion version = MqttProtocolVersion.V311, bool autoReconnect = false)
    {
        var groups = subscribeToGroups.ToList();
        var device = await TestDevice.ConnectAsync(broker, id, password, version, autoReconnect: autoReconnect, onConnected: async d =>
        {
            await d.TrySubscribeAsync(_server.ConfigTopic(id));
            foreach (var group in groups)
            {
                await d.TrySubscribeAsync(_server.PressFilter(group));
            }
        });
        _devices.Add(device);
        return device;
    }

    private static string Press(string who) => $$"""{"v":1,"id":"{{Guid.NewGuid():N}}","from":"{{who}}","name":"{{who}}"}""";

    [Fact]
    public async Task A_device_in_one_group_cannot_subscribe_to_or_publish_into_another()
    {
        var g1 = await NewGroupAsync("Brander Grandparents");
        var g2 = await NewGroupAsync("Smith Grandparents");
        var gran = await RegisterAsync("stable", g1);
        var mum = await RegisterAsync("stable", g2);
        var emma = await RegisterAsync("stable", g1, g2);
        await using var granDevice = await TestDevice.ConnectAsync(broker, gran.Id, gran.Password, MqttProtocolVersion.V500);
        var mumDevice = await ConnectAsync(mum.Id, mum.Password, [g2]);
        var emmaDevice = await ConnectAsync(emma.Id, emma.Password, [g1, g2]);

        // Listening: only its own group, its own config, never another device's status or config, never a wildcard over the lot.
        Assert.True(await granDevice.TrySubscribeAsync(_server.PressFilter(g1)));
        Assert.True(await granDevice.TrySubscribeAsync(_server.ConfigTopic(gran.Id)));
        Assert.False(await granDevice.TrySubscribeAsync(_server.PressFilter(g2)));
        Assert.False(await granDevice.TrySubscribeAsync($"{_server.Root}/groups/+/press/+"));
        Assert.False(await granDevice.TrySubscribeAsync($"{_server.Root}/groups/{g2}/press/{mum.Id}"));
        Assert.False(await granDevice.TrySubscribeAsync($"{_server.Root}/#"));
        Assert.False(await granDevice.TrySubscribeAsync("#"));
        Assert.False(await granDevice.TrySubscribeAsync(_server.ConfigTopic(emma.Id)));
        Assert.False(await granDevice.TrySubscribeAsync(_server.StatusTopic(emma.Id)));
        Assert.False(await granDevice.TrySubscribeAsync($"{_server.Root}/devices/+/status"));

        // Speaking: only as itself, only into its own groups.
        Assert.False(await granDevice.PublishAsync(_server.PressTopic(g2, gran.Id), Press(gran.Id)), "into a group it is not in");
        Assert.False(await granDevice.PublishAsync(_server.PressTopic(g1, emma.Id), Press(emma.Id)), "as another device");
        Assert.False(await granDevice.PublishAsync(_server.PressTopic(g1, "d-ffffffffffff"), Press("d-ffffffffffff")), "as a device that does not exist");
        Assert.False(await granDevice.PublishAsync(_server.ConfigTopic(gran.Id), "{\"v\":1,\"groups\":[],\"channel\":\"stable\"}"), "writing its own config");
        Assert.False(await granDevice.PublishAsync($"{_server.Root}/firmware/stable/esp32dev", "{}"), "writing a firmware manifest");
        Assert.True(await granDevice.PublishAsync(_server.PressTopic(g1, gran.Id), Press(gran.Id)));

        // Only the allowed press arrived, and only where it should.
        Assert.NotNull(await emmaDevice.WaitForAsync(_server.PressTopic(g1, gran.Id)));
        await Task.Delay(300);
        Assert.DoesNotContain(mumDevice.Received, m => m.Topic.Contains("/press/", StringComparison.Ordinal));
        Assert.Single(emmaDevice.Received, m => m.Topic.Contains("/press/", StringComparison.Ordinal));
    }

    [Fact]
    public async Task A_refused_publish_from_a_3_1_1_device_is_still_acknowledged_but_never_delivered()
    {
        // Worth knowing for the sender confirmation (device-controls): MQTT 3.1.1 has no way to say "refused", so the
        // broker acknowledges a press it dropped for permission reasons. Only MQTT 5 tells the sender.
        var g1 = await NewGroupAsync("Brander Grandparents");
        var g2 = await NewGroupAsync("Smith Grandparents");
        var gran = await RegisterAsync("stable", g1);
        var mum = await RegisterAsync("stable", g2);
        var granDevice = await ConnectAsync(gran.Id, gran.Password, [g1]);
        var mumDevice = await ConnectAsync(mum.Id, mum.Password, [g2]);

        Assert.True(await granDevice.PublishAsync(_server.PressTopic(g2, gran.Id), Press(gran.Id)));
        Assert.True(await mumDevice.PublishAsync(_server.PressTopic(g2, mum.Id), Press(mum.Id))); // the control

        Assert.NotNull(await mumDevice.WaitForAsync(_server.PressTopic(g2, mum.Id)));
        await Task.Delay(300);
        Assert.Null(await mumDevice.WaitForAsync(_server.PressTopic(g2, gran.Id), TimeSpan.FromMilliseconds(100)));
    }

    [Fact]
    public async Task A_device_in_two_groups_receives_both()
    {
        var g1 = await NewGroupAsync("Brander Grandparents");
        var g2 = await NewGroupAsync("Smith Grandparents");
        var gran = await RegisterAsync("stable", g1);
        var mum = await RegisterAsync("stable", g2);
        var emma = await RegisterAsync("stable", g1, g2);
        var granDevice = await ConnectAsync(gran.Id, gran.Password, [g1]);
        var mumDevice = await ConnectAsync(mum.Id, mum.Password, [g2]);
        var emmaDevice = await ConnectAsync(emma.Id, emma.Password, [g1, g2]);

        await granDevice.PublishAsync(_server.PressTopic(g1, gran.Id), Press(gran.Id));
        await mumDevice.PublishAsync(_server.PressTopic(g2, mum.Id), Press(mum.Id));

        Assert.NotNull(await emmaDevice.WaitForAsync(_server.PressTopic(g1, gran.Id)));
        Assert.NotNull(await emmaDevice.WaitForAsync(_server.PressTopic(g2, mum.Id)));
        Assert.Null(await granDevice.WaitForAsync(_server.PressTopic(g2, mum.Id), TimeSpan.FromMilliseconds(500)));
    }

    [Fact]
    public async Task A_device_that_connects_later_is_given_its_retained_config()
    {
        var g1 = await NewGroupAsync("Brander Grandparents");
        var g2 = await NewGroupAsync("Smith Grandparents");
        var emma = await RegisterAsync("beta", g1, g2);

        var device = await ConnectAsync(emma.Id, emma.Password, []);

        var config = JsonNode.Parse((await device.WaitForAsync(_server.ConfigTopic(emma.Id)))!)!;
        Assert.Equal("beta", config["channel"]!.GetValue<string>());
        Assert.Equal(
            new[] { "Brander Grandparents", "Smith Grandparents" },
            config["groups"]!.AsArray().Select(g => g!["name"]!.GetValue<string>()));
        Assert.Contains(g1, config["groups"]!.AsArray().Select(g => g!["id"]!.GetValue<string>()));
    }

    [Fact]
    public async Task Adding_a_member_gives_access_and_pushes_the_new_config_within_ten_seconds()
    {
        var g1 = await NewGroupAsync("Brander Grandparents");
        var g2 = await NewGroupAsync("Smith Grandparents");
        var mum = await RegisterAsync("stable", g2);
        var emma = await RegisterAsync("stable", g1);
        var mumDevice = await ConnectAsync(mum.Id, mum.Password, [g2]);
        var emmaDevice = await ConnectAsync(emma.Id, emma.Password, [g1], autoReconnect: true);
        Assert.NotNull(await emmaDevice.WaitForAsync(_server.ConfigTopic(emma.Id)));
        Assert.False(await emmaDevice.TrySubscribeAsync(_server.PressFilter(g2)));
        var timer = Stopwatch.StartNew();

        await _server.World.SetGroupsAsync(emma.Id, g1, g2);
        await _server.Sync.ApplyDevicesAsync([emma.Id]);

        // The broker drops the device, which comes back (as the firmware does) and is handed the new config by the
        // broker straight away. It learns of the new group from that, then uses it, with nobody touching it.
        var updated = await emmaDevice.WaitForAsync(_server.ConfigTopic(emma.Id), TimeSpan.FromSeconds(9), skip: 1);
        Assert.NotNull(updated);
        Assert.Equal(2, JsonNode.Parse(updated)!["groups"]!.AsArray().Count);
        Assert.True(await emmaDevice.TrySubscribeAsync(_server.PressFilter(g2)));
        await mumDevice.PublishAsync(_server.PressTopic(g2, mum.Id), Press(mum.Id));
        Assert.NotNull(await emmaDevice.WaitForAsync(_server.PressTopic(g2, mum.Id)));
        Assert.True(timer.Elapsed < TimeSpan.FromSeconds(10), $"took {timer.Elapsed.TotalSeconds:0.0} s");
    }

    [Fact]
    public async Task Removing_membership_stops_delivery_within_ten_seconds_even_with_a_persistent_session()
    {
        var g1 = await NewGroupAsync("Brander Grandparents");
        var g2 = await NewGroupAsync("Smith Grandparents");
        var gran = await RegisterAsync("stable", g1);
        var mum = await RegisterAsync("stable", g2);
        var emma = await RegisterAsync("stable", g1, g2);
        var granDevice = await ConnectAsync(gran.Id, gran.Password, [g1]);
        var mumDevice = await ConnectAsync(mum.Id, mum.Password, [g2]);

        // Emma's device subscribes to both groups, and when it reconnects it re-subscribes only to what it still has,
        // as the firmware does. The broker's persistent session still holds her subscription to the group she left.
        var wanted = new List<string> { g1, g2 };
        var emmaDevice = await TestDevice.ConnectAsync(broker, emma.Id, emma.Password, autoReconnect: true, onConnected: async d =>
        {
            await d.TrySubscribeAsync(_server.ConfigTopic(emma.Id));
            foreach (var group in wanted.ToList())
            {
                await d.TrySubscribeAsync(_server.PressFilter(group));
            }
        });
        _devices.Add(emmaDevice);
        await mumDevice.PublishAsync(_server.PressTopic(g2, mum.Id), Press(mum.Id));
        Assert.NotNull(await emmaDevice.WaitForAsync(_server.PressTopic(g2, mum.Id)));
        var delivered = emmaDevice.CountOn(_server.PressTopic(g2, mum.Id));
        var timer = Stopwatch.StartNew();

        wanted.Remove(g2);
        await _server.World.SetGroupsAsync(emma.Id, g1);
        await _server.Sync.ApplyDevicesAsync([emma.Id]);
        await mumDevice.PublishAsync(_server.PressTopic(g2, mum.Id), Press(mum.Id)); // while she is being dropped
        await Eventually.TrueAsync(() => emmaDevice.Disconnects >= 1, "the broker to disconnect the device that lost a group");
        await Eventually.TrueAsync(() => emmaDevice.IsConnected, "the device to reconnect");
        await mumDevice.PublishAsync(_server.PressTopic(g2, mum.Id), Press(mum.Id)); // after she is back

        // A press in her remaining group is the control: once it has arrived, the others had every chance to.
        await granDevice.PublishAsync(_server.PressTopic(g1, gran.Id), Press(gran.Id));
        Assert.NotNull(await emmaDevice.WaitForAsync(_server.PressTopic(g1, gran.Id), TimeSpan.FromSeconds(9)));
        await Task.Delay(500);

        Assert.True(timer.Elapsed < TimeSpan.FromSeconds(10), $"took {timer.Elapsed.TotalSeconds:0.0} s");
        Assert.Equal(delivered, emmaDevice.CountOn(_server.PressTopic(g2, mum.Id)));
        var config = JsonNode.Parse(emmaDevice.Received.Last(m => m.Topic == _server.ConfigTopic(emma.Id)).Payload)!;
        Assert.Equal([g1], config["groups"]!.AsArray().Select(g => g!["id"]!.GetValue<string>()));

        // A tidy device unsubscribes from the group it left, which the broker still allows.
        Assert.True(await emmaDevice.TryUnsubscribeAsync(_server.PressFilter(g2)));
    }

    [Fact]
    public async Task A_removed_device_is_disconnected_and_refused()
    {
        var g1 = await NewGroupAsync("Brander Grandparents");
        var gran = await RegisterAsync("stable", g1);
        var emma = await RegisterAsync("stable", g1);
        var granDevice = await ConnectAsync(gran.Id, gran.Password, [g1]);
        var emmaDevice = await ConnectAsync(emma.Id, emma.Password, [g1]);
        await _server.World.RemoveDeviceAsync(emma.Id);

        var result = await _server.Sync.ApplyDevicesAsync([emma.Id]);

        Assert.Equal([emma.Id], result.RemovedDevices);
        await Eventually.TrueAsync(() => !emmaDevice.IsConnected, "the removed device to be disconnected");
        await Assert.ThrowsAsync<ConnectionRefusedException>(() => TestDevice.ConnectAsync(broker, emma.Id, emma.Password));
        Assert.True(await granDevice.PublishAsync(_server.PressTopic(g1, gran.Id), Press(gran.Id)));
        Assert.Equal(0, granDevice.Disconnects);
    }

    [Fact]
    public async Task Deleting_a_role_by_hand_is_restored_by_the_reconciler_with_the_same_password()
    {
        var g1 = await NewGroupAsync("Brander Grandparents");
        var emma = await RegisterAsync("stable", g1);
        await _server.Dynsec.ExecuteAsync([DynsecCommands.DeleteRole(BrokerProjection.RoleName(emma.Id))], default);
        await using (var broken = await TestDevice.ConnectAsync(broker, emma.Id, emma.Password))
        {
            Assert.False(await broken.TrySubscribeAsync(_server.PressFilter(g1)), "with its role gone it may do nothing");
        }

        var result = await _server.Sync.ReconcileAsync(republishAllConfigs: false);

        Assert.Equal([emma.Id], result.ChangedDevices);
        await using var restored = await TestDevice.ConnectAsync(broker, emma.Id, emma.Password);
        Assert.True(await restored.TrySubscribeAsync(_server.PressFilter(g1)));
        Assert.True(await restored.TrySubscribeAsync(_server.ConfigTopic(emma.Id)));
    }

    [Fact]
    public async Task Losing_all_dynamic_security_state_is_restored_by_the_reconciler_with_the_same_passwords()
    {
        var g1 = await NewGroupAsync("Brander Grandparents");
        var g2 = await NewGroupAsync("Smith Grandparents");
        var gran = await RegisterAsync("stable", g1);
        var emma = await RegisterAsync("beta", g1, g2);

        // As if the dynamic-security file were lost and recreated (broker/README.md): the operator's and the server's own
        // accounts are back, but not one device account or role.
        var state = await _server.Dynsec.ReadStateAsync(default);
        await _server.Dynsec.ExecuteAsync(
            [.. state.Clients.Select(c => DynsecCommands.DeleteClient(c.Username)), .. state.Roles.Keys.Select(DynsecCommands.DeleteRole)], default);
        Assert.Empty((await _server.Dynsec.ReadStateAsync(default)).Clients);

        var result = await _server.Sync.ReconcileAsync(republishAllConfigs: true);

        Assert.Equal([gran.Id, emma.Id], result.ChangedDevices.Order());
        var granDevice = await ConnectAsync(gran.Id, gran.Password, [g1]);
        var emmaDevice = await ConnectAsync(emma.Id, emma.Password, [g1, g2]);
        await granDevice.PublishAsync(_server.PressTopic(g1, gran.Id), Press(gran.Id));
        Assert.NotNull(await emmaDevice.WaitForAsync(_server.PressTopic(g1, gran.Id)));
        Assert.Contains("\"channel\":\"beta\"", await emmaDevice.WaitForAsync(_server.ConfigTopic(emma.Id)));

        // And now that it matches, checking again changes nothing and disturbs nobody.
        var again = await _server.Sync.ReconcileAsync(republishAllConfigs: false);
        await Task.Delay(1000);
        Assert.Equal(0, again.CommandsSent);
        Assert.Equal(0, granDevice.Disconnects + emmaDevice.Disconnects);
    }

    [Fact]
    public async Task A_reconcile_of_a_correct_broker_does_not_disturb_connected_devices()
    {
        var g1 = await NewGroupAsync("Brander Grandparents");
        var gran = await RegisterAsync("stable", g1);
        var emma = await RegisterAsync("stable", g1);
        var granDevice = await ConnectAsync(gran.Id, gran.Password, [g1]);
        var emmaDevice = await ConnectAsync(emma.Id, emma.Password, [g1]);

        var first = await _server.Sync.ReconcileAsync(republishAllConfigs: false);
        var second = await _server.Sync.ReconcileAsync(republishAllConfigs: true);
        await Task.Delay(1000);

        Assert.Equal(0, first.CommandsSent);
        Assert.Equal(0, second.CommandsSent);
        Assert.Equal(0, granDevice.Disconnects + emmaDevice.Disconnects);
        Assert.True(await granDevice.PublishAsync(_server.PressTopic(g1, gran.Id), Press(gran.Id)));
        Assert.NotNull(await emmaDevice.WaitForAsync(_server.PressTopic(g1, gran.Id)));
    }

    [Fact]
    public async Task With_the_server_stopped_devices_keep_exchanging_presses_and_new_connections_still_work()
    {
        var g1 = await NewGroupAsync("Brander Grandparents");
        var gran = await RegisterAsync("stable", g1);
        var emma = await RegisterAsync("stable", g1);
        var mum = await RegisterAsync("stable", g1);
        var granDevice = await ConnectAsync(gran.Id, gran.Password, [g1]);
        var emmaDevice = await ConnectAsync(emma.Id, emma.Password, [g1]);

        await _server.StopServerAsync();
        Assert.False(_server.Connection.IsConnected);

        await granDevice.PublishAsync(_server.PressTopic(g1, gran.Id), Press(gran.Id));
        Assert.NotNull(await emmaDevice.WaitForAsync(_server.PressTopic(g1, gran.Id)));
        var latecomer = await ConnectAsync(mum.Id, mum.Password, [g1]);
        await granDevice.PublishAsync(_server.PressTopic(g1, gran.Id), Press(gran.Id));
        Assert.NotNull(await latecomer.WaitForAsync(_server.PressTopic(g1, gran.Id)));
        Assert.Equal(0, granDevice.Disconnects + emmaDevice.Disconnects);
    }

    [Fact]
    public async Task A_device_in_the_maximum_32_groups_is_fully_configured()
    {
        var groups = new List<string>();
        for (var i = 0; i < 32; i++)
        {
            groups.Add(await NewGroupAsync($"Family group number {i:00} with a longer name"));
        }

        var emma = await RegisterAsync("stable", [.. groups]);
        var device = await ConnectAsync(emma.Id, emma.Password, []);

        var payload = (await device.WaitForAsync(_server.ConfigTopic(emma.Id)))!;
        Assert.Equal(32, JsonNode.Parse(payload)!["groups"]!.AsArray().Count);
        Assert.InRange(payload.Length, 1000, 4096);
        Assert.True(await device.TrySubscribeAsync(_server.PressFilter(groups[0])));
        Assert.True(await device.TrySubscribeAsync(_server.PressFilter(groups[31])));
        Assert.True(await device.PublishAsync(_server.PressTopic(groups[31], emma.Id), Press(emma.Id)));
    }
}
