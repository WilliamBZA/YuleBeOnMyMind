using System.Diagnostics;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Yule.Server.Broker;
using Yule.Server.Configuration;
using Yule.Server.Tests;

namespace Yule.Server.IntegrationTests;

/// <summary>
/// The Yule server's broker-facing parts wired together exactly as in production (a real connection, dynamic-security
/// client and synchroniser over a real database), minus the web host. <see cref="StopServerAsync"/> takes the server
/// off the broker, as a crashed or stopped server would.
/// </summary>
public sealed class BrokerHarness : IAsyncDisposable
{
    private readonly CancellationTokenSource _stopping = new();
    private Task _running = Task.CompletedTask;

    private BrokerHarness(ServerWorld world, BrokerConnection connection, string root)
    {
        World = world;
        Connection = connection;
        Dynsec = new DynsecClient(connection);
        Sync = new BrokerSynchroniser(
            world.Services.GetRequiredService<IServiceScopeFactory>(),
            world.Passwords,
            Dynsec,
            connection,
            Options.Create(new YuleOptions { TopicRoot = root }),
            NullLogger<BrokerSynchroniser>.Instance);
        Root = root;
    }

    public ServerWorld World { get; }

    public BrokerConnection Connection { get; }

    public DynsecClient Dynsec { get; }

    public BrokerSynchroniser Sync { get; }

    public string Root { get; }

    public static async Task<BrokerHarness> StartAsync(MosquittoFixture broker, string root = "yule/v1")
    {
        var world = await ServerWorld.CreateAsync();
        var connection = new BrokerConnection(broker.ServerOptions(), NullLogger<BrokerConnection>.Instance);
        var harness = new BrokerHarness(world, connection, root);
        harness._running = connection.RunAsync(harness._stopping.Token);
        await connection.WaitUntilConnectedAsync(new CancellationTokenSource(TimeSpan.FromSeconds(15)).Token);

        // A new test starts from a clean broker: whatever an earlier test left behind is not in this database, so the
        // reconciler removes it, which is also what would happen to leftovers in production.
        await harness.Sync.ReconcileAsync(republishAllConfigs: false);
        return harness;
    }

    /// <summary>The topic a group's presses are published on by one device.</summary>
    public string PressTopic(string groupId, string senderId) => $"{Root}/groups/{groupId}/press/{senderId}";

    public string PressFilter(string groupId) => $"{Root}/groups/{groupId}/press/+";

    public string ConfigTopic(string deviceId) => $"{Root}/devices/{deviceId}/config";

    public string StatusTopic(string deviceId) => $"{Root}/devices/{deviceId}/status";

    /// <summary>Disconnects the server from the broker and leaves it off.</summary>
    public async Task StopServerAsync()
    {
        _stopping.Cancel();
        try
        {
            await _running;
        }
        catch (OperationCanceledException)
        {
        }
    }

    public async ValueTask DisposeAsync()
    {
        await StopServerAsync();
        await Connection.DisposeAsync();
        await World.DisposeAsync();
        _stopping.Dispose();
    }
}

public static class Eventually
{
    /// <summary>Polls until the condition holds; fails with a message saying what never happened.</summary>
    public static async Task TrueAsync(Func<bool> condition, string what, int seconds = 10)
    {
        var timer = Stopwatch.StartNew();
        while (timer.Elapsed < TimeSpan.FromSeconds(seconds))
        {
            if (condition())
            {
                return;
            }

            await Task.Delay(25);
        }

        throw new Xunit.Sdk.XunitException($"Timed out after {seconds} s waiting for: {what}");
    }

    public static async Task TrueAsync(Func<Task<bool>> condition, string what, int seconds = 10)
    {
        var timer = Stopwatch.StartNew();
        while (timer.Elapsed < TimeSpan.FromSeconds(seconds))
        {
            if (await condition())
            {
                return;
            }

            await Task.Delay(25);
        }

        throw new Xunit.Sdk.XunitException($"Timed out after {seconds} s waiting for: {what}");
    }
}
