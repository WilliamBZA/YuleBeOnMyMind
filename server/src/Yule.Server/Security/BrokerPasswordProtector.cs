using Microsoft.AspNetCore.DataProtection;

namespace Yule.Server.Security;

/// <summary>
/// Encrypts device broker passwords at rest with ASP.NET Data Protection (key ring persisted on the data volume).
/// Passwords must be recoverable, not hashed, because the reconciler re-creates broker logins from them.
/// </summary>
public interface IBrokerPasswordProtector
{
    string Protect(string password);

    string Unprotect(string protectedPassword);
}

public sealed class BrokerPasswordProtector(IDataProtectionProvider provider) : IBrokerPasswordProtector
{
    private readonly IDataProtector _protector = provider.CreateProtector("Yule.BrokerPassword.v1");

    public string Protect(string password) => _protector.Protect(password);

    public string Unprotect(string protectedPassword) => _protector.Unprotect(protectedPassword);
}
