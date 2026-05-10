# Hardware — ESP32 School Robot Car

Complete bill of materials, wiring map, and notes. Prices reflect what I paid in May 2026 in Germany — yours may differ depending on supplier and region.

> Replace the placeholder vendor links and exact part numbers below with the components you actually used. Specific links increase the value of this document for anyone trying to rebuild the car.

---

## Bill of materials

| # | Component | Quantity | Approx. price | Vendor / Link |
|---|-----------|----------|---------------|---------------|
| 1 | ESP32 dev board (e.g. ESP32-WROOM-32 DevKit V1) | 1 | € X | [vendor link] |
| 2 | L298N motor driver module | 1 | € X | [vendor link] |
| 3 | DC gear motor with wheel (TT motor 1:48 or similar) | 2 | € X | [vendor link] |
| 4 | Caster wheel / front ball roller | 1 | € X | [vendor link] |
| 5 | 0.96″ OLED display, SSD1306, I²C, 128×64 | 1 | € X | [vendor link] |
| 6 | HC-SR04 ultrasonic distance sensor | 1 | € X | [vendor link] |
| 7 | PS4 or PS5 wireless controller (DualShock 4 / DualSense) | 1 | € X | [vendor link] |
| 8 | Battery holder (4 × AA) **or** 1 × 18650 LiPo + holder | 1 | € X | [vendor link] |
| 9 | AA batteries / 18650 cell | 4 / 1 | € X | — |
| 10 | Jumper wires (M-M, M-F, F-F mixed) | 1 set | € X | [vendor link] |
| 11 | M3 screws + nuts (10 mm) | ~10 | € X | [vendor link] |
| 12 | Header pins (if board doesn't include them) | 1 set | € X | [vendor link] |
| 13 | USB cable (Micro-USB or USB-C, depending on board) | 1 | € X | — |

**Total approx. cost per car: € XX** *(controller often re-used between several cars in a workshop setting)*

---

## Wiring map

The firmware does PWM on the L298N **IN1–IN4** lines, not on ENA/ENB. This means:

> ⚠️ **Bridge ENA and ENB to +5 V (HIGH) on the L298N module.** Most L298N modules ship with a pair of jumpers next to the ENA/ENB headers — leave them in place. If you bought a bare-bones module without jumpers, use two short jumper wires to connect ENA and ENB to the +5 V rail of the module.

### L298N motor driver → ESP32

| L298N pin | ESP32 GPIO | Function |
|-----------|------------|----------|
| IN1       | **GPIO 13** | Left motor — direction A (PWM) |
| IN2       | **GPIO 12** | Left motor — direction B (PWM) |
| IN3       | **GPIO 27** | Right motor — direction A (PWM) |
| IN4       | **GPIO 33** | Right motor — direction B (PWM) |
| ENA       | (jumpered to +5 V) | Left motor enable — always HIGH |
| ENB       | (jumpered to +5 V) | Right motor enable — always HIGH |
| GND       | GND         | Common ground |
| +5 V (logic) | — | from L298N onboard regulator (when battery ≥ 7 V) |
| +12 V (motor power) | — | from battery pack |

### HC-SR04 ultrasonic sensor → ESP32

| HC-SR04 pin | ESP32 pin | Function |
|-------------|-----------|----------|
| VCC         | 5 V       | Power |
| GND         | GND       | Ground |
| TRIG        | **GPIO 5** | Trigger pulse |
| ECHO        | **GPIO 18** | Echo (distance reading) |

### OLED display (SSD1306, I²C) → ESP32

| OLED pin | ESP32 pin | Function |
|----------|-----------|----------|
| VCC      | 3.3 V (or 5 V — check your module) | Power |
| GND      | GND       | Ground |
| SDA      | **GPIO 21** (default I²C SDA) | I²C data |
| SCL      | **GPIO 22** (default I²C SCL) | I²C clock |

> The default I²C address used by the firmware is **0x3C**. If your display uses 0x3D, change `SCREEN_ADDRESS` in the sketch.

### Controller

The PS4/PS5 controller pairs **wirelessly** via Bluetooth — no wiring needed. The Bluepad32 library on the ESP32 handles pairing and decoding.

To pair: power on the car, then on the controller hold **Share + PS** (PS4) or **Create + PS** (PS5) until the lightbar flashes rapidly. The OLED will show *Ctrl Connected* once paired.

---

## Power notes

- The ESP32 runs on 5 V via USB or, in standalone operation, from the L298N's onboard 5 V regulator (which only works reliably with a battery voltage ≥ 7 V — for 4×AA = 6 V, you'll usually still need to power the ESP32 separately via USB).
- The motors run from the battery pack via the L298N's +12 V input, **not** from the ESP32.
- **Connect all grounds together**: ESP32 GND, L298N GND, battery GND, OLED GND, HC-SR04 GND. Without a common ground reference, the motor driver and sensors won't behave correctly.
- For workshops with kids, AA-battery packs are safer than LiPo cells. They're easier to handle and don't require a charger or charge management.

---

## Where to buy

Most components are available at:

- [Reichelt Elektronik](https://www.reichelt.de/) (DE)
- [AZ-Delivery](https://www.az-delivery.de/) (DE)
- [Eckstein Komponente](https://eckstein-shop.de/) (DE)
- [Berrybase](https://www.berrybase.de/) (DE)
- [Welectron](https://www.welectron.com/) (DE)

Replace this list with whatever you use locally.

---

## Common issues

- **Motors don't move at all** → check that ENA and ENB are jumpered HIGH. Without them, the L298N keeps the motor outputs disabled regardless of the IN1–IN4 signals.
- **Motor only spins in one direction** → check the four direction pins (GPIO 13, 12, 27, 33) and make sure none of them are floating or shorted.
- **Car drives in a curve instead of straight** → that's the calibration use-case. Press *Options* on the controller to enter the setup menu, then adjust *Left Trim* / *Right Trim* until both wheels turn at the same speed. Press *Triangle* to save.
- **ESP32 resets when motors start** → shared power supply collapsing under motor inrush. Either separate the motor power from the ESP32 power, add a 470 µF capacitor across the motor driver's input, or feed the ESP32 from USB during testing.
- **OLED stays blank** → wrong I²C address or swapped SDA/SCL. Try changing `SCREEN_ADDRESS` from `0x3C` to `0x3D` in the sketch. If still blank, swap the SDA and SCL wires.
- **HC-SR04 always reads the same distance** → check the trigger/echo wiring (GPIO 5 / GPIO 18) and make sure the sensor isn't pointing at a soft surface (carpet, foam) — those absorb ultrasound.
- **Controller won't pair** → enable Bluetooth on the controller in pairing mode (Share/Create + PS, until the lightbar flashes *rapidly* — slow flashing means it's just looking for its last paired device). If it still won't pair, uncomment `BP32.forgetBluetoothKeys();` in `setup()`, flash once, then re-comment and re-flash.
