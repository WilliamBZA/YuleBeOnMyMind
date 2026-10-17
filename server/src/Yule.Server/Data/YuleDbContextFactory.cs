using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Design;

namespace Yule.Server.Data;

/// <summary>Used only by `dotnet ef` to create migrations, so the tools need none of the app's runtime configuration.</summary>
public sealed class YuleDbContextFactory : IDesignTimeDbContextFactory<YuleDbContext>
{
    public YuleDbContext CreateDbContext(string[] args) =>
        new(new DbContextOptionsBuilder<YuleDbContext>().UseSqlite("Data Source=design-time.db").Options);
}
