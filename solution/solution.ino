#include "funshield.h"
#ifndef slowDigitalWrite
#define slowDigitalWrite(a,b) digitalWrite(a,b)
#define constDigitalWrite(a,b) digitalWrite(a,b)
#endif

/* ----------------------------------------------- Prototypes ----------------------------------------------- */

/********************* Buttons Prototype ********************/
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
	long longPressDeadline;
    bool enableLongPress;
	bool caluclateLongPressDeadline;
	bool checkPressDone;
};

void init(Button& b, int which, int seedDown, int seedUp, bool enableLongPress);
int get_pulse(Button& b);

/**************** 7-Segment Display Prototype ****************/
struct Disp
{
	unsigned char place[4];	/* place[0] is the rightmost digit, place[3] the leftmost one */
	unsigned char phase;	/* index of next place to be displayed 0-3 refer to place[], 4==disp off */
	unsigned long anchor;
};

inline void shift_out8(unsigned char data);
void shift_out(unsigned int bit_pattern);
void disp_7seg(unsigned char column, unsigned char glowing_leds_bitmask);
void disp_init();
void init(Disp& d);
void updatePlaces(Disp& d);

/**************** Configuration Mode Prototype ****************/
int configModeOptionsMapper[] = {4, 6, 8, 10, 12, 20, 0};
/* enum is only in use for better readability */
enum ConfigurationModeOptions {D04 = 4, D06 = 6, D08 = 8, D10 = 10, D12 = 12, D20 = 20, D100 = 0};

struct Configuration
{
	ConfigurationModeOptions configMode;
	int indexConfigMode;
	int numberOfThrows;
    int randomNumber;
};
void init(Configuration& conf);

/************* Random Number Generator Prototype *************/
unsigned int log2_ceil(int n);
unsigned long rand(unsigned long n);
void seed(unsigned long x);
int generateRandomOutput(int n, int m);

/********************  Displaying Output ********************/
void setOutputConfig(Configuration& conf, Disp& disp);
void setOutputRandomNumber(Configuration& conf, Disp& disp);
void printZero(Disp& disp);

/******************** Generating output from user actions ********************/
void RollDice(Configuration& conf, Disp& disp);
void ChangeDice(Configuration& conf, Disp& disp);
void ChangeNumberOfThrows(Configuration& conf, Disp& disp);

/******************** Input Handling ********************/
enum Action {NONE, PRESS, LONG_PRESS, RELEASE};
Action HandleInput(Button& btn);

/******************* Global State Variables ******************/
Button normalMode, currentConfigurationMode, changeConfigurationMode;
Disp display;
Configuration config;
int updateSeed;
/* enum is only in use for better readability */
enum LightDigits {ALL = -1, DICE_TYPE = 1, NUMBER_OF_THROWS = 3};
LightDigits current = ALL;
bool loading = false;
const long short_display = 100;
const long long_display = 10000;
const long loadingZero = 100000;

/* ----------------------------------------------- Implementation ----------------------------------------------- */

/************************** Buttons **************************/
const long t_debounce = 10000;
const long t_slow = 500000;
const long t_fast = 180000;

inline long duration(long now, long then) {
	return ((unsigned long)now) - then;
}

void init(Button& b, int which, int seedDown, int seedUp, bool enableLongPress, bool checkPressDone)
{
	b.pin = which;
	b.state = UP;
    b.pressed = false;
    b.seedOnUp = seedUp;
    b.seedOnDown = seedDown;
    b.enableLongPress = enableLongPress;
	b.caluclateLongPressDeadline = true;
	b.checkPressDone = checkPressDone;
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
				if (!b.enableLongPress && duration(now, b.deadline) < 0)
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
/* second to last element of font is the letter d and last element is all lights off */
const unsigned char font[] = {0b11111100, 0b01100000, 0b11011010, 0b11110010, 0b01100110,
					0b10110110, 0b10111110, 0b11100000, 0b11111110, 0b11110110, 0b01111010, 0b00000000};

const long t_disp_on = 60;			/* [us] duration of a given segment being on */
const long t_disp_bright = 2800;	/* [us] duration of highligted */
const long t_disp_off = 5000;		/* [us] duration of the display being off */

inline void shift_out8(unsigned char data)
{
	for (signed char i = 7; i >= 0; i--, data >>= 1) {
		digitalWrite(data_pin, data & 1);	/* set up the data bit */
		constDigitalWrite(clock_pin, HIGH); /* upon clock rising edge, the 74HC595 will shift the data bit in */
		constDigitalWrite(clock_pin, LOW);
	}
}

void shift_out(unsigned int bit_pattern)
{
	shift_out8(bit_pattern);
	shift_out8(bit_pattern >> 8);
	constDigitalWrite(latch_pin, HIGH); /* upon latch_pin rising edge, both 74HC595s will instantly change its outputs to */
	constDigitalWrite(latch_pin, LOW);	/* its internal value that we've just shifted in */
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

void updatePlaces(Disp& d, int result[])
{
	for (int i = 0; i < 4; i++)
	{
		d.place[i] = font[result[i]];
	}
}

/********************** Configuration Mode **********************/
void init(Configuration& conf){
	conf.configMode = ConfigurationModeOptions::D04;
	conf.numberOfThrows = 1;
	conf.indexConfigMode = 0;
	conf.randomNumber = 0;
}

/******************** Random Number Generator ********************/
unsigned int log2_ceil(unsigned long x)
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

	for (i = 0; i < 6; i++)
	{
		int k = (((x & t[i]) == 0) ? 0 : j);
		y += k;
		x >>= k;
		j >>= 1;
	}

	return y;
}

unsigned long rand(unsigned long n)	/* assuming sizeof(unsigned long)==4 and random() is returning uniform random numbers from 0 to 2^L2RM (inclusive) */
{ 									/* for standard Arduino random() function, L2RM=31 */
	unsigned int bin_log = log2_ceil(n);	//i.e. 2^bin_log ??? n and it is the smallest such number
	unsigned long r;
	do
	{
		r = random() >> (31 - bin_log);
	}
	while (r >= n);

	return r;
}

void seed(unsigned long x)
{
	if (x != 0)
	{
		srandom(x);
	}
	else
	{
		srandom(1);
	}
}

int generateRandomOutput(int n, int m)
{
	int result = 0;
	for (int i = 0; i < m; ++i)
	{
		seed(random() + (micros() >> 2));
		result += (rand(n) + 1);
	}

	return result;
}

/* --------------------------------------  Processing input and generating output  -------------------------------------- */
/********************  Displaying Output ********************/
void setOutputConfig(Configuration& conf, Disp& disp)
{
	if(conf.configMode == ConfigurationModeOptions::D10 || conf.configMode == ConfigurationModeOptions::D12 || conf.configMode == ConfigurationModeOptions::D20)
	{
		int result[] = {conf.configMode % 10, conf.configMode / 10, 10, conf.numberOfThrows};
	    updatePlaces(disp, result);
	}
	else
	{
		int result[] = {conf.configMode, 0, 10, conf.numberOfThrows};
		updatePlaces(disp, result);
	}
}

void setOutputRandomNumber(Configuration& conf, Disp& disp)
{
    int result[4];
    for(int i = 0; i < 4; ++i)
	{
        if(conf.randomNumber == 0)
		{
            result[i] = 11;
        }
        else
		{
            result[i] = conf.randomNumber % 10;
            conf.randomNumber /= 10;
        }
    }
    updatePlaces(disp, result);
}

void printZero(Disp& disp)
{
	int result[] = {11, 11, 11, 11};
	result[disp.phase] = 0;
    updatePlaces(disp, result);
	loading = true;
}

/******************** Generating output from user actions ********************/
void RollDice(Configuration& conf, Disp& disp)
{
	if (conf.configMode == ConfigurationModeOptions::D100)
	{
		conf.randomNumber = generateRandomOutput(100, conf.numberOfThrows);
	}
	else
	{
		conf.randomNumber = generateRandomOutput(conf.configMode, conf.numberOfThrows);
	}

	setOutputRandomNumber(conf, disp);
}

void ChangeDice(Configuration& conf, Disp& disp)
{
	conf.indexConfigMode = (conf.indexConfigMode + 1) % (sizeof(configModeOptionsMapper) / sizeof(int));
	conf.configMode = (ConfigurationModeOptions)configModeOptionsMapper[conf.indexConfigMode];
	setOutputConfig(conf, disp);
}

void ChangeNumberOfThrows(Configuration& conf, Disp& disp)
{
	conf.numberOfThrows = (conf.numberOfThrows + 1) % 10;

	if(conf.numberOfThrows == 0)
	{
		conf.numberOfThrows = 1;
	}

	setOutputConfig(conf, disp);
}

/******************** Input Handling ********************/
Action HandleInput(Button& btn)
{
	int pulse_on = get_pulse(btn);
	Action rtr_val = Action::NONE;

	if(pulse_on || btn.pressed)
	{
		updateSeed = btn.seedOnDown;

		if(btn.pressed && !pulse_on)
		{
			btn.pressed = false;
			btn.caluclateLongPressDeadline = true;
			updateSeed = btn.seedOnUp;
            rtr_val = Action::RELEASE;
		}
		else if(btn.enableLongPress)
		{
			if(btn.caluclateLongPressDeadline)
			{
				btn.caluclateLongPressDeadline = false;
				btn.longPressDeadline = micros() + t_fast;
			}

			if(duration(micros(), btn.longPressDeadline) > 0)
			{
				rtr_val = Action::LONG_PRESS;
			}
			else
			{
				rtr_val = Action::PRESS;
			}

			btn.pressed = true;
		}
		else
		{
			btn.pressed = true;
			rtr_val = Action::PRESS;
		}

		seed(random() + (micros() >> 2) + updateSeed);
	}

	return rtr_val;
}

/****************** Global State of the Program ******************/
void setup()
{
	Serial.begin(9600);
	init(normalMode, button1_pin, 0b001, 0b1000, true, true);
	init(currentConfigurationMode, button2_pin, 0b010, 0b10000, false, false);
	init(changeConfigurationMode, button3_pin, 0b100, 0b100000, false, false);
	init(display);
	init(config);
	setOutputConfig(config, display);
}

void loop()
{
	Action normalBtnAction = HandleInput(normalMode);

	if(normalBtnAction == Action::LONG_PRESS)
	{
		printZero(display);
		currentConfigurationMode.checkPressDone = false;
		changeConfigurationMode.checkPressDone = false;
		current = LightDigits::ALL;
	}
	else if(normalBtnAction == Action::RELEASE)
	{
		RollDice(config, display);
		currentConfigurationMode.checkPressDone = false;
		changeConfigurationMode.checkPressDone = false;
		current = LightDigits::ALL;
	}
	else if(HandleInput(currentConfigurationMode) == Action::PRESS)
	{
		if(currentConfigurationMode.checkPressDone)
		{
			ChangeDice(config, display);
		}
		else
		{
			setOutputConfig(config, display);
			currentConfigurationMode.checkPressDone = true;
		}

		changeConfigurationMode.checkPressDone = false;
		current = LightDigits::DICE_TYPE;
	}
	else if(HandleInput(changeConfigurationMode) == Action::PRESS)
	{
		if(changeConfigurationMode.checkPressDone)
		{
			ChangeNumberOfThrows(config, display);
		}
		else
		{
			setOutputConfig(config, display);
			changeConfigurationMode.checkPressDone = true;
		}
		currentConfigurationMode.checkPressDone = false;
		current = LightDigits::NUMBER_OF_THROWS;
	}

	disp_7seg(display.phase, display.place[display.phase]);

	long now = micros();
	long curDisplay = short_display;
	if(current == display.phase || (current == LightDigits::DICE_TYPE && display.phase == 0))
	{
		curDisplay = long_display;
	}
	else if(loading)
	{
		curDisplay = loadingZero;
		loading = false;
	}

	if(duration(now, display.anchor) > curDisplay)
	{
		display.anchor = now;
		display.phase = (display.phase + 1) % 4;
	}
}