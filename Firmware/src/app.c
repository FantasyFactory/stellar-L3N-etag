#include "corrado.h"
#include <stdint.h>
#include "tl_common.h"
#include "app.h"
#include "main.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "vendor/common/blt_common.h"

#include "battery.h"
#include "ble.h"
#include "flash.h"
#include "ota.h"
#include "epd.h"
#include "etime.h"
#include "bart_tif.h"
#include "debug_log.h"

RAM uint8_t battery_level;
RAM uint16_t battery_mv;
RAM int16_t temperature;

// Settings
extern settings_struct settings;

#if defined EXP_UART
RAM uint16_t gUART_RX_Counter = 0;
_attribute_ram_code_ void inc_UART_RX_Counter(int r)
{
    gUART_RX_Counter += r;
}
#endif

// Provided by static_src/boot.link. There's no heap/RTOS on this build (the
// FreeRTOS component is linked in but no task/scheduler is ever started --
// this firmware is a bare superloop, see main.c), so the only meaningful
// "memory status" we can report is the static footprint the linker already
// computed: how much of the 32K deep-sleep-retention SRAM budget is used
// (blc_pm_setDeepsleepRetentionType(..._LOW32K) in ble.c), and how much
// non-retained scratch RAM (.bss) the rest of the firmware needs while awake.
extern uint8_t _retention_data_start_[];
extern uint8_t _retention_data_end_[];
extern uint8_t _start_bss_[];
extern uint8_t _end_bss_[];

static void log_meminfo(void)
{
    unsigned int retention_bytes = (unsigned int)(_retention_data_end_ - _retention_data_start_);
    unsigned int bss_bytes = (unsigned int)(_end_bss_ - _start_bss_);
    printf("[MEM] retention=%d/32768B bss=%dB\r\n", retention_bytes, bss_bytes);
}

// Set once the firmware is up and running normally, and left set (retention
// RAM survives both a watchdog-triggered reset and a normal deep-retention
// sleep -- only an actual power loss clears it back to 0). So if we get here
// via user_init_normal() (i.e. NOT the deep-retention-wake path) and find
// this already set, we know it wasn't a fresh power-on: something forced a
// real reset, most likely the watchdog in main_loop() (see app_config.h).
#define BOOT_CANARY_MAGIC 0xC0FFEE01
RAM uint32_t boot_canary = 0;

_attribute_ram_code_ void user_init_normal(void)
{                            // this will get executed one time after power up
    random_generator_init(); // must
    init_time();
#if defined EXP_UART

#else
    init_ble();
    init_nfc();
#endif
    init_flash();
    // OR, not assign: a debug build's hardcoded default in debug_log.c
    // should not be clobbered back off just because it was never (yet)
    // persisted via BLE cmd 0xE8+0xDF -- only the persisted setting being
    // *on* can turn logging on here, it never turns it off.
    if (settings.debug_log_enabled)
        dbg_log_set_enabled(1);
    if (boot_canary == BOOT_CANARY_MAGIC)
        printf("[BOOT] unexpected reset (watchdog or crash) -- was already running\r\n");
    else
        printf("[BOOT] cold start\r\n");
    boot_canary = BOOT_CANARY_MAGIC;
    log_meminfo();
    // epd_display_tiff((uint8_t *)bart_tif, sizeof(bart_tif));
    // epd_display(3334533);
}

_attribute_ram_code_ void user_init_deepRetn(void)
{ // after sleep this will get executed
    blc_ll_initBasicMCU();
    rf_set_power_level_index(RF_POWER_P3p01dBm);
    blc_ll_recoverDeepRetention();
}

// Tracks how long the EPD has been continuously "busy" (epd_update_state
// stuck at 1, BUSY pin never clearing) so the watchdog feed below can also
// catch that failure mode, not just a true CPU hang -- see app_config.h.
RAM uint32_t epd_busy_since = 0;
RAM uint8_t epd_was_busy = 0;

_attribute_ram_code_ void main_loop(void)
{
    blt_sdk_main_loop();
    handler_time();
    uint8_t flag = 0;
    if (time_reached_period(Timer_CH_1, 30))
    {
        battery_mv = get_battery_mv();
        battery_level = get_battery_level(battery_mv);
        temperature = get_temperature_c();
        set_adv_data(EPD_read_temp() * 10, battery_level, battery_mv);
        ble_send_battery(battery_level);
        ble_send_temp(EPD_read_temp() * 10);
        flag = 1;
    }
    if (!flag && time_reached_period(Timer_CH_3, 17)) {
        set_air_tag_adv_data();
    }

    epd_update(get_time(), battery_mv, temperature); //RECE

    if (time_reached_period(Timer_CH_0, 10))
    {
        if (ble_get_connected())
            set_led_color(3);
        else
            set_led_color(2);
        WaitMs(1);
        set_led_color(0);
    }

    uint8_t epd_busy = epd_state_handler(); // if epd_update is ongoing enable gpio wakeup to put the display to sleep as fast as possible
    if (epd_busy)
    {
        if (!epd_was_busy)
        {
            epd_busy_since = clock_time();
            epd_was_busy = 1;
        }
        cpu_set_gpio_wakeup(EPD_BUSY, 1, 1);
        bls_pm_setWakeupSource(PM_WAKEUP_PAD);
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
    }
    else
    {
        epd_was_busy = 0;
        blt_pm_proc();
    }

    // The "stop feeding if EPD stays busy too long" idea (WATCHDOG_EPD_STUCK_LIMIT_MS)
    // is DISABLED: the very first main_loop() after any boot always forces a
    // full refresh (hour_refresh/minute_refresh sentinels), and a full BWR
    // refresh apparently takes longer than the 8s we'd assumed -- causing a
    // reset (and reboot loop, since every boot repeats the same first full
    // refresh) on totally legitimate, expected behaviour. Until we know the
    // actual worst-case refresh duration for this panel, just feed
    // unconditionally every iteration: this still catches genuine CPU hangs
    // (main_loop() not returning at all), which was the primary goal, without
    // risking false positives during a slow-but-normal EPD operation.
    wd_clear();
}
