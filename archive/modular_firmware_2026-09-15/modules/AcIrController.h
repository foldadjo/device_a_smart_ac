#pragma once

#include <Arduino.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <IRac.h>
#include <Preferences.h>

#include "StatusLed.h"
#include "config.h"

enum LearnSlot
{
  LEARN_NONE,
  LEARN_POWER_ON,
  LEARN_POWER_OFF
};

class AcIrController
{
public:
  AcIrController(uint8_t rxPin, uint8_t txPin, StatusLed &led);

  void begin();
  void service();
  void startLearning(LearnSlot slot);
  bool sendPowerOn();
  bool sendPowerOff();
  bool sendAcState(uint8_t temperature, const String &mode, const String &fan);
  void setAcProtocol(decode_type_t protocol);
  void clearLearnedSignals();
  String runSensorTest();

  String learningLabel() const;
  bool hasPowerOn() const;
  bool hasPowerOff() const;
  uint16_t powerOnLength() const;
  uint16_t powerOffLength() const;
  uint32_t learnSequence() const;
  uint8_t minTemperature() const;
  uint8_t maxTemperature() const;
  decode_type_t acProtocol() const;
  String acProtocolName() const;
  bool acProtocolReady() const;
  bool isAcProtocolSupported(decode_type_t protocol) const;
  uint8_t receiverPin() const;
  uint8_t transmitterPin() const;
  const String &message() const;

private:
  void setReceiverActive(bool active);
  void loadLearnedSignals();
  void loadRaw(const char *lenKey, const char *rawKey, uint16_t *buffer, uint16_t &length);
  void saveRaw(const char *lenKey, const char *rawKey, const uint16_t *buffer, uint16_t length);
  void clearRaw(const char *lenKey, const char *rawKey, uint16_t &length);
  bool isValidTemperature(uint8_t temperature) const;
  bool captureLearnedSignal(decode_results *decode);
  void printDiagnostic(decode_results *decode);
  bool sendRawSignal(const uint16_t *raw, uint16_t length, const char *name);
  void loadAcConfig();
  void saveAcProtocol();
  stdAc::opmode_t parseMode(const String &mode) const;
  stdAc::fanspeed_t parseFan(const String &fan) const;
  String slotLabel(LearnSlot slot) const;
  String jsonEscape(const String &value) const;

  uint8_t rxPin;
  uint8_t txPin;
  StatusLed &led;
  Preferences prefs;
  IRrecv receiver;
  IRsend transmitter;
  decode_results results;
  LearnSlot learnSlot;
  bool receiverActive;
  uint16_t rawPowerOn[MAX_IR_RAW_LEN];
  uint16_t rawPowerOff[MAX_IR_RAW_LEN];
  uint16_t rawPowerOnLen;
  uint16_t rawPowerOffLen;
  uint32_t learnedSequence;
  decode_type_t selectedAcProtocol;
  int16_t selectedAcModel;
  String lastMessage;
};
