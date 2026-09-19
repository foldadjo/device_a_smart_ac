#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

class StatusLed
{
public:
  explicit StatusLed(uint8_t pin);

  void begin();
  void set(uint8_t r, uint8_t g, uint8_t b);
  void showIrIdle();
  void showIrReceive();
  void showIrTransmit();
  void showIrLearn();
  void showSpeakerIdle();
  void showSpeakerActive();
  void showSpeakerLoud();

private:
  Adafruit_NeoPixel pixel;
};
