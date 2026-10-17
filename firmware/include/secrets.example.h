// Copy this file to secrets.h (git-ignored) and edit it. secrets.h holds the values that are shared by
// every device built from this checkout; per-device values (Wi-Fi, broker password) are never compiled in.
#pragma once

// MQTT broker. The host must be covered by the broker's TLS certificate.
#define YULE_BROKER_HOST "williamb.net"
// The non-standard public TLS port the broker listens on. CHANGE THIS to the port you opened.
#define YULE_BROKER_PORT 18883

// Release signing public keys (ECDSA P-256, PEM). Firmware updates are only installed when their
// signature verifies against one of these. Generate a key pair with the openssl commands in the
// README ("Releasing firmware"), keep the private key offline, and paste the public key here.
// A second key allows key rotation: ship firmware that trusts both, then sign with the new key.
// Leave YULE_RELEASE_PUBKEY_2 as an empty string if unused.
#define YULE_RELEASE_PUBKEY_1 \
  "-----BEGIN PUBLIC KEY-----\n" \
  "REPLACE_WITH_YOUR_PUBLIC_KEY_BASE64_LINE_1\n" \
  "REPLACE_WITH_YOUR_PUBLIC_KEY_BASE64_LINE_2\n" \
  "-----END PUBLIC KEY-----\n"
#define YULE_RELEASE_PUBKEY_2 ""

// Optional: pin a specific CA certificate (PEM) for the broker instead of using the built-in
// certificate bundle. Leave empty to use the bundle (right for Let's Encrypt).
#define YULE_BROKER_CA_PEM ""
