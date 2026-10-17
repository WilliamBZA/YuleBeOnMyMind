using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Yule.Server.Admin;

namespace Yule.Server.Pages.Devices;

public class RegisterInput
{
    public string? Id { get; set; }

    public string? Label { get; set; }
}

public class IndexModel(DeviceAdminService devices, TimeProvider time) : PageModel
{
    public IReadOnlyList<DeviceView> Devices { get; private set; } = [];

    [BindProperty]
    public RegisterInput Input { get; set; } = new();

    /// <summary>Set only in the response to a successful registration: the one and only time the password is shown.</summary>
    public RegisterResult? Registered { get; private set; }

    public string? RegisteredLabel { get; private set; }

    public DateTime NowUtc => time.GetUtcNow().UtcDateTime;

    public async Task OnGetAsync(CancellationToken cancellationToken) =>
        Devices = await devices.ListAsync(cancellationToken);

    /// <summary>
    /// Registers a device. The password is rendered directly in this response, never redirected to or stored, so it
    /// cannot be seen again by reloading, going back, or following a link; reloading the response re-submits the
    /// registration, which is refused because the device now exists.
    /// </summary>
    public async Task<IActionResult> OnPostAsync(CancellationToken cancellationToken)
    {
        var result = await devices.RegisterAsync(Input.Id, Input.Label, cancellationToken);
        if (result.Change.Succeeded)
        {
            Registered = result;
            RegisteredLabel = Input.Label!.Trim();
            ModelState.Clear();
            Input = new RegisterInput();
        }
        else
        {
            foreach (var (field, message) in result.Change.Errors)
            {
                ModelState.AddModelError($"{nameof(Input)}.{field}", message);
            }
        }

        Devices = await devices.ListAsync(cancellationToken);
        return Page();
    }
}
