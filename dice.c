/*
 * AVR Dice
 *
 * Copyright (c) 2015 Kari Lavikka
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <avr/io.h>
#include <util/delay.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>

#define F_CPU 1000000UL

/* Convenience macros */
#define set_low(reg, bit) reg &= ~(1 << bit)
#define set_high(reg, bit) reg |= (1 << bit)


#define FACES 6
#define WAIT_BEFORE_SLEEP 10

#define BUTTON PB1
#define BEEPER PB0


/*
 * LED layout:
 *
 * 0 - 1
 * 2 3 4
 * 5 - 6
 */

#define DOT_0 (1 << 0)
#define DOT_1 (1 << 1)
#define DOT_2 (1 << 2)
#define DOT_3 (1 << 3)
#define DOT_4 (1 << 4)
#define DOT_5 (1 << 5)
#define DOT_6 (1 << 6)


static const uint8_t faces[] = {
	DOT_3,
	DOT_0 | DOT_6,
	DOT_1 | DOT_3 | DOT_5,
	DOT_0 | DOT_1 | DOT_5 | DOT_6,
	DOT_0 | DOT_1 | DOT_3 | DOT_5 | DOT_6,
	DOT_0 | DOT_1 | DOT_2 | DOT_4 | DOT_5 | DOT_6
};


static const uint8_t spin_sequence[] = {
	DOT_1 | DOT_0,
	DOT_4 | DOT_1,
	DOT_6 | DOT_4,
	DOT_5 | DOT_6,
	DOT_2 | DOT_5,
	DOT_0 | DOT_2
};

/* Gamma correction for led intensity */
static const uint8_t PROGMEM intensity_table[] = {
	0, 0, 0, 0,
	0, 0, 0, 0,
	1, 1, 1, 1,
	2, 2, 3, 3,
	4, 5, 6, 7,
	8, 9, 11, 12,
	14, 16, 18, 20,
	22, 25, 28, 30,
	33, 37, 40, 44,
	48, 52, 56, 60,
	65, 70, 76, 81,
	87, 93, 99, 106,
	113, 120, 127, 135,
	143, 152, 161, 170,
	179, 189, 199, 209,
	220, 231, 243, 255
};


/*
 * Returns true if the button is pressed.
 */
static bool button_down() {
	return PINB & _BV(BUTTON);
}

/*
 * Beeps for 'len' milliseconds
 */
static void beep(int len) {
	set_high(PORTB, BEEPER);

	for (int a = 0; a < len; a++) {
		_delay_ms(1);
	}

	set_low(PORTB, BEEPER);
}

/*
 * Spins the dice until the button is released. Returns a random number.
 */
static uint16_t spin(uint16_t seed) {
	while (true) {
		for (int i = 0; i < sizeof(spin_sequence); i++) {
			PORTA = spin_sequence[i];

			for (int f = 0; f < 32; f++) {
				seed++;

				if (!button_down()) {
					return seed;
				}

				_delay_ms(1);
			}
		}
	}
}

/*
 * Tosses the dice. Returns true if the button was pressed during tossing.
 */
static bool throw(int16_t seed, int16_t previous_seed) {

	// Randomize initial face
	int8_t face = seed % FACES;

	// Make tossing more exciting by adding some variation
	int16_t stop_at = 500 + (seed & 127) * 4;

	// Initial velocity depends on the duration the button was held down.
	// Seed was incremented by one per millisecond
	uint16_t duration = seed - previous_seed;
	if (duration > 1000) {
		duration = 1000;
	}

	uint16_t delay = 50 - duration * 45 / 1000;

	while (delay < stop_at) {
		// Increase the delay exponentially
		delay += 5 + delay / 4;

		for (int i = 0; i < delay; i++) {
			_delay_ms(1);

			if (button_down()) {
				return true;
			}
		}

		PORTA = faces[face];

		beep(3);

		face++;
		if (face >= FACES) {
			face = 0;
		}
	}

	beep(20);

	return false;
}

/*
 * LED fade effect
 */
static void fade() {
	// http://startingelectronics.com/tutorials/AVR-8-microcontrollers/ATtiny2313-tutorial/P11-PWM/
	set_high(DDRB, PB2);                    // PWM output on PB2
	TCCR0A = (1 << COM0A1) | (1 << WGM00);  // phase correct PWM mode
	TCCR0B = (1 << CS01);                   // clock source = CLK/8, start PWM

	OCR0A = 255;
	_delay_ms(500);

	for (int16_t value = sizeof(intensity_table) - 1; value >= 0 && !button_down(); value--) {
		OCR0A = pgm_read_byte(&(intensity_table[value]));
		_delay_ms(1200 / sizeof(intensity_table));
	}

	set_low(DDRB, PB2);
}

/*
 * Handle pin change interrupt
 */
ISR(PCINT1_vect) {
	sleep_disable();
	set_low(PCMSK1, PCINT9);
	set_low(GIMSK, PCIE1);
}

/*
 * Power down
 */
static void sleep() {
	PORTA = 0;
	cli();

	// Activate pin change interrupt and wake up when button is pressed
	set_high(PCMSK1, PCINT9);
	set_high(GIMSK, PCIE1);

	sleep_enable();
	sleep_bod_disable();
	sei();
	sleep_cpu();
}

/*
 * Wait for the button or go to sleep after a while
 */
static void wait_or_sleep() {
	int16_t wait = 1000 * WAIT_BEFORE_SLEEP;
	while (wait-- > 0) {
		if (button_down()) return;
		_delay_ms(1);
	}

	sleep();
}

/*
 * Called when battery is plugged in
 */
static void welcome() {
	PORTA = 0b01111111;
	beep(200);
	PORTA = 0;
}

int main(void) {

	DDRA = 0b0111111;
	DDRB = 0b0000001;

	set_sleep_mode(SLEEP_MODE_PWR_DOWN); // Conserve power when sleeping
	ADCSRA = 0; // Disable ADC

	welcome();

	int16_t previous_seed = 0;
	int16_t seed = 1000;
	
	while (true) {
		wait_or_sleep();
		seed = spin(seed);
		if (!throw(seed, previous_seed)) {
			fade();
		}
		previous_seed = seed;
	}

	return(0);
} 

