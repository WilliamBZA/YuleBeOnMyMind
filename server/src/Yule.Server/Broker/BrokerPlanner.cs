namespace Yule.Server.Broker;

/// <summary>The commands that bring the broker in line with the projection, in an order that is safe to run.</summary>
/// <param name="Commands">Role changes first, then client changes, then client and role removals.</param>
/// <param name="ChangedDevices">Devices that have at least one command, so are worth republishing a config for.</param>
/// <param name="RemovedDevices">Devices whose client and role are being deleted.</param>
public sealed record BrokerPlan(
    IReadOnlyList<DynsecCommand> Commands,
    IReadOnlySet<string> ChangedDevices,
    IReadOnlySet<string> RemovedDevices)
{
    public bool IsEmpty => Commands.Count == 0;
}

/// <summary>
/// The pure difference between what the broker holds and what the projection wants. It issues a command only
/// where the broker differs. That matters: Mosquitto disconnects a client when its roles, client id or password
/// change, and when the ACLs of its role change (whether they grow or shrink), so a plan for an already-correct
/// device must be empty, or the reconciler would disconnect the whole fleet every ten minutes. A device that
/// loses a permission is disconnected by that same mechanism, and reconnects to read its new config.
/// </summary>
public static class BrokerPlanner
{
    public static BrokerPlan Plan(IEnumerable<string> deviceIds, DesiredBrokerState desired, ActualBrokerState actual)
    {
        var desiredClients = desired.Clients.ToDictionary(c => c.Username, StringComparer.Ordinal);
        var desiredRoles = desired.Roles.ToDictionary(r => r.Name, StringComparer.Ordinal);
        var actualClients = actual.Clients.ToDictionary(c => c.Username, StringComparer.Ordinal);

        var roleCommands = new List<DynsecCommand>();
        var clientCommands = new List<DynsecCommand>();
        var removeClientCommands = new List<DynsecCommand>();
        var removeRoleCommands = new List<DynsecCommand>();
        var changed = new SortedSet<string>(StringComparer.Ordinal);
        var removed = new SortedSet<string>(StringComparer.Ordinal);

        // Only device accounts are ever planned for: the operator's and the server's own accounts are not ours to change.
        foreach (var id in deviceIds.Where(BrokerProjection.IsManagedClient).Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal))
        {
            var roleName = BrokerProjection.RoleName(id);
            actual.Roles.TryGetValue(roleName, out var actualAcls);
            actualClients.TryGetValue(id, out var actualClient);

            if (desiredClients.TryGetValue(id, out var client) && desiredRoles.TryGetValue(roleName, out var role))
            {
                var roleChange = PlanRole(role, actualAcls);
                var clientChange = PlanClient(client, actualClient);
                roleCommands.AddRange(roleChange);
                clientCommands.AddRange(clientChange);
                if (roleChange.Count + clientChange.Count > 0)
                {
                    changed.Add(id);
                }
            }
            else
            {
                if (actualClient is not null)
                {
                    removeClientCommands.Add(DynsecCommands.DeleteClient(id));
                }

                if (actualAcls is not null)
                {
                    removeRoleCommands.Add(DynsecCommands.DeleteRole(roleName));
                }

                if (actualClient is not null || actualAcls is not null)
                {
                    removed.Add(id);
                }
            }
        }

        var commands = roleCommands.Concat(clientCommands).Concat(removeClientCommands).Concat(removeRoleCommands).ToList();
        return new BrokerPlan(commands, changed, removed);
    }

    /// <summary>Creates the role if it is missing, otherwise adds and removes only the ACLs that differ.</summary>
    public static IReadOnlyList<DynsecCommand> PlanRole(DesiredRole desired, IReadOnlyList<AclEntry>? actualAcls)
    {
        var commands = new List<DynsecCommand>();
        if (actualAcls is null)
        {
            commands.Add(DynsecCommands.CreateRole(desired.Name));
            commands.AddRange(desired.Acls.Distinct().Select(acl => DynsecCommands.AddRoleAcl(desired.Name, acl)));
            return commands;
        }

        var want = desired.Acls.ToHashSet();
        var have = actualAcls.ToHashSet();

        // An ACL is identified by its type and topic; if one exists with a different effect it is replaced.
        commands.AddRange(have.Where(acl => !want.Contains(acl)).Select(acl => DynsecCommands.RemoveRoleAcl(desired.Name, acl)));
        commands.AddRange(want.Where(acl => !have.Contains(acl)).Select(acl => DynsecCommands.AddRoleAcl(desired.Name, acl)));
        return commands;
    }

    /// <summary>
    /// Creates the client (with its password) and attaches its role if it is missing; otherwise fixes its client id
    /// and roles where they differ. Never touches the password of an existing client.
    /// </summary>
    public static IReadOnlyList<DynsecCommand> PlanClient(DesiredClient desired, ActualClient? actual)
    {
        if (actual is null)
        {
            return [DynsecCommands.CreateClient(desired), DynsecCommands.AddClientRole(desired.Username, desired.RoleName)];
        }

        var commands = new List<DynsecCommand>();
        if (actual.ClientId != desired.ClientId)
        {
            commands.Add(DynsecCommands.SetClientId(desired.Username, desired.ClientId));
        }

        commands.AddRange(actual.Roles.Where(r => r != desired.RoleName).Select(r => DynsecCommands.RemoveClientRole(desired.Username, r)));
        if (!actual.Roles.Contains(desired.RoleName))
        {
            commands.Add(DynsecCommands.AddClientRole(desired.Username, desired.RoleName));
        }

        return commands;
    }
}
