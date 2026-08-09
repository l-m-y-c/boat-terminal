# 15 – Useful Resources

Curated reference material for the LMYC Boat Terminal project.

Focus is on practical understanding of **NMEA 0183**, **NMEA 2000**, wiring, isolation, and marine networking so that club members and developers can make good decisions.

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

## Key Concepts We Care About for the Boat Terminal

- **NMEA 0183** is fundamentally single-talker / multi-listener. Multiple talkers require a multiplexer or careful timing.
- **NMEA 2000** uses CAN and therefore has proper arbitration — much safer for a shared bus.
- Differential signalling (RS-422/RS-485 style) is preferred for noise immunity.
- Galvanic isolation is strongly recommended when connecting any new device to an existing boat network.
- Always respect backbone length, drop length, and termination rules on NMEA 2000.

---

## Project-Specific Notes

- The prototype board (ESP32-S3-Touch-LCD-7) has both **CAN** (for NMEA 2000) and **RS485** (for NMEA 0183).
- For permanent installation we will still require proper isolation on both interfaces.
- Listening is straightforward. Transmitting onto a live boat network needs more care (especially on NMEA 0183).

---

*This list will grow as the project progresses. Prefer primary manufacturer documentation and well-regarded technical sources over random forum posts.*
