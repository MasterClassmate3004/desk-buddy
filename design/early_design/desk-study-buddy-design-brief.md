# Desk Study Buddy — Integrated Design Brief

## Project direction

A rechargeable, small desktop robot combining the references: rounded body, expressive face, ears and tail as printed details, a side rotary control, and three small top buttons. It has a clock, focus/break timer, study-time tracking, and sound feedback. USB-C connects it to a Mac for time sync, timer control, settings, and session-log transfer while also charging the battery.

## Main features

- A Waveshare 1.5-inch square 128×128 grayscale OLED shows animated eyes, expressions, clock, focus/break state, and status text. Its active area is 26.86 × 26.86 mm: the diagonal is 1.5 inches, not the width. It is monochrome/grayscale, so animated faces are possible but colored expressions are not.
- An ESP32-C3 receives USB serial commands from a small Mac companion app, stores completed study sessions in flash, and runs independently when disconnected. Wi-Fi can provide optional time sync, but USB is the main computer link.
- The ESP32 keeps time while the rechargeable cell is powering it. The Mac app syncs the clock over USB when connected. If the cell is fully depleted or disconnected, the clock resyncs on the next computer connection; omit a separate RTC and coin cell to keep the circuit simple.
- A side-mounted rotary encoder with push navigates and adjusts settings. Three 6 mm low-profile tactile switches on the top handle start/pause, view/mode, and mute/sleep. Printed caps make them easier to find and click; these are small PCB switches, not keyboard switches.
- Printed ears and tail add character without motors. The PCB does not need to drive them unless you later decide movement is a defining feature.
- A passive piezo gives short button and timer sounds. The PCB should include a transistor driver footprint so the final sounder current can be handled safely.
- A protected one-cell 3.7 V LiPo charges through USB-C via the XIAO's onboard charger. The custom PCB exposes the battery connection and aligns the XIAO's USB-C port with the case opening; it does not add a second charger.
- Numeric battery monitoring is excluded from the base pin map; adding it later would require reclaiming an ADC-capable GPIO and adding a divider.
- The Mac app can sync time, set focus/break durations and sound preferences, start or pause a session, show the buddy's current state, and retrieve session history. Keep the USB protocol small and documented; the device remains useful without the app.

## Scope decisions

- No servo: ears and tail are printed details; expressions, blinking, and sounds provide the character without a motor or boost converter.
- No TP4056: the XIAO already has a single-cell charger. The protected cell connects to its BAT/GND pads, and the XIAO USB-C port is used for both charging and Mac communication.
- No separate RTC or coin cell: the device keeps time while its battery remains connected and powered. If the battery is fully depleted or unplugged, the Mac resends the time when USB reconnects.
- No audio amplifier or speaker: the passive piezo produces beeps and simple tones, not recorded music or speech.

## Proposed electronics

| Block | Direction |
| --- | --- |
| Controller | Seeed XIAO ESP32C3, 21×17.8 mm, USB-C, Wi-Fi, onboard single-cell lithium charging |
| Display | Waveshare 1.5-inch square 128×128 grayscale OLED module, SSD1327; set it to I²C mode. |
| Clock | ESP32 software clock, synchronized from the Mac over USB |
| Controls | EC11-class side encoder with push switch and three low-profile momentary tactile buttons on the top |
| Sound | Passive piezo, driven from a GPIO through a small transistor stage |
| Battery | Protected 3.7 V 1000 mAh LiPo with JST-PH2 lead; carrier PCB socket routes to XIAO BAT/GND |
| Charging and regulation | Use the charger and 3.3 V regulator already on the XIAO; its USB-C port serves the Mac serial connection and charging |
| Carrier PCB | XIAO headers/pads, display connector, buttons, encoder, piezo driver, battery pigtail, decoupling, and test pads; no 5 V rail or boost converter |

The ESP32-C3 has a built-in USB Serial/JTAG controller with a CDC serial port, so a Mac companion app can communicate over USB without Wi-Fi or an extra USB-to-UART chip. Keep the three top buttons, side encoder, piezo, and OLED wiring on the custom carrier PCB. The XIAO documentation specifies 380 mA fast charging and permits a data cable to be connected while the battery is attached. Use a qualified protected cell and verify BAT/GND pad polarity against the exact XIAO board revision before soldering. Final GPIO numbers and OLED jumper positions must follow the selected module revisions.

## Mechanical size and placement draft

Use a starting outer case envelope of **65 mm wide × 80 mm tall × 38 mm deep**. This is a CAD starting point, not a frozen enclosure size. It leaves room for a square face window, three top buttons, a side rotary knob, and a battery compartment.

| Part | Physical envelope to reserve | Placement notes |
| --- | --- | --- |
| Waveshare 1.5-inch OLED module | PCB: 44.5 × 37 mm; rotate to 37 mm wide × 44.5 mm tall. Lit area: 26.86 × 26.86 mm. | Front-mounted, square lit area centered in the taller bezel. Cut a paper 26.9 mm square and 37 × 44.5 mm module outline before final CAD. |
| Protected 1000 mAh LiPo | 43 × 30.5 × 8.5 mm; 100 mm lead | Rear/lower battery pocket with a smooth support surface and clearance; do not clamp, bend sharply, puncture, or compress the pouch. |
| XIAO ESP32C3 | 21 × 17.8 mm | Carrier PCB near the rear/top edge, USB-C port aligned to a case opening. Leave access to its reset/boot buttons and underside battery pads. |
| Carrier PCB | Initial target 55 × 45 mm, two layers | Horizontal base board: top buttons face upward; display uses a short lead to the front; XIAO USB-C faces the rear. Final outline follows measured CAD stack-up. |
| Side EC11 encoder | Approx. 11.7 × 12 mm body; selected listing has a 20 mm shaft | Panel mount in side wall, with shaft/nut clearance and a small printed knob. Connect its five terminals to the carrier by short wires or a 5-pin connector. Confirm the actual encoder's bushing and pin drawing before drilling. |
| Three tactile buttons | 6 × 6 × 4.3 mm each | Put across the top of the carrier PCB, about 11–12 mm center-to-center. Add 8–10 mm printed caps/plungers so they are easy to press and the small switches do not need large travel. |
| Piezo transducer | Target 12 × 12 mm footprint; reserve up to 4 mm height | Mount inside behind a sound grille; keep it away from the battery and case wall damping. Select a passive, externally driven piezo so firmware can make tones. |

The 1.5-inch panel is a small square face: the lit square is about 27 mm across, while the module board is 37 mm wide when rotated to portrait. Cut a paper mockup of both sizes before fixing the case opening, and leave a small mask margin so the bezel cannot cover active pixels. The exact body dimensions need the purchased display, encoder, and battery measured together in CAD.

## Electrical connections

### Power and charging

```text
Mac USB-C ── data + 5 V ──> XIAO ESP32C3 USB-C
                                  │
Protected 1S LiPo ── BAT/GND ─────┤ onboard charger / regulator
                                  └── 3V3 ──> OLED + controls + piezo driver
```

- The cell is one protected 3.7 V LiPo, 1000 mAh, with its JST-PH2 lead. Since the XIAO does not use a JST battery socket, connect a matching JST-PH2 socket/pigtail to the carrier PCB, then route its positive/negative pads to the XIAO BAT/GND pads. Verify polarity against the exact board revision before soldering and add strain relief for the battery lead.
- USB-C on the XIAO is the only charge input and the wired Mac data connection. Use a data-capable cable. Do not put a TP4056 in parallel with the XIAO charger and do not route USB 5 V to the battery pads.
- The XIAO guide lists 380 mA fast-charge current and allows computer data connection while the battery is attached. The board does not expose a fuel gauge, so the base plan reports no numeric battery percentage. The device can stay running while connected; charge time then increases because some input current runs the device.
- Keep the cell and XIAO's 3.3 V output within their datasheet current limits. This design has no servo, LED strip, speaker amplifier, or 5 V boost rail.
- Battery voltage measurement is omitted from the base design to save a pin and parts. The Mac can show “USB connected” while plugged in; when on battery, use a low-battery warning only if a divider and ADC reading are later added. Do not use the protection circuit cutoff as a warning signal.
- For idle periods, firmware can lower OLED contrast, issue display-off, and enter a low-power state. The D0 button is on a deep-sleep-wake-capable GPIO; confirm actual current and wake behavior on the assembled board before claiming battery runtime.

### XIAO GPIO allocation draft

This mapping uses the OLED in I²C mode. GPIO8 is used for OLED reset with a 10 kΩ pull-up, keeping the strap high during boot; firmware may pulse it low only after startup. GPIO2 is also a strapping pin, so its button input gets a 10 kΩ pull-up and must not be held low when resetting or entering the bootloader. Keep GPIO9 free of external loads because it is also the XIAO boot button signal. Review startup levels before locking the board.

| XIAO pin | ESP32-C3 GPIO | Connection | Electrical behavior |
| --- | ---: | --- | --- |
| D4 | GPIO6 | OLED SDA / DIN | I²C data, 3.3 V logic |
| D5 | GPIO7 | OLED SCL / CLK | I²C clock, 3.3 V logic |
| D8 | GPIO8 | OLED RST | 10 kΩ pull-up to 3V3; firmware reset pulse only after boot |
| D1 | GPIO3 | Encoder A | Input with pull-up; quadrature edge input |
| D2 | GPIO4 | Encoder B | Input with pull-up; quadrature direction input |
| D3 | GPIO5 | Encoder push switch | Active-low input with pull-up |
| D6 | GPIO21 | Top button 1: start/pause | Active-low input with pull-up |
| D7 | GPIO20 | Top button 2: view/mode | Active-low input with pull-up |
| D0 | GPIO2 | Top button 3: mute/sleep/wake | Active-low input with external 10 kΩ pull-up; release before reset/bootloader entry |
| D10 | GPIO10 | Piezo driver input | PWM output for short tones; no audio data stream |
| D9 | GPIO9 | Unused | Leave unconnected on the carrier; XIAO GPIO9 remains available for its boot function |

All button/encoder switches close to common ground and are debounced in firmware. The selected Waveshare SSD1327 OLED must be changed from factory 4-wire SPI to I²C by moving BS1/BS2 to VCC; DIN is SDA, CLK is SCL, and CS/DC are left disconnected. Set/check the address strap (0x3D by default; 0x3C is the alternate). Power it from 3V3 so its I²C pull-ups are safe for the ESP32. Confirm the purchased module's revision, header order, and reset behavior before ordering the carrier PCB.

### Piezo output circuit

Use one small, 3 V passive piezo transducer for beeps and simple melodies. GPIO10 drives an NPN transistor through a 1 kΩ base resistor; add a 100 kΩ base-to-ground pull-down so it stays silent during reset. Power the piezo from 3V3 and connect its return to ground. PWM generates short tones/chimes; this cannot play recorded music or speech. Audio clips would require a speaker and amplifier, which are excluded from this version.

### Carrier PCB support parts and connections

| Part | Draft specification | Connection and job |
| --- | --- | --- |
| Battery socket | JST-PH2, 2.0 mm pitch, keyed 2-pin; footprint must match the selected connector maker | Battery positive to XIAO BAT; battery negative to GND. Keep connector polarity printed on the PCB silkscreen. |
| OLED cable/header | Seven-pin 2.0 mm pitch cable/header matching the selected Waveshare module | VCC to 3V3, GND to GND, DIN to GPIO6/SDA, CLK to GPIO7/SCL, RST to GPIO8; CS and DC unconnected in I²C mode. Confirm pin order against the module label. |
| Pull-ups | 10 kΩ from GPIO2/button line to 3V3; 10 kΩ from OLED RST/GPIO8 to 3V3 | Preserves the GPIO2/GPIO8 recommended startup levels. Other buttons use MCU internal pull-ups. |
| Decoupling | 100 nF ceramic close to piezo driver/logic branch and 10 µF ceramic near the XIAO 3V3 distribution | Local supply bypassing; short power/ground loops. The XIAO already has its own onboard regulator and decoupling. |
| Piezo driver | Small NPN transistor, 1 kΩ base resistor, 100 kΩ base pull-down | Lets GPIO10 switch the piezo without relying on the GPIO to source the transducer current. |
| Test pads | BAT, 3V3, GND, SDA, SCL, GPIO10/PZ, plus one button net | Allows safe staged bring-up, charging checks, and I²C/piezo debugging before the case is closed. |

No battery boost converter, external 5 V rail, RTC, fuel gauge, audio amp, or servo connector is included. On the two-layer board, keep an uninterrupted ground plane where possible, put the XIAO and its USB-C port at the rear edge, route the battery and 3V3 rails clearly, and keep piezo switching current away from the OLED I²C pair. This is a small low-current design, so it does not need a separate analog ground region.

## Functions and user interaction

### On the robot

- **Idle face and clock:** animated eyes/blinks with time and next focus duration; the display sleeps/dims after inactivity but the clock keeps running from the battery.
- **Focus session:** start, pause, resume, and finish a configurable focus timer. Eye expression changes while focusing. On completion, play a short chime and save one session record to flash.
- **Break session:** switch to a break timer with a relaxed face, countdown, and end chime.
- **Session records:** retain completed sessions in flash so unplugging USB does not erase the history. Avoid writing every second; save only session transitions and completed records.
- **Mute and sleep:** top button 3 toggles piezo mute on a short press; a long press blanks the screen and enters low power. Pressing it again wakes the unit. The screen and clock state should restore without losing a running focus session.
- **Encoder:** rotate to move between clock/timer/settings or adjust focus/break duration; press to select/confirm. Debounce the encoder push and decode A/B transitions without blocking the clock.
- **Top buttons:** button 1 starts/pauses; button 2 changes view or timer mode; button 3 mutes on a short press and sleeps/wakes on a long press. Long/short timing must avoid accidental sleep on a normal mute press.

Use a monotonic elapsed-time counter for timers rather than decrementing a countdown once per loop. When entering sleep, store the session state and remaining duration in retained memory; on wake, resume from retained time if the selected sleep mode supports it. Otherwise keep the screen off and the MCU awake at low refresh rather than risking a silently corrupted session. Battery life is an item to measure on the assembled unit, not promise from capacity alone.

### On the Mac over USB

- Sync wall-clock time when the serial connection opens.
- Set focus and break durations, start/pause a session, and toggle mute.
- Show the connected buddy's current mode and remaining time.
- Request and export stored session history as CSV or JSON.
- Use a small line-based USB serial protocol (for example `TIME`, `SET_FOCUS`, `SET_BREAK`, `START`, `PAUSE`, `MUTE`, `STATUS`, `GET_LOGS`). Device replies should include a protocol version and explicit status/errors so the app can recover after reconnects.
- Keep time, controls, eyes, alarms, and session logging functional without the Mac app; no Wi-Fi permission or internet connection is required for normal use.

## Budget snapshot (India listings checked 2026-10-11)

Listed prices are not a delivered quote. Shipping and stock can change. The Half Life page currently says up to $100 per project and customs costs are not covered; it does not publish the proposed hour/funding ladder.

| Item | Price used |
| --- | ---: |
| XIAO ESP32C3, techiesms listing marked in stock | ₹560 |
| 1.5-inch square 128×128 OLED, Waveshare module listing | ₹1,296.82 |
| Protected 3.7 V 1000 mAh LiPo, Thingbits listing marked in stock | ₹649 |
| EC11 encoder with push, Circuitcomps listing | ₹47.25 |
| Allowance for tactile buttons, piezo, transistor/resistor, capacitors, connector, and small protection parts | ₹212 estimated |
| Two-layer PCB example, five boards up to 100×100 mm, delivery/GST included by the listing | ₹1,500 |
| **Planning subtotal** | **₹4,265.07 (about US$44.2 at ₹96.6/US$)** |

The subtotal excludes component shipping and any enclosure/fasteners. The OLED price is from a Waveshare listing; confirm stock and delivery before ordering. The XIAO listing at ₹560 is the current price used; Element14 showed ₹456.31 but its availability status was ambiguous.

The current component and PCB planning subtotal is about $44 before component shipping and enclosure materials. The OLED's active area is only 26.9 × 26.9 mm despite its 1.5-inch diagonal, so use the paper mockup before committing to the face opening. Treat this as a budget target, not a complete delivered quote; the exact battery pigtail, headers, fasteners, enclosure filament, and shipping still need supplier quotes.

## Program rules check

The current [Half Life program page](https://halflife.hackclub.com/) describes a 10-week program, lists Week 1 as PCBs, and says projects can receive up to $100 each; it says customs are not covered. It does not state that 20 hours earns $30, 27 hours earns $65, or 34 hours earns $100. Treat those hour/funding pairs as unverified unless the application workflow or program staff confirms them. The public page currently lists Week 5 as breadboard logic; it does not list a separate “computer” week.

## Before sending the PCB to fabrication

1. Lock the square OLED face and set enclosure proportions, viewing angle, top-button placement, and the static printed ear/tail geometry using full-size paper/card mockups.
2. Select exact purchasable part numbers for the XIAO, OLED, battery, encoder, buttons, piezo, and connectors; record tax, shipping, and stock.
3. Produce a complete delivered-cost BOM, including the enclosure parts and the actual PCB quote.
4. Draw the schematic and review XIAO BAT/GND polarity and charge path, USB data/charging access, display bus/voltage, GPIO allocation, button pulls, piezo driver, and test pads.
5. Estimate and measure battery current in active, screen-off, sleep, and charging states. Keep a divider/ADC battery monitor out of the first board unless numeric battery status is worth the extra pin and parts.
6. Run ERC/DRC, inspect the 3D board against the enclosure plan, then export and inspect both Gerbers and Excellon drill files in an independent viewer before ordering.
7. After boards arrive, validate USB enumeration and battery charging first; then bring up controller, display, buttons/encoder, sound, sleep/wake, and Mac serial commands as separate checks.

## Sources

- [Half Life program page](https://halflife.hackclub.com/)
- [Seeed XIAO ESP32C3 official guide and power/battery specifications](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/)
- [XIAO ESP32C3, techiesms listing](https://techiesms.com/product/seeed-studio-xiao-esp32c3/)
- [ESP32-C3 USB Serial/JTAG documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-guides/usb-serial-jtag-console.html)
- [Waveshare SSD1327 OLED specifications and I²C configuration](https://www.waveshare.com/wiki/1.5inch_OLED_Module)
- [1.54-inch square TFT price comparison](https://findmychips.com/part/waveshare-lcd-display-module-18079-1-54inch-ips-65k-rgb-240x240-015599)
- [2.4-inch portrait TFT, TecnoMate listing](https://www.tecnomate.in/component-library/2-4-inch-tft-lcd-display-spi-320x240)
- [Protected 1000 mAh LiPo, Thingbits listing](https://www.thingbits.in/products/lithium-ion-polymer-battery-3-7v-1000mah)
- [EC11 encoder, Circuitcomps listing](https://circuitcomps.com/collections/rotary-encoder/products/ec11-rotary-encoder-20mm-knurled-shaft-with-push-switch)
- [TP4056 standalone charger datasheet](https://cdn.sparkfun.com/datasheets/Prototyping/TP4056.pdf)
- [India prototype PCB price example](https://www.whypcb.com/pcb-prototype-service-india)
- [RBI reference rate table via NSE India](https://www.nseindia.com/report-detail/rbi-reference-rate-statistics)
