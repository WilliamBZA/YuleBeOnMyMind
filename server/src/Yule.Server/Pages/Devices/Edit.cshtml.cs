using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Yule.Server.Admin;

namespace Yule.Server.Pages.Devices;

public class EditModel(DeviceAdminService devices, TimeProvider time) : PageModel
{
    public DeviceView Device { get; private set; } = null!;

    [BindProperty]
    public string? Label { get; set; }

    [BindProperty]
    public string? Channel { get; set; }

    public DateTime NowUtc => time.GetUtcNow().UtcDateTime;

    public async Task<IActionResult> OnGetAsync(string id, CancellationToken cancellationToken)
    {
        if (await devices.FindAsync(id, cancellationToken) is not { } device)
        {
            return NotFound();
        }

        Device = device;
        Label = device.Label;
        Channel = device.Channel;
        return Page();
    }

    public async Task<IActionResult> OnPostAsync(string id, CancellationToken cancellationToken)
    {
        var result = await devices.UpdateAsync(id, Label, Channel, cancellationToken);
        if (result.NotFound)
        {
            return NotFound();
        }

        if (result.Succeeded)
        {
            TempData["Notice"] = $"Saved {Label!.Trim()}.";
            if (result.Broker.Problem is { } problem)
            {
                TempData["Warning"] = problem;
            }

            return RedirectToPage("/Devices/Index");
        }

        foreach (var (field, message) in result.Errors)
        {
            ModelState.AddModelError(field, message);
        }

        Device = (await devices.FindAsync(id, cancellationToken))!;
        return Page();
    }
}
