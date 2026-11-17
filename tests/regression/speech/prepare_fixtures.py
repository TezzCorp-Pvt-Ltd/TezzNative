"""Generate small PCM and malformed RIFF fixtures without using audio devices."""
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/speech-regression/fixtures"
OUT.mkdir(parents=True, exist_ok=True)


def chunk(kind, data):
    return kind + struct.pack("<I", len(data)) + data + (b"\0" if len(data) & 1 else b"")


def fmt(rate=16000, channels=1, bits=16, tag=1, align=None, byte_rate=None):
    block = channels * (bits // 8) if align is None else align
    speed = rate * block if byte_rate is None else byte_rate
    return struct.pack("<HHIIHH", tag, channels, rate, speed, block, bits)


def riff(*chunks):
    body = b"WAVE" + b"".join(chunks)
    return b"RIFF" + struct.pack("<I", len(body)) + body


mono = struct.pack("<hhh", -32768, 0, 32767)
stereo = struct.pack("<hhhhhh", -32768, 32766, 1000, -1000, 32767, 32767)
fixtures = {
    "mono8k.wav": riff(chunk(b"fmt ", fmt(8000)), chunk(b"data", mono)),
    "stereo.wav": riff(chunk(b"fmt ", fmt(channels=2)), chunk(b"data", stereo)),
    "pcm8.wav": riff(chunk(b"fmt ", fmt(bits=8)), chunk(b"data", bytes([0, 128, 255]))),
    "extra_chunks.wav": riff(chunk(b"JUNK", b"abc"), chunk(b"fmt ", fmt() + b"\0\0"), chunk(b"LIST", b"test"), chunk(b"data", mono)),
    "data_first.wav": riff(chunk(b"data", mono), chunk(b"fmt ", fmt())),
    "bad_magic.wav": b"NOPE" + riff(chunk(b"fmt ", fmt()), chunk(b"data", mono))[4:],
    "zero_channels.wav": riff(chunk(b"fmt ", fmt(channels=0)), chunk(b"data", mono)),
    "zero_rate.wav": riff(chunk(b"fmt ", fmt(rate=0)), chunk(b"data", mono)),
    "float_format.wav": riff(chunk(b"fmt ", fmt(tag=3)), chunk(b"data", mono)),
    "unsupported_bits.wav": riff(chunk(b"fmt ", fmt(bits=24)), chunk(b"data", mono)),
    "bad_align.wav": riff(chunk(b"fmt ", fmt(align=1)), chunk(b"data", mono)),
    "bad_byte_rate.wav": riff(chunk(b"fmt ", fmt(byte_rate=1)), chunk(b"data", mono)),
    "partial_frame.wav": riff(chunk(b"fmt ", fmt()), chunk(b"data", b"x")),
    "truncated_header.wav": b"RIFF\0\0\0\0WA",
    "truncated_data.wav": riff(chunk(b"fmt ", fmt()), chunk(b"data", mono))[:-2],
    "duplicate_fmt.wav": riff(chunk(b"fmt ", fmt()), chunk(b"fmt ", fmt()), chunk(b"data", mono)),
    "empty_data.wav": riff(chunk(b"fmt ", fmt()), chunk(b"data", b"")),
    "too_long.wav": riff(chunk(b"fmt ", fmt(8000)), chunk(b"data", b"\0" * (8000 * 31 * 2))),
    "invalid_model.tnw": b"not a native trained checkpoint\n",
}
for name, data in fixtures.items():
    (OUT / name).write_bytes(data)
print(f"Prepared {len(fixtures)} speech fixtures in {OUT}")
