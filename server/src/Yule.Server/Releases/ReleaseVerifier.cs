using System.Globalization;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace Yule.Server.Releases;

/// <summary>A release that passed every check: its image is what the manifest describes, signed with a release key.</summary>
public sealed record VerifiedRelease(string Version, string Board, long Size, string Sha256, string Signature);

/// <summary>Either a verified release, or every reason the upload was refused.</summary>
public sealed record VerificationResult(VerifiedRelease? Release, IReadOnlyList<string> Errors)
{
    public bool Succeeded => Release is not null;
}

/// <summary>
/// Checks an uploaded firmware image against its manifest (spec <c>firmware-updates</c>, "Publishing releases"). The
/// manifest is what <c>firmware/scripts/sign_firmware.py</c> writes:
/// <c>{"v":1,"version":"0.3.0","board":"esp32dev","size":1534016,"sha256":"&lt;hex&gt;","sig":"&lt;base64 DER&gt;"}</c>.
/// The signature is ECDSA P-256 over the SHA-256 of the image, the same check the devices make before installing.
/// </summary>
public static partial class ReleaseVerifier
{
    /// <summary>One app slot in <c>firmware/partitions.csv</c> (0x1E0000 bytes). A larger image cannot be installed.</summary>
    public const long MaxImageBytes = 0x1E0000;

    /// <summary>A DER ECDSA P-256 signature is at most 72 bytes.</summary>
    private const int MaxSignatureBytes = 80;

    /// <summary>The boards a release can be for, with the chip id each image's header must carry (ESP-IDF <c>esp_chip_id_t</c>).</summary>
    public static readonly IReadOnlyDictionary<string, ushort> Boards = new Dictionary<string, ushort>(StringComparer.Ordinal)
    {
        ["esp32dev"] = 0x0000,
        ["esp32-c3"] = 0x0005,
        ["esp32-s3"] = 0x0009,
    };

    [GeneratedRegex("^[0-9]{1,4}\\.[0-9]{1,4}\\.[0-9]{1,4}$")]
    private static partial Regex SemVer();

    [GeneratedRegex("^[0-9a-fA-F]{64}$")]
    private static partial Regex Sha256Hex();

    public static VerificationResult Verify(ReadOnlySpan<byte> image, string? manifestJson, IReadOnlyList<ECDsa> releaseKeys)
    {
        var errors = new List<string>();
        if (!TryReadManifest(manifestJson, errors, out var manifest))
        {
            return new VerificationResult(null, errors);
        }

        if (image.Length == 0)
        {
            errors.Add("The image file is empty.");
            return new VerificationResult(null, errors);
        }

        if (image.Length != manifest.Size)
        {
            errors.Add(string.Create(CultureInfo.InvariantCulture,
                $"The image is {image.Length:N0} bytes but the manifest says {manifest.Size:N0}. Upload the .bin and the .manifest.json from the same build."));
        }

        if (image.Length > MaxImageBytes)
        {
            errors.Add(string.Create(CultureInfo.InvariantCulture, $"The image is {image.Length:N0} bytes, more than an app slot holds ({MaxImageBytes:N0})."));
        }

        if (image[0] != 0xE9 || image.Length < 24)
        {
            errors.Add("This is not an ESP32 firmware image (upload the signed .bin that sign_firmware.py wrote).");
        }
        else
        {
            var chip = (ushort)(image[12] | (image[13] << 8));
            if (Boards.TryGetValue(manifest.Board, out var expected) && chip != expected)
            {
                errors.Add($"The image was built for a different chip than board {manifest.Board} (chip id 0x{chip:X4}, expected 0x{expected:X4}).");
            }
        }

        var digest = SHA256.HashData(image);
        var digestHex = Convert.ToHexStringLower(digest);
        if (!string.Equals(digestHex, manifest.Sha256, StringComparison.Ordinal))
        {
            errors.Add("The image's SHA-256 does not match the manifest: the image was changed after it was signed, or the two files come from different builds.");
        }

        // The signature must cover the image actually uploaded, whatever the manifest claims.
        if (!SignatureVerifies(digest, manifest.SignatureBytes, releaseKeys))
        {
            errors.Add(releaseKeys.Count == 0
                ? "No release public key is configured, so no signature can be checked."
                : "The signature does not verify against the configured release public keys. Was it signed with the current release key?");
        }

        return errors.Count > 0
            ? new VerificationResult(null, errors)
            : new VerificationResult(new VerifiedRelease(manifest.Version, manifest.Board, manifest.Size, digestHex, manifest.Signature), errors);
    }

    private sealed record Manifest(string Version, string Board, long Size, string Sha256, string Signature, byte[] SignatureBytes);

    private static bool TryReadManifest(string? json, List<string> errors, out Manifest manifest)
    {
        manifest = null!;
        if (string.IsNullOrWhiteSpace(json))
        {
            errors.Add("The manifest is missing. Upload the .manifest.json written next to the signed image.");
            return false;
        }

        JsonDocument document;
        try
        {
            document = JsonDocument.Parse(json);
        }
        catch (JsonException)
        {
            errors.Add("The manifest is not valid JSON.");
            return false;
        }

        using (document)
        {
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object)
            {
                errors.Add("The manifest is not a JSON object.");
                return false;
            }

            // Unknown fields (a "url", for instance) are ignored: the server sets the download address itself.
            if (!root.TryGetProperty("v", out var v) || v.ValueKind != JsonValueKind.Number || !v.TryGetInt32(out var vNumber) || vNumber != 1)
            {
                errors.Add("The manifest's format version (\"v\") must be 1.");
            }

            var version = Text(root, "version");
            if (version is null || !SemVer().IsMatch(version))
            {
                errors.Add("The manifest's version must be MAJOR.MINOR.PATCH, for example 0.3.0.");
            }

            var board = Text(root, "board");
            if (board is null || !Boards.ContainsKey(board))
            {
                errors.Add($"The manifest's board must be one of {string.Join(", ", Boards.Keys)}.");
            }

            long size = 0;
            if (!root.TryGetProperty("size", out var sizeElement) || sizeElement.ValueKind != JsonValueKind.Number || !sizeElement.TryGetInt64(out size) || size <= 0)
            {
                errors.Add("The manifest's size must be a positive number of bytes.");
            }

            var sha256 = Text(root, "sha256");
            if (sha256 is null || !Sha256Hex().IsMatch(sha256))
            {
                errors.Add("The manifest's sha256 must be 64 hex digits.");
            }

            var signature = Text(root, "sig");
            byte[]? signatureBytes = null;
            if (string.IsNullOrEmpty(signature))
            {
                errors.Add("The manifest has no signature (\"sig\"): the image is unsigned. Sign it with sign_firmware.py.");
            }
            else
            {
                try
                {
                    signatureBytes = Convert.FromBase64String(signature);
                }
                catch (FormatException)
                {
                }

                if (signatureBytes is null || signatureBytes.Length is 0 or > MaxSignatureBytes)
                {
                    errors.Add("The manifest's signature (\"sig\") is not a base64 ECDSA signature.");
                }
            }

            if (errors.Count > 0)
            {
                return false;
            }

            manifest = new Manifest(version!, board!, size, sha256!.ToLower(CultureInfo.InvariantCulture), signature!, signatureBytes!);
            return true;
        }
    }

    private static string? Text(JsonElement root, string name) =>
        root.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String ? value.GetString() : null;

    private static bool SignatureVerifies(byte[] digest, byte[] signature, IReadOnlyList<ECDsa> keys)
    {
        foreach (var key in keys)
        {
            try
            {
                if (key.VerifyHash(digest, signature, DSASignatureFormat.Rfc3279DerSequence))
                {
                    return true;
                }
            }
            catch (CryptographicException)
            {
                // A malformed signature is simply one that does not verify.
            }
        }

        return false;
    }
}
