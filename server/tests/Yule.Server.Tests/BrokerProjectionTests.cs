using Yule.Server.Broker;

namespace Yule.Server.Tests;

public class BrokerProjectionTests
{
    private const string Root = "yule/v1";
    private const string EmmaId = "d-3c71bf4a2b10";

    private static readonly GroupInfo Brander = new("g-7f3k2abc", "Brander Grandparents");
    private static readonly GroupInfo Smith = new("g-q7m4x2td", "Smith Grandparents");

    private static DeviceModel Emma(string channel = "stable", params GroupInfo[] groups) =>
        new(EmmaId, "Emma", channel, "emma-broker-password-24", groups);

    private static List<(string Type, string Topic)> Topics(DeviceModel device) =>
        BrokerProjection.DeviceAcls(device, Root).Select(a => (a.AclType, a.Topic)).ToList();

    [Fact]
    public void A_device_in_no_groups_gets_a_login_and_a_role_that_allows_no_group_topics()
    {
        var state = BrokerProjection.Project([Emma()], Root);

        var client = Assert.Single(state.Clients);
        Assert.Equal(new DesiredClient(EmmaId, EmmaId, "emma-broker-password-24", "device-" + EmmaId), client);
        var role = Assert.Single(state.Roles);
        Assert.Equal("device-" + EmmaId, role.Name);
        Assert.DoesNotContain(role.Acls, a => a.Topic.Contains("/groups/", StringComparison.Ordinal) && !a.AclType.StartsWith("unsubscribe", StringComparison.Ordinal));

        // It still reports presence and receives its config and firmware, per the "device in no groups" requirement.
        Assert.Contains(role.Acls, a => a.AclType == AclTypes.PublishClientSend && a.Topic == $"{Root}/devices/{EmmaId}/status");
        Assert.Contains(role.Acls, a => a.AclType == AclTypes.SubscribeLiteral && a.Topic == $"{Root}/devices/{EmmaId}/config");

        var config = Assert.Single(state.RetainedConfigs);
        Assert.Equal($"{Root}/devices/{EmmaId}/config", config.Topic);
        Assert.Equal("""{"v":1,"groups":[],"channel":"stable"}""", config.Payload);
    }

    [Fact]
    public void A_device_in_two_groups_may_use_exactly_those_two_groups()
    {
        var emma = Emma("stable", Smith, Brander);

        var acls = BrokerProjection.DeviceAcls(emma, Root);

        foreach (var group in new[] { Brander, Smith })
        {
            Assert.Contains(new AclEntry(AclTypes.PublishClientSend, $"{Root}/groups/{group.Id}/press/{EmmaId}"), acls);
            Assert.Contains(new AclEntry(AclTypes.SubscribeLiteral, $"{Root}/groups/{group.Id}/press/+"), acls);
            Assert.Contains(new AclEntry(AclTypes.PublishClientReceive, $"{Root}/groups/{group.Id}/press/+"), acls);
        }

        // Publishing is only ever as itself: never to another sender's topic, and never to a wildcard.
        var publishTopics = acls.Where(a => a.AclType == AclTypes.PublishClientSend).Select(a => a.Topic).ToList();
        Assert.All(publishTopics, t => Assert.DoesNotContain("+", t));
        Assert.DoesNotContain(acls, a => a.Topic.Contains("#", StringComparison.Ordinal));

        Assert.Equal(
            """{"v":1,"groups":[{"id":"g-7f3k2abc","name":"Brander Grandparents"},{"id":"g-q7m4x2td","name":"Smith Grandparents"}],"channel":"stable"}""",
            BrokerProjection.ConfigPayload(emma));
    }

    [Fact]
    public void Changing_the_channel_changes_only_the_firmware_permission_and_the_config()
    {
        var stable = Emma("stable", Brander);
        var beta = Emma("beta", Brander);

        var stableTopics = Topics(stable);
        var betaTopics = Topics(beta);

        Assert.Equal(
            stableTopics.Select(t => (t.Type, t.Topic.Replace("/firmware/stable/", "/firmware/beta/", StringComparison.Ordinal))),
            betaTopics);
        Assert.Contains((AclTypes.SubscribePattern, $"{Root}/firmware/beta/+"), betaTopics);
        Assert.DoesNotContain((AclTypes.SubscribePattern, $"{Root}/firmware/stable/+"), betaTopics);
        Assert.Contains("\"channel\":\"beta\"", BrokerProjection.ConfigPayload(beta));
        Assert.Contains("\"channel\":\"stable\"", BrokerProjection.ConfigPayload(stable));
    }

    [Fact]
    public void A_removed_device_is_absent_from_the_projection()
    {
        var other = new DeviceModel("d-000000000001", "Tester", "stable", "tester-password-123456", [Brander]);

        var both = BrokerProjection.Project([Emma("stable", Brander), other], Root);
        var withoutEmma = BrokerProjection.Project([other], Root);

        Assert.Equal(2, both.Clients.Count);
        Assert.Equal("d-000000000001", Assert.Single(withoutEmma.Clients).Username);
        Assert.DoesNotContain(withoutEmma.Roles, r => r.Name == "device-" + EmmaId);
        Assert.DoesNotContain(withoutEmma.RetainedConfigs, m => m.Topic.Contains(EmmaId, StringComparison.Ordinal));
        Assert.Empty(BrokerProjection.Project([], Root).Clients);
    }

    [Fact]
    public void The_exact_permissions_of_one_device_in_two_groups()
    {
        var topics = Topics(Emma("stable", Brander, Smith));

        Assert.Equal(
            [
                (AclTypes.PublishClientSend, "yule/v1/devices/d-3c71bf4a2b10/status"),
                (AclTypes.SubscribeLiteral, "yule/v1/devices/d-3c71bf4a2b10/config"),
                (AclTypes.PublishClientReceive, "yule/v1/devices/d-3c71bf4a2b10/config"),
                (AclTypes.SubscribePattern, "yule/v1/firmware/stable/+"),
                (AclTypes.PublishClientReceive, "yule/v1/firmware/stable/+"),
                (AclTypes.UnsubscribePattern, "yule/v1/groups/+/press/+"),
                (AclTypes.UnsubscribePattern, "yule/v1/firmware/+/+"),
                (AclTypes.PublishClientSend, "yule/v1/groups/g-7f3k2abc/press/d-3c71bf4a2b10"),
                (AclTypes.SubscribeLiteral, "yule/v1/groups/g-7f3k2abc/press/+"),
                (AclTypes.PublishClientReceive, "yule/v1/groups/g-7f3k2abc/press/+"),
                (AclTypes.PublishClientSend, "yule/v1/groups/g-q7m4x2td/press/d-3c71bf4a2b10"),
                (AclTypes.SubscribeLiteral, "yule/v1/groups/g-q7m4x2td/press/+"),
                (AclTypes.PublishClientReceive, "yule/v1/groups/g-q7m4x2td/press/+"),
            ],
            topics);
        Assert.All(BrokerProjection.DeviceAcls(Emma("stable", Brander, Smith), Root), a => Assert.True(a.Allow));
    }

    [Fact]
    public void The_projection_does_not_depend_on_the_order_of_its_input()
    {
        var a = new DeviceModel("d-000000000001", "A", "stable", "password-a-password-aa", [Brander, Smith]);
        var b = new DeviceModel("d-000000000002", "B", "beta", "password-b-password-bb", [Smith, Brander]);

        var forward = BrokerProjection.Project([a, b], Root);
        var reverse = BrokerProjection.Project([b, a], Root);

        Assert.Equal(forward.Clients, reverse.Clients);
        Assert.Equal(forward.Roles.Select(r => (r.Name, r.Acls)), reverse.Roles.Select(r => (r.Name, r.Acls)), new RoleComparer());
        Assert.Equal(forward.RetainedConfigs, reverse.RetainedConfigs);
        Assert.Equal(BrokerProjection.ConfigPayload(a), BrokerProjection.ConfigPayload(b).Replace("beta", "stable"));
    }

    [Fact]
    public void Group_names_survive_the_payload_unchanged()
    {
        const string name = "Grandma & Grandpa's 🎄 café";
        var payload = BrokerProjection.ConfigPayload(Emma("stable", new GroupInfo("g-aaaaaaaa", name)));

        // Characters outside the Basic Multilingual Plane (the tree) are written as \u escapes, which is still valid
        // JSON that the firmware decodes; everything else, including &, ' and é, is written as itself.
        Assert.Contains("Grandma & Grandpa's", payload);
        Assert.Contains("café", payload);
        Assert.Equal(name, System.Text.Json.JsonDocument.Parse(payload).RootElement.GetProperty("groups")[0].GetProperty("name").GetString());
    }

    [Theory]
    [InlineData("d-3c71bf4a2b10", true)]
    [InlineData("admin", false)]
    [InlineData("yule-server", false)]
    [InlineData("", false)]
    public void Only_device_logins_are_managed_by_the_server(string username, bool managed)
    {
        Assert.Equal(managed, BrokerProjection.IsManagedClient(username));
    }

    [Theory]
    [InlineData("device-d-3c71bf4a2b10", true)]
    [InlineData("device-admin", false)]
    [InlineData("admin", false)]
    [InlineData("yule-server", false)]
    [InlineData("d-3c71bf4a2b10", false)]
    public void Only_device_roles_are_managed_by_the_server(string role, bool managed)
    {
        Assert.Equal(managed, BrokerProjection.IsManagedRole(role));
    }

    private sealed class RoleComparer : IEqualityComparer<(string Name, IReadOnlyList<AclEntry> Acls)>
    {
        public bool Equals((string Name, IReadOnlyList<AclEntry> Acls) x, (string Name, IReadOnlyList<AclEntry> Acls) y) =>
            x.Name == y.Name && x.Acls.SequenceEqual(y.Acls);

        public int GetHashCode((string Name, IReadOnlyList<AclEntry> Acls) obj) => obj.Name.GetHashCode();
    }
}
