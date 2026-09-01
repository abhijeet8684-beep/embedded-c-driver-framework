/**
 * @file driver.c
 * @brief Production-Grade Embedded Firmware Implementations:
 *        - Generic Bitfield Read/Modify Helpers with 32-bit Shift Overflow Guards
 *        - SPX-100 Serial Peripheral Driver with Wall-Clock Timeout & Errata Handling
 *        - High-Performance Relaxed Atomic Counter for High-Frequency ISRs
 *        - Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer
 * 
 * @author Abhijeet (https://github.com/abhijeet8684-beep)
 */

#include "spx_peripheral.h"
#include "ring_buffer.h"
#include <time.h>

/* ============================================================================
 * Platform Timing Hook
 * ============================================================================ */

/**
 * @brief Default platform millisecond tick provider using monotonic clock.
 * @note Marked as weak so bare-metal platforms (e.g. ARM Cortex-M SysTick)
 *       can override this implementation without modifying driver source.
 * @return Monotonic time in milliseconds.
 */
#if defined(__GNUC__)
__attribute__((weak))
#endif
uint32_t spx_get_tick_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((ts.tv_sec * 1000u) + (ts.tv_nsec / 1000000u));
}

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
 * @brief Transmits a byte over the SPX-100 peripheral with configurable wall-clock timeout.
 * @param data Byte payload to transmit.
 * @param timeout_ms Maximum time in milliseconds to wait before timing out.
 * @return 0 on success, -1 on hardware transmission error, -2 on timeout.
 * 
 * @note Relies on spx_get_tick_ms() for elapsed wall-clock tracking, decoupling
 *       timeout duration from CPU clock frequency and optimization flags.
 * @note MMIO Reading Model: A single 32-bit read from SPX_STATUS_OFFSET captures a
 *       coherent snapshot of BUSY, DONE, and ERR. Terminal states (ERR/DONE) take
 *       precedence; if silicon temporarily clears BUSY before DONE is latched,
 *       the loop continues polling until a terminal flag or timeout is reached.
 * @warning Blocking function call. Executes bounded polling up to timeout_ms.
 */
int spx_send_byte_timeout(uint8_t data, uint32_t timeout_ms) {
    /* 1. Clear any pending DONE or ERR status flags via Write-1-to-Clear */
    uint32_t clear = (1u << SPX_STATUS_DONE_BIT) | (1u << SPX_STATUS_ERR_BIT);
    reg_write(SPX_STATUS_OFFSET, clear);

    /* 2. Load byte into transmit FIFO */
    reg_write(SPX_TXDATA_OFFSET, data);

    /* 3. Trigger transmission by asserting START bit in CTRL */
    uint32_t ctrl = reg_read(SPX_CTRL_OFFSET);
    ctrl = set_field(ctrl, SPX_CTRL_START_BIT, 1, 1);
    reg_write(SPX_CTRL_OFFSET, ctrl);

    /* 4. Wall-clock bounded polling loop */
    uint32_t start_tick = spx_get_tick_ms();
    for (;;) {
        uint32_t status = reg_read(SPX_STATUS_OFFSET);
        bool busy = get_field(status, SPX_STATUS_BUSY_BIT, 1) != 0;
        bool done = get_field(status, SPX_STATUS_DONE_BIT, 1) != 0;
        bool err  = get_field(status, SPX_STATUS_ERR_BIT, 1) != 0;

        /* Prioritize error and completion evaluation */
        if (!busy && err)  return -1;
        if (!busy && done) return 0;
        if (err)           return -1;
        if (done)          return 0;

        /* Check wall-clock timeout */
        if ((spx_get_tick_ms() - start_tick) >= timeout_ms) {
            return -2; /* Timeout */
        }
    }
}

/**
 * @brief Transmits a single byte with default timeout (SPX_DEFAULT_TIMEOUT_MS).
 * @param data Byte payload to transmit.
 * @return 0 on success, -1 on hardware transmission error, -2 on timeout.
 */
int spx_send_byte(uint8_t data) {
    return spx_send_byte_timeout(data, SPX_DEFAULT_TIMEOUT_MS);
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
 * @brief Atomic increment invoked from interrupt service routine (ISR) context.
 * 
 * @note MEMORY ORDERING DECISION: __ATOMIC_RELAXED is intentionally chosen over
 *       __ATOMIC_SEQ_CST. This counter represents an independent telemetry / event
 *       counter with no causal happens-before dependencies on surrounding data structures.
 *       __ATOMIC_RELAXED guarantees indivisible RMW operations without emitting expensive
 *       full memory barrier instructions (e.g. DMB on ARM, locked bus on x86), minimizing
 *       latency in high-frequency interrupt contexts.
 */
void counter_isr_increment(void) {
    __atomic_fetch_add(&g_counter, 1, __ATOMIC_RELAXED);
}

/**
 * @brief Atomic load of the shared counter from main execution context.
 * @note Uses __ATOMIC_RELAXED for zero-overhead atomic read of the counter value.
 * @return Current counter value.
 */
uint32_t counter_read(void) {
    return __atomic_load_n(&g_counter, __ATOMIC_RELAXED);
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
 * 
 * @warning Strictly Single-Producer Single-Consumer (SPSC). Calling rb_push from
 *          multiple concurrent threads/ISRs is undefined behavior.
 * @note Memory Model: Uses __ATOMIC_ACQUIRE on tail to observe consumer progress,
 *       and __ATOMIC_RELEASE on head to ensure payload is visible in memory before
 *       the updated head index is published.
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
 * 
 * @warning Strictly Single-Producer Single-Consumer (SPSC). Concurrent multi-consumer
 *          calls without external synchronization are unsafe.
 * @note Memory Model: Uses __ATOMIC_ACQUIRE on head to ensure data is written before
 *       reading, and __ATOMIC_RELEASE on tail to signal slot reclamation.
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
