#pragma once
#include <stdbool.h>
#define uart0 ((void *)1)
bool uart_is_writable(void *uart);
void uart_putc_raw(void *uart, char c);
