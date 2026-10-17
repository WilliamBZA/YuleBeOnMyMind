using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace Yule.Server.Pages;

/// <summary>The home address has nothing of its own: it goes to the Devices page (or, if not logged in, to the login page first).</summary>
public class IndexModel : PageModel
{
    public IActionResult OnGet() => RedirectToPage("/Devices/Index");
}
