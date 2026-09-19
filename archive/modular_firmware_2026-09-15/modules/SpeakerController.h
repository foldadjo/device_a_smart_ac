#pragma once

#include <Arduino.h>
#include <driver/i2s.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "StatusLed.h"

enum SpeakerTestKind
{
  SPEAKER_TEST_NONE,
  SPEAKER_TEST_440HZ,
  SPEAKER_TEST_1KHZ,
  SPEAKER_TEST_2KHZ,
  SPEAKER_TEST_SWEEP,
  SPEAKER_TEST_VOLUME_25,
  SPEAKER_TEST_VOLUME_50,
  SPEAKER_TEST_VOLUME_75,
  SPEAKER_TEST_VOLUME_100,
  SPEAKER_TEST_BEEP_3X,
  SPEAKER_TEST_MUTE,
  SPEAKER_TEST_PCM_SAMPLE,
  SPEAKER_TEST_MUSIC_DEMO,
  SPEAKER_TEST_BURUNG_KAKA_TUA,
  SPEAKER_TEST_FULL_DIAGNOSTIC,
  SPEAKER_NOTIFY_AC_ON,
  SPEAKER_NOTIFY_AC_OFF,
  SPEAKER_NOTIFY_TEMP_SET,
  SPEAKER_NOTIFY_PROTOCOL_SET,
  SPEAKER_NOTIFY_WIFI_SAVED
};

class SpeakerController
{
public:
  SpeakerController(uint8_t bclkPin, uint8_t lrcPin, uint8_t doutPin, StatusLed &led);

  void begin();
  bool startTask();
  bool enqueue(SpeakerTestKind command);
  bool enqueueByName(const String &name);
  bool announceAcOn();
  bool announceAcOff();
  bool announceTemperatureSet(uint8_t temperature);
  bool announceProtocolSet(const String &protocol);
  bool announceWifiSaved();
  void setVolume(uint8_t volume);

  uint8_t bclkPin() const;
  uint8_t lrcPin() const;
  uint8_t doutPin() const;
  bool isReady() const;
  bool isRunning() const;
  const String &status() const;
  const String &error() const;
  const String &message() const;
  const String &currentTest() const;
  uint32_t currentFrequency() const;
  uint8_t currentVolume() const;
  uint32_t writeErrors() const;
  uint32_t shortWrites() const;
  uint32_t dmaErrors() const;
  uint32_t feedGaps() const;

private:
  static void taskEntry(void *param);

  void setupI2S();
  void powerDownI2S(const char *statusText);
  void logWriteIssue(const char *message, esp_err_t errorCode, size_t bytesWritten, size_t expectedBytes);
  bool writeStereoChunk(const int16_t *stereoSamples, uint16_t frames);
  void fillStereoChunk(int16_t sample, uint16_t frames);
  void flushAudioChunk();
  const char *testName(SpeakerTestKind kind) const;
  uint8_t defaultVolumeForTest(SpeakerTestKind kind) const;
  bool isNotification(SpeakerTestKind kind) const;
  uint32_t demoMelodyDurationMs() const;
  uint32_t burungKakaTuaDurationMs() const;
  uint32_t durationMsForTest(SpeakerTestKind kind) const;
  void logStart(SpeakerTestKind kind) const;
  void logDone(SpeakerTestKind kind) const;
  void startTest(SpeakerTestKind kind, bool partOfFullDiagnostic = false);
  float sampleEnvelope(uint32_t position, uint32_t length) const;
  int16_t nextSample();
  SpeakerTestKind nextFullDiagnosticStep(SpeakerTestKind current) const;
  void stopTest(const char *reason);
  void startFullDiagnostic();
  void serviceTest();
  void runTask();

  uint8_t bclk;
  uint8_t lrc;
  uint8_t dout;
  StatusLed &led;
  bool ready;
  bool i2sActive;
  String i2sStatus;
  String i2sErrorDetail;
  uint32_t i2sWriteErrors;
  uint32_t i2sShortWrites;
  uint32_t lastI2SErrorLogMs;
  uint32_t i2sDmaErrors;
  uint32_t audioFeedGaps;
  int64_t lastAudioWriteUs;
  QueueHandle_t i2sEventQueue;
  QueueHandle_t commandQueue;
  SemaphoreHandle_t stateMutex;
  TaskHandle_t taskHandle;
  int16_t lastOutputSample;
  uint16_t pendingAudioFrames;
  String lastSoundMessage;
  String pendingAnnouncementMessage;
  uint8_t testVolumePercent;
  SpeakerTestKind activeTest;
  SpeakerTestKind fullDiagnosticStep;
  bool testRunning;
  bool fullDiagnosticRunning;
  bool stopRequested;
  uint32_t sampleIndex;
  uint32_t totalSamples;
  float phase;
  float currentFreq;
  uint8_t volumePercent;
  String currentTestName;
};
