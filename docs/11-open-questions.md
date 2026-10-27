# 11 – Open Questions for Club Discussion

These items need input from members, the Board, and technical volunteers. Several are now considered **go / no-go** items rather than optional discussion points.

## Go / No-Go Items (Resolve Before Phase 1 Hardware)

1. **Who owns ongoing firmware and hardware maintenance?**  
   A single-volunteer bus factor is unacceptable once devices are wired into instrument networks. What is the succession plan?

2. **Is the Board comfortable with limited, isolated modifications to boat instrument networks?**  
   This has potential insurance and liability implications.

3. **Is the Board comfortable with an uncertified NMEA 2000 node until the open hardware items are closed?**  
   The terminal behaves as a standard NMEA 2000 node, but it is not electrically isolated from the network (onboard TJA1051 and Pololu supply), and it uses the open-source manufacturer code 2046 instead of NMEA-issued codes. Isolation needs an isolated CAN transceiver with isolated bus-side power. NMEA codes need membership (about $1,200 for a manufacturer code and $450 for a product code at 2024 member prices), and certification costs extra (about $4,000 in total for a new member, per the library author). Does the club pay for isolation only, or also for codes and certification? See [05](05-nmea-connectivity.md#hardware-compliance-open-items).

4. **Which boat is the best pilot candidate?**  
   Preference for a vessel with clean, well-documented NMEA access and a suitable cabin location.

## Product & Experience

5. Is a 4.3" screen large enough, or should we standardise on 7"?
6. The companion is the Flutter app in `mobile/`. A website-only flow remains for members at home. Login is shared. See [18](18-maintenance-handoff-and-auth.md).
7. Should the terminal show the member’s name while paired, or simply “Booked”?
8. Do we want light digital checklists in the first version?

## Technical

9. Direct NMEA versus an intermediate Signal K layer — still prefer direct for v1?
10. How should elevated access for maintenance volunteers work?
11. What is the preferred firmware update path (local USB vs marina Wi-Fi)?

## Process

12. How do we handle boats that have little or no usable instrument data?
13. Should pairing be encouraged but always optional for using the boat?

Please add further questions as GitHub Issues or raise them in meetings.
