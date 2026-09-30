# 12 – Next Steps

## Before Any Hardware Is Purchased

1. Collect feedback on this tightened proposal
2. Form a small working group of technical and operational members
3. Explicitly answer the **go / no-go** questions (maintenance ownership, Board comfort with network modifications, pilot boat selection)
4. Confirm the isolated NMEA 2000 approach and liability posture with the Board

## If the Club Proceeds

5. Survey the chosen pilot boat’s instrument network in detail
6. Design and bench-test the isolated interface **before** connecting to the boat
7. Implement the minimum vertical slice:
   - Powered terminal showing live instruments
   - Secure pairing with terminal identity verification
   - Explicit confirmation that instrument display survives token expiry
8. Run a limited pilot (2–3 months) with volunteer members
9. Review results, including firmware update practicality and real-world pairing experience
10. Decide on wider rollout only after the pilot has answered the hard questions

## On the bench (2026-09)

Hardware is here and QR + BLE pairing works. The firmware runs the BoatLink bench from [17](17-android-boat-roadmap.md): NMEA simulator, sentence console, TCP 10110, SoftAP, chart snapshots, and phone GNSS that the 7″ must arm. Real RS485 listen is still ahead.

Writing onto a live backbone, N2K install, and fleet rollout still require the Board items above.

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
