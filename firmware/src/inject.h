#pragma once

/* Slice C-lite: phone GNSS may enter the parser only when LOCAL is armed
 * on the 7″. Never RS485/CAN TX. */

void inject_init(void);
bool inject_local_armed(void);
void inject_set_local(bool armed);

/* True if this sentence is allowed RMC/GGA/VTG/GLL. */
bool inject_is_gnss_sentence(const char *sentence);
