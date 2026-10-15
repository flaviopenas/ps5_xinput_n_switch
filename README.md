# PS5 XInput Controller Manager

Use supported Xbox and 8BitDo controllers (Ultimate 2.4GHZ and Ultimate 2C) on a PS5 
through USB or the console's Bluetooth radio. A local web interface manages pairing, 
saved controllers, automatic reconnect and input monitoring.

This project builds on **FGG STORE / Fathi Ghanem's
[FGG-XSense](https://github.com/FGGstore/FGG-XSense)**. Their original USB and
virtual-controller work is the foundation of this project. See
[credits](CREDITS.md) for the authors, libraries and research used.

**The default build targets firmware 4.03.** The separate 13.60 build has been
confirmed working by a tester with a USB Xbox controller. Compatibility with
other firmware versions has not been established.

## Features and support

The combined service has capacity for four supported USB devices and four
Bluetooth links, each with its own virtual PlayStation pad. It supports USB
hotplug, eight saved Bluetooth bonds, four reconnect preferences, and Xbox
Guide / supported controller Home mapping. Closing the browser leaves the
controller service running.

| Controller / connection | Evidence |
| --- | --- |
| Xbox One / Series, USB GIP | 4.03 controls, gameplay and Home confirmed; two USB controllers independently confirmed |
| Xbox Series, Bluetooth LE | Three wireless Xbox controllers working together on 4.03 confirmed |
| 8BitDo Ultimate 2.4 GHz receiver, XInput mode | USB controls and Home confirmed on 4.03; tested receiver `2DC8:3106` |
| 8BitDo Ultimate C Bluetooth, USB | Input delivery confirmed on 4.03 |
| Ultimate 2C Bluetooth for Switch, direct Bluetooth | Sticks, face buttons and Home confirmed on 4.03; timing/reconnect investigation remains open |
| Official Switch Pro USB (`057E:2009`) | Implemented; stick stability needs more live validation |
| Wired-format Xbox 360 XInput devices | Decoder and host tests available; Xbox 360 wireless-receiver framing unsupported |
| USB Xbox on firmware 13.60 | Separate firmware build confirmed working by the user's tester |

Four-controller routing and mixed three-Xbox-plus-Switch Bluetooth sessions are
covered by host simulations. Four physical controllers of either transport,
mixed Xbox/Switch operation, and eight physical controllers together still
need live validation. Xbox BLE input requires the verified report-map prefix;
this is not a generic Bluetooth HID implementation. Rumble, motion controls,
adaptive triggers, touch gestures and headset audio are not supported features.

## Run on 4.03

1. Sign into a PS5 user, run your jailbreak setup, and start a compatible TCP
   ELF loader on port **9021**.
2. Load **`dist/ps5-xinput-manager.elf`** using your payload manager, or send it
   from a PC on the same network:

   ```sh
   python3 testing_tools/scripts/send_payload.py PS5_IP --elf dist/ps5-xinput-manager.elf
   ```

3. The launcher starts its embedded service and opens the console browser.
   You can also use `http://PS5_IP:9030` from a PC.
4. Plug in a supported USB controller, or put a wireless controller into
   pairing mode and select **Search Xbox** or **Search Switch / 8BitDo**.
5. Select a PS5 user for each virtual controller when prompted. Use separate
   users when testing local multiplayer.

Before replacing a running build, stop the old service cleanly:

```sh
python3 testing_tools/scripts/stop_payload.py PS5_IP --port 9024
```

The launcher reuses an existing service on port 9030; loading a newer ELF does
not replace that service. Run one controller service at a time. After rebooting,
run the jailbreak and payload again. See the [manager guide](docs/controller-manager.md)
for controller mapping, pairing, disconnect shortcuts and reconnect behavior.

## Build and test

Build on Linux or WSL using GNU Make, Python 3, a host C compiler and PS5 Payload
SDK **v0.44**. Install Clang/LLD/LLVM 18 and the setup helper's command-line
prerequisites first. The helper verifies the SDK archive's SHA-256.

```sh
bash scripts/setup_sdk.sh
make                  # firmware 4.03 combined manager and ZIP
make -C testing_tools host-test  # optional host tests; no PS5 connection
```

For an existing SDK, use `PS5_PAYLOAD_SDK=/path/to/sdk make`.
The embedded interface uses plain HTML/CSS/JavaScript: no npm, CDN or PC web
server is required.

`make` or `make manager` builds the one supported main entry point:
`dist/ps5-xinput-manager.elf`. The matching service is embedded and kept only
as an internal build artifact at `build/ps5-xinput-service.elf`.

`dist/` contains only the manager ELF, its checksum and its ZIP bundle. The ZIP
contains the same single ELF, documentation, credits and license notices.
Standalone payloads, the 13.60 variant, host tests, probes and development tools
are isolated under `testing_tools/` (see `testing_tools/README.md`), with their own
Makefile and output directories. They are not dependencies of the main build.

## Logs and state

| Location / port | Purpose |
| --- | --- |
| `/data/ps5-xinput/manager.log` | Combined service log on PS5; appended across runs |
| `/data/ps5-xinput/bonds/` | Private bonds and reconnect preferences |
| TCP 9030 | Manager web interface and API |
| UDP 9024 | Manager live logs and normal cleanup requests |
| TCP 9021 | External ELF loader |

Start a PC listener before loading the manager:

```sh
python3 testing_tools/scripts/watch_logs.py --ps5 PS5_IP --remote-port 9024 --port 19024 --output manager-test.log --deduplicate
```

Older standalone diagnostic targets can use UDP 9023. The live viewer receives
a short recent-log replay; the console file may contain earlier test sessions.

## Source layout

```text
src/app/           Main manager launcher, service and HTTP API
src/usb/           USB protocols and the combined service scheduler
src/bluetooth/     Shared radio, pairing, HID profiles, sessions and bonds
src/platform/      Firmware 4.03 virtual pads, user lookup and logging
manager/           Embedded web interface
mk/                Main manager build configuration and rules
scripts/           SDK setup, asset embedding and main release packaging
third_party/       Required runtime cryptography and original licenses
docs/              Main manager guide, architecture and dependencies
testing_tools/     Tests, probes, alternate builds, tools and historical material
```

- [Architecture and ownership](docs/architecture.md)
- [Libraries, tools and native PS5 interfaces](docs/dependencies.md)
- [Credits and provenance](CREDITS.md)
- [Controller Manager guide](docs/controller-manager.md)
- Testing tools, alternate firmware and investigation history: `testing_tools/README.md`

This project retains the upstream [GPL-3.0 license](LICENSE). Bundled libraries
retain their original licenses; see [third-party inventory](third_party/README.md).
