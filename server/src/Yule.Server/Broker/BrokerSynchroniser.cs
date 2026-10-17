using System.Security.Cryptography;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Options;
using Yule.Server.Configuration;
using Yule.Server.Data;
using Yule.Server.Security;

namespace Yule.Server.Broker;

/// <param name="CommandsSent">Dynamic-security commands that changed the broker; zero when it already matched the database.</param>
/// <param name="ChangedDevices">Devices whose client or role was created or changed.</param>
/// <param name="RemovedDevices">Devices whose client and role were deleted.</param>
/// <param name="ConfigsPublished">Retained device-config messages published (or cleared, for removed devices).</param>
public sealed record SyncResult(
    int CommandsSent,
    IReadOnlySet<string> ChangedDevices,
    IReadOnlySet<string> RemovedDevices,
    int ConfigsPublished);

/// <summary>Keeps the broker's permissions and the devices' retained configs in step with the database, which is the source of truth.</summary>
public interface IBrokerSynchroniser
{
    /// <summary>
    /// The change-driven path: call it after the database changed for these devices (registered, edited, removed, or
    /// their groups changed or were renamed or deleted). Permissions are updated first, then each device's retained
    /// config is republished, so a device never learns of a group before the broker lets it use it.
    /// </summary>
    Task<SyncResult> ApplyDevicesAsync(IEnumerable<string> deviceIds, CancellationToken cancellationToken = default);

    /// <summary>
    /// The safety net: compares the broker with the whole database and repairs any difference, including deleting
    /// accounts of devices the database no longer has. With <paramref name="republishAllConfigs"/> every device's
    /// retained config is published again (at start-up and after a reconnection, when the broker may have lost
    /// them); otherwise only the configs of devices that needed repair.
    /// </summary>
    Task<SyncResult> ReconcileAsync(bool republishAllConfigs, CancellationToken cancellationToken = default);
}

public sealed class BrokerSynchroniser(
    IServiceScopeFactory scopes,
    IBrokerPasswordProtector passwords,
    DynsecClient dynsec,
    IBrokerConnection connection,
    IOptions<YuleOptions> options,
    ILogger<BrokerSynchroniser> logger) : IBrokerSynchroniser
{
    // One writer at a time: the pages, the periodic reconciler and a reconnection can all ask for a sync at once.
    private readonly SemaphoreSlim _gate = new(1, 1);

    private string Root => options.Value.TopicRoot;

    public async Task<SyncResult> ApplyDevicesAsync(IEnumerable<string> deviceIds, CancellationToken cancellationToken = default)
    {
        var requested = deviceIds.Where(Identifiers.IsValidDeviceId).Distinct(StringComparer.Ordinal).ToList();
        if (requested.Count == 0)
        {
            return new SyncResult(0, new HashSet<string>(), new HashSet<string>(), 0);
        }

        await _gate.WaitAsync(cancellationToken);
        try
        {
            var loaded = await LoadDevicesAsync(requested, cancellationToken);
            var scope = requested.Except(loaded.Unreadable).ToList();
            var existing = loaded.Devices.Select(d => d.Id).ToHashSet(StringComparer.Ordinal);
            return await SyncAsync(
                loaded.Devices,
                _ => scope,
                (_, plan) => scope.Where(existing.Contains),
                (_, plan) => scope.Where(id => !existing.Contains(id)),
                cancellationToken);
        }
        finally
        {
            _gate.Release();
        }
    }

    public async Task<SyncResult> ReconcileAsync(bool republishAllConfigs, CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            var loaded = await LoadDevicesAsync(null, cancellationToken);
            return await SyncAsync(
                loaded.Devices,
                actual => loaded.Devices.Select(d => d.Id)
                    .Concat(actual.Clients.Select(c => c.Username))
                    .Concat(actual.Roles.Keys.Select(r => r["device-".Length..]))
                    .Except(loaded.Unreadable),
                (desired, plan) => republishAllConfigs ? desired.Clients.Select(c => c.Username) : plan.ChangedDevices,
                (_, plan) => plan.RemovedDevices,
                cancellationToken);
        }
        finally
        {
            _gate.Release();
        }
    }

    private async Task<SyncResult> SyncAsync(
        IReadOnlyList<DeviceModel> devices,
        Func<ActualBrokerState, IEnumerable<string>> scope,
        Func<DesiredBrokerState, BrokerPlan, IEnumerable<string>> configsToPublish,
        Func<DesiredBrokerState, BrokerPlan, IEnumerable<string>> configsToClear,
        CancellationToken cancellationToken)
    {
        var desired = BrokerProjection.Project(devices, Root);
        var actual = await dynsec.ReadStateAsync(cancellationToken);
        var plan = BrokerPlanner.Plan(scope(actual), desired, actual);

        if (!plan.IsEmpty)
        {
            logger.LogInformation("Updating broker permissions: {Changed} device(s) changed, {Removed} removed ({Commands} commands)",
                plan.ChangedDevices.Count, plan.RemovedDevices.Count, plan.Commands.Count);

            // The broker disconnects every device whose role or login changed (see BrokerPlanner), so each comes back
            // with exactly what it may now use and reads its new config.
            await dynsec.ExecuteAsync(plan.Commands, cancellationToken);
        }

        // Only after the permissions are in place, so a device told about a new group is already allowed to use it.
        var published = 0;
        var byId = desired.RetainedConfigs.ToDictionary(m => m.Topic, StringComparer.Ordinal);
        foreach (var id in configsToPublish(desired, plan).Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal))
        {
            var config = byId[BrokerProjection.ConfigTopic(Root, id)];
            await connection.PublishAsync(config.Topic, config.Payload, retain: true, cancellationToken);
            published++;
        }

        foreach (var id in configsToClear(desired, plan).Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal))
        {
            await connection.PublishAsync(BrokerProjection.ConfigTopic(Root, id), "", retain: true, cancellationToken);
            published++;
        }

        return new SyncResult(plan.Commands.Count, plan.ChangedDevices, plan.RemovedDevices, published);
    }

    /// <summary>Loads devices with their groups and decrypts their broker passwords. A device whose password cannot be decrypted is reported, and must be left alone rather than treated as absent.</summary>
    private async Task<(List<DeviceModel> Devices, HashSet<string> Unreadable)> LoadDevicesAsync(IReadOnlyCollection<string>? ids, CancellationToken cancellationToken)
    {
        await using var scope = scopes.CreateAsyncScope();
        var db = scope.ServiceProvider.GetRequiredService<YuleDbContext>();
        var query = db.Devices.AsNoTracking().Include(d => d.Memberships).ThenInclude(m => m.Group).AsQueryable();
        if (ids is not null)
        {
            query = query.Where(d => ids.Contains(d.Id));
        }

        var devices = new List<DeviceModel>();
        var unreadable = new HashSet<string>(StringComparer.Ordinal);
        foreach (var device in await query.OrderBy(d => d.Id).ToListAsync(cancellationToken))
        {
            string password;
            try
            {
                password = passwords.Unprotect(device.EncryptedPassword);
            }
            catch (CryptographicException ex)
            {
                logger.LogError(ex, "Cannot decrypt the broker password of {Device}; leaving its broker account as it is. Was the Data Protection key ring lost?", device.Id);
                unreadable.Add(device.Id);
                continue;
            }

            var groups = device.Memberships.Select(m => new GroupInfo(m.Group.Id, m.Group.Name)).ToList();
            devices.Add(new DeviceModel(device.Id, device.Label, device.Channel, password, groups));
        }

        return (devices, unreadable);
    }
}
