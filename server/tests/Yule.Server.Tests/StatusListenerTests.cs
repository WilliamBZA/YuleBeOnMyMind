using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Yule.Server.Broker;
using Yule.Server.Configuration;
using Yule.Server.Data;

namespace Yule.Server.Tests;

public class StatusMessagesTests
{
    [Theory]
    [InlineData("yule/v1/devices/d-3c71bf4a2b10/status", true)]
    [InlineData("yule/v1/devices/d-3c71bf4a2b10/config", false)]
    [InlineData("yule/v1/devices/d-3c71bf4a2b10/status/extra", false)]
    [InlineData("yule/v1/devices//status", false)]
    [InlineData("yule/v1/devices/D-3C71BF4A2B10/status", false)]
    [InlineData("yule/v1/devices/nobody/status", false)]
    [InlineData("yule/v2/devices/d-3c71bf4a2b10/status", false)]
    [InlineData("yule/v1/groups/g-aaaaaaaa/press/d-3c71bf4a2b10", false)]
    public void Only_a_device_status_topic_under_the_configured_root_is_accepted(string topic, bool accepted)
    {
        Assert.Equal(accepted, StatusMessages.TryParseTopic("yule/v1", topic, out var id));
        Assert.Equal(accepted ? "d-3c71bf4a2b10" : "", id);
    }

    [Fact]
    public void An_online_report_carries_the_whole_picture()
    {
        Assert.True(StatusMessages.TryParsePayload(
            """{"v":1,"state":"online","fw":"0.3.0","board":"esp32dev","name":"Gran","ota":"ok"}""", out var update));

        Assert.Equal(new StatusUpdate("online", "0.3.0", "esp32dev", "Gran", "ok"), update);
    }

    [Fact]
    public void The_last_will_says_only_that_the_device_is_offline()
    {
        Assert.True(StatusMessages.TryParsePayload("""{"v":1,"state":"offline"}""", out var update));

        Assert.Equal(new StatusUpdate("offline", null, null, null, null), update);
    }

    [Fact]
    public void Unknown_fields_and_newer_versions_are_tolerated()
    {
        Assert.True(StatusMessages.TryParsePayload("""{"v":7,"state":"online","fw":"1.0.0","uptime":12,"extra":{"a":1}}""", out var update));

        Assert.Equal("1.0.0", update.FirmwareVersion);
    }

    [Theory]
    [InlineData("")]
    [InlineData("not json")]
    [InlineData("[]")]
    [InlineData("\"online\"")]
    [InlineData("{}")]
    [InlineData("""{"state":"sleeping"}""")]
    [InlineData("""{"state":1}""")]
    [InlineData("""{"state":"ONLINE"}""")]
    public void Anything_else_is_rejected(string payload)
    {
        Assert.False(StatusMessages.TryParsePayload(payload, out _));
    }

    [Fact]
    public void Fields_of_the_wrong_type_are_ignored_rather_than_fatal()
    {
        Assert.True(StatusMessages.TryParsePayload("""{"state":"online","fw":3,"board":["x"],"name":null,"ota":"ok"}""", out var update));

        Assert.Equal(new StatusUpdate("online", null, null, null, "ok"), update);
    }
}

public class StatusListenerTests : IAsyncLifetime
{
    private const string Emma = "d-000000000002";
    private const string Topic = "yule/v1/devices/" + Emma + "/status";

    private ServerWorld _world = null!;
    private readonly FakeBrokerConnection _connection = new();
    private StatusListener _listener = null!;

    public async Task InitializeAsync()
    {
        _world = await ServerWorld.CreateAsync();
        await _world.AddDeviceAsync(Emma);
        _listener = new StatusListener(
            _connection,
            _world.Services.GetRequiredService<IServiceScopeFactory>(),
            Options.Create(new YuleOptions { TopicRoot = "yule/v1" }),
            NullLogger<StatusListener>.Instance);
        await _listener.StartAsync(CancellationToken.None);

        // The hosted service starts on its own task; messages sent before it has subscribed would be missed
        // (in production the broker replays retained statuses on subscribing, so nothing is lost).
        var deadline = DateTime.UtcNow.AddSeconds(5);
        while (!_connection.Subscriptions.Contains("yule/v1/devices/+/status") && DateTime.UtcNow < deadline)
        {
            await Task.Delay(10);
        }
    }

    public async Task DisposeAsync()
    {
        await _listener.StopAsync(CancellationToken.None);
        await _world.DisposeAsync();
    }

    private void Send(string topic, string payload) => _connection.Receive(new BrokerMessage(topic, payload, Retain: true));

    private async Task<DeviceStatus> WaitForAsync(Func<DeviceStatus, bool> condition)
    {
        var deadline = DateTime.UtcNow.AddSeconds(5);
        DeviceStatus? status = null;
        while (DateTime.UtcNow < deadline)
        {
            status = await _world.GetStatusAsync(Emma);
            if (status is not null && condition(status))
            {
                return status;
            }

            await Task.Delay(25);
        }

        throw new Xunit.Sdk.XunitException("The status was not recorded in time. Last seen: " + (status is null ? "no row" : $"{status.State} {status.FirmwareVersion}"));
    }

    [Fact]
    public void It_subscribes_to_every_device_status_topic()
    {
        // (InitializeAsync waits for exactly this.)
        Assert.Contains("yule/v1/devices/+/status", _connection.Subscriptions);
    }

    [Fact]
    public async Task Online_offline_and_a_firmware_change_are_recorded()
    {
        Send(Topic, """{"v":1,"state":"online","fw":"0.2.0","board":"esp32dev","name":"Emma","ota":"none"}""");
        var online = await WaitForAsync(s => s.State == "online");
        Assert.Equal(("0.2.0", "esp32dev", "Emma", "none"), (online.FirmwareVersion, online.Board, online.Name, online.OtaResult));

        Send(Topic, """{"v":1,"state":"online","fw":"0.3.0","board":"esp32dev","name":"Emma","ota":"ok"}""");
        var updated = await WaitForAsync(s => s.FirmwareVersion == "0.3.0");
        Assert.Equal("ok", updated.OtaResult);

        Send(Topic, """{"v":1,"state":"offline"}""");
        var offline = await WaitForAsync(s => s.State == "offline");
        Assert.Equal("0.3.0", offline.FirmwareVersion); // what it last reported is kept
        Assert.Equal("Emma", offline.Name);
    }

    [Fact]
    public async Task Last_seen_moves_forward_with_each_report()
    {
        Send(Topic, """{"state":"online","fw":"0.2.0"}""");
        var first = (await WaitForAsync(s => s.State == "online")).LastSeenUtc;
        await Task.Delay(50);

        Send(Topic, """{"state":"online","fw":"0.2.1"}""");
        var second = (await WaitForAsync(s => s.FirmwareVersion == "0.2.1")).LastSeenUtc;

        Assert.True(second > first);
    }

    [Fact]
    public async Task Bad_messages_unregistered_devices_and_cleared_retained_messages_are_ignored_and_do_not_stop_the_listener()
    {
        Send("yule/v1/devices/d-0000000000ff/status", """{"state":"online"}""");  // not registered
        Send(Topic, "garbage");
        Send(Topic, """{"state":"unknown"}""");
        Send(Topic, "");                                                          // a cleared retained message
        Send("yule/v1/devices/" + Emma + "/config", """{"state":"online"}""");    // some other topic
        Send(Topic, """{"state":"online","fw":"9.9.9"}""");

        var status = await WaitForAsync(s => s.FirmwareVersion == "9.9.9");

        Assert.Equal("online", status.State);
        Assert.Null(await _world.GetStatusAsync("d-0000000000ff"));
    }
}
