using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Yule.Server.Broker;
using Yule.Server.Configuration;

namespace Yule.Server.Tests;

public class BrokerSynchroniserTests : IAsyncLifetime
{
    private const string G1 = "g-aaaaaaaa";
    private const string G2 = "g-bbbbbbbb";
    private const string Gran = "d-000000000001";
    private const string Emma = "d-000000000002";

    private ServerWorld _world = null!;
    private readonly FakeBrokerConnection _connection = new();
    private readonly FakeDynsecBroker _broker = new();
    private BrokerSynchroniser _sync = null!;

    public async Task InitializeAsync()
    {
        _world = await ServerWorld.CreateAsync();
        _broker.Attach(_connection);
        _sync = new BrokerSynchroniser(
            _world.Services.GetRequiredService<IServiceScopeFactory>(),
            _world.Passwords,
            new DynsecClient(_connection),
            _connection,
            Options.Create(new YuleOptions { TopicRoot = "yule/v1" }),
            NullLogger<BrokerSynchroniser>.Instance);
        await _world.AddGroupAsync(G1, "Brander Grandparents");
        await _world.AddGroupAsync(G2, "Smith Grandparents");
    }

    public async Task DisposeAsync() => await _world.DisposeAsync();

    private List<PublishedMessage> Configs => _connection.Published.Where(p => p.Topic.EndsWith("/config", StringComparison.Ordinal)).ToList();

    private int IndexOfLastDynsecRequest() => _connection.Published.FindLastIndex(p => p.Topic == DynsecCommands.ControlTopic);

    private int IndexOfFirstConfig() => _connection.Published.FindIndex(p => p.Topic.EndsWith("/config", StringComparison.Ordinal));

    [Fact]
    public async Task Applying_a_new_device_creates_its_account_then_publishes_its_retained_config()
    {
        var password = await _world.AddDeviceAsync(Emma, "stable", G1, G2);

        var result = await _sync.ApplyDevicesAsync([Emma]);

        Assert.Equal([Emma], result.ChangedDevices);
        Assert.Equal(password, _broker.Clients[Emma].Password);
        Assert.Equal(Emma, _broker.Clients[Emma].ClientId);
        Assert.Equal(["device-" + Emma], _broker.Clients[Emma].Roles);
        Assert.Equal(7 + 2 * 3, _broker.Roles["device-" + Emma].Count);

        var config = Assert.Single(Configs);
        Assert.Equal($"yule/v1/devices/{Emma}/config", config.Topic);
        Assert.True(config.Retain);
        Assert.Equal(
            """{"v":1,"groups":[{"id":"g-aaaaaaaa","name":"Brander Grandparents"},{"id":"g-bbbbbbbb","name":"Smith Grandparents"}],"channel":"stable"}""",
            config.Payload);
        Assert.True(IndexOfFirstConfig() > IndexOfLastDynsecRequest(), "permissions must be in place before the device is told about its groups");
    }

    [Fact]
    public async Task Applying_the_same_state_again_changes_nothing_on_the_broker()
    {
        await _world.AddDeviceAsync(Emma, "stable", G1);
        await _sync.ApplyDevicesAsync([Emma]);
        var changes = _broker.Changes.Count;

        var again = await _sync.ApplyDevicesAsync([Emma]);

        Assert.Equal(0, again.CommandsSent);
        Assert.Empty(again.ChangedDevices);
        Assert.Equal(changes, _broker.Changes.Count);
        Assert.Equal(2, Configs.Count); // the config is republished: a rename changes only the config
    }

    [Fact]
    public async Task Leaving_a_group_removes_its_permissions_and_then_republishes_the_shorter_config()
    {
        await _world.AddDeviceAsync(Emma, "stable", G1, G2);
        await _sync.ApplyDevicesAsync([Emma]);
        _connection.Published.Clear();
        _broker.Changes.Clear();

        await _world.SetGroupsAsync(Emma, G1);
        var result = await _sync.ApplyDevicesAsync([Emma]);

        Assert.Equal(3, result.CommandsSent);
        Assert.All(_broker.Changes, c => Assert.StartsWith("removeRoleACL", c));
        Assert.All(_broker.Changes, c => Assert.Contains(G2, c));
        var config = Assert.Single(Configs);
        Assert.Equal("""{"v":1,"groups":[{"id":"g-aaaaaaaa","name":"Brander Grandparents"}],"channel":"stable"}""", config.Payload);
        Assert.True(IndexOfFirstConfig() > IndexOfLastDynsecRequest());
    }

    [Fact]
    public async Task Renaming_a_group_republishes_its_members_configs_without_touching_permissions()
    {
        await _world.AddDeviceAsync(Gran, "stable", G1);
        await _world.AddDeviceAsync(Emma, "stable", G1, G2);
        await _sync.ApplyDevicesAsync([Gran, Emma]);
        _connection.Published.Clear();
        _broker.Changes.Clear();

        await _world.RenameGroupAsync(G1, "Brander Family");
        await _sync.ApplyDevicesAsync([Gran, Emma]);

        Assert.Empty(_broker.Changes);
        Assert.Equal(2, Configs.Count);
        Assert.All(Configs, c => Assert.Contains("Brander Family", c.Payload));
    }

    [Fact]
    public async Task Deleting_a_group_removes_it_from_its_members()
    {
        await _world.AddDeviceAsync(Gran, "stable", G1);
        await _world.AddDeviceAsync(Emma, "stable", G1, G2);
        await _sync.ApplyDevicesAsync([Gran, Emma]);
        _broker.Changes.Clear();

        var members = await _world.RemoveGroupAsync(G1);
        await _sync.ApplyDevicesAsync(members);

        Assert.Equal([Gran, Emma], members.Order());
        Assert.Equal(6, _broker.Changes.Count);
        Assert.All(_broker.Changes, c => Assert.StartsWith("removeRoleACL", c));
        Assert.All(_broker.Changes, c => Assert.Contains(G1, c));
        Assert.DoesNotContain(_broker.Roles["device-" + Emma], a => a.Topic.Contains(G1, StringComparison.Ordinal));
        Assert.Contains(_broker.Roles["device-" + Emma], a => a.Topic.Contains(G2, StringComparison.Ordinal));
    }

    [Fact]
    public async Task Changing_the_channel_swaps_the_firmware_permission_and_republishes_the_config()
    {
        await _world.AddDeviceAsync(Emma, "stable", G1);
        await _sync.ApplyDevicesAsync([Emma]);
        _connection.Published.Clear();

        await _world.SetChannelAsync(Emma, "beta");
        await _sync.ApplyDevicesAsync([Emma]);

        Assert.Contains(_broker.Roles["device-" + Emma], a => a.Topic == "yule/v1/firmware/beta/+");
        Assert.DoesNotContain(_broker.Roles["device-" + Emma], a => a.Topic == "yule/v1/firmware/stable/+");
        Assert.Contains("\"channel\":\"beta\"", Assert.Single(Configs).Payload);
    }

    [Fact]
    public async Task Removing_a_device_deletes_its_account_and_clears_its_retained_config()
    {
        await _world.AddDeviceAsync(Emma, "stable", G1);
        await _sync.ApplyDevicesAsync([Emma]);
        _connection.Published.Clear();

        await _world.RemoveDeviceAsync(Emma);
        var result = await _sync.ApplyDevicesAsync([Emma]);

        Assert.Equal([Emma], result.RemovedDevices);
        Assert.DoesNotContain(Emma, _broker.Clients.Keys);
        Assert.DoesNotContain("device-" + Emma, _broker.Roles.Keys);
        var cleared = Assert.Single(Configs);
        Assert.Equal($"yule/v1/devices/{Emma}/config", cleared.Topic);
        Assert.Equal("", cleared.Payload);
        Assert.True(cleared.Retain);
    }

    [Fact]
    public async Task Applying_ids_that_are_not_device_ids_does_nothing()
    {
        var result = await _sync.ApplyDevicesAsync(["admin", "yule-server", "", "device-" + Emma]);

        Assert.Equal(0, result.CommandsSent);
        Assert.Empty(_connection.Published);
        Assert.Contains("admin", _broker.Clients.Keys);
    }

    [Fact]
    public async Task A_reconcile_of_a_broker_that_already_matches_sends_nothing()
    {
        await _world.AddDeviceAsync(Gran, "stable", G1);
        await _world.AddDeviceAsync(Emma, "stable", G1, G2);
        await _sync.ApplyDevicesAsync([Gran, Emma]);
        _connection.Published.Clear();
        _broker.Changes.Clear();

        var quiet = await _sync.ReconcileAsync(republishAllConfigs: false);

        Assert.Equal(0, quiet.CommandsSent);
        Assert.Equal(0, quiet.ConfigsPublished);
        Assert.Empty(_broker.Changes);
        Assert.Empty(Configs);

        var startup = await _sync.ReconcileAsync(republishAllConfigs: true);

        Assert.Equal(0, startup.CommandsSent);
        Assert.Equal(2, startup.ConfigsPublished);
        Assert.Empty(_broker.Changes);
    }

    [Fact]
    public async Task A_reconcile_restores_lost_broker_state_with_the_same_passwords()
    {
        var granPassword = await _world.AddDeviceAsync(Gran, "stable", G1);
        var emmaPassword = await _world.AddDeviceAsync(Emma, "beta", G1, G2);
        await _sync.ApplyDevicesAsync([Gran, Emma]);
        var emmaAcls = _broker.Roles["device-" + Emma].ToList();
        _broker.LoseDeviceState();

        var result = await _sync.ReconcileAsync(republishAllConfigs: false);

        Assert.Equal([Gran, Emma], result.ChangedDevices.Order());
        Assert.Equal(granPassword, _broker.Clients[Gran].Password);
        Assert.Equal(emmaPassword, _broker.Clients[Emma].Password);
        Assert.Equal(emmaAcls.ToHashSet(), _broker.Roles["device-" + Emma].ToHashSet());
        Assert.Equal(2, result.ConfigsPublished); // a device that needed repair gets its config again
    }

    [Fact]
    public async Task A_reconcile_repairs_a_role_deleted_by_hand()
    {
        await _world.AddDeviceAsync(Emma, "stable", G1);
        await _sync.ApplyDevicesAsync([Emma]);
        var before = _broker.Roles["device-" + Emma].ToHashSet();
        _broker.Roles.Remove("device-" + Emma);
        _broker.Clients[Emma].Roles.Clear();

        await _sync.ReconcileAsync(republishAllConfigs: false);

        Assert.Equal(before, _broker.Roles["device-" + Emma].ToHashSet());
        Assert.Equal(["device-" + Emma], _broker.Clients[Emma].Roles);
    }

    [Fact]
    public async Task A_reconcile_removes_accounts_the_database_no_longer_has_but_never_the_operators_or_the_servers()
    {
        await _world.AddDeviceAsync(Gran, "stable", G1);
        await _world.AddDeviceAsync(Emma, "stable", G1);
        await _sync.ApplyDevicesAsync([Gran, Emma]);
        await _world.RemoveDeviceAsync(Emma); // removed without telling the synchroniser
        _connection.Published.Clear();

        var result = await _sync.ReconcileAsync(republishAllConfigs: false);

        Assert.Equal([Emma], result.RemovedDevices);
        Assert.DoesNotContain(Emma, _broker.Clients.Keys);
        Assert.Contains(Gran, _broker.Clients.Keys);
        Assert.Contains("admin", _broker.Clients.Keys);
        Assert.Contains("yule-server", _broker.Clients.Keys);
        Assert.Contains("admin", _broker.Roles.Keys);
        Assert.Contains("yule-server", _broker.Roles.Keys);
        Assert.Equal("", Assert.Single(Configs).Payload);
    }

    [Fact]
    public async Task A_device_whose_password_cannot_be_decrypted_is_left_exactly_as_it_is()
    {
        await _world.AddDeviceAsync(Gran, "stable", G1);
        await _world.AddDeviceAsync(Emma, "stable", G1);
        await _sync.ApplyDevicesAsync([Gran, Emma]);
        await _world.CorruptPasswordAsync(Emma);
        var password = _broker.Clients[Emma].Password;
        _broker.Changes.Clear();

        var reconcile = await _sync.ReconcileAsync(republishAllConfigs: true);
        var apply = await _sync.ApplyDevicesAsync([Emma]);

        Assert.Equal(0, reconcile.CommandsSent);
        Assert.Equal(0, apply.CommandsSent);
        Assert.Empty(_broker.Changes);
        Assert.Equal(password, _broker.Clients[Emma].Password);
        Assert.Contains("device-" + Emma, _broker.Roles.Keys);
        Assert.DoesNotContain(Configs, c => c.Topic.Contains(Emma, StringComparison.Ordinal) && c.Payload == "");
    }

    [Fact]
    public async Task Several_syncs_at_once_are_run_one_at_a_time()
    {
        for (var i = 1; i <= 6; i++)
        {
            await _world.AddDeviceAsync($"d-{i:x12}", "stable", G1);
        }

        var ids = Enumerable.Range(1, 6).Select(i => $"d-{i:x12}").ToList();
        await Task.WhenAll(ids.Select(id => _sync.ApplyDevicesAsync([id])).Append(_sync.ReconcileAsync(true)));

        // If two syncs had interleaved, one would have tried to create what the other just created, and failed.
        Assert.Equal(6, _broker.DeviceClients.Count());
        Assert.Equal(6, _broker.DeviceRoles.Count());
    }
}
