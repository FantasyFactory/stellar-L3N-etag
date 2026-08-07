#pragma once


#include <stdint.h>
void init_uart(void);
int putchar_custom(int c);
// puts() is intentionally NOT declared here: GCC treats it as a built-in
// (int puts(const char*)) and our uart.c version returns void, so declaring
// it in a widely-included header spams a "conflicting types" warning into
// every file that includes uart.h. Callers that need it (currently only
// u_printf.c) declare it locally instead.

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

