#ifndef _SCOM_H_
#define _SCOM_H_

// S-COM output generator

#include <stdbool.h>
#include "io.h"

void scom_init();
void scom_update();

void scom_reset();
void scom_output(uint8_t output, int8_t code);
void scom_disable_output(uint8_t output);
inline bool scom_is_output(uint8_t output);

#endif
