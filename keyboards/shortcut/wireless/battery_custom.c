#include <stdbool.h>
#include <stdint.h>
#include "module.h"

void battery_driver_init(void) {
}

uint8_t battery_driver_sample_percent(void) {
    return *md_getp_bat();
}
