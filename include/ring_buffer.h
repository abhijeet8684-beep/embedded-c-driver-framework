#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * Bitfield Manipulation & Concurrency API
 * ============================================================================ */

/* Generic 32-bit Bitfield Helpers */
uint32_t get_field(uint32_t reg_val, uint32_t bit_offset, uint32_t width);
uint32_t set_field(uint32_t reg_val, uint32_t bit_offset, uint32_t width, uint32_t value);

/* Atomic ISR-Shared Counter API */
void     counter_isr_increment(void);
uint32_t counter_read(void);

/* Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer API */
#define RB_CAPACITY 16

int rb_push(uint8_t byte);
int rb_pop(uint8_t *out);

#endif /* RING_BUFFER_H */
