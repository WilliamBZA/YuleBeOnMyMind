using Microsoft.AspNetCore.Authentication.Cookies;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.HttpOverrides;
using Microsoft.Extensions.Options;
using Yule.Server.Admin;
using Yule.Server.Configuration;
using Yule.Server.Security;

namespace Yule.Server.Hosting;

public static class AdminAuth
{
    public const string Scheme = "YuleAdmin";
}

public static class AdminServiceCollectionExtensions
{
    /// <summary>Registers the admin pages, the single-administrator cookie login and the services behind the pages.</summary>
    public static IServiceCollection AddYuleAdmin(this IServiceCollection services)
    {
        services.AddRazorPages();

        services.AddAuthentication(AdminAuth.Scheme).AddCookie(AdminAuth.Scheme, options =>
        {
            options.Cookie.Name = ".Yule.Admin";
            options.Cookie.HttpOnly = true;
            options.Cookie.SameSite = SameSiteMode.Strict;
            // Secure whenever the request arrived over https. Behind the reverse proxy that is known through
            // X-Forwarded-Proto (see YuleOptions.TrustForwardedHeaders); plain http is for local development.
            options.Cookie.SecurePolicy = CookieSecurePolicy.SameAsRequest;
            options.LoginPath = "/login";
            options.AccessDeniedPath = "/login";
            options.ExpireTimeSpan = TimeSpan.FromHours(8);
            options.SlidingExpiration = true;
        });

        // Everything requires the administrator unless it opts out with [AllowAnonymous] (the login page, the error
        // page, /healthz and /firmware/*). A page added without thinking about it is therefore protected.
        services.AddAuthorization(options =>
            options.FallbackPolicy = new AuthorizationPolicyBuilder().RequireAuthenticatedUser().Build());

        services.AddOptions<ForwardedHeadersOptions>().Configure<IOptions<YuleOptions>>((forwarded, yule) =>
        {
            if (!yule.Value.TrustForwardedHeaders)
            {
                forwarded.ForwardedHeaders = ForwardedHeaders.None;
                return;
            }

            forwarded.ForwardedHeaders = ForwardedHeaders.XForwardedFor | ForwardedHeaders.XForwardedProto;
            forwarded.KnownIPNetworks.Clear();
            forwarded.KnownProxies.Clear();
        });

        services.AddSingleton(TimeProvider.System);
        services.AddSingleton<LoginThrottle>();
        services.AddScoped<BrokerChangeApplier>();
        services.AddScoped<DeviceAdminService>();
        services.AddScoped<GroupAdminService>();
        services.AddScoped<ReleaseAdminService>();
        return services;
    }
}
