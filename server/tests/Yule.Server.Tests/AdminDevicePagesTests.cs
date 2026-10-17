using System.Net;
using System.Text.RegularExpressions;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.DependencyInjection;
using Yule.Server.Security;

namespace Yule.Server.Tests;

/// <summary>The Devices pages (spec <c>family-groups</c>, "Administration through the server").</summary>
public partial class AdminDevicePagesTests
{
    private const string DeviceId = "d-3c71bf4a2b10";

    [GeneratedRegex("<code id=\"broker-password\">([^<]+)</code>")]
    private static partial Regex ShownPassword();

    private static string? PasswordIn(string html) => ShownPassword().Match(html) is { Success: true } m ? m.Groups[1].Value : null;

    private static Task<HttpResponseMessage> RegisterAsync(AdminClient client, string id, string label) =>
        client.PostFormAsync("/devices", "/devices", ("Input.Id", id), ("Input.Label", label));

    [Fact]
    public async Task Registering_a_device_shows_a_generated_password_once_and_stores_it_encrypted()
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();

        var response = await RegisterAsync(client, DeviceId, "Gran (Brander)");
        var html = await response.Content.ReadAsStringAsync();

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        var password = PasswordIn(html);
        Assert.NotNull(password);
        Assert.Equal(24, password.Length);
        Assert.Contains("only this once", html);
        Assert.Contains("no-store", response.Headers.CacheControl?.ToString());

        var stored = await site.WithDbAsync(db => db.Devices.SingleAsync());
        Assert.Equal(DeviceId, stored.Id);
        Assert.Equal("Gran (Brander)", stored.Label);
        Assert.Equal("stable", stored.Channel);
        Assert.DoesNotContain(password, stored.EncryptedPassword);
        Assert.Equal(password, site.Services.GetRequiredService<IBrokerPasswordProtector>().Unprotect(stored.EncryptedPassword));

        // The broker account is made at once, so the device can connect as soon as the password is typed in.
        Assert.Equal([DeviceId], site.Broker.AppliedIds);

        // And nowhere does the password appear again.
        foreach (var page in new[] { "/devices", $"/devices/{DeviceId}", $"/devices/{DeviceId}/remove", "/groups", "/status" })
        {
            Assert.DoesNotContain(password, await client.GetStringAsync(page));
        }
    }

    [Fact]
    public async Task Each_registration_gets_its_own_password()
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();

        var first = PasswordIn(await (await RegisterAsync(client, DeviceId, "One")).Content.ReadAsStringAsync());
        var second = PasswordIn(await (await RegisterAsync(client, "d-000000000001", "Tester")).Content.ReadAsStringAsync());

        Assert.NotNull(first);
        Assert.NotNull(second);
        Assert.NotEqual(first, second);
    }

    [Fact]
    public async Task Registering_the_same_device_again_is_refused_and_shows_no_password()
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();
        var first = PasswordIn(await (await RegisterAsync(client, DeviceId, "Gran")).Content.ReadAsStringAsync());
        site.Broker.Clear();

        var again = await RegisterAsync(client, DeviceId, "Gran");
        var html = await again.Content.ReadAsStringAsync();

        Assert.Contains("already registered", html);
        Assert.Null(PasswordIn(html));
        Assert.Empty(site.Broker.Applied);
        var stored = await site.WithDbAsync(db => db.Devices.SingleAsync());
        Assert.Equal(first, site.Services.GetRequiredService<IBrokerPasswordProtector>().Unprotect(stored.EncryptedPassword));
    }

    [Theory]
    [InlineData("")]
    [InlineData("   ")]
    [InlineData("d-3c71bf4a2b1")]
    [InlineData("d-3c71bf4a2b100")]
    [InlineData("d-3c71bf4a2b1g")]
    [InlineData("3c71bf4a2b10")]
    [InlineData("x-3c71bf4a2b10")]
    [InlineData("d-3c71bf4a2b10; drop table")]
    public async Task A_malformed_device_id_is_refused_and_nothing_is_registered(string id)
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();

        var response = await RegisterAsync(client, id, "Gran");
        var html = await response.Content.ReadAsStringAsync();

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.Contains("A device id is d- followed by 12", html);
        Assert.Null(PasswordIn(html));
        Assert.Empty(await site.WithDbAsync(db => db.Devices.ToListAsync()));
        Assert.Empty(site.Broker.Applied);
    }

    [Fact]
    public async Task A_device_id_typed_with_capitals_or_spaces_is_registered_in_its_proper_form()
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();

        var response = await RegisterAsync(client, "  D-3C71BF4A2B10 ", "Gran");

        Assert.NotNull(PasswordIn(await response.Content.ReadAsStringAsync()));
        Assert.Equal(DeviceId, (await site.WithDbAsync(db => db.Devices.SingleAsync())).Id);
    }

    [Theory]
    [InlineData("")]
    [InlineData("    ")]
    public async Task A_device_needs_a_label(string label)
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();

        var html = await (await RegisterAsync(client, DeviceId, label)).Content.ReadAsStringAsync();

        Assert.Contains("Give the device a label", html);
        Assert.Contains(DeviceId, html); // what was typed is kept
        Assert.Null(PasswordIn(html));
        Assert.Empty(await site.WithDbAsync(db => db.Devices.ToListAsync()));
    }

    [Fact]
    public async Task A_label_longer_than_sixty_characters_is_refused()
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();

        var html = await (await RegisterAsync(client, DeviceId, new string('x', 61))).Content.ReadAsStringAsync();

        Assert.Contains("Give the device a label", html);
        Assert.Empty(await site.WithDbAsync(db => db.Devices.ToListAsync()));
    }

    [Fact]
    public async Task The_list_shows_each_devices_groups_and_live_status()
    {
        using var site = new AdminSite();
        await site.SeedGroupAsync("g-aaaaaaaa", "Brander Grandparents");
        await site.SeedGroupAsync("g-bbbbbbbb", "Smith Grandparents");
        await site.SeedDeviceAsync("d-000000000001", "Emma", "stable", "g-aaaaaaaa", "g-bbbbbbbb");
        await site.SeedDeviceAsync("d-000000000002", "Gran", "beta");
        await site.SeedDeviceAsync("d-000000000003", "Grandpa");
        await site.SeedStatusAsync("d-000000000001", "online", seenAgo: TimeSpan.FromMinutes(3));
        await site.SeedStatusAsync("d-000000000002", "offline", seenAgo: TimeSpan.FromHours(5));
        var client = await site.LoggedInClientAsync();

        var html = await client.GetStringAsync("/devices");

        Assert.Contains("3 devices", html);
        Assert.Contains("Brander Grandparents", html);
        Assert.Contains("Smith Grandparents", html);
        Assert.Contains("badge online", html);
        Assert.Contains("3 min ago", html);
        Assert.Contains("badge offline", html);
        Assert.Contains("5 h ago", html);
        Assert.Contains("never seen", html);
        Assert.Contains("beta", html);
    }

    [Fact]
    public async Task Editing_the_channel_updates_the_device_and_the_broker()
    {
        using var site = new AdminSite();
        await site.SeedDeviceAsync(DeviceId, "Gran");
        var client = await site.LoggedInClientAsync();
        var path = $"/devices/{DeviceId}";

        var response = await client.PostFormAsync(path, path, ("Label", "Gran (Brander)"), ("Channel", "beta"));

        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        Assert.Equal("/devices", AdminClient.Location(response));
        var stored = await site.WithDbAsync(db => db.Devices.SingleAsync());
        Assert.Equal("Gran (Brander)", stored.Label);
        Assert.Equal("beta", stored.Channel);
        Assert.Equal([DeviceId], site.Broker.AppliedIds);
        Assert.Contains("Saved Gran (Brander).", await client.GetStringAsync("/devices"));
        Assert.DoesNotContain("Saved Gran", await client.GetStringAsync("/devices")); // the message is shown once
    }

    [Fact]
    public async Task Changing_only_the_label_does_not_touch_the_broker()
    {
        using var site = new AdminSite();
        await site.SeedDeviceAsync(DeviceId, "Gran");
        var client = await site.LoggedInClientAsync();
        var path = $"/devices/{DeviceId}";

        await client.PostFormAsync(path, path, ("Label", "Granny"), ("Channel", "stable"));

        Assert.Equal("Granny", (await site.WithDbAsync(db => db.Devices.SingleAsync())).Label);
        Assert.Empty(site.Broker.Applied);
    }

    [Theory]
    [InlineData("nightly", "Gran")]
    [InlineData("", "Gran")]
    [InlineData("stable", "")]
    public async Task Editing_with_invalid_values_is_refused_and_changes_nothing(string channel, string label)
    {
        using var site = new AdminSite();
        await site.SeedDeviceAsync(DeviceId, "Gran");
        var client = await site.LoggedInClientAsync();
        var path = $"/devices/{DeviceId}";

        var response = await client.PostFormAsync(path, path, ("Label", label), ("Channel", channel));

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        var stored = await site.WithDbAsync(db => db.Devices.SingleAsync());
        Assert.Equal("Gran", stored.Label);
        Assert.Equal("stable", stored.Channel);
        Assert.Empty(site.Broker.Applied);
    }

    [Fact]
    public async Task A_device_that_does_not_exist_is_not_found()
    {
        using var site = new AdminSite();
        await site.SeedDeviceAsync(DeviceId, "Gran");
        var client = await site.LoggedInClientAsync();

        Assert.Equal(HttpStatusCode.NotFound, (await client.GetAsync("/devices/d-ffffffffffff")).StatusCode);
        Assert.Equal(HttpStatusCode.NotFound, (await client.GetAsync("/devices/d-ffffffffffff/remove")).StatusCode);
        var post = await client.PostFormAsync("/devices/d-ffffffffffff/remove", $"/devices/{DeviceId}/remove");
        Assert.Equal(HttpStatusCode.NotFound, post.StatusCode);
        Assert.Single(await site.WithDbAsync(db => db.Devices.ToListAsync()));
    }

    [Fact]
    public async Task Removing_asks_for_confirmation_first_and_removes_nothing_until_confirmed()
    {
        using var site = new AdminSite();
        await site.SeedGroupAsync("g-aaaaaaaa", "Brander Grandparents");
        await site.SeedDeviceAsync(DeviceId, "Gran", "stable", "g-aaaaaaaa");
        var client = await site.LoggedInClientAsync();

        var html = await client.GetStringAsync($"/devices/{DeviceId}/remove");

        Assert.Contains("Remove Gran?", html);
        Assert.Contains("Brander Grandparents", html);
        Assert.Single(await site.WithDbAsync(db => db.Devices.ToListAsync()));
        Assert.Empty(site.Broker.Applied);
    }

    [Fact]
    public async Task Removing_a_device_deletes_it_and_its_memberships_and_revokes_its_broker_access()
    {
        using var site = new AdminSite();
        await site.SeedGroupAsync("g-aaaaaaaa", "Brander Grandparents");
        await site.SeedDeviceAsync(DeviceId, "Gran", "stable", "g-aaaaaaaa");
        await site.SeedDeviceAsync("d-000000000002", "Grandpa", "stable", "g-aaaaaaaa");
        await site.SeedStatusAsync(DeviceId);
        var client = await site.LoggedInClientAsync();
        var path = $"/devices/{DeviceId}/remove";

        var response = await client.PostFormAsync(path, path);

        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        Assert.Equal("/devices", AdminClient.Location(response));
        Assert.Equal(["d-000000000002"], (await site.WithDbAsync(db => db.Devices.ToListAsync())).Select(d => d.Id));
        Assert.Equal(["d-000000000002"], (await site.WithDbAsync(db => db.Memberships.ToListAsync())).Select(m => m.DeviceId));
        Assert.Empty(await site.WithDbAsync(db => db.DeviceStatuses.ToListAsync()));
        Assert.Single(await site.WithDbAsync(db => db.Groups.ToListAsync()));

        // The broker is told about the removal straight away: the account is deleted and its connection closed.
        Assert.Equal([DeviceId], site.Broker.AppliedIds);
        Assert.Contains("Removed Gran.", await client.GetStringAsync("/devices"));
        Assert.Equal(HttpStatusCode.NotFound, (await client.GetAsync($"/devices/{DeviceId}")).StatusCode);
    }

    [Fact]
    public async Task When_the_broker_cannot_be_reached_the_change_is_still_saved_and_the_administrator_is_told()
    {
        using var site = new AdminSite();
        site.Broker.Behaviour = _ => throw new InvalidOperationException("no route to host");
        var client = await site.LoggedInClientAsync();

        var html = await (await RegisterAsync(client, DeviceId, "Gran")).Content.ReadAsStringAsync();

        // Registered, with the password still shown (this is its only chance), and an honest warning.
        Assert.NotNull(PasswordIn(html));
        Assert.Contains("could not be updated just now", html);
        Assert.Contains("no route to host", html);
        Assert.Single(await site.WithDbAsync(db => db.Devices.ToListAsync()));
    }

    [Fact]
    public async Task A_broker_that_does_not_answer_does_not_hang_the_page()
    {
        using var site = new AdminSite(brokerTimeout: TimeSpan.FromMilliseconds(200));
        site.Broker.Behaviour = ct => Task.Delay(Timeout.Infinite, ct);
        await site.SeedDeviceAsync(DeviceId, "Gran");
        var client = await site.LoggedInClientAsync();
        var path = $"/devices/{DeviceId}/remove";

        var response = await client.PostFormAsync(path, path);

        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        Assert.Empty(await site.WithDbAsync(db => db.Devices.ToListAsync()));
        var next = await client.GetStringAsync("/devices");
        Assert.Contains("did not answer within", next);
        Assert.Contains("Removed Gran.", next);
    }
}
