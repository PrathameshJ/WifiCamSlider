/*
   TABS=3

	WiFi Camera Slider Controller
	Controls the stepper motor on the slider as well as providing an HTTP server and AP to enable WiFi control from any browser without a WiFI network host.
	This version uses the TB6612FNG controller and a WeMos D1 Mini ESP8266 (ESP-12F) devboard.
	
	References:
		Stepper library: http://www.airspayce.com/mikem/arduino/AccelStepper/classAccelStepper.html
	
	Prior to running this sketch, the HTML file must be loaded into the ESP8266 SPIFFS file system.
	Currently, the instructions for installing and running the uplaod tool can be found here: http://esp8266.github.io/Arduino/versions/2.3.0/doc/filesystem.html

   Copyright 2016 Rob Redford
   This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
   To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/.
   
*/

#define DEBUG				2

#include <AccelStepper.h>
#include "LED3.h"
#include "CamSlider.h"

/*================================= stepper motor interface ==============================

This code was written for an OSM 17HS24-0644S stepper motor
 References:
	http://www.omc-stepperonline.com/nema-17-60ncm85ozin-064a-bipolar-stepper-motor-17hs240644s-p-19.html (product page)
	http://www.omc-stepperonline.com/download/pdf/17HS24-0644S.pdf (datasheet [partially in Chinese])
	http://www.osmtec.com/stepper_motors.htm (OSM stepper motor catalog)
	
Current controller: TB6612FNG
	https://www.sparkfun.com/products/9457
	
Future driver support:
	http://www.electrodragon.com/product/stepstick-stepper-driver-board-a4988-v2/ (Allegro A4988 chopper driver (microstepping))
	
	
*/

// ================================= stepper ==============================================

#define MOTORA1				14						// ESP8266 pins
#define MOTORA2				12
#define MOTORB1				4
#define MOTORB2				5	
#define STANDBY				16

AccelStepper stepper(AccelStepper::FULL4WIRE, MOTORA1, MOTORA2, MOTORB1, MOTORB2);

// ================================ slider controls =======================================
#define LIMIT_MOTOR		0				// motor-side endstop switch pin - pullup resistor in this pin
#define LIMIT_END			13				// other endstop switch pin - use internal pullup

#define BOUNCE_DELAY		300			// delay window in msec to ignore pin value fluctuation

volatile	bool				newMove = false;							// true when we need to initiate a new move
volatile bool				clockwise = true;
volatile EndstopMode		endstopAction = REVERSE;				// action to take when an endstop is hit
volatile CarriageMode	carriageState = PARKED;					// current state of the carriage (for the motion state machine)
volatile bool				debounce = false;							// ISR debounce control flag
volatile unsigned long 	currentTime = 0;							// set in loop() so we don't have to call it in the ISR
volatile unsigned long	debounceStart = 0;						// start of debounce window
volatile bool				plannedMoveEnd = false;					// true if calling endOfTravel for a planned move termination

long							targetPosition = 0;						// inches to travel
float							targetSpeed = 0.0;						// speed in steps/second
unsigned long				travelStart = 0;							// start of curent carriage movement
unsigned long				lastRunDuration = 0;						// duration of last movement
int							stepsTaken = 0;							// counts steps actually executed
bool							running = false;							// true iff moving the carriage

/* ===================================== LED =================================================

Status colors:
---------------
White		system initializing
Blue		WiFi setup complete
Cyan		motors stopped
Red		user input disabled
Green		carriage in motion

*/

#define LED_RED			1												// 1 & 3 MUST BE DISCONNECTED to use the Serial output (TX & RX)
#define LED_GREEN			3
#define LED_BLUE			2

LED3			led(LED_RED, LED_GREEN, LED_BLUE, LED3_CATHODE);

extern bool	inputPermitted;											// input enable flag
extern void setupWiFi(void);
extern void WiFiService(void);


/*
 light the LED to indicate status - THIS FUNCTION DOES NOT RETURN IF FATAL IS TRUE
*/
void statusLED ( const uint32_t color, const bool fatal = false ) {
	if ( fatal ) {
		while ( true ) {
			led.setLED3Color(color);
			delay(250);
			led.setLED3Color(LED3_OFF);
			delay(250);
		}
	} else {
		led.setLED3Color(color);
	}
}

/*
 endstop ISR (used for both endstops and end of planned moves)
 set flags & state to be used in loop()
 we clear the debounce flag after the debounce interval expires in loop()
*/
void endOfTravel ( void ) {
	if ( plannedMoveEnd || !debounce ) {
#if DEBUG > 0
		Serial.println("**** ENDSTOP HIT ****");
#endif
		switch ( endstopAction ) {
		case STOP_HERE:
			carriageState = CARRIAGE_STOP;
			if ( !plannedMoveEnd ) {
				// reverse direction IFF we hit the endstop switch
				clockwise = !clockwise;
			}
			break;
			
		case REVERSE:
			carriageState = CARRIAGE_TRAVEL_REVERSE;
			clockwise = !clockwise;
			newMove = true;										// execute the same move parameters in the opporsite direction
			break;
			
		case ONE_CYCLE:
			// return once
			carriageState = CARRIAGE_TRAVEL_REVERSE;
			endstopAction = STOP_HERE;							// stop next time
			clockwise = !clockwise;
			newMove = true;
			break;
		
		default:
			break;
		}
		if ( !plannedMoveEnd ) {
			// set up debounce window (see loop())
			debounce = true;
			debounceStart = currentTime;			
		} else {
			plannedMoveEnd = false;
		}
	}
}

void setup ( void ) {
#if DEBUG > 0
	Serial.begin(19200);
	Serial.println("Initializing ...");
#endif
	
	// housekeeping
	statusLED(LED3_WHITE);
	pinMode(MOTORA1, OUTPUT);
	pinMode(MOTORA2, OUTPUT);
	pinMode(MOTORB1, OUTPUT);
	pinMode(MOTORB2, OUTPUT);
	pinMode(STANDBY, OUTPUT);
	pinMode(LIMIT_MOTOR, INPUT);								// WeMos internal pullup resistor
	pinMode(LIMIT_END, INPUT_PULLUP);
	
	stepper.setMaxSpeed(HS24_MAX_SPEED);					// max steps/sec 
	//stepper.setPinsInverted (false, false, true);		// inverted ENABLE pin on Allegro A4988
	stepper.setEnablePin(STANDBY);							// set LOW to standby - internal pulldown in TB6612FNG
	stepper.disableOutputs();									// don't energize the motors or enable controller until user initiates movement
	
	attachInterrupt(digitalPinToInterrupt(LIMIT_MOTOR), endOfTravel, FALLING);
	attachInterrupt(digitalPinToInterrupt(LIMIT_END), endOfTravel, FALLING);
	
	setupWiFi();
	statusLED(LED3_BLUE);
}

 
void loop ( void ) {
	
	yield();
	WiFiService();

#if DEBUG >= 3
	// manual inputs for debugging - note that motor speed will be significantly slower if debug statements are being output
	static bool askForInput = true;
	
	if ( askForInput ) {
		Serial.print("*** INPUT DISTANCE IN INCHES and ELAPSED TIME: ");
		askForInput = false;
	}
	if ( Serial.available() ) {
		int inches, elapsed;
		
		inches = Serial.parseInt();
		elapsed = Serial.parseInt();

		if ( (inches > 0) && (elapsed > 0) ) {
			targetPosition = (long)INCHES_TO_STEPS(inches);
			targetSpeed = (float)(targetPosition / elapsed);							// steps per second
			newMove = true;
			askForInput = true;
			Serial.println(String("Target Position: ") + String(targetPosition) + String(" steps at ") + String(targetSpeed) + String(" steps/sec"));
		}
		Serial.flush();
	}
#endif

	// check if the ISR debounce window is now closed
	currentTime = millis();
	if ( debounce && ((currentTime - debounceStart) > BOUNCE_DELAY) ) {
		debounce = false;
	}
	

#if DEBUG >= 3
	static int counter = 0;
	if ( counter++ > 500 ) {
		Serial.println(String("Reamaining steps: ") + String(stepper.distanceToGo()) + String(" carriage state: ") + String(carriageState));
		Serial.println(String("\tPOSITIONS: current: ") + String(stepper.currentPosition()) + String(" target: ") + String(stepper.targetPosition()));
		counter = 0;
	}
#endif
	/*
	  motion state machine - move flag always set OUTSIDE of this FSM
	  NOTE: becuase of the WiFi interface, it is essential to use the non-blocking stepper library calls for movement
	*/
	switch ( carriageState ) {
	case CARRIAGE_TRAVEL:
		/*
		 when moving CCW, the position will increment negatively from 0 (CW = positive)
		 however, distanceToGo in CCW rotation will start at target and INCREASE (in the negative direction)
		 ==> we need to check the absolute value of position rather than relying on distanceToGo() which
		 always increases positively regardless of direction (since it is subtracting a negative number in CCW rotation)
		*/
		if ( abs(stepper.currentPosition()) < targetPosition ) {
			// constant speed - no acceleration
			if ( stepper.runSpeed() ) {
				++stepsTaken;
			}								
		} else if ( stepsTaken == targetPosition ) {
			// target reached without hitting the endstop, so simulate it to initiate next step (if any)
			plannedMoveEnd = true;
			endOfTravel();
		}
		break;
		
	case CARRIAGE_STOP:
#if DEBUG >= 1
			Serial.println(String("*** Traveled ") + String(targetPosition) + String(" steps in ") + String((float)((millis() - travelStart)/1000.0)) + String(" sec"));
#endif
		stepper.stop();
		stepper.runSpeedToPosition();				// a necessary part of the stop mechanism
		carriageState = PARKED;						// only place this is set other than initial condition
		stepper.disableOutputs();
		running = false;
		if ( travelStart ) {
			lastRunDuration = millis() - travelStart;
			travelStart = 0;
		}
		break;
		
	case CARRIAGE_TRAVEL_REVERSE:
		// stop the motion in the current direction - move flag will be set in the ISR so new (opposite) movement will be initiaited below
		stepper.stop();
		stepper.runSpeedToPosition();
		break;
		
	case PARKED:
		if ( inputPermitted ) {
			statusLED(LED3_CYAN);					// indicates motors stopped
		} else {
			statusLED(LED3_RED);						// controls locked out
		}
		// FALLTHRU
	default:
			break;
	}
	
	if ( newMove ) {
		// initiate a new move using current settings
#if DEBUG >= 1
		Serial.println(String(">>> Move to ") + String(targetPosition) + String(" at speed ") + String(targetSpeed) + String( " direction ") + String(clockwise));
#endif
		stepper.setCurrentPosition(0);
		stepper.moveTo(targetPosition);
		stepper.setSpeed(clockwise ? targetSpeed : (targetSpeed * -1.0));
		if ( carriageState == PARKED ) {
			// enable the motor & controller only if it had been turned off
			stepper.enableOutputs();
		}
		carriageState = CARRIAGE_TRAVEL;
		travelStart = millis();
		running = true;
		stepsTaken = 0;
		statusLED(LED3_GREEN);
		newMove = false;
	}
 }
