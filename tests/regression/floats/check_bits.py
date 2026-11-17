from pathlib import Path
import bisect
import math
import struct
import sys

base = Path(sys.argv[1])
failures = []

def decode_small(bits, e5):
    sign = -1 if bits & 128 else 1
    mb, bias = (2,15) if e5 else (3,7)
    exp, mant = (bits & 127) >> mb, bits & ((1 << mb)-1)
    if exp == 0:
        return sign * math.ldexp(mant / (1 << mb), 1-bias)
    if e5 and exp == 31:
        return math.nan if mant else sign * math.inf
    if not e5 and exp == 15 and mant == 7:
        return math.nan
    return sign * math.ldexp(1 + mant / (1 << mb), exp-bias)

def float_bits(x):
    return struct.unpack('<I', struct.pack('<f',x))[0]

decoded = struct.unpack('<65536I', (base/'half_decode.bin').read_bytes())
for bits, actual in enumerate(decoded):
    value = struct.unpack('<e', struct.pack('<H',bits))[0]
    expected = float_bits(value)
    if math.isnan(value):
        if (actual & 0x7f800000) != 0x7f800000 or not (actual & 0x7fffff):
            failures.append(('half_decode_nan', bits, actual))
    elif actual != expected:
        failures.append(('half_decode',bits,actual,expected))

actual_small = struct.unpack('<512I', (base/'fp8_decode.bin').read_bytes())
for e5 in (False, True):
    for bits in range(256):
        value = decode_small(bits,e5)
        actual = actual_small[bits + (256 if e5 else 0)]
        if math.isnan(value):
            if (actual & 0x7f800000) != 0x7f800000 or not (actual & 0x7fffff):
                failures.append(('fp8_decode_nan',e5,bits,actual))
        elif actual != float_bits(value):
            failures.append(('fp8_decode',e5,bits,actual,float_bits(value)))

encoded = struct.unpack('<65536H',(base/'half_encode.bin').read_bytes())
for i,actual in enumerate(encoded):
    raw = i*65537
    value = struct.unpack('<f',struct.pack('<I',raw))[0]
    try:
        expected = struct.unpack('<H',struct.pack('<e',value))[0]
    except OverflowError:
        expected = (0x8000 if raw >> 31 else 0) | 0x7c00
    if math.isnan(value):
        if (actual & 0x7c00) != 0x7c00 or not(actual & 1023):
            failures.append(('half_encode_nan',raw,actual))
    elif actual != expected:
        failures.append(('half_encode',raw,actual,expected))

for e5,name,max_bits in ((False,'e4_encode.bin',126),(True,'e5_encode.bin',123)):
    actual_values=(base/name).read_bytes()
    values=[decode_small(b,e5) for b in range(max_bits+1)]
    for i,actual in enumerate(actual_values):
        raw=i*65537
        value=struct.unpack('<f',struct.pack('<I',raw))[0]
        sign=(raw >> 24) & 128
        if math.isnan(value):
            expected=sign | 127
        else:
            magnitude=abs(value)
            hi=bisect.bisect_left(values,magnitude)
            if hi == 0: nearest=0
            elif hi >= len(values): nearest=max_bits
            else:
                lo=hi-1
                left,right=magnitude-values[lo],values[hi]-magnitude
                nearest=lo if left<right or (left==right and lo%2==0) else hi
            expected=sign | nearest
        if actual != expected:
            failures.append(('fp8_encode',e5,raw,actual,expected))
print('FLOAT_BITS_DIFFERENTIAL checks=262656 failures='+str(len(failures)))
print(failures[:12])
sys.exit(bool(failures))
