#ifndef _CONFIG_H_
#define _CONFIG_H_

/* General configuration of MTB-UNI module
 */

#include <stdint.h>
#include <stdbool.h>
#include "io.h"

extern uint8_t config_safe_state[NO_OUTPUTS];
extern uint8_t config_inputs_delay[NO_INPUTS/2];
extern bool config_write;
extern uint8_t config_mtbbus_speed;

// Warning: these functions take long time to execute
void config_load(void);
bool config_save(void);

void config_boot_fwupgd(void);
void config_boot_normal(void);

void config_int_wdrf(bool value);
bool config_is_int_wdrf(void);

uint16_t config_bootloader_version(void);

uint8_t input_delay(uint8_t input);

#define CONFIG_MODULE_TYPE 0x16
#define CONFIG_FW_MAJOR 1
#define CONFIG_FW_MINOR 6
#define CONFIG_PROTO_MAJOR 4
#define CONFIG_PROTO_MINOR 1

#define CONFIG_BOOT_FWUPGD 0x01
#define CONFIG_BOOT_NORMAL 0x00

#endif
