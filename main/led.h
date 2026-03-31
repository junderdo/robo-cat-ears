/*
 * Description LED controller header for robo cat ears
 * Author: Jeff Underdown (junderdo)
 * Company: Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef LED_H
#define LED_H

#include <stdint.h>

// LED configuration
#define LED_GPIO_0 6 // GPIO connects to the LED strip
#define LED_STRIP_LED_COUNT 31 // Number of LEDs in the strip
#define LED_STRIP_RMT_RES_HZ (10 * 1000 * 1000) // 10MHz resolution

/**
 * @brief Initialize the LED strip
 * 
 * Configures the RMT peripheral and starts the LED animation task
 */
void init_leds(void);

#endif // LED_H
