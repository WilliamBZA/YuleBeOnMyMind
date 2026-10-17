# Yule server

The ASP.NET Core (.NET 10) service behind the decorations. It owns the list of **devices**, **family groups** and
(later) **firmware releases**, and keeps the Mosquitto broker in step with them:

- It creates each device's broker login and permissions when you register the device, and updates or removes
  them as you change groups. Changes reach the broker, and the devices, within seconds.
- It publishes every device's retained `config` (its group names and update channel) and records each device's
  `status` (online, firmware version, last update result).
- It serves a small set of admin pages behind one administrator login.

The database is the source of truth. The broker's permissions are a projection of it that the server rechecks at
start-up and every 10 minutes, repairing anything that differs (including lost device accounts, with the same
passwords). If the server is down, devices keep exchanging presses; only admin changes wait.

## The admin pages

| Page | Address | What you do there |
|---|---|---|
| Devices | `/devices` | Register a device by its id and a label. Edit a label and update channel. Remove a device. |
| Groups | `/groups` | Create, rename and delete groups. Add and remove member devices. |
| Status | `/status` | See each device's state, firmware version, board, last update result and last seen. Refreshes every 30 seconds. |

Everything except `/login`, `/healthz` and the error page needs the administrator login. There is one
administrator and no user accounts.

**Registering a device.** Read the device's id (`d-` and 12 hex digits) from its settings page or its serial log,
and enter it with a label such as "Gran (Brander)". The page then shows the device's generated broker password
**once**. Type it into the device's settings page under *Device setup*. It cannot be looked up again: if it is
lost, remove the device and register it again. The device can connect as soon as the password is entered.

**Groups.** A press reaches the other devices in every group the presser's device is in, and nobody else. Add a
device to a group from the group's page; within about 10 seconds the device is told (nothing is done on it) and the
broker lets it send to and hear from the group. Removing a device from a group, deleting a group, or removing a
device takes effect the same way, and a removed device is disconnected immediately.

**If the broker is unreachable** when you save a change, the change is still saved and the page says the broker
could not be updated just now. The server applies it as soon as it can reach the broker again (it checks every
10 minutes and whenever the connection comes back).

## Run it locally

You need the [.NET 10 SDK](https://dotnet.microsoft.com/download) and a running Mosquitto set up as in
`../broker/README.md` (Docker is the easiest way). Then, from this folder:

```sh
# 1. A password hash for the administrator login
dotnet run --project src/Yule.Server -- hash-password

# 2. A release signing public key (the private key stays offline; see "Release keys" below)
openssl ecparam -name prime256v1 -genkey -noout -out release-key.pem
openssl ec -in release-key.pem -pubout -out release-pub.pem

# 3. Run, with the settings as environment variables (PowerShell: $env:NAME = "value")
export Yule__Admin__PasswordHash='PBKDF2-SHA256.600000...'
export Yule__Broker__Host=localhost            # a broker whose plain listener is reachable from here
export Yule__Broker__Port=1883
export Yule__Broker__Username=yule-server
export Yule__Broker__Password='SERVER-PASSWORD'
export Yule__PublicBaseUrl=http://localhost:5071
export Yule__ReleasePublicKeys__0="$(cat release-pub.pem)"
export Yule__DataDirectory=./data
dotnet run --project src/Yule.Server
```

Browse to <http://localhost:5071>. The development profile listens there. The `data` folder holds the database
and the encryption keys; it is created on first run.

The plain listener (1883) is not published by `../broker/docker-compose.yml`, so for local development either run
a throwaway broker that publishes it (see how `tests/Yule.Server.IntegrationTests/MosquittoFixture.cs` sets one
up), or run the whole stack in Docker as described below.

## Configuration

Settings are read from the `Yule` configuration section. In deployment they come from environment variables,
where `__` stands for `:` (so `Yule__Admin__PasswordHash` is `Yule:Admin:PasswordHash`). The server refuses to
start, with a message naming each missing or invalid setting, until the required ones are set.

| Environment variable | Required | Meaning |
|---|---|---|
| `Yule__Admin__PasswordHash` | yes | The administrator's password hash, from `hash-password`. |
| `Yule__Broker__Host` | yes | The broker's internal address (in Compose: `mosquitto`, set by the Compose file). |
| `Yule__Broker__Port` | no | The broker's plain listener. Default `1883`. |
| `Yule__Broker__Username` | yes | The broker service account, normally `yule-server` (`../broker/README.md`, step 4). |
| `Yule__Broker__Password` | yes | That account's password. |
| `Yule__Broker__ClientId` | no | MQTT client id. Default `yule-server`. |
| `Yule__PublicBaseUrl` | yes | The server's public address, for example `https://yule.williamb.net`. Must be `https` (plain `http` is accepted only for localhost). Firmware download addresses are built from it. |
| `Yule__ReleasePublicKeys__0`, `__1` | yes, one or two | PEM public keys (ECDSA P-256) that firmware releases must be signed with. Write newlines as `\n` in an env file. |
| `Yule__DataDirectory` | yes | Where the database (`yule.db`), firmware images and encryption keys (`keys/`) live. A volume in Docker (`/data`, set by the Compose file). |
| `Yule__TopicRoot` | no | Root of the MQTT topics. Default `yule/v1`. Must match the firmware's `YULE_TOPIC_ROOT`. |
| `Yule__ReconcileIntervalMinutes` | no | How often the broker's permissions are checked and repaired. Default `10`. |
| `Yule__TrustForwardedHeaders` | no | Believe `X-Forwarded-For` and `X-Forwarded-Proto`. Default `false`; the Compose file sets `true` because the server's port is published on loopback only, so only your reverse proxy can reach it. Leave it off anywhere the server can be reached directly: anyone could otherwise forge their address and defeat the login throttle. |

### Secrets

`broker/server.env` holds the administrator hash and the broker password. It is git-ignored; keep it that way,
and keep it readable only by you (`chmod 600`). The release **private** key is never given to the server and
never goes in this file: the server only needs the public half.

### The administrator password

```sh
# Local:
dotnet run --project src/Yule.Server -- hash-password
# With Docker, once the image is built (docker compose build yule-server):
docker run --rm -it yule-server:local hash-password
```

Both ask for the password twice (at least 12 characters, typed hidden) and print a line such as
`PBKDF2-SHA256.600000.<salt>.<hash>`. Put that whole line in `Yule__Admin__PasswordHash`. It contains no `$`, so
Docker Compose leaves it alone. To change the password, make a new hash, replace the line and
`docker compose up -d yule-server`. To script it, pipe the password in instead:
`printf '%s\n' "$PASSWORD" | docker run --rm -i yule-server:local hash-password`.

Login attempts are throttled: five wrong passwords from one address within 15 minutes lock that address out until
the oldest has aged out, and thirty wrong passwords from all addresses together lock everyone out the same way, so
guessing over the internet is not practical. A sign-in lasts eight hours of inactivity.

### Release keys

Firmware updates are signed with an ECDSA P-256 key pair. The public key goes in `Yule__ReleasePublicKeys__0` (and
in the firmware's `secrets.h`); the private key stays offline on the build PC.

```sh
openssl ecparam -name prime256v1 -genkey -noout -out release-key.pem      # keep offline, back it up
openssl ec -in release-key.pem -pubout -out release-pub.pem               # the public half
# For server.env, as one line with \n for the newlines:
awk 'BEGIN{ORS="\\n"} {print}' release-pub.pem; echo
```

Up to two public keys may be configured so the key can be rotated later. The release workflow is described in the
top-level `README.md`.

## Deploy with Docker Compose

The Compose file in `../broker` runs the server next to Mosquitto. Set up the broker first
(`../broker/README.md`, steps 1 to 4), then:

1. **Configure the server.** From `broker/`: `cp server.env.example server.env`, then fill in
   `Yule__Admin__PasswordHash`, `Yule__Broker__Password` (the password you gave the `yule-server` account),
   `Yule__PublicBaseUrl` and `Yule__ReleasePublicKeys__0`. The broker address, data directory and
   forwarded-header setting are fixed by the Compose file.
2. **Build and start.**
   ```sh
   docker compose build yule-server
   docker compose up -d
   docker compose ps
   docker compose logs yule-server
   ```
   The log should show `Connected to the broker` and `Broker reconciled`. A message beginning "The Yule server
   cannot start because its configuration is incomplete" lists exactly what to set.
3. **Check it.** `curl http://127.0.0.1:8080/healthz` prints `ok` (the host port is `SERVER_HTTP_PORT` in `.env`).
   The server listens on the loopback address only; it is not reachable from the network until you put the
   reverse proxy in front.

### Behind the reverse proxy

The server speaks plain HTTP on `127.0.0.1:${SERVER_HTTP_PORT}` and relies on your reverse proxy for TLS. The
proxy must pass the client's address and scheme on. The login cookie is only marked `Secure` when the server
sees `https`, which it learns from `X-Forwarded-Proto`.

Caddy:

```
yule.williamb.net {
    reverse_proxy 127.0.0.1:8080
}
```

(Caddy sets the forwarded headers itself.)

nginx:

```nginx
server {
    listen 443 ssl;
    server_name yule.williamb.net;
    # ssl_certificate ... ssl_certificate_key ...

    location / {
        proxy_pass http://127.0.0.1:8080;
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-For $remote_addr;      # replace, never append: the server trusts it
        proxy_set_header X-Forwarded-Proto $scheme;
    }
}
```

Then visit `https://yule.williamb.net` and log in. To keep the admin pages private you can also restrict `/` at the
proxy to your own network, but leave `/firmware/` and `/healthz` open: devices download updates from
`/firmware/` without logging in.

Do not publish the server's port on anything other than loopback, and do not set
`Yule__TrustForwardedHeaders` if you change that.

### Upgrading

```sh
git pull
docker compose build yule-server
docker compose up -d yule-server
```

The database migrates itself on start-up. The data volume is kept.

## Backups

Everything the server needs to come back is in the `yule_yule-data` volume: the SQLite database and the Data
Protection key ring in `keys/`. **Back up both together.** Broker passwords are stored encrypted with that key
ring; without it the passwords cannot be read, the server cannot rebuild device logins, and every device would need
a new password. The commands for backing up the volumes are in `../broker/README.md` ("Backups").

To restore, create the volume again, unpack the backup into it, and start the stack. Within 10 minutes the
reconciler brings the broker's accounts and permissions back in line with the restored database.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| The container exits straight away with exit code 78 | The configuration is incomplete. `docker compose logs yule-server` names each setting. |
| The log repeats `Cannot connect to the broker` | The broker is not up, or `Yule__Broker__Username`/`Password` do not match the `yule-server` account. It keeps retrying with a back-off. |
| `The broker rejected dynamic-security commands` or a timeout on `$CONTROL/dynamic-security` | The `yule-server` role is missing or incomplete (`../broker/README.md`, step 4), or the plugin is not loaded. |
| A page says "The change is saved, but the broker could not be updated just now" | The broker could not be reached or did not answer within 15 seconds. The change is safe in the database and is applied by the next reconcile. |
| `Cannot decrypt the broker password of d-…` | The Data Protection key ring was lost or replaced. Restore the volume from a backup, or remove and re-register the affected devices. |
| You cannot log in and have not got the password | Generate a new hash and replace `Yule__Admin__PasswordHash`. |
| "Too many failed attempts" at the login page | Wait for the time it gives (at most 15 minutes), or restart the server, which forgets the count. |

## Tests

```sh
dotnet test                                       # everything
dotnet test tests/Yule.Server.Tests                # unit and page tests; no Docker needed
dotnet test tests/Yule.Server.IntegrationTests     # a real Mosquitto in Docker (Testcontainers)
```

The page tests run the real application with a recording stand-in for the broker. The integration tests start the
real application against a real Mosquitto with dynamic security and drive the admin pages over HTTP, with stand-in
devices that connect using the password the Devices page showed.
