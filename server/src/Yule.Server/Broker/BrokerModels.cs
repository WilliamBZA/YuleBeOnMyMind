namespace Yule.Server.Broker;

/// <summary>Mosquitto dynamic-security ACL types used by Yule.</summary>
public static class AclTypes
{
    public const string PublishClientSend = "publishClientSend";
    public const string PublishClientReceive = "publishClientReceive";
    public const string SubscribeLiteral = "subscribeLiteral";
    public const string SubscribePattern = "subscribePattern";
    public const string UnsubscribeLiteral = "unsubscribeLiteral";
    public const string UnsubscribePattern = "unsubscribePattern";
}

/// <summary>A group as seen by the broker projection: just the parts that reach devices.</summary>
public sealed record GroupInfo(string Id, string Name);

/// <summary>
/// A device as the projection needs it, with its broker password already decrypted. Kept free of EF types so the
/// projection is a plain function of its input.
/// </summary>
public sealed record DeviceModel(string Id, string Label, string Channel, string Password, IReadOnlyList<GroupInfo> Groups);

/// <summary>One permission in a role. Equality is by value, so sets of ACLs can be compared directly.</summary>
public sealed record AclEntry(string AclType, string Topic, bool Allow = true, int Priority = 0);

public sealed record DesiredRole(string Name, IReadOnlyList<AclEntry> Acls);

/// <summary>A broker login for a device. <see cref="ClientId"/> restricts the login to that MQTT client id.</summary>
public sealed record DesiredClient(string Username, string ClientId, string Password, string RoleName);

/// <summary>A retained message the server keeps published (a device's config or a firmware manifest).</summary>
public sealed record RetainedMessage(string Topic, string Payload);

/// <summary>Everything the broker should contain for the devices the server knows about, as projected from the database.</summary>
public sealed record DesiredBrokerState(
    IReadOnlyList<DesiredClient> Clients,
    IReadOnlyList<DesiredRole> Roles,
    IReadOnlyList<RetainedMessage> RetainedConfigs);

public sealed record ActualClient(string Username, string? ClientId, IReadOnlyList<string> Roles);

/// <summary>What the broker currently holds, as read back through the dynamic-security API.</summary>
public sealed record ActualBrokerState(
    IReadOnlyList<ActualClient> Clients,
    IReadOnlyDictionary<string, IReadOnlyList<AclEntry>> Roles);
