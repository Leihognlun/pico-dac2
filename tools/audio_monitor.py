"""Linux: read audio diagnostics over hidraw without third-party packages."""
import argparse
import os
from pathlib import Path
import select
import struct
import time


FIELDS = {
    0: ('underruns', 'dropped_frames', 'silence_blocks'),
    1: ('spdif_stalls', 'i2s_stalls', 'bad_packets'),
    2: ('sample_rate', 'rx_packets', 'rx_frames'),
    3: ('rx_queue_drops', 'led_mask', 'led_commands'),
}


def find_device():
    matches = []
    for entry in Path('/sys/class/hidraw').glob('hidraw*'):
        info = (entry / 'device/uevent').read_text()
        for line in info.splitlines():
            if line.startswith('HID_ID='):
                _, vid, pid = line.split('=', 1)[1].split(':')
                if (int(vid, 16), int(pid, 16)) == (0xcafe, 0xbabe):
                    matches.append('/dev/' + entry.name)
    if len(matches) != 1:
        raise SystemExit('Use --device /dev/hidrawN; matching devices: ' + str(matches))
    return matches[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', help='Explicit hidraw device (also for custom VID/PID)')
    parser.add_argument('--seconds', type=float, default=60)
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error('--seconds must be positive')
    device = args.device or find_device()
    fd = os.open(device, os.O_RDONLY | os.O_NONBLOCK)
    start = time.monotonic()
    previous = {}
    reports = 0
    try:
        print('Reading', device, '- counters are cumulative; + values are changes.', flush=True)
        while True:
            remaining = args.seconds - (time.monotonic() - start)
            if remaining <= 0:
                break
            ready, _, _ = select.select([fd], [], [], min(1, remaining))
            if not ready:
                continue
            report = os.read(fd, 64)
            if len(report) == 17 and report[0] == 1:
                report = report[1:]
            if len(report) != 16 or report[:3] != b'AD\x01' or report[3] not in FIELDS:
                continue
            page = report[3]
            values = struct.unpack_from('<III', report, 4)
            output = []
            for name, value in zip(FIELDS[page], values):
                delta = ''
                if name in previous and name not in ('sample_rate', 'led_mask'):
                    delta = f' (+{(value - previous[name]) & 0xffffffff})'
                previous[name] = value
                output.append(f'{name}={value}{delta}')
            print(f'{time.monotonic() - start:7.2f}s ' + ' '.join(output), flush=True)
            reports += 1
    finally:
        os.close(fd)
    if not reports:
        raise SystemExit('No diagnostic reports. Check device and flash the diagnostic firmware.')


if __name__ == '__main__':
    main()
