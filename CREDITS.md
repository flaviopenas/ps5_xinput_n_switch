# Credits and provenance

This project extends existing PS5 homebrew work. The original authors retain
credit for their code and research. These acknowledgements do not imply that
the listed developers contributed to, tested or endorse this fork.

## Foundation

**FGG STORE / Fathi Ghanem and the FGG-XSense contributors** created
[FGG-XSense](https://github.com/FGGstore/FGG-XSense), the original Xbox USB GIP
bridge and virtual PlayStation controller implementation from which this
project is derived. The preserved reference is commit
`bfc5f9d324e2ca8b0a90fc498957699caa9fe5b0`. Its GPL-3.0 license is retained
in [LICENSE](LICENSE). The optional original checkout is `testing_tools/upstream/FGG-XSense/`.

**FGG STORE and the FGG-PlayPods contributors** provided the shared USB HCI
endpoint approach investigated for the console's Bluetooth radio:
[FGG-PlayPods](https://github.com/FGGstore/FGG-PlayPods), reference commit
`4b4ddb50e68a70af1cb914cff28bc4807a74e4bd`. The acknowledgement remains in
`src/bluetooth/bt_hci.c`.

**The ps5-payload-dev SDK developers and contributors** provide the
[PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk): compiler wrappers,
runtime, dynamic linking and platform interfaces. This project pins **v0.44**.
FGG-XSense also credits their `ftpsrv` work; this manager does not bundle an FTP
server.

## Included libraries and test material

| Authors / project | Contribution | Preserved license |
| --- | --- | --- |
| Kenneth MacKay and [micro-ecc](https://github.com/kmackay/micro-ecc) contributors | P-256 for Bluetooth Secure Connections | [BSD-2-Clause](third_party/micro-ecc/LICENSE.txt) |
| kokke and [tiny-AES-c](https://github.com/kokke/tiny-AES-c) contributors | AES-128 for Bluetooth cryptographic helpers | [Unlicense](third_party/tiny-aes/unlicense.txt) |
| Nordic Semiconductor ASA and [Zephyr](https://github.com/zephyrproject-rtos/zephyr) contributors | Adapted Bluetooth crypto test vectors; test-only | [Apache-2.0](https://github.com/zephyrproject-rtos/zephyr/blob/main/LICENSE) |

Exact revisions and build settings are recorded in
[third_party/README.md](third_party/README.md). Original vendored code and
license headers are preserved.

## Protocol research and implementation references

These references informed protocol handling and investigation. They are not
all linked libraries, and a protocol reference does not mean its source was
copied into the production payload.

| Developers / project | Use in this project |
| --- | --- |
| Linux [xpad contributors](https://github.com/torvalds/linux/blob/master/drivers/input/joystick/xpad.c) | Xbox USB initialization/protocol research; also credited by FGG-XSense |
| Linux [hid-nintendo contributors](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-nintendo.c) | Switch Pro handshake, reports and calibration |
| dekuNukem and [Nintendo Switch Reverse Engineering](https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering) contributors | Nintendo controller protocol notes |
| Ricardo Quesada and [Bluepad32 contributors](https://github.com/ricardoquesada/bluepad32) | Switch Bluetooth and simple `0x3F` report layout reference |
| [Chromium contributors](https://github.com/chromium/chromium/blob/main/device/gamepad/nintendo_controller.cc) | Additional Switch USB reference |
| [BlueZ contributors](https://github.com/bluez/bluez) | SMP and f4/f5/f6 cross-checks; no BlueZ library linked |
| [Zephyr contributors](https://github.com/zephyrproject-rtos/zephyr/tree/main/include/zephyr/bluetooth) | HCI definitions and protocol cross-checks |
| [FreeBSD contributors](https://github.com/freebsd/freebsd-src/blob/releng/11.0/sys/dev/usb/usb_generic.c) | USB filesystem transfers and shared-endpoint investigation |
| diegobarbosaa and [OmniPad-PS5 contributors](https://github.com/diegobarbosaa/OmniPad-PS5) | Later-firmware virtual-pad comparison and shell-context investigation; experimental shell binding remains disabled by default |
| DNNDHH and [PS5-3.20_Libs contributors](https://github.com/DNNDHH/PS5-3.20_Libs) | Bluetooth HID export-name candidates for diagnostics |
| StonedModder and contributors to [NRF52840-dualsense](https://github.com/StonedModder/NRF52840-dualsense) and [dualsense-controller](https://github.com/StonedModder/dualsense-controller) | DualSense HID/report references and visualization; not imported runtime libraries |
| cragson and [ps5-hen contributors](https://github.com/cragson/ps5-hen) | Pinned GPU-helper reference for a parked diagnostic experiment, revision `699870d16ca4cd5cd70fc88f8cf61b3953d4a292`; not linked into the manager |

## Work in this project

Local changes include the firmware 4.03 virtual-pad layout, multi-controller
ownership and routing, USB hotplug and receiver handling, Xbox BLE pairing,
the Controller Manager interface, Ultimate 2C Switch Bluetooth support,
firmware build variants, timing diagnostics and host integration tests.

Thanks to the project owner for 4.03 hardware testing and Raven for the
13.60 module and successful USB Xbox test. Public attribution names were not
supplied, so personal names are omitted here.
