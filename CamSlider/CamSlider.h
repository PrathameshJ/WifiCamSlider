/*
   TABS=3

	WiFi Camera Slider Controller common definitions

   Copyright 2016 Rob Redford
   This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
   To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/.
   
*/

#ifndef CAMSLIDER_H
#define CAMSLIDER_H

// OSM 17HS24-0644S stepper motor parameters needed for motion calculations
#define STEPS_PER_REV		200
#define PULLEY_TEETH			20
#define BELT_PITCH			2						// 2 MM
#define INCHES_PER_MM		25.4
#define HS24_MAX_SPEED		592.0					// in steps/sec set using calculator at http://www.daycounter.com/Calculators/Stepper-Motor-Calculator.phtml

#define MAX_TRAVEL_DISTANCE	120				// maximum possible travel distance (inches)
#define MAX_TRAVEL_TIME			3600				// maximum possible travel duration (sec)
#define MAX_IMAGES				1000				// maximum number of images (timelapse mode)
#define CARR_SETTLE_TIME		2 					// delay to stabilize carriage before triggering shutter

#define STEPS_PER_MM			(STEPS_PER_REV / (BELT_PITCH * PULLEY_TEETH))
#define INCHES_TO_STEPS(I)	(STEPS_PER_MM * (I) * INCHES_PER_MM)
#define STEPS_TO_INCHES(S)	((S)/(STEPS_PER_MM * INCHES_PER_MM))


typedef enum { STOP_HERE, REVERSE, ONE_CYCLE } EndstopMode;
typedef enum { CARRIAGE_STOP, CARRIAGE_TRAVEL, CARRIAGE_TRAVEL_REVERSE, CARRIAGE_PARKED } CarriageMode;
typedef enum { MOVE_DISABLED, MOVE_VIDEO, MOVE_TIMELAPSE } MoveMode;

typedef struct {
	bool				homing;					// indicates a home move action
	EndstopMode		lastEndstopState;		// saved endstop state
	long				lastTargetPosition;	// saved stepper position target
	float				lastTargetSpeed;		// saved target speed
} Home_State;

// timelapse move inputs & parameters
typedef struct {
	int	totalDistance;						// total distance traveled for the timelapse sequence (user input)
	int	totalDuration;						// total timelapse duration (user input)
	int	totalImages;						// total number of images to take (user input)
	int	moveDistance;						// distance to move in each interval
	int	moveInterval;						// delay in sec between moves
	int	imageCount;							// realtime shutter activation count
	bool	waitToMove;							// waiting to initiate the move
	bool  waitForStop;						// waiting for the current move to end
	bool	enabled;								// enable timelapse movement
} TL_Data;

#endif