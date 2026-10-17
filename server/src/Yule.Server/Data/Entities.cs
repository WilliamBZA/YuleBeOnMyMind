namespace Yule.Server.Data;

/// <summary>A registered decoration. The id comes from the device's hardware (d-&lt;12 hex digits&gt;).</summary>
public class Device
{
    public string Id { get; set; } = "";

    public string Label { get; set; } = "";

    /// <summary>"stable" or "beta".</summary>
    public string Channel { get; set; } = "stable";

    /// <summary>The broker password, encrypted with ASP.NET Data Protection. Never shown again after registration.</summary>
    public string EncryptedPassword { get; set; } = "";

    public DateTime CreatedUtc { get; set; } = DateTime.UtcNow;

    public List<Membership> Memberships { get; set; } = new();

    public DeviceStatus? Status { get; set; }
}

/// <summary>A family group. Names need not be unique; the id is what identifies the group.</summary>
public class Group
{
    public string Id { get; set; } = "";

    public string Name { get; set; } = "";

    public DateTime CreatedUtc { get; set; } = DateTime.UtcNow;

    public List<Membership> Memberships { get; set; } = new();
}

public class Membership
{
    public string DeviceId { get; set; } = "";

    public string GroupId { get; set; } = "";

    public Device Device { get; set; } = null!;

    public Group Group { get; set; } = null!;
}

/// <summary>A signed firmware image. It is the current release of a channel while it is the latest one published there.</summary>
public class FirmwareRelease
{
    public int Id { get; set; }

    public string Version { get; set; } = "";

    public string Board { get; set; } = "";

    public string Sha256 { get; set; } = "";

    public long Size { get; set; }

    /// <summary>Base64 of the DER-encoded ECDSA signature.</summary>
    public string Signature { get; set; } = "";

    /// <summary>File name of the image in the firmware directory (content-addressed, immutable).</summary>
    public string FileName { get; set; } = "";

    public DateTime UploadedUtc { get; set; } = DateTime.UtcNow;

    /// <summary>When it was published to the beta channel, or null if it never was.</summary>
    public DateTime? BetaPublishedUtc { get; set; }

    /// <summary>When it was promoted to the stable channel, or null if it has not been.</summary>
    public DateTime? StablePublishedUtc { get; set; }
}

/// <summary>The last status a device reported on its status topic.</summary>
public class DeviceStatus
{
    public string DeviceId { get; set; } = "";

    /// <summary>"online" or "offline".</summary>
    public string State { get; set; } = "offline";

    public string? FirmwareVersion { get; set; }

    public string? Board { get; set; }

    public string? Name { get; set; }

    /// <summary>"none", "ok", "failed:&lt;reason&gt;" or "rolled-back:&lt;version&gt;".</summary>
    public string? OtaResult { get; set; }

    public DateTime LastSeenUtc { get; set; } = DateTime.UtcNow;

    public Device Device { get; set; } = null!;
}
