# Testing tools and preserved experiments

Everything outside the primary firmware 4.03 manager lives here. This directory
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
The canonical firmware 4.03 artifact remains the parent project's
dist/ps5-xinput-manager.elf. Do not confuse archived outputs with newly built ones.

Send the main payload from the project root with:

```sh
python3 testing_tools/scripts/send_payload.py PS5_IP --elf dist/ps5-xinput-manager.elf
python3 testing_tools/scripts/watch_logs.py --ps5 PS5_IP --remote-port 9024 --port 19024
python3 testing_tools/scripts/stop_payload.py PS5_IP --port 9024
```

The files in artifacts and local reference checkouts are retained working
material, not required release inputs. Source and runtime library attribution
remain in the project's CREDITS.md and third_party license notices.
