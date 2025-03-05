#pragma once


#include <stdint.h>
void init_uart(void);

#if defined EXP_UART
#define UART_BUFF_SIZE 512

typedef struct {
  unsigned int dma_len;
  unsigned char data[UART_BUFF_SIZE];
} uart_data_t __attribute__((aligned(4)));

extern uart_data_t uart_rx_buff;
extern uart_data_t uart_tx_buff;

int nhandle_uart_rx(const uart_data_t *);

#endif

