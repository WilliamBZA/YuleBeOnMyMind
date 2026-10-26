using Microsoft.Extensions.Options;
using Yule.Server.Configuration;

namespace Yule.Server.Releases;

public static class FirmwareDownloads
{
    public const string PathPrefix = "/firmware";

    /// <summary>
    /// <c>GET /firmware/{file}</c>: the published images, without a login (devices download them). Names are
    /// content-addressed, so a response can be cached for good.
    /// </summary>
    public static IEndpointRouteBuilder MapFirmwareDownloads(this IEndpointRouteBuilder endpoints)
    {
        endpoints.MapGet(PathPrefix + "/{file}", (string file, IOptions<YuleOptions> options) =>
        {
            if (!FirmwareManifests.IsFileName(file))
            {
                return Results.NotFound();
            }

            var path = Path.Combine(FirmwareManifests.Directory(options.Value), file);
            return File.Exists(path) ? Results.File(path, "application/octet-stream") : Results.NotFound();
        }).AllowAnonymous();
        return endpoints;
    }

    public static bool IsDownload(PathString path) => path.StartsWithSegments(PathPrefix);
}
