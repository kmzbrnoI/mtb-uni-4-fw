#ifndef _IO_H_
#define _IO_H_

/* Raw input-output operation of MTB-UNI v4 module.
 * This unit does not perform any debouncing nor any complicated IO stuff.
 * It just abstracts IO with nice & fast functions.
 */

#include <stdbool.h>
#include <stdint.h>
#include <avr/io.h>

// Defines below are informative only, they are basically never used.
// If you change IO pins mapping, look into ‹io.c› and change approprite
// functions.

#define NO_INPUTS 16
#define INPUT_0 PINF7
#define INPUT_1 PINF6
#define INPUT_2 PINF5
#define INPUT_3 PINF4
#define INPUT_4 PINF3
#define INPUT_5 PINF2
#define INPUT_6 PINF1
#define INPUT_7 PINF0
#define INPUT_8 PINE3
#define INPUT_9 PINE4
#define INPUT_10 PINE5
#define INPUT_11 PINE6
#define INPUT_12 PINE7
#define INPUT_13 PINB0
#define INPUT_14 PINB4
#define INPUT_15 PINB5

#define NO_OUTPUTS 16
#define OUTPUT_0 PIND7
#define OUTPUT_1 PIND6
#define OUTPUT_2 PIND5
#define OUTPUT_3 PIND4
#define OUTPUT_4 PIND3
#define OUTPUT_5 PIND2
#define OUTPUT_6 PIND1
#define OUTPUT_7 PIND0
#define OUTPUT_8 PINC7
#define OUTPUT_9 PINC6
#define OUTPUT_10 PINC5
#define OUTPUT_11 PINC4
#define OUTPUT_12 PINC3
#define OUTPUT_13 PINC2
#define OUTPUT_14 PINC1
#define OUTPUT_15 PINC0

#define NO_INPUTS_ADDR 8
#define INPUT_ADDR_0 PINA0
#define INPUT_ADDR_1 PINA1
#define INPUT_ADDR_2 PINA2
#define INPUT_ADDR_3 PINA3
#define INPUT_ADDR_4 PINA4
#define INPUT_ADDR_5 PINA5
#define INPUT_ADDR_6 PINA6
#define INPUT_ADDR_7 PINA7

#define PIN_LED_RED PB7
#define PIN_LED_GREEN PB6
#define PIN_LED_BLUE PG3

#define PIN_UART_RX PE0
#define PIN_UART_TX PE1
#define PIN_UART_DIR PE2

// Test/debug pins
#define PIN_TPMISO PB3
#define PIN_TPMOSI PB2

#define PIN_BUTTON PING4

void io_init(void);

bool io_get_input_raw(uint8_t inum);
uint16_t io_get_inputs_raw();

static inline uint8_t io_get_addr_raw(void) {
	return ~PINA;
}

void io_set_output_raw(uint8_t onum, bool state);
void io_set_outputs_raw(uint16_t state);
void io_set_outputs_raw_mask(uint16_t state, uint16_t mask);
uint16_t io_get_outputs_raw(void);

bool io_get_output_raw(uint8_t onum);

static inline void io_led_red_on(void) { PORTB |= (1 << PIN_LED_RED); }
static inline void io_led_red_off(void) { PORTB &= ~(1 << PIN_LED_RED); }
static inline void io_led_green_on(void) { PORTB |= (1 << PIN_LED_GREEN); }
static inline void io_led_green_off(void) { PORTB &= ~(1 << PIN_LED_GREEN); }
static inline void io_led_blue_on(void) { PORTG |= (1 << PIN_LED_BLUE); }
static inline void io_led_blue_off(void) { PORTG &= ~(1 << PIN_LED_BLUE); }

static inline void io_led_red(bool state) {
	if (state)
		io_led_red_on();
	else
		io_led_red_off();
}

static inline void io_led_green(bool state) {
	if (state)
		io_led_green_on();
	else
		io_led_green_off();
}

static inline void io_led_blue(bool state) {
	if (state)
		io_led_blue_on();
	else
		io_led_blue_off();
}

static inline bool io_led_red_state(void) { return (PORTB >> PIN_LED_RED) & 0x1; }
static inline bool io_led_green_state(void) { return (PORTB >> PIN_LED_GREEN) & 0x1; }
static inline bool io_led_blue_state(void) { return (PORTG >> PIN_LED_BLUE) & 0x1; }

static inline void io_led_red_toggle(void) { io_led_red(!io_led_red_state()); }
static inline void io_led_green_toggle(void) { io_led_green(!io_led_green_state()); }
static inline void io_led_blue_toggle(void) { io_led_blue(!io_led_blue_state()); }

static inline bool io_button(void) { return (PING >> PIN_BUTTON) & 0x1; }

static inline void uart_out(void) { PORTE |= (1 << PIN_UART_DIR); }
static inline void uart_in(void) { PORTE &= ~(1 << PIN_UART_DIR); }

static inline void io_tpmiso(bool state) {
	if (state)
		PORTB |= (1 << PIN_TPMISO);
	else
		PORTB &= ~(1 << PIN_TPMISO);
}

static inline void io_tpmiso_toggle(void) {
	if (PORTB & (1 << PIN_TPMISO))
		PORTB &= ~(1 << PIN_TPMISO);
	else
		PORTB |= (1 << PIN_TPMISO);
}

static inline void io_tpmosi(bool state) {
	if (state)
		PORTB |= (1 << PIN_TPMOSI);
	else
		PORTB &= ~(1 << PIN_TPMOSI);
}

static inline void io_tpmosi_toggle(void) {
	if (PORTB & (1 << PIN_TPMOSI))
		PORTB &= ~(1 << PIN_TPMOSI);
	else
		PORTB |= (1 << PIN_TPMOSI);
}

#endif
