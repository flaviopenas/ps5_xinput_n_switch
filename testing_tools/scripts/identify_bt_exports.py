"""Match captured HID export NIDs to candidate names; keep unknowns explicit."""
import argparse
from pathlib import Path
import re
from identify_bt_imports import nid

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    names = set()
    for path in Path("src").rglob("*.c"):
        names.update(re.findall(r"\bsceBluetooth\w+", path.read_text()))
    verbs = ["Get", "Set", "Register", "Unregister", "Start", "Stop", "Connect", "Disconnect", "Read", "Write"]
    objects = ["Device", "DeviceInfo", "DeviceList", "Callback", "InputReport", "OutputReport", "FeatureReport",
               "ReportDescriptor", "Version", "ConnectionStatus", "Status", "Event", "Report", "Info", "DeviceType",
               "ReportDescriptorSize", "ReportDescriptorLength", "InputReportList", "Reports", "InputReports",
               "OutputReports", "Idle", "Protocol", "ProtocolMode", "LibraryVersion", "DeviceState", "ReportSize"]
    for verb in verbs:
        for obj in objects:
            for suffix in ["", "2", "Ex", "Internal", "Async"]:
                names.add("sceBluetoothHid" + verb + obj + suffix)
    for name in ["Init", "Initialize", "Term", "Finalize", "ParamInitialize", "ParamInitialize2", "GetSdkVersion",
                 "GetVersion", "GetLibVersion", "GetLibraryVersion", "ParameterInitialize", "CallbackParamInitialize",
                 "ParamSet", "Read", "Write"]:
        names.add("sceBluetoothHid" + name)
    matches = {nid(name): name for name in names}
    for encoded, value, size in re.findall(r"BT export: ([^#\s]+)#[^ ]+ value (0x\w+) size (\d+)", args.log.read_text()):
        print(f"{value:>8} {size:>4} bytes: {matches.get(encoded, 'unresolved')} ({encoded})")

if __name__ == "__main__":
    main()
