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
This proposal explores a simple next step: a small, always-available touchscreen in the cabin of each boat that works hand-in-hand with the tools members already use.

When you arrive for a booked sail, you scan a QR code on the terminal with your phone.  
The terminal becomes the boat’s digital logbook and maintenance book for the duration of your booking, shows live instruments, and gives quick access to boat-specific reference information.  
Your phone continues to handle full charts, weather, photos, and everything else you already rely on.  
When the booking ends, the pairing is released.

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

We considered turning the terminal into a full chartplotter (large display, NOAA/Canadian raster charts, route planning).  
**We deliberately chose not to.**  
Members are better served by continuing to trust a professional chartplotter correctly paired with the autopilot.

The terminal may still listen to waypoint and autopilot data and offer basic navigation *awareness*, but it will not replace those systems or issue autopilot commands.

---

### Proposal Documents

| Document | What’s inside |
| :--- | :--- |
| [01 – Executive Summary](docs/01-executive-summary.md) | One-page overview |
| [02 – Goals & Opportunities](docs/02-problem-and-goals.md) | Purpose, goals, and explicit non-goals |
| [03 – System Architecture](docs/03-system-architecture.md) | Terminal + phone + existing booking system |
| [04 – Hardware](docs/04-hardware.md) | Terminal options, isolated interfaces, mounting |
| [05 – NMEA Connectivity](docs/05-nmea-connectivity.md) | Data sources and **bus safety requirements** |
| [06 – Phone App & Pairing](docs/06-phone-app-and-pairing.md) | QR pairing flow and security model |
| [07 – Software Frameworks](docs/07-software-frameworks.md) | Practical technology choices |
| [08 – User Flows](docs/08-user-flows.md) | Journeys focused on co-op value |
| [09 – Security & Privacy](docs/09-security-and-privacy.md) | Pairing security, occupancy privacy, edge cases |
| [10 – Costs & Rollout](docs/10-costs-and-rollout.md) | Realistic numbers and phased approach |
| [11 – Open Questions](docs/11-open-questions.md) | Go/no-go items and discussion points |
| [12 – Next Steps](docs/12-next-steps.md) | Careful path forward |
| [13 – Risks & Mitigations](docs/13-risks-and-mitigations.md) | Serious risks and how they will be handled |

---

### Current Status

This is a **proposal only**.  
No hardware has been ordered and no software has been written yet.  
The documents now include explicit treatment of NMEA bus safety, pairing security, firmware maintenance ownership, navigation scope, and other issues that must be resolved before any boat is modified.

---

### Feedback welcome

The best proposals improve when real sailors and careful reviewers look at them.

- Open a [GitHub Issue](https://github.com/thisdidwork/lmyc/issues)
- Raise it at a meeting
- Or talk with the working group once one is formed

Everyone contributes, everyone sails, everyone smiles.

<p align="center">
  <img src="https://lmyc.ca/wp-content/uploads/2025/07/cropped-LMYCLogoTransparent.png" alt="LMYC" width="60">
</p>
