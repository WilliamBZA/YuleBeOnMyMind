using Yule.Server.Broker;

namespace Yule.Server.Tests;

public class BrokerPlannerTests
{
    private const string Root = "yule/v1";
    private const string EmmaId = "d-3c71bf4a2b10";
    private const string EmmaRole = "device-" + EmmaId;

    private static readonly GroupInfo Brander = new("g-7f3k2abc", "Brander Grandparents");
    private static readonly GroupInfo Smith = new("g-q7m4x2td", "Smith Grandparents");

    private static DeviceModel Emma(params GroupInfo[] groups) => new(EmmaId, "Emma", "stable", "emma-password-0123456789", groups);

    private static ActualBrokerState Broker(DesiredBrokerState desired) => new(
        desired.Clients.Select(c => new ActualClient(c.Username, c.ClientId, [c.RoleName])).ToList(),
        desired.Roles.ToDictionary(r => r.Name, r => r.Acls));

    private static readonly ActualBrokerState Empty = new([], new Dictionary<string, IReadOnlyList<AclEntry>>());

    private static string[] Names(BrokerPlan plan) => plan.Commands.Select(c => c.Name).ToArray();

    [Fact]
    public void A_broker_that_already_matches_needs_no_commands()
    {
        var desired = BrokerProjection.Project([Emma(Brander, Smith)], Root);

        var plan = BrokerPlanner.Plan([EmmaId], desired, Broker(desired));

        Assert.True(plan.IsEmpty);
        Assert.Empty(plan.ChangedDevices);
        Assert.Empty(plan.RemovedDevices);
    }

    [Fact]
    public void A_new_device_gets_its_role_and_permissions_before_its_login()
    {
        var desired = BrokerProjection.Project([Emma(Brander)], Root);

        var plan = BrokerPlanner.Plan([EmmaId], desired, Empty);

        var aclCount = desired.Roles.Single().Acls.Count;
        Assert.Equal(["createRole", .. Enumerable.Repeat("addRoleACL", aclCount), "createClient", "addClientRole"], Names(plan));
        Assert.Equal([EmmaId], plan.ChangedDevices);

        var create = plan.Commands[^2].Body;
        Assert.Equal(EmmaId, create["username"]!.GetValue<string>());
        Assert.Equal(EmmaId, create["clientid"]!.GetValue<string>());
        Assert.Equal("emma-password-0123456789", create["password"]!.GetValue<string>());

        // The role is attached by its own command, never inline: Mosquitto 2.0.22 mishandles an inline role.
        Assert.False(create.ContainsKey("roles"));
        var attach = plan.Commands[^1].Body;
        Assert.Equal(EmmaId, attach["username"]!.GetValue<string>());
        Assert.Equal(EmmaRole, attach["rolename"]!.GetValue<string>());
    }

    [Fact]
    public void Leaving_a_group_removes_only_that_groups_permissions()
    {
        var before = BrokerProjection.Project([Emma(Brander, Smith)], Root);
        var after = BrokerProjection.Project([Emma(Brander)], Root);

        var plan = BrokerPlanner.Plan([EmmaId], after, Broker(before));

        Assert.Equal(["removeRoleACL", "removeRoleACL", "removeRoleACL"], Names(plan));
        Assert.All(plan.Commands, c => Assert.Contains(Smith.Id, c.Body["topic"]!.GetValue<string>()));
        Assert.Equal([EmmaId], plan.ChangedDevices);
        Assert.Empty(plan.RemovedDevices);
    }

    [Fact]
    public void Joining_a_group_adds_only_that_groups_permissions()
    {
        var before = BrokerProjection.Project([Emma(Brander)], Root);
        var after = BrokerProjection.Project([Emma(Brander, Smith)], Root);

        var plan = BrokerPlanner.Plan([EmmaId], after, Broker(before));

        Assert.Equal(["addRoleACL", "addRoleACL", "addRoleACL"], Names(plan));
        Assert.All(plan.Commands, c => Assert.Contains(Smith.Id, c.Body["topic"]!.GetValue<string>()));
    }

    [Fact]
    public void A_stale_role_with_no_login_gets_its_login_created_and_attached()
    {
        var after = BrokerProjection.Project([Emma(Brander)], Root);
        var rolesOnly = new ActualBrokerState([], new Dictionary<string, IReadOnlyList<AclEntry>> { [EmmaRole] = [new AclEntry(AclTypes.SubscribeLiteral, "yule/v1/groups/g-zzzzzzzz/press/+")] });

        var plan = BrokerPlanner.Plan([EmmaId], after, rolesOnly);

        Assert.Contains(plan.Commands, c => c.Name == "removeRoleACL");
        Assert.Equal(["createClient", "addClientRole"], Names(plan).Where(n => n.EndsWith("Client", StringComparison.Ordinal) || n.EndsWith("ClientRole", StringComparison.Ordinal)));
    }

    [Fact]
    public void A_device_that_is_no_longer_registered_has_its_login_and_role_deleted_login_first()
    {
        var before = BrokerProjection.Project([Emma(Brander)], Root);
        var after = BrokerProjection.Project([], Root);

        var plan = BrokerPlanner.Plan([EmmaId], after, Broker(before));

        Assert.Equal(["deleteClient", "deleteRole"], Names(plan));
        Assert.Equal([EmmaId], plan.RemovedDevices);
        Assert.Empty(plan.ChangedDevices);
    }

    [Fact]
    public void A_device_that_is_already_gone_from_both_sides_needs_nothing()
    {
        var plan = BrokerPlanner.Plan([EmmaId], BrokerProjection.Project([], Root), Empty);

        Assert.True(plan.IsEmpty);
        Assert.Empty(plan.RemovedDevices);
    }

    [Fact]
    public void A_changed_channel_swaps_only_the_firmware_permissions()
    {
        var before = BrokerProjection.Project([Emma(Brander)], Root);
        var beta = Emma(Brander) with { Channel = "beta" };
        var after = BrokerProjection.Project([beta], Root);

        var plan = BrokerPlanner.Plan([EmmaId], after, Broker(before));

        Assert.Equal(2, plan.Commands.Count(c => c.Name == "removeRoleACL"));
        Assert.Equal(2, plan.Commands.Count(c => c.Name == "addRoleACL"));
        Assert.All(plan.Commands, c => Assert.Contains("/firmware/", c.Body["topic"]!.GetValue<string>()));
    }

    [Fact]
    public void A_wrong_client_id_or_role_is_repaired_without_touching_the_password()
    {
        var desired = BrokerProjection.Project([Emma(Brander)], Root);
        ActualBrokerState With(string? clientId, params string[] roles) =>
            new([new ActualClient(EmmaId, clientId, roles)], Broker(desired).Roles);

        var cases = new (ActualBrokerState Actual, string[] Expected)[]
        {
            (With("somebody-else", EmmaRole), ["setClientId"]),
            (With(null, EmmaRole), ["setClientId"]),
            (With(EmmaId, "some-other-role"), ["removeClientRole", "addClientRole"]),
            (With(EmmaId, EmmaRole, "extra"), ["removeClientRole"]),
            (With(EmmaId), ["addClientRole"]),
        };

        foreach (var (actual, expected) in cases)
        {
            var plan = BrokerPlanner.Plan([EmmaId], desired, actual);

            // Mosquitto cannot report a stored password and disconnects a client whose login changes, so a repair never resends one.
            Assert.Equal(expected, Names(plan));
            Assert.All(plan.Commands, c => Assert.False(c.Body.ContainsKey("password")));
            Assert.Equal([EmmaId], plan.ChangedDevices);
        }
    }

    [Fact]
    public void A_lost_role_is_recreated_and_the_surviving_login_is_attached_to_it()
    {
        var desired = BrokerProjection.Project([Emma(Brander)], Root);
        var actual = new ActualBrokerState([new ActualClient(EmmaId, EmmaId, [])], new Dictionary<string, IReadOnlyList<AclEntry>>());

        var plan = BrokerPlanner.Plan([EmmaId], desired, actual);

        Assert.Equal("createRole", plan.Commands[0].Name);
        Assert.Equal("addClientRole", plan.Commands[^1].Name);
        Assert.DoesNotContain(plan.Commands, c => c.Name == "createClient");
    }

    [Fact]
    public void An_acl_with_the_wrong_effect_is_replaced()
    {
        var desired = BrokerProjection.Project([Emma(Brander)], Root);
        var acls = desired.Roles.Single().Acls.ToList();
        var index = acls.FindIndex(a => a.AclType == AclTypes.PublishClientSend);
        acls[index] = acls[index] with { Allow = false };
        var actual = new ActualBrokerState(Broker(desired).Clients, new Dictionary<string, IReadOnlyList<AclEntry>> { [EmmaRole] = acls });

        var plan = BrokerPlanner.Plan([EmmaId], desired, actual);

        Assert.Equal(["removeRoleACL", "addRoleACL"], Names(plan));
    }

    [Fact]
    public void Other_accounts_are_never_planned_for()
    {
        var desired = BrokerProjection.Project([], Root);
        var actual = new ActualBrokerState(
            [new ActualClient("admin", null, ["admin"]), new ActualClient("yule-server", null, ["yule-server"])],
            new Dictionary<string, IReadOnlyList<AclEntry>> { ["admin"] = [], ["yule-server"] = [] });

        var plan = BrokerPlanner.Plan(["admin", "yule-server", "device-" + EmmaId], desired, actual);

        Assert.True(plan.IsEmpty);
    }

    [Fact]
    public void Roles_are_planned_before_clients_and_removals_come_last_across_several_devices()
    {
        var keep = new DeviceModel("d-000000000001", "Keep", "stable", "keep-password-0123456789", [Brander]);
        var add = new DeviceModel("d-000000000002", "Add", "stable", "add-password-01234567890", [Brander]);
        var gone = new DeviceModel("d-000000000003", "Gone", "stable", "gone-password-0123456789", [Brander]);
        var broker = Broker(BrokerProjection.Project([keep, gone], Root));
        var desired = BrokerProjection.Project([keep, add], Root);

        var plan = BrokerPlanner.Plan(["d-000000000001", "d-000000000002", "d-000000000003"], desired, broker);

        var names = Names(plan);
        Assert.Equal("createRole", names.First());
        Assert.True(Array.IndexOf(names, "createClient") > Array.LastIndexOf(names, "addRoleACL"));
        Assert.Equal(["deleteClient", "deleteRole"], names[^2..]);
        Assert.Equal(["d-000000000002"], plan.ChangedDevices);
        Assert.Equal(["d-000000000003"], plan.RemovedDevices);
    }
}
