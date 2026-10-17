using System.Diagnostics;
using System.Net;
using System.Security.Cryptography;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;
using Microsoft.AspNetCore.Mvc.Testing;
using Yule.Server.Security;
using Yule.Server.Tests;

namespace Yule.Server.IntegrationTests;

/// <summary>
/// The admin pages driven over HTTP, exactly as the administrator would, against the real application and a real
/// Mosquitto with dynamic security. Devices are stand-ins that connect the way the firmware does, with the password
/// the Devices page showed. Every test uses its own ids: persistent sessions are keyed by client id and outlive a test.
/// </summary>
public class AdminPagesIntegrationTests(MosquittoFixture broker) : IClassFixture<MosquittoFixture>, IAsyncLifetime
{
    private const string Root = "yule/v1";
    private static readonly Regex ShownPassword = new("<code id=\"broker-password\">([^<]+)</code>", RegexOptions.Compiled);

    private readonly string _dataDirectory = Path.Combine(Path.GetTempPath(), "yule-pages-" + Guid.NewGuid().ToString("N"));
    private readonly List<TestDevice> _devices = [];
    private WebApplicationFactory<Program> _factory = null!;
    private AdminClient _admin = null!;

    public async Task InitializeAsync()
    {
        var serverOptions = broker.ServerOptions();
        using var releaseKey = ECDsa.Create(ECCurve.NamedCurves.nistP256);
        _factory = new WebApplicationFactory<Program>().WithWebHostBuilder(host =>
        {
            host.UseSetting("Yule:Admin:PasswordHash", AdminPasswordHasher.Hash(AdminSiteDefaults.Password, iterations: 2_000));
            host.UseSetting("Yule:Broker:Host", serverOptions.Host);
            host.UseSetting("Yule:Broker:Port", serverOptions.Port.ToString());
            host.UseSetting("Yule:Broker:Username", serverOptions.Username);
            host.UseSetting("Yule:Broker:Password", serverOptions.Password);
            host.UseSetting("Yule:Broker:ClientId", serverOptions.ClientId);
            host.UseSetting("Yule:PublicBaseUrl", "https://yule.example.org");
            host.UseSetting("Yule:ReleasePublicKeys:0", releaseKey.ExportSubjectPublicKeyInfoPem());
            host.UseSetting("Yule:DataDirectory", _dataDirectory);
        });
        _admin = new AdminClient(_factory.CreateClient(new WebApplicationFactoryClientOptions
        {
            AllowAutoRedirect = false,
            BaseAddress = new Uri("https://localhost"),
        }));
        await _admin.LoginAsync();
    }

    public async Task DisposeAsync()
    {
        foreach (var device in _devices)
        {
            await device.DisposeAsync();
        }

        await _factory.DisposeAsync();
        Microsoft.Data.Sqlite.SqliteConnection.ClearAllPools();
        try
        {
            Directory.Delete(_dataDirectory, recursive: true);
        }
        catch (IOException)
        {
        }
    }

    private static string NewDeviceId() => "d-" + Guid.NewGuid().ToString("N")[..12];

    private static string ConfigTopic(string deviceId) => $"{Root}/devices/{deviceId}/config";

    private static string PressTopic(string groupId, string deviceId) => $"{Root}/groups/{groupId}/press/{deviceId}";

    private static string PressFilter(string groupId) => $"{Root}/groups/{groupId}/press/+";

    private async Task<(string Id, string Password)> RegisterAsync(string label)
    {
        var id = NewDeviceId();
        var response = await _admin.PostFormAsync("/devices", "/devices", ("Input.Id", id), ("Input.Label", label));
        var html = await response.Content.ReadAsStringAsync();
        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        var match = ShownPassword.Match(html);
        Assert.True(match.Success, "the Devices page should show the new password");
        Assert.DoesNotContain("could not be updated", html);
        return (id, match.Groups[1].Value);
    }

    private async Task<string> CreateGroupAsync(string name)
    {
        var response = await _admin.PostFormAsync("/groups", "/groups", ("Name", name));
        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        return AdminClient.Location(response)["/groups/".Length..];
    }

    private async Task PostAsync(string path, string handler, params (string, string)[] fields)
    {
        var response = await _admin.PostFormAsync($"{path}?handler={handler}", path, fields);
        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        Assert.DoesNotContain("could not be updated", await _admin.GetStringAsync(path));
    }

    /// <summary>Connects as the firmware does: persistent session, reconnecting by itself, subscribing to its own config each time.</summary>
    private async Task<TestDevice> ConnectAsync(string id, string password, params string[] groups)
    {
        var device = await TestDevice.ConnectAsync(broker, id, password, autoReconnect: true, onConnected: async d =>
        {
            await d.TrySubscribeAsync(ConfigTopic(id));
            foreach (var group in groups)
            {
                await d.TrySubscribeAsync(PressFilter(group));
            }
        });
        _devices.Add(device);
        return device;
    }

    private static string? LatestConfig(TestDevice device) =>
        device.Received.LastOrDefault(m => m.Topic == ConfigTopic(device.Id)).Payload;

    private static List<string> GroupIdsIn(string? config) =>
        config is null ? [] : JsonNode.Parse(config)!["groups"]!.AsArray().Select(g => g!["id"]!.GetValue<string>()).ToList();

    private static List<string> GroupNamesIn(string? config) =>
        config is null ? [] : JsonNode.Parse(config)!["groups"]!.AsArray().Select(g => g!["name"]!.GetValue<string>()).ToList();

    private static string Press(string from) => $$"""{"v":1,"id":"{{Guid.NewGuid():N}}","from":"{{from}}","name":"{{from}}"}""";

    [Fact]
    public async Task A_device_registered_on_the_page_connects_with_the_password_that_was_shown()
    {
        var (id, password) = await RegisterAsync("Gran (Brander)");

        await using (var device = await TestDevice.ConnectAsync(broker, id, password))
        {
            Assert.True(device.IsConnected);
            Assert.True(await device.TrySubscribeAsync(ConfigTopic(id)));

            // Registered but in no group: it gets its (empty) config, and may not listen to any group.
            var config = await device.WaitForAsync(ConfigTopic(id));
            Assert.NotNull(config);
            Assert.Empty(GroupIdsIn(config));
            Assert.Equal("stable", JsonNode.Parse(config)!["channel"]!.GetValue<string>());
            Assert.False(await device.TrySubscribeAsync(PressFilter("g-aaaaaaaa")));
        }

        // Any other password, or any other device id, is refused.
        await Assert.ThrowsAsync<ConnectionRefusedException>(() => TestDevice.ConnectAsync(broker, id, password + "x"));
        await Assert.ThrowsAsync<ConnectionRefusedException>(() => TestDevice.ConnectAsync(broker, NewDeviceId(), password));
    }

    [Fact]
    public async Task Changing_the_channel_on_the_page_reaches_the_broker_and_the_device()
    {
        var (id, password) = await RegisterAsync("Gran");
        var device = await ConnectAsync(id, password);
        Assert.NotNull(await device.WaitForAsync(ConfigTopic(id)));
        Assert.False(await device.TrySubscribeAsync($"{Root}/firmware/beta/esp32dev"), "a stable device may not follow the beta channel");

        var response = await _admin.PostFormAsync($"/devices/{id}", $"/devices/{id}", ("Label", "Gran"), ("Channel", "beta"));

        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);

        // The device may reconnect (it is dropped when its permissions change) just before or just after the new
        // config is published, so it can see the old one once more first; what matters is that it ends up with the new.
        await Eventually.TrueAsync(() => LatestConfig(device)?.Contains("\"channel\":\"beta\"") == true, "the device to be handed the beta channel");
        await Eventually.TrueAsync(() => device.IsConnected, "the device to be back");
        Assert.True(await device.TrySubscribeAsync($"{Root}/firmware/beta/esp32dev"));
    }

    [Fact]
    public async Task A_membership_change_made_on_the_page_reaches_the_broker_and_the_devices_config_within_ten_seconds()
    {
        var gran = await RegisterAsync("Gran");
        var emma = await RegisterAsync("Emma");
        var group = await CreateGroupAsync("Brander Grandparents");
        var granDevice = await ConnectAsync(gran.Id, gran.Password, group);
        var emmaDevice = await ConnectAsync(emma.Id, emma.Password, group);
        Assert.NotNull(await emmaDevice.WaitForAsync(ConfigTopic(emma.Id)));

        // Added: the broker drops the device, it comes back and is handed the new config by the broker straight away.
        var timer = Stopwatch.StartNew();
        await PostAsync($"/groups/{group}", "AddMember", ("DeviceId", gran.Id));
        await PostAsync($"/groups/{group}", "AddMember", ("DeviceId", emma.Id));
        await Eventually.TrueAsync(() => GroupIdsIn(LatestConfig(emmaDevice)).Contains(group), "Emma's device to be handed the group");
        await Eventually.TrueAsync(() => GroupIdsIn(LatestConfig(granDevice)).Contains(group), "Gran's device to be handed the group");
        Assert.Equal(["Brander Grandparents"], GroupNamesIn(LatestConfig(emmaDevice)));
        Assert.True(timer.Elapsed < TimeSpan.FromSeconds(10), $"took {timer.Elapsed.TotalSeconds:0.0} s");

        // They can now hear each other, with nobody touching either device.
        await Eventually.TrueAsync(() => emmaDevice.IsConnected && granDevice.IsConnected, "both devices to be back");
        Assert.True(await emmaDevice.TrySubscribeAsync(PressFilter(group)));
        Assert.True(await granDevice.PublishAsync(PressTopic(group, gran.Id), Press(gran.Id)));
        Assert.NotNull(await emmaDevice.WaitForAsync(PressTopic(group, gran.Id)));

        // Renamed: every member is handed the new name.
        timer.Restart();
        await PostAsync($"/groups/{group}", "Rename", ("Name", "The Branders"));
        await Eventually.TrueAsync(() => GroupNamesIn(LatestConfig(emmaDevice)).SequenceEqual(["The Branders"]), "the new name to reach Emma's device");
        Assert.True(timer.Elapsed < TimeSpan.FromSeconds(10), $"took {timer.Elapsed.TotalSeconds:0.0} s");

        // Removed: the device is told, and the broker stops sending it that group's presses, even on its old session.
        timer.Restart();
        await PostAsync($"/groups/{group}", "RemoveMember", ("DeviceId", emma.Id));
        await Eventually.TrueAsync(() => GroupIdsIn(LatestConfig(emmaDevice)).Count == 0, "Emma's device to be told it left the group");
        Assert.True(timer.Elapsed < TimeSpan.FromSeconds(10), $"took {timer.Elapsed.TotalSeconds:0.0} s");
        await Eventually.TrueAsync(() => emmaDevice.IsConnected, "Emma's device to be back");
        var heard = emmaDevice.CountOn(PressTopic(group, gran.Id));
        Assert.True(await granDevice.PublishAsync(PressTopic(group, gran.Id), Press(gran.Id)));
        await Task.Delay(700);
        Assert.Equal(heard, emmaDevice.CountOn(PressTopic(group, gran.Id)));
        Assert.False(await emmaDevice.TrySubscribeAsync(PressFilter(group)));
    }

    [Fact]
    public async Task Deleting_a_group_on_the_page_stops_its_members_hearing_each_other()
    {
        var gran = await RegisterAsync("Gran");
        var emma = await RegisterAsync("Emma");
        var group = await CreateGroupAsync("Brander Grandparents");
        await PostAsync($"/groups/{group}", "AddMember", ("DeviceId", gran.Id));
        await PostAsync($"/groups/{group}", "AddMember", ("DeviceId", emma.Id));
        var granDevice = await ConnectAsync(gran.Id, gran.Password, group);
        var emmaDevice = await ConnectAsync(emma.Id, emma.Password, group);
        Assert.True(await granDevice.PublishAsync(PressTopic(group, gran.Id), Press(gran.Id)));
        Assert.NotNull(await emmaDevice.WaitForAsync(PressTopic(group, gran.Id)));
        var timer = Stopwatch.StartNew();

        var response = await _admin.PostFormAsync($"/groups/{group}/remove", $"/groups/{group}/remove");

        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        await Eventually.TrueAsync(() => GroupIdsIn(LatestConfig(emmaDevice)).Count == 0, "Emma's device to be told the group is gone");
        Assert.True(timer.Elapsed < TimeSpan.FromSeconds(10), $"took {timer.Elapsed.TotalSeconds:0.0} s");
        await Eventually.TrueAsync(() => emmaDevice.IsConnected && granDevice.IsConnected, "both devices to be back");
        Assert.False(await granDevice.TrySubscribeAsync(PressFilter(group)));
        Assert.False(await emmaDevice.TrySubscribeAsync(PressFilter(group)));
    }

    [Fact]
    public async Task Removing_a_device_on_the_page_closes_its_connection_and_refuses_its_next_one()
    {
        var gran = await RegisterAsync("Gran");
        var emma = await RegisterAsync("Emma");
        var group = await CreateGroupAsync("Brander Grandparents");
        await PostAsync($"/groups/{group}", "AddMember", ("DeviceId", gran.Id));
        await PostAsync($"/groups/{group}", "AddMember", ("DeviceId", emma.Id));
        var granDevice = await ConnectAsync(gran.Id, gran.Password, group);
        var emmaDevice = await ConnectAsync(emma.Id, emma.Password, group);
        emmaDevice.AutoReconnect = false;

        var response = await _admin.PostFormAsync($"/devices/{emma.Id}/remove", $"/devices/{emma.Id}/remove");

        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        await Eventually.TrueAsync(() => !emmaDevice.IsConnected, "the removed device to be disconnected");
        await Assert.ThrowsAsync<ConnectionRefusedException>(() => TestDevice.ConnectAsync(broker, emma.Id, emma.Password));

        // Gran is untouched, and Emma's old presses no longer reach anyone.
        Assert.True(granDevice.IsConnected);
        Assert.Equal(0, granDevice.Disconnects);
        Assert.Contains("Gran", await _admin.GetStringAsync($"/groups/{group}"));
        Assert.DoesNotContain("Emma", await _admin.GetStringAsync($"/groups/{group}"));
    }

    [Fact]
    public async Task The_status_page_follows_two_devices_as_they_report_and_as_one_drops_off()
    {
        var gran = await RegisterAsync("Gran");
        var grandpa = await RegisterAsync("Grandpa");
        const string Offline = """{"v":1,"state":"offline"}""";
        await using var granDevice = await TestDevice.ConnectAsync(
            broker, gran.Id, gran.Password, MQTTnet.Formatter.MqttProtocolVersion.V500, persistentSession: false,
            lastWill: ($"{Root}/devices/{gran.Id}/status", Offline));
        await using var grandpaDevice = await TestDevice.ConnectAsync(
            broker, grandpa.Id, grandpa.Password, MQTTnet.Formatter.MqttProtocolVersion.V500, persistentSession: false,
            lastWill: ($"{Root}/devices/{grandpa.Id}/status", Offline));

        // Each reports itself the way the firmware does: different versions, boards and update results.
        await granDevice.PublishAsync($"{Root}/devices/{gran.Id}/status", """{"v":1,"state":"online","fw":"0.9.0","board":"esp32dev","name":"Gran","ota":"ok"}""", retain: true);
        await grandpaDevice.PublishAsync($"{Root}/devices/{grandpa.Id}/status", """{"v":1,"state":"online","fw":"0.8.0","board":"esp32-c3","name":"Grandpa","ota":"rolled-back:0.9.0"}""", retain: true);

        await Eventually.TrueAsync(async () =>
        {
            var html = await _admin.GetStringAsync("/status");
            return html.Contains("2 of 2 devices online") && html.Contains("0.9.0") && html.Contains("esp32-c3")
                   && html.Contains("0.8.0") && html.Contains("rolled-back:0.9.0");
        }, "the Status page to show both devices online with their own versions, boards and update results");

        // One loses its connection without saying goodbye: the broker publishes its Last Will.
        await grandpaDevice.DisconnectWithWillAsync();

        await Eventually.TrueAsync(async () =>
        {
            var html = await _admin.GetStringAsync("/status");
            return html.Contains("1 of 2 devices online") && html.Contains("badge offline");
        }, "the Status page to show Grandpa's device offline while Gran's stays online");
        Assert.Contains("badge online", await _admin.GetStringAsync("/status"));
    }
}
