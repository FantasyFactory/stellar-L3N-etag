#include <stdint.h>
#include "tl_common.h"
#include "main.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_ssd168x.h"
#include "drivers.h"
#include "stack/ble/ble.h"

// Same LUT content used to be duplicated identically across
// epd_bwr_213.c / epd_bwr_154.c / epd_bwr_296.c (byte-for-byte, only the
// #define'd length marker name differed). epd_bw_213_ice.c uses a
// differently-shaped LUT and is not covered by this generic driver.
//
// Structurally this is a single short voltage pulse (everything else in the
// table is 0x00) -- fine for a fast B/W-only bistable flip, but too short to
// move the red pigment, which is why red content fades on a partial refresh
// with this LUT. Tried replacing it with the panel's own OTP "Display Mode
// 2" waveform (cmd 0x22 codes 0xB9/0xCF) as an experiment: on real hardware
// that made partial refreshes stop visibly updating the panel at all after
// a few minutes (worse than the known "red fades" limitation), so it was
// reverted. Back to this LUT until a real fix is found -- see epd_myScene()
// for why red is no longer used in the calendar scene at all in the
// meantime.
#define SSD168x_LUT_LEN 50
static uint8_t ssd168x_lut_partial[] = {
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    SSD168x_LUT_LEN, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
    0x00, 0x00, 0x00,
};

// Our confirmed panel: HINK-E0213A200 (Holitech), SSD1680, 122x250 physical
// (source x gate) -> 250x128 landscape (122 real rows, padded to 128 for a
// full byte), 1-bit B/W/R. See docs/HINK-E0213A200-A0_Spec_bwr.pdf sections
// 4-5 (mechanical) and 9 (command table, RAM window commands 0x44/0x45).
const epd_ssd168x_profile_t epd_ssd168x_hink_213bwr = {
    .name = "HINK213BWR",
    .width = 250,
    .height = 128,
    .x_end = 0x0F,
    .y_start_lo = 0x28, .y_start_hi = 0x01,
    .y_end_lo = 0x2E, .y_end_hi = 0x00,
    .mux_lo = 0x28, .mux_hi = 0x01, .mux_scan = 0x01,
    .border_waveform = 0x05,
    .has_red = 1,
};

const epd_ssd168x_profile_t epd_ssd168x_bwr_296 = {
    .name = "BWR296",
    .width = 296,
    .height = 128,
    .x_end = 0x0F,
    .y_start_lo = 0x28, .y_start_hi = 0x01,
    .y_end_lo = 0x00, .y_end_hi = 0x00,
    .mux_lo = 0x28, .mux_hi = 0x01, .mux_scan = 0x01,
    .border_waveform = 0x05,
    .has_red = 1,
};

const epd_ssd168x_profile_t epd_ssd168x_bwr_154 = {
    .name = "BWR154",
    .width = 200,
    .height = 200,
    .x_end = 0x18,
    .y_start_lo = 0xC7, .y_start_hi = 0x00,
    .y_end_lo = 0x00, .y_end_hi = 0x00,
    .mux_lo = 0xC7, .mux_hi = 0x00, .mux_scan = 0x01,
    .border_waveform = 0x05,
    .has_red = 1,
};

_attribute_ram_code_ uint8_t EPD_SSD168x_detect_family(void)
{
    // Status Bit Read (cmd 0x2F): D[1:0] = Chip ID, POR = 01, with every
    // other status flag also 0 right after reset -> byte reads back 0x01.
    // Same technique already used (and confirmed working) by
    // epd_bw_213_ice.c; it only proves "this is an SSD1680-class
    // controller", not which panel/resolution is wired to it.
    EPD_WriteCmd(0x12);
    WaitMs(10);
    EPD_WriteCmd(0x2F);
    return EPD_SPI_read() == 0x01;
}

_attribute_ram_code_ static void ssd168x_init_common(const epd_ssd168x_profile_t *p)
{
    // SW Reset
    EPD_WriteCmd(0x12);
    EPD_CheckStatus_inverted(100);

    // Set Analog / Digital Block control
    EPD_WriteCmd(0x74);
    EPD_WriteData(0x54);
    EPD_WriteCmd(0x7E);
    EPD_WriteData(0x3B);

    // Booster soft start (POR defaults per datasheet cmd 0x0C)
    EPD_WriteCmd(0x0C);
    EPD_WriteData(0x8B);
    EPD_WriteData(0x9C);
    EPD_WriteData(0x96);
    EPD_WriteData(0x0F);

    // Driver output control (gate MUX)
    EPD_WriteCmd(0x01);
    EPD_WriteData(p->mux_lo);
    EPD_WriteData(p->mux_hi);
    EPD_WriteData(p->mux_scan);

    // Data entry mode setting
    EPD_WriteCmd(0x11);
    EPD_WriteData(0x01);

    // RAM X/Y window = the actual panel resolution
    EPD_WriteCmd(0x44);
    EPD_WriteData(0x00);
    EPD_WriteData(p->x_end);

    EPD_WriteCmd(0x45);
    EPD_WriteData(p->y_start_lo);
    EPD_WriteData(p->y_start_hi);
    EPD_WriteData(p->y_end_lo);
    EPD_WriteData(p->y_end_hi);

    // Border waveform control
    EPD_WriteCmd(0x3C);
    EPD_WriteData(p->border_waveform);

    // Display update control 1
    EPD_WriteCmd(0x21);
    EPD_WriteData(0x00);
    EPD_WriteData(0x80);

    // Temperature Sensor Control: select internal sensor.
    // NB: POR for this register is 0x48 (EXTERNAL sensor) per datasheet
    // cmd 0x18 -- we deliberately override to internal since no external
    // I2C temperature sensor is wired on this board (TSCL/TSDA on the FPC
    // are unconnected). If one is ever added, this is where to switch.
    EPD_WriteCmd(0x18);
    EPD_WriteData(0x80);

    // Display update control 2 + Master Activation: enable clock, load
    // temperature, load LUT (mode 1). Wait for the sequence to complete.
    EPD_WriteCmd(0x22);
    EPD_WriteData(0xB1);
    EPD_WriteCmd(0x20);
    EPD_CheckStatus_inverted(100);
}

_attribute_ram_code_ static int16_t ssd168x_read_temp_and_leave_awake(void)
{
    // Cmd 0x1B: read from temperature register. 2 bytes: byte1 = D11..D4,
    // byte2 = D3..D0 followed by 0000. The 12-bit register V satisfies
    // DegC = V/16 (datasheet sec.8). Concatenated as (byte1<<8 | byte2),
    // that reconstructs 16*V, i.e. Q8.8 fixed point -- divide by 256.0 for
    // degrees C (see docs/HINK-E0213A200-A0_Spec_bwr.pdf sec.8/9, cmd 0x1B).
    EPD_WriteCmd(0x1B);
    uint8_t hi = EPD_SPI_read();
    uint8_t lo = EPD_SPI_read();
    return (int16_t)((hi << 8) | lo);
}

_attribute_ram_code_ int16_t EPD_SSD168x_read_temp_raw(const epd_ssd168x_profile_t *p)
{
    ssd168x_init_common(p);
    int16_t raw = ssd168x_read_temp_and_leave_awake();
    WaitMs(5);

    // deep sleep
    EPD_WriteCmd(0x10);
    EPD_WriteData(0x01);

    return raw;
}

_attribute_ram_code_ uint8_t EPD_SSD168x_Display(const epd_ssd168x_profile_t *p, unsigned char *image, unsigned char *red_image, int size, uint8_t full_or_partial)
{
    ssd168x_init_common(p);
    int16_t temp_raw = ssd168x_read_temp_and_leave_awake();
    WaitMs(5);

    // Write BW plane
    EPD_WriteCmd(0x4E);
    EPD_WriteData(0x00);
    EPD_WriteCmd(0x4F);
    EPD_WriteData(p->y_start_lo);
    EPD_WriteData(p->y_start_hi);

    if (red_image != NULL && !p->has_red)
    {
        // No RED plane on this panel: a "red" pixel degrades to black.
        // BW RAM polarity is 1=white/0=black (cmd 0x24 description), so
        // forcing black means clearing the bit wherever red is set.
        EPD_WriteCmd(0x24);
        for (int i = 0; i < size; i++)
        {
            EPD_WriteData(image[i] & ~red_image[i]);
        }
        WaitMs(2);
    }
    else
    {
        EPD_LoadImage(image, size, 0x24);
    }

    // Write RED plane, if the panel has one
    if (p->has_red)
    {
        EPD_WriteCmd(0x4E);
        EPD_WriteData(0x00);
        EPD_WriteCmd(0x4F);
        EPD_WriteData(p->y_start_lo);
        EPD_WriteData(p->y_start_hi);

        if (red_image != NULL)
        {
            EPD_LoadImage(red_image, size, 0x26);
        }
        else
        {
            EPD_WriteCmd(0x26);
            for (int i = 0; i < size; i++)
            {
                EPD_WriteData(0x00);
            }
        }
    }

    if (!full_or_partial)
    {
        EPD_WriteCmd(0x32);
        for (unsigned int i = 0; i < sizeof(ssd168x_lut_partial); i++)
        {
            EPD_WriteData(ssd168x_lut_partial[i]);
        }
    }

    // Display update control 2 + Master Activation: enable clock, enable
    // analog, display, disable analog, disable OSC.
    EPD_WriteCmd(0x22);
    EPD_WriteData(0xC7);
    EPD_WriteCmd(0x20);

    return (uint8_t)(temp_raw >> 8); // whole-degree value, for legacy callers
}

_attribute_ram_code_ void EPD_SSD168x_set_sleep(const epd_ssd168x_profile_t *p)
{
    (void)p;
    EPD_WriteCmd(0x10);
    EPD_WriteData(0x01);
}
