#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "DebugConfig.h"

#if ACTIVATE_TTS_MODULE
#include <WitAITTS.h>
#endif

#include "../modules/StatusLed.h"
#include "../modules/WifiConnector.h"

enum VoiceEventKind
{
  VOICE_EVENT_STARTUP,
  VOICE_EVENT_AC_ON,
  VOICE_EVENT_AC_OFF,
  VOICE_EVENT_TEMP_SET,
  VOICE_EVENT_PROTOCOL_SET,
  VOICE_EVENT_WIFI_SAVED
};

struct VoiceEvent
{
  VoiceEventKind kind;
  uint8_t temperature;
  char detail[281];
};

class TextToVoiceTask
{
public:
  TextToVoiceTask(WifiConnector &wifi,
                  StatusLed &led,
                  uint8_t bclkPin,
                  uint8_t lrcPin,
                  uint8_t doutPin);

  void begin();
  bool announceStartup();
  bool announceText(const String &text);
  bool announceAcOn();
  bool announceAcOff();
  bool announceTemperatureSet(uint8_t temperature);
  bool announceProtocolSet(const String &protocol);
  bool announceWifiSaved();
  void stop();
  void setVolume(uint8_t volume);

  uint8_t bclkPin() const;
  uint8_t lrcPin() const;
  uint8_t doutPin() const;
  uint8_t volume() const;
  bool isReady() const;
  bool isSpeaking() const;
  const String &status() const;
  const String &lastText() const;

private:
  static TextToVoiceTask *activeInstance;
  static void taskEntry(void *param);
  static void errorThunk(String error);

  bool enqueue(const VoiceEvent &event);
  bool initializeTts();
  String textForEvent(const VoiceEvent &event) const;
  String numberToWords(uint8_t value) const;
  void speak(const String &text);
  void runTask();

  WifiConnector &wifi;
  StatusLed &led;
  uint8_t bclk;
  uint8_t lrc;
  uint8_t dout;
#if ACTIVATE_TTS_MODULE
  WitAITTS tts;
#endif
  Preferences prefs;
  QueueHandle_t voiceQueue;
  TaskHandle_t taskHandle;
  uint8_t voiceVolume;
  bool ready;
  bool initializationAttempted;
  bool speaking;
  String voiceStatus;
  String lastSpokenText;
};
