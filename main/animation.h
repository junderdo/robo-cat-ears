/*
 * Description: Animation header file for robo cat ears
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <esp_err.h>

#include "servo_calibration.h"

#ifndef ANIMATION_H
#define ANIMATION_H

#define NUM_ANIMATIONS 8


 /**
 * @brief Dispatch function to execute the specified animation by ID
 */
void do_animation(int8_t animation_id);

 /**
 * @brief Animation 1: Left ear listening intently
 * 
 * Performs a focused listening movement with the left ear
 */
void do_animation_1(servo_calibration_t calibration);

 /**
 * @brief Animation 2: Right ear listening intently
 * 
 * Performs a focused listening movement with the right ear
 */
void do_animation_2(servo_calibration_t calibration);

 /**
 * @brief Animation 3: Happy wiggle
 * 
 * Performs an upbeat, happy ear movement pattern
 */
void do_animation_3(servo_calibration_t calibration);

/**
 * @brief Animation 4: Sad
 * 
 * Performs a droopy, sad ear movement pattern
 */
void do_animation_4(servo_calibration_t calibration);

/**
 * @brief Animation 5: Playful bounce
 * 
 * Performs rapid, playful bouncing movements
 */
void do_animation_5(servo_calibration_t calibration);

/**
 * @brief Animation 6: Curious tilt
 * 
 * Alternates ears up and down in a curious manner
 */
void do_animation_6(servo_calibration_t calibration);

/**
 * @brief Animation 7: Listening/Radar
 * 
 * Rotates ears side to side like scanning or listening
 */
void do_animation_7(servo_calibration_t calibration);

/**
 * @brief Animation 8: Excited twitch
 * 
 * Performs rapid excited twitching movements
 */
void do_animation_8(servo_calibration_t calibration);

/**
 * @brief Start the animation mode task
 * 
 * Loads the saved animation_mode_t from NVS and starts a FreeRTOS task that
 * randomly calls do_animation(1) or do_animation(2) at the saved frequency
 * (number of times per hour). Does nothing if frequency is 0 or already running.
 */
void start_animation_mode(void);

/**
 * @brief Stop the animation mode task
 * 
 * Stops and deletes the animation mode task if it is running.
 */
void stop_animation_mode(void);

#endif // ANIMATION_H