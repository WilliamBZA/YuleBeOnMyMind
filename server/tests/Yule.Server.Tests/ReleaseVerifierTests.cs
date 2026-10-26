using System.Security.Cryptography;
using System.Text.Json.Nodes;
using Yule.Server.Releases;

namespace Yule.Server.Tests;

/// <summary>Checking uploaded releases (spec <c>firmware-updates</c>, "Publishing releases").</summary>
public sealed class ReleaseVerifierTests : IDisposable
{
    private readonly ECDsa _key = ECDsa.Create(ECCurve.NamedCurves.nistP256);
    private readonly ECDsa _otherKey = ECDsa.Create(ECCurve.NamedCurves.nistP256);

    public void Dispose()
    {
        _key.Dispose();
        _otherKey.Dispose();
    }

    private VerificationResult Verify(byte[] image, string manifest, params ECDsa[] keys) =>
        ReleaseVerifier.Verify(image, manifest, keys.Length == 0 ? [_key] : keys);

    private static string Edit(string manifest, Action<JsonObject> change)
    {
        var json = JsonNode.Parse(manifest)!.AsObject();
        change(json);
        return json.ToJsonString();
    }

    private static string Fixture(string name) => Path.Combine(AppContext.BaseDirectory, "Fixtures", "signed-release", name);

    private static (byte[] Image, string Manifest, ECDsa Key) SigningScriptOutput()
    {
        Assert.True(ReleaseKeyParser.TryParse(File.ReadAllText(Fixture("release-public-key.txt")), out var key, out var error), error);
        return (File.ReadAllBytes(Fixture("yule-0.0.1-esp32dev.bin")), File.ReadAllText(Fixture("yule-0.0.1-esp32dev.manifest.json")), key!);
    }

    [Fact]
    public void What_the_signing_script_writes_verifies()
    {
        var (image, manifest, key) = SigningScriptOutput();
        using (key)
        {
            var result = ReleaseVerifier.Verify(image, manifest, [key]);

            Assert.True(result.Succeeded, string.Join(" ", result.Errors));
            Assert.Equal("0.0.1", result.Release!.Version);
            Assert.Equal("esp32dev", result.Release.Board);
        }
    }

    [Fact]
    public void The_signing_script_output_with_one_byte_changed_fails()
    {
        var (image, manifest, key) = SigningScriptOutput();
        using (key)
        {
            for (var offset = 0; offset < image.Length; offset += 1021)
            {
                var changed = (byte[])image.Clone();
                changed[offset] ^= 0x01;
                Assert.False(ReleaseVerifier.Verify(changed, manifest, [key]).Succeeded, $"byte {offset} changed");
            }

            // Nor does it verify against any other key.
            Assert.False(ReleaseVerifier.Verify(image, manifest, [_key]).Succeeded);
        }
    }

    [Fact]
    public void A_correctly_signed_release_verifies()
    {
        var image = TestFirmware.Image();
        var result = Verify(image, TestFirmware.Manifest(image, _key));

        Assert.True(result.Succeeded, string.Join(" ", result.Errors));
        var release = result.Release!;
        Assert.Equal("0.3.0", release.Version);
        Assert.Equal("esp32dev", release.Board);
        Assert.Equal(image.Length, release.Size);
        Assert.Equal(Convert.ToHexStringLower(SHA256.HashData(image)), release.Sha256);
    }

    [Theory]
    [InlineData("esp32dev")]
    [InlineData("esp32-c3")]
    [InlineData("esp32-s3")]
    public void Every_board_is_accepted_with_an_image_for_its_chip(string board)
    {
        var image = TestFirmware.Image(board);
        Assert.True(Verify(image, TestFirmware.Manifest(image, _key, board: board)).Succeeded);
    }

    [Fact]
    public void A_tampered_image_is_rejected()
    {
        var image = TestFirmware.Image();
        var manifest = TestFirmware.Manifest(image, _key);
        image[2000] ^= 0x01;

        var result = Verify(image, manifest);

        Assert.False(result.Succeeded);
        Assert.Contains(result.Errors, e => e.Contains("SHA-256 does not match"));
        Assert.Contains(result.Errors, e => e.Contains("signature does not verify"));
    }

    [Fact]
    public void A_tampered_image_with_a_matching_digest_still_fails_the_signature()
    {
        // Someone swaps the image and fixes up the digest in the manifest, but cannot sign without the key.
        var image = TestFirmware.Image();
        var manifest = TestFirmware.Manifest(image, _key);
        var other = TestFirmware.Image(seed: 2);
        var forged = Edit(manifest, m => m["sha256"] = Convert.ToHexStringLower(SHA256.HashData(other)));

        var result = Verify(other, forged);

        Assert.False(result.Succeeded);
        Assert.Equal(["The signature does not verify against the configured release public keys. Was it signed with the current release key?"], result.Errors);
    }

    [Fact]
    public void An_unsigned_release_is_rejected()
    {
        var image = TestFirmware.Image();
        var sha = Convert.ToHexStringLower(SHA256.HashData(image));
        foreach (var manifest in new[]
                 {
                     TestFirmware.ManifestJson("0.3.0", "esp32dev", image.Length, sha, null),
                     TestFirmware.ManifestJson("0.3.0", "esp32dev", image.Length, sha, ""),
                     Edit(TestFirmware.Manifest(image, _key), m => m.Remove("sig")),
                 })
        {
            var result = Verify(image, manifest);
            Assert.False(result.Succeeded);
            Assert.Contains(result.Errors, e => e.Contains("unsigned"));
        }
    }

    [Fact]
    public void A_release_signed_with_another_key_is_rejected()
    {
        var image = TestFirmware.Image();
        var result = Verify(image, TestFirmware.Manifest(image, _otherKey));

        Assert.False(result.Succeeded);
        Assert.Contains(result.Errors, e => e.Contains("signature does not verify"));
    }

    [Fact]
    public void Either_of_two_configured_keys_verifies_so_keys_can_be_rotated()
    {
        var image = TestFirmware.Image();
        Assert.True(Verify(image, TestFirmware.Manifest(image, _otherKey), _key, _otherKey).Succeeded);
        Assert.True(Verify(image, TestFirmware.Manifest(image, _key), _key, _otherKey).Succeeded);
    }

    [Fact]
    public void A_garbage_signature_is_rejected_without_throwing()
    {
        var image = TestFirmware.Image();
        var manifest = TestFirmware.Manifest(image, _key);
        Assert.False(Verify(image, Edit(manifest, m => m["sig"] = Convert.ToBase64String(new byte[64]))).Succeeded);
        Assert.False(Verify(image, Edit(manifest, m => m["sig"] = "not base64 at all!")).Succeeded);
        Assert.False(Verify(image, Edit(manifest, m => m["sig"] = Convert.ToBase64String(new byte[200]))).Succeeded);
    }

    [Fact]
    public void An_image_for_another_chip_is_rejected()
    {
        var image = TestFirmware.Image("esp32-c3");
        var result = Verify(image, TestFirmware.Manifest(image, _key, board: "esp32dev"));

        Assert.False(result.Succeeded);
        Assert.Contains(result.Errors, e => e.Contains("different chip than board esp32dev"));
    }

    [Fact]
    public void An_unknown_board_is_rejected()
    {
        var image = TestFirmware.Image();
        var result = Verify(image, TestFirmware.Manifest(image, _key, board: "esp8266"));

        Assert.False(result.Succeeded);
        Assert.Contains(result.Errors, e => e.Contains("board must be one of esp32dev, esp32-c3, esp32-s3"));
    }

    [Fact]
    public void A_size_that_differs_from_the_manifest_is_rejected()
    {
        var image = TestFirmware.Image();
        var manifest = TestFirmware.Manifest(image, _key);
        var result = Verify(image[..^16], manifest);

        Assert.False(result.Succeeded);
        Assert.Contains(result.Errors, e => e.Contains("but the manifest says 4,096"));
    }

    [Fact]
    public void Something_that_is_not_an_esp32_image_is_rejected()
    {
        var image = TestFirmware.Image();
        image[0] = 0x7F; // an ELF file, say
        var result = Verify(image, TestFirmware.Manifest(image, _key));

        Assert.False(result.Succeeded);
        Assert.Contains(result.Errors, e => e.Contains("not an ESP32 firmware image"));
    }

    [Fact]
    public void An_image_larger_than_an_app_slot_is_rejected()
    {
        var image = TestFirmware.Image(size: (int)ReleaseVerifier.MaxImageBytes + 1);
        var result = Verify(image, TestFirmware.Manifest(image, _key));

        Assert.False(result.Succeeded);
        Assert.Contains(result.Errors, e => e.Contains("more than an app slot holds"));
    }

    [Theory]
    [InlineData("")]
    [InlineData("not json")]
    [InlineData("[1,2,3]")]
    [InlineData("{\"v\":2,\"version\":\"0.3.0\",\"board\":\"esp32dev\",\"size\":4096,\"sha256\":\"00\",\"sig\":\"AA==\"}")]
    [InlineData("{\"v\":1,\"version\":\"0.3\",\"board\":\"esp32dev\",\"size\":4096}")]
    [InlineData("{\"v\":1,\"version\":\"0.3.0-beta\",\"board\":\"esp32dev\",\"size\":-1}")]
    public void A_malformed_manifest_is_rejected(string manifest)
    {
        var result = Verify(TestFirmware.Image(), manifest);
        Assert.False(result.Succeeded);
        Assert.NotEmpty(result.Errors);
    }

    [Fact]
    public void Unknown_manifest_fields_are_ignored_and_the_digest_may_be_upper_case()
    {
        var image = TestFirmware.Image();
        var manifest = Edit(TestFirmware.Manifest(image, _key), m =>
        {
            m["url"] = "https://elsewhere.example/firmware.bin";
            m["builtBy"] = "ci";
            m["sha256"] = m["sha256"]!.GetValue<string>().ToUpperInvariant();
        });

        var result = Verify(image, manifest);

        Assert.True(result.Succeeded, string.Join(" ", result.Errors));
        Assert.Equal(result.Release!.Sha256, result.Release.Sha256.ToLowerInvariant());
    }

    [Fact]
    public void Without_any_configured_key_nothing_verifies()
    {
        var image = TestFirmware.Image();
        var result = ReleaseVerifier.Verify(image, TestFirmware.Manifest(image, _key), []);

        Assert.False(result.Succeeded);
        Assert.Contains(result.Errors, e => e.Contains("No release public key is configured"));
    }

    [Fact]
    public void Download_names_are_content_addressed_and_nothing_else_is_served()
    {
        var name = FirmwareManifests.FileName("0.3.0", "esp32-c3", new string('a', 64));
        Assert.Equal("yule-0.3.0-esp32-c3-aaaaaaaaaaaa.bin", name);
        Assert.True(FirmwareManifests.IsFileName(name));
        foreach (var bad in new[] { "../yule.db", "yule-0.3.0-esp32dev.bin", "yule-0.3.0-esp32dev-aaaaaaaaaaaa.bin.tmp", "keys", "", "yule-0.3.0-ESP32-aaaaaaaaaaaa.bin" })
        {
            Assert.False(FirmwareManifests.IsFileName(bad), bad);
        }
    }

    [Fact]
    public void The_published_manifest_is_what_the_firmware_reads()
    {
        var release = new Data.FirmwareRelease { Version = "0.3.0", Board = "esp32dev", Size = 1534016, Sha256 = new string('b', 64), Signature = "MEUCIQ==", FileName = "f.bin" };
        var json = JsonNode.Parse(FirmwareManifests.Payload(release, FirmwareManifests.DownloadUrl("https://yule.williamb.net/", "f.bin")))!.AsObject();

        Assert.Equal(1, json["v"]!.GetValue<int>());
        Assert.Equal("0.3.0", json["version"]!.GetValue<string>());
        Assert.Equal("esp32dev", json["board"]!.GetValue<string>());
        Assert.Equal(1534016, json["size"]!.GetValue<long>());
        Assert.Equal(new string('b', 64), json["sha256"]!.GetValue<string>());
        Assert.Equal("MEUCIQ==", json["sig"]!.GetValue<string>());
        Assert.Equal("https://yule.williamb.net/firmware/f.bin", json["url"]!.GetValue<string>());
        Assert.Equal("yule/v1/firmware/beta/esp32dev", FirmwareManifests.Topic("yule/v1", "beta", "esp32dev"));
    }
}
