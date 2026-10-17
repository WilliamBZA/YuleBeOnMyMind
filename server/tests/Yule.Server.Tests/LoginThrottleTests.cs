using Yule.Server.Security;

namespace Yule.Server.Tests;

public class LoginThrottleTests
{
    private readonly ManualTimeProvider _time = new();

    private LoginThrottle NewThrottle() => new(_time);

    private static void Fail(LoginThrottle throttle, string client, int times)
    {
        for (var i = 0; i < times; i++)
        {
            throttle.RecordFailure(client);
        }
    }

    [Fact]
    public void A_client_with_no_failures_may_try()
    {
        Assert.Null(NewThrottle().GetLockout("10.0.0.1"));
    }

    [Fact]
    public void A_client_may_fail_a_few_times_and_is_then_locked_out()
    {
        var throttle = NewThrottle();

        Fail(throttle, "10.0.0.1", LoginThrottle.MaxFailuresPerClient - 1);
        Assert.Null(throttle.GetLockout("10.0.0.1"));

        throttle.RecordFailure("10.0.0.1");
        var wait = throttle.GetLockout("10.0.0.1");

        Assert.NotNull(wait);
        Assert.Equal(LoginThrottle.Window, wait);
    }

    [Fact]
    public void The_lockout_is_per_client()
    {
        var throttle = NewThrottle();

        Fail(throttle, "10.0.0.1", LoginThrottle.MaxFailuresPerClient);

        Assert.NotNull(throttle.GetLockout("10.0.0.1"));
        Assert.Null(throttle.GetLockout("10.0.0.2"));
    }

    [Fact]
    public void The_lockout_counts_down_and_ends_when_the_oldest_failure_ages_out()
    {
        var throttle = NewThrottle();
        throttle.RecordFailure("10.0.0.1");
        _time.Advance(TimeSpan.FromMinutes(5));
        Fail(throttle, "10.0.0.1", LoginThrottle.MaxFailuresPerClient - 1);

        // Locked until the first failure, five minutes older than the rest, is 15 minutes old.
        Assert.Equal(LoginThrottle.Window - TimeSpan.FromMinutes(5), throttle.GetLockout("10.0.0.1"));

        _time.Advance(LoginThrottle.Window - TimeSpan.FromMinutes(5) - TimeSpan.FromSeconds(1));
        Assert.NotNull(throttle.GetLockout("10.0.0.1"));

        _time.Advance(TimeSpan.FromSeconds(1));
        Assert.Null(throttle.GetLockout("10.0.0.1"));
    }

    [Fact]
    public void A_correct_password_clears_that_clients_failures()
    {
        var throttle = NewThrottle();
        Fail(throttle, "10.0.0.1", LoginThrottle.MaxFailuresPerClient - 1);

        throttle.RecordSuccess("10.0.0.1");
        Fail(throttle, "10.0.0.1", LoginThrottle.MaxFailuresPerClient - 1);

        Assert.Null(throttle.GetLockout("10.0.0.1"));
    }

    [Fact]
    public void Many_clients_together_are_capped_too_so_forged_addresses_do_not_help()
    {
        var throttle = NewThrottle();

        // Each address stays under its own limit, but together they guess far too often.
        for (var i = 0; i < LoginThrottle.MaxFailuresOverall; i++)
        {
            Assert.Null(throttle.GetLockout($"10.1.0.{i}"));
            throttle.RecordFailure($"10.1.0.{i}");
        }

        Assert.NotNull(throttle.GetLockout("10.9.9.9"));

        _time.Advance(LoginThrottle.Window);
        Assert.Null(throttle.GetLockout("10.9.9.9"));
    }
}
