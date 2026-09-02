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

## On the bench now (2026-09)

Hardware is here and QR + BLE pairing works. Software work no longer waits on the club go/no-go for *listening* and phone UX.

Follow **[17 – Android Boat / BoatLink Roadmap](17-android-boat-roadmap.md)**:

- Slice A — NMEA simulator + sentence console + TCP 10110 (no boat wire)
- Slice B — SoftAP gateway so Navionics can subscribe
- Slice C — gated phone GNSS, local only
- Later slices — real 0183 listen, SD log, snapshots, isolated N2K listen

Writing onto a live backbone, N2K install, and fleet rollout still require the Board items above.

## What This Repository Will Hold

- Firmware source
- Phone companion source
- Hardware notes and per-boat installation records
- Decision log and risk register
- The BoatLink protocol and slice plan in doc 17

---

**Thank you for reading.**  
Serious feedback — especially on safety, security, and long-term ownership — is welcome and necessary.
