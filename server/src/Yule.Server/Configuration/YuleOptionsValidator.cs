using Yule.Server.Releases;
using Yule.Server.Security;

namespace Yule.Server.Configuration;

/// <summary>
/// Checks that every required setting is present and usable, so a misconfigured server fails at start-up with
/// a message that names the setting and the environment variable to set, instead of failing later.
/// </summary>
public static class YuleOptionsValidator
{
    public static IReadOnlyList<string> Validate(YuleOptions options)
    {
        var errors = new List<string>();

        if (string.IsNullOrWhiteSpace(options.Admin.PasswordHash))
        {
            errors.Add("Yule:Admin:PasswordHash is required (the administrator's password hash). " +
                       "Generate one with `hash-password` and set the environment variable Yule__Admin__PasswordHash.");
        }
        else if (!AdminPasswordHasher.IsValidHash(options.Admin.PasswordHash))
        {
            errors.Add("Yule:Admin:PasswordHash is not a valid hash. Generate one with `hash-password`.");
        }

        if (string.IsNullOrWhiteSpace(options.Broker.Host))
        {
            errors.Add("Yule:Broker:Host is required (the broker's internal address). Set the environment variable Yule__Broker__Host.");
        }

        if (options.Broker.Port is < 1 or > 65535)
        {
            errors.Add("Yule:Broker:Port must be between 1 and 65535. Set the environment variable Yule__Broker__Port.");
        }

        if (string.IsNullOrWhiteSpace(options.Broker.Username))
        {
            errors.Add("Yule:Broker:Username is required (the broker service account, normally yule-server). " +
                       "Set the environment variable Yule__Broker__Username.");
        }

        if (string.IsNullOrEmpty(options.Broker.Password))
        {
            errors.Add("Yule:Broker:Password is required (the broker service account's password). " +
                       "Set the environment variable Yule__Broker__Password.");
        }

        if (string.IsNullOrWhiteSpace(options.PublicBaseUrl))
        {
            errors.Add("Yule:PublicBaseUrl is required (for example https://yule.williamb.net). Set the environment variable Yule__PublicBaseUrl.");
        }
        else if (!TryParsePublicBaseUrl(options.PublicBaseUrl, out var reason))
        {
            errors.Add($"Yule:PublicBaseUrl {reason}");
        }

        var keys = options.ReleasePublicKeys.Where(k => !string.IsNullOrWhiteSpace(k)).ToList();
        if (keys.Count == 0)
        {
            errors.Add("Yule:ReleasePublicKeys needs at least one release public key (PEM, ECDSA P-256). " +
                       "Set the environment variable Yule__ReleasePublicKeys__0.");
        }
        else if (keys.Count > 2)
        {
            errors.Add("Yule:ReleasePublicKeys can hold at most two keys (the current one and the next one, for key rotation).");
        }
        else
        {
            for (var i = 0; i < keys.Count; i++)
            {
                if (!ReleaseKeyParser.TryParse(keys[i], out _, out var error))
                {
                    errors.Add($"Yule:ReleasePublicKeys:{i} is not a usable key: {error}");
                }
            }
        }

        if (string.IsNullOrWhiteSpace(options.DataDirectory))
        {
            errors.Add("Yule:DataDirectory is required (a writable directory, a volume in Docker). Set the environment variable Yule__DataDirectory.");
        }

        if (string.IsNullOrWhiteSpace(options.TopicRoot) || options.TopicRoot.Contains('+') || options.TopicRoot.Contains('#')
            || options.TopicRoot.StartsWith('/') || options.TopicRoot.EndsWith('/'))
        {
            errors.Add("Yule:TopicRoot must be a plain topic prefix such as yule/v1 (no wildcards, no leading or trailing slash).");
        }

        if (options.ReconcileIntervalMinutes is < 1 or > 1440)
        {
            errors.Add("Yule:ReconcileIntervalMinutes must be between 1 and 1440.");
        }

        return errors;
    }

    /// <summary>The public base URL must be absolute https (plain http is accepted only for localhost development).</summary>
    public static bool TryParsePublicBaseUrl(string value, out string reason)
    {
        reason = "";
        if (!Uri.TryCreate(value, UriKind.Absolute, out var uri))
        {
            reason = "must be an absolute address such as https://yule.williamb.net.";
            return false;
        }

        var local = uri.IsLoopback;
        if (uri.Scheme != Uri.UriSchemeHttps && !(local && uri.Scheme == Uri.UriSchemeHttp))
        {
            reason = "must use https (firmware is downloaded over TLS).";
            return false;
        }

        if (!string.IsNullOrEmpty(uri.Query) || !string.IsNullOrEmpty(uri.Fragment))
        {
            reason = "must not contain a query or fragment.";
            return false;
        }

        return true;
    }
}
