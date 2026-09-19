#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>

#include "../modules/AcIrController.h"
#include "../modules/EnergyMeter.h"
#include "../modules/WifiConnector.h"
#include "TextToVoiceTask.h"

class MqttTask
{
public:
  MqttTask(WifiConnector &wifi, AcIrController &acIr, EnergyMeter &meter, TextToVoiceTask &voice);

  void begin();
  void service();

  String deviceId() const;
  String controlTopic() const;
  String stateTopic() const;
  bool isConnected();
  const String &status() const;
  const String &lastPayload() const;

private:
  static MqttTask *activeInstance;
  static void callbackThunk(char *topic, byte *payload, unsigned int length);

  void connect();
  void handleMessage(char *topic, byte *payload, unsigned int length);
  void publishMeterState();
  String buildMeterStateJson() const;
  String utcTimestamp() const;
  void executeCommand(const String &payload);
  bool applyProtocolToken(const String &token);
  decode_type_t protocolFromName(const String &name) const;
  String readToken(const String &payload, const String &key, const String &fallback = "") const;
  int readIntToken(const String &payload, const String &key, int fallback) const;

  WifiConnector &wifi;
  AcIrController &acIr;
  EnergyMeter &meter;
  TextToVoiceTask &voice;
  WiFiClient wifiClient;
  PubSubClient client;
  String mqttStatus;
  String mqttDeviceId;
  String mqttControlTopic;
  String mqttStateTopic;
  String lastMqttPayload;
  uint32_t lastConnectAttemptMs;
  uint32_t lastPublishedMeterReadMs;
  bool timeConfigured;
};
