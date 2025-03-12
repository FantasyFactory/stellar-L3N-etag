#include "corrado.h"
#include <stdint.h>
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "vendor/common/user_config.h"
#include "drivers/8258/gpio_8258.h"
#include "app_config.h"
#include "main.h"
#include "app.h"
#include "battery.h"
#include "ble.h"
#include "cmd_parser.h"
#include "epd.h"
#include "flash.h"
#include "i2c.h"
#include "led.h"
#include "nfc.h"
#include "ota.h"
#include "uart.h"

#if defined EXP_UART
RAM unsigned char uart_dma_irqsrc;

_attribute_ram_code_ void irq_handler(void)
{
    //gpio_toggle(LED_RED);
    
    //irq_blt_sdk_handler();
	
    uart_dma_irqsrc = dma_chn_irq_status_get();
	
    if (uart_dma_irqsrc & FLD_DMA_CHN_UART_RX) {
        dma_chn_irq_status_clr(FLD_DMA_CHN_UART_RX);
       
        inc_UART_RX_Counter(nhandle_uart_rx(&uart_rx_buff));
        
        //sleep_us(100);
        //printf("[irq_handler] Received %u bytes\n", uart_rx_buff.dma_len);
        
        //printf("v%d:NICKT: UART RX. \r\n", NICKT_VERSION);
    }
    
    if (uart_dma_irqsrc & FLD_DMA_CHN_UART_TX) {
        //gpio_toggle(LED_GREEN);
        dma_chn_irq_status_clr(FLD_DMA_CHN_UART_TX);
        //sleep_us(75);
        while(uart_tx_is_busy()) { 
          sleep_us(100);
        }
        //gpio_toggle(LED_GREEN);
    }
    
    //gpio_toggle(LED_RED);
}
#else
_attribute_ram_code_ __attribute__((optimize("-Os"))) void irq_handler(void)
{
	irq_blt_sdk_handler();
}
#endif

_attribute_ram_code_ int main (void)    //must run in ramcode
{
	blc_pm_select_internal_32k_crystal();
	cpu_wakeup_init();
	int deepRetWakeUp = pm_is_MCU_deepRetentionWakeup();  //MCU deep retention wakeUp
	rf_drv_init(RF_MODE_BLE_1M);
	gpio_init( !deepRetWakeUp );  //analog resistance will keep available in deepSleep mode, so no need initialize again
#if (CLOCK_SYS_CLOCK_HZ == 16000000)
	clock_init(SYS_CLK_16M_Crystal);
#elif (CLOCK_SYS_CLOCK_HZ == 24000000)
	clock_init(SYS_CLK_24M_Crystal);
#endif
	blc_app_loadCustomizedParameters();
	
    init_led();
	init_uart();
	init_i2c();
		
	if( deepRetWakeUp ){
		user_init_deepRetn ();
		printf("\r\n\r\n\r\nBooting deep\r\n\r\n\r\n\r\n");
	}
	else{
		printf("\r\n\r\n\r\nBooting\r\n\r\n\r\n\r\n");
		user_init_normal ();
	}	
    irq_enable();
	while (1) {
		main_loop ();
	}
}

