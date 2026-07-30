#ifndef CARDPUTER_EXTREME_BOARD_INTERNAL_H
#define CARDPUTER_EXTREME_BOARD_INTERNAL_H

#include <driver/i2c_master.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Target-only accessor for peripherals which share the Cardputer ADV system
 * I2C bus.  The board owns the bus lifetime; clients may add devices but must
 * not delete the bus.
 */
i2c_master_bus_handle_t CardputerExtreme_I2cBus(void);

#ifdef __cplusplus
}
#endif

#endif
