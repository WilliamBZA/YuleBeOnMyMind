using Microsoft.EntityFrameworkCore;

namespace Yule.Server.Data;

public class YuleDbContext(DbContextOptions<YuleDbContext> options) : DbContext(options)
{
    public DbSet<Device> Devices => Set<Device>();

    public DbSet<Group> Groups => Set<Group>();

    public DbSet<Membership> Memberships => Set<Membership>();

    public DbSet<FirmwareRelease> FirmwareReleases => Set<FirmwareRelease>();

    public DbSet<DeviceStatus> DeviceStatuses => Set<DeviceStatus>();

    protected override void OnModelCreating(ModelBuilder model)
    {
        model.Entity<Device>(e =>
        {
            e.HasKey(d => d.Id);
            e.Property(d => d.Id).HasMaxLength(14);
            e.Property(d => d.Label).HasMaxLength(60).IsRequired();
            e.Property(d => d.Channel).HasMaxLength(6).IsRequired();
            e.Property(d => d.EncryptedPassword).IsRequired();
            e.HasOne(d => d.Status).WithOne(s => s.Device).HasForeignKey<DeviceStatus>(s => s.DeviceId)
                .OnDelete(DeleteBehavior.Cascade);
        });

        model.Entity<Group>(e =>
        {
            e.HasKey(g => g.Id);
            e.Property(g => g.Id).HasMaxLength(10);
            e.Property(g => g.Name).HasMaxLength(160).IsRequired();
        });

        model.Entity<Membership>(e =>
        {
            e.HasKey(m => new { m.DeviceId, m.GroupId });
            e.HasOne(m => m.Device).WithMany(d => d.Memberships).HasForeignKey(m => m.DeviceId).OnDelete(DeleteBehavior.Cascade);
            e.HasOne(m => m.Group).WithMany(g => g.Memberships).HasForeignKey(m => m.GroupId).OnDelete(DeleteBehavior.Cascade);
        });

        model.Entity<FirmwareRelease>(e =>
        {
            e.HasKey(r => r.Id);
            e.Property(r => r.Version).HasMaxLength(20).IsRequired();
            e.Property(r => r.Board).HasMaxLength(32).IsRequired();
            e.Property(r => r.Sha256).HasMaxLength(64).IsRequired();
            e.Property(r => r.Signature).IsRequired();
            e.Property(r => r.FileName).HasMaxLength(200).IsRequired();
            e.HasIndex(r => new { r.Board, r.Version }).IsUnique();
        });

        model.Entity<DeviceStatus>(e =>
        {
            e.HasKey(s => s.DeviceId);
            e.Property(s => s.State).HasMaxLength(10).IsRequired();
        });
    }
}
