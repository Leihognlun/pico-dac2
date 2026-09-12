"""Run host tests with GCC, without a Pico SDK or attached hardware."""
import argparse
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = root / "build" / "host-tests"
    build.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(root / "tests/i2s_pio_test.py")], check=True)
    common = [args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(root)]
    cases = [
        ("usb_buffer_control", ["-I", str(root / "tests/stubs")],
         ["tests/usb_buffer_control_test.c"]),
        ("dual_output", [], ["spdif_encode.c", "tests/dual_output_test.c"]),
        ("spdif_encode", [], ["spdif_encode.c", "tests/spdif_encode_test.c"]),
        ("descriptors_spdif", ["-DPICODAC_OUTPUT_SPDIF=1", "-DHID_ENABLE=1"],
         ["tests/usb_descriptors_test.c"]),
        ("descriptors_i2s", ["-DPICODAC_OUTPUT_SPDIF=0", "-DHID_ENABLE=1"],
         ["tests/usb_descriptors_test.c"]),
        ("passthrough", ["-I", str(root / "tests/stubs"),
                         "-DPICODAC_OUTPUT_SPDIF=1", "-DPICODAC_SPDIF_PIN=22",
                         "-DPICODAC_I2S_DATA_PIN=18", "-DPICODAC_I2S_BASE_CLOCK_PIN=16",
                         "-DLOG_LEVEL=0"],
         ["tests/usb_audio_passthrough_test.c", "usb_audio.c", "audio_device.c",
          "ringbuffer.c", "spdif_encode.c"]),
    ]
    for name, flags, sources in cases:
        binary = build / (name + (".exe" if os.name == "nt" else ""))
        subprocess.run(common + flags + sources + ["-o", str(binary)], cwd=root, check=True)
        subprocess.run([str(binary)], cwd=root, check=True)


if __name__ == "__main__":
    main()
