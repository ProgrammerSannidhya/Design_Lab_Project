# AegisMed — Wokwi Simulation (Educational Prototype)

**Disclaimer:** AegisMed is a college/hobbyist demonstration project. All
sensor readings are simulated and the "Demo Severity" output is a simple
illustrative calculation — it is **not** a medical diagnostic device and
must never be used to make real health decisions.

## Project structure

```
aegismed/
├── diagram.json          Wiring / hardware layout
├── sketch.ino             ESP32 firmware
├── libraries.txt          Arduino library dependencies
├── wokwi.toml              Wokwi project config (registers the two custom chips)
├── max30102.chip.json      Custom chip definition (HR/SpO2 sim)
├── max30102.chip.c         Custom chip implementation (I2C slave, addr 0x57)
├── mlx90614.chip.json      Custom chip definition (temperature sim)
├── mlx90614.chip.c         Custom chip implementation (I2C slave, addr 0x5A)
└── README.md               This file
```

## 8. Wokwi setup instructions

1. Go to https://wokwi.com and create a **New Project → ESP32**.
2. Replace the generated `diagram.json` with the one provided here.
3. Replace `sketch.ino` with the provided firmware.
4. Open **Library Manager** (or the `libraries.txt` panel in the new
   Wokwi project UI) and add:
   - `Adafruit GFX Library`
   - `Adafruit SSD1306`
5. Create two new files in the project named `max30102.chip.json` and
   `max30102.chip.c`, and paste in the provided content. Repeat for
   `mlx90614.chip.json` / `mlx90614.chip.c`.
   - Wokwi auto-builds any `*.chip.c` file it finds alongside a matching
     `*.chip.json` in the same project (Custom Chips feature). The chip's
     part type in `diagram.json` is `chip-<filename>`, e.g. `chip-max30102`.
   - If your Wokwi account/version requires an explicit build step or the
     separate `wokwi-chip` CLI instead of in-browser building, run:
     `npm install -g wokwi-chip-cli` (or follow the current instructions at
     docs.wokwi.com/chips-api) and build each `.chip.c` into `.chip.wasm`
     before starting the simulation.
6. Copy `wokwi.toml` into the project root.
7. Click **Start Simulation** (green play button).

## 9. Testing procedure

### TEST A — Normal readings
1. Open the MAX30102 chip's control panel (click the chip in the diagram).
2. Set the **Heart Rate** slider to ~75 BPM and **SpO2** slider to ~98%.
3. Open the MLX90614 chip's control panel and set **Object Temp** to ~368
   (36.8 °C).
4. On the OLED, press **Button 1** until "AegisMed - Readings" is shown.
5. Confirm HR/SpO2/Temp match the sliders, the RGB LED is **green**, and
   the Serial Monitor prints `Demo Severity: NORMAL`.

### TEST B — Warning readings
1. Set SpO2 slider to ~92% (or HR to ~110 BPM, or Temp to ~380 = 38.0 °C).
2. Wait up to 1 second for the next sensor read cycle.
3. Confirm the RGB LED shows the warning color (blue), and Serial Monitor
   prints `Demo Severity: WARNING`. Check the "Severity" screen (press
   Button 1 once more) to see the status directly.

### TEST C — Critical readings
1. Set SpO2 slider below 90%, or Temp slider to ≥390 (39.0 °C), or HR
   slider to ≥120 BPM.
2. Confirm the RGB LED turns **red**, the buzzer pulses periodically, and
   Serial Monitor prints `Demo Severity: CRITICAL`.

### TEST D — Emergency button
1. At any time, press **Button 2**.
2. Confirm the OLED immediately shows `!!ALERT!!` / "Check readings" /
   "Consult a doctor", the buzzer sounds a short 3-beep pattern, and the
   RGB LED blinks red for a few seconds before the display returns to the
   previously selected screen.
3. Confirm Serial Monitor logs `Button 2 pressed -> emergency/demo alert
   triggered`.

### TEST E — Sensor value changes
1. While the simulation is running, move any of the three sliders (HR,
   SpO2, Temp) in the chip control panels.
2. Within one `SENSOR_READ_INTERVAL_MS` cycle (1 second), confirm the OLED
   "Readings" screen and Serial Monitor output update to the new values,
   and that the severity/RGB/buzzer state updates accordingly if it
   crosses a threshold.

## Adapting to real hardware

- Swap `max30102.chip.c`'s simplified register access in `sketch.ino` for
  a real driver (e.g. SparkFun MAX3010x library) — the rest of the
  architecture (`readSensors()`, `calculateSeverity()`, `updateOLED()`,
  etc.) does not need to change.
- The MLX90614 driver already uses the real TOBJ1 register formula, so
  it can be swapped for the Adafruit MLX90614 library with minimal change
  (mainly adding PEC handling, which this simulation omits).
- All GPIO assignments match a realistic ESP32 DevKit V1 wiring and can be
  used as-is on physical hardware.
