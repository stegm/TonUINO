#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "Types.hpp"

// admin settings stored in eeprom
typedef struct {
  uint32_t cookie;
  byte version;
  uint8_t maxVolume;
  uint8_t minVolume;
  uint8_t initVolume;
  uint8_t eq;
  bool locked;
  long standbyTimer;
  bool invertVolumeButtons;
  FolderSettings shortCuts[4];
  uint8_t adminMenuLocked;
  uint8_t adminMenuPin[4];
} AdminSettings;

extern AdminSettings mySettings;

void writeSettingsToFlash(FolderSettings * myFolder);
void resetSettings(uint32_t cardCookie, FolderSettings * myFolder);
void migrateSettings(int oldVersion, FolderSettings * myFolder);
void loadSettingsFromFlash(uint32_t cardCookie, FolderSettings * myFolder);
