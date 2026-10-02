#pragma once

#include <cstddef>
#include <cstdint>

/* This boat's maintenance book on the TF card (FAT32).
 *   /maint/<16 hex>/item.json
 *   /maint/<16 hex>/1.jpg …
 * See docs/18-maintenance-handoff-and-auth.md.
 * LMYC_SAILBOAT_ID may be set in secrets.h before this header is included. */

#ifndef LMYC_SAILBOAT_ID
#define LMYC_SAILBOAT_ID 0
#endif

static constexpr size_t kMaintIdHex = 16;
static constexpr size_t kMaintTitleMax = 200;
static constexpr size_t kMaintDescMax = 2000;
static constexpr size_t kMaintReporterMax = 80;
static constexpr size_t kMaintPhotoMax = 5;
static constexpr size_t kMaintPhotoBytesMax = 500 * 1024;
static constexpr size_t kMaintPhotoNoteMax = 120;

struct MaintItem {
    char id[kMaintIdHex + 1];
    uint32_t wp_id;
    uint32_t sailboat_id;
    char title[kMaintTitleMax + 1];
    char description[kMaintDescMax + 1];
    char category[32];
    char priority[16];
    char status[24];
    char reporter[kMaintReporterMax + 1];
    char created[32];
    char modified[32];
    char sync_state[16];
    uint8_t photo_count;
    /* note[0] belongs to 1.jpg. Empty until the phone sends one. */
    char photo_note[kMaintPhotoMax][kMaintPhotoNoteMax + 1];
};

/* Call after SD.begin() succeeds. Creates /maint. */
bool maint_store_begin(void);
bool maint_store_ready(void);
/* Increases after a successful item.json write. Unchanged while the book
 * on the card is the same. */
uint32_t maint_store_generation(void);

/* Copy the text fields the phone posts. Does not create an id. */
bool maint_item_from_json(const char *json, MaintItem *out);

/* Fills id, sailboat_id, status=reported, sync_state=pending. */
bool maint_store_create(MaintItem *item);

/* id must be 16 hex chars. */
bool maint_store_load(const char *id, MaintItem *out);
bool maint_store_save(const MaintItem *item);

/* Open /maint. Returns false when there is no next directory. */
bool maint_store_list_start(void);
bool maint_store_list_next(MaintItem *out);
void maint_store_list_end(void);

/* Stream a photo onto the card. n is 1-based. */
bool maint_store_photo_begin(const char *id, uint8_t n);
bool maint_store_photo_write(const uint8_t *data, size_t n);
bool maint_store_photo_finish(void);
void maint_store_photo_abort(void);

/* Path of an existing photo, or false. */
bool maint_store_photo_path(const char *id, uint8_t n, char *out, size_t cap);

/* Stores the note beside photo n (1-based) in item.json. */
bool maint_store_set_photo_note(const char *id, uint8_t n, const char *note);

bool maint_category_ok(const char *category);
bool maint_priority_ok(const char *priority);
bool maint_status_ok(const char *status);
bool maint_id_ok(const char *id);

/* Club JSON for one item: wp_id, title, category, priority, and optional
 * status, description, reporter, created, modified, sailboat_id. */
bool maint_club_item_from_json(const char *json, MaintItem *out);

/* Website modified time wins. A pending boat note is left as skipped.
 * action is added, updated, kept, or skipped. out_id receives the card id. */
bool maint_store_reconcile(const MaintItem *incoming, char *out_id, size_t id_cap,
                           const char **action);
