using System.Security.Cryptography;
using Microsoft.AspNetCore.DataProtection;
using Microsoft.AspNetCore.Mvc.Testing;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.DependencyInjection;
using MQTTnet.Formatter;
using Yule.Server.Data;
using Yule.Server.Security;

namespace Yule.Server.IntegrationTests;

/// <summary>
/// Boots the real application (Program.cs, the hosted services, the DI graph) against a real broker, with a database
/// that already holds a device, and checks that the server does its broker work on its own, with nobody calling it.
/// </summary>
public class ServerHostIntegrationTests(MosquittoFixture broker) : IClassFixture<MosquittoFixture>, IAsyncLifetime
{
    // Unique per test: the broker outlives each test's host, and an existing account keeps the password it was created with.
    private readonly string DeviceId = "d-" + Guid.NewGuid().ToString("N")[..12];
    private const string GroupId = "g-eeeeeeee";

    private readonly string _dataDirectory = Path.Combine(Path.GetTempPath(), "yule-host-" + Guid.NewGuid().ToString("N"));
    private WebApplicationFactory<Program> _factory = null!;
    private string _devicePassword = "";

    public async Task InitializeAsync()
    {
        Directory.CreateDirectory(Path.Combine(_dataDirectory, "keys"));
        var protector = new BrokerPasswordProtector(DataProtectionProvider.Create(
            new DirectoryInfo(Path.Combine(_dataDirectory, "keys")), o => o.SetApplicationName("Yule")));
        _devicePassword = Identifiers.NewBrokerPassword();

        // The database as the server will find it on start-up: a device in a group, registered before the server ran.
        var options = new DbContextOptionsBuilder<YuleDbContext>().UseSqlite($"Data Source={Path.Combine(_dataDirectory, "yule.db")};Pooling=False").Options;
        await using (var db = new YuleDbContext(options))
        {
            await db.Database.MigrateAsync();
            db.Groups.Add(new Group { Id = GroupId, Name = "Brander Grandparents" });
            db.Devices.Add(new Device { Id = DeviceId, Label = "Gran", EncryptedPassword = protector.Protect(_devicePassword) });
            db.Memberships.Add(new Membership { DeviceId = DeviceId, GroupId = GroupId });
            await db.SaveChangesAsync();
        }

        var serverOptions = broker.ServerOptions();
        using var releaseKey = ECDsa.Create(ECCurve.NamedCurves.nistP256);
        _factory = new WebApplicationFactory<Program>().WithWebHostBuilder(host =>
        {
            host.UseSetting("Yule:Admin:PasswordHash", AdminPasswordHasher.Hash("correct horse battery staple", iterations: 2_000));
            host.UseSetting("Yule:Broker:Host", serverOptions.Host);
            host.UseSetting("Yule:Broker:Port", serverOptions.Port.ToString());
            host.UseSetting("Yule:Broker:Username", serverOptions.Username);
            host.UseSetting("Yule:Broker:Password", serverOptions.Password);
            host.UseSetting("Yule:Broker:ClientId", serverOptions.ClientId);
            host.UseSetting("Yule:PublicBaseUrl", "https://yule.example.org");
            host.UseSetting("Yule:ReleasePublicKeys:0", releaseKey.ExportSubjectPublicKeyInfoPem());
            host.UseSetting("Yule:DataDirectory", _dataDirectory);
        });
        _ = _factory.Server; // starts the host, and with it the background services
    }

    public async Task DisposeAsync()
    {
        await _factory.DisposeAsync();
        SqliteCleanup();
    }

    private void SqliteCleanup()
    {
        Microsoft.Data.Sqlite.SqliteConnection.ClearAllPools();
        try
        {
            Directory.Delete(_dataDirectory, recursive: true);
        }
        catch (IOException)
        {
        }
    }

    [Fact]
    public async Task The_server_creates_the_accounts_of_the_devices_in_its_database_by_itself_at_start_up()
    {
        // No one calls the synchroniser: the start-up reconcile must have made the account, with the stored password.
        TestDevice? device = null;
        await Eventually.TrueAsync(async () =>
        {
            try
            {
                device = await TestDevice.ConnectAsync(broker, DeviceId, _devicePassword);
                return true;
            }
            catch (ConnectionRefusedException)
            {
                return false;
            }
        }, "the server to create the device's account at start-up", seconds: 20);

        await using (device)
        {
            Assert.True(await device!.TrySubscribeAsync($"yule/v1/groups/{GroupId}/press/+"));

            // The start-up pass also published the device's retained config, which a device gets on subscribing.
            Assert.True(await device.TrySubscribeAsync($"yule/v1/devices/{DeviceId}/config"));
            var config = await device.WaitForAsync($"yule/v1/devices/{DeviceId}/config");
            Assert.NotNull(config);
            Assert.Contains("Brander Grandparents", config);
        }
    }

    [Fact]
    public async Task The_server_records_a_device_status_and_applies_a_change_made_through_its_services()
    {
        TestDevice? device = null;
        await Eventually.TrueAsync(async () =>
        {
            try
            {
                device = await TestDevice.ConnectAsync(broker, DeviceId, _devicePassword, MqttProtocolVersion.V500, persistentSession: false);
                return true;
            }
            catch (ConnectionRefusedException)
            {
                return false;
            }
        }, "the server to create the device's account at start-up", seconds: 20);

        await using (device)
        {
            // The status listener, registered by the host, records what the device says.
            await device!.PublishAsync($"yule/v1/devices/{DeviceId}/status", """{"v":1,"state":"online","fw":"0.9.0","board":"esp32dev","name":"Gran","ota":"ok"}""", retain: true);
            await Eventually.TrueAsync(async () =>
            {
                await using var scope = _factory.Services.CreateAsyncScope();
                var status = await scope.ServiceProvider.GetRequiredService<YuleDbContext>().DeviceStatuses.AsNoTracking().SingleOrDefaultAsync();
                return status?.FirmwareVersion == "0.9.0" && status.State == "online";
            }, "the host's status listener to record the device's report");

            // And the synchroniser the pages will use is the same, working, singleton.
            var sync = _factory.Services.GetRequiredService<Broker.IBrokerSynchroniser>();
            var result = await sync.ReconcileAsync(republishAllConfigs: false);
            Assert.Equal(0, result.CommandsSent);
        }

        var health = await _factory.CreateClient().GetStringAsync("/healthz");
        Assert.Equal("ok", health);
    }
}
