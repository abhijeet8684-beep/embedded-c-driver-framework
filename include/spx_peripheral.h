#ifndef SPX_PERIPHERAL_H
#define SPX_PERIPHERAL_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * SPX-100 Serial Transmit Peripheral Register Map & Definitions
 * ============================================================================ */

/* Register Base Offsets */
#define SPX_CTRL_OFFSET       0x00u  /* Control Register (R/W) */
#define SPX_STATUS_OFFSET     0x04u  /* Status Register (R/W1C) */
#define SPX_TXDATA_OFFSET     0x08u  /* Transmit Data FIFO Register (W) */

/* CTRL Register Bit Positions & Field Widths */
#define SPX_CTRL_EN_BIT       0u     /* Peripheral Enable (1 = Enabled) */
#define SPX_CTRL_START_BIT    1u     /* Trigger Transmit (Self-clearing in HW) */
#define SPX_CTRL_CLKDIV_BIT   2u     /* Clock Divider start bit */
#define SPX_CTRL_CLKDIV_WIDTH 3u     /* Clock Divider field width (3-bit, 0-7) */
#define SPX_CTRL_IE_BIT       5u     /* Interrupt Enable */

/* STATUS Register Bit Positions */
#define SPX_STATUS_BUSY_BIT   0u     /* 1 when transmission is active (Read-only) */
#define SPX_STATUS_DONE_BIT   1u     /* 1 when transmission finishes (Write-1-to-clear) */
#define SPX_STATUS_ERR_BIT    2u     /* 1 on transmit underflow/error (Write-1-to-clear) */

/* Memory-Mapped I/O Register Accessors */
uint32_t reg_read(uint32_t offset);
void     reg_write(uint32_t offset, uint32_t value);

/* SPX-100 Peripheral Driver API */
void spx_init(uint8_t clkdiv);
int  spx_send_byte(uint8_t data);
bool spx_is_busy(void);
bool spx_is_enabled(void);

#endif /* SPX_PERIPHERAL_H */
