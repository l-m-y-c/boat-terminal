<p align="center">
  <img src="https://lmyc.ca/wp-content/uploads/2025/07/cropped-LMYCLogoTransparent.png" alt="LMYC Logo" width="120">
</p>

<h1 align="center">LMYC Boat Terminal</h1>

<p align="center">
  <strong>A cabin companion that extends the LMYC experience onto the boat</strong>
</p>

<p align="center">
  <em>Affordable • Shared • Member-driven</em>
</p>

---

### Extending what we already do well

LMYC already gives members a clear, web-based way to view the fleet, check the calendar, and book boats.  
This proposal explores a simple next step: a small, always-available touchscreen in the cabin of each boat that works hand-in-hand with the tools members already use.

When you arrive for a booked sail, you pair your phone with the boat’s terminal.  
The terminal shows live instrument data and confirms your reservation.  
Your phone continues to handle charts, weather, photos, and everything else you already rely on.  
When the booking ends, the pairing is released.

The goal is a seamless experience that feels like a natural part of sailing with LMYC — not a separate system.

---

### Why this fits LMYC

| | |
| :--- | :--- |
| **Builds on the existing booking system** | Works with the calendar and boat pages members already know |
| **Keeps costs reasonable** | Hardware target in the low-to-mid hundreds per boat |
| **Stays open and maintainable** | Designed so more than one volunteer can understand and improve it |
| **Respects how members actually sail** | Phone remains the rich interface; the terminal is the always-on local companion |
| **Safety-conscious** | Isolated NMEA interfaces and clear failure modes are required, not optional |

---

### Proposal Documents

| Document | What’s inside |
| :--- | :--- |
| [01 – Executive Summary](docs/01-executive-summary.md) | One-page overview |
| [02 – Goals & Opportunities](docs/02-problem-and-goals.md) | How this builds on current LMYC tools |
| [03 – System Architecture](docs/03-system-architecture.md) | Terminal + phone + existing booking system |
| [04 – Hardware](docs/04-hardware.md) | Terminal options, isolated interfaces, mounting |
| [05 – NMEA Connectivity](docs/05-nmea-connectivity.md) | Data sources and **bus safety requirements** |
| [06 – Phone App & Pairing](docs/06-phone-app-and-pairing.md) | Pairing flow and security model |
| [07 – Software Frameworks](docs/07-software-frameworks.md) | Practical technology choices |
| [08 – User Flows](docs/08-user-flows.md) | What it feels like on a typical sail |
| [09 – Security & Privacy](docs/09-security-and-privacy.md) | Pairing security, occupancy privacy, edge cases |
| [10 – Costs & Rollout](docs/10-costs-and-rollout.md) | Realistic numbers and phased approach |
| [11 – Open Questions](docs/11-open-questions.md) | Go/no-go items and discussion points |
| [12 – Next Steps](docs/12-next-steps.md) | Careful path forward |
| [13 – Risks & Mitigations](docs/13-risks-and-mitigations.md) | Serious risks and how they will be handled |

---

### Current Status

This is a **proposal only**.  
No hardware has been ordered and no software has been written yet.  
The documents now include explicit treatment of NMEA bus safety, pairing security, firmware maintenance ownership, and other issues that must be resolved before any boat is modified.

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
