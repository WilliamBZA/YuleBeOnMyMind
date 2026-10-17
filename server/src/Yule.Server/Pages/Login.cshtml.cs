using System.Security.Claims;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.Extensions.Options;
using Yule.Server.Configuration;
using Yule.Server.Hosting;
using Yule.Server.Security;

namespace Yule.Server.Pages;

[AllowAnonymous]
public class LoginModel(IOptions<YuleOptions> options, LoginThrottle throttle, ILogger<LoginModel> logger) : PageModel
{
    [BindProperty]
    public string? Password { get; set; }

    /// <summary>Where the visitor was heading when they were sent here. Only addresses on this site are followed.</summary>
    [BindProperty(SupportsGet = true)]
    public string? ReturnUrl { get; set; }

    public string? ErrorMessage { get; private set; }

    public IActionResult OnGet() => User.Identity?.IsAuthenticated == true ? LocalRedirect(Destination()) : Page();

    public async Task<IActionResult> OnPostAsync()
    {
        // One spelling per address: the same caller must not count as two just because the socket reports it as IPv4-mapped IPv6.
        var address = HttpContext.Connection.RemoteIpAddress;
        var client = (address is { IsIPv4MappedToIPv6: true } ? address.MapToIPv4() : address)?.ToString() ?? "unknown";

        // Asked before the password is looked at, so a locked-out guesser learns nothing from the answer.
        if (throttle.GetLockout(client) is { } wait)
        {
            var minutes = Math.Max(1, (int)Math.Ceiling(wait.TotalMinutes));
            Response.Headers.RetryAfter = ((int)Math.Ceiling(wait.TotalSeconds)).ToString(System.Globalization.CultureInfo.InvariantCulture);
            Response.StatusCode = StatusCodes.Status429TooManyRequests;
            ErrorMessage = $"Too many failed attempts. Try again in about {Yule.Server.Admin.Display.Plural(minutes, "minute")}.";
            return Page();
        }

        if (!AdminPasswordHasher.Verify(Password ?? "", options.Value.Admin.PasswordHash))
        {
            throttle.RecordFailure(client);
            logger.LogWarning("Failed admin login from {Client}", client);
            ErrorMessage = "That password is not right.";
            return Page();
        }

        throttle.RecordSuccess(client);
        var identity = new ClaimsIdentity([new Claim(ClaimTypes.Name, "admin")], AdminAuth.Scheme);
        await HttpContext.SignInAsync(AdminAuth.Scheme, new ClaimsPrincipal(identity));
        logger.LogInformation("Admin logged in from {Client}", client);
        return LocalRedirect(Destination());
    }

    private string Destination() => !string.IsNullOrEmpty(ReturnUrl) && Url.IsLocalUrl(ReturnUrl) ? ReturnUrl : "/devices";
}
