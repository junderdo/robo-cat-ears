/*
 * Description: Keyframe animation playback for robo cat ears
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CUSTOM_ANIMATION_H
#define CUSTOM_ANIMATION_H

#include "types/custom_animation_types.h"

/**
 * @brief Play a custom keyframe animation
 *
 * Blocks for the full duration of the animation, driving the servos at a fixed
 * tick while interpolating between keyframes. The servos are left at the pose
 * of the final keyframe; the animation data decides where the ears end up.
 *
 * @param anim Animation to play
 */
void custom_animation_play(const custom_animation_t *anim);

#endif // CUSTOM_ANIMATION_H
