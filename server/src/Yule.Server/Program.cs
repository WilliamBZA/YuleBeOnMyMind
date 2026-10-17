using Microsoft.Extensions.Options;
using Yule.Server.Broker;
using Yule.Server.Configuration;
using Yule.Server.Hosting;

// `hash-password` prints a hash for Yule__Admin__PasswordHash and exits; it needs no configuration.
if (args is ["hash-password", ..])
{
    return HashPasswordCommand.Run(Console.In, Console.Out, Console.Error, interactive: !Console.IsInputRedirected);
}

var builder = WebApplication.CreateBuilder(args);
builder.Services.AddYuleCore(builder.Configuration);
builder.Services.AddYuleBroker();
builder.Services.AddYuleAdmin();

var app = builder.Build();

// Fail fast, with a message that says exactly what to set, if the configuration is incomplete.
var errors = YuleOptionsValidator.Validate(app.Services.GetRequiredService<IOptions<YuleOptions>>().Value);
if (errors.Count > 0)
{
    Console.Error.WriteLine("The Yule server cannot start because its configuration is incomplete or invalid:");
    foreach (var error in errors)
    {
        Console.Error.WriteLine($"  - {error}");
    }

    return 78; // EX_CONFIG
}

await app.Services.MigrateDatabaseAsync();

// Behind the reverse proxy this makes the client's address and the https scheme visible (when configured to trust it).
app.UseForwardedHeaders();

if (!app.Environment.IsDevelopment())
{
    app.UseExceptionHandler("/error");
}

// Static files (the stylesheet) come first so they can be cached. Everything after them is a page that holds device
// and group data or a one-time password, so it is never cached, framed or loaded from anywhere but this server.
app.UseStaticFiles();

app.Use(async (context, next) =>
{
    context.Response.OnStarting(() =>
    {
        var headers = context.Response.Headers;
        headers.CacheControl = "no-store";
        headers.XContentTypeOptions = "nosniff";
        headers["X-Frame-Options"] = "DENY";
        headers["Referrer-Policy"] = "no-referrer";
        headers.ContentSecurityPolicy = "default-src 'self'; img-src 'self' data:; frame-ancestors 'none'; base-uri 'none'; form-action 'self'";
        return Task.CompletedTask;
    });
    await next();
});

app.UseRouting();
app.UseAuthentication();
app.UseAuthorization();

app.MapGet("/healthz", () => Results.Text("ok")).AllowAnonymous();
app.MapRazorPages();

await app.RunAsync();
return 0;

// Lets the integration tests host the real application with WebApplicationFactory<Program>.
public partial class Program;
