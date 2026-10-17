# Broker and server deployment

This folder deploys the two services that every decoration talks to:

- **Mosquitto**, the MQTT broker, with the **dynamic-security** plugin. Devices reach it over TLS on one
  public, non-standard port. The Yule server reaches it over a second, plain listener that is only visible
  inside the Docker network.
- **The Yule server** (`../server`), which owns devices, family groups and firmware releases and keeps the
  broker's permissions in step with its database.

```
 decoration ──TLS, port 18883──▶ mosquitto ◀──plain 1883, internal only── yule-server ◀── reverse proxy ◀── admin browser
                                  (dynamic security:                       (SQLite, Data Protection)
                                   deny by default)
```

The server's database is the source of truth. Mosquitto's permission file is a projection of it that the
server can always rebuild (README step 4 below is the only part you ever redo by hand). If the server is
down, the broker keeps enforcing the last permissions and devices keep exchanging presses.

## What you need

- A host with Docker and Docker Compose v2.
- A DNS name for the broker (for example `williamb.net`) and a TLS certificate for it (Let's Encrypt is fine).
- One TCP port opened to the internet for MQTT over TLS (not 8883; pick something non-standard, such as 18883).
- For the admin pages: a reverse proxy that terminates TLS and forwards to the server (for example at
  `yule.williamb.net`). See `../server/README.md`.

## 1. Configure

```sh
cd broker
cp .env.example .env              # MQTT_TLS_PORT, CERT_DIR, SERVER_HTTP_PORT
cp server.env.example server.env  # the Yule server's configuration and secrets
```

Both files are git-ignored. `MQTT_TLS_PORT` must match `YULE_BROKER_PORT` in `firmware/include/secrets.h`.
The server settings are described in `../server/README.md`; you can leave `server.env` incomplete until
step 5.

## 2. Certificates

Mosquitto reads `fullchain.pem` and `privkey.pem` from `CERT_DIR` (default `./certs`).

With certbot on the host, a deploy hook copies renewed certificates in and tells Mosquitto to reload them.
Save it as `/etc/letsencrypt/renewal-hooks/deploy/yule-broker.sh` and make it executable:

```sh
#!/bin/sh
set -e
DOMAIN=williamb.net
CERT_DIR=/srv/yule/broker/certs        # the same as CERT_DIR in .env
install -d -o 1883 -g 1883 -m 0750 "$CERT_DIR"
install -o 1883 -g 1883 -m 0640 /etc/letsencrypt/live/$DOMAIN/fullchain.pem "$CERT_DIR/fullchain.pem"
install -o 1883 -g 1883 -m 0600 /etc/letsencrypt/live/$DOMAIN/privkey.pem   "$CERT_DIR/privkey.pem"
cd /srv/yule/broker && docker compose kill -s SIGHUP mosquitto   # reload certificates, no dropped config
```

Run the script once by hand to install the first certificate. Uid 1883 is the `mosquitto` user inside the
container: it must be able to read the files, because Mosquitto reads them again on `SIGHUP`.

The firmware checks this certificate against the built-in certificate bundle and checks that the host name
matches, so use a certificate for the exact name in `YULE_BROKER_HOST`.

## 3. Create the dynamic-security state (once)

Mosquitto will not start serving clients without its dynamic-security file. Create it with an operator
account called `admin` (you are asked for a password; use a long random one and keep it in your password
manager):

```sh
docker compose run --rm --no-deps mosquitto \
  mosquitto_ctrl dynsec init /mosquitto/data/dynamic-security.json admin
docker compose up -d mosquitto
```

The log should show the plugin loading and both listeners opening:

```sh
docker compose logs mosquitto
#   Loading plugin: /usr/lib/mosquitto_dynamic_security.so
#   Opening ipv4 listen socket on port 8883.
#   Opening ipv4 listen socket on port 1883.
```

From here on, run `mosquitto_ctrl` inside the container, which reaches the internal listener on `localhost`.
Define a shell shortcut (it prompts for the admin password each time with `-P`; or pass it explicitly):

```sh
ctl() { docker compose exec mosquitto mosquitto_ctrl -u admin -P 'ADMIN-PASSWORD' dynsec "$@"; }
```

The `admin` account created by `dynsec init` can administer the plugin and can also **read** every topic (it holds
the broad `#` read permissions), although it cannot publish to ordinary topics. Step 4 narrows it.

## 4. Deny by default, and create the server's account

Everything is refused unless a role allows it. First make the default access "deny" for all four kinds of
access:

```sh
for access in publishClientSend publishClientReceive subscribe unsubscribe; do
  ctl setDefaultACLAccess $access deny
done
ctl getDefaultACLAccess
```

Narrow the operator account first, so that a leaked `admin` password cannot be used to eavesdrop on presses. Keep
its `$SYS` access for monitoring:

```sh
ctl removeRoleACL admin publishClientReceive '#'
ctl removeRoleACL admin subscribePattern '#'
ctl removeRoleACL admin unsubscribePattern '#'
```

Then create the `yule-server` service account with exactly the permissions design D5 gives it: manage the
dynamic-security plugin, publish device configs and firmware manifests, and read device status.

```sh
ctl createRole yule-server
ctl addRoleACL yule-server publishClientSend    '$CONTROL/dynamic-security/#'   allow
ctl addRoleACL yule-server publishClientReceive '$CONTROL/dynamic-security/#'   allow
ctl addRoleACL yule-server subscribePattern     '$CONTROL/dynamic-security/#'   allow
ctl addRoleACL yule-server publishClientSend    'yule/v1/devices/+/config'      allow
ctl addRoleACL yule-server publishClientSend    'yule/v1/firmware/#'            allow
ctl addRoleACL yule-server subscribePattern     'yule/v1/devices/+/status'      allow
ctl addRoleACL yule-server publishClientReceive 'yule/v1/devices/+/status'      allow
ctl createClient yule-server -p 'SERVER-PASSWORD'
ctl addClientRole yule-server yule-server 1
```

Put `SERVER-PASSWORD` in `server.env` as `Yule__Broker__Password`. Note the single quotes: `$CONTROL` is the
literal topic prefix, not a shell variable.

You never create device accounts by hand. The server creates each device's client and role when you register
the device, and fixes anything that drifts (it checks at start-up and every 10 minutes).

## 5. Start the stack

```sh
docker compose up -d
docker compose ps
```

Open the MQTT port in the host firewall (and any router or cloud security group):

```sh
sudo ufw allow 18883/tcp    # the value of MQTT_TLS_PORT
```

Do **not** open 1883. Compose does not publish it, and it must stay reachable only from the Docker network.

## Check it works

From any machine with `mosquitto_sub` (replace the host, port and CA as appropriate; with a Let's Encrypt
certificate `--capath /etc/ssl/certs` replaces `--cafile`):

```sh
# Anonymous and unknown users are refused on the public port:
mosquitto_sub -h williamb.net -p 18883 --capath /etc/ssl/certs -i probe -t x
#   Connection error: Connection Refused: not authorised.
mosquitto_sub -h williamb.net -p 18883 --capath /etc/ssl/certs -i probe -t x -u nobody -P wrong
#   Connection error: Connection Refused: not authorised.

# The internal port is not reachable from the host or the internet:
nc -vz -w 3 127.0.0.1 1883        # connection refused

# The server's account can administer dynamic security over the internal listener
# (here from a throwaway container on the Compose network):
docker run --rm --network yule_internal eclipse-mosquitto:2.0.22 \
  mosquitto_ctrl -h mosquitto -u yule-server -P 'SERVER-PASSWORD' dynsec listClients
#   admin
#   yule-server
```

## Operations

**Logs.** `docker compose logs -f mosquitto`. Compose rotates them (3 files of 10 MB).

**Certificates.** Renewal needs no restart: the deploy hook sends `SIGHUP`, Mosquitto logs
`Reloading config.` and keeps serving.

**Backups.** Back up both named volumes. They are `yule_mosquitto-data` (persistence and
`dynamic-security.json`) and `yule_yule-data` (the server's SQLite database and Data Protection keys; without the
key ring the encrypted broker passwords cannot be read again). For example:

```sh
docker run --rm -v yule_mosquitto-data:/data -v "$PWD":/backup alpine \
  tar czf /backup/mosquitto-data-$(date +%F).tgz -C /data .
docker run --rm -v yule_yule-data:/data -v "$PWD":/backup alpine \
  tar czf /backup/yule-data-$(date +%F).tgz -C /data .
```

**If the dynamic-security file is lost.** Run steps 3 and 4 again (creating the `admin` and `yule-server`
accounts). Within 10 minutes of the server running, its reconciler recreates every device account and
permission from its database, with the same passwords. No device needs re-provisioning.

**Limits.** `persistent_client_expiration 2h` and `max_queued_messages 20` stop an offline device from holding
memory indefinitely. A press that waited longer than five minutes is dropped by the device anyway.

**Upgrading Mosquitto.** Change the image tag in `docker-compose.yml` (stay on 2.0.x), then
`docker compose pull && docker compose up -d`.

## Why these settings

- `allow_anonymous false` and deny-by-default ACLs: a device can only do what its own role says: publish its
  own presses into its own groups, and receive those groups' presses. See `family-groups` in the specs.
- Two listeners: the public one carries only devices, over TLS; the plain one carries only the server and never
  leaves the Docker network. A compromised internet-facing port therefore cannot reach the server's account.
- One role per device (rather than one per group): "publish only as yourself" needs the device's own id in the
  topic, which a role shared by a group cannot express.

## Test device

To exercise the system without a physical decoration, register a virtual device such as `d-000000000001`,
labelled "Tester", on the server's Devices page and add it to a group. Its generated password then lets
`mosquitto_pub`/`mosquitto_sub` act as that device (use `-i d-000000000001` as the client id). The firmware
README ("Troubleshooting") shows how to inject test presses this way.
