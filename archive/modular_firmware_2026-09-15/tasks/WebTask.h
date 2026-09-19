#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "../modules/AcIrController.h"
#include "../modules/EnergyMeter.h"
#include "../modules/MicrophoneMonitor.h"
#include "../modules/SpeakerController.h"
#include "../modules/StatusDisplay.h"
#include "../modules/StatusLed.h"
#include "../modules/WifiConnector.h"
#include "MqttTask.h"
#include "TextToVoiceTask.h"

class WebTask
{
public:
  WebTask(const char *ssid,
          const char *password,
          AcIrController &acIr,
          StatusDisplay &display,
          EnergyMeter &meter,
          MicrophoneMonitor &microphone,
          SpeakerController &speaker,
          StatusLed &led,
          WifiConnector &wifi,
          MqttTask &mqtt,
          TextToVoiceTask &voice);

  void begin();
  void service();
  String ip() const;

private:
  String jsonEscape(const String &value) const;
  String buildStatusJson();
  String buildPage() const;
  void sendJsonResponse();
  void handleRoot();
  void handleStatus();
  void handleVoice();
  void handleVoiceCommand();
  void handleMicrophone();
  void handleSpeakerTest();
  void handleDisplayTest();
  void handleLedTest();
  void handleLearn();
  void handleSend();
  void handleClearIr();
  void handleSensorTest();
  void handleTemperature();
  void handleProtocol();
  void handleWifi();
  void handleFavicon();
  void handleNotFound();

  const char *ssid;
  const char *password;
  AcIrController &acIr;
  StatusDisplay &display;
  EnergyMeter &meter;
  MicrophoneMonitor &microphone;
  SpeakerController &speaker;
  StatusLed &led;
  WifiConnector &wifi;
  MqttTask &mqtt;
  TextToVoiceTask &voice;
  WebServer server;
  uint32_t voiceWakeUntilMs;
  uint8_t remoteSetupStep;
  uint32_t remoteSetupLearnSequence;
};
