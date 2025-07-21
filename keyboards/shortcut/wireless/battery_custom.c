#include <stdbool.h>
#include <stdint.h>
#include "module.h"

#ifdef BATTERY_DRIVER
void battery_driver_init(void) {
}

uint8_t battery_driver_sample_percent(void) {
    return *md_getp_bat();
}
#endif
