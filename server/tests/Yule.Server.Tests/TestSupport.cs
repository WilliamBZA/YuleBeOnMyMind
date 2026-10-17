using System.Security.Cryptography;
using Yule.Server.Configuration;
using Yule.Server.Security;

namespace Yule.Server.Tests;

internal static class TestSupport
{
    /// <summary>A fresh ECDSA P-256 key pair: the private key signs test releases, the PEM public key goes into configuration.</summary>
    public static (ECDsa Private, string PublicPem) NewReleaseKey()
    {
        var key = ECDsa.Create(ECCurve.NamedCurves.nistP256);
        return (key, key.ExportSubjectPublicKeyInfoPem());
    }

    /// <summary>A complete, valid configuration. Tests remove or break one value at a time.</summary>
    public static YuleOptions ValidOptions(string? dataDirectory = null) => new()
    {
        Admin = new AdminOptions { PasswordHash = AdminPasswordHasher.Hash("correct horse battery staple", iterations: 2_000) },
        Broker = new BrokerOptions { Host = "mosquitto", Port = 1883, Username = "yule-server", Password = "server-secret" },
        PublicBaseUrl = "https://yule.example.org",
        ReleasePublicKeys = [NewReleaseKey().PublicPem],
        DataDirectory = dataDirectory ?? Path.Combine(Path.GetTempPath(), "yule-tests"),
    };

    public static string NewTempDirectory()
    {
        var path = Path.Combine(Path.GetTempPath(), "yule-tests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(path);
        return path;
    }
}
