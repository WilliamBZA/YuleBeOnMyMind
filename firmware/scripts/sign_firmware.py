"""Signs a firmware image for release (design D13).

Writes, next to each other in firmware/dist/ (git-ignored):
  yule-<version>-<board>.bin            the image, unchanged
  yule-<version>-<board>.manifest.json  {"v":1,"version":...,"board":...,"size":...,"sha256":...,"sig":...}

"sig" is an ECDSA P-256 signature over the SHA-256 of the image, DER-encoded, in base64: what the Yule server
checks on upload and the devices check before installing. Upload both files on the server's Releases page.

The private key never enters the repository. Point YULE_SIGNING_KEY at its PEM file:

  As a PlatformIO post-build step (runs after every ESP32 build; skipped with a notice if YULE_SIGNING_KEY is unset):
    YULE_SIGNING_KEY=~/keys/yule-release.pem pio run -e esp32dev

  By hand, for any image:
    python scripts/sign_firmware.py --image .pio/build/esp32dev/firmware.bin --version 0.3.0 --board esp32dev \\
        [--key ~/keys/yule-release.pem] [--out dist]

Uses the `cryptography` package when it is installed (PlatformIO's own Python has it), otherwise the `openssl`
command-line tool. Generating a key pair is described in the README ("Releasing firmware").
"""

import argparse
import base64
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

KEY_ENV = "YULE_SIGNING_KEY"
MAX_IMAGE_BYTES = 0x1E0000  # one app slot in partitions.csv
VERSION_PATTERN = re.compile(r"^[0-9]{1,4}\.[0-9]{1,4}\.[0-9]{1,4}$")
BOARD_PATTERN = re.compile(r"^[a-z0-9-]{1,32}$")


class SigningError(Exception):
    pass


def sign_image(image, key_path):
    """Returns the DER ECDSA P-256 signature over SHA-256(image)."""
    try:
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import ec
    except ImportError:
        return sign_with_openssl(image, key_path)

    with open(key_path, "rb") as f:
        try:
            key = serialization.load_pem_private_key(f.read(), password=None)
        except (ValueError, TypeError) as e:
            raise SigningError("%s is not an unencrypted PEM private key (%s)" % (key_path, e))
    if not isinstance(key, ec.EllipticCurvePrivateKey) or key.curve.name != "secp256r1":
        raise SigningError("%s is not an ECDSA P-256 (prime256v1) key" % key_path)
    signature = key.sign(image, ec.ECDSA(hashes.SHA256()))
    key.public_key().verify(signature, image, ec.ECDSA(hashes.SHA256()))  # never ship a signature that does not check
    return signature


def sign_with_openssl(image, key_path):
    openssl = shutil.which("openssl")
    if not openssl:
        raise SigningError("neither the Python 'cryptography' package nor the 'openssl' tool is available")
    curve = subprocess.run([openssl, "ec", "-in", key_path, "-noout", "-text"], capture_output=True, text=True)
    if curve.returncode != 0:
        raise SigningError("%s is not an unencrypted EC private key: %s" % (key_path, curve.stderr.strip()))
    if "prime256v1" not in curve.stdout and "P-256" not in curve.stdout:
        raise SigningError("%s is not an ECDSA P-256 (prime256v1) key" % key_path)

    with tempfile.TemporaryDirectory() as work:
        image_path = os.path.join(work, "image.bin")
        sig_path = os.path.join(work, "image.sig")
        pub_path = os.path.join(work, "public.pem")
        with open(image_path, "wb") as f:
            f.write(image)
        steps = [
            [openssl, "dgst", "-sha256", "-sign", key_path, "-out", sig_path, image_path],
            [openssl, "ec", "-in", key_path, "-pubout", "-out", pub_path],
            [openssl, "dgst", "-sha256", "-verify", pub_path, "-signature", sig_path, image_path],
        ]
        for step in steps:
            run = subprocess.run(step, capture_output=True, text=True)
            if run.returncode != 0:
                raise SigningError("openssl failed: %s" % (run.stderr.strip() or run.stdout.strip()))
        with open(sig_path, "rb") as f:
            return f.read()


def sign_release(image_path, version, board, key_path, out_dir):
    if not VERSION_PATTERN.match(version):
        raise SigningError("version %r is not MAJOR.MINOR.PATCH (see fw_version in platformio.ini)" % version)
    if not BOARD_PATTERN.match(board):
        raise SigningError("board %r is not a board id such as esp32dev" % board)
    if not key_path:
        raise SigningError("no signing key: set %s to the path of the release private key" % KEY_ENV)
    key_path = os.path.expanduser(key_path)
    if not os.path.isfile(key_path):
        raise SigningError("signing key %s does not exist" % key_path)

    with open(image_path, "rb") as f:
        image = f.read()
    if not image or image[0] != 0xE9:
        raise SigningError("%s is not an ESP32 firmware image" % image_path)
    if len(image) > MAX_IMAGE_BYTES:
        raise SigningError("%s is %d bytes, more than an app slot holds (%d)" % (image_path, len(image), MAX_IMAGE_BYTES))

    signature = sign_image(image, key_path)
    manifest = {
        "v": 1,
        "version": version,
        "board": board,
        "size": len(image),
        "sha256": hashlib.sha256(image).hexdigest(),
        "sig": base64.b64encode(signature).decode("ascii"),
    }

    os.makedirs(out_dir, exist_ok=True)
    stem = os.path.join(out_dir, "yule-%s-%s" % (version, board))
    with open(stem + ".bin", "wb") as f:
        f.write(image)
    with open(stem + ".manifest.json", "w", encoding="ascii", newline="\n") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    return stem + ".bin", stem + ".manifest.json", manifest


def main(argv):
    parser = argparse.ArgumentParser(description="Sign a Yule firmware image for release.")
    parser.add_argument("--image", required=True, help="the firmware .bin to sign")
    parser.add_argument("--version", required=True, help="its version, MAJOR.MINOR.PATCH")
    parser.add_argument("--board", required=True, help="its board id: esp32dev, esp32-c3 or esp32-s3")
    parser.add_argument("--key", default=os.environ.get(KEY_ENV), help="release private key (PEM); default $%s" % KEY_ENV)
    parser.add_argument("--out", default="dist", help="output directory (default: dist)")
    args = parser.parse_args(argv)
    try:
        bin_path, manifest_path, manifest = sign_release(args.image, args.version, args.board, args.key, args.out)
    except SigningError as e:
        print("sign_firmware: %s" % e, file=sys.stderr)
        return 1
    print("sign_firmware: signed %s %s (%d bytes, sha256 %s)" % (manifest["version"], manifest["board"], manifest["size"], manifest["sha256"]))
    print("  %s\n  %s" % (bin_path, manifest_path))
    return 0


def board_from_defines(env):
    for define in env.get("CPPDEFINES", []):
        if isinstance(define, (list, tuple)) and len(define) == 2 and define[0] == "YULE_BOARD_ID":
            return str(define[1]).replace("\\", "").strip('"')
    return None


def register_post_build(env):
    project = env.subst("$PROJECT_DIR")
    version = env.GetProjectConfig().get("common", "fw_version").strip()
    board = board_from_defines(env)

    def sign_after_build(source, target, env):  # noqa: ARG001 - SCons action signature
        key = os.environ.get(KEY_ENV)
        if not key:
            print("sign_firmware: not signing (set %s to sign release images)" % KEY_ENV)
            return
        if not board:
            print("sign_firmware: YULE_BOARD_ID is not defined for this environment; not signing", file=sys.stderr)
            env.Exit(1)
        try:
            bin_path, manifest_path, manifest = sign_release(
                env.subst("$BUILD_DIR/${PROGNAME}.bin"), version, board, key, os.path.join(project, "dist"))
        except SigningError as e:
            print("sign_firmware: %s" % e, file=sys.stderr)
            env.Exit(1)
        print("sign_firmware: signed %s for %s -> %s, %s" % (version, board, os.path.relpath(bin_path, project),
                                                            os.path.relpath(manifest_path, project)))

    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", sign_after_build)


try:
    Import("env")  # noqa: F821 - defined when PlatformIO runs this as an extra script
except NameError:
    if __name__ == "__main__":
        sys.exit(main(sys.argv[1:]))
else:
    register_post_build(env)  # noqa: F821
