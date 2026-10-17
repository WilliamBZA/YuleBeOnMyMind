using Microsoft.EntityFrameworkCore;
using Yule.Server.Data;

namespace Yule.Server.Admin;

public sealed record MemberRef(string Id, string Label);

public sealed record GroupView(string Id, string Name, DateTime CreatedUtc, IReadOnlyList<MemberRef> Members);

public sealed record CreateGroupResult(ChangeResult Change, string? GroupId);

/// <summary>The administrator's group actions (spec <c>family-groups</c>, "Administration through the server").</summary>
public sealed class GroupAdminService(YuleDbContext db, BrokerChangeApplier broker)
{
    public const string NameField = "Name";
    public const string DeviceField = "DeviceId";

    public async Task<IReadOnlyList<GroupView>> ListAsync(CancellationToken cancellationToken = default)
    {
        var groups = await db.Groups.AsNoTracking()
            .Include(g => g.Memberships).ThenInclude(m => m.Device)
            .ToListAsync(cancellationToken);

        // Sorted here rather than in SQL, whose default comparison puts every capital letter before every lower-case one.
        return groups.OrderBy(g => g.Name, StringComparer.CurrentCultureIgnoreCase).ThenBy(g => g.Id, StringComparer.Ordinal)
            .Select(ToView).ToList();
    }

    public async Task<GroupView?> FindAsync(string id, CancellationToken cancellationToken = default)
    {
        var group = await db.Groups.AsNoTracking()
            .Include(g => g.Memberships).ThenInclude(m => m.Device)
            .SingleOrDefaultAsync(g => g.Id == id, cancellationToken);
        return group is null ? null : ToView(group);
    }

    /// <summary>The devices that could be added to the group: every registered device that is not already in it.</summary>
    public async Task<IReadOnlyList<MemberRef>> CandidatesAsync(string groupId, CancellationToken cancellationToken = default)
    {
        var devices = await db.Devices.AsNoTracking()
            .Where(d => !d.Memberships.Any(m => m.GroupId == groupId))
            .ToListAsync(cancellationToken);
        return devices.OrderBy(d => d.Label, StringComparer.CurrentCultureIgnoreCase).ThenBy(d => d.Id, StringComparer.Ordinal)
            .Select(d => new MemberRef(d.Id, d.Label)).ToList();
    }

    public async Task<CreateGroupResult> CreateAsync(string? name, CancellationToken cancellationToken = default)
    {
        name = (name ?? "").Trim();
        if (!Identifiers.IsValidGroupName(name))
        {
            return new CreateGroupResult(ChangeResult.Refused(NameField, NameMessage), null);
        }

        string id;
        do
        {
            id = Identifiers.NewGroupId();
        }
        while (await db.Groups.AnyAsync(g => g.Id == id, cancellationToken));

        db.Groups.Add(new Group { Id = id, Name = name });
        await db.SaveChangesAsync(cancellationToken);

        // A new group has no members, so no device's permissions or config change yet.
        return new CreateGroupResult(ChangeResult.Saved(ApplyOutcome.Done), id);
    }

    /// <summary>Renames the group. Its members carry the name in their config, so each of them is updated.</summary>
    public async Task<ChangeResult> RenameAsync(string id, string? name, CancellationToken cancellationToken = default)
    {
        name = (name ?? "").Trim();
        var group = await db.Groups.SingleOrDefaultAsync(g => g.Id == id, cancellationToken);
        if (group is null)
        {
            return ChangeResult.Missing("group");
        }

        if (!Identifiers.IsValidGroupName(name))
        {
            return ChangeResult.Refused(NameField, NameMessage);
        }

        if (group.Name == name)
        {
            return ChangeResult.Saved(ApplyOutcome.Done);
        }

        group.Name = name;
        await db.SaveChangesAsync(cancellationToken);
        return ChangeResult.Saved(await broker.ApplyAsync(await MemberIdsAsync(id, cancellationToken), cancellationToken));
    }

    /// <summary>Deletes the group; its members lose access to its presses and are updated.</summary>
    public async Task<ChangeResult> DeleteAsync(string id, CancellationToken cancellationToken = default)
    {
        var group = await db.Groups.SingleOrDefaultAsync(g => g.Id == id, cancellationToken);
        if (group is null)
        {
            return ChangeResult.Missing("group");
        }

        var members = await MemberIdsAsync(id, cancellationToken);
        db.Groups.Remove(group);
        await db.SaveChangesAsync(cancellationToken);
        return ChangeResult.Saved(await broker.ApplyAsync(members, cancellationToken));
    }

    public async Task<ChangeResult> AddMemberAsync(string groupId, string? deviceId, CancellationToken cancellationToken = default)
    {
        if (!await db.Groups.AnyAsync(g => g.Id == groupId, cancellationToken))
        {
            return ChangeResult.Missing("group");
        }

        if (!Identifiers.IsValidDeviceId(deviceId) || !await db.Devices.AnyAsync(d => d.Id == deviceId, cancellationToken))
        {
            return ChangeResult.Refused(DeviceField, "Choose one of the registered devices.");
        }

        if (!await db.Memberships.AnyAsync(m => m.GroupId == groupId && m.DeviceId == deviceId, cancellationToken))
        {
            db.Memberships.Add(new Membership { GroupId = groupId, DeviceId = deviceId! });
            try
            {
                await db.SaveChangesAsync(cancellationToken);
            }
            catch (DbUpdateException)
            {
                // Added by someone else in the meantime, or the device or group was just removed; either way, look again below.
                db.ChangeTracker.Clear();
                if (!await db.Memberships.AnyAsync(m => m.GroupId == groupId && m.DeviceId == deviceId, cancellationToken))
                {
                    return ChangeResult.Missing("device or group");
                }
            }
        }

        return ChangeResult.Saved(await broker.ApplyAsync([deviceId!], cancellationToken));
    }

    public async Task<ChangeResult> RemoveMemberAsync(string groupId, string? deviceId, CancellationToken cancellationToken = default)
    {
        var membership = await db.Memberships.SingleOrDefaultAsync(m => m.GroupId == groupId && m.DeviceId == deviceId, cancellationToken);
        if (membership is not null)
        {
            db.Memberships.Remove(membership);
            await db.SaveChangesAsync(cancellationToken);
        }

        // Applied even if the membership was already gone: it is cheap, and it repairs a broker that missed the first change.
        return Identifiers.IsValidDeviceId(deviceId)
            ? ChangeResult.Saved(await broker.ApplyAsync([deviceId!], cancellationToken))
            : ChangeResult.Missing("device");
    }

    private const string NameMessage = "Give the group a name of 1 to 40 characters, for example \"Brander Grandparents\".";

    private Task<List<string>> MemberIdsAsync(string groupId, CancellationToken cancellationToken) =>
        db.Memberships.Where(m => m.GroupId == groupId).Select(m => m.DeviceId).ToListAsync(cancellationToken);

    private static GroupView ToView(Group group) => new(
        group.Id,
        group.Name,
        group.CreatedUtc,
        group.Memberships.Select(m => new MemberRef(m.Device.Id, m.Device.Label))
            .OrderBy(m => m.Label, StringComparer.CurrentCultureIgnoreCase).ThenBy(m => m.Id, StringComparer.Ordinal).ToList());
}
