#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/boot.h>
#include "diag.h"

///////////////////////////////////////////////////////////////////////////////
// Global variables

mcucsr_t mcucsr;
error_flags_t error_flags = {0};
mtbbus_warn_flags_t mtbbus_warn_flags = {0};
mtbbus_warn_flags_t mtbbus_warn_flags_old = {0};
volatile uint16_t vcc_voltage = 0;
volatile uint16_t init_vcc = 0xFFFF;
volatile uint32_t uptime_seconds = 0;

#define DIAG_STEP_VCC_PREPARE 0
#define DIAG_STEP_VCC_READY 1
#define DIAG_STEP_VCC_MEASURE 2
#define DIAG_STEP_OVER 5

volatile uint8_t diag_step = DIAG_STEP_OVER-1;

///////////////////////////////////////////////////////////////////////////////
// Function prototypes

static inline void vcc_prepare_measure(void);
static inline void adc_start(void);

///////////////////////////////////////////////////////////////////////////////

void diag_init(void) {
	ADCSRA = (1 << ADIE) | (1 << ADEN); // enable ADC interrupt, enable ADC
	ADCSRA |= 0x5; // prescaler 32×
}

void diag_update(void) {
	// called each 100 ms
	switch (diag_step) {
	case DIAG_STEP_VCC_PREPARE:
		vcc_prepare_measure();
		break;
	case DIAG_STEP_VCC_READY:
		adc_start();
		break;
	}

	{
		static uint8_t uptime_counter = 0;
		uptime_counter++;
		if (uptime_counter >= 10) {
			uptime_seconds++;
			uptime_counter = 0;
		}
	}

	diag_step++;
	if (diag_step >= DIAG_STEP_OVER)
		diag_step = 0;
}

void vcc_prepare_measure(void) {
	ADMUX = (1 << REFS0); // AVCC with external capacitor at AREF pin
	ADMUX |= 0x1E; // measure internal 1V22 band-gap reference
}

void adc_start(void) {
	ADCSRA |= (1 << ADSC); // start conversion
}

ISR(ADC_vect) {
	uint16_t value = ADCL;
	value |= (ADCH << 8);

	switch (diag_step) {
	case DIAG_STEP_VCC_MEASURE:
		vcc_voltage = value;

		if (init_vcc == 0xFFFF)
			init_vcc = vcc_voltage;

		uint16_t diff = vcc_voltage > init_vcc ? vcc_voltage-init_vcc : init_vcc-vcc_voltage;
		if (diff > VCC_MAX_DIFF)
			mtbbus_warn_flags.bits.vcc_oscilating = true;
		break;
	}
}
