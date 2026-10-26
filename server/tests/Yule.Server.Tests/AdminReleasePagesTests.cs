using System.Net;
using System.Text;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.DependencyInjection;
using Yule.Server.Releases;

namespace Yule.Server.Tests;

/// <summary>The Releases page and the firmware downloads (spec <c>firmware-updates</c>, "Publishing releases" and "Release channels").</summary>
public class AdminReleasePagesTests
{
    private static Task<HttpResponseMessage> UploadAsync(AdminClient client, byte[] image, string manifest, string version = "0.3.0", string board = "esp32dev") =>
        client.PostFilesAsync("/releases?handler=Upload", "/releases",
            ("Image", $"yule-{version}-{board}.bin", image),
            ("Manifest", $"yule-{version}-{board}.manifest.json", Encoding.UTF8.GetBytes(manifest)));

    private static (byte[] Image, string Manifest) SignedRelease(string version = "0.3.0", string board = "esp32dev", int seed = 1)
    {
        var image = TestFirmware.Image(board, seed: seed);
        return (image, TestFirmware.Manifest(image, AdminSite.ReleaseKey, version, board));
    }

    [Fact]
    public async Task A_valid_upload_is_stored_and_published_to_beta()
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();
        var (image, manifest) = SignedRelease();

        var response = await UploadAsync(client, image, manifest);

        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        var release = await site.WithDbAsync(db => db.FirmwareReleases.SingleAsync());
        Assert.Equal("0.3.0", release.Version);
        Assert.Equal("esp32dev", release.Board);
        Assert.Equal(image.Length, release.Size);
        Assert.NotNull(release.BetaPublishedUtc);
        Assert.Null(release.StablePublishedUtc);
        Assert.StartsWith("yule-0.3.0-esp32dev-", release.FileName);
        Assert.Equal(image, await File.ReadAllBytesAsync(Path.Combine(site.DataDirectory, "firmware", release.FileName)));
        Assert.Equal([("beta", "esp32dev", "0.3.0", release.FileName)], site.Releases.Published);

        var page = await client.GetStringAsync("/releases");
        Assert.Contains("Uploaded 0.3.0 for esp32dev and published it to beta", page);
        Assert.Contains("current beta", page);
    }

    [Fact]
    public async Task A_tampered_image_is_rejected_and_nothing_is_stored_or_published()
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();
        var (image, manifest) = SignedRelease();
        image[1234] ^= 0x40;

        var response = await UploadAsync(client, image, manifest);
        var html = await response.Content.ReadAsStringAsync();

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.Contains("was not uploaded and nothing was published", html);
        Assert.Contains("SHA-256 does not match", html);
        Assert.Empty(await site.WithDbAsync(db => db.FirmwareReleases.ToListAsync()));
        Assert.Empty(site.Releases.Published);
        Assert.False(Directory.Exists(Path.Combine(site.DataDirectory, "firmware")) &&
                     Directory.EnumerateFiles(Path.Combine(site.DataDirectory, "firmware")).Any());
    }

    [Fact]
    public async Task An_unsigned_upload_is_rejected()
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();
        var image = TestFirmware.Image();
        var manifest = TestFirmware.ManifestJson("0.3.0", "esp32dev", image.Length, Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(image)), null);

        var html = await (await UploadAsync(client, image, manifest)).Content.ReadAsStringAsync();

        Assert.Contains("the image is unsigned", html);
        Assert.Empty(site.Releases.Published);
    }

    [Fact]
    public async Task A_release_signed_with_an_unknown_key_is_rejected()
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();
        using var stranger = System.Security.Cryptography.ECDsa.Create(System.Security.Cryptography.ECCurve.NamedCurves.nistP256);
        var image = TestFirmware.Image();

        var html = await (await UploadAsync(client, image, TestFirmware.Manifest(image, stranger))).Content.ReadAsStringAsync();

        Assert.Contains("signature does not verify against the configured release public keys", html);
        Assert.Empty(site.Releases.Published);
    }

    [Fact]
    public async Task Missing_files_are_explained()
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();

        var response = await client.PostFilesAsync("/releases?handler=Upload", "/releases");
        var html = await response.Content.ReadAsStringAsync();

        Assert.Contains("Choose the signed firmware image", html);
        Assert.Contains("Choose its manifest", html);
    }

    [Fact]
    public async Task The_same_version_cannot_be_uploaded_twice()
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();
        var (image, manifest) = SignedRelease();
        await UploadAsync(client, image, manifest);
        var (rebuilt, rebuiltManifest) = SignedRelease(seed: 2);

        var html = await (await UploadAsync(client, rebuilt, rebuiltManifest)).Content.ReadAsStringAsync();

        Assert.Contains("Version 0.3.0 for esp32dev was already uploaded", html);
        Assert.Single(await site.WithDbAsync(db => db.FirmwareReleases.ToListAsync()));
        Assert.Single(site.Releases.Published);
    }

    [Fact]
    public async Task Promoting_publishes_to_stable()
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();
        var (image, manifest) = SignedRelease();
        await UploadAsync(client, image, manifest);
        var id = await site.WithDbAsync(db => db.FirmwareReleases.Select(r => r.Id).SingleAsync());

        var response = await client.PostFormAsync($"/releases?handler=Promote&id={id}", "/releases");

        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        var release = await site.WithDbAsync(db => db.FirmwareReleases.SingleAsync());
        Assert.NotNull(release.StablePublishedUtc);
        Assert.Equal(("stable", "esp32dev", "0.3.0", release.FileName), site.Releases.Published[^1]);
        var page = await client.GetStringAsync("/releases");
        Assert.Contains("Promoted 0.3.0 for esp32dev to stable", page);
        Assert.Contains("current stable", page);
    }

    [Fact]
    public async Task The_current_release_per_board_and_channel_is_the_latest_published_there()
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();
        foreach (var (version, seed) in new[] { ("0.3.0", 1), ("0.3.1", 2) })
        {
            var (image, manifest) = SignedRelease(version, seed: seed);
            await UploadAsync(client, image, manifest, version);
            site.Time.Advance(TimeSpan.FromMinutes(1));
        }

        var (c3, c3Manifest) = SignedRelease("0.3.0", "esp32-c3", seed: 3);
        await UploadAsync(client, c3, c3Manifest, "0.3.0", "esp32-c3");
        var first = await site.WithDbAsync(db => db.FirmwareReleases.SingleAsync(r => r.Version == "0.3.0" && r.Board == "esp32dev"));
        await client.PostFormAsync($"/releases?handler=Promote&id={first.Id}", "/releases");

        await using var scope = site.Services.CreateAsyncScope();
        var service = scope.ServiceProvider.GetRequiredService<Admin.ReleaseAdminService>();
        var current = Admin.ReleaseAdminService.Current(await service.ListAsync());
        Assert.Equal("0.3.1", current.Single(c => c is { Board: "esp32dev", Channel: "beta" }).Release?.Version);
        Assert.Equal("0.3.0", current.Single(c => c is { Board: "esp32dev", Channel: "stable" }).Release?.Version);
        Assert.Equal("0.3.0", current.Single(c => c is { Board: "esp32-c3", Channel: "beta" }).Release?.Version);
        Assert.Null(current.Single(c => c is { Board: "esp32-s3", Channel: "beta" }).Release);

        // After a broker reconnection every current release is announced again.
        var before = site.Releases.Published.Count;
        Assert.Equal(3, await service.RepublishCurrentAsync());
        Assert.Equal(
            [("beta", "esp32-c3", "0.3.0"), ("beta", "esp32dev", "0.3.1"), ("stable", "esp32dev", "0.3.0")],
            site.Releases.Published.Skip(before).Select(p => (p.Channel, p.Board, p.Version)).ToList());
    }

    [Fact]
    public async Task When_the_broker_is_down_the_release_is_saved_with_a_warning()
    {
        using var site = new AdminSite(brokerTimeout: TimeSpan.FromMilliseconds(200));
        site.Releases.Behaviour = ct => Task.Delay(Timeout.Infinite, ct);
        var client = await site.LoggedInClientAsync();
        var (image, manifest) = SignedRelease();

        var response = await UploadAsync(client, image, manifest);

        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        Assert.Single(await site.WithDbAsync(db => db.FirmwareReleases.ToListAsync()));
        Assert.Contains("devices could not be told about it just now", await client.GetStringAsync("/releases"));
    }

    [Fact]
    public async Task Published_images_download_without_a_login_and_are_cacheable()
    {
        using var site = new AdminSite();
        var admin = await site.LoggedInClientAsync();
        var (image, manifest) = SignedRelease();
        await UploadAsync(admin, image, manifest);
        var fileName = await site.WithDbAsync(db => db.FirmwareReleases.Select(r => r.FileName).SingleAsync());

        var device = site.NewClient(); // no login
        var response = await device.GetAsync("/firmware/" + fileName);

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.Equal(image, await response.Content.ReadAsByteArrayAsync());
        Assert.Equal("application/octet-stream", response.Content.Headers.ContentType?.MediaType);
        Assert.Equal(image.Length, response.Content.Headers.ContentLength);
        Assert.Contains("immutable", response.Headers.CacheControl?.ToString());
    }

    [Theory]
    [InlineData("/firmware/yule-9.9.9-esp32dev-aaaaaaaaaaaa.bin")]
    [InlineData("/firmware/yule.db")]
    [InlineData("/firmware/..%2Fyule.db")]
    [InlineData("/firmware/keys")]
    public async Task Nothing_else_is_served_from_the_firmware_path(string path)
    {
        using var site = new AdminSite();
        var response = await site.NewClient().GetAsync(path);

        Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
        Assert.Equal("no-store", response.Headers.CacheControl?.ToString());
    }

    [Fact]
    public async Task The_releases_page_requires_the_administrator()
    {
        using var site = new AdminSite();
        var response = await site.NewClient().GetAsync("/releases");

        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        Assert.StartsWith("/login", AdminClient.Location(response));
    }

    [Fact]
    public void The_upload_limit_fits_a_full_app_slot() =>
        Assert.True(Pages.Releases.IndexModel.MaxUploadBytes > ReleaseVerifier.MaxImageBytes + 64 * 1024);
}
