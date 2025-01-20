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
    else if (epd_model == 4 || epd_model == 5)
        epd_temperature = EPD_BW_213_ice_read_temp();

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
            update_time_scene(_time, battery_mv, temperature, drawCalendar);
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


void drawCalendar(struct date_time _time, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial) {
    const char *months[] = {"Gennaio", "Febbraio", "Marzo", "Aprile", "Maggio", "Giugno",
                           "Luglio", "Agosto", "Settembre", "Ottobre", "Novembre", "Dicembre"};
    const char *weekdays[] = {"L", "M", "M", "G", "V", "S", "D"};
    char buffer[32];
    int x=32, y=0;
    int cell_width = 34;  // larghezza di ogni cella
    int cell_height = 17; // altezza di ogni cella

    epd_clear();

    obdCreateVirtualDisplay(&obd, epd_width, epd_height, epd_temp);
    obdFill(&obd, 0, 0); // fill with white
    
    // Stampa mese e anno nella parte superiore
    sprintf(buffer, "%s %d", months[_time.tm_month - 1], _time.tm_year);
    obdWriteString(&obd, 0, x + 30, y, buffer, 1, 0, 1);
    
    // Disegna i giorni della settimana (intestazione)
    for(int i = 0; i < 7; i++) {
        obdWriteString(&obd, 0, x + (i * cell_width), y + 25, 
                      (char*)weekdays[i], 1, 1, 1); // invertito
    }
    
    // Calcola il giorno della settimana del primo giorno del mese
    // Nota: questa è una versione semplificata, potresti voler usare una funzione più precisa
    struct date_time temp = _time;
    temp.tm_day = 1;
    int first_day = calculateDayOfWeek(temp); // dovrai implementare questa funzione
    
    int day = 1;
    int max_days = getDaysInMonth(_time.tm_month, _time.tm_year); // dovrai implementare questa funzione
    
    // Disegna la griglia dei giorni
    for(int row = 0; row < 5; row++) {
        for(int col = 0; col < 7; col++) {
            int current_pos = row * 7 + col;
            if(current_pos >= first_day && day <= max_days) {
                sprintf(buffer, "%2d", day);
                // Inverte il colore per il giorno corrente
                int is_current = (day == _time.tm_day);
                obdWriteString(&obd, 0, 
                             x + (col * cell_width),
                             y + 40 + (row * cell_height),
                             buffer, 1, is_current, 1);
                day++;
            }
        }
    }
    
    // Disegna un rettangolo attorno al calendario
    obdRectangle(&obd, 
                x - 5, y + 20,  // angolo superiore sinistro
                x + (7 * cell_width), y + 40 + (5 * cell_height), // angolo inferiore destro
                1, 0); // colore nero, non riempito

    FixBuffer(epd_temp, epd_buffer, epd_width, epd_height);

    EPD_Display(epd_buffer, NULL, epd_width * epd_height / 8, full_or_partial);             
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


