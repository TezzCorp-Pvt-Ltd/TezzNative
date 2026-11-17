# Arduino UNO R3 emulator profile

`arduino-uno` builds AVR machine code for an ATmega328P at 16 MHz, the MCU
used by Arduino UNO R3. The current verification uses simavr's CPU and
peripheral models. Physical boards have not been tested or flashed.

The supported target module is `arduino.uno`. The older `arduino` module
remains unavailable because its broader proposed API has no complete runtime.
No Arduino CLI or Arduino C++ core is required: the profile links its AVR
runtime and AVR-LibC, then emits ELF and Intel HEX artifacts.

```tn
import "arduino.uno"

fn main() -> int:
  arduino.uno.pin_mode(arduino.uno.LED_BUILTIN(), arduino.uno.OUTPUT())
  arduino.uno.digital_write(arduino.uno.LED_BUILTIN(), arduino.uno.HIGH())
  arduino.uno.delay(10)
  arduino.uno.digital_toggle(arduino.uno.LED_BUILTIN())
  ret 0
```

Build from a checkout with the target toolchain available:

```bash
python3 tools/tezz_hw.py build tests/hardware/uno_gpio.tn \
  --target arduino-uno --compiler /path/to/source-built/tezzc \
  --out build/hardware/uno-gpio
```

The public constants use accessor functions, like `OUTPUT()` and `A0()`.
This avoids mutable global storage on a device with 2 KB of SRAM.

| API | Contract |
| --- | --- |
| `pin_mode`, `digital_write`, `digital_read`, `digital_toggle` | UNO pin mapping D0..D19; modes 0/input, 1/output, 2/pullup; values 0/low and 1/high. UART0 uses pins D0/D1. |
| `port_write`, `port_read` | Ports `PORT_B()`, `PORT_C()`, `PORT_D()`; masked writes preserve other latch bits. B/C expose six UNO GPIO bits; D exposes eight. |
| `analog_read` | ADC channels 0..5 or A0()..A5(); returns 0..1023. The emulator gate supplies 0, 2500 and 5000 mV with a 5000 mV reference. |
| `analog_reference` | `DEFAULT()`/AVcc=1, `EXTERNAL()`/AREF=0, `INTERNAL()`/1.1 V=3. |
| `eeprom_read`, `eeprom_write` | Addresses 0..1023; byte values 0..255; update waits for the hardware write to finish. |
| `micros`, `millis`, `delay_us`, `delay` | Timer0 uses a /64 prescaler; micros has 4-us resolution. Busy delays may be extended by interrupts. Clock accounting requires Timer0 overflow interrupts; masking them beyond one 1024-us overflow loses elapsed overflows. |
| `uart_begin`, `uart_write_byte`, `uart_available`, `uart_read_byte`, `uart_flush` | UART0, 8N1, default 115200 baud. Byte writes return 1; empty/nonvalid reads return -1; flush also works before the first write. Reconfiguration drains pending output. |
| `interrupts_enable`, `interrupts_disable`, `interrupts_enabled` | Enable/disable return the previous global I-bit; the state accessor returns its current value. |

Invalid GPIO, ADC, EEPROM, baud, byte, port or delay arguments return -1.
The profile reserves Timer0 for timekeeping. PWM, SPI, I2C, servo, tone and
callback-interrupt APIs are not provided by this module.

The MCU profile retains Tezz's 64-bit `int` and eight-byte pointer storage in
its generated IR memory. HAL entrypoints receive int64 value slots; AVR
pointers are converted explicitly only at the hardware boundary. This is not
an ABI promise for passing Tezz structs to arbitrary AVR C functions.
Runtime floating-point operations/storage are rejected because this toolchain
uses 32-bit `double`; there is no silent downgrade of Tezz `float`.

```bash
python3 tests/hardware/check_uno.py --compiler /path/to/source-built/tezzc \
  --out build/verification/uno-hardware
```

The gate compiles actual TN firmware and checks GPIO register state and timed
PB5 edges, external digital input, ADC voltages, EEPROM boundary writes,
UART transmission/reception/divisor, interrupt state, timers and 64-bit values.
Its independent observer also checks SRAM stack separation and a bounded
simulation cycle budget. Reports include firmware/source hashes and explicitly
record `physical_hardware: false`. The builder's 512-byte static stack reserve
does not bound dynamic stack usage. A successful build alone does not establish
stack safety for a different program; the observer measures the tested images.
The memory fixture reaches a 77-byte gap between static storage and the stack;
the observer requires at least 32 bytes in every successful image.

The nine-case suite also covers the common hardware core (aggregate copies and
returns, globals/BSS and strings), pointer arrays, signed narrow loads, runtime
64-bit wrap/shift behavior and UTF-8 bytes followed by an ASCII hex digit. It
requires a runtime floating-point build to fail before producing a C artifact,
and proves that firmware returning 37 is reported as failure through the actual
UART/status path.

References: [Arduino UNO R3](https://docs.arduino.cc/hardware/uno-rev3),
[Arduino AVR build process](https://docs.arduino.cc/arduino-cli/sketch-build-process),
[GCC AVR options](https://gcc.gnu.org/onlinedocs/gcc/AVR-Options.html),
[simavr](https://github.com/buserror/simavr).
