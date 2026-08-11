# 15 – Useful Resources

Curated reference material for the LMYC Boat Terminal project.

Focus is on practical understanding of **NMEA 0183**, **NMEA 2000**, wiring, isolation, marine networking, display hardware, and related open projects.

---

## Foundational Guides

| Resource | Why it is useful |
|----------|------------------|
| [Beginners guide to NMEA 2000, NMEA 0183, and bridging](https://boatprojects.blogspot.com/2012/12/beginners-guide-to-nmea-2000-nmea-0183.html) | Clear, practical introduction covering single-ended vs differential, multipath talkers, and bridging. Good starting point. (2012) |
| [NMEA 2000 Explained – A Simple Intro (CSS Electronics)](https://www.csselectronics.com/pages/nmea-2000-n2k-intro-tutorial) | Excellent modern technical overview of NMEA 2000, PGNs, Fast Packets, and relationship to CAN/J1939. |
| [NMEA Primer (Hat Labs / Sailor Hat)](https://docs.hatlabs.fi/sh-wg/pages/nmea_primer/) | Short, clean primer on both 0183 and 2000 with good explanations of electrical layers. |
| [BoatHowTo – NMEA 0183 and 2000 module](https://boathowto.com/module/nmea-networks/) | Structured learning module covering physical layer, installation rules, and troubleshooting. |

---

## Official / Manufacturer Reference Material

| Resource | Notes |
|----------|-------|
| [Actisense – Everything you need to know about NMEA 0183 (PDF)](https://actisense.com/wp-content/uploads/2021/01/Everything-you-need-to-know-about-NMEA-0183-1.pdf) | One of the best single documents on NMEA 0183 electrical characteristics, talkers/listeners, and common pitfalls. |
| [Actisense – How to build a hybrid NMEA 0183 + NMEA 2000 network](https://actisense.com/news/how-do-i-build-a-hybrid-nmea-0183-and-nmea-2000-network/) | Practical guidance on gateways and conversion between the two standards. |
| [Actisense – Building and troubleshooting an NMEA 2000 network](https://actisense.com/news/the-basics-of-building-and-testing-your-nmea-2000-network/) | Clear rules for backbone, drops, power, and termination. |
| [Digital Yacht – How to create an NMEA 2000 network](https://digitalyacht.support/tutorials/how-to-create-an-nmea-2000-network/) | Concise list of the key networking rules (length limits, LEN, terminators, etc.). |
| [Digital Yacht – NMEA 2000 Cabling & Getting Started Guide (PDF)](https://digitalyacht.net/wp-content/uploads/2022/01/NMEA-2000-Cabling-and-Connectors-with-Getting-Started-Guide.pdf) | Good visual reference for connectors and basic network construction. |

---

## Installation & Best-Practice Guides

| Resource | Notes |
|----------|-------|
| [NMEA 2000 for Beginners: From Network Design to Installation](https://renhotec-industrial.com/nmea-2000-guide.html) | Step-by-step design and installation checklist style guide. |
| [Yachting Pages – Guide to NMEA 2000 installations for yachts](https://www.yachting-pages.com/articles/a-guide-to-nmea-2000-installations-for-yachts.html) | Practical installation overview with thanks to NMEA & Actisense. |
| [Ocean Science Technology – NMEA 0183 and NMEA 2000 Guide](https://www.oceansciencetechnology.com/resources/nmea-2000-nmea-0183-guide/) | Good comparison table and bridging notes. |

---

## Technical Deep Dives

| Resource | Notes |
|----------|-------|
| [Gary C. Kessler – Overview of the NMEA 0183 Standard (PDF)](https://www.garykessler.net/gck/NMEA%200183_Kessler.pdf) | More formal technical overview of sentences, physical layer, and protocol details. |
| [Actisense – Sharing NMEA data over Ethernet](https://actisense.com/news/how-to-share-nmea-0183-and-nmea-2000-data-over-ethernet/) | Useful when we later consider Wi-Fi / phone integration and data sharing. |

---

## Hardware We Are Using / Evaluating

| Resource | Notes |
|----------|-------|
| [Waveshare ESP32-S3-Touch-LCD-7](https://www.waveshare.com/esp32-s3-touch-lcd-7.htm) | 7″ colour capacitive touch prototype (CAN + RS485 headers). Interactive cabin terminal. |
| [13.3″ Spectra 6 E-Ink Display (Seeed)](https://www.seeedstudio.com/13-3inch-Six-Color-eInk-ePaper-Display-with-1200x1600-Pixels-p-6569.html) | 1200×1600 six-colour e-paper panel. Excellent daylight readability, ultra-low static power. |
| [XIAO ePaper Display Board EE02 (Seeed)](https://www.seeedstudio.com/XIAO-ePaper-Display-Board-ESP32-S3-EE02-p-6639.html) | ESP32-S3 driver board for the 13.3″ Spectra 6. Battery connector, charging support, Wi-Fi/BLE. |
| [Getting Started with EE02 (Seeed Wiki)](https://wiki.seeedstudio.com/getting_started_with_ee02/) | Official setup, battery notes, and examples for the EE02 + 13.3″ panel. |
| [XIAO ESP32S3 & Wio-SX1262 Kit (Seeed)](https://www.seeedstudio.com/Wio-SX1262-with-XIAO-ESP32S3-p-5982.html) | Compact LoRa / Meshtastic kit for optional mesh experiments. |

---

## Related Open / Semi-Open Projects

| Project | Relevance |
|---------|-----------|
| **[Tritium](https://tritium.co.uk/projects)** | ESP32 NMEA WiFi bridge + touchscreen AIS display. Excellent UX patterns. **Firmware is closed-source** — see [doc 16](16-related-projects-tritium.md) for full analysis. |
| [Sailor Hat ESP32 (Hat Labs)](https://docs.hatlabs.fi/sh-esp32/) | Open hardware ESP32 board designed for marine 12/24 V systems with NMEA 2000 compatibility. |
| [Signal K](https://signalk.org/) | Open marine data exchange format and server ecosystem. Strong long-term integration candidate. |
| [canboat](https://github.com/canboat/canboat) | NMEA 2000 PGN decoder utilities (reference implementation). |
| [NMEA2000 library (ttlappalainen)](https://github.com/ttlappalainen/NMEA2000) | Widely used ESP32 / Arduino NMEA 2000 library. |
| [Boatingwiththebaileys Marine Displays](https://github.com/Boatingwiththebaileys/Marine-Displays) | Open-source ESP32 marine instrument displays using Signal K / NMEA 2000. |
| **[Bareboat Necessities / BBN Marine OS](https://bareboat-necessities.github.io/my-bareboat/)** | Full Raspberry Pi marine computer stack (Signal K, OpenCPN, AvNav, PyPilot, etc.). Feature-rich and mature, but significantly more complex and higher power/cost than our ESP32 approach. Best viewed as a possible future “full boat computer” option rather than the core of this project. |

---

## Key Concepts We Care About for the Boat Terminal

- **NMEA 0183** is fundamentally single-talker / multi-listener. Multiple talkers require a multiplexer or careful timing.
- **NMEA 2000** uses CAN and therefore has proper arbitration — much safer for a shared bus.
- Differential signalling (RS-422/RS-485 style) is preferred for noise immunity.
- Galvanic isolation is strongly recommended when connecting any new device to an existing boat network.
- Always respect backbone length, drop length, and termination rules on NMEA 2000.
- WiFi NMEA (UDP 2000 / TCP 10110) is a practical and widely used complement to hard-wired interfaces.
- E-Ink (Spectra 6) is excellent for high-resolution static or slowly updating content; it is not suitable for fluid interactive maps.

---

## Project-Specific Notes

- The 7″ prototype board has both **CAN** (for NMEA 2000) and **RS485** (for NMEA 0183).
- For permanent installation we will still require proper isolation on both interfaces.
- Listening is straightforward. Transmitting onto a live boat network needs more care (especially on NMEA 0183).
- Supporting **both** hard-wired and WiFi NMEA input is recommended (see Tritium analysis).
- The 13.3″ E-Ink is intentionally used for persistent status and high-quality map/route overviews pushed from the phone (on demand or every 30–60 s).

---

*This list will grow as the project progresses. Prefer primary manufacturer documentation and well-regarded technical sources over random forum posts.*
