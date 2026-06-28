<!-- # VerhoBot
<img width="1536" height="1024" alt="verho_concept_design" src="https://github.com/user-attachments/assets/45290109-c627-40e5-b835-4a0c27403841" /> 
<img width="1536" height="1024" alt="verho_b" src="https://github.com/user-attachments/assets/ea8144e8-805f-48bb-890e-384471ef9897" /> -->
<img width="1535" height="1024" alt="vbot" src="https://github.com/user-attachments/assets/ee001041-8415-46b3-afd2-778e4268ae52" />


<div align="center">

### The Tiny Bot That Opens and Closes Your Curtains for You

*Sleep in darkness. Wake up with sunlight.*

[![Status](https://img.shields.io/badge/Status-In%20Development-orange)]()
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

### Planned Electronics

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


### Planned Mechanical Design

<img width="1536" height="1024" alt="vbot1" src="https://github.com/user-attachments/assets/3202e9c1-f443-4a04-8838-7c1d054d3fdf" />

- Compact form factor
- Rail-mounted operation
- Existing curtain compatibility
- 3D-printable enclosure
- Modular internal architecture

[▶ Video demo VerhoBot enclosure and mechanical design](https://www.youtube.com/watch?v=YXQe8-dixHU)

[▶ Video full build steps](https://www.youtube.com/watch?v=6-GEg8fdRDQ) 


Pre-release 3D CAD files [here](https://www.thingiverse.com/thing:7370927)

### Parts

<img width="4080" height="3072" alt="VerhoBot_parts" src="https://github.com/user-attachments/assets/a81883dd-afa9-40b6-8be4-5f3731d33b52" />


---

## Software Roadmap

### Phase 1


- Basic motor control
- Position calibration
- Manual open/close control

### Phase 2

- Scheduling system
- Battery monitoring
- Configuration interface

### Phase 3

- Home Assistant integration
- OTA updates
- Smart automation features

---

## Planned operation

<!-- <img width="1264" height="833" alt="op_mode" src="https://github.com/user-attachments/assets/f6f8d905-06a9-4c95-84db-494406e7b41f" /> -->
<img width="969" height="1033" alt="VerhoBot - visual selection" src="https://github.com/user-attachments/assets/88effa7d-0860-4459-9377-e51b27e27843" />


## How it suppose to behave:
- First boot → AP VerhoBot-Setup → dashboard at 192.168.4.1 → click ⚙ to enter WiFi & schedule → save → reboot.
- Normal operation → connects to home WiFi → syncs time → sleeps.
- Scheduled wake → moves curtain → starts web server → stays awake 60 s → sleeps.
- Button wake → toggles curtain → starts web server → stays awake 60 s → sleeps.
- During the 60‑second awake window, you can open your browser, go to the device’s IP (shown in Serial), and see the full dashboard, control the curtain, and check telemetry.
- After 60 s of inactivity, it goes back to deep sleep.

## Project Status

```text

v_0.1.0-beta1.2  -> Firmware complete
v_0.1.0-beta1.1  -> Testing and tuning
v_0.1.0-beta1  ->   Manual open/close control
v1.0  -> Stable release

```

Active development is ongoing.

---

## Design Principles

VerhoBot follows a few simple rules:

- Open-source first
- Repairable hardware
- Affordable components
- Low power consumption
- Easy to build
- Easy to modify
- No vendor lock-in

---


## Future versions may incorporate:

- Position sensing
- Motor current monitoring
- Automatic obstacle detection
- Endstop detection
- Environmental sensors
- Advanced power management

---

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
