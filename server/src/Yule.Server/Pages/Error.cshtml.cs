using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace Yule.Server.Pages;

/// <summary>
/// Where unhandled errors land. It must be reachable without logging in: an error on the login page itself would
/// otherwise be redirected to the login page again. It shows nothing about the error.
/// </summary>
[AllowAnonymous]
[IgnoreAntiforgeryToken]
public class ErrorModel : PageModel
{
    public void OnGet() => Response.StatusCode = StatusCodes.Status500InternalServerError;

    public void OnPost() => Response.StatusCode = StatusCodes.Status500InternalServerError;
}
