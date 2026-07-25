

<!-- # VerhoBot
<img width="1536" height="1024" alt="verho_concept_design" src="https://github.com/user-attachments/assets/45290109-c627-40e5-b835-4a0c27403841" /> 
<img width="1536" height="1024" alt="verho_b" src="https://github.com/user-attachments/assets/ea8144e8-805f-48bb-890e-384471ef9897" /> -->

<!-- <img width="1535" height="1024" alt="verho_banner" src="https://github.com/user-attachments/assets/6782ed62-eeb0-4958-a5c3-7888029129f1" /> 

<img width="1535" height="1024" alt="verho_banner_white" src="https://github.com/user-attachments/assets/3a6c4d62-edfc-40f7-b2f0-12bc582d1462" /> -->

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="https://github.com/user-attachments/assets/6782ed62-eeb0-4958-a5c3-7888029129f1">
  <source media="(prefers-color-scheme: light)" srcset="https://github.com/user-attachments/assets/3a6c4d62-edfc-40f7-b2f0-12bc582d1462">
  <img width="100%" height="100%" alt="verho_banner" src="https://github.com/user-attachments/assets/3a6c4d62-edfc-40f7-b2f0-12bc582d1462">
</picture>



<div align="center">

### The Tiny Bot That Opens and Closes Your Curtains for You

*Sleep in darkness. Wake up with sunlight.*

[![Status](https://img.shields.io/badge/Status-Evolving-purple)]()
[![License](https://img.shields.io/badge/License-MIT-blue)]()
[![Platform](https://img.shields.io/badge/Platform-ESP32-red)]()
[![Open Source](https://img.shields.io/badge/Open%20Source-Yes-brightgreen)]()
![OSHWA Certified](https://img.shields.io/badge/OSHWA-FI000004-brightgreen)

[![Made in Finland](https://img.shields.io/badge/Made%20for-Finnish%20Summer-003580)]()
![Visitor Count](https://visitor-badge.laobi.icu/badge?page_id=migit.verhoBot)

</div>

---

## Overview

VerhoBot is an open-source smart curtain robot designed to automate existing curtain rails using affordable, widely available hardware and fully customizable software.

The project was born from a simple observation: many people use blackout curtains to improve sleep quality, but those same curtains prevent natural sunlight from entering the room when it is time to wake up. This challenge is particularly noticeable during the Finnish summer, when long daylight hours and bright nights can disrupt normal sleep patterns.

VerhoBot addresses this problem by providing a compact robotic platform capable of opening and closing curtains automatically according to schedules, user commands, or future smart-home automations.

Unlike many commercial curtain robots, VerhoBot is designed from the beginning as an open-source project. Every aspect of the system including hardware design, firmware, mechanical components, and future software integrations is intended to be accessible, modifiable, and extensible by makers, students, engineers, and hobbyists.


**VerhoBot** solves both problems.

VerhoBot is a compact curtain-moving robot designed to automatically close your curtains when you need darkness and open them again when it is time to wake up. The goal is simple: improve sleep quality while letting natural sunlight become part of your morning routine.

Built around the ESP32 platform, VerhoBot aims to be affordable, repairable, open-source, and easy to integrate into modern smart homes.

---

## Features

### Current Goals

- Automatic curtain opening and closing
- Compact rail-mounted design
- Battery-powered operation
- ESP32-based controller
- Quiet operation
- USB-C charging
- Low-power sleep modes
- Open-source hardware and software

### Planned Features

- Scheduled curtain control 
- Sunrise and sunset automation
- Home Assistant integration
- Mobile configuration
- OTA firmware updates
- Battery monitoring
- Manual override controls
- Position calibration
- Smart alarm mode

---

## Why?

Finland's summer is famous for its bright nights.

While blackout curtains help create a dark sleeping environment, they also block the morning sunlight that helps regulate the body's natural wake-up cycle.

VerhoBot combines the best of both worlds:

- Darkness when you need sleep
- Sunlight when it is time to wake up

No alarms.
No bright phone screens.
Just natural light at the right moment.
The long-term vision of VerhoBot is to create a flexible, affordable, and fully open curtain automation platform that can evolve through community contributions.
Rather than competing with commercial smarthome products, VerhoBot aims to give users complete ownership of their hardware, software, and data while remaining accessible to makers of all skill levels.

## Comunity Goals

The project aims to provide a practical open-source platform for learning:

Embedded systems
ESP32 development
Mechanical design
Battery-powered electronics
IoT communication
Smart-home integration
Product development

Whether you are building your first ESP32 project or developing advanced automation systems, VerhoBot should provide a foundation that is easy to understand, modify, and improve.

---

## Hardware

###  Electronics

| Component | Purpose |
|------------|----------|
| ESP32-C3 supermini | Main controller |
| N20 Geared DC Motor (100 RPM 6V works best without loosing good torque and speed) | Curtain movement |
| Motor Driver (TB6612FNG is chosen for best effeciency and size form factor) | Motor control |
| Li-Ion Battery (3.7v 300mAh is chosen) | Portable power |
| USB-C Charger | Charging interface |
| Position Sensors (note yet decide, but position calibration is now done in the firmware for now : const uint32_t CURTAIN_TRAVEL_TIME = 4000;)| Open/close detection |

### schematic

<img width="1536" height="1024" alt="VehoBot-schematic" src="https://github.com/user-attachments/assets/dab667c9-d4ce-4847-ab22-8194f0a0e182" />

### PCB design
-PCB design is active now, I will release the first draft, so that the community can comment on it before it is has been manufactured (Help is wanted here)

###  Mechanical Design

<img width="1536" height="1024" alt="vbot1" src="https://github.com/user-attachments/assets/3202e9c1-f443-4a04-8838-7c1d054d3fdf" />

- Compact form factor
- Rail-mounted operation
- Existing curtain compatibility
- 3D-printable enclosure built arround the designed PCB board
- Modular internal architecture

[▶ Video demo VerhoBot enclosure and mechanical design](https://www.youtube.com/watch?v=YXQe8-dixHU)

[▶ Video full build steps](https://www.youtube.com/watch?v=6-GEg8fdRDQ) 


[3D CAD files here](https://www.thingiverse.com/thing:7370927)

### Parts

<img width="4080" height="3072" alt="VerhoBot_parts" src="https://github.com/user-attachments/assets/a81883dd-afa9-40b6-8be4-5f3731d33b52" />

---

## Software Roadmap

### Phase 1


- Basic motor control [Done]
- Position calibration [Done]
- Manual open/close control [Done]

### Phase 2

- Scheduling system [Done]
- Battery monitoring [Done]
- Configuration interface [Done] 

### Phase 3

- OTA updates [Done]
- Web-based firmware flasher (Done,)
- Home Assistant integration [Pending...for Version 2]
- Smart automation features [Pending... for Version 2]

### NOTE: All software, firware and hardware works of this project is complete and in stable version but verho bot is still evolving as V2 as next generation VerhoBot, meanning This repo (V1) will continue to be mainatined and evolve without affecting the OSHWA certification standard and what it promised to deliver. 

---

## operation

<!-- <img width="1264" height="833" alt="op_mode" src="https://github.com/user-attachments/assets/f6f8d905-06a9-4c95-84db-494406e7b41f" /> -->
<img width="969" height="1033" alt="VerhoBot - visual selection" src="https://github.com/user-attachments/assets/88effa7d-0860-4459-9377-e51b27e27843" />


## How it suppose to behave:
- First boot -> AP VerhoBot-Setup -> dashboard at 192.168.4.1 -> click ⚙ to enter WiFi & schedule -> save -> reboot.
- Normal operation -> connects to home WiFi -> syncs time -> sleeps.
- Scheduled wake -> moves curtain -> starts web server -> stays awake 60 s -> sleeps.
- Button wake -> toggles curtain -> starts web server -> stays awake 60 s -> sleeps.
- During the 60‑second awake window, you can open your browser, go to the device’s IP (shown in Serial), and see the full dashboard, control the curtain, and check telemetry.
- After 60 s of inactivity, it goes back to deep sleep.

## Quick Troubleshooting:

- It restarts automatically after saving, and should join your home WiFi. The VerhoBot-Setup network will disappear (expected - that's the sign it's now trying your real WiFi).
- Find it on your network. It'll get an IP from your router. Easiest way: check your router's connected-devices list for "verhobot" or similar, or use a phone WiFi scanner app / your router admin page. There's no verhobot.local mDNS support yet, so this is the one manual step for now.
- Open the dashboard at that IP. You should see the schedule card showing your configured open/close times and a live countdown to the next action.
If it doesn't show up on your network within a minute or two, it likely couldn't connect (wrong password, wrong network, etc.) - with the fix from earlier, it'll fall back to broadcasting VerhoBot-Setup again so you can fix the credentials. Reconnect to that and try /setup again.
- Calibrate the position once it's mounted on the actual curtain rail - run it fully open and fully closed once, then use the "set as fully open / fully closed" buttons on the dashboard so the tracked position matches reality (it's timing-based, not sensor-based, so it can drift).
- Mount it and let it run unattended - check back around your first scheduled open/close time to confirm it moves as expected, then it should just run on its own, waking briefly at each scheduled event and sleeping the rest of the time.

- If it connects fine and everything looks right, you're basically done - just keep an eye on battery level on the dashboard for the first few days to get a sense of how long it lasts between charges.

## Project Status

```text

v_0.1.0-beta1.2  -> Firmware complete
v_0.1.0-beta1.1  -> Testing and tuning
v_0.1.0-beta1  ->   Manual open/close control
v1.0  -> Stable release
v1.1  -> Stable with Improved dashboard
V1.2  -> OTA feature added
V1.2.1 -> Fixed a boot button deep sleep wake to GPIO1 as esp32-c3 does not support this
```

Active patching is ongoing but stable phase.

## Lesson Learned 

- So far verhoBot is being tested for over two weeks, firmware bug is being fixed so plugging and unplugging the USB C was a headache, so impliment OTA at the early stage saved me time.
- Control & config dashboard is being improved scheduling,OTA update, home wifi connect and curtain calibration (time based) are working flowlessly these are small features so working on them separately was a good idea even if they seem small and manageable.
- On the hardware side wakr up GPIO has been added. Before, it was suppose to work with boot button press which the esp32-c super mini board does not support
- The deep sleep has saved lots of battery life but the LED (I call it the terminator eye) glows in the dark was cool indicating the bot is on but still consumes battery life, in the future desoldering it from the board will defenetly save some more battery life.
- The Li-Po battery I am using is 300mAh, still the bot runs for 6 days straight...(I will update this when I finish experimenting on it!).I leaned battery size (physical size) matters and as the battery gets bigger the form factor of the bot case changes I should have considered this before I designed the case but hey, now harm!


--- 

## Design Principles

VerhoBot follows a few simple rules:

- Open-source first
- Repairable hardware
- Affordable components
- Low power consumption
- Easy to build
- Easy to modify
- No vendor lock-in (you own everything)

---

## Future versions may incorporate:

- Position sensing
- Motor current monitoring
- Automatic obstacle detection
- Endstop detection
- Environmental sensors
- Advanced power management

---

## Demo

#Dashboard preview:

https://github.com/user-attachments/assets/f727beac-c3cd-45c5-90aa-1bad3a5c13f2

## Contributing

Contributions, ideas, testing feedback, and hardware improvements are welcome.
As the project evolves, documentation, CAD files, schematics, and firmware source code will be published in this repository.

---

## License

Distributed under the MIT License.
See `LICENSE` for more information.


VerhoBot has been officially certified as Open Source Hardware by the Open Source Hardware Association (OSHWA).

- **Certification UID:** FI000004
- **Certification Date:** 2026
- **Status:** OSHWA Certified Open Source Hardware

This project complies with the Open Source Hardware Definition and all design files are publicly available.
For more information about Open Source Hardware certification, visit the Open Source Hardware Association (OSHWA).

[<img width="147" height="42" alt="OSHWA Certified Open Source Hardware FI000004" src="https://github.com/user-attachments/assets/a9026b4a-a719-42a9-8c82-fcb286305578" />](https://certification.oshwa.org/fi000004.html)

---

<div align="center">

### VerhoBot

*Built for the Finnish summer. Makers for Makers*

</div>
