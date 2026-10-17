using Microsoft.AspNetCore.DataProtection;
using Microsoft.Data.Sqlite;
using Microsoft.EntityFrameworkCore;
using Yule.Server.Data;
using Yule.Server.Hosting;
using Yule.Server.Security;

namespace Yule.Server.Tests;

public class IdentifierTests
{
    [Theory]
    [InlineData("d-3c71bf4a2b10", true)]
    [InlineData("d-000000000001", true)]
    [InlineData("D-3C71BF4A2B10", false)]
    [InlineData("d-3c71bf4a2b1", false)]
    [InlineData("d-3c71bf4a2b100", false)]
    [InlineData("d-3c71bf4a2b1g", false)]
    [InlineData("x-3c71bf4a2b10", false)]
    [InlineData(" d-3c71bf4a2b10", false)]
    [InlineData("", false)]
    [InlineData(null, false)]
    public void Device_ids_are_d_dash_and_twelve_lowercase_hex_digits(string? id, bool valid)
    {
        Assert.Equal(valid, Identifiers.IsValidDeviceId(id));
    }

    [Fact]
    public void Generated_group_ids_are_valid_and_do_not_repeat()
    {
        var ids = Enumerable.Range(0, 2000).Select(_ => Identifiers.NewGroupId()).ToList();

        Assert.All(ids, id =>
        {
            Assert.StartsWith("g-", id);
            Assert.Equal(10, id.Length);
            Assert.True(Identifiers.IsValidGroupId(id), id);
        });
        Assert.Equal(ids.Count, ids.Distinct().Count());
    }

    [Theory]
    [InlineData("g-7f3k2abc", true)]
    [InlineData("g-00000000", true)]
    [InlineData("g-7F3K2ABC", false)]
    [InlineData("g-7f3k2ab", false)]
    [InlineData("g-7f3k2abcd", false)]
    [InlineData("g-7f3k2ab!", false)]
    [InlineData("", false)]
    public void Group_id_validation_matches_what_the_device_accepts(string id, bool valid)
    {
        Assert.Equal(valid, Identifiers.IsValidGroupId(id));
    }

    [Fact]
    public void Generated_broker_passwords_are_24_typeable_characters_and_do_not_repeat()
    {
        var passwords = Enumerable.Range(0, 2000).Select(_ => Identifiers.NewBrokerPassword()).ToList();

        Assert.All(passwords, p =>
        {
            Assert.Equal(24, p.Length);
            Assert.All(p, c => Assert.True(char.IsAsciiLetterOrDigit(c), p));
            Assert.DoesNotContain(p, c => "0O1lI".Contains(c)); // no look-alikes
        });
        Assert.Equal(passwords.Count, passwords.Distinct().Count());
    }

    [Theory]
    [InlineData("stable", true)]
    [InlineData("beta", true)]
    [InlineData("Beta", false)]
    [InlineData("nightly", false)]
    [InlineData("", false)]
    [InlineData(null, false)]
    public void Only_stable_and_beta_are_channels(string? channel, bool valid)
    {
        Assert.Equal(valid, Identifiers.IsValidChannel(channel));
    }

    [Theory]
    [InlineData("Brander Grandparents", true)]
    [InlineData("A", true)]
    [InlineData("", false)]
    [InlineData("   ", false)]
    [InlineData(null, false)]
    public void Group_names_need_something_in_them(string? name, bool valid)
    {
        Assert.Equal(valid, Identifiers.IsValidGroupName(name));
    }

    [Fact]
    public void Group_names_are_limited_to_40_characters_not_bytes()
    {
        Assert.True(Identifiers.IsValidGroupName(new string('n', 40)));
        Assert.False(Identifiers.IsValidGroupName(new string('n', 41)));
        Assert.True(Identifiers.IsValidGroupName(string.Concat(Enumerable.Repeat("é", 40))));
        Assert.True(Identifiers.IsValidGroupName(string.Concat(Enumerable.Repeat("🎄", 40))));
        Assert.False(Identifiers.IsValidGroupName(string.Concat(Enumerable.Repeat("🎄", 41))));
    }

    [Fact]
    public void Device_labels_are_1_to_60_characters()
    {
        Assert.True(Identifiers.IsValidDeviceLabel("Gran (Brander)"));
        Assert.False(Identifiers.IsValidDeviceLabel(""));
        Assert.False(Identifiers.IsValidDeviceLabel("   "));
        Assert.False(Identifiers.IsValidDeviceLabel(new string('x', 61)));
    }
}

public class SecurityTests
{
    [Fact]
    public void The_admin_password_hashes_and_verifies()
    {
        var hash = AdminPasswordHasher.Hash("correct horse battery staple", iterations: 2_000);

        Assert.True(AdminPasswordHasher.Verify("correct horse battery staple", hash));
        Assert.False(AdminPasswordHasher.Verify("correct horse battery stapl", hash));
        Assert.False(AdminPasswordHasher.Verify("", hash));
        Assert.True(AdminPasswordHasher.IsValidHash(hash));
        Assert.StartsWith("PBKDF2-SHA256.2000.", hash);
        Assert.DoesNotContain("$", hash);
    }

    [Fact]
    public void Hashing_the_same_password_twice_gives_different_hashes_because_of_the_salt()
    {
        Assert.NotEqual(AdminPasswordHasher.Hash("same-password-123", 2_000), AdminPasswordHasher.Hash("same-password-123", 2_000));
    }

    [Theory]
    [InlineData("")]
    [InlineData("plaintext")]
    [InlineData("PBKDF2-SHA256.abc.AAAA.BBBB")]
    [InlineData("PBKDF2-SHA512.600000.AAAAAAAAAAAAAAAAAAAAAA==.AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")]
    public void Garbage_never_verifies_and_never_throws(string stored)
    {
        Assert.False(AdminPasswordHasher.Verify("anything", stored));
        Assert.False(AdminPasswordHasher.IsValidHash(stored));
    }

    [Fact]
    public void A_hash_made_with_a_higher_work_factor_still_verifies()
    {
        var hash = AdminPasswordHasher.Hash("another-long-password", iterations: 5_000);

        Assert.True(AdminPasswordHasher.Verify("another-long-password", hash));
    }

    [Fact]
    public void The_hash_password_command_prints_a_verifiable_hash()
    {
        var output = new StringWriter();
        var error = new StringWriter();

        var code = HashPasswordCommand.Run(new StringReader("a-sufficiently-long-password\n"), output, error, interactive: false);

        Assert.Equal(0, code);
        Assert.True(AdminPasswordHasher.Verify("a-sufficiently-long-password", output.ToString().Trim()));
        Assert.Equal("", error.ToString());
    }

    [Fact]
    public void The_hash_password_command_rejects_short_passwords()
    {
        var output = new StringWriter();
        var error = new StringWriter();

        var code = HashPasswordCommand.Run(new StringReader("short\n"), output, error, interactive: false);

        Assert.Equal(1, code);
        Assert.Equal("", output.ToString());
        Assert.Contains("12 characters", error.ToString());
    }

    [Fact]
    public void Broker_passwords_survive_an_encryption_round_trip_and_a_restart()
    {
        var keys = new DirectoryInfo(TestSupport.NewTempDirectory());
        var first = new BrokerPasswordProtector(DataProtectionProvider.Create(keys));
        var password = Identifiers.NewBrokerPassword();

        var stored = first.Protect(password);

        Assert.NotEqual(password, stored);
        Assert.DoesNotContain(password, stored);
        Assert.Equal(password, first.Unprotect(stored));

        // A new process with the same key ring (the data volume) reads the same password back.
        var second = new BrokerPasswordProtector(DataProtectionProvider.Create(keys));
        Assert.Equal(password, second.Unprotect(stored));

        // With a different key ring it cannot.
        var other = new BrokerPasswordProtector(DataProtectionProvider.Create(new DirectoryInfo(TestSupport.NewTempDirectory())));
        Assert.ThrowsAny<System.Security.Cryptography.CryptographicException>(() => other.Unprotect(stored));
    }
}

public class DatabaseTests : IAsyncLifetime
{
    private readonly SqliteConnection _connection = new("Data Source=:memory:");

    public async Task InitializeAsync() => await _connection.OpenAsync();

    public async Task DisposeAsync() => await _connection.DisposeAsync();

    private YuleDbContext NewContext() => new(new DbContextOptionsBuilder<YuleDbContext>().UseSqlite(_connection).Options);

    [Fact]
    public async Task The_initial_migration_applies_to_an_empty_database()
    {
        await using var db = NewContext();

        await db.Database.MigrateAsync();

        Assert.Empty(await db.Database.GetPendingMigrationsAsync());
        Assert.Single(await db.Database.GetAppliedMigrationsAsync());
        Assert.Equal(0, await db.Devices.CountAsync());
        Assert.Equal(0, await db.Groups.CountAsync());
        Assert.Equal(0, await db.FirmwareReleases.CountAsync());
    }

    [Fact]
    public async Task Migrating_twice_is_harmless()
    {
        await using var db = NewContext();
        await db.Database.MigrateAsync();
        await db.Database.MigrateAsync();

        Assert.Empty(await db.Database.GetPendingMigrationsAsync());
    }

    [Fact]
    public async Task The_model_has_no_drift_from_the_migration()
    {
        await using var db = NewContext();
        await db.Database.MigrateAsync();

        // If this fails, the entities changed without a new migration (dotnet ef migrations add ...).
        Assert.False(db.Database.HasPendingModelChanges());
    }

    [Fact]
    public async Task Devices_groups_and_memberships_round_trip()
    {
        await using (var db = NewContext())
        {
            await db.Database.MigrateAsync();
            db.Devices.Add(new Device { Id = "d-000000000001", Label = "Tester", Channel = "beta", EncryptedPassword = "x" });
            db.Groups.Add(new Group { Id = "g-aaaaaaaa", Name = "One" });
            db.Groups.Add(new Group { Id = "g-bbbbbbbb", Name = "Two" });
            db.Memberships.Add(new Membership { DeviceId = "d-000000000001", GroupId = "g-aaaaaaaa" });
            db.Memberships.Add(new Membership { DeviceId = "d-000000000001", GroupId = "g-bbbbbbbb" });
            db.DeviceStatuses.Add(new DeviceStatus { DeviceId = "d-000000000001", State = "online", FirmwareVersion = "0.3.0" });
            await db.SaveChangesAsync();
        }

        await using (var db = NewContext())
        {
            var device = await db.Devices.Include(d => d.Memberships).ThenInclude(m => m.Group).Include(d => d.Status).SingleAsync();
            Assert.Equal("beta", device.Channel);
            Assert.Equal(["One", "Two"], device.Memberships.Select(m => m.Group.Name).Order());
            Assert.Equal("0.3.0", device.Status!.FirmwareVersion);
        }
    }

    [Fact]
    public async Task Removing_a_device_removes_its_memberships_and_status_but_not_its_groups()
    {
        await using var db = NewContext();
        await db.Database.MigrateAsync();
        db.Devices.Add(new Device { Id = "d-000000000001", Label = "A", EncryptedPassword = "x" });
        db.Groups.Add(new Group { Id = "g-aaaaaaaa", Name = "One" });
        db.Memberships.Add(new Membership { DeviceId = "d-000000000001", GroupId = "g-aaaaaaaa" });
        db.DeviceStatuses.Add(new DeviceStatus { DeviceId = "d-000000000001" });
        await db.SaveChangesAsync();

        db.Devices.Remove(await db.Devices.SingleAsync());
        await db.SaveChangesAsync();

        Assert.Equal(0, await db.Memberships.CountAsync());
        Assert.Equal(0, await db.DeviceStatuses.CountAsync());
        Assert.Equal(1, await db.Groups.CountAsync());
    }

    [Fact]
    public async Task Removing_a_group_removes_its_memberships_but_not_its_devices()
    {
        await using var db = NewContext();
        await db.Database.MigrateAsync();
        db.Devices.Add(new Device { Id = "d-000000000001", Label = "A", EncryptedPassword = "x" });
        db.Groups.Add(new Group { Id = "g-aaaaaaaa", Name = "One" });
        db.Memberships.Add(new Membership { DeviceId = "d-000000000001", GroupId = "g-aaaaaaaa" });
        await db.SaveChangesAsync();

        db.Groups.Remove(await db.Groups.SingleAsync());
        await db.SaveChangesAsync();

        Assert.Equal(0, await db.Memberships.CountAsync());
        Assert.Equal(1, await db.Devices.CountAsync());
    }

    [Fact]
    public async Task A_device_cannot_join_the_same_group_twice_and_group_names_need_not_be_unique()
    {
        await using var db = NewContext();
        await db.Database.MigrateAsync();
        db.Devices.Add(new Device { Id = "d-000000000001", Label = "A", EncryptedPassword = "x" });
        db.Groups.Add(new Group { Id = "g-aaaaaaaa", Name = "Grandparents" });
        db.Groups.Add(new Group { Id = "g-bbbbbbbb", Name = "Grandparents" });
        db.Memberships.Add(new Membership { DeviceId = "d-000000000001", GroupId = "g-aaaaaaaa" });
        await db.SaveChangesAsync();

        // A second context, so it is the database's key constraint that is exercised, not the change tracker.
        await using var other = NewContext();
        other.Memberships.Add(new Membership { DeviceId = "d-000000000001", GroupId = "g-aaaaaaaa" });
        await Assert.ThrowsAsync<DbUpdateException>(() => other.SaveChangesAsync());
    }

    [Fact]
    public async Task A_release_version_is_unique_per_board()
    {
        await using var db = NewContext();
        await db.Database.MigrateAsync();
        db.FirmwareReleases.Add(NewRelease("0.3.0", "esp32dev"));
        db.FirmwareReleases.Add(NewRelease("0.3.0", "esp32-c3"));
        await db.SaveChangesAsync();

        db.FirmwareReleases.Add(NewRelease("0.3.0", "esp32dev"));
        await Assert.ThrowsAsync<DbUpdateException>(() => db.SaveChangesAsync());
    }

    private static FirmwareRelease NewRelease(string version, string board) => new()
    {
        Version = version, Board = board, Sha256 = new string('a', 64), Size = 100, Signature = "sig", FileName = $"{version}-{board}.bin",
    };
}
