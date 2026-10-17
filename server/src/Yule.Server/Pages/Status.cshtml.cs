using Microsoft.AspNetCore.Mvc.RazorPages;
using Yule.Server.Admin;

namespace Yule.Server.Pages;

public class StatusModel(DeviceAdminService devices, TimeProvider time) : PageModel
{
    public IReadOnlyList<DeviceView> Devices { get; private set; } = [];

    public DateTime NowUtc => time.GetUtcNow().UtcDateTime;

    public int Online => Devices.Count(d => d.Status?.State == "online");

    public async Task OnGetAsync(CancellationToken cancellationToken) =>
        Devices = await devices.ListAsync(cancellationToken);
}
