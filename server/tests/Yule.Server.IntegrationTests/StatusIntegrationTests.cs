using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using MQTTnet.Formatter;
using Yule.Server.Broker;
using Yule.Server.Configuration;

namespace Yule.Server.IntegrationTests;

public class StatusIntegrationTests(MosquittoFixture broker) : IClassFixture<MosquittoFixture>, IAsyncLifetime
{
    private const string Offline = """{"v":1,"state":"offline"}""";

    private BrokerHarness _server = null!;
    private StatusListener _listener = null!;

    public async Task InitializeAsync() => _server = await BrokerHarness.StartAsync(broker);

    public async Task DisposeAsync()
    {
        if (_listener is not null)
        {
            await _listener.StopAsync(CancellationToken.None);
        }

        await _server.DisposeAsync();
    }

    private async Task StartListenerAsync()
    {
        _listener = new StatusListener(
            _server.Connection,
            _server.World.Services.GetRequiredService<IServiceScopeFactory>(),
            Options.Create(new YuleOptions { TopicRoot = _server.Root }),
            NullLogger<StatusListener>.Instance);
        await _listener.StartAsync(CancellationToken.None);
    }

    private async Task<string> RegisterAsync(string id)
    {
        var password = await _server.World.AddDeviceAsync(id);
        await _server.Sync.ApplyDevicesAsync([id]);
        return password;
    }

    private Task WaitForStatusAsync(string id, Func<Data.DeviceStatus, bool> condition, string what) =>
        Eventually.TrueAsync(async () => await _server.World.GetStatusAsync(id) is { } status && condition(status), what);

    [Fact]
    public async Task Online_offline_from_the_last_will_and_firmware_changes_are_recorded()
    {
        const string id = "d-00000000d001";
        var password = await RegisterAsync(id);
        await StartListenerAsync();

        // Version 5 so that the device can disconnect in the way that makes the broker publish its Last Will.
        await using var device = await TestDevice.ConnectAsync(
            broker, id, password, MqttProtocolVersion.V500, persistentSession: false, lastWill: (_server.StatusTopic(id), Offline));

        await device.PublishAsync(_server.StatusTopic(id), """{"v":1,"state":"online","fw":"0.2.0","board":"esp32dev","name":"Gran","ota":"none"}""", retain: true);
        await WaitForStatusAsync(id, s => s.State == "online" && s.FirmwareVersion == "0.2.0", "the online report");
        var online = (await _server.World.GetStatusAsync(id))!;
        Assert.Equal(("esp32dev", "Gran", "none"), (online.Board, online.Name, online.OtaResult));

        await device.PublishAsync(_server.StatusTopic(id), """{"v":1,"state":"online","fw":"0.3.0","board":"esp32dev","name":"Gran","ota":"ok"}""", retain: true);
        await WaitForStatusAsync(id, s => s.FirmwareVersion == "0.3.0" && s.OtaResult == "ok", "the new firmware version");

        await device.DisconnectWithWillAsync();
        await WaitForStatusAsync(id, s => s.State == "offline", "the Last Will");
        var offline = (await _server.World.GetStatusAsync(id))!;
        Assert.Equal("0.3.0", offline.FirmwareVersion);
    }

    [Fact]
    public async Task A_status_the_broker_already_holds_is_picked_up_when_the_server_starts_later()
    {
        const string id = "d-00000000d002";
        var password = await RegisterAsync(id);
        await using (var device = await TestDevice.ConnectAsync(broker, id, password, MqttProtocolVersion.V500, persistentSession: false))
        {
            await device.PublishAsync(_server.StatusTopic(id), """{"v":1,"state":"online","fw":"0.4.0","board":"esp32-c3","name":"Mum","ota":"rolled-back:0.3.9"}""", retain: true);
        }

        Assert.Null(await _server.World.GetStatusAsync(id));
        await StartListenerAsync();

        await WaitForStatusAsync(id, s => s.FirmwareVersion == "0.4.0", "the retained status");
        var status = (await _server.World.GetStatusAsync(id))!;
        Assert.Equal(("esp32-c3", "Mum", "rolled-back:0.3.9"), (status.Board, status.Name, status.OtaResult));
    }

    [Fact]
    public async Task A_device_cannot_publish_another_devices_status()
    {
        const string gran = "d-00000000d003";
        const string emma = "d-00000000d004";
        var granPassword = await RegisterAsync(gran);
        await RegisterAsync(emma);
        await StartListenerAsync();
        await using var granDevice = await TestDevice.ConnectAsync(broker, gran, granPassword, MqttProtocolVersion.V500, persistentSession: false);

        Assert.False(await granDevice.PublishAsync(_server.StatusTopic(emma), """{"state":"online","fw":"6.6.6"}""", retain: true));
        Assert.True(await granDevice.PublishAsync(_server.StatusTopic(gran), """{"state":"online","fw":"1.0.0"}""", retain: true));

        await WaitForStatusAsync(gran, s => s.FirmwareVersion == "1.0.0", "Gran's own status");
        Assert.Null(await _server.World.GetStatusAsync(emma));
    }
}
