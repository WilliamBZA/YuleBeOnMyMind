namespace Yule.Server.Tests;

/// <summary>The Status page: what each device last reported.</summary>
public class AdminStatusPageTests
{
    [Fact]
    public async Task It_shows_every_devices_state_firmware_board_last_update_result_and_last_seen()
    {
        using var site = new AdminSite();
        await site.SeedDeviceAsync("d-000000000001", "Gran", "beta");
        await site.SeedDeviceAsync("d-000000000002", "Grandpa");
        await site.SeedDeviceAsync("d-000000000003", "Emma");
        await site.SeedDeviceAsync("d-000000000004", "Tester");
        await site.SeedStatusAsync("d-000000000001", "online", "0.4.0", "esp32dev", "ok", TimeSpan.FromSeconds(5));
        await site.SeedStatusAsync("d-000000000002", "offline", "0.3.0", "esp32-c3", "none", TimeSpan.FromHours(2));
        await site.SeedStatusAsync("d-000000000003", "online", "0.3.0", "esp32-s3", "rolled-back:0.4.0", TimeSpan.FromMinutes(10));
        var client = await site.LoggedInClientAsync();

        var html = await client.GetStringAsync("/status");

        Assert.Contains("2 of 4 devices online", html);
        foreach (var expected in new[]
                 {
                     "Gran", "0.4.0", "esp32dev", "just now", "2026-12-01 11:59 UTC",
                     "Grandpa", "0.3.0", "esp32-c3", "none", "2 h ago",
                     "Emma", "esp32-s3", "rolled-back:0.4.0", "10 min ago",
                     "Tester", "never seen",
                 })
        {
            Assert.Contains(expected, html);
        }
    }

    [Fact]
    public async Task A_failed_or_rolled_back_update_is_flagged()
    {
        using var site = new AdminSite();
        await site.SeedDeviceAsync("d-000000000001", "Gran");
        await site.SeedDeviceAsync("d-000000000002", "Grandpa");
        await site.SeedStatusAsync("d-000000000001", ota: "failed:signature");
        await site.SeedStatusAsync("d-000000000002", ota: "ok");
        var client = await site.LoggedInClientAsync();

        var html = await client.GetStringAsync("/status");

        Assert.Contains("badge bad\">failed:signature", html);
        Assert.DoesNotContain("badge bad\">ok", html);
    }

    [Fact]
    public async Task It_refreshes_itself_so_the_state_stays_live()
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();

        var html = await client.GetStringAsync("/status");

        Assert.Contains("http-equiv=\"refresh\"", html);
        Assert.Contains("No devices are registered yet", html);
    }
}
