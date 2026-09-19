#include "StatusLed.h"

StatusLed::StatusLed(uint8_t pin) :
  pixel(1, pin, NEO_GRB + NEO_KHZ800)
{
}

void StatusLed::begin()
{
  pixel.begin();
  pixel.clear();
  pixel.show();
}

void StatusLed::set(uint8_t r, uint8_t g, uint8_t b)
{
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

void StatusLed::showIrIdle()
{
  set(255, 0, 0);
}

void StatusLed::showIrReceive()
{
  set(0, 255, 0);
}

void StatusLed::showIrTransmit()
{
  set(0, 0, 255);
}

void StatusLed::showIrLearn()
{
  set(255, 180, 0);
}

void StatusLed::showSpeakerIdle()
{
  set(0, 30, 255);
}

void StatusLed::showSpeakerActive()
{
  set(0, 255, 60);
}

void StatusLed::showSpeakerLoud()
{
  set(255, 80, 0);
}
