<p align="center">
  <img src="https://lmyc.ca/wp-content/uploads/2025/07/cropped-LMYCLogoTransparent.png" alt="LMYC Logo" width="120">
</p>

<h1 align="center">LMYC Boat Terminal</h1>

<p align="center">
  <strong>The boat’s digital logbook, maintenance book, and cabin companion</strong>
</p>

<p align="center">
  <em>Affordable • Shared • Member-driven</em>
</p>

---

### Extending what we already do well

LMYC already gives members a clear, web-based way to view the fleet, check the calendar, and book boats.  
This proposal explores a simple next step that works hand-in-hand with the tools members already use.

**The phone remains the powerhouse and primary user interface.**  
On the boat we add two complementary displays:

- A fast **7″ colour touch terminal** for check-in, logbook, live instruments, and quick reference
- A large **13.3″ E-Ink status board** for always-on, daylight-readable overview (including map context) that draws almost no power between updates

When you arrive for a booked sail you scan a QR code. The system becomes the boat’s digital logbook and maintenance book for the duration of your booking. Your phone continues to handle full charts, weather, photos, and detailed interaction. When the booking ends, the pairing is released.

The goal is a seamless experience that strengthens the co-operative — not a separate navigation system.

---

### Why this fits LMYC

| | |
| :--- | :--- |
| **Builds on the existing booking system** | Works with the calendar and boat pages members already know |
| **Solves real co-op problems** | Clean handovers, consistent logging, visible maintenance, boat knowledge that stays with the vessel |
| **Keeps costs reasonable** | Hardware target in the low-to-mid hundreds per boat |
| **Stays open and maintainable** | Designed so more than one volunteer can understand and improve it |
| **Safety-conscious** | Isolated NMEA interfaces and clear failure modes are required |
| **Respects professional tools** | Navigation and autopilot control remain with proper chartplotters |

---

### Navigation scope – clear decision

We considered turning the terminal into a full chartplotter.  
**We deliberately chose not to.**  
Members are better served by continuing to trust a professional chartplotter correctly paired with the autopilot.

The system may still listen to waypoint and autopilot data and can push high-quality static or slowly updating map overviews to the large E-Ink board (on demand or every 30–60 seconds). It will not replace those systems or issue autopilot commands.

---

### Proposal Documents

| Document | What’s inside |
| :--- | :--- |
| [01 – Executive Summary](docs/01-executive-summary.md) | One-page overview |
| [02 – Goals & Opportunities](docs/02-problem-and-goals.md) | Purpose, goals, and explicit non-goals |
| [03 – System Architecture](docs/03-system-architecture.md) | Phone + dual on-boat displays + booking system |
| [04 – Hardware](docs/04-hardware.md) | 7″ interactive terminal + 13.3″ E-Ink status board |
| [05 – NMEA Connectivity](docs/05-nmea-connectivity.md) | Data sources and **bus safety requirements** |
| [06 – Phone App & Pairing](docs/06-phone-app-and-pairing.md) | QR pairing flow and security model |
| [07 – Software Frameworks](docs/07-software-frameworks.md) | Practical technology choices |
| [08 – User Flows](docs/08-user-flows.md) | Journeys focused on co-op value |
| [09 – Security & Privacy](docs/09-security-and-privacy.md) | Pairing security, occupancy privacy, edge cases |
| [10 – Costs & Rollout](docs/10-costs-and-rollout.md) | Realistic numbers and phased approach |
| [11 – Open Questions](docs/11-open-questions.md) | Go/no-go items and discussion points |
| [12 – Next Steps](docs/12-next-steps.md) | Careful path forward |
| [13 – Risks & Mitigations](docs/13-risks-and-mitigations.md) | Serious risks and how they will be handled |
| [14 – Prototype Hardware](docs/14-prototype-hardware.md) | Ordered 7″ board + getting started guide |
| [15 – Useful Resources](docs/15-useful-resources.md) | Curated NMEA guides and related projects |
| [16 – Related Projects: Tritium](docs/16-related-projects-tritium.md) | Deep dive into tritium.co.uk ESP32 marine projects |
| [17 – Android Boat / BoatLink Roadmap](docs/17-android-boat-roadmap.md) | Phone-as-computer plan, protocol, and Grok Build slices |

---

### Current Status

**Hardware ordered:**
- Waveshare ESP32-S3-Touch-LCD-7 (7″ colour touch, CAN + RS485) — interactive cabin terminal
- 13.3″ Spectra 6 E-Ink + XIAO EE02 — persistent status / overview board (battery capable, outdoor-mountable)
- Supporting XIAO Expansion and LoRa (Meshtastic) kits

See [04 – Hardware](docs/04-hardware.md) and [14 – Prototype Hardware](docs/14-prototype-hardware.md) for details.

**Prototype software (this repo):** firmware for the 7″ ESP32-S3 terminal (`firmware/`) and an Android companion (`mobile/`). Bench pairing is QR + BLE OOB. **Use the app’s Find terminals** — Android Settings hide most LE-only devices. After a pull: `make pair-test`, then rescan the QR (OOB rotates every boot).

**Next build:** [17 – Android Boat / BoatLink](docs/17-android-boat-roadmap.md) — NMEA tap, Wi-Fi gateway, gated phone GNSS. Mirror the bus, not the pixels.

NMEA bus safety, pairing security, and firmware ownership remain required items before any boat is modified.

---

### Feedback welcome

The best proposals improve when real sailors and careful reviewers look at them.

- Open a [GitHub Issue](https://github.com/l-m-y-c/boat-terminal/issues)
- Raise it at a meeting
- Or talk with the working group once one is formed

Everyone contributes, everyone sails, everyone smiles.

<p align="center">
  <img src="https://lmyc.ca/wp-content/uploads/2025/07/cropped-LMYCLogoTransparent.png" alt="LMYC" width="60">
</p>
