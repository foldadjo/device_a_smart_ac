#pragma once

#include <Arduino.h>

#include "EnergyMeter.h"

class StatusDisplay
{
public:
  StatusDisplay(uint8_t sdaPin, uint8_t sclPin);

  void begin();
  void update(const String &ip,
              const String &irMessage,
              const String &learning,
              bool hasOn,
              bool hasOff,
              const String &voiceStatus,
              bool voiceSpeaking,
              const EnergyMeterData &meter,
              bool force = false);

  bool isReady() const;
  const String &status() const;
  const String &error() const;
  uint8_t sdaPin() const;
  uint8_t sclPin() const;
  bool showTestScreen(const String &title,
                      const String &line1,
                      const String &line2,
                      const String &line3,
                      uint32_t holdMs = 7000);

private:
  uint8_t sda;
  uint8_t scl;
  bool ready;
  String displayStatus;
  String errorDetail;
  uint32_t lastUpdateMs;
  uint32_t testScreenUntilMs;
};
