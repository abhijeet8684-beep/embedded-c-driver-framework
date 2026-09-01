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

/**
 * @brief Enqueues a byte into the lock-free circular queue.
 * @param byte Data byte to enqueue.
 * @return 0 on success, -1 if the buffer is full.
 * @warning Strictly Single-Producer Single-Consumer (SPSC). Calling rb_push from
 *          multiple concurrent threads/ISRs without external locking will cause
 *          lost updates and race conditions.
 * @note Actual usable capacity is (RB_CAPACITY - 1) bytes to unambiguously
 *       distinguish full from empty states without an extra counter.
 */
int rb_push(uint8_t byte);

/**
 * @brief Dequeues a byte from the lock-free circular queue.
 * @param out Destination pointer for the dequeued byte.
 * @return 0 on success, -1 if the buffer is empty.
 * @warning Strictly Single-Producer Single-Consumer (SPSC). Calling rb_pop from
 *          multiple concurrent consumers without external locking is unsafe.
 */
int rb_pop(uint8_t *out);

#endif /* RING_BUFFER_H */
