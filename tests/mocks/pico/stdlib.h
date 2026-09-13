#pragma once
#include <stdbool.h>
#include <stdint.h>

void gpio_init(unsigned pin);
void gpio_set_dir(unsigned pin, bool output);
void gpio_put(unsigned pin, bool value);
void sleep_us(uint64_t duration);
void sleep_ms(uint32_t duration);
uint32_t time_us_32(void);
int getchar_timeout_us(uint32_t timeout);
void stdio_flush(void);