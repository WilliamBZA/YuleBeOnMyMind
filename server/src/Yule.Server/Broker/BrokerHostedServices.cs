using Microsoft.Extensions.Options;
using Yule.Server.Configuration;

namespace Yule.Server.Broker;

/// <summary>Keeps the server's connection to the broker up for as long as the host runs.</summary>
public sealed class BrokerConnectionService(BrokerConnection connection) : BackgroundService
{
    protected override Task ExecuteAsync(CancellationToken stoppingToken) => connection.RunAsync(stoppingToken);
}

/// <summary>
/// Runs the reconciler once the broker is reachable, again after every reconnection (the broker may have restarted
/// and lost retained messages), and every <see cref="YuleOptions.ReconcileIntervalMinutes"/> minutes. A failed pass
/// is logged and retried; it never stops the server, because devices do not depend on the server being up.
/// </summary>
public sealed class BrokerSyncService(
    IBrokerConnection connection,
    IBrokerSynchroniser synchroniser,
    IOptions<YuleOptions> options,
    ILogger<BrokerSyncService> logger) : BackgroundService
{
    /// <summary>How long to wait between passes while the broker is unchanged.</summary>
    public TimeSpan Interval { get; init; } = TimeSpan.FromMinutes(options.Value.ReconcileIntervalMinutes);

    /// <summary>How long to wait before trying again after a failed pass.</summary>
    public TimeSpan RetryDelay { get; init; } = TimeSpan.FromSeconds(30);

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        var reconnected = new SemaphoreSlim(0);
        connection.Connected += () => reconnected.Release();

        // The first pass, and the first after a reconnection, also republish every retained config.
        var republish = true;

        while (!stoppingToken.IsCancellationRequested)
        {
            try
            {
                await connection.WaitUntilConnectedAsync(stoppingToken);
                var result = await synchroniser.ReconcileAsync(republish, stoppingToken);
                logger.LogInformation("Broker reconciled: {Commands} command(s), {Configs} config(s) published", result.CommandsSent, result.ConfigsPublished);
                republish = false;

                // Wait for the next interval, or for a reconnection, whichever comes first.
                if (await reconnected.WaitAsync(Interval, stoppingToken))
                {
                    republish = true;
                    while (reconnected.CurrentCount > 0)
                    {
                        reconnected.Wait(0);
                    }
                }
            }
            catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested)
            {
                return;
            }
            catch (Exception ex)
            {
                logger.LogWarning(ex, "Reconciling the broker failed; trying again in {Delay:0.#} s", RetryDelay.TotalSeconds);
                try
                {
                    await Task.Delay(RetryDelay, stoppingToken);
                }
                catch (OperationCanceledException)
                {
                    return;
                }
            }
        }
    }
}

public static class BrokerServiceCollectionExtensions
{
    /// <summary>Registers the broker link, the dynamic-security client, the synchroniser and the background services that run them.</summary>
    public static IServiceCollection AddYuleBroker(this IServiceCollection services)
    {
        services.AddSingleton(sp => new BrokerConnection(
            sp.GetRequiredService<IOptions<YuleOptions>>().Value.Broker,
            sp.GetRequiredService<ILogger<BrokerConnection>>()));
        services.AddSingleton<IBrokerConnection>(sp => sp.GetRequiredService<BrokerConnection>());
        services.AddSingleton<DynsecClient>();
        services.AddSingleton<IBrokerSynchroniser, BrokerSynchroniser>();
        services.AddHostedService<BrokerConnectionService>();
        services.AddHostedService<BrokerSyncService>();
        services.AddHostedService<StatusListener>();
        return services;
    }
}
