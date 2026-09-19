#pragma once

#include <Arduino.h>

class MicrophoneMonitor
{
public:
  MicrophoneMonitor(uint8_t bclkPin, uint8_t wsPin, uint8_t dinPin, uint32_t sampleRate);

  void begin();
  void resetStats();

  bool isReady() const;
  const String &status() const;
  const String &error() const;
  uint8_t bclkPin() const;
  uint8_t wsPin() const;
  uint8_t dinPin() const;
  uint32_t sampleRate() const;
  uint32_t rms() const;
  uint32_t peak() const;
  uint32_t chunksRead() const;
  uint32_t readErrors() const;
  uint32_t lastReadMs() const;

private:
  static void taskEntry(void *param);

  void runTask();
  bool initI2s();
  void updateAudioStats(const int32_t *rawSamples, size_t rawWordCount);
  void setError(const String &message);

  uint8_t bclk;
  uint8_t ws;
  uint8_t din;
  uint32_t rate;
  bool i2sInstalled;
  TaskHandle_t taskHandle;
  bool ready;
  String micStatus;
  String errorDetail;
  volatile uint32_t levelRms;
  volatile uint32_t levelPeak;
  volatile uint32_t totalChunksRead;
  volatile uint32_t totalReadErrors;
  volatile uint32_t lastAudioReadMs;
  uint32_t lastSerialLogMs;
};
