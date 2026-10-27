# 12 – Next Steps

## Before Any Hardware Is Purchased

1. Collect feedback on this tightened proposal
2. Form a small working group of technical and operational members
3. Explicitly answer the **go / no-go** questions (maintenance ownership, Board comfort with network modifications, pilot boat selection)
4. Confirm the NMEA 2000 approach and liability posture with the Board: the terminal is a standard NMEA 2000 node that sends only network-management messages, but it is not yet isolated and not certified (see [05](05-nmea-connectivity.md#hardware-compliance-open-items))

## If the Club Proceeds

5. Survey the chosen pilot boat’s instrument network in detail
6. Fit and bench-test an isolated CAN transceiver (TI ISO1042 / ISO1050 class, or ADI ADM3053) with isolated bus-side power in place of the onboard TJA1051. Until it is fitted, the prototype goes on a backbone only bus-powered with nothing else that shares ground connected
7. Decide whether to apply for NMEA membership and NMEA-issued manufacturer and product codes (about $1,200 and $450 at 2024 member prices), and whether to pay for certification (about $4,000 in total for a new member, per the library author). Until then the terminal uses the open-source manufacturer code 2046 and is not certified
8. Implement the minimum vertical slice:
   - Powered terminal showing live instruments
   - Secure pairing with terminal identity verification
   - Explicit confirmation that instrument display survives token expiry
9. Run a limited pilot (2–3 months) with volunteer members
10. Review results, including firmware update practicality and real-world pairing experience
11. Decide on wider rollout only after the pilot has answered the hard questions

## On the bench (2026-09)

Hardware is here and QR + BLE pairing works. The firmware runs the BoatLink bench from [17](17-android-boat-roadmap.md): NMEA simulator, sentence console, TCP 10110, SoftAP, chart snapshots, and phone GNSS that the 7″ must arm. Real RS485 listen is still ahead.

Joining a live backbone as an NMEA 2000 node, the N2K install, and fleet rollout still require the Board items above. The first run is planned in [21](21-first-boat-run.md).

## Agreed next (2026-09-30)

Member priority is the maintenance loop in **[18 – Maintenance handoff & login](18-maintenance-handoff-and-auth.md)**:

1. This boat's maintenance book on the TF card, photos included, readable with no cell service.
2. Phone login via OAuth 2.0 authorization code + PKCE against the existing lmyc.ca users.
3. A JSON API on lmyc.ca that wraps the maintenance form the club already uses.
4. The phone carries pending reports up and club-side edits down. The terminal does not hold member tokens.

The 13.3″ E-Ink stays a separate project. It shows pictures the phone sends.

## What This Repository Will Hold

- Firmware source
- Phone companion source
- Hardware notes and per-boat installation records
- Decision log and risk register
- The BoatLink protocol and slice plan in doc 17
- The maintenance handoff, login, and on-boat copy in doc 18

---

**Thank you for reading.**  
Serious feedback — especially on safety, security, and long-term ownership — is welcome and necessary.
