"""Test host LED command encoding and write failures without USB hardware."""
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.dont_write_bytecode = True
from tools.comm import led_report, set_leds, select_device, verify_leds
import struct


class MediaControlsTest(unittest.TestCase):
    def test_firmware_confirmation(self):
        class Device:
            def get_feature_report(self, report_id, size):
                assert (report_id, size) == (4, 6)
                return b'\x04\x05' + struct.pack('<I', 3)
        self.assertEqual(verify_leds(Device(), (1, 0, 1), 2), 3)
        with self.assertRaises(OSError):
            verify_leds(Device(), (0, 0, 0), 2)
        # Same mask and unchanged counter must never report success.
        with self.assertRaises(OSError):
            verify_leds(Device(), (1, 0, 1), 3)
    def test_linux_missing_usage(self):
        info = {"path": b'/dev/hidraw0', "interface_number": 2,
                "usage_page": 0, "usage": 0}
        self.assertEqual(select_device([info], 'linux'), info)
        with self.assertRaises(ValueError):
            select_device([info], 'win32')

    def test_collections_and_multiple_devices(self):
        vendor = {"path": b'vendor', "interface_number": 2,
                  "usage_page": 0xFF00, "usage": 1}
        media = {"path": b'media', "interface_number": 2,
                 "usage_page": 0x0C, "usage": 1}
        self.assertEqual(select_device([vendor, media], 'win32'), vendor)
        # Linux collection entries can refer to a single hidraw node.
        media['path'] = vendor['path']
        self.assertEqual(select_device([vendor, media], 'linux')['path'], b'vendor')
        for devices in ([], [vendor, dict(vendor, path=b'second')],
                        [{'path': b'audio', 'interface_number': 1}]):
            with self.assertRaises(ValueError):
                select_device(devices, 'linux')

    def test_all_led_combinations(self):
        for mask in range(8):
            states = [(mask >> bit) & 1 for bit in range(3)]
            self.assertEqual(led_report(states), bytes((3, mask)))

    def test_invalid_states(self):
        for states in [(), (1, 0), (0, 0, 0, 0), (1, 2, 0), (-1, 0, 0)]:
            with self.assertRaises(ValueError):
                led_report(states)

    def test_write(self):
        class Device:
            def send_feature_report(self, report):
                self.report = report
                return 6
        device = Device()
        set_leds(device, (1, 0, 1))
        self.assertEqual(device.report, b'\x04\x05\x00\x00\x00\x00')
        for result in (-1, 0, 1, 5):
            device.send_feature_report = lambda report: result
            with self.assertRaises(OSError):
                set_leds(device, (0, 0, 0))


if __name__ == '__main__':
    unittest.main()
