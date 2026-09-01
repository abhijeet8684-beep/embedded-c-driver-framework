#ifndef HW_SIM_H
#define HW_SIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hardware simulation control interface */
void     hw_sim_reset(void);
uint32_t hw_sim_peek(uint32_t offset);
int      hw_sim_latch_armed(void);
uint32_t hw_sim_status_read_count(void);
uint32_t hw_sim_txdata_write_count(void);
uint32_t hw_sim_start_write_count(void);
void     hw_sim_force_next_error(void);
void     hw_sim_set_transfer_delay_ms(unsigned int ms);

#ifdef __cplusplus
}
#endif

#endif /* HW_SIM_H */
