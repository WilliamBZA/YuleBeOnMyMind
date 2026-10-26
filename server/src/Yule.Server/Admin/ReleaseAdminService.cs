using System.Security.Cryptography;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Options;
using Yule.Server.Configuration;
using Yule.Server.Data;
using Yule.Server.Releases;

namespace Yule.Server.Admin;

/// <summary>A release as the Releases page shows it, with whether it is what devices on each channel are offered now.</summary>
public sealed record ReleaseView(
    int Id,
    string Version,
    string Board,
    long Size,
    string Sha256,
    string FileName,
    DateTime UploadedUtc,
    DateTime? BetaPublishedUtc,
    DateTime? StablePublishedUtc,
    bool CurrentBeta,
    bool CurrentStable);

/// <summary>The release devices of one board on one channel are offered (null: nothing published there yet).</summary>
public sealed record ChannelRelease(string Board, string Channel, ReleaseView? Release);

/// <param name="Errors">Why the upload was refused; nothing was stored or published.</param>
/// <param name="PublishProblem">Set when the release was saved but the broker could not be told yet.</param>
public sealed record UploadResult(IReadOnlyList<string> Errors, ReleaseView? Release, string? PublishProblem)
{
    public bool Succeeded => Errors.Count == 0;
}

/// <summary>The administrator's firmware releases (spec <c>firmware-updates</c>, "Release channels" and "Publishing releases").</summary>
public sealed class ReleaseAdminService(
    YuleDbContext db,
    IReleasePublisher publisher,
    IOptions<YuleOptions> options,
    TimeProvider time,
    ILogger<ReleaseAdminService> logger)
{
    /// <summary>How long a page waits for the broker before leaving the announcement to the next reconnection.</summary>
    public TimeSpan PublishTimeout { get; init; } = TimeSpan.FromSeconds(15);

    public async Task<IReadOnlyList<ReleaseView>> ListAsync(CancellationToken cancellationToken = default)
    {
        var releases = await db.FirmwareReleases.AsNoTracking().ToListAsync(cancellationToken);
        var currentBeta = CurrentIds(releases, r => r.BetaPublishedUtc);
        var currentStable = CurrentIds(releases, r => r.StablePublishedUtc);
        return releases
            .OrderByDescending(r => r.UploadedUtc).ThenByDescending(r => r.Id)
            .Select(r => ToView(r, currentBeta.Contains(r.Id), currentStable.Contains(r.Id)))
            .ToList();
    }

    /// <summary>For every board and channel, the release devices there are offered now.</summary>
    public static IReadOnlyList<ChannelRelease> Current(IReadOnlyList<ReleaseView> releases) =>
        ReleaseVerifier.Boards.Keys
            .SelectMany(board => FirmwareManifests.Channels.Select(channel => new ChannelRelease(board, channel,
                releases.FirstOrDefault(r => r.Board == board && (channel == "beta" ? r.CurrentBeta : r.CurrentStable)))))
            .ToList();

    /// <summary>
    /// Verifies an uploaded image and manifest against the configured release keys, stores the image under a
    /// content-addressed name and publishes it to the beta channel. A refused upload stores and publishes nothing.
    /// </summary>
    public async Task<UploadResult> UploadAsync(byte[] image, string? manifestJson, CancellationToken cancellationToken = default)
    {
        var keys = LoadKeys();
        VerificationResult verification;
        try
        {
            verification = ReleaseVerifier.Verify(image, manifestJson, keys);
        }
        finally
        {
            foreach (var key in keys)
            {
                key.Dispose();
            }
        }

        if (verification.Release is not { } verified)
        {
            return new UploadResult(verification.Errors, null, null);
        }

        if (await db.FirmwareReleases.AsNoTracking().SingleOrDefaultAsync(r => r.Board == verified.Board && r.Version == verified.Version, cancellationToken) is { } existing)
        {
            return new UploadResult(
                [$"Version {verified.Version} for {verified.Board} was already uploaded on {Display.Utc(existing.UploadedUtc)}. "
                 + "Releases never change once published: bump fw_version in firmware/platformio.ini, build and sign again."],
                null, null);
        }

        var fileName = FirmwareManifests.FileName(verified.Version, verified.Board, verified.Sha256);
        await StoreAsync(fileName, image, cancellationToken);

        var now = time.GetUtcNow().UtcDateTime;
        var release = new FirmwareRelease
        {
            Version = verified.Version,
            Board = verified.Board,
            Sha256 = verified.Sha256,
            Size = verified.Size,
            Signature = verified.Signature,
            FileName = fileName,
            UploadedUtc = now,
            BetaPublishedUtc = now,
        };
        db.FirmwareReleases.Add(release);
        await db.SaveChangesAsync(cancellationToken);
        logger.LogInformation("Release {Version} for {Board} uploaded as {File}", release.Version, release.Board, fileName);

        var problem = await PublishAsync(release, "beta", cancellationToken);
        return new UploadResult([], ToView(release, currentBeta: true, currentStable: false), problem);
    }

    /// <summary>Makes a release the current stable release for its board.</summary>
    public async Task<(ChangeResult Change, ReleaseView? Release)> PromoteAsync(int id, CancellationToken cancellationToken = default)
    {
        var release = await db.FirmwareReleases.SingleOrDefaultAsync(r => r.Id == id, cancellationToken);
        if (release is null)
        {
            return (ChangeResult.Missing("release"), null);
        }

        release.StablePublishedUtc = time.GetUtcNow().UtcDateTime;
        await db.SaveChangesAsync(cancellationToken);
        logger.LogInformation("Release {Version} for {Board} promoted to stable", release.Version, release.Board);

        var problem = await PublishAsync(release, "stable", cancellationToken);
        return (ChangeResult.Saved(new ApplyOutcome(problem)), ToView(release, release.BetaPublishedUtc is not null, currentStable: true));
    }

    /// <summary>
    /// Publishes the current release of every board and channel again. Run after every connection to the broker,
    /// which may have lost its retained messages, and to deliver anything a page could not publish at the time.
    /// </summary>
    public async Task<int> RepublishCurrentAsync(CancellationToken cancellationToken = default)
    {
        var releases = await db.FirmwareReleases.AsNoTracking().ToListAsync(cancellationToken);
        var published = 0;
        foreach (var (channel, current) in new[] { ("beta", CurrentIds(releases, r => r.BetaPublishedUtc)), ("stable", CurrentIds(releases, r => r.StablePublishedUtc)) })
        {
            foreach (var release in releases.Where(r => current.Contains(r.Id)).OrderBy(r => r.Board, StringComparer.Ordinal))
            {
                await publisher.PublishAsync(release, channel, cancellationToken);
                published++;
            }
        }

        return published;
    }

    private async Task<string?> PublishAsync(FirmwareRelease release, string channel, CancellationToken cancellationToken)
    {
        using var bounded = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        bounded.CancelAfter(PublishTimeout);
        try
        {
            await publisher.PublishAsync(release, channel, bounded.Token);
            return null;
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        catch (Exception ex)
        {
            var reason = ex is OperationCanceledException ? $"the broker did not answer within {PublishTimeout.TotalSeconds:0} seconds" : ex.Message;
            logger.LogWarning(ex, "Could not publish release {Version} for {Board} to {Channel}; it is published on the next broker connection",
                release.Version, release.Board, channel);
            return $"The release is saved, but devices could not be told about it just now ({reason}). "
                   + "The server announces it as soon as it reaches the broker again.";
        }
    }

    private async Task StoreAsync(string fileName, byte[] image, CancellationToken cancellationToken)
    {
        var directory = FirmwareManifests.Directory(options.Value);
        Directory.CreateDirectory(directory);
        var path = Path.Combine(directory, fileName);
        var temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        await File.WriteAllBytesAsync(temporary, image, cancellationToken);

        // The name is derived from the content, so an existing file of that name already holds these bytes.
        File.Move(temporary, path, overwrite: true);
    }

    private List<ECDsa> LoadKeys()
    {
        var keys = new List<ECDsa>();
        foreach (var pem in options.Value.ReleasePublicKeys.Where(k => !string.IsNullOrWhiteSpace(k)))
        {
            if (ReleaseKeyParser.TryParse(pem, out var key, out _) && key is not null)
            {
                keys.Add(key);
            }
        }

        return keys;
    }

    /// <summary>Per board, the release most recently published on a channel.</summary>
    private static HashSet<int> CurrentIds(IEnumerable<FirmwareRelease> releases, Func<FirmwareRelease, DateTime?> publishedOn) =>
        releases.Where(r => publishedOn(r) is not null)
            .GroupBy(r => r.Board, StringComparer.Ordinal)
            .Select(g => g.OrderByDescending(r => publishedOn(r)).ThenByDescending(r => r.Id).First().Id)
            .ToHashSet();

    private static ReleaseView ToView(FirmwareRelease r, bool currentBeta, bool currentStable) =>
        new(r.Id, r.Version, r.Board, r.Size, r.Sha256, r.FileName, r.UploadedUtc, r.BetaPublishedUtc, r.StablePublishedUtc, currentBeta, currentStable);
}
