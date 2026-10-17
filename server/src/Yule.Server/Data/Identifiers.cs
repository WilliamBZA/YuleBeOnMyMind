using System.Security.Cryptography;
using System.Text.RegularExpressions;

namespace Yule.Server.Data;

/// <summary>Generation and validation of the ids and secrets used on the wire (see design D4 and D5).</summary>
public static partial class Identifiers
{
    // RFC 4648 base32 alphabet in lower case. Group ids are "g-" + 8 of these, which matches ^g-[a-z0-9]{8}$ on the device.
    private const string Base32 = "abcdefghijklmnopqrstuvwxyz234567";

    private const string PasswordAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";

    public const int BrokerPasswordLength = 24;

    [GeneratedRegex("^d-[0-9a-f]{12}$")]
    private static partial Regex DeviceIdPattern();

    [GeneratedRegex("^g-[a-z0-9]{8}$")]
    private static partial Regex GroupIdPattern();

    public static bool IsValidDeviceId(string? id) => id is not null && DeviceIdPattern().IsMatch(id);

    public static bool IsValidGroupId(string? id) => id is not null && GroupIdPattern().IsMatch(id);

    public static string NewGroupId() => "g-" + new string(RandomNumberGenerator.GetItems<char>(Base32, 8));

    /// <summary>A random 24-character broker password (about 138 bits). Look-alike characters are left out so it is easy to type on the device page.</summary>
    public static string NewBrokerPassword() =>
        new(RandomNumberGenerator.GetItems<char>(PasswordAlphabet, BrokerPasswordLength));

    public static bool IsValidChannel(string? channel) => channel is "stable" or "beta";

    /// <summary>Group names are 1-40 characters (Unicode code points), as the device enforces too.</summary>
    public static bool IsValidGroupName(string? name)
    {
        if (name is null)
        {
            return false;
        }

        var length = name.EnumerateRunes().Count();
        return length is >= 1 and <= 40 && name.Trim().Length > 0;
    }

    public static bool IsValidDeviceLabel(string? label) =>
        label is not null && label.Trim().Length is >= 1 and <= 60;
}
