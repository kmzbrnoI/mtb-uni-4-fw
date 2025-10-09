#include <stdbool.h>
#include <string.h>
#include "outputs.h"
#include "io.h"
#include "scom.h"

const uint8_t _flicker_periods[] = { // in times of calls to outputs_update (10 ms)
	10, // invalid
	50, // 1 Hz
	25, // 2 Hz
	16, // ~ 3 Hz
	12, // ~ 4 Hz
	10, // 5 Hz
	5,  // 10 Hz
	100, // 33 tick / min
	50, // 66 tick / min
};

// Each output has it's flickering counter to avoid neccessity for single
// lowest-common-multiplier counter (LCM is too big)
uint8_t _flicker_counters[NO_OUTPUTS] = {0, };
bool _flicker_enabled[NO_OUTPUTS] = {false, };

// State according to ‹protocol›
// ‹https://github.com/kmzbrnoI/mtbbus-protocol/blob/master/modules/uni.md›
uint8_t _outputs_state[NO_OUTPUTS] = {0, };

void outputs_apply_state(void) {
	uint16_t plain_mask = 0;
	uint16_t plain_state = 0;

	for (int i = NO_OUTPUTS-1; i >= 0; i--) {
		plain_mask <<= 1;
		plain_state <<= 1;

		_flicker_enabled[i] = ((_outputs_state[i] & 0xC0) == 0x40);
		if (!_flicker_enabled[i])
			_flicker_counters[i] = 0;

		if ((_outputs_state[i] & 0x80) == 0)
			scom_disable_output(i);

		if (_outputs_state[i] & 0x80) { // S-COM
			scom_output(i, _outputs_state[i] & 0x7F);
		} else if ((_outputs_state[i] & 0x40) == 0) { // plain output
			plain_mask |= 1;
			if (_outputs_state[i] & 1)
				plain_state |= 1;
		}
	}

	io_set_outputs_raw_mask(plain_state, plain_mask);
}

void outputs_set_zipped(uint8_t data[], size_t length) {
	if (length < 4)
		return;

	uint16_t full_mask = data[1] | (data[0] << 8);
	uint16_t bin_state = data[3] | (data[2] << 8);
	size_t bytei = 4;

	for (size_t i = 0; i < NO_OUTPUTS; i++) {
		if ((full_mask & 1) == 0) {
			_outputs_state[i] = bin_state & 1;
		} else {
			if (bytei < length) {
				_outputs_state[i] = data[bytei];
				bytei++;
			}
		}

		full_mask >>= 1;
		bin_state >>= 1;
	}

	outputs_apply_state();
}

void outputs_set_full(uint8_t data[NO_OUTPUTS]) {
	memcpy((uint8_t*)_outputs_state, (uint8_t*)data, NO_OUTPUTS);
	outputs_apply_state();
}

void outputs_update(void) {
	for (size_t i = 0; i < NO_OUTPUTS; i++) {
		if (!_flicker_enabled[i])
			continue;

		_flicker_counters[i]++;
		uint8_t flicker_type = _outputs_state[i] & 0x0F;
		if (flicker_type >= sizeof(_flicker_periods)/sizeof(*_flicker_periods))
			flicker_type = 0; // error: unknown flicker period → default flicker period
		if (_flicker_counters[i] == _flicker_periods[flicker_type]) {
			io_set_output_raw(i, true);
		} else if (_flicker_counters[i] >= 2*_flicker_periods[flicker_type]) {
			io_set_output_raw(i, false);
			_flicker_counters[i] = 0;
		}
	}
}
