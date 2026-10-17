using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Yule.Server.Admin;

namespace Yule.Server.Pages.Groups;

public class DetailsModel(GroupAdminService groups) : PageModel
{
    public GroupView Group { get; private set; } = null!;

    public IReadOnlyList<MemberRef> Candidates { get; private set; } = [];

    [BindProperty]
    public string? Name { get; set; }

    [BindProperty]
    public string? DeviceId { get; set; }

    public async Task<IActionResult> OnGetAsync(string id, CancellationToken cancellationToken)
    {
        if (!await LoadAsync(id, cancellationToken))
        {
            return NotFound();
        }

        Name = Group.Name;
        return Page();
    }

    public async Task<IActionResult> OnPostRenameAsync(string id, CancellationToken cancellationToken)
    {
        var result = await groups.RenameAsync(id, Name, cancellationToken);
        if (result.NotFound)
        {
            return NotFound();
        }

        return await ConcludeAsync(id, result, $"Renamed to {Name?.Trim()}.", nameof(Name), cancellationToken);
    }

    public async Task<IActionResult> OnPostAddMemberAsync(string id, CancellationToken cancellationToken)
    {
        var result = await groups.AddMemberAsync(id, DeviceId, cancellationToken);
        if (result.NotFound)
        {
            return NotFound();
        }

        return await ConcludeAsync(id, result, "Added to the group. It picks this up within a few seconds.", nameof(DeviceId), cancellationToken);
    }

    public async Task<IActionResult> OnPostRemoveMemberAsync(string id, CancellationToken cancellationToken)
    {
        var result = await groups.RemoveMemberAsync(id, DeviceId, cancellationToken);
        if (result.NotFound)
        {
            return NotFound();
        }

        return await ConcludeAsync(id, result, "Removed from the group. It stops receiving this group's presses within a few seconds.", nameof(DeviceId), cancellationToken);
    }

    /// <summary>On success, go back to the group's page with a message (the redirect keeps a reload from repeating the change); otherwise show the problem next to the field.</summary>
    private async Task<IActionResult> ConcludeAsync(string id, ChangeResult result, string notice, string errorField, CancellationToken cancellationToken)
    {
        if (result.Succeeded)
        {
            TempData["Notice"] = notice;
            if (result.Broker.Problem is { } problem)
            {
                TempData["Warning"] = problem;
            }

            return RedirectToPage(new { id });
        }

        foreach (var (field, message) in result.Errors)
        {
            ModelState.AddModelError(field.Length == 0 ? errorField : field, message);
        }

        await LoadAsync(id, cancellationToken);
        return Page();
    }

    private async Task<bool> LoadAsync(string id, CancellationToken cancellationToken)
    {
        if (await groups.FindAsync(id, cancellationToken) is not { } group)
        {
            return false;
        }

        Group = group;
        Candidates = await groups.CandidatesAsync(id, cancellationToken);
        return true;
    }
}
