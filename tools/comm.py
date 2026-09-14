# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "hidapi",
# ]
# ///
"""Control LEDs: python tools/comm.py --leds 1 0 1 (GPIO 9, 12, 26)."""
import argparse
import os
import sys
import struct

VID = 0xCAFE
PID = 0xBABE


LED_PINS = (9, 12, 26)


def select_device(devices, platform=None):
    """Select our HID interface without relying on Linux usage metadata."""
    platform = sys.platform if platform is None else platform
    candidates = []
    for info in devices:
        vendor = info.get("usage_page") == 0xFF00 and info.get("usage") == 1
        # Linux exposes the entire HID interface, unlike Windows collections.
        linux_interface = platform.startswith("linux") and info.get("interface_number") == 2
        if (vendor or linux_interface) and info.get("path"):
            candidates.append(info)
    # hidraw may enumerate several collections sharing the same device path.
    unique = {info["path"]: info for info in candidates}
    if len(unique) != 1:
        raise ValueError(f"Expected one control HID device; found {len(unique)}. "
                         "Run --list to inspect devices; use --path for an explicit selection.")
    return next(iter(unique.values()))


def print_devices(devices):
    if not devices:
        print("No matching VID/PID devices enumerated. Check USB connection, "
              "firmware and --vid/--pid.")
    for info in devices:
        print(" ".join(f"{key}={info.get(key)!r}" for key in
                      ("path", "vendor_id", "product_id", "interface_number",
                       "usage_page", "usage", "release_number")))

def led_report(states):
    states = tuple(states)
    if len(states) != 3 or any(value not in (0, 1) for value in states):
        raise ValueError("Expected three LED states, each 0 or 1")
    return bytes((3, sum(int(value) << i for i, value in enumerate(states))))


def set_leds(device, states, transport="feature"):
    """Write all three states to an opened hidapi device; 1 = on."""
    report = led_report(states)
    if transport == "feature":
        report = bytes((4, report[1], 0, 0, 0, 0))
        written = device.send_feature_report(report)
    elif transport == "interrupt":
        written = device.write(report)
    else:
        raise ValueError("Unknown LED transport")
    if written != len(report):
        error = getattr(device, "error", None)
        detail = error() if callable(error) else "backend supplied no error detail"
        raise OSError(f"Incomplete HID {transport} write: {written}/{len(report)} bytes; {detail}")


def read_led_state(device):
    report = bytes(device.get_feature_report(4, 6))
    if len(report) != 6 or report[0] != 4 or report[1] & 0xF8:
        raise OSError(f"Invalid LED feature report: {report.hex()}")
    return report[1], struct.unpack_from('<I', report, 2)[0]


def verify_leds(device, states, previous_commands):
    """Require a new accepted command, not just a matching pre-existing mask."""
    expected = led_report(states)[1]
    mask, commands = read_led_state(device)
    if mask != expected or commands != ((previous_commands + 1) & 0xFFFFFFFF):
        raise OSError(f"LED command not confirmed: wanted mask={expected:#x}, "
                      f"got mask={mask:#x}, commands={previous_commands}->{commands}")
    return commands


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--leds", nargs=3, type=int, choices=(0, 1),
                        metavar=("GPIO9", "GPIO12", "GPIO26"))
    parser.add_argument("--vid", type=lambda s: int(s, 0), default=VID)
    parser.add_argument("--pid", type=lambda s: int(s, 0), default=PID)
    parser.add_argument("--path", help="Explicit hidapi path for multiple devices")
    parser.add_argument("--list", action="store_true", help="List matching HID devices without writing")
    parser.add_argument("--verify", action="store_true", help="Read back applied LED state from firmware")
    parser.add_argument("--transport", choices=("feature", "interrupt"), default="feature",
                        help="Default: EP0 feature reports; interrupt is for diagnosis")
    args = parser.parse_args()
    if args.leds is None and not args.list:
        parser.error("--leds is required unless using --list")
    import hid

    devices = hid.enumerate(args.vid, args.pid)
    if args.list:
        print_devices(devices)
        return
    if args.path:
        path = os.fsencode(args.path)
        selected = next((d for d in devices if d.get("path") == path), {})
    else:
        try:
            selected = select_device(devices)
        except ValueError as error:
            print_devices(devices)
            parser.error(str(error))
        path = selected["path"]
    release = selected.get("release_number")
    if release and release < 0x0107:
        parser.error(f"Device firmware is 0x{release:04x}; LED control requires "
                     "firmware 1.07 or later. Flash the new UF2 and reconnect USB.")
    device = hid.device()
    try:
        device.open_path(path)
        # EP0 readback is synchronous and cannot return queued old IN reports.
        _, before = read_led_state(device)
        set_leds(device, args.leds, args.transport)
        commands = verify_leds(device, args.leds, before)
        print(f"Firmware LED state confirmed ({args.transport}); accepted commands={before}->{commands}")
        print("LED command sent: " + ", ".join(
            f"GPIO{pin}={'ON' if state else 'OFF'}"
            for pin, state in zip(LED_PINS, args.leds)))
    finally:
        device.close()


if __name__ == "__main__":
    main()
