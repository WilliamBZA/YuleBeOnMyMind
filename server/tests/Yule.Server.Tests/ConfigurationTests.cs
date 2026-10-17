using Yule.Server.Configuration;

namespace Yule.Server.Tests;

public class ConfigurationTests
{
    [Fact]
    public void A_complete_configuration_is_valid()
    {
        Assert.Empty(YuleOptionsValidator.Validate(TestSupport.ValidOptions()));
    }

    [Theory]
    [InlineData("Admin.PasswordHash", "Yule:Admin:PasswordHash", "Yule__Admin__PasswordHash")]
    [InlineData("Broker.Host", "Yule:Broker:Host", "Yule__Broker__Host")]
    [InlineData("Broker.Username", "Yule:Broker:Username", "Yule__Broker__Username")]
    [InlineData("Broker.Password", "Yule:Broker:Password", "Yule__Broker__Password")]
    [InlineData("PublicBaseUrl", "Yule:PublicBaseUrl", "Yule__PublicBaseUrl")]
    [InlineData("ReleasePublicKeys", "Yule:ReleasePublicKeys", "Yule__ReleasePublicKeys__0")]
    [InlineData("DataDirectory", "Yule:DataDirectory", "Yule__DataDirectory")]
    public void A_missing_required_value_fails_validation_and_names_the_setting(string property, string setting, string environmentVariable)
    {
        var options = TestSupport.ValidOptions();
        switch (property)
        {
            case "Admin.PasswordHash": options.Admin.PasswordHash = ""; break;
            case "Broker.Host": options.Broker.Host = ""; break;
            case "Broker.Username": options.Broker.Username = "  "; break;
            case "Broker.Password": options.Broker.Password = ""; break;
            case "PublicBaseUrl": options.PublicBaseUrl = ""; break;
            case "ReleasePublicKeys": options.ReleasePublicKeys = []; break;
            case "DataDirectory": options.DataDirectory = ""; break;
        }

        var errors = YuleOptionsValidator.Validate(options);

        var message = Assert.Single(errors);
        Assert.Contains(setting, message);
        Assert.Contains(environmentVariable, message);
    }

    [Fact]
    public void An_empty_configuration_reports_every_problem_at_once()
    {
        var errors = YuleOptionsValidator.Validate(new YuleOptions());

        Assert.True(errors.Count >= 7, string.Join(Environment.NewLine, errors));
    }

    [Theory]
    [InlineData("not-a-hash")]
    [InlineData("PBKDF2-SHA256.1.AAAA.BBBB")]
    [InlineData("PBKDF2-SHA256.600000.!!!.!!!")]
    public void A_malformed_admin_hash_is_rejected(string hash)
    {
        var options = TestSupport.ValidOptions();
        options.Admin.PasswordHash = hash;

        Assert.Contains(YuleOptionsValidator.Validate(options), e => e.Contains("PasswordHash"));
    }

    [Theory]
    [InlineData("https://yule.williamb.net", true)]
    [InlineData("https://yule.williamb.net/", true)]
    [InlineData("https://example.org:8443/base", true)]
    [InlineData("http://localhost:8080", true)]
    [InlineData("http://127.0.0.1:5071", true)]
    [InlineData("http://yule.williamb.net", false)]
    [InlineData("yule.williamb.net", false)]
    [InlineData("ftp://yule.williamb.net", false)]
    [InlineData("https://yule.williamb.net/?a=b", false)]
    [InlineData("not a url", false)]
    public void The_public_base_url_must_be_https_except_for_local_development(string url, bool valid)
    {
        Assert.Equal(valid, YuleOptionsValidator.TryParsePublicBaseUrl(url, out _));
    }

    [Fact]
    public void Release_keys_must_be_p256_public_keys_and_at_most_two()
    {
        var options = TestSupport.ValidOptions();
        options.ReleasePublicKeys = ["-----BEGIN PUBLIC KEY-----\nnot a key\n-----END PUBLIC KEY-----"];
        Assert.Contains(YuleOptionsValidator.Validate(options), e => e.Contains("ReleasePublicKeys:0"));

        using var p384 = System.Security.Cryptography.ECDsa.Create(System.Security.Cryptography.ECCurve.NamedCurves.nistP384);
        options.ReleasePublicKeys = [p384.ExportSubjectPublicKeyInfoPem()];
        Assert.Contains(YuleOptionsValidator.Validate(options), e => e.Contains("P-256"));

        options.ReleasePublicKeys = [TestSupport.NewReleaseKey().PublicPem, TestSupport.NewReleaseKey().PublicPem];
        Assert.Empty(YuleOptionsValidator.Validate(options));

        options.ReleasePublicKeys = [.. Enumerable.Range(0, 3).Select(_ => TestSupport.NewReleaseKey().PublicPem)];
        Assert.Contains(YuleOptionsValidator.Validate(options), e => e.Contains("at most two"));
    }

    [Fact]
    public void A_key_whose_newlines_were_passed_as_backslash_n_is_accepted()
    {
        var options = TestSupport.ValidOptions();
        options.ReleasePublicKeys = [TestSupport.NewReleaseKey().PublicPem.Replace("\r\n", "\\n").Replace("\n", "\\n")];

        Assert.Empty(YuleOptionsValidator.Validate(options));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(65536)]
    [InlineData(-1)]
    public void The_broker_port_must_be_a_real_port(int port)
    {
        var options = TestSupport.ValidOptions();
        options.Broker.Port = port;

        Assert.Contains(YuleOptionsValidator.Validate(options), e => e.Contains("Broker:Port"));
    }

    [Theory]
    [InlineData("yule/v1", true)]
    [InlineData("custom", true)]
    [InlineData("yule/+", false)]
    [InlineData("yule/#", false)]
    [InlineData("/yule", false)]
    [InlineData("yule/", false)]
    [InlineData("", false)]
    public void The_topic_root_must_be_a_plain_prefix(string root, bool valid)
    {
        var options = TestSupport.ValidOptions();
        options.TopicRoot = root;

        Assert.Equal(valid, YuleOptionsValidator.Validate(options).Count == 0);
    }
}
