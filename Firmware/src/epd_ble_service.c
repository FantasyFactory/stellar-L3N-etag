#include "epd_ble_service.h"

#include <stdint.h>
#include "tl_common.h"
#include "stack/ble/ble.h"

#include "epd.h"
#include "ble.h"

extern uint8_t epd_temp[epd_buffer_size];

#define ASSERT_MIN_LEN(val, min_len) \
	if (val < min_len)               \
	{                                \
		return 0;                    \
	}

extern uint8_t epd_buffer[epd_buffer_size];
unsigned int byte_pos = 0;

// epd_temp is ALSO the scene-drawing pipeline's black-plane scratch buffer
// (epd.c, un-rotated OneBitDisplay staging area) -- it gets clobbered with
// unrelated leftover content by every normal clock scene redraw. Only trust
// it as real red-plane data here if THIS protocol actually wrote into it
// (case 0x03, payload[1] != 0xff) since the last clear (case 0x00); otherwise
// pushing (case 0x01) would send whatever stale scene-drawing garbage happens
// to be sitting there, misinterpreted as a red bitmap -- exactly the garbled
// red streaks seen when "push" was sent without a prior clear+upload.
static uint8_t red_plane_dirty = 0;

int epd_ble_handle_write(void *p)
{
	rf_packet_att_write_t *req = (rf_packet_att_write_t *)p;
	uint8_t *payload = &req->value;
	unsigned int payload_len = req->l2capLen - 3;
	uint8_t out_buffer[20] = {0};

	ASSERT_MIN_LEN(payload_len, 1);

	switch (payload[0])
	{
	// Clear EPD display.
	case 0x00:
	    ASSERT_MIN_LEN(payload_len, 2);
		memset(epd_buffer, payload[1], epd_buffer_size);
		memset(epd_temp, payload[1], epd_buffer_size);
		red_plane_dirty = 0;
		ble_set_connection_speed(40);
		return 0;
	// Push buffer to display.
	case 0x01:
		ble_set_connection_speed(200);
		// Must be the ACTIVE panel's byte count, not the epd_buffer_size
		// macro (max static capacity, sized for the biggest supported
		// panel/296px): telling the controller to load more bytes than its
		// own RAM window (see epd_ssd168x.c) desyncs the SPI protocol and
		// hangs, which is exactly what "Clear Screen" was doing before.
		EPD_Display(epd_buffer, red_plane_dirty ? epd_temp : NULL, epd_res_width * epd_res_height / 8, payload[1]);
		return 0;
	// Set byte_pos.
	case 0x02:
		ASSERT_MIN_LEN(payload_len, 3);
		byte_pos = payload[1] << 8 | payload[2];
		return 0;
	// Write data to image buffer.
	case 0x03:
		if ((payload[2] << 8 | payload[3]) + payload_len - 4 >= epd_buffer_size + 1)
		{
		    out_buffer[0] = 0x00;
		    out_buffer[1] = 0x00;
		    bls_att_pushNotifyData(EPD_BLE_CMD_OUT_DP_H, out_buffer, 2);
			return 0;
		}
		if (payload[1] == 0xff) {
		    memcpy(epd_buffer + (payload[2] << 8 | payload[3]), payload + 4, payload_len - 4);
		} else {
		    memcpy(epd_temp + (payload[2] << 8 | payload[3]), payload + 4, payload_len - 4);
		    red_plane_dirty = 1;
		}

		out_buffer[0] = payload_len >> 8;
		out_buffer[1] = payload_len & 0xff;
		bls_att_pushNotifyData(EPD_BLE_CMD_OUT_DP_H, out_buffer, 2);
		return 0;
	case 0x04: // decode & display a TIFF image
		epd_display_tiff(epd_buffer, byte_pos);
		return 0;
	default:
		return 0;
	}

	return 0;
}
