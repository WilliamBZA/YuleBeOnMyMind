using System.Security.Cryptography;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Mvc.Testing;
using Microsoft.AspNetCore.TestHost;
using Microsoft.Data.Sqlite;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Yule.Server.Admin;
using Yule.Server.Broker;
using Yule.Server.Configuration;
using Yule.Server.Data;
using Yule.Server.Releases;
using Yule.Server.Security;

namespace Yule.Server.Tests;

/// <summary>A clock a test moves by hand.</summary>
public sealed class ManualTimeProvider : TimeProvider
{
    private DateTimeOffset _now = new(2026, 12, 1, 12, 0, 0, TimeSpan.Zero);

    public override DateTimeOffset GetUtcNow() => _now;

    public void Advance(TimeSpan by) => _now += by;
}

/// <summary>An <see cref="IBrokerSynchroniser"/> that remembers what the pages asked it to apply, and can be told to fail or stall.</summary>
public sealed class RecordingSynchroniser : IBrokerSynchroniser
{
    private readonly List<string[]> _applied = [];

    /// <summary>Each call to apply device changes, with the device ids it was given, in order.</summary>
    public IReadOnlyList<string[]> Applied
    {
        get
        {
            lock (_applied)
            {
                return _applied.ToList();
            }
        }
    }

    /// <summary>Every device id applied so far.</summary>
    public IReadOnlyList<string> AppliedIds => Applied.SelectMany(a => a).ToList();

    /// <summary>Run for every apply, before it is reported done. A test makes it throw, or wait, to play a broker that is down.</summary>
    public Func<CancellationToken, Task>? Behaviour { get; set; }

    public void Clear()
    {
        lock (_applied)
        {
            _applied.Clear();
        }
    }

    public async Task<SyncResult> ApplyDevicesAsync(IEnumerable<string> deviceIds, CancellationToken cancellationToken = default)
    {
        var ids = deviceIds.ToArray();
        lock (_applied)
        {
            _applied.Add(ids);
        }

        if (Behaviour is not null)
        {
            await Behaviour(cancellationToken);
        }

        return new SyncResult(0, new HashSet<string>(), new HashSet<string>(), 0);
    }

    public Task<SyncResult> ReconcileAsync(bool republishAllConfigs, CancellationToken cancellationToken = default) =>
        Task.FromResult(new SyncResult(0, new HashSet<string>(), new HashSet<string>(), 0));
}

/// <summary>An <see cref="IReleasePublisher"/> that remembers each announcement, and can be told to fail like a broker that is down.</summary>
public sealed class RecordingReleasePublisher : IReleasePublisher
{
    private readonly List<(string Channel, string Board, string Version, string FileName)> _published = [];

    public IReadOnlyList<(string Channel, string Board, string Version, string FileName)> Published
    {
        get
        {
            lock (_published)
            {
                return _published.ToList();
            }
        }
    }

    public Func<CancellationToken, Task>? Behaviour { get; set; }

    public async Task PublishAsync(FirmwareRelease release, string channel, CancellationToken cancellationToken)
    {
        if (Behaviour is not null)
        {
            await Behaviour(cancellationToken);
        }

        lock (_published)
        {
            _published.Add((channel, release.Board, release.Version, release.FileName));
        }
    }
}

/// <summary>
/// The real application (Program.cs, the real pages, authentication, database and migrations) with everything that
/// talks to the broker replaced by a <see cref="RecordingSynchroniser"/> and a <see cref="RecordingReleasePublisher"/>,
/// so the pages can be tested without Docker.
/// </summary>
public sealed class AdminSite : WebApplicationFactory<Program>
{
    // PBKDF2 at its real strength is deliberately slow; a low count keeps hundreds of logins in the tests quick.
    private static readonly string PasswordHash = AdminPasswordHasher.Hash(AdminSiteDefaults.Password, iterations: 2_000);
    private static readonly (ECDsa Private, string PublicPem) ReleaseKeyPair = TestSupport.NewReleaseKey();
    private static readonly string ReleasePublicKey = ReleaseKeyPair.PublicPem;

    /// <summary>The private half of the release key the site is configured with, for signing test releases.</summary>
    public static ECDsa ReleaseKey => ReleaseKeyPair.Private;

    private readonly string _dataDirectory = Path.Combine(Path.GetTempPath(), "yule-site-" + Guid.NewGuid().ToString("N"));
    private readonly bool _trustForwardedHeaders;
    private readonly TimeSpan _brokerTimeout;

    public AdminSite(bool trustForwardedHeaders = false, TimeSpan? brokerTimeout = null)
    {
        _trustForwardedHeaders = trustForwardedHeaders;
        _brokerTimeout = brokerTimeout ?? TimeSpan.FromSeconds(15);
    }

    public RecordingSynchroniser Broker { get; } = new();

    public RecordingReleasePublisher Releases { get; } = new();

    public ManualTimeProvider Time { get; } = new();

    public string DataDirectory => _dataDirectory;

    /// <summary>A new browser with its own cookies. Requests go to https, as they do behind the reverse proxy.</summary>
    public AdminClient NewClient() => new(CreateClient(new WebApplicationFactoryClientOptions
    {
        AllowAutoRedirect = false,
        BaseAddress = new Uri("https://localhost"),
    }));

    /// <summary>A browser that has already logged in.</summary>
    public async Task<AdminClient> LoggedInClientAsync()
    {
        var client = NewClient();
        await client.LoginAsync();
        return client;
    }

    public async Task<T> WithDbAsync<T>(Func<YuleDbContext, Task<T>> action)
    {
        await using var scope = Services.CreateAsyncScope();
        return await action(scope.ServiceProvider.GetRequiredService<YuleDbContext>());
    }

    public Task WithDbAsync(Func<YuleDbContext, Task> action) => WithDbAsync<int>(async db =>
    {
        await action(db);
        return 0;
    });

    /// <summary>Adds a device straight to the database, as if registered earlier, and returns the broker password it was given.</summary>
    public async Task<string> SeedDeviceAsync(string id, string label = "Gran", string channel = "stable", params string[] groupIds)
    {
        var password = Identifiers.NewBrokerPassword();
        await WithDbAsync(async db =>
        {
            var protector = Services.GetRequiredService<IBrokerPasswordProtector>();
            db.Devices.Add(new Device { Id = id, Label = label, Channel = channel, EncryptedPassword = protector.Protect(password) });
            db.Memberships.AddRange(groupIds.Select(g => new Membership { DeviceId = id, GroupId = g }));
            await db.SaveChangesAsync();
        });
        return password;
    }

    public Task SeedGroupAsync(string id, string name) => WithDbAsync(async db =>
    {
        db.Groups.Add(new Group { Id = id, Name = name });
        await db.SaveChangesAsync();
    });

    public Task SeedStatusAsync(string deviceId, string state = "online", string? firmware = "0.3.0", string? board = "esp32dev", string? ota = "ok", TimeSpan? seenAgo = null) =>
        WithDbAsync(async db =>
        {
            db.DeviceStatuses.Add(new DeviceStatus
            {
                DeviceId = deviceId,
                State = state,
                FirmwareVersion = firmware,
                Board = board,
                OtaResult = ota,
                LastSeenUtc = Time.GetUtcNow().UtcDateTime - (seenAgo ?? TimeSpan.Zero),
            });
            await db.SaveChangesAsync();
        });

    protected override void ConfigureWebHost(IWebHostBuilder builder)
    {
        builder.UseSetting("Yule:Admin:PasswordHash", PasswordHash);
        builder.UseSetting("Yule:Broker:Host", "broker.invalid");
        builder.UseSetting("Yule:Broker:Username", "yule-server");
        builder.UseSetting("Yule:Broker:Password", "unused");
        builder.UseSetting("Yule:PublicBaseUrl", "https://yule.example.org");
        builder.UseSetting("Yule:ReleasePublicKeys:0", ReleasePublicKey);
        builder.UseSetting("Yule:DataDirectory", _dataDirectory);
        builder.UseSetting("Yule:TrustForwardedHeaders", _trustForwardedHeaders ? "true" : "false");

        builder.ConfigureTestServices(services =>
        {
            // Nothing in these tests may reach for a broker.
            foreach (var hosted in services
                         .Where(d => d.ServiceType == typeof(IHostedService) && d.ImplementationType?.Namespace == typeof(IBrokerSynchroniser).Namespace)
                         .ToList())
            {
                services.Remove(hosted);
            }

            services.RemoveAll<IBrokerSynchroniser>();
            services.AddSingleton<IBrokerSynchroniser>(Broker);

            services.RemoveAll<TimeProvider>();
            services.AddSingleton<TimeProvider>(Time);

            services.RemoveAll<BrokerChangeApplier>();
            services.AddScoped(sp => new BrokerChangeApplier(Broker, sp.GetRequiredService<ILogger<BrokerChangeApplier>>()) { Timeout = _brokerTimeout });

            services.RemoveAll<IReleasePublisher>();
            services.AddSingleton<IReleasePublisher>(Releases);
            services.RemoveAll<ReleaseAdminService>();
            services.AddScoped(sp => new ReleaseAdminService(
                sp.GetRequiredService<YuleDbContext>(),
                Releases,
                sp.GetRequiredService<IOptions<YuleOptions>>(),
                sp.GetRequiredService<TimeProvider>(),
                sp.GetRequiredService<ILogger<ReleaseAdminService>>()) { PublishTimeout = _brokerTimeout });
        });
    }

    protected override void Dispose(bool disposing)
    {
        base.Dispose(disposing);
        if (!disposing)
        {
            return;
        }

        SqliteConnection.ClearAllPools();
        try
        {
            Directory.Delete(_dataDirectory, recursive: true);
        }
        catch (IOException)
        {
            // A temp directory left behind is harmless.
        }
    }
}
