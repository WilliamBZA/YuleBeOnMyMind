using Yule.Server.Data;

namespace Yule.Server.Admin;

/// <summary>What the state badge partial needs: a device's last reported status (null: never reported) and the current time.</summary>
public sealed record DeviceStateModel(DeviceStatus? Status, DateTime NowUtc);

/// <summary>Small formatting helpers for the admin pages. All times are UTC; SQLite hands them back without a kind.</summary>
public static class Display
{
    public static string Utc(DateTime utc) => utc.ToString("yyyy-MM-dd HH:mm 'UTC'", System.Globalization.CultureInfo.InvariantCulture);

    public static string Ago(DateTime utc, DateTime nowUtc)
    {
        var age = nowUtc - utc;
        if (age < TimeSpan.FromSeconds(45))
        {
            return "just now";
        }

        if (age < TimeSpan.FromMinutes(90))
        {
            return $"{Math.Max(1, (int)Math.Round(age.TotalMinutes))} min ago";
        }

        if (age < TimeSpan.FromHours(36))
        {
            return $"{(int)Math.Round(age.TotalHours)} h ago";
        }

        return $"{(int)Math.Round(age.TotalDays)} days ago";
    }

    public static string Plural(int count, string singular, string? plural = null) =>
        count == 1 ? $"1 {singular}" : $"{count} {plural ?? singular + "s"}";
}
