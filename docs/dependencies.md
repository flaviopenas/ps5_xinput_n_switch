# Libraries, tools and PS5 interfaces

The controller manager is a native C payload with an embedded browser client.
There is no Node.js runtime, npm dependency tree, remote API or hosted backend.
The dependency inventory below distinguishes code linked into the payload
from build tools, console services and protocol references.

## Linked third-party code

| Dependency | Pinned revision | Purpose | License |
| --- | --- | --- | --- |
| [tiny-AES-c](https://github.com/kokke/tiny-AES-c) | `23856752fbd139da0b8ca6e471a13d5bcc99a08d` | AES-128 blocks for Bluetooth address resolution and CMAC/f4/f5/f6 helpers | [Unlicense](../third_party/tiny-aes/unlicense.txt) |
| [micro-ecc](https://github.com/kmackay/micro-ecc) | `541b3a78026420a3e369c4c9281c396b5e531113` | P-256 public-key/ECDH operations during fresh BLE Secure Connections pairing | [BSD-2-Clause](../third_party/micro-ecc/LICENSE.txt) |

These libraries are compiled into the combined manager service. Optional
standalone Bluetooth builds under `testing_tools` also use them; the optional
USB-only legacy release does not.
tiny-AES-c is configured for AES-128 ECB block operations, with CBC/CTR
disabled; the project implements the required Bluetooth CMAC constructions.
micro-ecc is configured for P-256 only. Fresh pairing uses the console's
`libSceRandom` service for key generation and ECDH blinding.

Vendored files are kept in `third_party/` with their original notices. The
source-layout cleanup changes project includes, not those library sources.

## Test-only material

`testing_tools/tests/bt_crypto_vectors.c` adapts vectors from Zephyr's
`tests/bluetooth/bt_crypto/src/test_bt_crypto.c`, copyright 2022 Nordic
Semiconductor ASA, under Apache-2.0. The original acknowledgement and
[license text](https://github.com/zephyrproject-rtos/zephyr/blob/main/LICENSE) are preserved. Zephyr itself
is not linked into the controller payload.

## Build and development tools

| Tool | Role |
| --- | --- |
| [PS5 Payload SDK v0.44](https://github.com/ps5-payload-dev/sdk/releases/tag/v0.44) | Platform headers, Prospero compiler wrappers, runtime/link support and console symbols |
| Clang, LLD and LLVM 18 | SDK compiler/linker dependencies used by the setup |
| GNU Make | Native payload variants and host-test orchestration |
| Python 3 standard library | Asset embedding, ZIP packaging, send/stop/log tools and manager API tests |
| Host C compiler | Protocol, crypto, input mapping and simulated service tests |
| `curl`, `unzip`, `sha256sum` | SDK download, extraction and integrity verification |
| GNU `objdump` | Optional disassembly of supplied/captured module code |
| clang-format 18.1.8 | Optional production C/header formatting; no role in running the payload |

The SDK download/version/hash are in `scripts/setup_sdk.sh`. Reference trees
under `testing_tools/.tools/` and `testing_tools/upstream/` are development material, not runtime services
or build outputs that need to be installed on the PS5.

`make -C testing_tools format` and `make -C testing_tools format-check` use `testing_tools/.clang-format`.
Set `CLANG_FORMAT=/path/to/clang-format` when needed. The cleanup used the
18.1.8 formatter in an isolated development environment under `testing_tools/.tools/format/`.
Vendored sources and historical diagnostic probes are excluded from formatting.

## Native console services and devices

| Interface | Use |
| --- | --- |
| `/dev/hid` | Add, submit input to, and delete owned virtual controllers through firmware-specific ioctls |
| `/dev/ugen*` and USB filesystem ioctls | Supported controller discovery and endpoint transfers; the Bluetooth transport uses the console's checked function-0 layout |
| `libSceUserService` | Resolve the active/initial user for controller ownership |
| `libSceSystemService` | Open the browser from the launcher |
| `libSceRandom` | Randomness for fresh Bluetooth pairing |
| `libScePad` | Read-only ABI inspection/reference; the production virtual-pad path issues `/dev/hid` requests directly |
| `libSceBluetoothHid` | Experimental diagnostic investigation; not the production Xbox BLE input implementation |
| TCP ELF loader, port 9021 | External prerequisite that starts the launcher/service ELF |

Sony system modules are provided by the console and are not redistributed in
the project ZIP. Firmware-specific layouts are documented from inspection;
matching an export name or obtaining a handle alone does not prove compatibility.

## Research references and attribution

Linux xpad/hid-nintendo, Bluepad32, Nintendo Switch protocol notes, Chromium,
BlueZ, FreeBSD, FGG-PlayPods, OmniPad and other community references are listed
in [CREDITS.md](../CREDITS.md) with the reason they were consulted. They should
not be described as bundled libraries unless their code is actually vendored.

The project's inherited license is [GPL-3.0](../LICENSE). The main release bundle includes that license, credits, the runtime library
inventory and original runtime library notices alongside its single ELF.
Test-only notices remain under `testing_tools/licenses`.
