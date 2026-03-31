/*
 * Description: Servo control and animation functions for robo cat ears
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SERVO_H
#define SERVO_H

// Servo configuration
#define SERVO_MIN_PULSEWIDTH_US 500  // Minimum pulse width in microsecond
#define SERVO_MAX_PULSEWIDTH_US 2500  // Maximum pulse width in microsecond
#define SERVO_MAX_ANGLE         180    // Maximum angle (0-180 degrees)
#define SERVO_FREQ              50     // 50Hz for standard servos
#define SERVO_PULSE_GPIO_0        2      // GPIO connects to the PWM signal line
#define SERVO_PULSE_GPIO_1        3      // GPIO connects to the PWM signal line
#define SERVO_PULSE_GPIO_2        4      // GPIO connects to the PWM signal line
#define SERVO_PULSE_GPIO_3        5      // GPIO connects to the PWM signal line

/**
 * @brief Initialize the servo motors
 * 
 * Configures 4 servos on GPIO pins 2-5 and resets them to center position
 */
void init_servos(void);

/**
 * @brief Reset all servos to center position (90 degrees)
 */
void reset_servos(void);

/**
 * @brief Animation 1: Happy wiggle
 * 
 * Performs an upbeat, happy ear movement pattern
 */
void do_animation_1(void);

/**
 * @brief Animation 2: Sad
 * 
 * Performs a droopy, sad ear movement pattern
 */
void do_animation_2(void);

/**
 * @brief Animation 3: Playful bounce
 * 
 * Performs rapid, playful bouncing movements
 */
void do_animation_3(void);

/**
 * @brief Animation 4: Curious tilt
 * 
 * Alternates ears up and down in a curious manner
 */
void do_animation_4(void);

/**
 * @brief Animation 5: Listening/Radar
 * 
 * Rotates ears side to side like scanning or listening
 */
void do_animation_5(void);

/**
 * @brief Animation 6: Excited twitch
 * 
 * Performs rapid excited twitching movements
 */
void do_animation_6(void);

#endif // SERVO_H
