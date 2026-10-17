namespace Yule.Server.Security;

/// <summary>
/// Slows down guessing of the administrator password. A client address that fails <see cref="MaxFailuresPerClient"/>
/// times within <see cref="Window"/> is refused until its oldest failure has aged out. Because the address can be
/// forged when the server is reachable directly, all clients together are also capped at
/// <see cref="MaxFailuresOverall"/> failures per window, which bounds an online guesser however many addresses they use.
/// Attempts made while locked out are refused without being checked or counted, so waiting is the only way out.
/// </summary>
public sealed class LoginThrottle(TimeProvider time)
{
    public const int MaxFailuresPerClient = 5;
    public const int MaxFailuresOverall = 30;
    public static readonly TimeSpan Window = TimeSpan.FromMinutes(15);

    private const int SweepAbove = 1000;

    private readonly object _lock = new();
    private readonly Dictionary<string, List<DateTimeOffset>> _byClient = new(StringComparer.Ordinal);
    private readonly List<DateTimeOffset> _overall = [];

    /// <summary>How long until this client may try again, or null if it may try now.</summary>
    public TimeSpan? GetLockout(string client)
    {
        lock (_lock)
        {
            var now = time.GetUtcNow();
            Prune(_overall, now);
            var wait = WaitFor(_overall, MaxFailuresOverall, now);

            if (_byClient.TryGetValue(client, out var failures))
            {
                Prune(failures, now);
                if (failures.Count == 0)
                {
                    _byClient.Remove(client);
                }
                else if (WaitFor(failures, MaxFailuresPerClient, now) is { } clientWait && (wait is null || clientWait > wait))
                {
                    wait = clientWait;
                }
            }

            return wait;
        }
    }

    public void RecordFailure(string client)
    {
        lock (_lock)
        {
            var now = time.GetUtcNow();
            if (_byClient.Count > SweepAbove)
            {
                foreach (var key in _byClient.Keys.ToList())
                {
                    Prune(_byClient[key], now);
                    if (_byClient[key].Count == 0)
                    {
                        _byClient.Remove(key);
                    }
                }
            }

            if (!_byClient.TryGetValue(client, out var failures))
            {
                _byClient[client] = failures = [];
            }

            failures.Add(now);
            _overall.Add(now);
        }
    }

    /// <summary>A correct password clears that client's failures. The overall count is left to age out.</summary>
    public void RecordSuccess(string client)
    {
        lock (_lock)
        {
            _byClient.Remove(client);
        }
    }

    private static void Prune(List<DateTimeOffset> failures, DateTimeOffset now) =>
        failures.RemoveAll(f => f + Window <= now);

    /// <summary>
    /// Failures are kept in the order they happened. At or over the limit, the client may try again once enough of the
    /// oldest have aged out to bring the count below the limit.
    /// </summary>
    private static TimeSpan? WaitFor(List<DateTimeOffset> failures, int limit, DateTimeOffset now) =>
        failures.Count >= limit ? failures[failures.Count - limit] + Window - now : null;
}
