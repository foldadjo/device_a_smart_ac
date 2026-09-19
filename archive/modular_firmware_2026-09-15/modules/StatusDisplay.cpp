#include "StatusDisplay.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <Wire.h>

#include "DebugConfig.h"
#include "config.h"

namespace
{
const uint8_t OLED_WIDTH = 128;
const uint8_t OLED_HEIGHT = 64;
const int8_t OLED_RESET_PIN = -1;

Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);
}

StatusDisplay::StatusDisplay(uint8_t sdaPin, uint8_t sclPin) :
  sda(sdaPin),
  scl(sclPin),
  ready(false),
  displayStatus("Not initialized"),
  errorDetail("-"),
  lastUpdateMs(0),
  testScreenUntilMs(0)
{
}

void StatusDisplay::begin()
{
#if ENABLE_OLED_LOGS
  Serial.println("OLED init start");
#endif
  Wire.begin(sda, scl);
  displayStatus = "Initializing";
  errorDetail = "-";

  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR))
  {
#if ENABLE_OLED_LOGS
    Serial.println("OLED init failed at 0x3C, trying 0x3D");
#endif

    if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3D))
    {
      ready = false;
      displayStatus = "Failed";
      errorDetail = "OLED not found at 0x3C or 0x3D";
#if ENABLE_OLED_LOGS
      Serial.println("OLED INIT FAILED");
#endif
      return;
    }
  }

  ready = true;
  displayStatus = "Initialized";
  errorDetail = "-";

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("AQU AC Remote");
  oled.setCursor(0, 12);
  oled.println("OLED siap");
  oled.display();

#if ENABLE_OLED_LOGS
  Serial.println("OLED INIT OK");
#endif
}

void StatusDisplay::update(const String &ip,
                           const String &irMessage,
                           const String &learning,
                           bool hasOn,
                           bool hasOff,
                           const String &voiceStatus,
                           bool voiceSpeaking,
                           const EnergyMeterData &meter,
                           bool force)
{
  // Refresh OLED berkala: IP, status IR/TTS, dan ringkasan meter jika valid.
  if (!ready)
  {
    return;
  }

  uint32_t now = millis();
  if (now < testScreenUntilMs)
  {
    return;
  }

  if (!force && now - lastUpdateMs < 1500)
  {
    return;
  }
  lastUpdateMs = now;

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);

  oled.setCursor(0, 0);
  oled.println("AQU AC Remote");

  oled.setCursor(0, 12);
  oled.print("IP ");
  oled.println(ip);

  oled.setCursor(0, 24);
  oled.print("Learn ");
  oled.print(learning);
  oled.print(" ON:");
  oled.print(hasOn ? "Y" : "N");
  oled.print(" OFF:");
  oled.println(hasOff ? "Y" : "N");

  oled.setCursor(0, 36);
  if (meter.valid)
  {
    oled.print("V ");
    oled.print(meter.voltageV, 1);
    oled.print(" I ");
    oled.print(meter.currentA, 2);
    oled.println("A");
  }
  else
  {
    oled.print("Meter ");
    oled.println(meter.lastError.substring(0, 14));
  }

  oled.setCursor(0, 48);
  if (meter.valid)
  {
    oled.print("P ");
    oled.print(meter.activePowerKw, 3);
    oled.print("kW PF ");
    oled.println(meter.powerFactor, 2);
  }
  else
  {
    oled.print("IR ");
    oled.println(irMessage.substring(0, 18));
  }

  oled.setCursor(0, 56);
  if (meter.valid)
  {
    oled.print("E ");
    oled.print(meter.totalActiveEnergyKwh, 2);
    oled.println(" kWh");
  }
  else
  {
    oled.print("TTS ");
    oled.print(voiceSpeaking ? "PLAY " : "IDLE ");
    oled.println(voiceStatus.substring(0, 8));
  }

  oled.display();
}

bool StatusDisplay::isReady() const
{
  return ready;
}

const String &StatusDisplay::status() const
{
  return displayStatus;
}

const String &StatusDisplay::error() const
{
  return errorDetail;
}

uint8_t StatusDisplay::sdaPin() const
{
  return sda;
}

uint8_t StatusDisplay::sclPin() const
{
  return scl;
}

bool StatusDisplay::showTestScreen(const String &title,
                                   const String &line1,
                                   const String &line2,
                                   const String &line3,
                                   uint32_t holdMs)
{
  if (!ready)
  {
    return false;
  }

  testScreenUntilMs = millis() + holdMs;
  lastUpdateMs = millis();

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);

  oled.setCursor(0, 0);
  oled.println(title.substring(0, 21));
  oled.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  oled.setCursor(0, 16);
  oled.println(line1.substring(0, 21));
  oled.setCursor(0, 28);
  oled.println(line2.substring(0, 21));
  oled.setCursor(0, 40);
  oled.println(line3.substring(0, 21));
  oled.setCursor(0, 56);
  oled.println("Web UI module test");
  oled.display();

  return true;
}
