#include "corrado.h"
#include <stdint.h>
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "drivers/8258/flash.h"
#include "uart.h"
#include "main.h"


#if defined EXP_UART
uart_data_t uart_rx_buff = {0,
	{
		0,
	}};
uart_data_t uart_tx_buff = {0,
	{
		0,
	}};

	
_attribute_ram_code_ int nhandle_uart_rx(const uart_data_t *rx)
	{
	return rx->dma_len;
	}
#endif

void init_uart(void)
{
#if defined EXP_UART
    uart_recbuff_init((unsigned char*)&uart_rx_buff, sizeof(uart_rx_buff));
    uart_gpio_set(TXD, RXD);
    uart_reset();
    uart_init(12, 15, PARITY_NONE, STOP_BIT_ONE);  // baud rate: 115200
    uart_irq_enable(0, 0);
    uart_dma_enable(1, 1);
    irq_set_mask(FLD_IRQ_DMA_EN);
    dma_chn_irq_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 1);
  
#else   // Orig
	gpio_set_func(TXD, AS_GPIO);
	gpio_set_output_en(TXD, 1);
	gpio_write(TXD, 0);
	gpio_set_func(RXD, AS_GPIO);
	gpio_set_input_en(RXD, 1);
	gpio_set_output_en(RXD, 0);

	uart_gpio_set(UART_TX_PB1, UART_RX_PA0);
	uart_reset();
	uart_init(12, 15, PARITY_NONE, STOP_BIT_ONE); // baud rate: 115200
	uart_dma_enable(0, 0);
	dma_chn_irq_enable(0, 0);
	uart_irq_enable(0, 0);
	uart_ndma_irq_triglevel(0, 0);
#endif
}

#if defined EXP_UART

_attribute_ram_code_ void puts(const char* str) {
    unsigned int len = strlen(str);
    uart_tx_buff.dma_len = len;
    memcpy(uart_tx_buff.data, str, len);
    uart_dma_send((unsigned char*)&uart_tx_buff);
}

int putchar_custom(int c)
{
    unsigned char b = (unsigned char)c & 0xFF;
    uart_send_byte(b);
    return 0;
}

#else

// 20ms is ~200x the time a single byte actually takes at 115200 baud --
// generous margin for a real transmission, but bounded: if nothing is
// wired to the TX pin (or some other condition keeps uart_tx_is_busy()
// permanently true), we give up on that byte instead of spinning forever.
// This used to be an unbounded wait; that's very likely why puts() was
// found commented out (see u_printf.c) instead of actually fixed.
#define UART_TX_WAIT_TIMEOUT_TICKS (20 * CLOCK_16M_SYS_TIMER_CLK_1MS)

_attribute_ram_code_ static void uart_wait_tx_done(void)
{
	unsigned long t0 = clock_time();
	while (uart_tx_is_busy())
	{
		if (clock_time() - t0 > UART_TX_WAIT_TIMEOUT_TICKS)
			return;
		sleep_us(10);
	}
}

_attribute_ram_code_ void puts(const char *str)
{
	while (*str != '\0')
	{
		uart_ndma_send_byte(*str);
		uart_wait_tx_done();
		str++;
	}
}

int putchar_custom(int c)
{
	uart_ndma_send_byte((char)c);
	uart_wait_tx_done();
	return 0;
}
#endif