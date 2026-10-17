using System.Text.Json.Nodes;

namespace Yule.Server.Broker;

/// <summary>One Mosquitto dynamic-security command: the JSON object that goes in the request's "commands" array.</summary>
public sealed record DynsecCommand(string Name, JsonObject Body)
{
    public override string ToString() => Body.ToJsonString();
}

/// <summary>Builders for the dynamic-security commands Yule uses (<c>$CONTROL/dynamic-security/v1</c>).</summary>
public static class DynsecCommands
{
    public const string ControlTopic = "$CONTROL/dynamic-security/v1";
    public const string ResponseTopic = "$CONTROL/dynamic-security/v1/response";

    /// <summary>Priority of the single role each device client holds. Irrelevant with one role; fixed so it never reads as drift.</summary>
    public const int ClientRolePriority = 1;

    public static DynsecCommand ListClients(int count, int offset) => Make("listClients", o =>
    {
        o["verbose"] = false;
        o["count"] = count;
        o["offset"] = offset;
    });

    public static DynsecCommand ListRoles(int count, int offset) => Make("listRoles", o =>
    {
        o["verbose"] = false;
        o["count"] = count;
        o["offset"] = offset;
    });

    public static DynsecCommand GetClient(string username) => Make("getClient", o => o["username"] = username);

    public static DynsecCommand GetRole(string roleName) => Make("getRole", o => o["rolename"] = roleName);

    /// <summary>Creates an empty role. ACLs are added one by one so that no single request can outgrow the broker's packet limit.</summary>
    public static DynsecCommand CreateRole(string roleName) => Make("createRole", o => o["rolename"] = roleName);

    public static DynsecCommand DeleteRole(string roleName) => Make("deleteRole", o => o["rolename"] = roleName);

    public static DynsecCommand AddRoleAcl(string roleName, AclEntry acl) => Make("addRoleACL", o =>
    {
        o["rolename"] = roleName;
        o["acltype"] = acl.AclType;
        o["topic"] = acl.Topic;
        o["priority"] = acl.Priority;
        o["allow"] = acl.Allow;
    });

    public static DynsecCommand RemoveRoleAcl(string roleName, AclEntry acl) => Make("removeRoleACL", o =>
    {
        o["rolename"] = roleName;
        o["acltype"] = acl.AclType;
        o["topic"] = acl.Topic;
    });

    /// <summary>
    /// Creates a login with no role. The role is attached with <see cref="AddClientRole"/>, never inline in this
    /// command: Mosquitto 2.0.22 links a role given inline to the client in one direction only, so deleting that
    /// role later leaves the client pointing at freed memory (its role name reads back as garbage and the broker
    /// can crash), and changes to the role's ACLs do not reach the client. <c>addClientRole</c> links both ways.
    /// </summary>
    public static DynsecCommand CreateClient(DesiredClient client) => Make("createClient", o =>
    {
        o["username"] = client.Username;
        o["password"] = client.Password;
        o["clientid"] = client.ClientId;
    });

    public static DynsecCommand AddClientRole(string username, string roleName) => Make("addClientRole", o =>
    {
        o["username"] = username;
        o["rolename"] = roleName;
        o["priority"] = ClientRolePriority;
    });

    public static DynsecCommand RemoveClientRole(string username, string roleName) => Make("removeClientRole", o =>
    {
        o["username"] = username;
        o["rolename"] = roleName;
    });

    /// <summary>Changes the client id a login must connect with. There is deliberately no command that sets a password: Mosquitto cannot report one, so the server cannot tell whether it differs.</summary>
    public static DynsecCommand SetClientId(string username, string clientId) => Make("setClientId", o =>
    {
        o["username"] = username;
        o["clientid"] = clientId;
    });

    public static DynsecCommand DeleteClient(string username) => Make("deleteClient", o => o["username"] = username);

    private static DynsecCommand Make(string name, Action<JsonObject> fill)
    {
        var body = new JsonObject { ["command"] = name };
        fill(body);
        return new DynsecCommand(name, body);
    }
}
