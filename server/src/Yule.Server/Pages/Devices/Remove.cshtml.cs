using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Yule.Server.Admin;

namespace Yule.Server.Pages.Devices;

public class RemoveModel(DeviceAdminService devices) : PageModel
{
    public DeviceView Device { get; private set; } = null!;

    public async Task<IActionResult> OnGetAsync(string id, CancellationToken cancellationToken)
    {
        if (await devices.FindAsync(id, cancellationToken) is not { } device)
        {
            return NotFound();
        }

        Device = device;
        return Page();
    }

    public async Task<IActionResult> OnPostAsync(string id, CancellationToken cancellationToken)
    {
        var device = await devices.FindAsync(id, cancellationToken);
        var result = await devices.RemoveAsync(id, cancellationToken);
        if (result.NotFound)
        {
            return NotFound();
        }

        TempData["Notice"] = $"Removed {device?.Label ?? id}. Its connection to the broker is closed and it can no longer connect.";
        if (result.Broker.Problem is { } problem)
        {
            TempData["Warning"] = problem;
        }

        return RedirectToPage("/Devices/Index");
    }
}
