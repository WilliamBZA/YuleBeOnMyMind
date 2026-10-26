# Signed release fixture

Output of `firmware/scripts/sign_firmware.py`, checked by `ReleaseVerifierTests` to prove that what the signing
script writes is what the server accepts (task 16.1).

- `yule-0.0.1-esp32dev.bin`: a 4 KB stand-in image with a real ESP32 image header (magic byte `0xE9`, chip id 0).
- `yule-0.0.1-esp32dev.manifest.json`: the manifest the script wrote for it.
- `release-public-key.txt`: the public half of a throwaway key pair (PEM). The private key was discarded.

To make it again:

```sh
openssl ecparam -name prime256v1 -genkey -noout -out test-release.pem
openssl ec -in test-release.pem -pubout -out release-public-key.txt
python firmware/scripts/sign_firmware.py --image firmware.bin --version 0.0.1 --board esp32dev --key test-release.pem --out .
```
