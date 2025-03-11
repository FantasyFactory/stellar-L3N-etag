#include <stdint.h>
#include "etime.h"
#include "tl_common.h"
#include "main.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_bw_213.h"
#include "epd_bwr_213.h"
#include "epd_bw_213_ice.h"
//#include "epd_bwr_154.h"
#include "epd_bwr_296.h"
#include "drivers.h"
#include "stack/ble/ble.h"

#include "battery.h"

#include "OneBitDisplay.h"
#include "TIFF_G4.h"
extern const uint8_t ucMirror[];
#include "font_60.h"
#include "font16.h"
#include "font8.h"
#include "font16zh.h"
#include "font30.h"

RAM uint8_t epd_model = 0; // 0 = Undetected, 1 = BW213, 2 = BWR213, 3 = BWR154, 4 = BW213ICE, 5 BWR296
const char *epd_model_string[] = {"NC", "BW213", "BWR213", "BWR154", "213ICE", "BWR296"};
RAM uint8_t epd_update_state = 0;

RAM uint8_t epd_scene = 2;
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

// Temperature hystory (every 15 minutes)
// 96 values for 24 hours
//
//                        00:00        01:00        02:00        03:00        04:00        05:00        06:00        07:00        08:00        09:00        10:00        11:00        12:00        13:00        14:00        15:00        16:00        17:00        18:00        19:00        20:00        21:00        22:00        23:00
/*
RAM uint8_t min_temp[96]={17,17,17,17, 16,16,16,16, 15,15,15,15, 15,15,15,15, 15,15,15,15, 15,15,15,15, 16,16,16,16, 17,17,18,18, 18,18,17,17, 17,17,16,16, 15,15,15,15, 15,15,15,15, 16,17,18,18, 18,18,18,18, 17,17,17,17, 17,17,17,17, 17,17,17,17, 17,17,17,17, 17,17,17,17, 17,17,18,18, 18,18,18,18, 18,18,18,18, 18,18,18,18, 18,18,17,17};
RAM uint8_t max_temp[96]={21,20,20,19, 19,19,18,18, 17,17,17,17, 17,17,17,17, 17,17,17,17, 17,17,17,17, 17,18,18,18, 19,20,20,21, 21,21,20,20, 20,20,20,20, 19,19,19,19, 19,19,19,19, 19,20,21,21, 22,22,23,23, 22,22,22,22, 21,21,21,21, 21,21,21,21, 21,21,21,21, 21,21,21,21, 21,21,22,22, 22,22,23,23, 23,23,22,22, 22,22,21,21, 21,21,21,21};
RAM uint8_t day_temp[96]={127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127, 127,127,127,127};
*/
RAM uint8_t min_temp[96]={255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255};
RAM uint8_t max_temp[96]={00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00};
RAM uint8_t day_temp[96]={255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255};
/*
RAM uint8_t min_temp[96]={00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00, 00,00,00,00};
RAM uint8_t max_temp[96]={39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39, 39,39,39,39};

RAM uint8_t day_temp[96]={17,17,17,17, 16,16,16,16, 15,15,15,15, 15,15,15,15, 15,15,15,15, 15,15,15,15, 16,16,16,16, 17,17,18,18, 18,18,17,17, 17,17,16,16, 15,15,15,15, 15,15,15,15, 16,17,18,18, 18,18,18,18, 17,17,17,17, 17,17,17,17, 17,17,17,17, 17,17,17,17, 17,17,17,17, 17,17,18,18, 18,18,18,18, 18,18,18,18, 18,18,18,18, 18,18,17,17};
*/


RAM uint16_t myTemp=0;

// With this we can force a display if it wasnt detected correctly
void set_EPD_model(uint8_t model_nr)
{
    epd_model = model_nr;
}

// With this we can force a display if it wasnt detected correctly
void set_EPD_scene(uint8_t scene)
{
    epd_scene = scene;
    set_EPD_wait_flush();
}

void set_EPD_wait_flush() {
    epd_wait_update = 1;
}



// Here we detect what E-Paper display is connected
_attribute_ram_code_ void EPD_detect_model(void)
{
    EPD_init();
    // system power
    EPD_POWER_ON();

    WaitMs(10);
    // Reset the EPD driver IC
    gpio_write(EPD_RESET, 0);
    WaitMs(10);
    gpio_write(EPD_RESET, 1);
    WaitMs(10);

    // Here we neeed to detect it
    if (EPD_BWR_296_detect())
    {
        epd_model = 5;
    }
    else if (EPD_BWR_213_detect())
    {
        epd_model = 2;
    }
//    else if (EPD_BWR_154_detect())// Right now this will never trigger, the 154 is same to 213BWR right now.
//    {
//        epd_model = 3;
//    }
    else if (EPD_BW_213_ice_detect())
    {
        epd_model = 4;
    }
    else
    {
        epd_model = 1;
    }
    epd_model = 5; // FIXME: only for bwr_296
    EPD_POWER_OFF();
}

_attribute_ram_code_ uint8_t EPD_read_temp(void)
{
    if (epd_temperature_is_read)
        return epd_temperature;

    if (!epd_model)
        EPD_detect_model();

    EPD_init();
    // system power
    EPD_POWER_ON();
    WaitMs(5);
    // Reset the EPD driver IC
    gpio_write(EPD_RESET, 0);
    WaitMs(10);
    gpio_write(EPD_RESET, 1);
    WaitMs(10);

    if (epd_model == 1)
        epd_temperature = EPD_BW_213_read_temp();
    else if (epd_model == 2)
        epd_temperature = EPD_BWR_213_read_temp();
//    else if (epd_model == 3)
//        epd_temperature = EPD_BWR_154_read_temp();
    else if (epd_model == 4)
        epd_temperature = EPD_BW_213_ice_read_temp();
    else if (epd_model == 5)
        epd_temperature = EPD_BW_213_ice_read_temp();
        //epd_temperature = EPD_BWR_296_read_temp();

    myTemp = EPD_BWR_296_read_temp();

    EPD_POWER_OFF();

    epd_temperature_is_read = 1;

    return epd_temperature;
}

_attribute_ram_code_ void EPD_Display(unsigned char *image, unsigned char *red_image, int size, uint8_t full_or_partial)
{
    if (!epd_model)
        EPD_detect_model();

    EPD_init();
    // system power
    EPD_POWER_ON();
    WaitMs(5);
    // Reset the EPD driver IC
    gpio_write(EPD_RESET, 0);
    WaitMs(10);
    gpio_write(EPD_RESET, 1);
    WaitMs(10);

    if (epd_model == 1)
        epd_temperature = EPD_BW_213_Display(image, size, full_or_partial);
    else if (epd_model == 2)
        epd_temperature = EPD_BWR_213_Display(image, size, full_or_partial);
//    else if (epd_model == 3)
//        epd_temperature = EPD_BWR_154_Display(image, size, full_or_partial);
    else if (epd_model == 4)
        epd_temperature = EPD_BW_213_ice_Display(image, size, full_or_partial);
    else if (epd_model == 5)
        epd_temperature = EPD_BWR_296_Display_BWR(image, red_image, size, full_or_partial);
        //epd_temperature = EPD_BWR_296_Display(image, size, full_or_partial);

    //myTemp = myEPD_BWR_296_read_temp();

    epd_temperature_is_read = 1;
    epd_update_state = 1;
}

_attribute_ram_code_ void epd_set_sleep(void)
{
    if (!epd_model)
        EPD_detect_model();

    if (epd_model == 1)
        EPD_BW_213_set_sleep();
    else if (epd_model == 2)
        EPD_BWR_213_set_sleep();
//    else if (epd_model == 3)
//        EPD_BWR_154_set_sleep();
    else if (epd_model == 4 || epd_model == 5)
        EPD_BW_213_ice_set_sleep();

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
        if (epd_model == 1)
        {
            if (!EPD_IS_BUSY())
                epd_set_sleep();
        }
        else
        {
            if (EPD_IS_BUSY())
                epd_set_sleep();
        }
        break;
    }
    return epd_update_state;
}

_attribute_ram_code_ void FixBuffer(uint8_t *pSrc, uint8_t *pDst, uint16_t width, uint16_t height)
{
    int x, y;
    uint8_t *s, *d;
    for (y = 0; y < (height / 8); y++)
    { // byte rows
        d = &pDst[y];
        s = &pSrc[y * width];
        for (x = 0; x < width; x++)
        {
            d[x * (height / 8)] = ~ucMirror[s[width - 1 - x]]; // invert and flip
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
    epd_clear();
    TIFF_openRAW(&tiff, 250, 122, BITDIR_MSB_FIRST, pData, iSize, TIFFDraw);
    TIFF_setDrawParameters(&tiff, 65536, TIFF_PIXEL_1BPP, 0, 0, 250, 122, NULL);
    TIFF_decode(&tiff);
    TIFF_close(&tiff);
    EPD_Display(epd_buffer, NULL, epd_buffer_size, 1);
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
    uint16_t resolution_w = 250;
    uint16_t resolution_h = 128; // 122 real pixel, but needed to have a full byte
    if (epd_model == 1)
    {
        resolution_w = 250;
        resolution_h = 128; // 122 real pixel, but needed to have a full byte
    }
    else if (epd_model == 2)
    {
        resolution_w = 250;
        resolution_h = 128; // 122 real pixel, but needed to have a full byte
    }
    else if (epd_model == 3)
    {
        resolution_w = 200;
        resolution_h = 200;
    }
    else if (epd_model == 4)
    {
        resolution_w = 212;
        resolution_h = 104;
    }
    else if (epd_model == 5)
    {
        resolution_w = 296;
        resolution_h = 128;
    }

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
    sprintf(buff, "-----%d'C-----", EPD_read_temp());
    obdWriteStringCustom(&obd, (GFXfont *)&Special_Elite_Regular_30, 10, 95, (char *)buff, 1);
    sprintf(buff, "Battery %dmV  %d%%", battery_mv, battery_level);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 10, 120, (char *)buff, 1);
    FixBuffer(epd_temp, epd_buffer, resolution_w, resolution_h);
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

    obdCreateVirtualDisplay(&obd, epd_width, epd_height, epd_temp);
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

    FixBuffer(epd_temp, epd_buffer, epd_width, epd_height);

    EPD_Display(epd_buffer, NULL, epd_width * epd_height / 8, full_or_partial);
}



void epd_display_info(struct date_time _time, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial) {
    uint16_t battery_level;

    if (epd_update_state)
        return;

    if (!epd_model)
    {
        EPD_detect_model();
    }
    uint16_t resolution_w = 250;
    uint16_t resolution_h = 128; // 122 real pixel, but needed to have a full byte
    if (epd_model == 1)
    {
        resolution_w = 250;
        resolution_h = 128; // 122 real pixel, but needed to have a full byte
    }
    else if (epd_model == 2)
    {
        resolution_w = 250;
        resolution_h = 128; // 122 real pixel, but needed to have a full byte
    }
    else if (epd_model == 3)
    {
        resolution_w = 200;
        resolution_h = 200;
    }
    else if (epd_model == 4)
    {
        resolution_w = 212;
        resolution_h = 104;
    }
    else if (epd_model == 5)
    {
        resolution_w = 296;
        resolution_h = 128;
    }

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
    sprintf(buff, "-----%d'C-----", EPD_read_temp());
    obdWriteStringCustom(&obd, (GFXfont *)&Special_Elite_Regular_30, 10, 95, (char *)buff, 1);
    sprintf(buff, "Battery %dmV  %d%%", battery_mv, battery_level);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 10, 120, (char *)buff, 1);
    FixBuffer(epd_temp, epd_buffer, resolution_w, resolution_h);
    EPD_Display(epd_buffer, NULL, resolution_w * resolution_h / 8, full_or_partial);
}



void epd_display_my(struct date_time _time, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial) {
    uint16_t battery_level;

    epd_clear();

    obdCreateVirtualDisplay(&obd, epd_width, epd_height, epd_temp);
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

    FixBuffer(epd_temp, epd_buffer, epd_width, epd_height);

    EPD_Display(epd_buffer, NULL, epd_width * epd_height / 8, full_or_partial);
}

void epd_myScene(struct date_time _time, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial) {

    epd_clear();

    obdCreateVirtualDisplay(&obd, epd_width, epd_height, epd_temp);
    obdFill(&obd, 0, 0); // fill with white

    // Calendario
    drawCalendar(&obd, _time, 47, 0, false);

    // Orologio
    drawClock(&obd, _time, 148, 1);
 
    //Grafico temperatura
    drawTempGraph(&obd, _time, temperature, 148, 43, false);

    // MAC address
    drawMAC(&obd, 48, 107);

    // battery level
    drawBattery(&obd, battery_mv, 252, 107, false);
    //drawBattery(&obd, battery_mv, 285, 1, true);

    FixBuffer(epd_temp, epd_buffer, epd_width, epd_height);

    EPD_Display(epd_buffer, NULL, epd_width * epd_height / 8, full_or_partial);             

}

void drawTempGraph(OBDISP *pOBD, struct date_time _time, int16_t temperature, int tgr_x, int tgr_y, bool draw_placeholder)  {
    uint8_t day_min=99;
    uint8_t day_max=0;
    uint8_t graph_height=48;
    uint8_t graph_width=96;
    uint8_t grx_offset=tgr_x+51;
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

    // Calcola i valori massimi e minimi del grafico
    for(int tx = 0; tx < 96; tx++) {
        if((min_temp[tx]!=255) && (min_temp[tx] < day_min)) day_min = min_temp[tx];
        if((max_temp[tx]!=0) && (max_temp[tx] > day_max)) day_max = max_temp[tx];
    }

    int temp_idx=_time.tm_hour*4+_time.tm_min/15; // indice nel grafico della temperatura: 96 byte, uno ogni 15 minuti

    sprintf(buffer, "%d", day_temp[temp_idx]);  // Temperatura al momento
    obdWriteStringCustom(pOBD, (GFXfont *)&Special_Elite_Regular_30, tgr_x, tgr_y +24, (char *)buffer, 1);
    obdScaledString(pOBD, tgr_x+34, tgr_y+2, "o", FONT_8x8, 0, 256, 256, 0);

    sprintf(buffer, "G:%d-%d", day_min, day_max); // Minima e massima del grafico
    obdScaledString(pOBD, tgr_x+1, tgr_y+32, (char *)buffer, FONT_6x8, 0, 256, 256, 0);
    sprintf(buffer, "H:%d-%d", min_temp[temp_idx], min_temp[temp_idx]); // Minima e massima nella stessa ora
    obdScaledString(pOBD, tgr_x+1, tgr_y+41, (char *)buffer, FONT_6x8, 0, 256, 256, 0);

    for(int ty = day_min; ty <= day_max; ty+=2) {
        obdDrawLine(pOBD, grx_offset -3, tgr_y + graph_height - scaleTemp(ty,day_max,day_min,graph_height), grx_offset -1, tgr_y + graph_height - scaleTemp(ty,day_max,day_min,graph_height), 1, 0);
    }
    obdDrawLine(pOBD, grx_offset + temp_idx, tgr_y + graph_height, grx_offset + temp_idx, tgr_y + graph_height +2, 1, 0);

    for(int tx = 0; tx < graph_width; tx++) {
        if(max_temp[tx]==0 && min_temp[tx]==255) {
            int min_y=scaleTemp(min_temp[tx],day_max,day_min,graph_height);
            int max_y=scaleTemp(max_temp[tx],day_max,day_min,graph_height);
            for(int ty = min_y; ty <= max_y; ty++) {
                if((tx%2==0 && ty%2==0) || (tx%2==1 && ty%2==1)) { // solo se x e y sono pari o x e y sono dispari, in modo da fare un reticolo
                    obdSetPixel(pOBD, grx_offset + tx, tgr_y + graph_height - ty , 1, 0);
                }
            }
        }
        if(day_temp[tx]!=255)
            obdSetPixel(pOBD, grx_offset +tx, tgr_y + graph_height - scaleTemp(day_temp[tx],day_max,day_min,graph_height), 1, 0);
    }

    
    sprintf(buffer, "T: (%u,%u) idx=%d=%d (%u,%u)", (temperature >> 8) & 0xFF, temperature & 0xFF, temp_idx,  day_temp[temp_idx], (myTemp >> 8) & 0xFF, myTemp & 0xFF);
    obdScaledString(pOBD, 48, 97, (char *)buffer, FONT_8x8, 0, 256, 256, 0);

}

uint8_t scaleTemp(uint8_t temp, uint8_t max, uint8_t min, uint8_t height) {
    return ((height/(max-min))*(temp-min));
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
    char buffer[32];
    uint16_t battery_level;
    battery_level = get_battery_level(battery_mv);
    sprintf(buffer, "%d%%", battery_level);
    if(vertical) {
        obdRectangle(pOBD, bat_x+5, bat_y, bat_x +7, bat_y +2, 1, 1);
        obdRectangle(pOBD, bat_x, bat_y+3, bat_x + 10, bat_y +40, 1, 1);
        obdScaledString(pOBD, bat_x +9, bat_y +7, (char *)buffer, FONT_8x8, 1, 256, 256, ROT_90);
    } else {
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

    struct date_time check_date = _time;
    char *holiday_name = NULL;
    int is_holiday = isHoliday(_time, &holiday_name);
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
                // Usa il colore appropriato (rosso per festivi)
                uint8_t text_color = is_holiday ? 2 : 1;  // 2 = rosso, 1 = nero
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

