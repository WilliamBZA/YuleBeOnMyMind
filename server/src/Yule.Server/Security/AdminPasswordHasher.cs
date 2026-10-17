using System.Security.Cryptography;

namespace Yule.Server.Security;

/// <summary>
/// PBKDF2-SHA256 hashing for the single administrator password. The stored form is
/// <c>PBKDF2-SHA256.iterations.salt.hash</c> (base64 for the last two; no "$", which Docker Compose would treat as interpolation), so the work factor can be raised later
/// and old hashes still verify.
/// </summary>
public static class AdminPasswordHasher
{
    private const string Scheme = "PBKDF2-SHA256";
    private const int DefaultIterations = 600_000;
    private const int SaltBytes = 16;
    private const int HashBytes = 32;

    public static string Hash(string password, int iterations = DefaultIterations)
    {
        ArgumentException.ThrowIfNullOrEmpty(password);
        var salt = RandomNumberGenerator.GetBytes(SaltBytes);
        var hash = Rfc2898DeriveBytes.Pbkdf2(password, salt, iterations, HashAlgorithmName.SHA256, HashBytes);
        return $"{Scheme}.{iterations}.{Convert.ToBase64String(salt)}.{Convert.ToBase64String(hash)}";
    }

    public static bool Verify(string password, string stored)
    {
        if (!TryParse(stored, out var iterations, out var salt, out var expected) || string.IsNullOrEmpty(password))
        {
            return false;
        }

        var actual = Rfc2898DeriveBytes.Pbkdf2(password, salt, iterations, HashAlgorithmName.SHA256, expected.Length);
        return CryptographicOperations.FixedTimeEquals(actual, expected);
    }

    public static bool IsValidHash(string stored) => TryParse(stored, out _, out _, out _);

    private static bool TryParse(string stored, out int iterations, out byte[] salt, out byte[] hash)
    {
        iterations = 0;
        salt = [];
        hash = [];
        var parts = stored.Split('.');
        if (parts.Length != 4 || parts[0] != Scheme || !int.TryParse(parts[1], out iterations) || iterations < 1_000
            || iterations > 10_000_000)
        {
            return false;
        }

        try
        {
            salt = Convert.FromBase64String(parts[2]);
            hash = Convert.FromBase64String(parts[3]);
        }
        catch (FormatException)
        {
            return false;
        }

        return salt.Length >= 8 && hash.Length >= 16;
    }
}
