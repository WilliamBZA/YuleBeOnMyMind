using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Yule.Server.Broker;
using Yule.Server.Configuration;

namespace Yule.Server.Tests;

public class BrokerSyncServiceTests
{
    private sealed class RecordingSynchroniser : IBrokerSynchroniser
    {
        private readonly object _gate = new();
        private readonly List<bool> _reconciles = [];

        /// <summary>Throws on the first n reconciles.</summary>
        public int FailFirst { get; set; }

        public List<bool> Reconciles
        {
            get
            {
                lock (_gate)
                {
                    return [.. _reconciles];
                }
            }
        }

        public Task<SyncResult> ApplyDevicesAsync(IEnumerable<string> deviceIds, CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public Task<SyncResult> ReconcileAsync(bool republishAllConfigs, CancellationToken cancellationToken = default)
        {
            lock (_gate)
            {
                _reconciles.Add(republishAllConfigs);
                if (_reconciles.Count <= FailFirst)
                {
                    throw new InvalidOperationException("the broker said no");
                }
            }

            return Task.FromResult(new SyncResult(0, new HashSet<string>(), new HashSet<string>(), 0));
        }
    }

    private static BrokerSyncService Create(FakeBrokerConnection connection, RecordingSynchroniser synchroniser, TimeSpan interval) =>
        new(connection, synchroniser, Options.Create(new YuleOptions()), NullLogger<BrokerSyncService>.Instance)
        {
            Interval = interval,
            RetryDelay = TimeSpan.FromMilliseconds(100),
        };

    private static async Task WaitAsync(Func<bool> condition, string what)
    {
        var deadline = DateTime.UtcNow.AddSeconds(5);
        while (!condition())
        {
            if (DateTime.UtcNow > deadline)
            {
                throw new Xunit.Sdk.XunitException("Timed out waiting for: " + what);
            }

            await Task.Delay(10);
        }
    }

    [Fact]
    public void The_interval_comes_from_configuration_and_defaults_to_ten_minutes()
    {
        var defaults = new BrokerSyncService(new FakeBrokerConnection(), new RecordingSynchroniser(), Options.Create(new YuleOptions()), NullLogger<BrokerSyncService>.Instance);
        var configured = new BrokerSyncService(new FakeBrokerConnection(), new RecordingSynchroniser(), Options.Create(new YuleOptions { ReconcileIntervalMinutes = 3 }), NullLogger<BrokerSyncService>.Instance);

        Assert.Equal(TimeSpan.FromMinutes(10), defaults.Interval);
        Assert.Equal(TimeSpan.FromMinutes(3), configured.Interval);
    }

    [Fact]
    public async Task It_reconciles_at_start_up_republishing_everything_and_then_every_interval_without_republishing()
    {
        var synchroniser = new RecordingSynchroniser();
        var service = Create(new FakeBrokerConnection(), synchroniser, TimeSpan.FromMilliseconds(80));

        await service.StartAsync(CancellationToken.None);
        await WaitAsync(() => synchroniser.Reconciles.Count >= 4, "four reconcile passes");
        await service.StopAsync(CancellationToken.None);

        Assert.True(synchroniser.Reconciles[0]);
        Assert.All(synchroniser.Reconciles.Skip(1), republish => Assert.False(republish));
    }

    [Fact]
    public async Task A_reconnection_triggers_a_prompt_pass_that_republishes_everything()
    {
        var connection = new FakeBrokerConnection();
        var synchroniser = new RecordingSynchroniser();
        var service = Create(connection, synchroniser, TimeSpan.FromMinutes(10));
        await service.StartAsync(CancellationToken.None);
        await WaitAsync(() => synchroniser.Reconciles.Count == 1, "the start-up pass");

        connection.RaiseConnected();
        connection.RaiseConnected(); // a flurry of reconnections is one pass, not several

        await WaitAsync(() => synchroniser.Reconciles.Count >= 2, "a pass after the reconnection");
        await Task.Delay(200);
        await service.StopAsync(CancellationToken.None);
        Assert.Equal([true, true], synchroniser.Reconciles);
    }

    [Fact]
    public async Task A_failed_pass_is_retried_still_republishing_and_never_stops_the_service()
    {
        var synchroniser = new RecordingSynchroniser { FailFirst = 2 };
        var service = Create(new FakeBrokerConnection(), synchroniser, TimeSpan.FromMinutes(10));

        await service.StartAsync(CancellationToken.None);
        await WaitAsync(() => synchroniser.Reconciles.Count >= 3, "a third attempt after two failures");
        await service.StopAsync(CancellationToken.None);

        Assert.Equal([true, true, true], synchroniser.Reconciles.Take(3));
        Assert.Null(service.ExecuteTask?.Exception);
    }

    [Fact]
    public async Task It_stops_promptly_when_the_host_stops()
    {
        var service = Create(new FakeBrokerConnection(), new RecordingSynchroniser(), TimeSpan.FromMinutes(10));
        await service.StartAsync(CancellationToken.None);

        var stopping = service.StopAsync(CancellationToken.None);

        Assert.Same(stopping, await Task.WhenAny(stopping, Task.Delay(TimeSpan.FromSeconds(5))));
    }
}
