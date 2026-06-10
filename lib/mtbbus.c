#include <stddef.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <string.h>

#include "mtbbus.h"
#include "io.h"
#include "crc16modbus.h"

volatile uint8_t mtbbus_output_buf[MTBBUS_OUTPUT_BUF_MAX_SIZE];
volatile uint8_t mtbbus_output_buf_size = 0;
volatile uint8_t mtbbus_next_byte_to_send = 0;
volatile bool sending = false;

volatile uint8_t mtbbus_input_buf[MTBBUS_INPUT_BUF_MAX_SIZE];
volatile uint8_t mtbbus_input_buf_size = 0;
volatile bool receiving = false;
volatile uint16_t received_crc = 0;
volatile uint8_t received_addr;
volatile bool received = false;
volatile bool sent = false;

volatile uint8_t mtbbus_addr;
volatile uint8_t mtbbus_speed;
void (*mtbbus_on_receive)(bool broadcast, uint8_t command_code, uint8_t *data, uint8_t data_len) = NULL;
void (*mtbbus_on_sent)() = NULL;

#ifdef SUP_MTBBUS_DIAG
volatile MtbBusDiag mtbbus_diag;
#endif

///////////////////////////////////////////////////////////////////////////////

static void _send_next_byte();
static inline void _mtbbus_send_buf();
static inline void _mtbbus_received_ninth(uint8_t data);
static inline void _mtbbus_received_non_ninth(uint8_t data);

static inline bool _t0_elapsed();
static inline void _t0_start();

///////////////////////////////////////////////////////////////////////////////
// Init

void mtbbus_init(uint8_t addr, uint8_t speed) {
	mtbbus_addr = addr;

#ifdef SUP_MTBBUS_DIAG
	memset((void*)&mtbbus_diag, 0, sizeof(mtbbus_diag));
#endif

	// Setup timer 0 @ 150 us (6666 Hz - MTBbus answer timeout)
	// This timer is used to check that a response is sent withing 150 us after
	// receiving the request. All requests to send messages after 150 us are discarded.
	// See mtbbus protocol specification.
	// No interrupt vector used - just start the timer & check for overflow
	TCCR0 = (1 << CS01) | (1 << CS00); // prescaler 32
	OCR0 = 69;

	DDRE |= 0x04; // UART direction
	uart_in();

	mtbbus_set_speed(speed);

	UCSR0C = _BV(UCSZ01) | _BV(UCSZ00); // 9-bit data
	UCSR0A = _BV(MPCM0); // Mutli-processor mode, receive onyl if 9. bit = 1
	UCSR0B = _BV(RXCIE0) | _BV(TXCIE0) | _BV(UCSZ02) | _BV(RXEN0) | _BV(TXEN0);  // RX, TX enable; RX, TX interrupt enable
}

bool _t0_elapsed() {
	return (TIFR & (1 << OCF0));
}

void _t0_start() {
	TCNT0 = 0;
	TIFR = (1 << OCF0);
}

void mtbbus_set_speed(uint8_t speed) {
	mtbbus_speed = speed;
	UBRR0H = 0;

	switch (speed) {
	case MTBBUS_SPEED_230400:
		UBRR0L = 3;
		break;
	case MTBBUS_SPEED_115200:
		UBRR0L = 7;
		break;
	case MTBBUS_SPEED_57600:
		UBRR0L = 15;
		break;
	case MTBBUS_SPEED_38400:
	default:
		UBRR0L = 23;
		break;
	}

	UCSR0A &= ~_BV(U2X0);
}

void mtbbus_update() {
	if (received) {
		if (mtbbus_on_receive != NULL)
			mtbbus_on_receive(received_addr == 0, mtbbus_input_buf[1],
			                  (uint8_t*)mtbbus_input_buf+2, mtbbus_input_buf_size-3);
		received = false; // must be after parsing (prevents changes in mtbbus_input_buf, mtbbus_input_buf_size)
	}

	if (sent) {
		sent = false;
		if (mtbbus_on_sent != NULL) {
			void (*tmp)() = mtbbus_on_sent;
			mtbbus_on_sent = NULL;
			tmp();
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
// Sending

int mtbbus_send(uint8_t *data, uint8_t size) {
	if (!mtbbus_can_fill_output_buf()) {
#ifdef SUP_MTBBUS_DIAG
		mtbbus_diag.unsent++;
#endif
		return 1;
	}
	if (size > MTBBUS_OUTPUT_BUF_MAX_SIZE_USER) {
#ifdef SUP_MTBBUS_DIAG
		mtbbus_diag.unsent++;
#endif
		return 2;
	}

	for (uint8_t i = 0; i < size; i++)
		mtbbus_output_buf[i] = data[i];
	mtbbus_output_buf_size = size;

	mtbbus_send_buf();
	return 0;
}

int mtbbus_send_buf() {
	if (sending) {
#ifdef SUP_MTBBUS_DIAG
		mtbbus_diag.unsent++;
#endif
		return 1;
	}
	sent = false;

	size_t i = mtbbus_output_buf_size;
	uint16_t crc = crc16modbus_bytes(0, (uint8_t*)mtbbus_output_buf, mtbbus_output_buf_size);
	mtbbus_output_buf_size += 2;
	mtbbus_output_buf[i] = crc & 0xFF;
	mtbbus_output_buf[i+1] = (crc >> 8) & 0xFF;
	_mtbbus_send_buf();
	return 0;
}

int mtbbus_send_buf_autolen() {
	if (sending) {
#ifdef SUP_MTBBUS_DIAG
		mtbbus_diag.unsent++;
#endif
		return 1;
	}
	if (mtbbus_output_buf[0] > MTBBUS_OUTPUT_BUF_MAX_SIZE_USER) {
#ifdef SUP_MTBBUS_DIAG
		mtbbus_diag.unsent++;
#endif
		return 2;
	}
	if (_t0_elapsed()) {
#ifdef SUP_MTBBUS_DIAG
		mtbbus_diag.unsent++;
#endif
		return 3;
	}
	mtbbus_output_buf_size = mtbbus_output_buf[0]+1;
	return mtbbus_send_buf();
}

static inline void _mtbbus_send_buf() {
	sending = true;
	mtbbus_next_byte_to_send = 0;
	uart_out();

	while (!(UCSR0A & _BV(UDRE0)));
	_send_next_byte();

#ifdef SUP_MTBBUS_DIAG
	mtbbus_diag.sent++;
#endif
}

static void _send_next_byte() {
	loop_until_bit_is_set(UCSR0A, UDRE0); // wait for empty transmit buffer
	UCSR0B &= ~_BV(TXB80); // 9 bit = 0
	UDR0 = mtbbus_output_buf[mtbbus_next_byte_to_send];
	mtbbus_next_byte_to_send++;
}

ISR(USART0_TX_vect) {
	if (mtbbus_next_byte_to_send < mtbbus_output_buf_size) {
		_send_next_byte();
	} else {
		uart_in();
		sending = false;
		sent = true;
	}
}

bool mtbbus_can_fill_output_buf() {
	return !sending;
}

///////////////////////////////////////////////////////////////////////////////
// Receiving

ISR(USART0_RX_vect) {
	uint8_t status = UCSR0A;
	bool ninth = (UCSR0B >> 1) & 0x01;
	uint8_t data = UDR0;

	if (status & ((1<<FE0)|(1<<DOR0)|(1<<UPE0)))
		return; // return on error

	if (ninth)
		_mtbbus_received_ninth(data);
	else
		_mtbbus_received_non_ninth(data);
}

static inline void _mtbbus_received_ninth(uint8_t data) {
	if (received) // received data pending
		return;

	received_addr = data;

	if ((received_addr == mtbbus_addr) || (received_addr == 0)) {
		UCSR0A &= ~_BV(MPCM0); // Receive rest of message
		receiving = true;
		mtbbus_input_buf_size = 0;
		received_crc = crc16modbus_byte(0, received_addr);
	} else {
		receiving = false;
	}
}

static inline void _mtbbus_received_non_ninth(uint8_t data) {
	if (!receiving)
		return;

	if (mtbbus_input_buf_size < MTBBUS_INPUT_BUF_MAX_SIZE) {
		if ((mtbbus_input_buf_size == 0) || (mtbbus_input_buf_size <= mtbbus_input_buf[0]))
			received_crc = crc16modbus_byte(received_crc, data);
		mtbbus_input_buf[mtbbus_input_buf_size] = data;
		mtbbus_input_buf_size++;
	}

	if (mtbbus_input_buf_size >= mtbbus_input_buf[0]+3) {
		// whole message received
		uint16_t msg_crc = (mtbbus_input_buf[mtbbus_input_buf_size-1] << 8) | (mtbbus_input_buf[mtbbus_input_buf_size-2]);
		if (received_crc == msg_crc) {
			received = true;
			_t0_start();
#ifdef SUP_MTBBUS_DIAG
			mtbbus_diag.received++;
		} else {
			mtbbus_diag.bad_crc++;
#endif
		}

		receiving = false;
		received_crc = 0;
		UCSR0A |= _BV(MPCM0); // Receive only if 9. bit = 1
	}
}
