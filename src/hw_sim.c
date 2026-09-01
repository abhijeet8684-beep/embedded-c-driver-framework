/**
 * @file hw_sim.c
 * @brief Hardware simulation engine for SPX-100 serial device.
 *        Models asynchronous transfer timing, register side-effects,
 *        and silicon errata (CTRL write latch post-reset).
 */

#include "spx_peripheral.h"
#include "hw_sim.h"

#include <pthread.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static uint32_t g_ctrl;
static uint32_t g_status;
static uint32_t g_txdata;
static uint32_t g_txdata_loaded;

static int      g_latch_armed;
static uint32_t g_status_read_count;
static uint32_t g_txdata_write_count;
static uint32_t g_start_write_count;
static unsigned int g_transfer_delay_ms = 5;

static pthread_t g_worker;
static int       g_worker_running;
static int       g_transfer_pending;
static int       g_transfer_had_data;
static int       g_force_error_next;

static void *transfer_worker(void *arg) {
    (void)arg;
    for (;;) {
        struct timespec ts = {0, 200 * 1000}; /* 0.2ms poll tick */
        nanosleep(&ts, NULL);

        pthread_mutex_lock(&g_lock);
        if (!g_worker_running) {
            pthread_mutex_unlock(&g_lock);
            break;
        }
        if (g_transfer_pending) {
            pthread_mutex_unlock(&g_lock);
            struct timespec delay = {0, (long)g_transfer_delay_ms * 1000 * 1000};
            nanosleep(&delay, NULL);
            pthread_mutex_lock(&g_lock);
            if (g_transfer_pending) {
                g_status &= ~(1u << SPX_STATUS_BUSY_BIT);
                if (!g_transfer_had_data || g_force_error_next) {
                    g_status |= (1u << SPX_STATUS_ERR_BIT);
                    g_force_error_next = 0;
                } else {
                    g_status |= (1u << SPX_STATUS_DONE_BIT);
                }
                g_transfer_pending = 0;
            }
        }
        pthread_mutex_unlock(&g_lock);
    }
    return NULL;
}

void hw_sim_reset(void) {
    pthread_mutex_lock(&g_lock);
    g_ctrl = 0;
    g_status = 0;
    g_txdata = 0;
    g_txdata_loaded = 0;
    g_latch_armed = 0;
    g_status_read_count = 0;
    g_txdata_write_count = 0;
    g_start_write_count = 0;
    g_transfer_delay_ms = 5;
    g_transfer_pending = 0;
    g_transfer_had_data = 0;
    g_force_error_next = 0;
    pthread_mutex_unlock(&g_lock);

    if (!g_worker_running) {
        g_worker_running = 1;
        pthread_create(&g_worker, NULL, transfer_worker, NULL);
    }
}

uint32_t hw_sim_peek(uint32_t offset) {
    uint32_t v = 0;
    pthread_mutex_lock(&g_lock);
    switch (offset) {
        case SPX_CTRL_OFFSET:   v = g_ctrl; break;
        case SPX_STATUS_OFFSET: v = g_status; break;
        case SPX_TXDATA_OFFSET: v = g_txdata; break;
        default: v = 0xFFFFFFFFu; break;
    }
    pthread_mutex_unlock(&g_lock);
    return v;
}

int hw_sim_latch_armed(void) {
    int v;
    pthread_mutex_lock(&g_lock);
    v = g_latch_armed;
    pthread_mutex_unlock(&g_lock);
    return v;
}

uint32_t hw_sim_status_read_count(void) {
    uint32_t v;
    pthread_mutex_lock(&g_lock);
    v = g_status_read_count;
    pthread_mutex_unlock(&g_lock);
    return v;
}

uint32_t hw_sim_txdata_write_count(void) {
    uint32_t v;
    pthread_mutex_lock(&g_lock);
    v = g_txdata_write_count;
    pthread_mutex_unlock(&g_lock);
    return v;
}

uint32_t hw_sim_start_write_count(void) {
    uint32_t v;
    pthread_mutex_lock(&g_lock);
    v = g_start_write_count;
    pthread_mutex_unlock(&g_lock);
    return v;
}

void hw_sim_force_next_error(void) {
    pthread_mutex_lock(&g_lock);
    g_force_error_next = 1;
    pthread_mutex_unlock(&g_lock);
}

void hw_sim_set_transfer_delay_ms(unsigned int ms) {
    pthread_mutex_lock(&g_lock);
    g_transfer_delay_ms = ms;
    pthread_mutex_unlock(&g_lock);
}

uint32_t reg_read(uint32_t offset) {
    uint32_t v = 0;
    pthread_mutex_lock(&g_lock);
    switch (offset) {
        case SPX_CTRL_OFFSET:
            v = g_ctrl;
            break;
        case SPX_STATUS_OFFSET:
            v = g_status;
            g_status_read_count++;
            g_latch_armed = 1; /* Errata A3: First read arms latch */
            break;
        case SPX_TXDATA_OFFSET:
            v = g_txdata;
            break;
        default:
            v = 0xFFFFFFFFu;
            break;
    }
    pthread_mutex_unlock(&g_lock);
    return v;
}

void reg_write(uint32_t offset, uint32_t value) {
    pthread_mutex_lock(&g_lock);
    switch (offset) {
        case SPX_CTRL_OFFSET: {
            if (!g_latch_armed) {
                /* Errata A3: Write dropped if latch not armed */
                pthread_mutex_unlock(&g_lock);
                return;
            }
            uint32_t clean = value & 0x3Fu;
            int start_bit = (clean >> SPX_CTRL_START_BIT) & 1u;
            g_ctrl = clean & ~(1u << SPX_CTRL_START_BIT);
            if (start_bit) {
                g_start_write_count++;
                if ((g_ctrl & (1u << SPX_CTRL_EN_BIT)) != 0) {
                    g_status |= (1u << SPX_STATUS_BUSY_BIT);
                    g_transfer_pending = 1;
                    g_transfer_had_data = g_txdata_loaded;
                    g_txdata_loaded = 0;
                }
            }
            break;
        }
        case SPX_STATUS_OFFSET: {
            if (value & (1u << SPX_STATUS_DONE_BIT))
                g_status &= ~(1u << SPX_STATUS_DONE_BIT);
            if (value & (1u << SPX_STATUS_ERR_BIT))
                g_status &= ~(1u << SPX_STATUS_ERR_BIT);
            break;
        }
        case SPX_TXDATA_OFFSET:
            g_txdata = value & 0xFFu;
            g_txdata_loaded = 1;
            g_txdata_write_count++;
            break;
        default:
            break;
    }
    pthread_mutex_unlock(&g_lock);
}
