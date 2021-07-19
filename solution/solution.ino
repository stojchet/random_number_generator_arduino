#define DEBUG

#include "funshield.h"
#ifndef slowDigitalWrite
#define slowDigitalWrite(a,b) digitalWrite(a,b)
#define constDigitalWrite(a,b) digitalWrite(a,b)
#endif

/* ----------------------------------------------- Prototypes ----------------------------------------------- */

/********************* Buttons Prototypes ********************/
enum ButtonState {UP, DEBOUNCING, DOWN};

struct Button
{
	ButtonState state;
	long deadline;
	long debounce_deadline;
	unsigned char pin;
    int seedOnUp;
    int seedOnDown;
    bool pressed;
    bool useDeadline;
	bool caluclateDeadline;
};

void init(Button& b, int which, int seedDown, int seedUp, bool useDeadline);
int get_pulse(Button& b);

/**************** 7-Segment Display Prototype ****************/
struct Disp
{
	unsigned char place[4]; /* place[0] is the rightmost digit, place[3] the leftmost one */
	unsigned char phase;		/* index of next place to be displayed 0-3 refer to place[], 4==disp off */
};

inline void shift_out8(unsigned char data);
void shift_out(unsigned int bit_pattern);
void disp_7seg(unsigned char column, unsigned char glowing_leds_bitmask);
void disp_init();
void init(Disp& d);
void updatePlaces(Disp& d);

/*********************** Configuration Mode ***********************/
String ConfigurationModeOptions[] = {"04", "06", "08", "10", "12", "20", "00"}; // last one is D100

struct Configuration
{
	String configMode;
	int indexConfigMode;
	int numberOfThrows;
    int randomNumber;
};
void setOutputConfig(Configuration& config);

enum Action {NONE, PRESSED, RELEASED};
//typedef void (*func)();
Action HandleInput(Button& btn);
//int HandleInput(Button& btn, func whenReleased = NULL, func whenPressed = NULL);

/************* Random Number Generator Prototype *************/
unsigned int log2_ceil(int n);
unsigned long rand(unsigned long n);
void seed(unsigned long x);
int generateRandomOutput(int n, int m);

/* ----------------------------------------------- Implementation ----------------------------------------------- */

/************************** Buttons **************************/
// TODO: Check whether I need the slow and fast constants added when buttons are pressed/not pressed
const long t_debounce = 10000;
const long t_slow = 500000;
const long t_fast = 180000;
inline long duration(long now, long then) {
	return ((unsigned long)now) - then;
}

void init(Button& b, int which, int seedDown, int seedUp, bool useDeadline)
{
	b.pin = which;
	b.state = UP;
    b.pressed = false;
    b.seedOnUp = seedUp;
    b.seedOnDown = seedDown;
    b.useDeadline = useDeadline;
	b.caluclateDeadline = true;
	pinMode(which, INPUT);
}

int get_pulse(Button& b)
{
	int on = digitalRead(b.pin) == ON;
	long now = micros();
	switch (b.state)
	{
		case UP:
			if (on)
			{
				b.deadline = now + t_slow;
				b.state = DOWN;
				return 1;
			}
			else
			{
				return 0;
			}

		case DOWN:
			if (on)
			{
				if (b.useDeadline && duration(now, b.deadline) < 0)
				{
					return 0;
				}

				b.deadline += t_fast;
				return 1;
			}
			else
			{
				b.state = DEBOUNCING;
				b.debounce_deadline = now + t_debounce;
				return 0;
			}

		case DEBOUNCING:
			if (on)
			{
				b.state = DOWN;

			}
			else
			{
				if (duration(now, b.debounce_deadline) >= 0)
					b.state = UP;
			}
			return 0;

		default:
			return 0;
	}
}

/*********************** 7-Segment Display ***********************/
Disp display;
const unsigned char font[] = {0b11111100, 0b01100000, 0b11011010, 0b11110010, 0b01100110,
															0b10110110, 0b10111110, 0b11100000, 0b11111110, 0b11110110, 0b01111010, 0b00000000
														 }; // last element of dont is the letter d

// TODO: Check whether I need disp on/off
const long t_disp_on = 60;		 /* [us] duration of a given segment being on */
const long t_disp_bright = 2800; /* [us] duration of highligted */
const long t_disp_off = 5000;	/* [us] duration of the display being off */

inline void shift_out8(unsigned char data)
{
	for (signed char i = 7; i >= 0; i--, data >>= 1) {
		digitalWrite(data_pin, data & 1); /* set up the data bit */
		constDigitalWrite(clock_pin, HIGH); /* upon clock rising edge, the 74HC595 will shift the data bit in */
		constDigitalWrite(clock_pin, LOW);
	}
}

void shift_out(unsigned int bit_pattern)
{
	shift_out8(bit_pattern);
	shift_out8(bit_pattern >> 8);
	constDigitalWrite(latch_pin, HIGH); /* upon latch_pin rising edge, both 74HC595s will instantly change its outputs to */
	constDigitalWrite(latch_pin, LOW); /* its internal value that we've just shifted in */
}

void disp_7seg(unsigned char column, unsigned char glowing_leds_bitmask)
{
	shift_out((1 << (8 + 4 + column)) | (0xFF ^ glowing_leds_bitmask));
}

void disp_init()
{
	pinMode(latch_pin, OUTPUT);
	pinMode(data_pin, OUTPUT);
	pinMode(clock_pin, OUTPUT);
	constDigitalWrite(clock_pin, LOW);
	constDigitalWrite(latch_pin, LOW);
	shift_out(0);	/* turn all the display LEDs off */
}

void init(Disp& d)
{
	d.phase = 0;
	disp_init();
	for (int i = 0; i < 4; i++) d.place[i] = font[i];
}

long power(int i) {
	long res = 10;
	for (int j = 0; j < i; ++j) {
		res *= 10;
	}
	return res;
}

void updatePlaces(Disp& d, int result[])
{
	for (int i = 0; i < 4; i++)
	{
		d.place[i] = font[result[i]];
	}
}

/************************** Output Mode **************************/

void setOutputConfig(Configuration& config){
    int result[] = {config.configMode[1] - '0', config.configMode[0] - '0', 10, config.numberOfThrows};
    updatePlaces(display, result);
}

void setOutputRandomNumber(Configuration& config){
    int result[4];
    for(int i = 0; i < 4; ++i){
        if(config.randomNumber == 0){
            result[i] = 11;
        }
        else{
            result[i] = config.randomNumber % 10;
            config.randomNumber /= 10;
        }
    }
    updatePlaces(display, result);
}


/******************** Random Number Generator ********************/
unsigned int ceil_log2(unsigned long x)
{
  static const unsigned long long t[6] = {
    0xFFFFFFFF00000000ull,
    0x00000000FFFF0000ull,
    0x000000000000FF00ull,
    0x00000000000000F0ull,
    0x000000000000000Cull,
    0x0000000000000002ull
  };

  int y = (((x & (x - 1)) == 0) ? 0 : 1);
  int j = 32;
  int i;

  for (i = 0; i < 6; i++) {
    int k = (((x & t[i]) == 0) ? 0 : j);
    y += k;
    x >>= k;
    j >>= 1;
  }

  return y;
}

unsigned int log2_ceil(int n)
{
    return ceil_log2(n);
}


unsigned long rand(unsigned long n)	 /* assuming sizeof(unsigned long)==4 and random() is returning uniform random numbers from 0 to 2^L2RM (inclusive) */
{ /* for standard Arduino random() function, L2RM=31 */
	unsigned int bin_log = log2_ceil(n);	//i.e. 2^bin_log ≥ n and it is the smallest such number
	unsigned long r;
	do {
		r = random() >> (31 - bin_log);
	} while (r >= n || r == 0);
	return r;
}

void seed(unsigned long x) {
	if (x != 0)
	{
		srandom(x);
	}
	else {
		srandom(1);
	}
}

int generateRandomOutput(int n, int m) {
	int result = 0;
	for (int i = 0; i < m; ++i) {
		seed(random() + (micros() >> 2));
		result += rand(n);
	}
	return result;
}

/****************** Global State of the Program ******************/
Button normalMode, currentConfigurationMode, changeConfigurationMode;
Configuration config = {ConfigurationModeOptions[0], 0, 1};
int updateSeed;

void setup() {
	Serial.begin(9600);
	init(normalMode, button1_pin, 0b001, 0b1000, false);
	init(currentConfigurationMode, button2_pin, 0b010, 0b10000, true);
	init(changeConfigurationMode, button3_pin, 0b100, 0b100000, true);
	init(display);
}

Action HandleInput(Button& btn) {
	int pulse_on = get_pulse(btn);
	Action rtr_val = Action::NONE; 

	if(pulse_on || btn.pressed) {
		updateSeed = btn.seedOnDown;

		if(btn.pressed && !pulse_on) {
			btn.pressed = false;
			btn.caluclateDeadline = true;
			updateSeed = btn.seedOnUp;
            rtr_val = Action::RELEASED;
		}
		else if(!btn.useDeadline) {
			if(btn.caluclateDeadline) {
				btn.caluclateDeadline = false;
				btn.deadline = micros() + t_fast;
			}

			if(duration(micros(), btn.deadline) < 0) {
				btn.pressed = true;
				rtr_val = Action::PRESSED;
			}
			else {
				rtr_val = Action::NONE;
			}
		}
		else {
			btn.pressed = true;
			rtr_val = Action::PRESSED;
		}

		seed(random() + (micros() >> 2) + updateSeed);
	}

	return rtr_val;
}

/*int HandleInput(Button& btn, func whenReleased, func whenPressed) {
	int pulse_on = get_pulse(btn);
	int rtr_val = 0; 

	if(pulse_on || btn.pressed) {
		updateSeed = btn.seedOnDown;

		if(btn.pressed && !pulse_on) {
            Serial.println(0);
			btn.pressed = false;
			updateSeed = btn.seedOnUp;
            if(whenReleased != NULL){
                whenReleased();
            }
		}
        // btn.pressed && pulse_on
        // !btn.pressed && !pulse_on
		else {
            Serial.println(1);
			btn.pressed = true;
			rtr_val = 1;
            if(whenPressed != NULL){
                whenPressed();
            }
		}

		seed(random() + (micros() >> 2) + updateSeed);
	}

	return rtr_val;
}*/

void RollDice() {
	if (config.configMode.toInt() == 0) {
		config.randomNumber = generateRandomOutput(100, config.numberOfThrows);
	}
	else {
		config.randomNumber = generateRandomOutput(config.configMode.toInt(), config.numberOfThrows);
	}

	setOutputRandomNumber(config);
}

void ChangeDice() {
	config.configMode = ConfigurationModeOptions[config.indexConfigMode];
	config.indexConfigMode = (config.indexConfigMode + 1) % (sizeof(ConfigurationModeOptions) / sizeof(String));
	setOutputConfig(config);
}

void ChangeNumberOfThrows() {
	config.numberOfThrows = (config.numberOfThrows + 1) % 10;

	if(config.numberOfThrows == 0) {
		config.numberOfThrows = 1;
	}

	setOutputConfig(config);
}

void printZero(){
    display.place[0] = font[0];
}

void loop() {
	Action normalBtnAction = HandleInput(normalMode);

	if(normalBtnAction == Action::PRESSED) {
		printZero();
	}
	else if(normalBtnAction == Action::RELEASED) {
		RollDice();
	}
	else if(HandleInput(currentConfigurationMode) == Action::PRESSED) {
		ChangeDice();
	}
	else if(HandleInput(changeConfigurationMode) == Action::PRESSED) {
		ChangeNumberOfThrows();
	}

	disp_7seg(display.phase, display.place[display.phase]);
	display.phase = (display.phase + 1) % 4;
}