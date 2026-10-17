using Yule.Server.Broker;

namespace Yule.Server.Admin;

/// <summary>
/// Whether the broker now matches a change that was just saved. The database is the source of truth, so a change is
/// never lost when the broker cannot be reached: the reconciler repairs the broker later.
/// </summary>
/// <param name="Problem">Null when the broker was updated (or nothing needed updating); otherwise what to tell the administrator.</param>
public sealed record ApplyOutcome(string? Problem)
{
    public static readonly ApplyOutcome Done = new((string?)null);

    public bool Applied => Problem is null;
}

/// <summary>
/// Pushes a saved change to the broker straight away, so that it takes effect within seconds. The synchroniser waits
/// for the broker for as long as it takes, so this bounds the wait and turns any failure into a message instead of an
/// error page: the change is already saved and the periodic reconciler will apply it.
/// </summary>
public sealed class BrokerChangeApplier(IBrokerSynchroniser synchroniser, ILogger<BrokerChangeApplier> logger)
{
    /// <summary>How long a page waits for the broker before giving up and leaving the change to the reconciler.</summary>
    public TimeSpan Timeout { get; init; } = TimeSpan.FromSeconds(15);

    public async Task<ApplyOutcome> ApplyAsync(IEnumerable<string> deviceIds, CancellationToken cancellationToken)
    {
        var ids = deviceIds.Distinct(StringComparer.Ordinal).ToList();
        if (ids.Count == 0)
        {
            return ApplyOutcome.Done;
        }

        using var bounded = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        bounded.CancelAfter(Timeout);
        try
        {
            await synchroniser.ApplyDevicesAsync(ids, bounded.Token);
            return ApplyOutcome.Done;
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw; // the visitor went away; nothing to report to them
        }
        catch (Exception ex)
        {
            var reason = ex is OperationCanceledException
                ? $"the broker did not answer within {Timeout.TotalSeconds:0} seconds"
                : ex.Message;
            logger.LogWarning(ex, "Could not update the broker for {Devices}; the reconciler will retry", string.Join(", ", ids));
            return new ApplyOutcome(
                $"The change is saved, but the broker could not be updated just now ({reason}). "
                + "The server will bring the broker up to date as soon as it can reach it.");
        }
    }
}
