# 10 – Costs & Rollout

## Hardware Cost per Boat (Estimate)

| Item | Low | High |
|------|-----|------|
| Waveshare terminal | $55 | $80 |
| Enclosure & mounting | $15 | $35 |
| Power & NMEA cabling | $20 | $50 |
| Misc (fuse, labels, etc.) | $10 | $15 |
| **Total** | **≈ $100** | **≈ $180** |

A five-boat fleet would be roughly **CAD $500–900** in hardware.

## Development Effort

- Firmware (terminal): moderate — NMEA + LVGL UI + BLE pairing
- Phone app / PWA: moderate — authentication, pairing, forms
- Backend changes: small — a few new endpoints
- Documentation & testing: ongoing volunteer effort

Most of the work can be done by interested technical members.  
No large external contract is required for a pilot.

## Suggested Rollout Phases

### Phase 0 – Proposal Review (Current)
- Club members and Board review this documentation
- Collect feedback and decide whether to proceed

### Phase 1 – Single Boat Pilot
- Choose one boat with reasonable instrument access
- Install one terminal
- Implement basic NMEA display + simple pairing
- Run for 2–3 months with a small group of volunteer members
- Gather real-world feedback

### Phase 2 – Refine & Expand
- Improve UI and reliability based on pilot results
- Add issue reporting and hour logging
- Roll out to remaining boats if the pilot is successful

### Phase 3 – Nice-to-Haves
- Richer phone features
- Maintenance dashboards
- Optional Signal K integration

## Risk vs Reward

| Risk | Mitigation |
|------|------------|
| Members find pairing awkward | Keep manual fallback; iterate on UX |
| Hardware fails in cabin environment | Choose robust board; simple replaceable design |
| Development stalls | Keep scope small; pilot first |
| Low adoption | Demonstrate clear value (instruments + easy logging) |

The financial risk is low. The main investment is volunteer time.
