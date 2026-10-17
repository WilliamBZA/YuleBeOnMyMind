using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Yule.Server.Admin;

namespace Yule.Server.Pages.Groups;

public class RemoveModel(GroupAdminService groups) : PageModel
{
    public GroupView Group { get; private set; } = null!;

    public async Task<IActionResult> OnGetAsync(string id, CancellationToken cancellationToken)
    {
        if (await groups.FindAsync(id, cancellationToken) is not { } group)
        {
            return NotFound();
        }

        Group = group;
        return Page();
    }

    public async Task<IActionResult> OnPostAsync(string id, CancellationToken cancellationToken)
    {
        var group = await groups.FindAsync(id, cancellationToken);
        var result = await groups.DeleteAsync(id, cancellationToken);
        if (result.NotFound)
        {
            return NotFound();
        }

        TempData["Notice"] = $"Deleted {group?.Name ?? id}. Its members no longer send or receive its presses.";
        if (result.Broker.Problem is { } problem)
        {
            TempData["Warning"] = problem;
        }

        return RedirectToPage("/Groups/Index");
    }
}
