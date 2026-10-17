using Microsoft.AspNetCore.DataProtection;
using Microsoft.AspNetCore.DataProtection.KeyManagement;
using Microsoft.AspNetCore.DataProtection.Repositories;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Options;
using Yule.Server.Configuration;
using Yule.Server.Data;
using Yule.Server.Security;

namespace Yule.Server.Hosting;

public static class ServiceCollectionExtensions
{
    /// <summary>Registers configuration, storage and security services. Everything is resolved lazily from the bound options.</summary>
    public static IServiceCollection AddYuleCore(this IServiceCollection services, IConfiguration configuration)
    {
        services.AddOptions<YuleOptions>().Bind(configuration.GetSection(YuleOptions.SectionName));

        // The encryption key ring for stored broker passwords lives on the data volume, so a redeployed container
        // can still read them and the reconciler can rebuild device logins.
        services.AddDataProtection().SetApplicationName("Yule");
        services.AddSingleton<IConfigureOptions<KeyManagementOptions>>(sp =>
            new ConfigureOptions<KeyManagementOptions>(o =>
            {
                var directory = Path.Combine(sp.GetRequiredService<IOptions<YuleOptions>>().Value.DataDirectory, "keys");
                Directory.CreateDirectory(directory);
                o.XmlRepository = new FileSystemXmlRepository(new DirectoryInfo(directory), sp.GetRequiredService<ILoggerFactory>());
            }));

        services.AddDbContext<YuleDbContext>((sp, builder) =>
        {
            var directory = sp.GetRequiredService<IOptions<YuleOptions>>().Value.DataDirectory;
            Directory.CreateDirectory(directory);
            builder.UseSqlite($"Data Source={Path.Combine(directory, "yule.db")}");
        });

        services.AddSingleton<IBrokerPasswordProtector, BrokerPasswordProtector>();
        return services;
    }

    /// <summary>Applies pending EF Core migrations, creating the database on first run.</summary>
    public static async Task MigrateDatabaseAsync(this IServiceProvider services, CancellationToken cancellationToken = default)
    {
        using var scope = services.CreateScope();
        var db = scope.ServiceProvider.GetRequiredService<YuleDbContext>();
        await db.Database.MigrateAsync(cancellationToken);
    }
}
