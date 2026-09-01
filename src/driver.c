/**
 * @file driver.c
 * @brief Core Embedded Firmware Implementations:
 *        - Generic Bitfield Read/Modify Helpers
 *        - SPX-100 Serial Peripheral Driver with Hardware Errata Handling
 *        - Atomic Concurrency-Safe Shared Counter
 *        - Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer
 * 
 * @author Abhijeet (https://github.com/abhijeet8684-beep)
 */

#include "spx_peripheral.h"
#include "ring_buffer.h"

/* ============================================================================
 * 1. Generic Bitfield Manipulation Helpers
 * ============================================================================ */

/**
 * @brief Extracts a multi-bit field from a 32-bit register value.
 * @param reg_val The input 32-bit register word.
 * @param bit_offset The starting bit position (0..31).
 * @param width The width in bits of the field (1..32).
 * @return Right-justified field value.
 */
uint32_t get_field(uint32_t reg_val, uint32_t bit_offset, uint32_t width) {
    /* Guard against 32-bit shift overflow (which triggers undefined behavior in C) */
    uint32_t mask = (width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
    return (reg_val >> bit_offset) & mask;
}

/**
 * @brief Inserts a value into a multi-bit field within a 32-bit register word.
 * @param reg_val The original 32-bit register word.
 * @param bit_offset The starting bit position (0..31).
 * @param width The width in bits of the field (1..32).
 * @param value The value to insert into the field.
 * @return Modified 32-bit register value with only the targeted field updated.
 */
uint32_t set_field(uint32_t reg_val, uint32_t bit_offset, uint32_t width, uint32_t value) {
    uint32_t mask = (width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
    value &= mask;
    reg_val &= ~(mask << bit_offset);
    reg_val |= (value << bit_offset);
    return reg_val;
}

/* ============================================================================
 * 2. SPX-100 Serial Transmit Peripheral Driver
 * ============================================================================ */

/**
 * @brief Checks if the SPX-100 peripheral is currently enabled.
 * @return true if enabled, false otherwise.
 */
bool spx_is_enabled(void) {
    uint32_t ctrl = reg_read(SPX_CTRL_OFFSET);
    return get_field(ctrl, SPX_CTRL_EN_BIT, 1) != 0;
}

/**
 * @brief Initializes and enables the SPX-100 peripheral.
 * @param clkdiv Clock divider setting (0..7).
 * @note Implements Errata A3 workaround: STATUS must be read prior to the first
 *       CTRL write to arm the hardware write latch post-reset.
 */
void spx_init(uint8_t clkdiv) {
    /* Errata A3: Dummy read on STATUS arms the CTRL write-enable latch */
    (void)reg_read(SPX_STATUS_OFFSET);

    /* Configure Control Register: Enable device, set clock divider, keep IE disabled */
    uint32_t ctrl = 0;
    ctrl = set_field(ctrl, SPX_CTRL_EN_BIT, 1, 1);
    ctrl = set_field(ctrl, SPX_CTRL_CLKDIV_BIT, SPX_CTRL_CLKDIV_WIDTH, clkdiv);
    ctrl = set_field(ctrl, SPX_CTRL_IE_BIT, 1, 0);
    reg_write(SPX_CTRL_OFFSET, ctrl);
}

/**
 * @brief Transmits a single byte over the SPX-100 peripheral and awaits resolution.
 * @param data Byte payload to transmit.
 * @return 0 on success, -1 on hardware transmission error, -2 on polling timeout.
 */
int spx_send_byte(uint8_t data) {
    /* 1. Clear any pending DONE or ERR status flags via Write-1-to-Clear */
    uint32_t clear = (1u << SPX_STATUS_DONE_BIT) | (1u << SPX_STATUS_ERR_BIT);
    reg_write(SPX_STATUS_OFFSET, clear);

    /* 2. Load byte into transmit FIFO */
    reg_write(SPX_TXDATA_OFFSET, data);

    /* 3. Trigger transmission by asserting START bit in CTRL */
    uint32_t ctrl = reg_read(SPX_CTRL_OFFSET);
    ctrl = set_field(ctrl, SPX_CTRL_START_BIT, 1, 1);
    reg_write(SPX_CTRL_OFFSET, ctrl);

    /* 4. Poll STATUS until completion, error, or timeout */
    for (uint32_t i = 0; i < 2000000u; i++) {
        uint32_t status = reg_read(SPX_STATUS_OFFSET);
        bool busy = get_field(status, SPX_STATUS_BUSY_BIT, 1) != 0;
        bool done = get_field(status, SPX_STATUS_DONE_BIT, 1) != 0;
        bool err  = get_field(status, SPX_STATUS_ERR_BIT, 1) != 0;

        /* Prioritize error evaluation once the device is no longer busy */
        if (!busy && err)  return -1;
        if (!busy && done) return 0;
    }
    return -2; /* Polling Timeout */
}

/**
 * @brief Non-blocking status inquiry to check if a transfer is in flight.
 * @return true if busy, false if idle.
 */
bool spx_is_busy(void) {
    uint32_t status = reg_read(SPX_STATUS_OFFSET);
    return get_field(status, SPX_STATUS_BUSY_BIT, 1) != 0;
}

/* ============================================================================
 * 3. Atomic ISR-Shared Counter
 * ============================================================================ */

static uint32_t g_counter = 0;

/**
 * @brief Atomic increment invoked from interrupt context.
 *        Guarantees thread-safe increments without read-modify-write lost updates.
 */
void counter_isr_increment(void) {
    __atomic_fetch_add(&g_counter, 1, __ATOMIC_SEQ_CST);
}

/**
 * @brief Atomic load of the shared counter from main execution context.
 * @return Current counter value.
 */
uint32_t counter_read(void) {
    return __atomic_load_n(&g_counter, __ATOMIC_SEQ_CST);
}

/* ============================================================================
 * 4. Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer
 * ============================================================================ */

static uint8_t           rb_buf[RB_CAPACITY];
static volatile uint32_t rb_head = 0;
static volatile uint32_t rb_tail = 0;

/**
 * @brief Pushes a single byte into the ring buffer (Producer / ISR context).
 * @param byte Payload to enqueue.
 * @return 0 on success, -1 if buffer is full.
 */
int rb_push(uint8_t byte) {
    uint32_t head = rb_head;
    uint32_t next = (head + 1u) % RB_CAPACITY;

    /* Check if buffer is full (next head index overlaps tail) */
    if (next == __atomic_load_n(&rb_tail, __ATOMIC_ACQUIRE)) {
        return -1;
    }

    rb_buf[head] = byte;

    /* Release barrier guarantees payload write completes before head index updates */
    __atomic_store_n(&rb_head, next, __ATOMIC_RELEASE);
    return 0;
}

/**
 * @brief Pops a single byte from the ring buffer (Consumer / Main loop context).
 * @param out Pointer to store the dequeued byte.
 * @return 0 on success, -1 if buffer is empty.
 */
int rb_pop(uint8_t *out) {
    uint32_t tail = rb_tail;

    /* Check if buffer is empty */
    if (tail == __atomic_load_n(&rb_head, __ATOMIC_ACQUIRE)) {
        return -1;
    }

    *out = rb_buf[tail];

    /* Release barrier guarantees byte is consumed before tail index updates */
    __atomic_store_n(&rb_tail, (tail + 1u) % RB_CAPACITY, __ATOMIC_RELEASE);
    return 0;
}
