# 09 – Security & Privacy

## Design Goals

- Pairing should be limited to the person who currently has a valid booking
- No long-term personal data should live permanently on the boat terminal
- Instrument data is local to the boat and does not need to leave the vessel
- Club backend remains the source of truth for membership and bookings

## Pairing Security Model (Proposed)

1. Only the club backend can issue a pairing token
2. Token is short-lived (e.g. booking duration + small grace period)
3. Token is bound to a specific boat and member
4. Transfer of the token happens over BLE (or local Wi-Fi) with standard encryption
5. Terminal rejects tokens that are expired, already used, or for a different boat
6. On check-out or expiry the terminal securely forgets the pairing

## Data Stored on the Terminal

**Allowed**
- Current pairing state (member display name, expiry)
- Recent instrument values (volatile)
- Short local log of hours or issues (until uploaded)
- Device configuration (NMEA settings, brightness, etc.)

**Not stored long-term**
- Member passwords or account credentials
- Full booking history
- Precise track logs
- Photos

## Privacy Considerations

- The system does **not** continuously track boat location for the club
- Position data shown on the terminal stays local unless the member explicitly shares it
- Issue reports and hour logs are associated with the booking, not used for surveillance

## Physical Security

- Terminal is in the cabin; normal boat security practices apply
- No high-value components that would attract theft beyond any other small electronics
- Firmware can be locked / secure-boot enabled if the club desires

## Failure Modes

| Situation | Behaviour |
|-----------|-----------|
| Phone lost or dead | Terminal still shows instruments; manual check-out possible |
| Backend unreachable | Previously issued token still works until expiry; new pairings delayed |
| Terminal power loss | Returns to unpaired state on reboot; no sensitive data retained |
