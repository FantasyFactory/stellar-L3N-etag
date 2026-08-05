#pragma once
#include <stdint.h>

// Unified driver for the SSD1675/SSD1680-compatible EPD controller family.
//
// These controllers (confirmed for our panel: HINK-E0213A200, see
// docs/HINK-E0213A200-A0_Spec_bwr.pdf) share one command set. Panels differ
// only in the RAM window (= resolution), whether a RED plane exists, and a
// couple of init constants (border waveform, gate MUX). Everything else
// (reset/init/LUT/display/sleep sequence, temperature register format) is
// identical, so a single parametrized implementation replaces what used to
// be four near-duplicate .c files (epd_bwr_213.c, epd_bwr_154.c,
// epd_bwr_296.c, and now this one).
//
// epd_bw_213_ice.c is NOT folded in here: its init sequence genuinely
// differs (extra ACVCOM step, two-phase display-update-control) and we have
// no hardware to verify a merged version behaves identically, so it stays a
// separate implementation behind the same epd_driver_t interface (see
// epd.c). epd_bw_213.c (UC8151, a different controller entirely) is also
// kept separate for the same reason.

typedef struct
{
    const char *name;
    uint16_t width;   // logical width in px (RAM Y-window span)
    uint8_t height;    // logical height in px, padded to a multiple of 8 (RAM X-window span)
    uint8_t x_end;     // 0x44 end byte = height/8 - 1 (start is always 0)
    uint8_t y_start_lo, y_start_hi; // 0x45 start (9-bit, split lo/hi)
    uint8_t y_end_lo, y_end_hi;     // 0x45 end
    uint8_t mux_lo, mux_hi, mux_scan; // 0x01 driver output control bytes
    uint8_t border_waveform;          // 0x3C value
    uint8_t has_red;                  // whether to drive the RED RAM plane (cmd 0x26)
} epd_ssd168x_profile_t;

// Known panel profiles. Index matches the "forced EPD model" numbering used
// by the BLE force-model command (0xE0) and persisted in flash settings, see
// epd.c / cmd_parser.c.
extern const epd_ssd168x_profile_t epd_ssd168x_hink_213bwr; // confirmed: our actual panel
extern const epd_ssd168x_profile_t epd_ssd168x_bwr_296;
extern const epd_ssd168x_profile_t epd_ssd168x_bwr_154;     // kept for completeness, not wired into autodetect

// Family-level detect: true if an SSD1680-class controller answers on the
// bus (Status Bit Read, cmd 0x2F, Chip ID bits POR = 01). This can only
// confirm the controller family, NOT which panel/resolution is attached --
// different glass sizes share the same die and register interface, so the
// specific profile has to be a stored/configured choice (see
// settings.epd_profile in flash.h), not something guessed at boot.
uint8_t EPD_SSD168x_detect_family(void);

// Reads the temperature register (cmd 0x1B) and returns it as a raw signed
// Q8.8 fixed-point value (divide by 256.0 to get degrees C). The controller
// itself stores a 12-bit value V with DegC = V/16 (datasheet section 8,
// two's complement, sign in bit 11); the two bytes read back are V shifted
// left by 4, so combined as one 16-bit word that's 16*V, i.e. Q8.8.
int16_t EPD_SSD168x_read_temp_raw(const epd_ssd168x_profile_t *p);

// Full display sequence. If red_image is NULL, or the profile has no red
// plane, the RED RAM write is skipped -- but if red_image is non-NULL on a
// profile without a red plane, its bits are merged (OR'd) into the black
// plane before sending, so a "red" pixel degrades to black instead of being
// silently dropped.
uint8_t EPD_SSD168x_Display(const epd_ssd168x_profile_t *p, unsigned char *image, unsigned char *red_image, int size, uint8_t full_or_partial);

void EPD_SSD168x_set_sleep(const epd_ssd168x_profile_t *p);
