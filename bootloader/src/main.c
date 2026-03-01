#include <stdbool.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/eeprom.h>
#include <avr/boot.h>
#include <avr/pgmspace.h>
#include <util/atomic.h>

#include "io.h"
#include "../lib/crc16modbus.h"
#include "../lib/mtbbus.h"

/* All boot_* functions executing SPM instruction need to be called in
 * ATOMIC_BLOCK - interrupts need to be disabled during execution of the function,
 * because the following needs to be met [ATmega128A datasheet]:
 * "To execute page erase, ... write “X0000011” to SPMCSR and execute SPM
 * **within four clock cycles** after writing SPMCSR."
 * This is a real issue in MTB-UNI-2-AVR with baudrate 230 400 bd/s - random
 * words in flash were not programmed after an attempt to reprogram the flash.
 */

///////////////////////////////////////////////////////////////////////////////
// Function prototypes

int main();
static void check_and_boot(void);
static inline void main_program(void);
bool fwcrc_ok(void);
static inline void _mtbbus_init(void);
void mtbbus_received(bool broadcast, uint8_t command_code, uint8_t *data, uint8_t data_len);
static void mtbbus_send_ack(void);
static void mtbbus_send_error(uint8_t code);


///////////////////////////////////////////////////////////////////////////////
// Defines & global variables

#define EEPROM_ADDR_MTBBUS_SPEED           ((uint8_t*)0x01)
#define EEPROM_ADDR_BOOT                   ((uint8_t*)0x03)
#define EEPROM_ADDR_BOOTLOADER_VER_MAJOR   ((uint8_t*)0x08)
#define EEPROM_ADDR_BOOTLOADER_VER_MINOR   ((uint8_t*)0x09)

#define CONFIG_BOOT_NORMAL 0x00
#define CONFIG_BOOT_FWUPGD 0x01

#define CONFIG_MODULE_TYPE 0x15
#define CONFIG_FW_MAJOR 1
#define CONFIG_FW_MINOR 4
#define CONFIG_PROTO_MAJOR 4
#define CONFIG_PROTO_MINOR 0

__attribute__((used, section(".fwattr"))) struct {
	uint8_t no_pages;
	uint16_t crc;
} fwattr = {0xFF, 0xFFFF};

typedef union {
	struct {
		bool addr_zero : 1;
		bool crc: 1;
	} bits;
	uint8_t all;
} error_flags_t;

volatile error_flags_t error_flags = {0};

volatile uint8_t page = 0xFF;
volatile uint8_t subpage = 0xFF;
volatile bool page_erase = false;
volatile bool page_write = false;

///////////////////////////////////////////////////////////////////////////////

int main() {
	cli();
	MCUCR = (1 << IVCE);
	MCUCR = (1 << IVSEL); // move interrupts to bootloader
	wdt_disable();
	io_init();

	io_led_red_on();
	io_led_green_off();
	io_led_blue_off();

	// Setup timer 3 @ 2.5 Hz (period 400 ms)
	TCCR3B = (1 << WGM12) | (1 << CS12); // CTC mode, 256× prescaler
	ETIMSK = (1 << OCIE3A); // enable compare match interrupt
	OCR3A = 23020;

	uint8_t boot = eeprom_read_byte(EEPROM_ADDR_BOOT);
	if (boot != CONFIG_BOOT_NORMAL)
		eeprom_write_byte(EEPROM_ADDR_BOOT, CONFIG_BOOT_NORMAL);

	eeprom_update_byte(EEPROM_ADDR_BOOTLOADER_VER_MAJOR, CONFIG_FW_MAJOR);
	eeprom_update_byte(EEPROM_ADDR_BOOTLOADER_VER_MINOR, CONFIG_FW_MINOR);

	if ((boot != CONFIG_BOOT_FWUPGD) && (io_button()))
		check_and_boot();

	// Not booting → start MTBbus
	_mtbbus_init();
	sei();

	while (true) {
		mtbbus_update();

		if (page_erase) {
			while (boot_spm_busy())
				mtbbus_update();
			ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
				boot_page_erase(SPM_PAGESIZE*page);
			}
			while (boot_spm_busy())
				mtbbus_update();
			page_erase = false;
		}

		if (page_write) {
			while (boot_spm_busy())
				mtbbus_update();
			ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
				boot_page_write(SPM_PAGESIZE*page);
			}
			while (boot_spm_busy())
				mtbbus_update();
			page_write = false;
			page = 0xFF; // reset so next write will erase flash first
		}
	}

	return 0;
}

static inline void _mtbbus_init(void) {
	uint8_t mtbbus_speed = eeprom_read_byte(EEPROM_ADDR_MTBBUS_SPEED);
	if (mtbbus_speed > MTBBUS_SPEED_MAX)
		mtbbus_speed = MTBBUS_SPEED_38400;

	uint8_t _mtbbus_addr = io_get_addr_raw();
	error_flags.bits.addr_zero = (_mtbbus_addr == 0);

	mtbbus_init(_mtbbus_addr, mtbbus_speed);
	mtbbus_on_receive = mtbbus_received;
}

void check_and_boot(void) {
	if (fwcrc_ok())
		main_program();

	error_flags.bits.crc = true;
}

static inline void main_program(void) {
	cli();
	TCCR3B = 0;
	ETIMSK = 0;
	MCUCR = (1 << IVCE);
	MCUCR = 0; // move interrupts back to normal program
	__asm__ volatile ("ijmp" ::"z" (0));
}

///////////////////////////////////////////////////////////////////////////////

bool fwcrc_ok(void) {
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		boot_rww_enable_safe();
	}
	uint8_t no_pages = pgm_read_byte_far((uint_farptr_t)&fwattr.no_pages);
	uint16_t crc_read = pgm_read_word_far((uint_farptr_t)&fwattr.crc);

	if ((no_pages == 0xFF) || (no_pages == 0))
		return false;

	uint16_t crc = 0;
	for (size_t i = 0; i < no_pages; i++)
		for (size_t j = 0; j < SPM_PAGESIZE; j++)
			crc = crc16modbus_byte(crc, pgm_read_byte_far((SPM_PAGESIZE*i) + j));

	return crc_read == crc;
}

///////////////////////////////////////////////////////////////////////////////

void mtbbus_received(bool broadcast, uint8_t command_code, uint8_t *data, uint8_t data_len) {
	_delay_us(2);

	if ((command_code == MTBBUS_CMD_MOSI_MODULE_INQUIRY) && (!broadcast)) {
		mtbbus_send_ack();

	} else if ((command_code == MTBBUS_CMD_MOSI_INFO_REQ) && (!broadcast)) {
		mtbbus_output_buf[0] = 7;
		mtbbus_output_buf[1] = MTBBUS_CMD_MISO_MODULE_INFO;
		mtbbus_output_buf[2] = CONFIG_MODULE_TYPE;
		mtbbus_output_buf[3] = (error_flags.bits.crc) ? 2 : 1;
		mtbbus_output_buf[4] = CONFIG_FW_MAJOR;
		mtbbus_output_buf[5] = CONFIG_FW_MINOR;
		mtbbus_output_buf[6] = CONFIG_PROTO_MAJOR;
		mtbbus_output_buf[7] = CONFIG_PROTO_MINOR;
		mtbbus_send_buf_autolen();

	} else if ((command_code == MTBBUS_CMD_MOSI_CHANGE_SPEED) && (data_len >= 1)) {
		mtbbus_set_speed(data[0]);
		if (!broadcast)
			mtbbus_send_ack();
		eeprom_update_byte(EEPROM_ADDR_MTBBUS_SPEED, data[0]);

	} else if ((command_code == MTBBUS_CMD_MOSI_WRITE_FLASH) && (data_len >= 66) && (!broadcast)) {
		uint8_t _page = data[0];
		uint8_t _subpage = data[1];
		if ((_subpage % 64 != 0) || (_page >= 240)) {
			mtbbus_send_error(MTBBUS_ERROR_BAD_ADDRESS);
			return;
		}
		if (page_erase || page_write) {
			mtbbus_send_error(MTBBUS_ERROR_BUSY);
			return;
		}

		mtbbus_send_ack();

		if (_page != page) {
			// new page → erase
			page = _page;
			page_erase = true;
		}
		subpage = _subpage;

		ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
			for (size_t i = 0; i < 64; i += 2) {
				uint16_t word = data[i+2] | (data[i+3] << 8);
				boot_page_fill(SPM_PAGESIZE*page + _subpage + i, word);
			}
		}

		if (_subpage == SPM_PAGESIZE-64) {
			// last subpage → write whole page
			page_write = true;
		}

	} else if ((command_code == MTBBUS_CMD_MOSI_WRITE_FLASH_STATUS_REQ) && (!broadcast)) {
		mtbbus_output_buf[0] = 4;
		mtbbus_output_buf[1] = MTBBUS_CMD_MISO_WRITE_FLASH_STATUS;
		mtbbus_output_buf[2] = (page_erase || page_write);
		mtbbus_output_buf[3] = page;
		mtbbus_output_buf[4] = subpage;
		mtbbus_send_buf_autolen();

	} else if ((command_code == MTBBUS_CMD_MOSI_REBOOT) && (!broadcast)) {
		mtbbus_on_sent = &check_and_boot;
		mtbbus_send_ack();

	} else if ((command_code == MTBBUS_CMD_MOSI_FWUPGD_REQUEST) && (data_len >= 1) && (!broadcast)) {
		mtbbus_send_ack();

	} else {
		if (!broadcast)
			mtbbus_send_error(MTBBUS_ERROR_UNKNOWN_COMMAND);
	}
}

void mtbbus_send_ack(void) {
	mtbbus_output_buf[0] = 1;
	mtbbus_output_buf[1] = MTBBUS_CMD_MISO_ACK;
	mtbbus_send_buf_autolen();
}

void mtbbus_send_error(uint8_t code) {
	mtbbus_output_buf[0] = 2;
	mtbbus_output_buf[1] = MTBBUS_CMD_MISO_ERROR;
	mtbbus_output_buf[2] = code;
	mtbbus_send_buf_autolen();
}

///////////////////////////////////////////////////////////////////////////////

ISR(TIMER3_COMPA_vect) {
	io_led_red_toggle();
	io_led_green_toggle();
}

///////////////////////////////////////////////////////////////////////////////
