using System.Collections.Concurrent;
using System.Text;
using MQTTnet;
using MQTTnet.Protocol;
using Yule.Server.Configuration;

namespace Yule.Server.Broker;

/// <summary>A message the server received from the broker. The payload is decoded as UTF-8 text.</summary>
public sealed record BrokerMessage(string Topic, string Payload, bool Retain);

/// <summary>
/// The server's single link to the broker over the internal listener. It is what the dynamic-security client, the
/// retained-message publisher and the status listener share, so each of them is written against this seam.
/// </summary>
public interface IBrokerConnection
{
    bool IsConnected { get; }

    /// <summary>Raised on the receive path for every message on a subscribed topic. Handlers must not block.</summary>
    event Action<BrokerMessage>? MessageReceived;

    /// <summary>Raised after every (re)connection, once the subscriptions have been restored.</summary>
    event Action? Connected;

    Task WaitUntilConnectedAsync(CancellationToken cancellationToken);

    /// <summary>Publishes at QoS 1 and waits for the broker to acknowledge it. An empty payload with <paramref name="retain"/> clears a retained message.</summary>
    Task PublishAsync(string topic, string payload, bool retain, CancellationToken cancellationToken);

    /// <summary>Remembers the filter and subscribes to it, now if connected and again after every reconnection.</summary>
    Task SubscribeAsync(string topicFilter, CancellationToken cancellationToken);
}

/// <summary>
/// <see cref="IBrokerConnection"/> over MQTTnet. <see cref="RunAsync"/> keeps the connection up, reconnecting with a
/// capped back-off for as long as the host runs, so a broker restart never needs a server restart.
/// </summary>
public sealed class BrokerConnection : IBrokerConnection, IAsyncDisposable
{
    private static readonly TimeSpan MaxBackoff = TimeSpan.FromSeconds(30);

    private readonly BrokerOptions _options;
    private readonly ILogger<BrokerConnection> _logger;
    private readonly IMqttClient _client;
    private readonly SemaphoreSlim _subscriptionLock = new(1, 1);
    private readonly HashSet<string> _wanted = new(StringComparer.Ordinal);
    private readonly HashSet<string> _active = new(StringComparer.Ordinal);
    private readonly ConcurrentQueue<TaskCompletionSource> _connectedWaiters = new();
    private volatile TaskCompletionSource _disconnected = NewSignal();

    public BrokerConnection(BrokerOptions options, ILogger<BrokerConnection> logger)
    {
        _options = options;
        _logger = logger;
        _client = new MqttClientFactory().CreateMqttClient();
        _client.ApplicationMessageReceivedAsync += OnMessageAsync;
        _client.DisconnectedAsync += OnDisconnectedAsync;
    }

    public bool IsConnected => _client.IsConnected;

    public event Action<BrokerMessage>? MessageReceived;

    public event Action? Connected;

    /// <summary>Connects, then reconnects whenever the link drops, until cancelled.</summary>
    public async Task RunAsync(CancellationToken stoppingToken)
    {
        var delay = TimeSpan.FromSeconds(1);
        while (!stoppingToken.IsCancellationRequested)
        {
            try
            {
                _disconnected = NewSignal();
                await ConnectOnceAsync(stoppingToken);
                delay = TimeSpan.FromSeconds(1);
                await _disconnected.Task.WaitAsync(stoppingToken);
                _logger.LogWarning("Lost the broker connection; reconnecting");
            }
            catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested)
            {
                break;
            }
            catch (Exception ex)
            {
                _logger.LogWarning("Cannot connect to the broker at {Host}:{Port}: {Message}. Retrying in {Delay:0}s",
                    _options.Host, _options.Port, ex.Message, delay.TotalSeconds);
                try
                {
                    await Task.Delay(delay, stoppingToken);
                }
                catch (OperationCanceledException)
                {
                    break;
                }

                delay = TimeSpan.FromSeconds(Math.Min(delay.TotalSeconds * 2, MaxBackoff.TotalSeconds));
            }
        }

        if (_client.IsConnected)
        {
            await _client.DisconnectAsync();
        }
    }

    public async Task WaitUntilConnectedAsync(CancellationToken cancellationToken)
    {
        if (IsConnected)
        {
            return;
        }

        var waiter = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _connectedWaiters.Enqueue(waiter);
        if (IsConnected)
        {
            waiter.TrySetResult();
        }

        await waiter.Task.WaitAsync(cancellationToken);
    }

    public async Task PublishAsync(string topic, string payload, bool retain, CancellationToken cancellationToken)
    {
        await WaitUntilConnectedAsync(cancellationToken);
        var message = new MqttApplicationMessageBuilder()
            .WithTopic(topic)
            .WithPayload(Encoding.UTF8.GetBytes(payload))
            .WithQualityOfServiceLevel(MqttQualityOfServiceLevel.AtLeastOnce)
            .WithRetainFlag(retain)
            .Build();
        var result = await _client.PublishAsync(message, cancellationToken);
        if (result.ReasonCode is not (MqttClientPublishReasonCode.Success or MqttClientPublishReasonCode.NoMatchingSubscribers))
        {
            throw new InvalidOperationException($"The broker refused a publish to '{topic}': {result.ReasonCode} {result.ReasonString}");
        }
    }

    public async Task SubscribeAsync(string topicFilter, CancellationToken cancellationToken)
    {
        await _subscriptionLock.WaitAsync(cancellationToken);
        try
        {
            _wanted.Add(topicFilter);
            if (IsConnected && !_active.Contains(topicFilter))
            {
                await SubscribeNowAsync(topicFilter, cancellationToken);
            }
        }
        finally
        {
            _subscriptionLock.Release();
        }
    }

    public async ValueTask DisposeAsync()
    {
        if (_client.IsConnected)
        {
            await _client.DisconnectAsync();
        }

        _client.Dispose();
        _subscriptionLock.Dispose();
    }

    private async Task ConnectOnceAsync(CancellationToken cancellationToken)
    {
        // Version 5: the broker caps packets at 4 KB, and a v5 client is the one kind whose own limit on packets it
        // will receive can be stated (we state none), so the larger dynsec responses are never cut short.
        var options = new MqttClientOptionsBuilder()
            .WithTcpServer(_options.Host, _options.Port)
            .WithCredentials(_options.Username, _options.Password)
            .WithClientId(_options.ClientId)
            .WithProtocolVersion(MQTTnet.Formatter.MqttProtocolVersion.V500)
            .WithCleanStart(true)
            .WithKeepAlivePeriod(TimeSpan.FromSeconds(30))
            .WithTimeout(TimeSpan.FromSeconds(10))
            .Build();
        var result = await _client.ConnectAsync(options, cancellationToken);
        if (result.ResultCode != MqttClientConnectResultCode.Success)
        {
            // MQTTnet 5 reports a refusal in the result rather than throwing.
            throw new InvalidOperationException(
                $"The broker refused the connection as '{_options.Username}': {result.ResultCode} {result.ReasonString}".TrimEnd()
                + ". Check Yule__Broker__Username and Yule__Broker__Password against the yule-server account (broker/README.md, step 4).");
        }

        await _subscriptionLock.WaitAsync(cancellationToken);
        try
        {
            _active.Clear();
            foreach (var filter in _wanted)
            {
                await SubscribeNowAsync(filter, cancellationToken);
            }
        }
        finally
        {
            _subscriptionLock.Release();
        }

        _logger.LogInformation("Connected to the broker at {Host}:{Port} as {User}", _options.Host, _options.Port, _options.Username);
        while (_connectedWaiters.TryDequeue(out var waiter))
        {
            waiter.TrySetResult();
        }

        foreach (var handler in Connected?.GetInvocationList().Cast<Action>() ?? [])
        {
            try
            {
                handler();
            }
            catch (Exception ex)
            {
                _logger.LogError(ex, "A handler of the broker connection coming up failed");
            }
        }
    }

    private async Task SubscribeNowAsync(string topicFilter, CancellationToken cancellationToken)
    {
        var options = new MqttClientSubscribeOptionsBuilder()
            .WithTopicFilter(f => f.WithTopic(topicFilter).WithQualityOfServiceLevel(MqttQualityOfServiceLevel.AtLeastOnce))
            .Build();
        var result = await _client.SubscribeAsync(options, cancellationToken);
        var code = result.Items.Single().ResultCode;
        if (code is not (MqttClientSubscribeResultCode.GrantedQoS0 or MqttClientSubscribeResultCode.GrantedQoS1 or MqttClientSubscribeResultCode.GrantedQoS2))
        {
            throw new InvalidOperationException($"The broker refused the subscription to '{topicFilter}': {code}. Check the yule-server role (broker/README.md, step 4).");
        }

        _active.Add(topicFilter);
    }

    private Task OnMessageAsync(MqttApplicationMessageReceivedEventArgs e)
    {
        var message = new BrokerMessage(e.ApplicationMessage.Topic, e.ApplicationMessage.ConvertPayloadToString() ?? "", e.ApplicationMessage.Retain);

        // One misbehaving subscriber must not stop the others, or the receive loop.
        foreach (var handler in MessageReceived?.GetInvocationList().Cast<Action<BrokerMessage>>() ?? [])
        {
            try
            {
                handler(message);
            }
            catch (Exception ex)
            {
                _logger.LogError(ex, "A handler of broker messages failed on {Topic}", message.Topic);
            }
        }

        return Task.CompletedTask;
    }

    private Task OnDisconnectedAsync(MqttClientDisconnectedEventArgs e)
    {
        _disconnected.TrySetResult();
        return Task.CompletedTask;
    }

    private static TaskCompletionSource NewSignal() => new(TaskCreationOptions.RunContinuationsAsynchronously);
}
