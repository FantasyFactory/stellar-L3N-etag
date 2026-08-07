#include "corrado.h"
#include "main.h"
#include "debug_log.h"

RAM char dbg_log_buf[DBG_LOG_SIZE];
RAM uint32_t dbg_log_total_written = 0; // total bytes ever appended (monotonic, wraps at 4G)
RAM uint32_t dbg_log_read_pos = 0;      // read cursor, in the same byte-count space
RAM uint8_t dbg_log_enabled = 1;        // enabled at boot

void dbg_log_set_enabled(uint8_t on)
{
    dbg_log_enabled = on ? 1 : 0;
}

uint8_t dbg_log_get_enabled(void)
{
    return dbg_log_enabled;
}

void dbg_log_clear(void)
{
    dbg_log_total_written = 0;
    dbg_log_read_pos = 0;
}

void dbg_log_write(const char *s)
{
    if (!dbg_log_enabled || !s)
        return;
    while (*s)
    {
        dbg_log_buf[dbg_log_total_written % DBG_LOG_SIZE] = *s++;
        dbg_log_total_written++;
    }
}

uint8_t dbg_log_read_chunk(uint8_t *out_buf, uint8_t max_len, uint32_t *out_remaining)
{
    // If the reader fell far enough behind that unread bytes were already
    // overwritten by newer log data, skip forward to the oldest byte the
    // ring buffer still actually holds.
    if (dbg_log_total_written - dbg_log_read_pos > DBG_LOG_SIZE)
        dbg_log_read_pos = dbg_log_total_written - DBG_LOG_SIZE;

    uint32_t available = dbg_log_total_written - dbg_log_read_pos;
    uint8_t n = (available < max_len) ? (uint8_t)available : max_len;

    for (uint8_t i = 0; i < n; i++)
        out_buf[i] = dbg_log_buf[(dbg_log_read_pos + i) % DBG_LOG_SIZE];

    dbg_log_read_pos += n;
    *out_remaining = dbg_log_total_written - dbg_log_read_pos;
    return n;
}
