using System.Net;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Routing;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.DependencyInjection;
using Yule.Server.Security;

namespace Yule.Server.Tests;

/// <summary>Spec <c>family-groups</c>, "Administration through the server": every admin page needs the administrator's login.</summary>
public class AdminLoginTests
{
    private const string AuthCookie = ".Yule.Admin";

    [Theory]
    [InlineData("/")]
    [InlineData("/devices")]
    [InlineData("/devices/d-3c71bf4a2b10")]
    [InlineData("/devices/d-3c71bf4a2b10/remove")]
    [InlineData("/groups")]
    [InlineData("/groups/g-aaaaaaaa")]
    [InlineData("/groups/g-aaaaaaaa/remove")]
    [InlineData("/status")]
    [InlineData("/logout")]
    public async Task Every_admin_page_sends_a_visitor_who_is_not_logged_in_to_the_login_page(string path)
    {
        using var site = new AdminSite();
        await site.SeedDeviceAsync("d-3c71bf4a2b10", "Secret device label");
        var client = site.NewClient();

        var response = await client.GetAsync(path);

        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        var location = AdminClient.Location(response);
        Assert.StartsWith("/login?", location);
        Assert.Contains("ReturnUrl=", location);
        Assert.DoesNotContain("Secret device label", await response.Content.ReadAsStringAsync());
    }

    [Fact]
    public async Task Posting_to_an_admin_page_without_logging_in_changes_nothing()
    {
        using var site = new AdminSite();
        var client = site.NewClient();

        var response = await client.PostWithTokenAsync("/groups", token: null, ("Name", "Sneaky group"));

        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        Assert.StartsWith("/login", AdminClient.Location(response));
        Assert.Empty(await site.WithDbAsync(db => db.Groups.ToListAsync()));
    }

    [Fact]
    public void Only_the_login_page_the_error_page_healthz_and_firmware_downloads_are_open_to_anyone()
    {
        using var site = new AdminSite();
        _ = site.Server; // starts the host

        var open = site.Services.GetRequiredService<EndpointDataSource>().Endpoints
            .Where(e => e.Metadata.GetMetadata<IAllowAnonymous>() is not null)
            .Select(e => e.DisplayName)
            .Order(StringComparer.Ordinal)
            .ToList();

        // Adding to this list is a decision about exposing a page without a login. Devices download firmware without one.
        Assert.Equal(["/Error", "/Login", "HTTP: GET /firmware/{file}", "HTTP: GET /healthz"], open);
    }

    [Fact]
    public async Task The_health_check_and_the_login_page_work_without_logging_in()
    {
        using var site = new AdminSite();
        var client = site.NewClient();

        Assert.Equal("ok", await client.GetStringAsync("/healthz"));
        var login = await client.GetAsync("/login");
        Assert.Equal(HttpStatusCode.OK, login.StatusCode);
        Assert.Contains("name=\"Password\"", await login.Content.ReadAsStringAsync());
    }

    [Fact]
    public async Task A_wrong_password_is_rejected_and_logs_nobody_in()
    {
        using var site = new AdminSite();
        var client = site.NewClient();

        var response = await client.TryLoginAsync("not the password");

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.Contains("That password is not right.", await response.Content.ReadAsStringAsync());
        Assert.DoesNotContain(AuthCookie, response.Headers.TryGetValues("Set-Cookie", out var cookies) ? string.Join(";", cookies) : "");
        Assert.Equal(HttpStatusCode.Redirect, (await client.GetAsync("/devices")).StatusCode);
    }

    [Fact]
    public async Task The_right_password_logs_in_and_opens_the_pages()
    {
        using var site = new AdminSite();
        var client = site.NewClient();

        var response = await client.TryLoginAsync(AdminSiteDefaults.Password);

        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        Assert.Equal("/devices", AdminClient.Location(response));
        foreach (var page in new[] { "/devices", "/groups", "/status" })
        {
            Assert.Equal(HttpStatusCode.OK, (await client.GetAsync(page)).StatusCode);
        }
    }

    [Fact]
    public async Task The_login_cookie_cannot_be_read_by_scripts_or_sent_from_other_sites()
    {
        using var site = new AdminSite();
        var client = site.NewClient();

        var response = await client.TryLoginAsync(AdminSiteDefaults.Password);

        var cookie = Assert.Single(response.Headers.GetValues("Set-Cookie"), c => c.StartsWith(AuthCookie, StringComparison.Ordinal));
        Assert.Contains("httponly", cookie, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("samesite=strict", cookie, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("secure", cookie, StringComparison.OrdinalIgnoreCase);
    }

    [Theory]
    [InlineData("/groups", "/groups")]
    [InlineData("/groups/g-aaaaaaaa?x=1", "/groups/g-aaaaaaaa?x=1")]
    [InlineData("https://evil.example/phish", "/devices")]
    [InlineData("//evil.example/phish", "/devices")]
    [InlineData("javascript:alert(1)", "/devices")]
    public async Task After_logging_in_the_visitor_goes_on_to_where_they_were_heading_but_only_on_this_site(string returnUrl, string expected)
    {
        using var site = new AdminSite();
        var client = site.NewClient();

        var response = await client.TryLoginAsync(AdminSiteDefaults.Password, returnUrl);

        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        Assert.Equal(expected, AdminClient.Location(response));
    }

    [Fact]
    public async Task Logging_in_needs_the_anti_forgery_token()
    {
        using var site = new AdminSite();
        var client = site.NewClient();
        await client.GetStringAsync("/login"); // gets the cookie half of the token pair

        var response = await client.PostWithTokenAsync("/login", token: null, ("Password", AdminSiteDefaults.Password));

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
        Assert.Equal(HttpStatusCode.Redirect, (await client.GetAsync("/devices")).StatusCode);
    }

    [Fact]
    public async Task Pages_that_change_things_need_the_anti_forgery_token()
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();

        var response = await client.PostWithTokenAsync("/groups", token: null, ("Name", "Forged group"));

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
        Assert.DoesNotContain("Forged group", await client.GetStringAsync("/groups"));
    }

    [Fact]
    public async Task Repeated_failures_are_throttled_even_for_the_right_password_until_the_wait_is_over()
    {
        using var site = new AdminSite();
        var client = site.NewClient();

        for (var i = 0; i < LoginThrottle.MaxFailuresPerClient; i++)
        {
            var failed = await client.TryLoginAsync("guess " + i);
            Assert.Equal(HttpStatusCode.OK, failed.StatusCode);
        }

        var throttled = await client.TryLoginAsync(AdminSiteDefaults.Password);

        Assert.Equal((HttpStatusCode)429, throttled.StatusCode);
        Assert.Contains("Too many failed attempts", await throttled.Content.ReadAsStringAsync());
        Assert.True(throttled.Headers.RetryAfter?.Delta > TimeSpan.Zero);
        Assert.Equal(HttpStatusCode.Redirect, (await client.GetAsync("/devices")).StatusCode);

        site.Time.Advance(LoginThrottle.Window + TimeSpan.FromSeconds(1));
        var allowed = await client.TryLoginAsync(AdminSiteDefaults.Password);

        Assert.Equal(HttpStatusCode.Redirect, allowed.StatusCode);
        Assert.Equal(HttpStatusCode.OK, (await client.GetAsync("/devices")).StatusCode);
    }

    [Fact]
    public async Task Attempts_made_while_locked_out_do_not_extend_the_lockout()
    {
        using var site = new AdminSite();
        var client = site.NewClient();
        for (var i = 0; i < LoginThrottle.MaxFailuresPerClient; i++)
        {
            await client.TryLoginAsync("guess " + i);
        }

        site.Time.Advance(TimeSpan.FromMinutes(10));
        Assert.Equal((HttpStatusCode)429, (await client.TryLoginAsync("another guess")).StatusCode);
        site.Time.Advance(TimeSpan.FromMinutes(5) + TimeSpan.FromSeconds(1));

        Assert.Equal(HttpStatusCode.Redirect, (await client.TryLoginAsync(AdminSiteDefaults.Password)).StatusCode);
    }

    [Fact]
    public async Task Failures_are_counted_per_client_address_when_the_proxy_headers_are_trusted()
    {
        using var site = new AdminSite(trustForwardedHeaders: true);
        var attacker = site.NewClient();
        attacker.Http.DefaultRequestHeaders.Add("X-Forwarded-For", "203.0.113.7");
        var admin = site.NewClient();
        admin.Http.DefaultRequestHeaders.Add("X-Forwarded-For", "198.51.100.20");

        for (var i = 0; i < LoginThrottle.MaxFailuresPerClient; i++)
        {
            await attacker.TryLoginAsync("guess " + i);
        }

        Assert.Equal((HttpStatusCode)429, (await attacker.TryLoginAsync(AdminSiteDefaults.Password)).StatusCode);
        Assert.Equal(HttpStatusCode.Redirect, (await admin.TryLoginAsync(AdminSiteDefaults.Password)).StatusCode);
    }

    [Fact]
    public async Task Forwarded_headers_are_ignored_unless_the_server_is_told_to_trust_them()
    {
        using var site = new AdminSite(trustForwardedHeaders: false);
        var forger = site.NewClient();

        // Varying the claimed address must not buy more guesses.
        for (var i = 0; i < LoginThrottle.MaxFailuresPerClient; i++)
        {
            forger.Http.DefaultRequestHeaders.Remove("X-Forwarded-For");
            forger.Http.DefaultRequestHeaders.Add("X-Forwarded-For", $"203.0.113.{i}");
            await forger.TryLoginAsync("guess " + i);
        }

        forger.Http.DefaultRequestHeaders.Remove("X-Forwarded-For");
        forger.Http.DefaultRequestHeaders.Add("X-Forwarded-For", "203.0.113.200");
        Assert.Equal((HttpStatusCode)429, (await forger.TryLoginAsync(AdminSiteDefaults.Password)).StatusCode);
    }

    [Fact]
    public async Task Logging_out_ends_the_session()
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();

        var response = await client.PostFormAsync("/logout", "/devices");

        Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        Assert.Equal("/login", AdminClient.Location(response));
        Assert.Equal(HttpStatusCode.Redirect, (await client.GetAsync("/devices")).StatusCode);
    }

    [Fact]
    public async Task Pages_are_never_cached_and_never_framed()
    {
        using var site = new AdminSite();
        var client = await site.LoggedInClientAsync();

        var response = await client.GetAsync("/devices");

        Assert.Contains("no-store", response.Headers.CacheControl?.ToString());
        Assert.Equal("DENY", response.Headers.GetValues("X-Frame-Options").Single());
        Assert.Contains("frame-ancestors 'none'", response.Headers.GetValues("Content-Security-Policy").Single());
    }
}
