using System.Text.Encodings.Web;
using System.Text.Json;
using Yule.Server.Data;

namespace Yule.Server.Broker;

/// <summary>
/// The pure projection from database state to what the broker should contain (design D5 and D15): one dynsec
/// client and one role per device, with exactly that device's permissions, plus the retained config payload each
/// device receives. No I/O, so it is exhaustively unit-tested.
/// </summary>
public static class BrokerProjection
{
    private static readonly JsonSerializerOptions JsonOptions = new() { Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping };

    public static string RoleName(string deviceId) => "device-" + deviceId;

    /// <summary>Whether a broker username/role name belongs to a device, and so is managed by this server.</summary>
    public static bool IsManagedClient(string username) => Identifiers.IsValidDeviceId(username);

    public static bool IsManagedRole(string roleName) =>
        roleName.StartsWith("device-", StringComparison.Ordinal) && Identifiers.IsValidDeviceId(roleName["device-".Length..]);

    public static string ConfigTopic(string root, string deviceId) => $"{root}/devices/{deviceId}/config";

    public static DesiredBrokerState Project(IEnumerable<DeviceModel> devices, string root)
    {
        var clients = new List<DesiredClient>();
        var roles = new List<DesiredRole>();
        var configs = new List<RetainedMessage>();
        foreach (var device in devices.OrderBy(d => d.Id, StringComparer.Ordinal))
        {
            var role = RoleName(device.Id);
            clients.Add(new DesiredClient(device.Id, device.Id, device.Password, role));
            roles.Add(new DesiredRole(role, DeviceAcls(device, root)));
            configs.Add(new RetainedMessage(ConfigTopic(root, device.Id), ConfigPayload(device)));
        }

        return new DesiredBrokerState(clients, roles, configs);
    }

    /// <summary>
    /// The permissions of one device. A device may publish only as itself, only into its own groups; it receives
    /// only its own groups' presses, its own config and its channel's firmware manifests; it may unsubscribe from
    /// group and firmware topics (harmless, and needed when its membership or channel changes).
    /// </summary>
    public static IReadOnlyList<AclEntry> DeviceAcls(DeviceModel device, string root)
    {
        var acls = new List<AclEntry>
        {
            // Presence: the online status and the broker-published Last Will.
            new(AclTypes.PublishClientSend, $"{root}/devices/{device.Id}/status"),

            // Its own retained config, written only by the server.
            new(AclTypes.SubscribeLiteral, ConfigTopic(root, device.Id)),
            new(AclTypes.PublishClientReceive, ConfigTopic(root, device.Id)),

            // Firmware manifests for its channel. The device subscribes to firmware/<channel>/<board>, a concrete
            // topic under the wildcard, so this needs the "pattern" form, which also matches concrete topics.
            new(AclTypes.SubscribePattern, $"{root}/firmware/{device.Channel}/+"),
            new(AclTypes.PublishClientReceive, $"{root}/firmware/{device.Channel}/+"),

            // Unsubscribing is always allowed from group and firmware topics, so a device whose membership or
            // channel just changed can tidy up its old subscriptions.
            new(AclTypes.UnsubscribePattern, $"{root}/groups/+/press/+"),
            new(AclTypes.UnsubscribePattern, $"{root}/firmware/+/+"),
        };

        foreach (var group in device.Groups.OrderBy(g => g.Id, StringComparer.Ordinal))
        {
            // Publish only as itself, only into a group it belongs to...
            acls.Add(new AclEntry(AclTypes.PublishClientSend, $"{root}/groups/{group.Id}/press/{device.Id}"));
            // ... and listen to everyone's presses in that group, via exactly the filter the firmware uses.
            acls.Add(new AclEntry(AclTypes.SubscribeLiteral, $"{root}/groups/{group.Id}/press/+"));
            acls.Add(new AclEntry(AclTypes.PublishClientReceive, $"{root}/groups/{group.Id}/press/+"));
        }

        return acls;
    }

    /// <summary>The retained device-config payload: <c>{"v":1,"groups":[{"id":..,"name":..}],"channel":"stable"}</c>.</summary>
    public static string ConfigPayload(DeviceModel device)
    {
        var groups = device.Groups
            .OrderBy(g => g.Name, StringComparer.Ordinal)
            .ThenBy(g => g.Id, StringComparer.Ordinal)
            .Select(g => new Dictionary<string, string> { ["id"] = g.Id, ["name"] = g.Name })
            .ToList();
        var payload = new Dictionary<string, object> { ["v"] = 1, ["groups"] = groups, ["channel"] = device.Channel };
        return JsonSerializer.Serialize(payload, JsonOptions);
    }
}
