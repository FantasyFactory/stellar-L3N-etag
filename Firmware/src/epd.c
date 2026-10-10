#include <stdint.h>
#include "etime.h"
#include "tl_common.h"
#include "main.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_bw_213.h"
#include "epd_bw_213_ice.h"
#include "epd_ssd168x.h"
#include "drivers.h"
#include "stack/ble/ble.h"

#include "battery.h"
#include "flash.h"

#include "OneBitDisplay.h"
#include "TIFF_G4.h"
extern const uint8_t ucMirror[];
#include "font_60.h"
#include "font16.h"
#include "font8.h"
#include "font16zh.h"
#include "font30.h"

extern settings_struct settings;

RAM uint8_t epd_model = 0; // 0 = Undetected, 1 = BW213, 2 = HINK213BWR, 4 = BW213ICE, 5 = BWR296
const char *epd_model_string[] = {"NC", "BW213", "HINK213", "BWR154", "213ICE", "BWR296"};
RAM uint16_t epd_res_width = epd_width;   // active panel resolution, set by EPD_detect_model()
RAM uint8_t epd_res_height = epd_height;  // (padded to a full byte)
RAM uint8_t epd_update_state = 0;

// One entry per supported controller/panel combination. The three SSD1680
// entries all go through the single parametrized epd_ssd168x driver; only
// their resolution/color profile differs. BW213ICE and BW213 (a genuinely
// different controller, UC8151) keep their own implementation but are
// exposed through the same interface so epd.c never branches on model
// number again outside of epd_find_driver().
typedef struct
{
    uint8_t id;
    uint16_t width;
    uint8_t height;
    uint8_t has_red;
    uint8_t busy_active_high; // set for controllers where EPD_IS_BUSY() reads inverted (UC8151)
    int16_t (*read_temp_raw)(void); // Q8.8 fixed point, /256.0 = DegC
    uint8_t (*display)(unsigned char *bw, unsigned char *red, int size, uint8_t full_or_partial);
    void (*set_sleep)(void);
} epd_driver_t;

static uint8_t drv_hink213bwr_display(unsigned char *bw, unsigned char *red, int size, uint8_t fop) { return EPD_SSD168x_Display(&epd_ssd168x_hink_213bwr, bw, red, size, fop); }
static int16_t drv_hink213bwr_read_temp(void) { return EPD_SSD168x_read_temp_raw(&epd_ssd168x_hink_213bwr); }
static void drv_hink213bwr_sleep(void) { EPD_SSD168x_set_sleep(&epd_ssd168x_hink_213bwr); }

static uint8_t drv_bwr296_display(unsigned char *bw, unsigned char *red, int size, uint8_t fop) { return EPD_SSD168x_Display(&epd_ssd168x_bwr_296, bw, red, size, fop); }
static int16_t drv_bwr296_read_temp(void) { return EPD_SSD168x_read_temp_raw(&epd_ssd168x_bwr_296); }
static void drv_bwr296_sleep(void) { EPD_SSD168x_set_sleep(&epd_ssd168x_bwr_296); }

// ice and bw213 (UC8151) have no native RED plane: if a red buffer is
// passed anyway, fold it into the black plane (red -> black) before
// dispatching, same guarantee the ssd168x driver gives natively.
static void epd_merge_red_into_bw(unsigned char *bw, const unsigned char *red, int size)
{
    for (int i = 0; i < size; i++)
        bw[i] &= ~red[i];
}

static uint8_t drv_ice_display(unsigned char *bw, unsigned char *red, int size, uint8_t fop)
{
    if (red != NULL)
        epd_merge_red_into_bw(bw, red, size);
    return EPD_BW_213_ice_Display(bw, size, fop);
}
static int16_t drv_ice_read_temp(void) { return EPD_BW_213_ice_read_temp_raw(); }
static void drv_ice_sleep(void) { EPD_BW_213_ice_set_sleep(); }

static uint8_t drv_bw213_display(unsigned char *bw, unsigned char *red, int size, uint8_t fop)
{
    if (red != NULL)
        epd_merge_red_into_bw(bw, red, size);
    return EPD_BW_213_Display(bw, size, fop);
}
// No datasheet-confirmed sub-degree precision for the UC8151 family; expose
// its whole-degree reading in the same Q8.8 shape so callers stay generic.
static int16_t drv_bw213_read_temp(void) { return (int16_t)(EPD_BW_213_read_temp() << 8); }
static void drv_bw213_sleep(void) { EPD_BW_213_set_sleep(); }

static const epd_driver_t epd_drivers[] = {
    {2, 250, 128, 1, 0, drv_hink213bwr_read_temp, drv_hink213bwr_display, drv_hink213bwr_sleep}, // HINK-E0213A200, confirmed real panel
    {5, 296, 128, 1, 0, drv_bwr296_read_temp, drv_bwr296_display, drv_bwr296_sleep},
    {4, 212, 104, 0, 0, drv_ice_read_temp, drv_ice_display, drv_ice_sleep},
    {1, 250, 128, 0, 1, drv_bw213_read_temp, drv_bw213_display, drv_bw213_sleep}, // UC8151, different controller entirely
};
#define EPD_DRIVER_COUNT (sizeof(epd_drivers) / sizeof(epd_drivers[0]))

static const epd_driver_t *epd_find_driver(uint8_t id)
{
    for (unsigned int i = 0; i < EPD_DRIVER_COUNT; i++)
        if (epd_drivers[i].id == id)
            return &epd_drivers[i];
    return NULL;
}

RAM uint8_t epd_scene = 5;
RAM uint8_t epd_wait_update = 0;

RAM uint8_t hour_refresh = 100;
RAM uint8_t minute_refresh = 100;

const char *BLE_conn_string[] = {"BLE 0", "BLE 1"};
RAM uint8_t epd_temperature_is_read = 0;
RAM uint8_t epd_temperature = 0;

RAM uint8_t epd_buffer[epd_buffer_size];
RAM uint8_t epd_temp[epd_buffer_size]; // for OneBitDisplay to draw into
OBDISP obd;                        // virtual display structure
TIFFIMAGE tiff;

// Red plane buffer, used by epd_purge_tick()'s full-refresh anti-ghosting
// cycle (black/white/red/white) and raw BLE image upload -- not by any
// scene right now (see epd_myScene: red doesn't survive a partial refresh
// on this panel). NOT retention RAM on purpose: unlike epd_buffer/epd_temp
// (which also back the incremental multi-packet BLE image upload in
// epd_ble_service.c, and so must survive sleep between packets), this is
// only ever used synchronously within one call -- fully rewritten every
// time, nothing depends on it surviving a sleep cycle. Keeping it out of
// retention matters: that budget is only 32K and already tight (see
// debug_log.h).
uint8_t epd_buffer_red[epd_buffer_size];

// Temperature hystory (every 15 minutes)
// 96 values for 24 hours
//
//                        00:00        01:00        02:00        03:00        04:00        05:00        06:00        07:00        08:00        09:00        10:00        11:00        12:00        13:00        14:00        15:00        16:00        17:00        18:00        19:00        20:00        21:00        22:00        23:00

//RAM uint8_t min_temp[96]={17,17,17,17, 16,16,16,16, 15,15,15,15, 15,15,15,15, 15,15,15,15, 15,15,15,15, 16,16,16,16, 17,17,18,18, 18,18,17,17, 17,17,16,16, 15,15,15,15, 15,15,15,15, 16,17,18,18, 18,18,18,18, 17,17,17,17, 17,17,17,17, 17,17,17,17, 17,17,17,17, 17,17,17,17, 17,17,18,18, 18,18,18,18, 18,18,18,18, 18,18,18,18, 18,18,17,17};
//RAM uint8_t max_temp[96]={21,20,20,19, 19,19,18,18, 17,17,17,17, 17,17,17,17, 17,17,17,17, 17,17,17,17, 17,18,18,18, 19,20,20,21, 21,21,20,20, 20,20,20,20, 19,19,19,19, 19,19,19,19, 19,20,21,21, 22,22,23,23, 22,22,22,22, 21,21,21,21, 21,21,21,21, 21,21,21,21, 21,21,21,21, 21,21,22,22, 22,22,23,23, 23,23,22,22, 22,22,21,21, 21,21,21,21};
//RAM uint8_t day_temp[96]={127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127};

RAM uint8_t min_temp[96]={255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255};
RAM uint8_t max_temp[96]={00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00};
RAM uint8_t day_temp[96]={255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255};

//RAM uint8_t min_temp[96]={00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00};
//RAM uint8_t max_temp[96]={39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39};
//RAM uint8_t day_temp[96]={17,17,17,17, 16,16,16,16, 15,15,15,15, 15,15,15,15, 15,15,15,15, 15,15,15,15, 16,16,16,16, 17,17,18,18, 18,18,17,17, 17,17,16,16, 15,15,15,15, 15,15,15,15, 16,17,18,18, 18,18,18,18, 17,17,17,17, 17,17,17,17, 17,17,17,17, 17,17,17,17, 17,17,17,17, 17,17,18,18, 18,18,18,18, 18,18,18,18, 18,18,18,18, 18,18,17,17};



RAM int16_t epd_temp_raw = 0; // last raw Q8.8 temperature reading (see epd_ssd168x.h)

// With this we can force a display if it wasnt detected correctly
void set_EPD_model(uint8_t model_nr)
{
    if (!epd_find_driver(model_nr))
        return; // ignore unknown ids, keep whatever was working
    epd_model = model_nr;
    const epd_driver_t *drv = epd_find_driver(epd_model);
    epd_res_width = drv->width;
    epd_res_height = drv->height;
    settings.epd_profile = epd_model; // persisted on next save_settings_to_flash()
    printf("[EPD] model set to %s (%dx%d)\r\n", epd_model_string[epd_model], epd_res_width, epd_res_height);
}

// With this we can force a display if it wasnt detected correctly
void set_EPD_scene(uint8_t scene)
{
    printf("[EPD] scene %d -> %d, epd_update_state=%d\r\n", epd_scene, scene, epd_update_state);
    epd_scene = scene;
    set_EPD_wait_flush();
}

void set_EPD_wait_flush() {
    epd_wait_update = 1;
}

// Detects the controller FAMILY (SSD1680-class vs UC8151-class) reliably
// via the SSD1680 Status Bit Read (cmd 0x2F, Chip ID). This can NOT tell
// apart different panel sizes/colors sharing the same SSD1680 die -- that
// is a configured choice (settings.epd_profile, set via set_EPD_model() /
// BLE cmd 0xE0), defaulting to our confirmed real panel (HINK-E0213A200,
// id 2) the first time no valid profile has been saved yet.
_attribute_ram_code_ void EPD_detect_model(void)
{
    EPD_init();
    EPD_POWER_ON();
    WaitMs(10);
    gpio_write(EPD_RESET, 0);
    WaitMs(10);
    gpio_write(EPD_RESET, 1);
    WaitMs(10);

    uint8_t chosen_id;
    uint8_t is_ssd168x = EPD_SSD168x_detect_family();
    uint8_t saved_profile = settings.epd_profile;
    if (is_ssd168x)
    {
        chosen_id = epd_find_driver(saved_profile) ? saved_profile : 2;
        if (chosen_id == 1 || chosen_id == 4) // a saved profile from the other family makes no sense here
            chosen_id = 2;
    }
    else
    {
        chosen_id = 1; // UC8151, no reliable positive detect available -> last-resort fallback
    }

    set_EPD_model(chosen_id);
    printf("[EPD] family=%s saved_profile=%d -> chosen=%s (%dx%d)\r\n",
           is_ssd168x ? "SSD1680" : "UC8151(fallback)", saved_profile,
           epd_model_string[chosen_id], epd_res_width, epd_res_height);
    EPD_POWER_OFF();
}

_attribute_ram_code_ uint8_t EPD_read_temp(void)
{
    if (epd_temperature_is_read)
        return epd_temperature;

    if (!epd_model)
        EPD_detect_model();

    EPD_init();
    EPD_POWER_ON();
    WaitMs(5);
    gpio_write(EPD_RESET, 0);
    WaitMs(10);
    gpio_write(EPD_RESET, 1);
    WaitMs(10);

    const epd_driver_t *drv = epd_find_driver(epd_model);
    epd_temp_raw = drv->read_temp_raw();
    epd_temperature = (uint8_t)(epd_temp_raw >> 8);

    EPD_POWER_OFF();

    epd_temperature_is_read = 1;

    return epd_temperature;
}

// Same reading as EPD_read_temp(), but as a signed Q8.8 fixed point value
// (divide by 256.0 for DegC) instead of a truncated whole degree.
_attribute_ram_code_ int16_t EPD_read_temp_raw(void)
{
    EPD_read_temp();
    return epd_temp_raw;
}

_attribute_ram_code_ void EPD_Display(unsigned char *image, unsigned char *red_image, int size, uint8_t full_or_partial)
{
    if (!epd_model)
        EPD_detect_model();

    EPD_init();
    EPD_POWER_ON();
    WaitMs(5);
    gpio_write(EPD_RESET, 0);
    WaitMs(10);
    gpio_write(EPD_RESET, 1);
    WaitMs(10);

    const epd_driver_t *drv = epd_find_driver(epd_model);
    epd_temperature = drv->display(image, red_image, size, full_or_partial);

    epd_temperature_is_read = 1;
    epd_update_state = 1;
}

_attribute_ram_code_ void epd_set_sleep(void)
{
    if (!epd_model)
        EPD_detect_model();

    const epd_driver_t *drv = epd_find_driver(epd_model);
    drv->set_sleep();

    EPD_POWER_OFF();
    epd_update_state = 0;
}

_attribute_ram_code_ uint8_t epd_state_handler(void)
{
    switch (epd_update_state)
    {
    case 0:
        // Nothing todo
        break;
    case 1: // check if refresh is done and sleep epd if so
    {
        const epd_driver_t *drv = epd_find_driver(epd_model);
        uint8_t busy_active_high = drv ? drv->busy_active_high : 0;
        if (busy_active_high ? !EPD_IS_BUSY() : EPD_IS_BUSY())
            epd_set_sleep();
        break;
    }
    }
    return epd_update_state;
}

// "Deep clean" the panel to reduce ghosting: cycle full-refresh black/white/
// red/white, per the panel datasheet's own recommendation (see
// docs/HINK-E0213A200-A0_Spec_bwr.pdf note 12-3: "the full black and full
// white picture could be added to clear the screen ... the display effect
// would be better"). Driven as a small non-blocking state machine (ticked
// from main_loop(), like everything else EPD-related) instead of a
// synchronous wait, so it can't starve the watchdog feed or BLE processing
// for the several seconds a full refresh can take -- see epd_purge_tick().
RAM uint8_t epd_purge_state = 0; // 0 = idle, 1..4 = phase in progress

void epd_purge_start(void)
{
    epd_purge_state = 1;
}

uint8_t epd_purge_is_active(void)
{
    return epd_purge_state != 0;
}

void epd_purge_tick(void)
{
    if (epd_purge_state == 0)
        return;
    if (epd_update_state) // previous phase's refresh still in flight, wait
        return;

    unsigned int size = epd_res_width * epd_res_height / 8;
    switch (epd_purge_state)
    {
    case 1: // full black
        memset(epd_buffer, 0x00, size);     // BW polarity: 0 = black
        memset(epd_buffer_red, 0x00, size); // no red
        EPD_Display(epd_buffer, epd_buffer_red, size, 1);
        epd_purge_state = 2;
        break;
    case 2: // full white
        memset(epd_buffer, 0xFF, size);     // BW polarity: 1 = white
        memset(epd_buffer_red, 0x00, size);
        EPD_Display(epd_buffer, epd_buffer_red, size, 1);
        epd_purge_state = 3;
        break;
    case 3: // full red (white background, red plane fully set)
        memset(epd_buffer, 0xFF, size);
        memset(epd_buffer_red, 0xFF, size); // RED polarity: 1 = red, direct
        EPD_Display(epd_buffer, epd_buffer_red, size, 1);
        epd_purge_state = 4;
        break;
    case 4: // back to blank white, then let the real scene redraw for real
        memset(epd_buffer, 0xFF, size);
        memset(epd_buffer_red, 0x00, size);
        EPD_Display(epd_buffer, epd_buffer_red, size, 1);
        set_EPD_wait_flush();
        epd_purge_state = 0;
        break;
    }
}

// invert must be true for the BW plane (panel polarity 1=white/0=black, but
// OneBitDisplay's own buffer uses 1=pixel-was-drawn -- needs flipping) and
// false for the RED plane (panel polarity is 1=red/0=not-red directly, i.e.
// already "1=drawn" like OneBitDisplay's own convention -- inverting it
// would light up red everywhere EXCEPT what was actually drawn red).
_attribute_ram_code_ void FixBuffer(uint8_t *pSrc, uint8_t *pDst, uint16_t width, uint16_t height, bool invert)
{
    int x, y;
    uint8_t *s, *d;
    for (y = 0; y < (height / 8); y++)
    { // byte rows
        d = &pDst[y];
        s = &pSrc[y * width];
        for (x = 0; x < width; x++)
        {
            uint8_t b = ucMirror[s[width - 1 - x]]; // flip
            d[x * (height / 8)] = invert ? (uint8_t)~b : b;
        }                                                      // for x
    }                                                          // for y
}

_attribute_ram_code_ void TIFFDraw(TIFFDRAW *pDraw)
{
    uint8_t uc = 0, ucSrcMask, ucDstMask, *s, *d;
    int x, y;

    s = pDraw->pPixels;
    y = pDraw->y;                          // current line
    d = &epd_buffer[(249 * 16) + (y / 8)]; // rotated 90 deg clockwise
    ucDstMask = 0x80 >> (y & 7);           // destination mask
    ucSrcMask = 0;                         // src mask
    for (x = 0; x < pDraw->iWidth; x++)
    {
        // Slower to draw this way, but it allows us to use a single buffer
        // instead of drawing and then converting the pixels to be the EPD format
        if (ucSrcMask == 0)
        { // load next source byte
            ucSrcMask = 0x80;
            uc = *s++;
        }
        if (!(uc & ucSrcMask))
        { // black pixel
            d[-(x * 16)] &= ~ucDstMask;
        }
        ucSrcMask >>= 1;
    }
}

_attribute_ram_code_ void epd_display_tiff(uint8_t *pData, int iSize)
{
    // test G4 decoder
    // NB: the decode target is hardcoded to 250x122 regardless of which
    // panel is actually active -- fine for HINK213 (250x128), but on
    // BWR296/ice it'll draw into the wrong portion of a differently-sized
    // canvas. Out of scope to fix properly here; what matters for now is
    // that the final size below matches the REAL panel (not the old
    // epd_buffer_size macro, i.e. max static capacity) so this can't
    // desync the SPI protocol the way the epd_ble_service.c bug did.
    epd_clear();
    TIFF_openRAW(&tiff, 250, 122, BITDIR_MSB_FIRST, pData, iSize, TIFFDraw);
    TIFF_setDrawParameters(&tiff, 65536, TIFF_PIXEL_1BPP, 0, 0, 250, 122, NULL);
    TIFF_decode(&tiff);
    TIFF_close(&tiff);
    EPD_Display(epd_buffer, NULL, epd_res_width * epd_res_height / 8, 1);
}

extern uint8_t mac_public[6];
_attribute_ram_code_ void epd_display(struct date_time _time, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial)
{
    uint8_t battery_level;

    if (epd_update_state)
        return;

    if (!epd_model)
    {
        EPD_detect_model();
    }
    uint16_t resolution_w = epd_res_width;
    uint16_t resolution_h = epd_res_height;

    epd_clear();

    obdCreateVirtualDisplay(&obd, resolution_w, resolution_h, epd_temp);
    obdFill(&obd, 0, 0); // fill with white

    char buff[100];
    battery_level = get_battery_level(battery_mv);
    sprintf(buff, "S24_%02X%02X%02X %s", mac_public[2], mac_public[1], mac_public[0], epd_model_string[epd_model]);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 1, 17, (char *)buff, 1);
    sprintf(buff, "%s", BLE_conn_string[ble_get_connected()]);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 232, 20, (char *)buff, 1);
    sprintf(buff, "%02d:%02d", _time.tm_hour, _time.tm_min);
    obdWriteStringCustom(&obd, (GFXfont *)&DSEG14_Classic_Mini_Regular_40, 75, 65, (char *)buff, 1);
    int16_t traw = EPD_read_temp_raw(); // Q8.8: DegC = traw/256.0
    int t_whole = traw / 256;           // truncates toward zero -> sign lands here
    int t_frac = traw % 256;
    if (t_frac < 0) t_frac = -t_frac;
    sprintf(buff, "--%d.%01d'C--%dx%d--", t_whole, (t_frac * 10) / 256, resolution_w, resolution_h);
    obdWriteStringCustom(&obd, (GFXfont *)&Special_Elite_Regular_30, 10, 95, (char *)buff, 1);
    sprintf(buff, "Battery %dmV  %d%%", battery_mv, battery_level);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 10, 120, (char *)buff, 1);
    FixBuffer(epd_temp, epd_buffer, resolution_w, resolution_h, true);
    EPD_Display(epd_buffer, NULL, resolution_w * resolution_h / 8, full_or_partial);
}

_attribute_ram_code_ void epd_display_char(uint8_t data)
{
    int i;
    for (i = 0; i < epd_buffer_size; i++)
    {
        epd_buffer[i] = data;
    }
    EPD_Display(epd_buffer, NULL, epd_buffer_size, 1);
}

_attribute_ram_code_ void epd_clear(void)
{
    memset(epd_buffer, 0x00, epd_buffer_size);
    memset(epd_temp, 0x00, epd_buffer_size);
}
void update_time_scene(struct date_time _time, uint16_t battery_mv, int16_t temperature, void (*scene)(struct date_time, uint16_t, int16_t,  uint8_t)) {
    // default scene: show default time, battery, ble address, temperature
    if (epd_update_state)
        return;

    if (!epd_model)
    {
        EPD_detect_model();
    }

    if (epd_wait_update) {
        scene(_time, battery_mv, temperature, 1);
        epd_wait_update = 0;
    }

    else if (_time.tm_min != minute_refresh)
    {
        minute_refresh = _time.tm_min;
        if (_time.tm_hour != hour_refresh)
        {
            hour_refresh = _time.tm_hour;
            scene(_time, battery_mv, temperature, 1);
        }
        else
        {
            scene(_time, battery_mv, temperature, 0);
        }
    }
}

void epd_update(struct date_time _time, uint16_t battery_mv, int16_t temperature) {

    int temp_idx=_time.tm_hour*4+_time.tm_min/15;
    uint8_t epd_temp=EPD_read_temp();
    if (min_temp[temp_idx] > epd_temp) min_temp[temp_idx] = epd_temp;
    if (max_temp[temp_idx] < epd_temp) max_temp[temp_idx] = epd_temp;
    day_temp[temp_idx] = epd_temp;

    switch(epd_scene) {
        case 1:
            update_time_scene(_time, battery_mv, temperature, epd_display);
            break;
        case 2:
            update_time_scene(_time, battery_mv, temperature, epd_display_time_with_date);
            break;
        case 3:
            update_time_scene(_time, battery_mv, temperature, epd_display_my);
            break;
        case 4:
            update_time_scene(_time, battery_mv, temperature, epd_display_info);
            break;
        case 5:
            update_time_scene(_time, battery_mv, temperature, epd_myScene);
            break;
        default:
            break;
    }
}

void epd_display_time_with_date(struct date_time _time, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial) {
    uint16_t battery_level;

    epd_clear();

    obdCreateVirtualDisplay(&obd, epd_res_width, epd_res_height, epd_temp);
    obdFill(&obd, 0, 0); // fill with white

    char buff[100];
    battery_level = get_battery_level(battery_mv);

    sprintf(buff, "S24_%02X%02X%02X", mac_public[2], mac_public[1], mac_public[0]);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 1, 17, (char *)buff, 1);

    if (ble_get_connected()) {
        sprintf(buff, "78%s", "234");
    } else {
        sprintf(buff, "78%s", "56");
    }

    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16_zh, 120, 21, (char *)buff, 1);

    obdRectangle(&obd, 252, 10, 255, 14, 1, 1);
    obdRectangle(&obd, 255, 2, 295, 22, 1, 1);

    sprintf(buff, "%d", battery_level);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 259, 18, (char *)buff, 0);

    obdRectangle(&obd, 0, 25, 295, 27, 1, 1);

    sprintf(buff, "%02d:%02d", _time.tm_hour, _time.tm_min);
    obdWriteStringCustom(&obd, (GFXfont *)&DSEG14_Classic_Mini_Regular_40, 35, 85, (char *)buff, 1);

    sprintf(buff, "   %d'C", EPD_read_temp());
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 216, 50, (char *)buff, 1);

    obdRectangle(&obd, 216, 60, 295, 62, 1, 1);

    sprintf(buff, " %dmV", battery_mv);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 216, 84, (char *)buff, 1);

    obdRectangle(&obd, 214, 27, 216, 99, 1, 1);
    obdRectangle(&obd, 0, 97, 295, 99, 1, 1);

    sprintf(buff, "%d-%02d-%02d", _time.tm_year, _time.tm_month, _time.tm_day);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 10, 120, (char *)buff, 1);

    if (_time.tm_week == 7) {
        sprintf(buff, "9:%c", _time.tm_week + 0x20 + 6);
    } else {
        sprintf(buff, "9:%c", _time.tm_week + 0x20);
    }
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16_zh, 120, 122, (char *)buff, 1);

    if (_time.tm_hour > 7 && _time.tm_hour < 20) {
        sprintf(buff, "%s", "EFGH");
    } else {
        sprintf(buff, "%s", "ABCD");
    }
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16_zh, 200, 122, (char *)buff, 1);

    FixBuffer(epd_temp, epd_buffer, epd_res_width, epd_res_height, true);

    EPD_Display(epd_buffer, NULL, epd_res_width * epd_res_height / 8, full_or_partial);
}



void epd_display_info(struct date_time _time, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial) {
    uint16_t battery_level;

    if (epd_update_state)
        return;

    if (!epd_model)
    {
        EPD_detect_model();
    }
    uint16_t resolution_w = epd_res_width;
    uint16_t resolution_h = epd_res_height;

    epd_clear();

    obdCreateVirtualDisplay(&obd, resolution_w, resolution_h, epd_temp);
    obdFill(&obd, 0, 0); // fill with white

    char buff[100];
    battery_level = get_battery_level(battery_mv);
    sprintf(buff, "S24_%02X%02X%02X %s", mac_public[2], mac_public[1], mac_public[0], epd_model_string[epd_model]);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 1, 17, (char *)buff, 1);

    sprintf(buff, "%s", BLE_conn_string[ble_get_connected()]);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 232, 20, (char *)buff, 1);
    sprintf(buff, "%02d:%02d", _time.tm_hour, _time.tm_min);
    obdWriteStringCustom(&obd, (GFXfont *)&DSEG14_Classic_Mini_Regular_40, 75, 65, (char *)buff, 1);
    int16_t traw = EPD_read_temp_raw(); // Q8.8: DegC = traw/256.0
    int t_whole = traw / 256;           // truncates toward zero -> sign lands here
    int t_frac = traw % 256;
    if (t_frac < 0) t_frac = -t_frac;
    sprintf(buff, "%d.%01d'C (%dx%d)", t_whole, (t_frac * 10) / 256, resolution_w, resolution_h);
    obdWriteStringCustom(&obd, (GFXfont *)&Special_Elite_Regular_30, 10, 95, (char *)buff, 1);
    sprintf(buff, "Battery %dmV  %d%%", battery_mv, battery_level);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 0, 120, (char *)buff, 1);
    FixBuffer(epd_temp, epd_buffer, resolution_w, resolution_h, true);
    EPD_Display(epd_buffer, NULL, resolution_w * resolution_h / 8, full_or_partial);
}



void epd_display_my(struct date_time _time, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial) {
    uint16_t battery_level;

    epd_clear();

    obdCreateVirtualDisplay(&obd, epd_res_width, epd_res_height, epd_temp);
    obdFill(&obd, 0, 0); // fill with white

    char buff[100];
    battery_level = get_battery_level(battery_mv);

    sprintf(buff, "S24_%02X%02X%02X", mac_public[2], mac_public[1], mac_public[0]);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 1, 17, (char *)buff, 1);

    if (ble_get_connected()) {
        sprintf(buff, "78%s", "234");
    } else {
        sprintf(buff, "78%s", "56");
    }

    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 120, 21, (char *)buff, 1);

    obdRectangle(&obd, 252, 10, 255, 14, 1, 1);
    obdRectangle(&obd, 255, 2, 295, 22, 1, 1);

    sprintf(buff, "%d", battery_level);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 259, 18, (char *)buff, 0);

    obdRectangle(&obd, 0, 25, 295, 27, 1, 1);

    sprintf(buff, "%02d:%02d", _time.tm_hour, _time.tm_min);
    obdWriteStringCustom(&obd, (GFXfont *)&DSEG14_Classic_Mini_Regular_40, 35, 85, (char *)buff, 1);

    sprintf(buff, "   %d'C", EPD_read_temp());
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 216, 50, (char *)buff, 1);

    obdRectangle(&obd, 216, 60, 295, 62, 1, 1);

    sprintf(buff, " %dmV", battery_mv);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 216, 84, (char *)buff, 1);

    obdRectangle(&obd, 214, 27, 216, 99, 1, 1);
    obdRectangle(&obd, 0, 97, 295, 99, 1, 1);

    sprintf(buff, "%d-%02d-%02d", _time.tm_year, _time.tm_month, _time.tm_day);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 10, 120, (char *)buff, 1);

    if (_time.tm_week == 7) {
        sprintf(buff, "9:%c", _time.tm_week + 0x20 + 6);
    } else {
        sprintf(buff, "9:%c", _time.tm_week + 0x20);
    }
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 120, 122, (char *)buff, 1);

    if (_time.tm_hour > 7 && _time.tm_hour < 20) {
        sprintf(buff, "%s", "EFGH");
    } else {
        sprintf(buff, "%s", "ABCD");
    }
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 200, 122, (char *)buff, 1);

    FixBuffer(epd_temp, epd_buffer, epd_res_width, epd_res_height, true);

    EPD_Display(epd_buffer, NULL, epd_res_width * epd_res_height / 8, full_or_partial);
}

void epd_myScene(struct date_time _time, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial) {

    epd_clear();

    obdCreateVirtualDisplay(&obd, epd_res_width, epd_res_height, epd_temp);
    obdFill(&obd, 0, 0); // fill with white

    // Calendario
    drawCalendar(&obd, _time, 0, 0, false);

    // Orologio
    drawClock(&obd, _time, 101, 1);

    //Grafico temperatura
    drawTempGraph(&obd, _time, temperature, 102, 43, false);

    char buff[300];
    memset(buff, 0, sizeof(buff));
    srand_custom(clock_time());
    smadonnator(buff);
    draw_optimized_text(&obd, buff,
                    0, 96,      // Posizione area
                    240, 24,   // Dimensioni area
                    0);
    //obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 10, 120, (char *)buff, 1);

    // MAC address
    //drawMAC(&obd, 48, 107);

    // battery level
    //drawBattery(&obd, battery_mv, 252, 107, false);
    drawBattery(&obd, battery_mv, 242, 96, true);

    FixBuffer(epd_temp, epd_buffer, epd_res_width, epd_res_height, true);

    // No red plane here: this panel doesn't keep red content through a
    // partial refresh (custom LUT: too short a pulse to move the pigment;
    // the panel's own alternate "Display Mode 2" waveform was tried too and
    // made partial refreshes stop updating visibly after a few minutes,
    // worse than the original symptom -- see epd_ssd168x.c). Red is still
    // used elsewhere (epd_purge_tick's full-refresh anti-ghosting cycle,
    // and raw BLE image upload), just not in this scene until a real fix is
    // found.
    EPD_Display(epd_buffer, NULL, epd_res_width * epd_res_height / 8, full_or_partial);

}

void drawTempGraph(OBDISP *pOBD, struct date_time _time, int16_t temperature, int tgr_x, int tgr_y, bool draw_placeholder)  {
    uint8_t day_min=255;
    uint8_t day_max=0;
    uint8_t graph_height=48;
    uint8_t graph_width=96;
    uint8_t grx_offset=tgr_x+51;
    uint8_t gry_offset=tgr_y;
    uint8_t tempY;
    char buffer[40];

    obdRectangle(pOBD,
        tgr_x , tgr_y,  // angolo superiore sinistro + altezza intestazione
        grx_offset + graph_width +1, tgr_y + graph_height +8, // angolo inferiore destro + altezza inte + 4 di mergine
        1, 0); // colore nero, non riempito
    obdRectangle(pOBD,
        grx_offset-1, tgr_y,  // angolo superiore sinistro + altezza intestazione
        grx_offset + graph_width, tgr_y + graph_height +1, // angolo inferiore destro + altezza inte + 4 di mergine
        1, 0); // colore nero, non riempito
    if(draw_placeholder) return;

    // Calcola i valori massimi e minimi STORICI (su tutti i 96 slot, non solo vicino alla temperatura attuale)
    for(int tx = 0; tx < 96; tx++) {
        if((min_temp[tx]!=255) && (min_temp[tx] < day_min)) day_min = min_temp[tx];
        if((max_temp[tx]!=0) && (max_temp[tx] > day_max)) day_max = max_temp[tx];
    }
    // se non trova valori validi (es. subito dopo il boot), allora li imposta a + - 10 rispetto alla temperatura corrente
    if(day_min==255) day_min=temperature-10;
    if(day_max==0) day_max=temperature+10;
    // se l'escursione storica e' troppo piccola il grafico non avrebbe senso
    // (praticamente piatto) -> garantisce sempre almeno 10 gradi di scala,
    // centrati sull'escursione reale.
    if(day_max-day_min < 10) {
        uint8_t mid = (uint8_t)((day_max + day_min) / 2);
        day_min = (mid >= 5) ? (mid - 5) : 0;
        day_max = day_min + 10;
    }
    if(day_max<=day_min) day_max=day_min+1; // guardia finale: evita divisione per zero in scaleTemp()

    int temp_idx=_time.tm_hour*4+_time.tm_min/15; // indice nel grafico della temperatura: 96 byte, uno ogni 15 minuti

    sprintf(buffer, "%d", day_temp[temp_idx]);  // Temperatura al momento
    // NB: niente rosso qui apposta -- il rosso non sopravvive al refresh
    // parziale su questo pannello (vedi epd_myScene/epd_ssd168x.c), quindi
    // qualunque rosso legato alla temperatura (che puo' cambiare ad ogni
    // refresh parziale) sbiadirebbe al giro successivo.
    obdWriteStringCustom(pOBD, (GFXfont *)&Special_Elite_Regular_30, tgr_x, tgr_y +24, (char *)buffer, 1);
    obdScaledString(pOBD, tgr_x+34, tgr_y+2, "o", FONT_8x8, 0, 256, 256, 0);

    sprintf(buffer, "G:%d-%d", day_min, day_max); // Minima e massima storica del grafico
    obdScaledString(pOBD, tgr_x+1, tgr_y+32, (char *)buffer, FONT_6x8, 0, 256, 256, 0);
    sprintf(buffer, "H:%d-%d", min_temp[temp_idx], max_temp[temp_idx]); // Minima e massima storica nella stessa fascia oraria
    obdScaledString(pOBD, tgr_x+1, tgr_y+41, (char *)buffer, FONT_6x8, 0, 256, 256, 0);

    for(int ty = day_min; ty <= day_max; ty+=2) {
        obdDrawLine(pOBD, grx_offset -3, (tgr_y + graph_height) - scaleTemp(ty,day_max,day_min,graph_height), grx_offset -1, tgr_y + graph_height - scaleTemp(ty,day_max,day_min,graph_height), 1, 0);
    }
    obdDrawLine(pOBD, grx_offset + temp_idx, tgr_y + graph_height, grx_offset + temp_idx, tgr_y + graph_height +2, 1, 0);

    for(int tx = 0; tx < graph_width; tx++) {
        if(max_temp[tx]!=0 && min_temp[tx]!=255) { // solo se questo slot ha dati storici reali
            int min_y=scaleTemp(min_temp[tx],day_max,day_min,graph_height);
            int max_y=scaleTemp(max_temp[tx],day_max,day_min,graph_height);
            for(int ty = min_y; ty <= max_y; ty++) {
                if((tx%2==0 && ty%2==0) || (tx%2==1 && ty%2==1)) { // solo se x e y sono pari o x e y sono dispari, in modo da fare un reticolo
                    obdSetPixel(pOBD, grx_offset + tx, (tgr_y + graph_height) - ty , 1, 0);
                }
            }
        }
        if(day_temp[tx]!=255) // linea della temperatura del giorno corrente
            obdSetPixel(pOBD, grx_offset +tx, (tgr_y + graph_height) - scaleTemp(day_temp[tx],day_max,day_min,graph_height), 1, 0);
    }

}

uint8_t scaleTemp(uint8_t temp, uint8_t max, uint8_t min, uint8_t height) {
    if (max <= min) return 0; // evita divisione per zero
    if (temp < min) temp = min; // clamp: un valore fuori range non deve sballare il disegno
    if (temp > max) temp = max;
    return (uint8_t)(((uint16_t)(temp - min) * height) / (max - min)); // interpolazione lineare corretta
}

void drawLabel(OBDISP *pOBD, char *text, int x, int y)  
{
    obdScaledString(pOBD, x, y, text, FONT_8x8, 0, 256, 256, 0);
}

void drawClock(OBDISP *pOBD, struct date_time _time, int cl_x, int cl_y) {
    char buffer[32];
    sprintf(buffer, "%02d:%02d", _time.tm_hour, _time.tm_min);
    obdWriteStringCustom(&obd, (GFXfont *)&DSEG14_Classic_Mini_Regular_40, cl_x, cl_y+39, (char *)buffer, 1);
}

void drawMAC(OBDISP *pOBD, int mac_x, int mac_y) {
    char buffer[32];
    sprintf(buffer, "S24_%02X%02X%02X", mac_public[2], mac_public[1], mac_public[0]);
    obdScaledString(pOBD, mac_x, mac_y +6, (char *)buffer, FONT_8x8, 0, 256, 256, 0);
}

void drawBattery(OBDISP *pOBD, uint16_t battery_mv, int bat_x, int bat_y, bool vertical) {   
    if(vertical) {
        uint16_t battery_level;
        if(battery_mv < 2200) battery_level=20;
        else if(battery_mv > 3100) battery_level=0;
        else battery_level = 20 -((battery_mv - 2200) / 45);
        obdRectangle(pOBD, bat_x+2, bat_y, bat_x +5, bat_y +1, 1, 1);
        obdRectangle(pOBD, bat_x, bat_y+2, bat_x + 7, bat_y +23, 1, 0);
        obdRectangle(pOBD, bat_x+1, bat_y+3+battery_level, bat_x + 6, bat_y +23, 1, 1);
    } else {
        char buffer[32];
        uint16_t battery_level;
        battery_level = get_battery_level(battery_mv);
        sprintf(buffer, "%d%%", battery_level);
        obdRectangle(pOBD, bat_x, bat_y+4, bat_x +2, bat_y +8, 1, 1);
        obdRectangle(pOBD, bat_x +3, bat_y, bat_x + 40, bat_y +12, 1, 1);
        obdScaledString(pOBD, bat_x +7, bat_y +3, (char *)buffer, FONT_8x8, 1, 256, 256, ROT_0);
    }
}

void drawCalendar(OBDISP *pOBD, struct date_time _time, int cal_x, int cal_y, bool draw_placeholder) {
    const char *months[] = {"Gen", "Feb", "Mar", "Apr", "Mag", "Giu",
                           "Lug", "Ago", "Set", "Ott", "Nov", "Dic"};
    const char *weekdays[] = {"L", "M", "M", "G", "V", "S", "D"};
    char buffer[32];
    const int cell_width = 14;  // larghezza di ogni cella
    const int cell_height = 10; // altezza di ogni cella

    obdRectangle(pOBD, 
        cal_x , cal_y,  // angolo superiore sinistro + altezza intestazione
        cal_x + (7 * cell_width), cal_y + (6 * cell_height) +32, // angolo inferiore destro + altezza inte + 4 di mergine
        1, 0); // colore nero, non riempito
    if(draw_placeholder) return;

    cal_y +=16; // sposta y più in basso dell'altezza del font perché li disegna dal basso

    // Stampa mese e anno nella parte superiore
    sprintf(buffer, "%s %d", months[_time.tm_month - 1], _time.tm_year);
    obdWriteStringCustom(pOBD, (GFXfont *)&Dialog_plain_16, cal_x+10, cal_y, buffer, 1);

    cal_y +=2; // sposta y più in basso per separare il calendario dalla data

    // Disegna un rettangolo attorno al calendario
    obdRectangle(pOBD, 
                cal_x , cal_y,  // angolo superiore sinistro + altezza intestazione
                cal_x + (7 * cell_width), cal_y + (6 * cell_height) +2, // angolo inferiore destro + aaltezza inte + 4 di mergine
                1, 0); // colore nero, non riempito
              
    // Disegna i giorni della settimana (intestazione)
    for(int i = 0; i < 7; i++) {
        obdScaledString(pOBD, cal_x + 3 + (i * cell_width), cal_y + 3, (char*)weekdays[i], FONT_8x8, 0, 256, 256, 0);
        obdDrawLine(pOBD, cal_x + (i * cell_width) , cal_y+1, cal_x + (i * cell_width), cal_y + (6 * cell_height) +2, 1, 0);
    }
    
    // Calcola il giorno della settimana del primo giorno del mese
    // Nota: questa è una versione semplificata, potresti voler usare una funzione più precisa
    struct date_time temp = _time;
    temp.tm_day = 1;
    int first_day = calculateDayOfWeek(temp); // dovrai implementare questa funzione
    
    int day = 1;
    int max_days = getDaysInMonth(_time.tm_month, _time.tm_year); // dovrai implementare questa funzione

    char *holiday_name = NULL;
    int is_holiday = isHoliday(_time, &holiday_name); // festivita' di OGGI, solo per l'etichetta sotto al calendario
    if(is_holiday) {
        obdScaledString(pOBD, cal_x+1, cal_y + 5 + (6 * cell_height), holiday_name, FONT_6x8, 0, 256, 256, 0);
    }

    // Disegna la griglia dei giorni
    for(int row = 0; row < 5; row++) {
        for(int col = 0; col < 7; col++) {
            int current_pos = row * 7 + col;
            if(current_pos >= first_day && day <= max_days) {
                sprintf(buffer, "%2d", day);
                int is_current = (day == _time.tm_day);
                // Le festivita'/domeniche non sono piu' evidenziate in rosso:
                // il rosso non sopravvive al refresh parziale su questo
                // pannello (vedi epd_ssd168x.c), quindi il calendario usa
                // solo il piano nero per restare corretto anche tra un full
                // refresh e l'altro.
                if (is_current) {
                    obdDrawLine(pOBD, cal_x + (col * cell_width) +1, cal_y + 13 + (row * cell_height), cal_x + ((col + 1) * cell_width) -2, cal_y + 13 + (row * cell_height), 1, 0);
                    obdDrawLine(pOBD, cal_x + ((col + 1) * cell_width), cal_y + 13 + (row * cell_height), cal_x + ((col + 1) * cell_width), cal_y + 12 + ((row+1) * cell_height), 1, 0);
                }
                obdScaledString(pOBD, cal_x + (col * cell_width) +1, cal_y + 14 + (row * cell_height), buffer, FONT_6x8, is_current, 256, 256, 0);
                day++;
            }
        }
        obdDrawLine(pOBD, cal_x, cal_y+12+(row * cell_height), cal_x+ (7 * cell_width), cal_y+12+(row * cell_height), 1, 0);
    }
}

int calculateDayOfWeek(struct date_time date) {
    int q = date.tm_day;
    int m = date.tm_month;
    int y = date.tm_year;
    
    if (m <= 2) {
        m += 12;
        y--;
    }
    
    int h = (q + 13*(m+1)/5 + y + y/4 - y/100 + y/400) % 7;
    
    // Converti da formato Zeller (0=Sabato) a formato Lunedì-Domenica (0=Lunedì)
    return ((h + 5) % 7);
}

int getDaysInMonth(int month, int year) {
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
        return 29;
    return days[month-1];
}


// Definizione di una struttura per le festività fisse
struct holiday {
    int day;
    int month;
    const char *name;
};

// Array delle festività fisse (potrebbero essere definite come const globale)
const struct holiday fixed_holidays[] = {
    {1, 1, "Capodanno"},
    {6, 1, "Epifania"},
    {25, 4, "Liberazione"},
    {1, 5, "Festa del Lavoro"},
    {2, 6, "Repubblica"},
    {15, 8, "Ferragosto"},
    {1, 11, "Tutti i Santi"},
    {8, 12, "Immacolata"},
    {25, 12, "Natale"},
    {26, 12, "S.Stefano"},
    {19, 5, "B.day Corrado"},
    {4, 5, "Star Wars Day"},
    {0, 0, NULL}  // terminatore
};

// Funzione per calcolare la Pasqua (algoritmo di Meeus/Jones/Butcher)
void calculateEaster(int year, int *easter_month, int *easter_day) {
    int a = year % 19;
    int b = year / 100;
    int c = year % 100;
    int d = b / 4;
    int e = b % 4;
    int f = (b + 8) / 25;
    int g = (b - f + 1) / 3;
    int h = (19 * a + b - d - g + 15) % 30;
    int i = c / 4;
    int k = c % 4;
    int l = (32 + 2 * e + 2 * i - h - k) % 7;
    int m = (a + 11 * h + 22 * l) / 451;
    *easter_month = (h + l - 7 * m + 114) / 31;
    *easter_day = ((h + l - 7 * m + 114) % 31) + 1;
}

// Funzione per verificare se una data è festiva
int isHoliday(struct date_time date, char **holiday_name) {
    // Controllo festività fisse
    for(int i = 0; fixed_holidays[i].day != 0; i++) {
        if(date.tm_day == fixed_holidays[i].day && 
           date.tm_month == fixed_holidays[i].month) {
            if(holiday_name) *holiday_name = (char*)fixed_holidays[i].name;
            return 1;
        }
    }
    
    // Controllo domeniche
    if(calculateDayOfWeek(date) == 6) {  // 6 = domenica
        if(holiday_name) *holiday_name = "Domenica";
        return 1;
    }
    
    // Calcolo Pasqua e festività correlate
    int easter_month, easter_day;
    calculateEaster(date.tm_year, &easter_month, &easter_day);
    
    // Pasqua
    if(date.tm_day == easter_day && date.tm_month == easter_month) {
        if(holiday_name) *holiday_name = "Pasqua";
        return 1;
    }
    
    // Lunedì dell'Angelo (Pasquetta)
    struct date_time easter = date;
    easter.tm_day = easter_day;
    easter.tm_month = easter_month;
    // Aggiungi un giorno a easter per ottenere Pasquetta
    if(date.tm_day == easter_day + 1 && date.tm_month == easter_month) {
        if(holiday_name) *holiday_name = "Pasquetta";
        return 1;
    }
    
    return 0;
}

// Funzione per trovare il punto di word wrap
static int find_wrap_point(const char *str, int max_chars) {
    int i;
    
    // Se la stringa è più corta del massimo, torna la lunghezza
    if (strlen(str) <= max_chars) {
        return strlen(str);
    }
    
    // Cerca l'ultimo spazio prima del limite
    for (i = max_chars; i > 0; i--) {
        if (str[i] == ' ' || str[i] == '\0') {
            return i;
        }
    }
    
    // Se non ci sono spazi, taglia brutalmente
    return max_chars;
}

// Calcola il layout ottimale
int calculate_optimal_layout(const char *text, int display_width, int display_height, 
                             LayoutResult *result) {
    int text_len = strlen(text);
    int font_idx, max_lines, chars_per_line;
    const char *ptr;
    int current_pos, line, wrap_point;
    
    // Prova ogni font dal più grande al più piccolo
    for (font_idx = 0; font_idx < NUM_FONTS; font_idx++) {
        chars_per_line = display_width / fonts[font_idx].width;
        max_lines = display_height / fonts[font_idx].height;
        
        if (chars_per_line == 0 || max_lines == 0) {
            continue;  // Font troppo grande
        }
        
        // Simula il word wrap per vedere se ci sta
        ptr = text;
        current_pos = 0;
        line = 0;
        
        while (*ptr && line < max_lines) {
            // Trova dove spezzare questa riga
            wrap_point = find_wrap_point(ptr, chars_per_line);
            
            if (wrap_point == 0) {
                break;  // Non ci sta nemmeno un carattere
            }
            
            result->line_starts[line] = current_pos;
            result->line_lengths[line] = wrap_point;
            
            ptr += wrap_point;
            current_pos += wrap_point;
            
            // Salta spazi all'inizio della nuova riga
            while (*ptr == ' ') {
                ptr++;
                current_pos++;
            }
            
            line++;
            
            if (*ptr == '\0') {
                // Stringa finita, tutto OK
                result->font_idx = font_idx;
                result->num_lines = line;
                result->chars_per_line = chars_per_line;
                return 1;  // Successo
            }
        }
        
        // Se siamo qui e la stringa è finita, va bene
        if (*ptr == '\0') {
            result->font_idx = font_idx;
            result->num_lines = line;
            result->chars_per_line = chars_per_line;
            return 1;  // Successo
        }
    }
    
    // Nessun font funziona - usa il più piccolo comunque
    result->font_idx = NUM_FONTS - 1;
    result->num_lines = max_lines;
    result->chars_per_line = chars_per_line;
    return 0;  // Fallimento (testo troncato)
}

// Funzione principale per disegnare il testo ottimizzato
void draw_optimized_text(OBDISP *pOBD, const char *text, 
                        int display_x, int display_y,
                        int display_width, int display_height,
                        int bInvert) {
    LayoutResult layout;
    int i, x, y, total_height, start_y;
    char line_buffer[256];
    
    // Calcola il layout ottimale
    if (!calculate_optimal_layout(text, display_width, display_height, &layout)) {
        // Testo troppo lungo, verrà troncato
    }
    
    const FontInfo *font = &fonts[layout.font_idx];
    
    // Calcola l'altezza totale del testo
    total_height = layout.num_lines * font->height;
    
    // Centra verticalmente
    start_y = display_y + (display_height - total_height) / 2;
    
    // Disegna ogni riga
    for (i = 0; i < layout.num_lines; i++) {
        // Copia la riga nel buffer
        int line_len = layout.line_lengths[i];
        if (line_len >= sizeof(line_buffer)) {
            line_len = sizeof(line_buffer) - 1;
        }
        
        memcpy(line_buffer, text + layout.line_starts[i], line_len);
        line_buffer[line_len] = '\0';
        
        // Calcola larghezza riga per centrare orizzontalmente
        int line_width = strlen(line_buffer) * font->width;
        x = display_x + (display_width - line_width) / 2;
        y = start_y + i * font->height;
        
        // Disegna la riga
        obdScaledString(pOBD, x, y, line_buffer, font->size_code, 
                       bInvert, 256, 256, 0);
    }
}
