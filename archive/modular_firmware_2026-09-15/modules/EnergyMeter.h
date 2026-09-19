#pragma once

#include <Arduino.h>

struct EnergyMeterData
{
  bool valid;
  uint32_t lastReadMs;
  uint32_t successCount;
  uint32_t errorCount;
  uint8_t slaveId;
  String status;
  String lastError;
  String lastRequestHex;
  String lastResponseHex;
  uint8_t lastResponseBytes;

  int32_t rawTotalActiveEnergy;
  uint32_t rawForwardActiveEnergy;
  uint32_t rawReverseActiveEnergy;
  uint16_t rawVoltage;
  uint32_t rawCurrent;
  int32_t rawActivePower;
  int16_t rawPowerFactor;
  uint16_t rawFrequency;
  int32_t rawReactivePower;
  uint32_t rawApparentPower;

  float totalActiveEnergyKwh;
  float forwardActiveEnergyKwh;
  float reverseActiveEnergyKwh;
  float voltageV;
  float currentA;
  float activePowerKw;
  float powerFactor;
  float frequencyHz;
  float reactivePowerKvar;
  float apparentPowerKva;
};

class EnergyMeter
{
public:
  EnergyMeter(uint8_t rxPin,
              uint8_t txPin,
              uint8_t deRePin,
              uint32_t baud,
              uint32_t serialConfig,
              uint8_t slaveId);

  void begin();
  void service();

  const EnergyMeterData &data() const;
  String buildJson() const;

  uint8_t rxPin() const;
  uint8_t txPin() const;
  uint8_t deRePin() const;
  uint32_t baudRate() const;

private:
  static const uint8_t REGISTER_COUNT = 17;
  static const uint16_t START_REGISTER = 0;
  static const uint32_t RESPONSE_TIMEOUT_MS = 700;

  bool readRegisters();
  uint8_t readResponse(uint8_t *buffer, uint8_t maxLength, uint32_t timeoutMs);
  void setTransmitMode(bool transmit);
  void parseRegisters(const uint16_t *registers);
  void logValues(const uint16_t *registers) const;
  void setError(const String &message);
  String bytesToHex(const uint8_t *buffer, uint8_t length) const;
  uint16_t crc16(const uint8_t *buffer, uint8_t length) const;
  uint32_t combineU32(uint16_t highWord, uint16_t lowWord) const;
  int32_t combineS32(uint16_t highWord, uint16_t lowWord) const;
  String jsonEscape(const String &value) const;

  uint8_t rx;
  uint8_t tx;
  uint8_t deRe;
  uint32_t baud;
  uint32_t serialConfig;
  EnergyMeterData meterData;
};
