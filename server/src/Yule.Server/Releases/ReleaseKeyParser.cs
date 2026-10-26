using System.Security.Cryptography;

namespace Yule.Server.Releases;

/// <summary>Parses the release-signing public keys from configuration (PEM, ECDSA on curve P-256).</summary>
public static class ReleaseKeyParser
{
    private const string P256Oid = "1.2.840.10045.3.1.7";

    /// <summary>
    /// Accepts a PEM public key. Newlines may be written as the two characters "\n", which is how a multi-line
    /// value is usually passed through an environment variable.
    /// </summary>
    public static bool TryParse(string pem, out ECDsa? key, out string error)
    {
        key = null;
        error = "";
        var normalised = pem.Replace("\\r\\n", "\n").Replace("\\n", "\n").Trim();
        try
        {
            var candidate = ECDsa.Create();
            candidate.ImportFromPem(normalised);
            var curve = candidate.ExportParameters(false).Curve;
            if (curve.Oid?.Value != P256Oid && curve.Oid?.FriendlyName is not ("nistP256" or "ECDSA_P256"))
            {
                candidate.Dispose();
                error = "the key is not on curve P-256 (generate it with `openssl ecparam -name prime256v1 -genkey`).";
                return false;
            }

            key = candidate;
            return true;
        }
        catch (Exception ex) when (ex is CryptographicException or ArgumentException)
        {
            error = "it is not a valid PEM public key.";
            return false;
        }
    }
}
