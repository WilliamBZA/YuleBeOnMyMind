using System.Security.Cryptography;
using System.Text.Json;
using Yule.Server.Releases;

namespace Yule.Server.Tests;

/// <summary>
/// Stand-in firmware releases: an image with a real ESP32 image header (magic byte and chip id) and a manifest signed the
/// way <c>firmware/scripts/sign_firmware.py</c> signs one. Shared with the integration tests (linked in).
/// </summary>
public static class TestFirmware
{
    /// <summary>An image of <paramref name="size"/> bytes for <paramref name="board"/>; different seeds give different contents.</summary>
    public static byte[] Image(string board = "esp32dev", int size = 4096, int seed = 1)
    {
        var image = new byte[size];
        new Random(seed).NextBytes(image);
        image[0] = 0xE9; // ESP_IMAGE_HEADER_MAGIC
        var chip = ReleaseVerifier.Boards.GetValueOrDefault(board, (ushort)0xFFFF);
        image[12] = (byte)(chip & 0xFF);
        image[13] = (byte)(chip >> 8);
        return image;
    }

    /// <summary>ECDSA P-256 over the SHA-256 of the image, DER-encoded, as base64.</summary>
    public static string Sign(byte[] image, ECDsa key) =>
        Convert.ToBase64String(key.SignHash(SHA256.HashData(image), DSASignatureFormat.Rfc3279DerSequence));

    public static string Manifest(byte[] image, ECDsa key, string version = "0.3.0", string board = "esp32dev") =>
        ManifestJson(version, board, image.Length, Convert.ToHexStringLower(SHA256.HashData(image)), Sign(image, key));

    public static string ManifestJson(string version, string board, long size, string sha256, string? sig) =>
        JsonSerializer.Serialize(new Dictionary<string, object?>
        {
            ["v"] = 1,
            ["version"] = version,
            ["board"] = board,
            ["size"] = size,
            ["sha256"] = sha256,
            ["sig"] = sig,
        });
}
