#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "led_strip.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize LED indicators (must be called after LED strip is initialized)
 */
void led_indicators_init(void);

/**
 * @brief Show speech detection indicator (blue, pulsing)
 * @param active True to show speech detected, false to turn off
 */
void led_indicators_speech_detected(bool active);

/**
 * @brief Show wake word detection indicator (green flash)
 */
void led_indicators_wake_word_detected(void);

/**
 * @brief Clear all LED indicators
 */
void led_indicators_clear(void);

/**
 * @brief Set the LED strip handle (internal use, called from app_main.c)
 */
void led_indicators_set_strip(led_strip_handle_t strip);

#ifdef __cplusplus
}
#endif
