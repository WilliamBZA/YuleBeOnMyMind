using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Yule.Server.Admin;

namespace Yule.Server.Pages.Groups;

public class IndexModel(GroupAdminService groups) : PageModel
{
    public IReadOnlyList<GroupView> Groups { get; private set; } = [];

    [BindProperty]
    public string? Name { get; set; }

    public async Task OnGetAsync(CancellationToken cancellationToken) =>
        Groups = await groups.ListAsync(cancellationToken);

    public async Task<IActionResult> OnPostAsync(CancellationToken cancellationToken)
    {
        var result = await groups.CreateAsync(Name, cancellationToken);
        if (result.Change.Succeeded)
        {
            TempData["Notice"] = $"Created {Name!.Trim()}. Add its members below.";
            return RedirectToPage("/Groups/Details", new { id = result.GroupId });
        }

        foreach (var (field, message) in result.Change.Errors)
        {
            ModelState.AddModelError(field, message);
        }

        Groups = await groups.ListAsync(cancellationToken);
        return Page();
    }
}
