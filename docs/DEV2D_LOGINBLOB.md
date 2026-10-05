# dev.2d LoginBlob gate

`v0.1.0-dev.2d-loginblob-r4` fixes two independent issues observed on the qualified Waveshare ESP32-S3 / WLED 17 target.

## Hardware evidence from r3

The official Spotify app delivered a structurally valid `addUser` (`user=28`, Base64 blob `476 -> 356`, client key `128 -> 96`). r3 progressed through DH, streaming SHA-1, manual HMAC-SHA1, primary MAC verification, AES-128 CTR, secondary Base64, device hash, PBKDF2 and base hash. The final r3 diagnostic was `stage=secondary-aes-ecb` with `primary=320` and `secondary=240`.

This isolates the remaining crypto failure to the required AES-192 ECB layer. Spotify/librespot specifies a 24-byte AES-192 key for this layer. ESP32-S3 hardware AES supports AES-128 and AES-256, not AES-192, so r4 performs only this one-time pairing decrypt in a small software AES-192 implementation. The primary AES-128 CTR path remains unchanged on mbedTLS.

The software AES-192 implementation is dependency-free and is verified prebuild against the FIPS-197 AES-192 known-answer vector.

## Postbuild runner correction

The previous generic runner used `strings FILE | grep -q` while `set -o pipefail` was active. Positive firmware checks could therefore report FAIL even when the string existed: once `grep -q` found a match it exited, `strings` received SIGPIPE, and the whole pipeline became non-zero. r4 searches ELF/BIN raw bytes with `grep -aFq` instead. Project-specific expected strings remain exclusively in `tests/release_checks.tsv`; the update script stays generic.

## Hardware gate

After flashing r4, select `WLED Matrix` once in the official Spotify app and inspect `/json/info`. The desired result is `accepted=1`, `credential=stored`, `authType=1`, non-zero auth bytes, `LoginBlob stage=complete`, `ok=1`, and `persisted=1`. Reboot without selecting the device again and confirm that the stored credential is restored.
