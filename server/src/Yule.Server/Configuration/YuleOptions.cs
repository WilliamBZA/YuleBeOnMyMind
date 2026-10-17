namespace Yule.Server.Configuration;

/// <summary>
/// Typed server configuration, bound from the "Yule" section. In deployment every value comes from environment
/// variables or secrets (for example <c>Yule__Admin__PasswordHash</c>). The server refuses to start if a
/// required value is missing; see <see cref="YuleOptionsValidator"/>.
/// </summary>
public sealed class YuleOptions
{
    public const string SectionName = "Yule";

    public AdminOptions Admin { get; set; } = new();

    public BrokerOptions Broker { get; set; } = new();

    /// <summary>Public address of this server, used to build firmware download URLs (for example https://yule.williamb.net).</summary>
    public string PublicBaseUrl { get; set; } = "";

    /// <summary>PEM public keys (ECDSA P-256) that firmware releases must be signed with. One or two, so keys can be rotated.</summary>
    public List<string> ReleasePublicKeys { get; set; } = new();

    /// <summary>Directory for the SQLite database, firmware images and the Data Protection key ring. A volume in Docker.</summary>
    public string DataDirectory { get; set; } = "";

    /// <summary>Root of the MQTT topic tree. Must match the firmware's YULE_TOPIC_ROOT.</summary>
    public string TopicRoot { get; set; } = "yule/v1";

    /// <summary>How often the broker's permission state is checked and repaired, in minutes.</summary>
    public int ReconcileIntervalMinutes { get; set; } = 10;

    /// <summary>
    /// Believe the X-Forwarded-For and X-Forwarded-Proto headers of any caller. Turn this on only when every request
    /// comes through the reverse proxy (the Compose file publishes the port on loopback only and sets it); anyone who
    /// can reach the server directly could otherwise forge their address and scheme.
    /// </summary>
    public bool TrustForwardedHeaders { get; set; }
}

public sealed class AdminOptions
{
    /// <summary>The administrator's password hash, produced by <c>hash-password</c>.</summary>
    public string PasswordHash { get; set; } = "";
}

public sealed class BrokerOptions
{
    /// <summary>Internal address of the broker's plain listener (the Compose service name).</summary>
    public string Host { get; set; } = "";

    public int Port { get; set; } = 1883;

    /// <summary>The service account created in the broker README (normally "yule-server").</summary>
    public string Username { get; set; } = "";

    public string Password { get; set; } = "";

    /// <summary>Client id the server connects with.</summary>
    public string ClientId { get; set; } = "yule-server";
}
