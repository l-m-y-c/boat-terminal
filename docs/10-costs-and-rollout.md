# 10 – Costs & Rollout

## Hardware Cost per Boat (Revised Estimate)

| Item | Low | High |
|------|-----|------|
| Waveshare terminal | $55 | $80 |
| Isolated NMEA 2000 interface | $40 | $90 |
| Enclosure & mounting | $15 | $35 |
| Power & fusing | $10 | $20 |
| Certified drop cable / adapters | $25 | $60 |
| Misc | $10 | $15 |
| **Hardware subtotal** | **≈ $155** | **≈ $300** |

## Additional Costs Not in the BOM

| Item | Notes |
|------|-------|
| Install & commissioning labour | Volunteer time; allow half a day to a full day per boat depending on access |
| Survey of existing NMEA wiring | Required before hardware is ordered for that boat |
| Possible small parts unique to each hull | Terminals, adapters, extra cable |
| Future firmware maintenance | Ongoing volunteer effort |

A five-boat fleet is therefore roughly **CAD $800–1,500** in hardware plus volunteer installation time.

## Boat Variability

Not every boat in the fleet has the same instrument network:

- Some may offer clean NMEA 2000
- Some may only have NMEA 0183
- Some may have limited or no networked instruments

The pilot must begin with a boat whose network is well understood. Later boats may receive a reduced feature set if full NMEA data is not practical.

## Suggested Rollout Phases

### Phase 0 – Proposal Review (Current)
- Membership and Board review the documentation
- Critical risks (NMEA isolation, pairing security, maintenance ownership) are discussed
- Decision whether to form a working group

### Phase 1 – Single Boat Pilot
- Select one boat with accessible, well-understood instrument data
- Design and test the isolated NMEA interface on the bench first
- Implement basic instrument display + secure pairing
- Run for 2–3 months with volunteer members
- Explicitly test token-expiry and offline behaviour

### Phase 2 – Refine & Decide
- Incorporate pilot feedback
- Confirm firmware update process works in practice
- Decide whether to proceed to additional boats

### Phase 3 – Controlled Expansion
- Roll out only to boats that have been surveyed
- Maintain documentation of each installation

## Risk vs Reward

The financial outlay remains modest. The larger risks are technical (bus safety) and organisational (long-term maintenance). Both are addressed in the Risks & Mitigations document and must be resolved before Phase 1 hardware is permanently installed.
