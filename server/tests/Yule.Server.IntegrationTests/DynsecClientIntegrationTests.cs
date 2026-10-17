using Yule.Server.Broker;

namespace Yule.Server.IntegrationTests;

public class DynsecClientIntegrationTests(MosquittoFixture broker) : IClassFixture<MosquittoFixture>, IAsyncLifetime
{
    private const string Root = "yule/v1";
    private const string Device = "d-00000000a001";
    private const string Group = "g-aaaaaaaa";

    private BrokerHarness _server = null!;

    public async Task InitializeAsync() => _server = await BrokerHarness.StartAsync(broker);

    public async Task DisposeAsync() => await _server.DisposeAsync();

    private static (DesiredRole Role, DesiredClient Login) AccountFor(string id, string password, params string[] groups)
    {
        var projected = BrokerProjection.Project(
            [new DeviceModel(id, "label", "stable", password, groups.Select(g => new GroupInfo(g, g)).ToList())], Root);
        return (projected.Roles.Single(), projected.Clients.Single());
    }

    [Fact]
    public async Task Creating_a_client_lets_it_connect_and_only_as_itself()
    {
        var (role, login) = AccountFor(Device, "device-a001-password", Group);

        Assert.True(await _server.Dynsec.EnsureRoleAsync(role, default));
        Assert.True(await _server.Dynsec.EnsureClientAsync(login, default));

        await using var device = await TestDevice.ConnectAsync(broker, Device, "device-a001-password");
        Assert.True(device.IsConnected);
        Assert.True(await device.TrySubscribeAsync($"{Root}/groups/{Group}/press/+"));
        Assert.True(await device.TrySubscribeAsync($"{Root}/firmware/stable/esp32dev"));

        // The wrong password, and the right password under someone else's client id, are both refused.
        await Assert.ThrowsAsync<ConnectionRefusedException>(() => TestDevice.ConnectAsync(broker, Device, "wrong-password"));
        await Assert.ThrowsAsync<ConnectionRefusedException>(() => TestDevice.ConnectAsync(broker, Device, "device-a001-password", clientId: "d-00000000ffff"));
    }

    [Fact]
    public async Task Applying_the_same_account_again_is_a_no_op_that_does_not_disturb_a_connected_device()
    {
        var (role, login) = AccountFor(Device, "device-a001-password", Group);
        await _server.Dynsec.EnsureRoleAsync(role, default);
        await _server.Dynsec.EnsureClientAsync(login, default);
        await using var device = await TestDevice.ConnectAsync(broker, Device, "device-a001-password");
        await device.TrySubscribeAsync($"{Root}/groups/{Group}/press/+");

        // Mosquitto disconnects a client whenever it is modified, even to the values it already has, so "no-op"
        // has to mean that no command is sent at all.
        Assert.False(await _server.Dynsec.EnsureRoleAsync(role, default));
        Assert.False(await _server.Dynsec.EnsureClientAsync(login, default));
        await Task.Delay(1000);

        Assert.True(device.IsConnected);
        Assert.Equal(0, device.Disconnects);
        var state = await _server.Dynsec.ReadStateAsync(default);
        Assert.Equal(Device, Assert.Single(state.Clients).Username);
        Assert.Equal(role.Acls.ToHashSet(), state.Roles[role.Name].ToHashSet());
    }

    [Fact]
    public async Task Changing_a_role_disconnects_the_devices_that_hold_it_and_applies_to_their_next_connection()
    {
        var (role, login) = AccountFor(Device, "device-a001-password", Group);
        await _server.Dynsec.EnsureRoleAsync(role, default);
        await _server.Dynsec.EnsureClientAsync(login, default);
        await using var device = await TestDevice.ConnectAsync(broker, Device, "device-a001-password");
        Assert.False(await device.TrySubscribeAsync($"{Root}/groups/g-bbbbbbbb/press/+"));

        var widened = AccountFor(Device, "device-a001-password", Group, "g-bbbbbbbb").Role;
        Assert.True(await _server.Dynsec.EnsureRoleAsync(widened, default));

        // The broker drops the device so that it comes back, reads its config and uses exactly what it now has.
        await Eventually.TrueAsync(() => device.Disconnects > 0, "the broker to disconnect the device whose role changed");
        await using var reconnected = await TestDevice.ConnectAsync(broker, Device, "device-a001-password");
        Assert.True(await reconnected.TrySubscribeAsync($"{Root}/groups/g-bbbbbbbb/press/+"));

        var narrowed = AccountFor(Device, "device-a001-password", "g-bbbbbbbb").Role;
        Assert.True(await _server.Dynsec.EnsureRoleAsync(narrowed, default));
        await Eventually.TrueAsync(() => reconnected.Disconnects > 0, "the broker to disconnect the device that lost a permission");
    }

    [Fact]
    public async Task A_role_deleted_by_hand_leaves_its_client_readable_and_repairable()
    {
        // Regression: attaching the role inline in createClient made Mosquitto 2.0.22 keep a dangling pointer to a role
        // deleted later, which read back as garbage and could take the broker down. The role is attached with
        // addClientRole instead, which unlinks cleanly.
        var (role, login) = AccountFor(Device, "device-a001-password", Group);
        await _server.Dynsec.EnsureRoleAsync(role, default);
        await _server.Dynsec.EnsureClientAsync(login, default);

        await _server.Dynsec.ExecuteAsync([DynsecCommands.DeleteRole(role.Name)], default);

        var state = await _server.Dynsec.ReadStateAsync(default);
        Assert.Empty(Assert.Single(state.Clients).Roles);
        Assert.Empty(state.Roles);

        Assert.True(await _server.Dynsec.EnsureRoleAsync(role, default));
        Assert.True(await _server.Dynsec.EnsureClientAsync(login, default));
        await using var device = await TestDevice.ConnectAsync(broker, Device, "device-a001-password");
        Assert.True(await device.TrySubscribeAsync($"{Root}/groups/{Group}/press/+"));
        Assert.False(await _server.Dynsec.EnsureRoleAsync(role, default));
        Assert.False(await _server.Dynsec.EnsureClientAsync(login, default));
    }

    [Fact]
    public async Task Repairing_a_clients_role_keeps_its_password()
    {
        var (role, login) = AccountFor(Device, "device-a001-password", Group);
        await _server.Dynsec.EnsureRoleAsync(role, default);
        await _server.Dynsec.EnsureClientAsync(login, default);
        await _server.Dynsec.ExecuteAsync([DynsecCommands.RemoveClientRole(Device, role.Name)], default);

        Assert.True(await _server.Dynsec.EnsureClientAsync(login, default));

        await using var device = await TestDevice.ConnectAsync(broker, Device, "device-a001-password");
        Assert.True(await device.TrySubscribeAsync($"{Root}/groups/{Group}/press/+"));
        Assert.False(await _server.Dynsec.EnsureClientAsync(login, default));
    }

    [Fact]
    public async Task A_device_in_the_maximum_32_groups_round_trips_through_the_brokers_packet_limit()
    {
        var groups = Enumerable.Range(0, 32).Select(i => $"g-{i:x8}").ToArray();
        var (role, login) = AccountFor(Device, "device-a001-password", groups);
        Assert.Equal(7 + 32 * 3, role.Acls.Count);

        Assert.True(await _server.Dynsec.EnsureRoleAsync(role, default));
        Assert.True(await _server.Dynsec.EnsureClientAsync(login, default));

        // Reading it back brings a response far larger than 4096 bytes, so this also proves the server can receive one.
        var state = await _server.Dynsec.ReadStateAsync(default);
        Assert.Equal(role.Acls.ToHashSet(), state.Roles[role.Name].ToHashSet());
        Assert.False(await _server.Dynsec.EnsureRoleAsync(role, default));
        await using var device = await TestDevice.ConnectAsync(broker, Device, "device-a001-password");
        Assert.True(await device.TrySubscribeAsync($"{Root}/groups/{groups[31]}/press/+"));
    }

    [Fact]
    public async Task A_command_the_broker_rejects_surfaces_as_an_error_naming_the_command()
    {
        var noSuchRole = "device-" + Device;
        var acl = new AclEntry(AclTypes.SubscribeLiteral, "yule/v1/anything");

        var thrown = await Assert.ThrowsAsync<DynsecException>(() => _server.Dynsec.ExecuteAsync([DynsecCommands.AddRoleAcl(noSuchRole, acl)], default));

        Assert.Contains("addRoleACL", thrown.Message);
        Assert.Contains("not found", thrown.Message, StringComparison.OrdinalIgnoreCase);
        Assert.Contains(noSuchRole, thrown.Message);
    }

    [Fact]
    public async Task Reading_the_state_ignores_the_operators_and_the_servers_own_accounts()
    {
        var state = await _server.Dynsec.ReadStateAsync(default);

        Assert.Empty(state.Clients);
        Assert.Empty(state.Roles);
        var all = await _server.Dynsec.ExecuteAsync([DynsecCommands.ListClients(100, 0)], default);
        Assert.Contains("yule-server", all[0].Data!["clients"]!.AsArray().Select(c => c!.GetValue<string>()));
    }

}
