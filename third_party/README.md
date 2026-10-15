# Third-party dependency inventory

These dependencies are linked into the combined Controller Manager service
and the standalone Bluetooth pairing/bridge builds. They are not linked into
the standalone USB-only release. See [credits](../CREDITS.md) and
[dependency documentation](../docs/dependencies.md) for the wider project.

| Directory | Upstream | Pinned commit | License |
| --- | --- | --- | --- |
| `tiny-aes` | https://github.com/kokke/tiny-AES-c | `23856752fbd139da0b8ca6e471a13d5bcc99a08d` | Unlicense (`tiny-aes/unlicense.txt`) |
| `micro-ecc` | https://github.com/kmackay/micro-ecc | `541b3a78026420a3e369c4c9281c396b5e531113` | BSD-2-Clause (`micro-ecc/LICENSE.txt`) |

Upstream files were copied without modification. micro-ecc is compiled for
P-256 only and uses the native `libSceRandom` service for key generation and
ECDH blinding in a fresh pairing. tiny-AES-c provides AES-128 block encryption
for Bluetooth f4/f5/f6 calculations; CBC/CTR are disabled.

`testing_tools/tests/bt_crypto_vectors.c` adapts vector data from Zephyr's
`tests/bluetooth/bt_crypto/src/test_bt_crypto.c`, Copyright (c) 2022 Nordic
Semiconductor ASA, Apache-2.0. Its license is preserved in `testing_tools/licenses/Zephyr-LICENSE`.
The protocol calculations were also cross-checked against BlueZ's
`src/shared/crypto.c` and `emulator/smp.c`; their source was not imported.
