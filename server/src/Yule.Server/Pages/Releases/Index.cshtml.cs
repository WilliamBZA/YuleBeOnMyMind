using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Yule.Server.Admin;

namespace Yule.Server.Pages.Releases;

// A signed image is at most one app slot (about 1.9 MB); this leaves room for the manifest and the form itself.
[RequestFormLimits(MultipartBodyLengthLimit = MaxUploadBytes)]
[RequestSizeLimit(MaxUploadBytes)]
public class IndexModel(ReleaseAdminService releases) : PageModel
{
    public const int MaxUploadBytes = 4 * 1024 * 1024;
    private const int MaxManifestBytes = 16 * 1024;

    public IReadOnlyList<ReleaseView> Releases { get; private set; } = [];

    public IReadOnlyList<ChannelRelease> Current { get; private set; } = [];

    /// <summary>Why the last upload was refused, shown above the form.</summary>
    public IReadOnlyList<string> UploadErrors { get; private set; } = [];

    [BindProperty]
    public IFormFile? Image { get; set; }

    [BindProperty]
    public IFormFile? Manifest { get; set; }

    public async Task OnGetAsync(CancellationToken cancellationToken) => await LoadAsync(cancellationToken);

    public async Task<IActionResult> OnPostUploadAsync(CancellationToken cancellationToken)
    {
        var errors = new List<string>();
        if (Image is null || Image.Length == 0)
        {
            errors.Add("Choose the signed firmware image (yule-<version>-<board>.bin).");
        }

        if (Manifest is null || Manifest.Length == 0)
        {
            errors.Add("Choose its manifest (yule-<version>-<board>.manifest.json).");
        }
        else if (Manifest.Length > MaxManifestBytes)
        {
            errors.Add("That manifest is far too large; choose the .manifest.json written by sign_firmware.py.");
        }

        if (errors.Count > 0)
        {
            UploadErrors = errors;
            await LoadAsync(cancellationToken);
            return Page();
        }

        byte[] image;
        await using (var stream = Image!.OpenReadStream())
        using (var buffer = new MemoryStream())
        {
            await stream.CopyToAsync(buffer, cancellationToken);
            image = buffer.ToArray();
        }

        string manifest;
        using (var reader = new StreamReader(Manifest!.OpenReadStream()))
        {
            manifest = await reader.ReadToEndAsync(cancellationToken);
        }

        var result = await releases.UploadAsync(image, manifest, cancellationToken);
        if (!result.Succeeded)
        {
            UploadErrors = result.Errors;
            await LoadAsync(cancellationToken);
            return Page();
        }

        var release = result.Release!;
        TempData["Notice"] = $"Uploaded {release.Version} for {release.Board} and published it to beta. Devices on the beta channel install it when idle.";
        if (result.PublishProblem is { } problem)
        {
            TempData["Warning"] = problem;
        }

        return RedirectToPage();
    }

    public async Task<IActionResult> OnPostPromoteAsync(int id, CancellationToken cancellationToken)
    {
        var (change, release) = await releases.PromoteAsync(id, cancellationToken);
        if (change.NotFound || release is null)
        {
            return NotFound();
        }

        TempData["Notice"] = $"Promoted {release.Version} for {release.Board} to stable. Devices on the stable channel install it when idle.";
        if (change.Broker.Problem is { } problem)
        {
            TempData["Warning"] = problem;
        }

        return RedirectToPage();
    }

    private async Task LoadAsync(CancellationToken cancellationToken)
    {
        Releases = await releases.ListAsync(cancellationToken);
        Current = ReleaseAdminService.Current(Releases);
    }
}
