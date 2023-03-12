#pragma once

#include "PlayMode.hpp"

typedef struct {
  uint8_t folder;
  PlayMode mode;
  uint8_t special;
  uint8_t special2;
} FolderSettings;

#define PIN_DIGIT_COUNT 4u

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))
