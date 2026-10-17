using System.Collections.Concurrent;
using System.Text;
using MQTTnet;
using MQTTnet.Formatter;
using MQTTnet.Protocol;

namespace Yule.Server.IntegrationTests;

/// <summary>The broker answered a connection attempt with something other than success.</summary>
public sealed class ConnectionRefusedException(MqttClientConnectResultCode code) : Exception($"The broker refused the connection: {code}")
{
    public MqttClientConnectResultCode Code { get; } = code;
}

/// <summary>
/// Stands in for a decoration's MQTT client. By default it connects like the firmware does: MQTT 3.1.1, client id
/// equal to the device id, a persistent session. Version 5 is available where a test needs the broker's reasons
/// (a denied publish) or a Last Will on a deliberate disconnect.
/// </summary>
public sealed class TestDevice : IAsyncDisposable
{
    private readonly IMqttClient _client = new MqttClientFactory().CreateMqttClient();
    private readonly ConcurrentQueue<(string Topic, string Payload)> _received = new();
    private MqttClientOptions _options = null!;
    private bool _disposing;
    private int _disconnects;

    private TestDevice(string id, string password)
    {
        Id = id;
        Password = password;
        _client.ApplicationMessageReceivedAsync += e =>
        {
            _received.Enqueue((e.ApplicationMessage.Topic, e.ApplicationMessage.ConvertPayloadToString() ?? ""));
            return Task.CompletedTask;
        };
        _client.DisconnectedAsync += OnDisconnectedAsync;
    }

    public string Id { get; }

    public string Password { get; }

    /// <summary>Reconnect on its own after being dropped, as the firmware does.</summary>
    public bool AutoReconnect { get; set; }

    /// <summary>Run after every successful (re)connection, as the firmware subscribes to its topics each time.</summary>
    public Func<TestDevice, Task>? OnConnected { get; set; }

    public bool IsConnected => _client.IsConnected;

    /// <summary>How many times the broker (or network) has dropped this device.</summary>
    public int Disconnects => Volatile.Read(ref _disconnects);

    public IReadOnlyList<(string Topic, string Payload)> Received => _received.ToArray();

    public static async Task<TestDevice> ConnectAsync(
        MosquittoFixture broker,
        string id,
        string password,
        MqttProtocolVersion version = MqttProtocolVersion.V311,
        string? clientId = null,
        bool persistentSession = true,
        bool autoReconnect = false,
        Func<TestDevice, Task>? onConnected = null,
        (string Topic, string Payload)? lastWill = null)
    {
        var device = new TestDevice(id, password) { AutoReconnect = autoReconnect, OnConnected = onConnected };
        var builder = new MqttClientOptionsBuilder()
            .WithTcpServer(broker.Host, broker.Port)
            .WithCredentials(id, password)
            .WithClientId(clientId ?? id)
            .WithProtocolVersion(version)
            .WithKeepAlivePeriod(TimeSpan.FromSeconds(30))
            .WithTimeout(TimeSpan.FromSeconds(10));
        if (version == MqttProtocolVersion.V500)
        {
            builder.WithCleanStart(!persistentSession);
            if (persistentSession)
            {
                builder.WithSessionExpiryInterval(3600);
            }
        }
        else
        {
            builder.WithCleanSession(!persistentSession);
        }

        if (lastWill is { } will)
        {
            builder.WithWillTopic(will.Topic)
                .WithWillPayload(Encoding.UTF8.GetBytes(will.Payload))
                .WithWillQualityOfServiceLevel(MqttQualityOfServiceLevel.AtLeastOnce)
                .WithWillRetain(true);
        }

        device._options = builder.Build();
        await device.ConnectCoreAsync();
        return device;
    }

    /// <summary>Subscribes, and says whether the broker granted it.</summary>
    public async Task<bool> TrySubscribeAsync(string filter)
    {
        var result = await _client.SubscribeAsync(new MqttClientSubscribeOptionsBuilder()
            .WithTopicFilter(f => f.WithTopic(filter).WithQualityOfServiceLevel(MqttQualityOfServiceLevel.AtLeastOnce))
            .Build());
        return result.Items.Single().ResultCode is MqttClientSubscribeResultCode.GrantedQoS0 or MqttClientSubscribeResultCode.GrantedQoS1 or MqttClientSubscribeResultCode.GrantedQoS2;
    }

    public async Task<bool> TryUnsubscribeAsync(string filter)
    {
        var result = await _client.UnsubscribeAsync(new MqttClientUnsubscribeOptionsBuilder().WithTopicFilter(filter).Build());
        return result.Items.Single().ResultCode == MqttClientUnsubscribeResultCode.Success;
    }

    /// <summary>
    /// Publishes at QoS 1. With MQTT 5 the result says whether the broker accepted it; with 3.1.1 a refusal cannot
    /// be seen from the publisher, so this returns true, and tests check the receiving side instead.
    /// </summary>
    public async Task<bool> PublishAsync(string topic, string payload, bool retain = false)
    {
        var result = await _client.PublishAsync(new MqttApplicationMessageBuilder()
            .WithTopic(topic)
            .WithPayload(Encoding.UTF8.GetBytes(payload))
            .WithQualityOfServiceLevel(MqttQualityOfServiceLevel.AtLeastOnce)
            .WithRetainFlag(retain)
            .Build());
        return result.ReasonCode is MqttClientPublishReasonCode.Success or MqttClientPublishReasonCode.NoMatchingSubscribers;
    }

    /// <summary>Waits for a message on exactly this topic and returns its payload, or null if none arrives in time.</summary>
    public async Task<string?> WaitForAsync(string topic, TimeSpan? timeout = null, int skip = 0)
    {
        var deadline = DateTime.UtcNow + (timeout ?? TimeSpan.FromSeconds(10));
        while (DateTime.UtcNow < deadline)
        {
            var matches = _received.Where(m => m.Topic == topic).Skip(skip).ToList();
            if (matches.Count > 0)
            {
                return matches[0].Payload;
            }

            await Task.Delay(20);
        }

        return null;
    }

    public int CountOn(string topic) => _received.Count(m => m.Topic == topic);

    /// <summary>Disconnects with a reason that makes the broker publish the Last Will (MQTT 5 only).</summary>
    public Task DisconnectWithWillAsync()
    {
        _disposing = true;
        return _client.DisconnectAsync(new MqttClientDisconnectOptions { Reason = MqttClientDisconnectOptionsReason.DisconnectWithWillMessage });
    }

    public async ValueTask DisposeAsync()
    {
        _disposing = true;
        if (_client.IsConnected)
        {
            await _client.DisconnectAsync();
        }

        _client.Dispose();
    }

    private async Task ConnectCoreAsync()
    {
        var result = await _client.ConnectAsync(_options);
        if (result.ResultCode != MqttClientConnectResultCode.Success)
        {
            throw new ConnectionRefusedException(result.ResultCode);
        }

        if (OnConnected is not null)
        {
            await OnConnected(this);
        }
    }

    private Task OnDisconnectedAsync(MqttClientDisconnectedEventArgs e)
    {
        Interlocked.Increment(ref _disconnects);
        if (AutoReconnect && !_disposing)
        {
            _ = Task.Run(async () =>
            {
                // The firmware waits before retrying; a short wait keeps the tests quick.
                while (!_disposing)
                {
                    await Task.Delay(200);
                    try
                    {
                        await ConnectCoreAsync();
                        return;
                    }
                    catch (Exception) when (!_disposing)
                    {
                        // The broker may still be applying a change; try again.
                    }
                }
            });
        }

        return Task.CompletedTask;
    }
}
