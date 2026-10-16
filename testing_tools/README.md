# Testing tools and preserved experiments

Everything outside the primary runtime-selecting manager lives here. This directory
is optional for building/running the main payload. Development commands below
assume a complete source checkout, not the binary release ZIP.

From either the project root or this directory:

```sh
make -C testing_tools host-test
make -C testing_tools manager-1360
make -C testing_tools release debug bt-bridge bt-pair
make -C testing_tools format-check
```

When already inside testing_tools, omit `-C testing_tools`.
Builds and tests execute from the project root so shared production sources
can be exercised directly. All development outputs go to testing_tools/build
and testing_tools/dist. The main Makefile never includes these build rules.

| Directory | Contents |
| --- | --- |
| tests/ | Host tests, platform fakes and controller fixtures |
| src/diagnostics/ | Native probes, ABI capture and disabled shell experiments |
| src/app/, src/platform/ | Preserved 13.60/diagnostic manager and virtual-pad implementations |
| src/usb/, src/bluetooth/ | Preserved standalone entry points and experimental transport variants |
| scripts/ | Payload sender, cleanup/log viewers, timing and offline inspection tools |
| mk/ | Explicit development, legacy and alternate-firmware build targets |
| docs/ | Timing investigations, firmware notes and historical documentation |
| licenses/ | Test-only notices, including Zephyr's vector license |
| upstream/, .tools/ | Local reference checkouts and optional development tools |
| artifacts/ | Previous builds, logs, snapshots and supplied kernel material |

The old working outputs are retained in artifacts/previous-dist. New alternate
firmware builds use dist/ps5-xinput-manager-1360.elf inside this directory.
The canonical runtime-selecting artifact is the parent project's
dist/ps5-xinput-manager.elf. It uses the provisional <=4.03 / >4.03 ABI threshold.
Do not confuse archived outputs with newly built ones.

The 13.60 manager uses the current shared Bluetooth session implementation,
including saved-Switch request classification, HID connection-request retries,
checked encryption readback and receive diagnostics. These changes build and
pass host tests; 13.60 console behavior still needs validation. Ultimate 2C
normal power-on reconnect remains inconsistent in 4.03 testing.

Send the main payload from the project root with:

```sh
python3 testing_tools/scripts/send_payload.py PS5_IP --elf dist/ps5-xinput-manager.elf
python3 testing_tools/scripts/watch_logs.py --ps5 PS5_IP --remote-port 9024 --port 19024
python3 testing_tools/scripts/stop_payload.py PS5_IP --port 9024
```

The files in artifacts and local reference checkouts are retained working
material, not required release inputs. Source and runtime library attribution
remain in the project's CREDITS.md and third_party license notices.

## Separate USB and Bluetooth services

Build from the project root:

```sh
make -f testing_tools/Makefile bluetooth-only usb-only
```

Stop the combined manager before loading these two payloads. Send
`testing_tools/dist/ps5-xinput-usb-only.elf` and then
`testing_tools/dist/ps5-xinput-bluetooth-only.elf` through the ELF loader.
Both stay running concurrently. The Bluetooth service retains the manager UI on
TCP 9030, saved bonds, and reconnect preferences, but its USB list is empty.
It does not load the USB controller service. The USB service handles hotplug
independently and does not load the Bluetooth transport.

| Service | UDP log/stop port | Console log |
| --- | --- | --- |
| Bluetooth only | 9024 | `/data/ps5-xinput/manager.log` |
| USB only | 9023 | `/data/ps5-xinput/usb-only.log` |

Use `watch_logs.py` with separate local ports for simultaneous captures. Stop
each service separately with `stop_payload.py --port 9024` or `--port 9023`.
Closing or stopping Bluetooth does not stop the USB process. Both use the same
runtime firmware selection as the main payload; concurrent physical operation
and firmware compatibility need console testing.
