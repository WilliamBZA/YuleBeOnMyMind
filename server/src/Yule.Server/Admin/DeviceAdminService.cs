using Microsoft.EntityFrameworkCore;
using Yule.Server.Data;
using Yule.Server.Security;

namespace Yule.Server.Admin;

public sealed record GroupRef(string Id, string Name);

/// <summary>A device as the pages show it: its groups and the last status it reported (null if it has never reported).</summary>
public sealed record DeviceView(string Id, string Label, string Channel, DateTime CreatedUtc, IReadOnlyList<GroupRef> Groups, DeviceStatus? Status);

/// <param name="Password">The generated broker password. It exists only in this result: the database keeps it encrypted and no page shows it again.</param>
public sealed record RegisterResult(ChangeResult Change, string? DeviceId, string? Password);

/// <summary>The administrator's device actions (spec <c>family-groups</c>, "Administration through the server").</summary>
public sealed class DeviceAdminService(YuleDbContext db, IBrokerPasswordProtector passwords, BrokerChangeApplier broker)
{
    public const string IdField = "Id";
    public const string LabelField = "Label";
    public const string ChannelField = "Channel";

    public async Task<IReadOnlyList<DeviceView>> ListAsync(CancellationToken cancellationToken = default)
    {
        var devices = await db.Devices.AsNoTracking()
            .Include(d => d.Status)
            .Include(d => d.Memberships).ThenInclude(m => m.Group)
            .ToListAsync(cancellationToken);

        // Sorted here rather than in SQL, whose default comparison puts every capital letter before every lower-case one.
        return devices.OrderBy(d => d.Label, StringComparer.CurrentCultureIgnoreCase).ThenBy(d => d.Id, StringComparer.Ordinal)
            .Select(ToView).ToList();
    }

    public async Task<DeviceView?> FindAsync(string id, CancellationToken cancellationToken = default)
    {
        var device = await db.Devices.AsNoTracking()
            .Include(d => d.Status)
            .Include(d => d.Memberships).ThenInclude(m => m.Group)
            .SingleOrDefaultAsync(d => d.Id == id, cancellationToken);
        return device is null ? null : ToView(device);
    }

    /// <summary>
    /// Registers a device and generates its broker password, returned for the one and only time. The broker account is
    /// created at once, so the device can connect as soon as the password has been entered on it.
    /// </summary>
    public async Task<RegisterResult> RegisterAsync(string? id, string? label, CancellationToken cancellationToken = default)
    {
        id = (id ?? "").Trim().ToLowerInvariant();
        label = (label ?? "").Trim();

        var errors = new Dictionary<string, string>();
        if (!Identifiers.IsValidDeviceId(id))
        {
            errors[IdField] = "A device id is d- followed by 12 digits or letters a-f, for example d-3c71bf4a2b10. "
                              + "It is shown on the device's settings page and in its serial log at start-up.";
        }
        else if (await db.Devices.AnyAsync(d => d.Id == id, cancellationToken))
        {
            errors[IdField] = "That device is already registered.";
        }

        if (!Identifiers.IsValidDeviceLabel(label))
        {
            errors[LabelField] = "Give the device a label of 1 to 60 characters, for example \"Gran (Brander)\".";
        }

        if (errors.Count > 0)
        {
            return new RegisterResult(ChangeResult.Refused(errors), null, null);
        }

        var password = Identifiers.NewBrokerPassword();
        db.Devices.Add(new Device { Id = id, Label = label, Channel = "stable", EncryptedPassword = passwords.Protect(password) });
        try
        {
            await db.SaveChangesAsync(cancellationToken);
        }
        catch (DbUpdateException)
        {
            // Two registrations of the same id raced and the other one won; anything else is a real failure.
            if (!await IsRegisteredElsewhereAsync(id, cancellationToken))
            {
                throw;
            }

            return new RegisterResult(ChangeResult.Refused(IdField, "That device is already registered."), null, null);
        }

        var outcome = await broker.ApplyAsync([id], cancellationToken);
        return new RegisterResult(ChangeResult.Saved(outcome), id, password);
    }

    public async Task<ChangeResult> UpdateAsync(string id, string? label, string? channel, CancellationToken cancellationToken = default)
    {
        label = (label ?? "").Trim();
        var errors = new Dictionary<string, string>();
        if (!Identifiers.IsValidDeviceLabel(label))
        {
            errors[LabelField] = "Give the device a label of 1 to 60 characters.";
        }

        if (!Identifiers.IsValidChannel(channel))
        {
            errors[ChannelField] = "The update channel is stable or beta.";
        }

        var device = await db.Devices.SingleOrDefaultAsync(d => d.Id == id, cancellationToken);
        if (device is null)
        {
            return ChangeResult.Missing("device");
        }

        if (errors.Count > 0)
        {
            return ChangeResult.Refused(errors);
        }

        // Only a channel change touches the broker (the device's firmware topic and its config); a label never leaves the server.
        var channelChanged = device.Channel != channel;
        device.Label = label;
        device.Channel = channel!;
        await db.SaveChangesAsync(cancellationToken);

        return ChangeResult.Saved(channelChanged ? await broker.ApplyAsync([id], cancellationToken) : ApplyOutcome.Done);
    }

    /// <summary>Removes the device and its memberships and status. The broker closes its connection and refuses it from then on.</summary>
    public async Task<ChangeResult> RemoveAsync(string id, CancellationToken cancellationToken = default)
    {
        var device = await db.Devices.SingleOrDefaultAsync(d => d.Id == id, cancellationToken);
        if (device is null)
        {
            return ChangeResult.Missing("device");
        }

        db.Devices.Remove(device);
        await db.SaveChangesAsync(cancellationToken);
        return ChangeResult.Saved(await broker.ApplyAsync([id], cancellationToken));
    }

    private async Task<bool> IsRegisteredElsewhereAsync(string id, CancellationToken cancellationToken)
    {
        db.ChangeTracker.Clear();
        return await db.Devices.AnyAsync(d => d.Id == id, cancellationToken);
    }

    private static DeviceView ToView(Device device) => new(
        device.Id,
        device.Label,
        device.Channel,
        device.CreatedUtc,
        device.Memberships.Select(m => new GroupRef(m.Group.Id, m.Group.Name)).OrderBy(g => g.Name, StringComparer.CurrentCultureIgnoreCase).ThenBy(g => g.Id, StringComparer.Ordinal).ToList(),
        device.Status);
}
