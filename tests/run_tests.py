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
    subprocess.run([sys.executable, str(root / "tests/media_controls_test.py")], check=True)
    common = [args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(root)]
    cases = [
        ("sd_diagnostics", ["-I", str(root / "tests/sd_stubs"), "-DPICODAC_SD_DIAGNOSTICS=1"],
         ["tests/sd_diagnostics_test.c", "sd_diagnostics.c"]),
        ("eac3_burst", [], ["tests/eac3_burst_test.c", "eac3_burst.c", "spdif_encode.c"]),
        ("usb_control_state", [], ["tests/usb_control_state_test.c"]),
        ("usb_audio_packet", [], ["tests/usb_audio_packet_test.c"]),
        ("diagnostics_spdif", ["-I", str(root / "tests/stubs"), "-DHID_ENABLE=1", "-DPICODAC_OUTPUT_SPDIF=1", "-DLOG_LEVEL=0"],
         ["tests/audio_diagnostics_test.c", "usb_hid.c"]),
        ("diagnostics_i2s", ["-I", str(root / "tests/stubs"), "-DHID_ENABLE=1", "-DPICODAC_OUTPUT_SPDIF=0", "-DLOG_LEVEL=0"],
         ["tests/audio_diagnostics_test.c", "usb_hid.c"]),
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

    binary = build / ("sd_player.exe" if os.name == "nt" else "sd_player")
    flags = ["-I", str(root / "tests/sd_stubs"), "-DPICODAC_SPDIF_PIN=22",
             "-DPICODAC_SD_SCK_PIN=2", "-DPICODAC_SD_MOSI_PIN=3",
             "-DPICODAC_SD_MISO_PIN=4", "-DPICODAC_SD_CS_PIN=5",
             "-DPICODAC_SD_SPI_HZ=12000000", '-DPICODAC_SD_FILE="0:/TRACK.EC3"']
    if os.name != "nt":
        flags += ["-pthread", "-D_POSIX_C_SOURCE=200809L"]
    subprocess.run(common + flags + ["tests/sd_player_test.c", "sd_eac3_player.c",
                                    "eac3_burst.c", "-o", str(binary)], cwd=root, check=True)
    for scenario in range(6):
        subprocess.run([str(binary), str(scenario)], cwd=root, check=True, timeout=15)


if __name__ == "__main__":
    main()
