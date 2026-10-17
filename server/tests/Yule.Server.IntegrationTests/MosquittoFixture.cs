using DotNet.Testcontainers.Builders;
using DotNet.Testcontainers.Containers;
using Yule.Server.Configuration;

namespace Yule.Server.IntegrationTests;

/// <summary>
/// A real Mosquitto 2.0.22 (the image the Compose file pins) with the dynamic-security plugin, set up the way
/// <c>broker/README.md</c> steps 3 and 4 describe: deny by default, and a <c>yule-server</c> account with exactly the
/// D5 permissions. Only the TLS listener and the "narrow the operator account" step are left out; neither affects
/// what the server does. Each test class gets its own broker.
/// </summary>
public sealed class MosquittoFixture : IAsyncLifetime
{
    public const string ServerUsername = "yule-server";
    public const string ServerPassword = "server-secret";

    private const string Script = """
        set -e
        mkdir -p /etc/yule /mosquitto/data
        cat > /etc/yule/mosquitto.conf <<'EOF'
        allow_anonymous false
        plugin /usr/lib/mosquitto_dynamic_security.so
        plugin_opt_config_file /mosquitto/data/dynamic-security.json
        persistence true
        persistence_location /mosquitto/data/
        persistent_client_expiration 2h
        max_queued_messages 20
        max_packet_size 4096
        log_type error
        log_type warning
        listener 1883
        protocol mqtt
        EOF
        mosquitto_ctrl dynsec init /mosquitto/data/dynamic-security.json admin admin-secret
        chown -R mosquitto:mosquitto /mosquitto/data
        mosquitto -c /etc/yule/mosquitto.conf &
        ctl() { mosquitto_ctrl -u admin -P admin-secret dynsec "$@" >/dev/null; }
        until mosquitto_ctrl -u admin -P admin-secret dynsec listClients >/dev/null 2>&1; do sleep 0.2; done
        for access in publishClientSend publishClientReceive subscribe unsubscribe; do ctl setDefaultACLAccess $access deny; done
        ctl createRole yule-server
        ctl addRoleACL yule-server publishClientSend    '$CONTROL/dynamic-security/#'   allow
        ctl addRoleACL yule-server publishClientReceive '$CONTROL/dynamic-security/#'   allow
        ctl addRoleACL yule-server subscribePattern     '$CONTROL/dynamic-security/#'   allow
        ctl addRoleACL yule-server publishClientSend    'yule/v1/devices/+/config'      allow
        ctl addRoleACL yule-server publishClientSend    'yule/v1/firmware/#'            allow
        ctl addRoleACL yule-server subscribePattern     'yule/v1/devices/+/status'      allow
        ctl addRoleACL yule-server publishClientReceive 'yule/v1/devices/+/status'      allow
        ctl createClient yule-server -p server-secret
        ctl addClientRole yule-server yule-server 1
        echo YULE-BOOTSTRAPPED
        wait
        """;

    private IContainer _container = null!;

    public string Host => _container.Hostname;

    public int Port => _container.GetMappedPublicPort(1883);

    /// <summary>How the Yule server connects: the <c>yule-server</c> account. Each call gets its own client id so a stopped server and a restarted one do not collide.</summary>
    public BrokerOptions ServerOptions() => new()
    {
        Host = Host,
        Port = Port,
        Username = ServerUsername,
        Password = ServerPassword,
        ClientId = "yule-server-" + Guid.NewGuid().ToString("N")[..8],
    };

    public async Task InitializeAsync()
    {
        _container = new ContainerBuilder("eclipse-mosquitto:2.0.22")
            .WithEntrypoint("sh", "-c", Script)
            .WithPortBinding(1883, assignRandomHostPort: true)
            .WithWaitStrategy(Wait.ForUnixContainer().UntilMessageIsLogged("YULE-BOOTSTRAPPED"))
            .Build();
        await _container.StartAsync();
    }

    public async Task DisposeAsync() => await _container.DisposeAsync();
}
