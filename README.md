# Smart Thermal Controller

An embedded automatic temperature monitoring and fan control system built on an STC 8051-compatible microcontroller. It continuously reads ambient temperature from a digital sensor, displays live status on an LCD, drives a cooling fan relay based on a user-configurable threshold, and persists that threshold in non-volatile memory.

Designed for protecting heat-sensitive equipment such as server cabinets or electronics enclosures.

---

## Features

- 🌡️ **Real-time temperature monitoring** via TMP102 digital sensor (12-bit resolution, 0.0625 °C/LSB)
- 🖥️ **16×2 LCD display** showing live temperature and fan status, driven over I2C through a PCF8574 backpack
- ⌨️ **Configurable threshold** entered via a 4×4 matrix keypad (press `*` to enter setup, `#` to save, `*` again to cancel)
- 💾 **Persistent settings** — threshold is stored in on-chip EEPROM (IAP) and survives power cycles
- 🌀 **Automatic fan control** — relay switches ON/OFF based on whether temperature crosses the threshold
- 🚨 **Critical temperature safety indicator** — server status LED turns off above 60 °C as an emergency signal
- 🔄 **Hardware reset** — dedicated reset button triggers a clean software restart via INT0 interrupt
- ⚡ **Non-blocking keypad polling** — main loop stays responsive while scanning for key presses

---

## Hardware

| Component | Interface | MCU Pin(s) |
|---|---|---|
| STC 8051-compatible MCU (IAP-capable, e.g. STC89C52RD+) | — | — |
| TMP102 temperature sensor | Bit-banged I2C | P2.2 (SDA), P2.3 (SCL) |
| 16×2 LCD + PCF8574 I2C backpack (addr `0x4E`) | Bit-banged I2C | P2.7 (SDA), P2.6 (SCL) |
| 4×4 matrix keypad | GPIO | P1.0–P1.7 |
| Fan relay (via transistor driver) | GPIO | P2.1 |
| Server status LED | GPIO | P3.4 |
| Reset status LED | GPIO | P3.5 |
| Reset push button | External Interrupt (INT0, falling edge) | P3.2 |

> An MCU pin cannot drive a relay coil directly — use an NPN transistor with a flyback diode between P2.1 and the relay coil. See [`docs/Proteus_Schematic_Guide.md`](docs/Proteus_Schematic_Guide.md) for full wiring instructions.

---

## Software / Toolchain

- **Language:** C (Keil C51 dialect — uses `reg52.h`, `intrins.h`, `sbit`, `sfr`)
- **Target:** STC 8051-family MCU with IAP EEPROM support
- **Recommended IDE:** Keil µVision (C51 compiler) or STC-ISP for flashing
- **Simulation (optional):** Proteus 8 Professional

### Building

1. Open the project in Keil µVision (or create a new project and add `main.c`).
2. Select your STC target device (or the closest AT89C52-family equivalent if not listed).
3. Build to generate the `.hex` file.
4. Flash to the microcontroller using STC-ISP or your preferred ISP/serial programmer.

---

## How it works

1. **Boot** — peripherals initialize, a boot message displays briefly, and the saved threshold is loaded from EEPROM (defaults to 30 °C on first boot or if the stored value is invalid).
2. **Main loop** — the keypad is polled, the current temperature is read and shown on the LCD, and the fan relay and server LED are updated based on the temperature thresholds.
3. **Setup mode** — pressing `*` pauses normal control (fan forced off for safety), lets the user type a new 1–2 digit threshold, and saves it to EEPROM on `#`.
4. **Emergency state** — if temperature reaches 60 °C, the server status LED turns off to signal a critical condition, independent of fan state.
5. **Reset** — pressing the reset button fires an INT0 interrupt that issues an STC software reset command.

### Temperature conversion

```c
temp_raw    = (MSB << 4) | (LSB >> 4);          // combine TMP102 bytes into 12-bit value
temp_scaled = temp_raw * 625 / 100;             // convert to hundredths of a degree (fixed-point)
current_temp_int = temp_scaled / 100;           // integer °C
frac_part        = temp_scaled % 100;           // fractional °C
```

---

## Configuration

| Constant | Default | Description |
|---|---|---|
| `max_threshold` | 30 °C | Fan-on temperature threshold (user-configurable, 1–99 °C, saved to EEPROM) |
| Critical limit | 60 °C | Hardcoded — server LED turns off at/above this temperature |
| `EEPROM_ADDR` | `0x2000` | IAP sector address used to store the threshold byte |

---

## Project Structure

```
.
├── main.c                      # Firmware source
├── docs/
│   └── Proteus_Schematic_Guide.md   # Step-by-step circuit wiring guide
└── README.md
```

---

## Known Limitations

- Single ON/OFF (bang-bang) fan control — no hysteresis, so the fan may cycle near the exact threshold.
- Single temperature zone / single sensor.
- No wireless connectivity or remote logging.
- Threshold entry is limited to two digits (1–99 °C).

## Possible Future Work

- Add hysteresis to reduce relay chatter near the threshold.
- Variable-speed (PWM) fan control instead of relay ON/OFF.
- Multiple sensor zones.
- Wireless monitoring / data logging module.
- Audible alarm for critical temperature conditions.

---

## Authors

- MUHAMMAD HARITH HAYKAL BIN
MOKHTAR
- IZZAT RAMZAN BIN AKBAR BATCHA
- MUHAMMAD AMMAR ZIKRY BIN
ZAIMI
- MUHAMMAD FIRMAN BIN RAMLI
- MUHAMMAD IRFAN SYAKIR BIN
SAMSUDIN
