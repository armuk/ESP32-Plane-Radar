#pragma once

/** Host stand-in for ESP-IDF's GPIO numbers (config.h only names pins). */
typedef enum {
  GPIO_NUM_0 = 0,
  GPIO_NUM_1 = 1,
  GPIO_NUM_3 = 3,
  GPIO_NUM_4 = 4,
  GPIO_NUM_9 = 9,
  GPIO_NUM_10 = 10,
} gpio_num_t;
