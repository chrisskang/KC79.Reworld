#pragma once


#define MOTION_STEPS_PER_MOTOR_ROTATION 32
#define MOTION_STEPPER_GEAR_REDUCTION 9759 / 296
#define MOTION_GEAR_DRIVE 21
#define MOTION_GEAR_RING 118

#define MOTION_STEPS_PER_PRISM_ROTATION ( MOTION_STEPS_PER_MOTOR_ROTATION \
	* MOTION_GEAR_RING \
	* MOTION_STEPPER_GEAR_REDUCTION \
	/ MOTION_GEAR_DRIVE )