#pragma once
#include <stdint.h>
#include <stddef.h>

/* C-lowered TezzNative calls use signed 64-bit argument/result slots.
 * Pointer arguments contain an address converted through uintptr_t.
 * UART reads are nonblocking. Status functions return zero or -1. */
int64_t tn_hw_init(void);
int64_t tn_hw_target_id(void);
int64_t tn_hw_uart_write(int64_t address, int64_t length);
int64_t tn_hw_uart_read(int64_t address, int64_t capacity);
int64_t tn_hw_micros(void);
int64_t tn_hw_delay_us(int64_t microseconds);
int64_t tn_hw_irq_enable(void);
int64_t tn_hw_irq_disable(void);
int64_t tn_hw_irq_state(void);
_Noreturn void tn_hw_finish(int64_t status);
int64_t tn_main(void);
