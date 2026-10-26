using System.Net;
using System.Text.RegularExpressions;

namespace Yule.Server.Tests;

/// <summary>
/// A browser, near enough, for the admin pages: it keeps cookies, does not follow redirects on its own, and knows how
/// to read the anti-forgery token out of a form and send it back. Shared with the integration tests (linked in).
/// </summary>
public sealed class AdminClient(HttpClient http)
{
    private static readonly Regex Token = new("name=\"__RequestVerificationToken\"[^>]*?value=\"([^\"]+)\"", RegexOptions.Compiled);

    public HttpClient Http { get; } = http;

    public Task<HttpResponseMessage> GetAsync(string path) => Http.GetAsync(path);

    public Task<string> GetStringAsync(string path) => Http.GetStringAsync(path);

    /// <summary>Reads the anti-forgery token from the first form on a page (the page must be reachable, so log in first).</summary>
    public async Task<string> TokenFromAsync(string formPage)
    {
        var html = await Http.GetStringAsync(formPage);
        var match = Token.Match(html);
        if (!match.Success)
        {
            throw new InvalidOperationException($"{formPage} has no form with an anti-forgery token.");
        }

        return WebUtility.HtmlDecode(match.Groups[1].Value);
    }

    /// <summary>Posts a form the way a browser would: it loads <paramref name="formPage"/> first, for the token and its cookie.</summary>
    public async Task<HttpResponseMessage> PostFormAsync(string path, string formPage, params (string Name, string Value)[] fields)
    {
        var token = await TokenFromAsync(formPage);
        return await PostWithTokenAsync(path, token, fields);
    }

    public Task<HttpResponseMessage> PostWithTokenAsync(string path, string? token, params (string Name, string Value)[] fields)
    {
        var all = token is null ? fields : [.. fields, ("__RequestVerificationToken", token)];
        return Http.PostAsync(path, new FormUrlEncodedContent(all.Select(f => KeyValuePair.Create(f.Name, f.Value))));
    }

    /// <summary>Posts a file-upload form (multipart) the way a browser would, with the token from <paramref name="formPage"/>.</summary>
    public async Task<HttpResponseMessage> PostFilesAsync(string path, string formPage, params (string Field, string FileName, byte[] Content)[] files)
    {
        var token = await TokenFromAsync(formPage);
        using var form = new MultipartFormDataContent();
        form.Add(new StringContent(token), "__RequestVerificationToken");
        foreach (var (field, fileName, content) in files)
        {
            var part = new ByteArrayContent(content);
            part.Headers.ContentType = new System.Net.Http.Headers.MediaTypeHeaderValue("application/octet-stream");
            form.Add(part, field, fileName);
        }

        return await Http.PostAsync(path, form);
    }

    public async Task<HttpResponseMessage> TryLoginAsync(string password, string? returnUrl = null)
    {
        var page = returnUrl is null ? "/login" : "/login?ReturnUrl=" + Uri.EscapeDataString(returnUrl);
        return await PostFormAsync(page, page, ("Password", password));
    }

    /// <summary>Logs in and fails the test if that did not work.</summary>
    public async Task LoginAsync(string password = AdminSiteDefaults.Password)
    {
        var response = await TryLoginAsync(password);
        if (response.StatusCode != HttpStatusCode.Redirect)
        {
            throw new InvalidOperationException($"Login failed: {(int)response.StatusCode} {await response.Content.ReadAsStringAsync()}");
        }
    }

    /// <summary>Where a redirect points, as a path and query on this site.</summary>
    public static string Location(HttpResponseMessage response) =>
        response.Headers.Location is { } uri ? (uri.IsAbsoluteUri ? uri.PathAndQuery : uri.OriginalString) : "";
}

public static class AdminSiteDefaults
{
    public const string Password = "correct horse battery staple";
}
