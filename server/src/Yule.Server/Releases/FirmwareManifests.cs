using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.Extensions.Options;
using Yule.Server.Broker;
using Yule.Server.Configuration;
using Yule.Server.Data;

namespace Yule.Server.Releases;

/// <summary>
/// Where release images live and how they are announced to devices: the retained manifest on
/// <c>&lt;root&gt;/firmware/&lt;channel&gt;/&lt;board&gt;</c> (design D4), pointing at <c>/firmware/{file}</c> on this server.
/// </summary>
public static partial class FirmwareManifests
{
    public static readonly IReadOnlyList<string> Channels = ["beta", "stable"];

    /// <summary>Image files are named after their content, so a published file never changes: yule-0.3.0-esp32dev-1a2b3c4d5e6f.bin.</summary>
    public static string FileName(string version, string board, string sha256) => $"yule-{version}-{board}-{sha256[..12]}.bin";

    [GeneratedRegex("^yule-[0-9]{1,4}\\.[0-9]{1,4}\\.[0-9]{1,4}-[a-z0-9-]{1,32}-[0-9a-f]{12}\\.bin$")]
    private static partial Regex FileNamePattern();

    /// <summary>True for a name <see cref="FileName"/> could have produced; anything else (paths, "..") is never served.</summary>
    public static bool IsFileName(string name) => FileNamePattern().IsMatch(name);

    public static string Directory(YuleOptions options) => Path.Combine(options.DataDirectory, "firmware");

    public static string Topic(string root, string channel, string board) => $"{root}/firmware/{channel}/{board}";

    public static string DownloadUrl(string publicBaseUrl, string fileName) => publicBaseUrl.TrimEnd('/') + "/firmware/" + fileName;

    /// <summary>The manifest devices read: the signed fields plus the download address.</summary>
    public static string Payload(FirmwareRelease release, string url) => JsonSerializer.Serialize(new
    {
        v = 1,
        version = release.Version,
        board = release.Board,
        size = release.Size,
        sha256 = release.Sha256,
        sig = release.Signature,
        url,
    });
}

/// <summary>Announces a release to the devices on a channel. A seam so the pages can be tested without a broker.</summary>
public interface IReleasePublisher
{
    /// <summary>Publishes the retained manifest of <paramref name="release"/> as the current release of <paramref name="channel"/> for its board.</summary>
    Task PublishAsync(FirmwareRelease release, string channel, CancellationToken cancellationToken);
}

public sealed class BrokerReleasePublisher(IBrokerConnection connection, IOptions<YuleOptions> options) : IReleasePublisher
{
    public Task PublishAsync(FirmwareRelease release, string channel, CancellationToken cancellationToken)
    {
        var o = options.Value;
        var url = FirmwareManifests.DownloadUrl(o.PublicBaseUrl, release.FileName);
        return connection.PublishAsync(FirmwareManifests.Topic(o.TopicRoot, channel, release.Board), FirmwareManifests.Payload(release, url), retain: true, cancellationToken);
    }
}
