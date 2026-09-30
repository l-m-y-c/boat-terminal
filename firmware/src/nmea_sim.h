#pragma once

/* Canned 1 Hz talker for Slice A bench work. UI must show FIX: SIM. */

void nmea_sim_init(void);
void nmea_sim_set_enabled(bool enabled);
bool nmea_sim_enabled(void);

/* When false, skip RMC/GGA so a LOCAL phone fix can own GNSS. Depth/wind stay. */
void nmea_sim_set_emit_gnss(bool emit);
bool nmea_sim_emit_gnss(void);

/* Call from loop(); emits RMC/GGA/DPT/MWV through nmea_feed_line at 1 Hz. */
void nmea_sim_poll(void);
