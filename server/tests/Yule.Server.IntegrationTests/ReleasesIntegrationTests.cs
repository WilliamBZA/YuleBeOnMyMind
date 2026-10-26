using System.Net;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;
using Microsoft.AspNetCore.Mvc.Testing;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.DependencyInjection;
using Yule.Server.Security;
using Yule.Server.Tests;

namespace Yule.Server.IntegrationTests;

/// <summary>
/// Releases end to end (spec <c>firmware-updates</c>): uploads through the Releases page against a real Mosquitto with
/// dynamic security, seen by stand-in devices that follow their channel the way the firmware does. Retained manifests
/// outlive a test on the shared broker, so each test uses its own board.
/// </summary>
public sealed class ReleasesIntegrationTests(MosquittoFixture broker) : IClassFixture<MosquittoFixture>, IAsyncLifetime
{
    private const string Root = "yule/v1";
    private const string PublicBaseUrl = "https://yule.example.org";
    private static readonly Regex ShownPassword = new("<code id=\"broker-password\">([^<]+)</code>", RegexOptions.Compiled);

    private readonly string _dataDirectory = Path.Combine(Path.GetTempPath(), "yule-releases-" + Guid.NewGuid().ToString("N"));
    private readonly ECDsa _releaseKey = ECDsa.Create(ECCurve.NamedCurves.nistP256);
    private readonly List<TestDevice> _devices = [];
    private WebApplicationFactory<Program> _factory = null!;
    private AdminClient _admin = null!;

    public async Task InitializeAsync()
    {
        var serverOptions = broker.ServerOptions();
        _factory = new WebApplicationFactory<Program>().WithWebHostBuilder(host =>
        {
            host.UseSetting("Yule:Admin:PasswordHash", AdminPasswordHasher.Hash(AdminSiteDefaults.Password, iterations: 2_000));
            host.UseSetting("Yule:Broker:Host", serverOptions.Host);
            host.UseSetting("Yule:Broker:Port", serverOptions.Port.ToString());
            host.UseSetting("Yule:Broker:Username", serverOptions.Username);
            host.UseSetting("Yule:Broker:Password", serverOptions.Password);
            host.UseSetting("Yule:Broker:ClientId", serverOptions.ClientId);
            host.UseSetting("Yule:PublicBaseUrl", PublicBaseUrl);
            host.UseSetting("Yule:ReleasePublicKeys:0", _releaseKey.ExportSubjectPublicKeyInfoPem());
            host.UseSetting("Yule:DataDirectory", _dataDirectory);
        });
        _admin = new AdminClient(_factory.CreateClient(new WebApplicationFactoryClientOptions
        {
            AllowAutoRedirect = false,
            BaseAddress = new Uri("https://localhost"),
        }));
        await _admin.LoginAsync();
    }

    public async Task DisposeAsync()
    {
        foreach (var device in _devices)
        {
            await device.DisposeAsync();
        }

        await _factory.DisposeAsync();
        _releaseKey.Dispose();
        Microsoft.Data.Sqlite.SqliteConnection.ClearAllPools();
        try
        {
            Directory.Delete(_dataDirectory, recursive: true);
        }
        catch (IOException)
        {
        }
    }

    private static string ManifestTopic(string channel, string board) => $"{Root}/firmware/{channel}/{board}";

    /// <summary>Registers a device on the given channel and connects it, following its channel's manifest for <paramref name="board"/>.</summary>
    private async Task<TestDevice> DeviceOnAsync(string channel, string board)
    {
        var id = "d-" + Guid.NewGuid().ToString("N")[..12];
        var registered = await _admin.PostFormAsync("/devices", "/devices", ("Input.Id", id), ("Input.Label", channel + " device"));
        var password = ShownPassword.Match(await registered.Content.ReadAsStringAsync()).Groups[1].Value;
        Assert.NotEmpty(password);
        if (channel != "stable")
        {
            var edited = await _admin.PostFormAsync($"/devices/{id}", $"/devices/{id}", ("Label", channel + " device"), ("Channel", channel));
            Assert.Equal(HttpStatusCode.Redirect, edited.StatusCode);
        }

        var device = await TestDevice.ConnectAsync(broker, id, password, autoReconnect: true,
            onConnected: d => d.TrySubscribeAsync(ManifestTopic(channel, board)));
        _devices.Add(device);
        return device;
    }

    private async Task<HttpResponseMessage> UploadAsync(byte[] image, string manifest) =>
        await _admin.PostFilesAsync("/releases?handler=Upload", "/releases",
            ("Image", "release.bin", image),
            ("Manifest", "release.manifest.json", Encoding.UTF8.GetBytes(manifest)));

    [Fact]
    public async Task A_valid_upload_reaches_beta_devices_only_until_it_is_promoted_and_downloads_without_a_login()
    {
        const string board = "esp32dev";
        var beta = await DeviceOnAsync("beta", board);
        var stable = await DeviceOnAsync("stable", board);
        var image = TestFirmware.Image(board, size: 64 * 1024);

        var upload = await UploadAsync(image, TestFirmware.Manifest(image, _releaseKey, "0.3.0", board));

        Assert.Equal(HttpStatusCode.Redirect, upload.StatusCode);
        var payload = await beta.WaitForAsync(ManifestTopic("beta", board));
        Assert.NotNull(payload);
        var manifest = JsonNode.Parse(payload)!.AsObject();
        Assert.Equal(1, manifest["v"]!.GetValue<int>());
        Assert.Equal("0.3.0", manifest["version"]!.GetValue<string>());
        Assert.Equal(board, manifest["board"]!.GetValue<string>());
        Assert.Equal(image.Length, manifest["size"]!.GetValue<long>());
        Assert.Equal(Convert.ToHexStringLower(SHA256.HashData(image)), manifest["sha256"]!.GetValue<string>());
        var url = manifest["url"]!.GetValue<string>();
        Assert.StartsWith(PublicBaseUrl + "/firmware/yule-0.3.0-esp32dev-", url);

        // The signature in the manifest is the one the devices will check.
        Assert.True(_releaseKey.VerifyHash(SHA256.HashData(image), Convert.FromBase64String(manifest["sig"]!.GetValue<string>()), DSASignatureFormat.Rfc3279DerSequence));

        // A stable device is not offered a beta release.
        Assert.Null(await stable.WaitForAsync(ManifestTopic("stable", board), TimeSpan.FromSeconds(1)));

        // The image downloads from the address in the manifest, without logging in.
        var anonymous = _factory.CreateClient(new WebApplicationFactoryClientOptions { AllowAutoRedirect = false, BaseAddress = new Uri("https://localhost") });
        var download = await anonymous.GetAsync(new Uri(url).AbsolutePath);
        Assert.Equal(HttpStatusCode.OK, download.StatusCode);
        Assert.Equal(image, await download.Content.ReadAsByteArrayAsync());

        // Promotion offers it to stable devices too.
        int id;
        await using (var scope = _factory.Services.CreateAsyncScope())
        {
            id = await scope.ServiceProvider.GetRequiredService<Data.YuleDbContext>().FirmwareReleases.Select(r => r.Id).SingleAsync();
        }

        var promote = await _admin.PostFormAsync($"/releases?handler=Promote&id={id}", "/releases");
        Assert.Equal(HttpStatusCode.Redirect, promote.StatusCode);
        var promoted = await stable.WaitForAsync(ManifestTopic("stable", board));
        Assert.NotNull(promoted);
        Assert.Equal("0.3.0", JsonNode.Parse(promoted)!["version"]!.GetValue<string>());
        Assert.Equal(url, JsonNode.Parse(promoted)!["url"]!.GetValue<string>());

        // Retained: a device that connects later is offered the current release straight away.
        var late = await DeviceOnAsync("stable", board);
        Assert.NotNull(await late.WaitForAsync(ManifestTopic("stable", board)));
    }

    [Fact]
    public async Task Tampered_and_unsigned_uploads_publish_nothing()
    {
        const string board = "esp32-s3";
        var beta = await DeviceOnAsync("beta", board);
        var image = TestFirmware.Image(board);
        var manifest = TestFirmware.Manifest(image, _releaseKey, "0.4.0", board);

        var tampered = (byte[])image.Clone();
        tampered[100] ^= 0xFF;
        var refused = await (await UploadAsync(tampered, manifest)).Content.ReadAsStringAsync();
        Assert.Contains("nothing was published", refused);

        var unsigned = TestFirmware.ManifestJson("0.4.0", board, image.Length, Convert.ToHexStringLower(SHA256.HashData(image)), null);
        Assert.Contains("the image is unsigned", await (await UploadAsync(image, unsigned)).Content.ReadAsStringAsync());

        using var otherKey = ECDsa.Create(ECCurve.NamedCurves.nistP256);
        Assert.Contains("signature does not verify", await (await UploadAsync(image, TestFirmware.Manifest(image, otherKey, "0.4.0", board))).Content.ReadAsStringAsync());

        Assert.Null(await beta.WaitForAsync(ManifestTopic("beta", board), TimeSpan.FromSeconds(2)));
        Assert.Contains("No releases yet", await _admin.GetStringAsync("/releases"));
    }
}
