#include <stdint.h>
#include "main.h"
#include "epd.h"
#include "tl_common.h"
#include "stack/ble/ble.h"
#include "vendor/common/blt_common.h"

#include "etime.h"
#include "flash.h"
#include "debug_log.h"

extern settings_struct settings;

// Chunk size for the 0xE9 diagnostic log read-back: kept at the same 20
// bytes already used for the OTA verify read-back (ota.c) since that's
// known to fit a default BLE ATT payload without needing an MTU exchange.
#define DBG_LOG_CHUNK_MAX 18

#define testPin GPIO_PD3
void cmd_parser(void * p){
	rf_packet_att_data_t *req = (rf_packet_att_data_t*)p;
	uint8_t inData = req->dat[0];
	// 0xE8/0xE9/0xEA (diagnostic log enable/read/clear) are deliberately
	// excluded here: logging the 0xE9 "read next chunk" command feeds back
	// into the very buffer it's reading, so a download would never reach
	// "0 bytes remaining" -- the log kept growing by one entry per chunk.
	if (inData != 0xE8 && inData != 0xE9 && inData != 0xEA)
		printf("[CMD] 0x%x 0x%x 0x%x\r\n", inData, req->dat[1], req->dat[2]);
	if(inData == 0xFF){
	gpio_set_func(testPin, AS_GPIO);
	gpio_set_output_en(testPin, 1);
	gpio_set_input_en(testPin, 0); 
	sleep_ms(500);
	}else if(inData == 0xCC){
	gpio_set_func(GPIO_PD2, AS_GPIO);
	gpio_set_output_en(GPIO_PD2, 1);
	gpio_set_input_en(GPIO_PD2, 0); 
	sleep_ms(500);
	}else if(inData == 0x0F){
		settings.advertising_temp_C_or_F = true;//Advertising Temp in F
	}else if(inData == 0x0C){
		settings.advertising_temp_C_or_F = false;//Advertising Temp in C
	}else if(inData == 0xB1){
		epd_display_char(req->dat[1]);
	}else if(inData == 0xB0){
		settings.show_batt_enabled = false;//Disable battery on LCD
	}else if(inData == 0xA0){
		settings.blinking_smiley = false;
		settings.comfort_smiley = false;
	}else if(inData == 0xA1){
		settings.blinking_smiley = false;
		settings.comfort_smiley = false;
	}else if(inData == 0xA2){ 
		settings.blinking_smiley = false;
		settings.comfort_smiley = false;
	}else if(inData == 0xA3){
		settings.blinking_smiley = false;
		settings.comfort_smiley = true; // Comfort Indicator
	}else if(inData == 0xAB){
		settings.blinking_smiley = true;//Smiley blinking
	}else if(inData == 0xFE){
		settings.advertising_interval = req->dat[1];//Set advertising interval with second byte, value*10second / 0=main_delay
	}else if(inData == 0xFA){
		settings.temp_offset = req->dat[1];//Set temp offset, -12,5 - +12,5 °C
	}else if(inData == 0xFC){
		settings.temp_alarm_point = req->dat[1];//Set temp alarm point value divided by 10 for temp in °C
		if(settings.temp_alarm_point==0)settings.temp_alarm_point = 1;
	}else if(inData == 0xDD){// Set time
		uint32_t new_time = (req->dat[1]<<24) +(req->dat[2]<<16) +(req->dat[3]<<8) +(req->dat[4]&0xff);
		set_time(new_time, (req->dat[5]<<8) + req->dat[6], req->dat[7], req->dat[8], req->dat[9]);
	}else if(inData == 0xDE){// Save settings in flash to default
		reset_settings_to_default();
		save_settings_to_flash();
	}else if(inData == 0xDF){// Save current settings in flash
		save_settings_to_flash();
	}
	else if(inData == 0xE0){// force set an EPD model, if it wasnt detect automatically correct
		set_EPD_model(req->dat[1]);
	}
	else if(inData == 0xE1){// force set an EPD scene
		set_EPD_scene(req->dat[1]);
	}
	else if(inData == 0xE2){// force set an EPD scene
		set_EPD_wait_flush();
	}
	else if(inData == 0x80){// switch off led
		set_led_color(0);
	}
	else if(inData == 0x81){// switch ON led
		set_led_color(req->dat[1]);
	}
	else if(inData == 0xE8){// enable/disable diagnostic log capture (persisted)
		settings.debug_log_enabled = req->dat[1] ? 1 : 0;
		dbg_log_set_enabled(settings.debug_log_enabled);
	}
	else if(inData == 0xE9){// read next diagnostic log chunk (response via notify on this same characteristic)
		uint8_t out_buffer[2 + DBG_LOG_CHUNK_MAX];
		uint32_t remaining = 0;
		uint8_t n = dbg_log_read_chunk(&out_buffer[2], DBG_LOG_CHUNK_MAX, &remaining);
		out_buffer[0] = (remaining >> 8) & 0xff; // remaining is capped at DBG_LOG_SIZE (2048), fits 16 bits
		out_buffer[1] = remaining & 0xff;
		bls_att_pushNotifyData(RxTx_CMD_OUT_DP_H, out_buffer, 2 + n);
	}
	else if(inData == 0xEA){// clear diagnostic log
		dbg_log_clear();
	}
}
