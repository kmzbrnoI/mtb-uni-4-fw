/* Main source file of MTB-UNI v4 CPU ATmega128.
 */

#include <stdbool.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/wdt.h>

#include "io.h"
#include "scom.h"
#include "outputs.h"
#include "config.h"
#include "inputs.h"
#include "../lib/mtbbus.h"
#include "../lib/crc16modbus.h"

///////////////////////////////////////////////////////////////////////////////

int main();
static inline void init();
void mtbbus_received(bool broadcast, uint8_t command_code, uint8_t *data, uint8_t data_len);
void mtbbus_send_ack();
void mtbbus_send_inputs(uint8_t message_code);
static inline void leds_update();

///////////////////////////////////////////////////////////////////////////////

#define LED_GR_ON 5
#define LED_GR_OFF 2
volatile uint8_t led_gr_counter = 0;

#define LED_RED_OK_ON 40
#define LED_RED_OK_OFF 20
#define LED_RED_ERR_ON 100
#define LED_RED_ERR_OFF 50
volatile uint8_t led_red_counter = 0;

typedef union {
	struct {
		bool addr_zero : 1;
	} bits;
	uint8_t all;
} error_flags_t;

error_flags_t error_flags = {0};

void led_red_ok();

///////////////////////////////////////////////////////////////////////////////

int main() {
	init();

	while (true) {
		if (config_write) {
			config_save();
			config_write = false;
		}

		_delay_ms(10);
		// wdt_reset();
	}
}

static inline void init() {
	// WDTCR |= 1 << WDE;  // watchdog enable
	// WDTCR |= WDP2; // ~250 ms timeout

	io_init();
	io_led_red_on();
	io_led_green_on();
	io_led_blue_on();
	scom_init();

	// Setup timer 1 @ 10 kHz (period 100 us)
	TCCR1B = (1 << WGM12) | (1 << CS10); // CTC mode, no prescaler
	TIMSK |= (1 << OCIE1A); // enable compare match interrupt
	OCR1A = 1473;

	// Setup timer 3 @ 100 Hz (period 10 ms)
	TCCR3B = (1 << WGM12) | (1 << CS11) | (1 << CS10); // CTC mode, 64× prescaler
	ETIMSK |= (1 << OCIE3A); // enable compare match interrupt
	OCR3A = 2302;

	config_load();
	outputs_set_full(config_safe_state);

	uint8_t _mtbbus_addr = io_get_addr_raw();
	error_flags.bits.addr_zero = (_mtbbus_addr == 0);
	mtbbus_init(_mtbbus_addr, config_mtbbus_speed);
	mtbbus_on_receive = mtbbus_received;

	_delay_ms(50);
	sei(); // enable interrupts globally
	io_led_red_off();
	io_led_green_off();
	io_led_blue_off();
}

ISR(TIMER1_COMPA_vect) {
	// Timer 1 @ 10 kHz (period 100 us)
	inputs_debounce_update();
}

ISR(TIMER3_COMPA_vect) {
	// Timer 3 @ 100 Hz (period 10 ms)
	scom_update();
	outputs_update();
	inputs_fall_update();
	leds_update();
}

///////////////////////////////////////////////////////////////////////////////

static inline void leds_update() {
	if (led_gr_counter > 0) {
		led_gr_counter--;
		if (led_gr_counter == LED_GR_OFF)
			io_led_green_off();
	}

	if (led_red_counter > 0) {
		led_red_counter--;
		if (((error_flags.all == 0) && (led_red_counter == LED_RED_OK_OFF)) ||
			((error_flags.all != 0) && (led_red_counter == LED_RED_ERR_OFF)))
			io_led_red_off();
	}
	if ((error_flags.all != 0) && (led_red_counter == 0)) {
		led_red_counter = LED_RED_ERR_ON;
		io_led_red_on();
	}
}

void led_red_ok() {
	if (led_red_counter == 0) {
		led_red_counter = LED_RED_OK_ON;
		io_led_red_on();
	}
}

///////////////////////////////////////////////////////////////////////////////

void btn_on_pressed() {
	uint8_t _mtbbus_addr = io_get_addr_raw();
	error_flags.bits.addr_zero = (_mtbbus_addr == 0);
	mtbbus_addr = _mtbbus_addr;
	if (mtbbus_addr != 0)
		led_red_ok();
}

void btn_on_depressed() {}

///////////////////////////////////////////////////////////////////////////////

void mtbbus_received(bool broadcast, uint8_t command_code, uint8_t *data, uint8_t data_len) {
	if (led_gr_counter == 0) {
		io_led_green_on();
		led_gr_counter = LED_GR_ON;
	}

	if ((command_code == MTBBUS_CMD_MOSI_MODULE_INQUIRY) && (data_len >= 1)) {
		static bool last_input_changed = false;
		bool last_ok = data[0] & 0x01;
		if ((inputs_logic_state != inputs_old) || (last_input_changed && !last_ok)) {
			// Send inputs changed
			last_input_changed = true;
			mtbbus_send_inputs(MTBBUS_CMD_MISO_INPUT_CHANGED);
			inputs_old = inputs_logic_state;
		} else {
			last_input_changed = false;
			mtbbus_send_ack();
		}
	}
}

void mtbbus_send_ack() {
	if (!mtbbus_can_fill_output_buf())
		return;
	mtbbus_output_buf[0] = 1;
	mtbbus_output_buf[1] = MTBBUS_CMD_MISO_ACK;
	mtbbus_send_buf_autolen();
}

void mtbbus_send_inputs(uint8_t message_code) {
	if (!mtbbus_can_fill_output_buf())
		return;
	mtbbus_output_buf[0] = 3;
	mtbbus_output_buf[1] = message_code;
	mtbbus_output_buf[2] = (inputs_logic_state >> 8) & 0xFF;
	mtbbus_output_buf[3] = inputs_logic_state & 0xFF;
	mtbbus_send_buf_autolen();
}

///////////////////////////////////////////////////////////////////////////////
