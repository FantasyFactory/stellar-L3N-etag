#pragma once
#include <stdint.h>

// Circular diagnostic log kept in retention RAM, so it survives deep sleep
// between BLE events (this device sleeps constantly, a non-retained buffer
// would lose everything on the very next sleep cycle). Meant as a stand-in
// for UART while the TX pin isn't wired up: every existing printf() call in
// the firmware feeds this for free once enabled (see u_printf.c), and it is
// readable over BLE via the RxTx characteristic (cmd_parser.c, cmds
// 0xE8/0xE9/0xEA) and the web UI (web_tools/index.html).
//
// Logging is OFF by default: formatting/copying strings on every printf()
// costs cycles we don't want to pay while just running the clock. Turn it
// on only while actively debugging (cmd 0xE8).

// NB on sizing: this lives in the 32K deep-sleep-retention region, which
// also has to hold .ram_code (currently ~17KB) before .text even starts --
// real headroom measured on this build is ~1.4KB, not the ~19KB a naive
// "32K minus retention_data" reading would suggest. 1KB leaves a safety
// margin; if more is needed, the bigger lever is trimming _attribute_ram_code_
// off functions that don't actually need RAM-speed execution (see epd.c),
// not shrinking this buffer further.
#define DBG_LOG_SIZE 1024

void dbg_log_set_enabled(uint8_t on);
uint8_t dbg_log_get_enabled(void);

// Appends a NUL-terminated string if logging is enabled; no-op otherwise.
void dbg_log_write(const char *s);

// Discards all buffered log data and resets the read cursor.
void dbg_log_clear(void);

// Copies up to max_len unread bytes into out_buf, advancing the read
// cursor. *out_remaining is set to how many bytes are still unread after
// this call (0 = caller has read everything currently available).
// Returns the number of bytes actually copied into out_buf.
uint8_t dbg_log_read_chunk(uint8_t *out_buf, uint8_t max_len, uint32_t *out_remaining);
