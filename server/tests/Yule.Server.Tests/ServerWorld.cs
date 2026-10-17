using Microsoft.AspNetCore.DataProtection;
using Microsoft.Data.Sqlite;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.DependencyInjection;
using Yule.Server.Data;
using Yule.Server.Security;

namespace Yule.Server.Tests;

/// <summary>
/// A real (temporary, migrated) database and password protector, plus helpers that do what the admin pages will do.
/// Shared by the unit tests and, linked in, the integration tests.
/// </summary>
public sealed class ServerWorld : IAsyncDisposable
{
    private readonly string _directory = Path.Combine(Path.GetTempPath(), "yule-world-" + Guid.NewGuid().ToString("N"));

    private ServerWorld()
    {
        Directory.CreateDirectory(_directory);
        var services = new ServiceCollection();
        services.AddLogging();
        services.AddDbContext<YuleDbContext>(o => o.UseSqlite($"Data Source={Path.Combine(_directory, "yule.db")};Pooling=False"));
        Passwords = new BrokerPasswordProtector(DataProtectionProvider.Create(new DirectoryInfo(Path.Combine(_directory, "keys"))));
        services.AddSingleton(Passwords);
        Services = services.BuildServiceProvider();
    }

    public ServiceProvider Services { get; }

    public IBrokerPasswordProtector Passwords { get; }

    public static async Task<ServerWorld> CreateAsync()
    {
        var world = new ServerWorld();
        await using var scope = world.Services.CreateAsyncScope();
        await scope.ServiceProvider.GetRequiredService<YuleDbContext>().Database.MigrateAsync();
        return world;
    }

    /// <summary>Registers a device with a freshly generated broker password and returns that password.</summary>
    public async Task<string> AddDeviceAsync(string id, string channel = "stable", params string[] groupIds)
    {
        var password = Identifiers.NewBrokerPassword();
        await WithDbAsync(async db =>
        {
            db.Devices.Add(new Device { Id = id, Label = "Device " + id, Channel = channel, EncryptedPassword = Passwords.Protect(password) });
            db.Memberships.AddRange(groupIds.Select(g => new Membership { DeviceId = id, GroupId = g }));
            await db.SaveChangesAsync();
        });
        return password;
    }

    public Task AddGroupAsync(string id, string name) => WithDbAsync(async db =>
    {
        db.Groups.Add(new Group { Id = id, Name = name });
        await db.SaveChangesAsync();
    });

    public Task RenameGroupAsync(string id, string name) => WithDbAsync(async db =>
    {
        (await db.Groups.SingleAsync(g => g.Id == id)).Name = name;
        await db.SaveChangesAsync();
    });

    /// <summary>Makes the device a member of exactly these groups.</summary>
    public Task SetGroupsAsync(string deviceId, params string[] groupIds) => WithDbAsync(async db =>
    {
        db.Memberships.RemoveRange(db.Memberships.Where(m => m.DeviceId == deviceId));
        db.Memberships.AddRange(groupIds.Select(g => new Membership { DeviceId = deviceId, GroupId = g }));
        await db.SaveChangesAsync();
    });

    public Task SetChannelAsync(string deviceId, string channel) => WithDbAsync(async db =>
    {
        (await db.Devices.SingleAsync(d => d.Id == deviceId)).Channel = channel;
        await db.SaveChangesAsync();
    });

    public Task RemoveDeviceAsync(string id) => WithDbAsync(async db =>
    {
        db.Devices.Remove(await db.Devices.SingleAsync(d => d.Id == id));
        await db.SaveChangesAsync();
    });

    /// <summary>Deletes the group and returns the devices that were in it, which are the ones to re-apply.</summary>
    public async Task<List<string>> RemoveGroupAsync(string id)
    {
        var members = new List<string>();
        await WithDbAsync(async db =>
        {
            members = await db.Memberships.Where(m => m.GroupId == id).Select(m => m.DeviceId).ToListAsync();
            db.Groups.Remove(await db.Groups.SingleAsync(g => g.Id == id));
            await db.SaveChangesAsync();
        });
        return members;
    }

    /// <summary>Stores a password that cannot be decrypted, as after losing the Data Protection key ring.</summary>
    public Task CorruptPasswordAsync(string deviceId) => WithDbAsync(async db =>
    {
        (await db.Devices.SingleAsync(d => d.Id == deviceId)).EncryptedPassword = "not-a-protected-payload";
        await db.SaveChangesAsync();
    });

    public async Task<DeviceStatus?> GetStatusAsync(string deviceId)
    {
        await using var scope = Services.CreateAsyncScope();
        return await scope.ServiceProvider.GetRequiredService<YuleDbContext>().DeviceStatuses.AsNoTracking()
            .SingleOrDefaultAsync(s => s.DeviceId == deviceId);
    }

    public async ValueTask DisposeAsync()
    {
        await Services.DisposeAsync();
        SqliteConnection.ClearAllPools();
        try
        {
            Directory.Delete(_directory, recursive: true);
        }
        catch (IOException)
        {
            // A temp directory left behind is harmless.
        }
    }

    private async Task WithDbAsync(Func<YuleDbContext, Task> action)
    {
        await using var scope = Services.CreateAsyncScope();
        await action(scope.ServiceProvider.GetRequiredService<YuleDbContext>());
    }
}
