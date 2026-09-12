"""Check the mirror PIO's timing and I2S bit/word alignment from its source."""
import re
from pathlib import Path


def main():
    source = Path(__file__).resolve().parents[1] / "i2s_mirror.pio"
    labels, instructions = {}, []
    for line in source.read_text().splitlines():
        line = line.split(";")[0].strip()
        if not line or line.startswith("."):
            continue
        if line.endswith(":"):
            labels[line[:-1]] = len(instructions)
        else:
            match = re.fullmatch(r"(.+?)\s+side\s+(0b[01]+)\s+\[(\d+)\]", line)
            assert match, line
            instructions.append((match[1].strip(), int(match[2], 2), int(match[3]) + 1))
    samples = [0x80000100, 0x7fffff00, 0x12345600, 0xfedcba00]
    pc = x = cycle = bit_index = data = previous_clock = 0
    edges, frame_starts, lr_edges = [], [], []
    previous_lr = 0
    while cycle <= 512:
        if pc == 0:
            frame_starts.append(cycle)
        op, side, duration = instructions[pc]
        next_pc = (pc + 1) % len(instructions)
        if op.startswith("out"):
            data = (samples[bit_index // 32] >> (31 - bit_index % 32)) & 1
            bit_index += 1
        elif op.startswith("set"):
            x = int(op.split(",")[1])
        elif op.startswith("jmp"):
            if x:
                next_pc = labels[op.split(",")[1].strip()]
            x = (x - 1) & 0xffffffff
        else:
            assert op == "nop"
        clock, lr = side & 1, side >> 1
        if clock and not previous_clock and cycle != 0:
            edges.append((cycle, data))
        if lr != previous_lr:
            lr_edges.append(cycle)
        previous_clock, previous_lr = clock, lr
        cycle += duration
        pc = next_pc
    assert frame_starts == [0, 256, 512]
    assert len(edges) == 128
    assert all(edges[i][0] - edges[i - 1][0] == 4 for i in range(1, len(edges)))
    decoded = []
    for word in range(4):
        value = 0
        for _, bit in edges[word * 32:(word + 1) * 32]:
            value = (value << 1) | bit
        decoded.append(value)
    assert decoded == samples
    # LRCLK changes on the falling edge preceding the previous word's LSB.
    assert lr_edges == [126, 254, 382, 510]
    print("PASS: I2S PIO 256 cycles/frame, 32-bit stereo, MSB first and one-bit LRCLK delay")


if __name__ == "__main__":
    main()
