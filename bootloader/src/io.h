#ifndef _IO_H_
#define _IO_H_

/* Raw input-output operation of bootloader of MTB-UNI v4 module.
 */

#include <stdbool.h>
#include <stdint.h>
#include <avr/io.h>

#define PIN_LED_RED PB7
#define PIN_LED_GREEN PB6
#define PIN_LED_BLUE PG3

#define PIN_UART_RX PE0
#define PIN_UART_TX PE1
#define PIN_UART_DIR PE2

#define PIN_BUTTON PING4

static inline void io_init(void) {
	DDRD = 0; // disable outputs 7-0
	DDRC = 0; // disable outputs 15-8
	DDRB = (1 << PIN_LED_RED) | (1 << PIN_LED_GREEN);
	DDRG = (1 << PIN_LED_BLUE);
	PORTG = (1 << PIN_BUTTON); // button pull-up
}

static inline uint8_t io_get_addr_raw(void) {
	return ~PINA;
}

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

#endif
