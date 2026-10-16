# Project guide for coding agents

## Purpose and scope

PS5 XInput Controller Manager lets supported Xbox and 8BitDo controllers control
a PS5 through USB and Bluetooth. A native background service owns
the controllers and serves an embedded web interface.

The main project has **one public payload with runtime firmware selection**:
`dist/ps5-xinput-manager.elf`. It embeds a matching service ELF built internally
at `build/ps5-xinput-service.elf`. Closing the browser does not stop the service.
The provisional ABI rule is firmware <=4.03: 16-byte submissions and motion
offset 0x80; firmware >4.03: 24-byte submissions and motion offset 0x78. This
threshold is user-authorized; it does not establish compatibility on untested
firmware. Firmware detection failure must prevent virtual-pad registration.

Everything outside that main product belongs in `testing_tools/`. This includes
host tests, diagnostics, standalone payloads, alternate firmware builds, helper
tools, reference checkouts, archived logs and earlier binaries.

Read [README.md](README.md) for setup and support status,
[CREDITS.md](CREDITS.md) for provenance, and
[architecture](docs/architecture.md) for ownership and data flow.

## Repository structure

```text
Makefile                         Main entry point; default target is manager
AGENTS.md                        This project and contributor guide
README.md                        User-facing overview and setup
CREDITS.md                       Upstream authors, libraries and research
LICENSE                          Inherited GPL-3.0 license
.gitignore                       Generated outputs and local material exclusions

src/
  app/                           Manager launcher, service and HTTP API
  usb/                           USB protocols and combined-service scheduler
  bluetooth/                     Radio transport, protocols, pairing and sessions
  platform/                      Runtime firmware selection, virtual pads, users and logs

manager/                         Embedded HTML, CSS and JavaScript
mk/
  config.mk                      SDK, production includes and crypto settings
  manager.mk                     Main service, assets and launcher build rules
scripts/
  setup_sdk.sh                   Install/check the pinned SDK archive
  embed_assets.py                Generate web-asset and service-ELF C arrays
  package_manager.py             Package the single main ELF and documentation

third_party/
  README.md                      Pinned library revisions and notices
  tiny-aes/                      AES implementation and original license
  micro-ecc/                     P-256 implementation and original license
docs/
  controller-manager.md          Main manager usage and API guide
  architecture.md                Module boundaries and lifecycle
  dependencies.md                Libraries, tools and native PS5 interfaces

.tools/ps5-payload-sdk/           Local build prerequisite; ignored
build/                           Generated main objects, assets and service ELF
dist/                            Main manager ELF, SHA256SUMS.manager and ZIP

testing_tools/
  Makefile                       Explicit opt-in development entry point
  README.md                      Development commands and preserved tools guide
  mk/                            Host, standalone, diagnostic and firmware rules
  tests/                         Host tests, platform fakes and report fixtures
  src/diagnostics/               Probes, ABI capture and shell experiments
  src/app/                       Preserved alternate manager implementations
  src/platform/                  Alternate firmware/diagnostic virtual-pad code
  src/usb/                       Preserved standalone USB entry point
  src/bluetooth/                 Preserved standalone/experimental Bluetooth code
  scripts/                       Send/stop/log helpers and inspection tools
  docs/                          Historical validation and investigation records
  licenses/                      Test-only license material
  .clang-format                  Production C/header formatting configuration
  .editorconfig                  Development editor configuration
  .tools/                        Optional local development tools/references
  upstream/                      Preserved upstream reference checkouts
  artifacts/                     Previous builds, logs, snapshots and kernel material
  build/                         Generated development/test outputs
  dist/                          Standalone, diagnostic and alternate-firmware ELFs
```

Generated and local directories may be absent in a fresh checkout. Do not treat
archived files under `testing_tools/artifacts/` as current build outputs.

## Production module responsibilities

| Files | Responsibility |
| --- | --- |
| `src/app/manager_launcher.c` | Start the embedded service through the ELF loader, open the browser, then exit |
| `src/app/manager_service.c` | Controller slots, reconnect policy, commands, service lifetime and orchestration |
| `src/app/manager_http.c` | Bounded HTTP requests, embedded assets and manager API |
| `src/usb/usb_bridge.c`, `usb_service.h` | USB hotplug, per-controller state, scheduling, retries and cleanup |
| `src/usb/gip.c`, `gip.h` | USB device discovery, endpoint transfers, Xbox GIP and wired-format XInput handling |
| `src/usb/switch_usb.c`, `switch_usb.h` | Switch setup, calibration and input normalization; some helpers are reused by classic Bluetooth |
| `src/bluetooth/bt_session.c`, `bt_session.h` | Per-link state, shared-radio orchestration and virtual-pad forwarding |
| `src/bluetooth/bt_hci.c`, `bt_hci.h` | Shared console USB HCI transport and completion handling |
| `src/bluetooth/bt_advert.c`, `bt_advert.h` | Discovery report parsing and controller identification |
| `src/bluetooth/bt_smp.c`, `bt_smp.h` | Bluetooth LE Secure Connections pairing |
| `src/bluetooth/bt_crypto.c`, `bt_crypto.h` | Bluetooth cryptographic constructions and secret-buffer helpers |
| `src/bluetooth/bt_hog.c`, `bt_hog.h` | GATT HID discovery, report-map checks and notification subscriptions |
| `src/bluetooth/bt_xbox.c`, `bt_xbox.h`, `bt_xbox_map.inc` | Verified Xbox BLE report decoding and report-map fixture |
| `src/bluetooth/bt_classic.c`, `bt_classic.h` | Switch Pro protocol over classic Bluetooth, including Ultimate 2C input |
| `src/bluetooth/bt_store.c`, `bt_store.h` | Private saved bonds and reconnect preferences |
| `src/bluetooth/bt_timing.c`, `bt_timing.h` | Independent per-link report-arrival and submission statistics |
| `src/platform/vpad.c`, `vpad.h` | Runtime firmware/ABI selection, `/dev/hid` requests and normalized-input translation |
| `src/platform/user.c`, `user.h` | Resolve the PS5 user owning a virtual controller |
| `src/platform/log.c`, `log.h` | Append-only console logs, UDP streaming, notifications and cleanup requests |

Input flows from USB/Bluetooth protocol decoding into an internal GIP-shaped
representation, then through `vpad_input` into the firmware-specific virtual-pad
report. Switch and BLE controllers do not speak GIP on the wire. Do not confuse
raw controller HID reports, `ScePadData`, and the kernel-facing virtual report.

## Boundaries to preserve

- Keep production source and required runtime libraries outside `testing_tools`.
  Keep tests, probes, alternate builds and nonessential utilities inside it.
- The main Makefile must not include development rules or require headers,
  sources, scripts, license inputs or reference checkouts from `testing_tools`.
  The main product has been built successfully with that directory absent.
- Publish only the manager ELF in main `dist/`; its service ELF is internal.
  A ZIP and checksum are packaging outputs, not additional payloads.
- Main sources select the 16/24-byte ABI using the provisional firmware threshold.
  Fixed 13.60 variants and legacy entry points remain under `testing_tools/src`.
- USB controllers own separate device handles, virtual pads and recovery state.
  Bluetooth links own separate protocol state, input queues, pads and bonds.
- Bluetooth links share one HCI transport/reader. Preserve peer input during
  setup and avoid recursive polling from transport callbacks.
- Preserve cleanup of owned devices and transfers. Native DualSense handoff
  can pause managed Bluetooth; do not remove that behavior casually.
- Never infer working console controls solely from registration or accepted
  report counts. Keep host simulation, measured delivery and physical testing
  clearly distinguished in support documentation.

## Build and validation

Run commands from the project root using Linux or WSL. On Windows this project
can be accessed through the WSL filesystem; use the Linux toolchain for builds.

```sh
bash scripts/setup_sdk.sh
make
# Existing SDK installation:
PS5_PAYLOAD_SDK=/path/to/sdk make
```

The SDK setup pins PS5 Payload SDK v0.44 and verifies its archive hash. The build
uses Clang/LLD/LLVM 18, GNU Make and Python 3. Production C builds enable
`-Wall -Wextra -Werror`. The browser assets need no npm or separate web server.

Development commands are explicit:

```sh
make -C testing_tools host-test
make -C testing_tools format-check
make -C testing_tools format
make -C testing_tools manager-1360
```

The formatting target uses `testing_tools/.clang-format`; set `CLANG_FORMAT`
to select a formatter if necessary. Vendored sources and archived probes are
excluded. See [testing_tools/README.md](testing_tools/README.md) for other builds.

Run checks appropriate to the change. For protocol, mapping, ownership or
source-layout changes, build the main payload and run the host suite. It covers
firmware request layouts, crypto, four-controller USB/BLE routing, mixed
Xbox/Switch simulations, manager commands and cleanup. Host tests do not verify
radio reliability, console UI behavior or physical input latency.

For packaging changes, verify that the main ZIP contains exactly one ELF,
required credits/licenses, and checksums naming files as extracted from the ZIP.
Update documentation when paths, commands, outputs or support status change.

## Console operation and logs

| Interface | Purpose |
| --- | --- |
| TCP 9021 | External ELF loader required to launch the manager/service |
| TCP 9030 | Manager web interface and API |
| UDP 9024 | Main manager log stream and clean-stop requests |
| `/data/ps5-xinput/manager.log` | Console log, appended across runs |
| `/data/ps5-xinput/bonds/` | Private Bluetooth bonds and reconnect preferences |

Optional tools, from the project root:

```sh
python3 testing_tools/scripts/send_payload.py PS5_IP --elf dist/ps5-xinput-manager.elf
python3 testing_tools/scripts/watch_logs.py --ps5 PS5_IP --remote-port 9024 --port 19024
python3 testing_tools/scripts/stop_payload.py PS5_IP --port 9024
```

Use the console address supplied for the current task. The launcher reuses a
service already listening on port 9030: stop an older service before replacing
it. Older standalone tools can use UDP 9023. A recent-log replay or appended
console log can contain earlier sessions; use timestamps when diagnosing input.

## Dependencies and attribution

The main service links pinned tiny-AES-c and micro-ecc for Bluetooth pairing.
Keep their original code/license notices and the revisions recorded in
[third_party/README.md](third_party/README.md). Zephyr vector material is
test-only, with its notice under `testing_tools/licenses/`.

Preserve the inherited GPL-3.0 license and credit to FGG STORE / Fathi Ghanem,
FGG-XSense, FGG-PlayPods, the SDK contributors and the other sources listed in
`CREDITS.md`. Distinguish linked libraries, derived code and protocol research
references. Add verified provenance when introducing dependencies; do not
invent author names or claim that referenced projects endorse this fork.
