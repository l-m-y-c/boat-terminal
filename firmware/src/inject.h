#pragma once

/* Slice C-lite: phone GNSS may enter the parser only when LOCAL is armed
 * on the 7″. Never RS485/CAN TX. */

void inject_init(void);
bool inject_local_armed(void);
void inject_set_local(bool armed);

/* True if this sentence is allowed RMC/GGA/VTG/GLL. */
bool inject_is_gnss_sentence(const char *sentence);

/* HTTP task: copy one sentence for loop() to parse. The parser, gateway and
 * BLE tap are loop()-only, so the HTTP task must not call nmea_feed_line().
 * Returns false when the queue is full. */
bool inject_submit(const char *sentence);

/* loop(): feed the queued phone sentences to the parser. */
void inject_poll(void);
