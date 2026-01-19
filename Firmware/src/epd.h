#pragma once
#include "etime.h"
#include "OneBitDisplay.h"
#define epd_height 128
#define epd_width 296
#define epd_buffer_size ((epd_height/8) * epd_width)

// Struttura per definire i font disponibili
typedef struct {
    int width;
    int height;
    int size_code;  // Codice per la funzione obdScaledString
} FontInfo;

// Array di font ordinati dal più grande al più piccolo
static const FontInfo fonts[] = {
    {16, 16, FONT_16x16},  // Sostituisci con i tuoi define
    {12, 16, FONT_12x16},
    {8, 8, FONT_8x8},
    {6, 8, FONT_6x8}
};
#define NUM_FONTS (sizeof(fonts) / sizeof(fonts[0]))

// Struttura risultato
typedef struct {
    int font_idx;       // Indice font da usare
    int num_lines;      // Numero di righe
    int chars_per_line; // Caratteri per riga
    int line_starts[10]; // Indici inizio di ogni riga (max 10 righe)
    int line_lengths[10]; // Lunghezza di ogni riga
} LayoutResult;

static int find_wrap_point(const char *str, int max_chars);
int calculate_optimal_layout(const char *text, int display_width, int display_height, LayoutResult *result);
void draw_optimized_text(OBDISP *pOBD, const char *text, int display_x, int display_y, int display_width, int display_height, int bInvert);

void set_EPD_scene(uint8_t scene);
void set_EPD_wait_flush();

void init_epd(void);
uint8_t EPD_read_temp(void);
void EPD_Display(unsigned char *image, unsigned char * red_image, int size, uint8_t full_or_partial);
void epd_display_tiff(uint8_t *pData, int iSize);
void epd_set_sleep(void);
uint8_t epd_state_handler(void);
void epd_display_char(uint8_t data);
void epd_clear(void);

void update_time_scene(struct date_time _time, uint16_t battery_mv, int16_t temperature, void (*scene)(struct date_time, uint16_t, int16_t,  uint8_t));
void epd_update(struct date_time _time, uint16_t battery_mv, int16_t temperature);

void epd_display(struct date_time _time, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial);
void epd_display_time_with_date(struct date_time _time, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial);
void epd_display_info(struct date_time _time, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial);
void epd_display_my(struct date_time _time, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial);
void epd_myScene(struct date_time _time, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial);
void drawTempGraph(OBDISP *pOBD, struct date_time _time, int16_t temperature, int tgr_x, int tgr_y, bool draw_placeholder);
uint8_t scaleTemp(uint8_t temp, uint8_t max, uint8_t min, uint8_t height);
void drawClock(OBDISP *pOBD, struct date_time _time, int cl_x, int cl_y);
void drawMAC(OBDISP *pOBD, int mac_x, int mac_y);
void drawBattery(OBDISP *pOBD, uint16_t battery_mv, int bat_x, int bat_y, bool vertical);
void drawCalendar(OBDISP *pOBD, struct date_time _time, int cal_x, int cal_y, bool draw_placeholder); 

int calculateDayOfWeek(struct date_time date);
int getDaysInMonth(int month, int year);
int isHoliday(struct date_time date, char **holiday_name);