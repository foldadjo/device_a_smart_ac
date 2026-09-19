#include "SpeakerController.h"

#include "DebugConfig.h"

#include <esp_timer.h>
#include <math.h>

namespace
{
const uint32_t AUDIO_SAMPLE_RATE = 48000;
const uint16_t AUDIO_BIT_DEPTH = 16;
const uint16_t AUDIO_CHUNK_FRAMES = 256;
const uint8_t AUDIO_DMA_BUFFERS = 8;
const int16_t AUDIO_BASE_AMPLITUDE = 20000;
const i2s_port_t I2S_PORT = I2S_NUM_0;
const float AUDIO_TWO_PI = 6.28318530718f;
int16_t audioChunkBuffer[AUDIO_CHUNK_FRAMES * 2];

struct DemoMelodyNote
{
  float frequency;
  uint16_t durationMs;
  uint8_t volumePercent;
};

const DemoMelodyNote demoMelody[] = {
  { 392.00f, 375, 35 }, { 392.00f, 125, 35 },
  { 440.00f, 500, 35 }, { 392.00f, 500, 35 },
  { 523.25f, 500, 35 }, { 493.88f, 1000, 35 },
  { 392.00f, 375, 35 }, { 392.00f, 125, 35 },
  { 440.00f, 500, 35 }, { 392.00f, 500, 35 },
  { 587.33f, 500, 35 }, { 523.25f, 1000, 35 },
  { 392.00f, 375, 35 }, { 392.00f, 125, 35 },
  { 783.99f, 500, 35 }, { 659.25f, 500, 35 },
  { 523.25f, 500, 35 }, { 493.88f, 500, 35 },
  { 440.00f, 1000, 35 },
  { 698.46f, 375, 35 }, { 698.46f, 125, 35 },
  { 659.25f, 500, 35 }, { 523.25f, 500, 35 },
  { 587.33f, 500, 35 }, { 523.25f, 1000, 35 },
  { 0.00f, 250, 0 }
};

const uint8_t demoMelodyLength = sizeof(demoMelody) / sizeof(demoMelody[0]);

const DemoMelodyNote burungKakaTuaMelody[] = {
  { 523.25f, 360, 100 }, { 587.33f, 360, 100 },
  { 659.25f, 360, 100 }, { 523.25f, 360, 100 },
  { 523.25f, 360, 100 }, { 587.33f, 360, 100 },
  { 659.25f, 520, 100 },
  { 659.25f, 360, 100 }, { 698.46f, 360, 100 },
  { 783.99f, 360, 100 }, { 659.25f, 360, 100 },
  { 587.33f, 360, 100 }, { 523.25f, 620, 100 },
  { 783.99f, 360, 100 }, { 659.25f, 360, 100 },
  { 587.33f, 360, 100 }, { 523.25f, 520, 100 },
  { 587.33f, 360, 100 }, { 659.25f, 360, 100 },
  { 523.25f, 700, 100 },
  { 0.00f, 250, 0 }
};

const uint8_t burungKakaTuaMelodyLength = sizeof(burungKakaTuaMelody) / sizeof(burungKakaTuaMelody[0]);
}

SpeakerController::SpeakerController(uint8_t bclkPin, uint8_t lrcPin, uint8_t doutPin, StatusLed &led) :
  bclk(bclkPin),
  lrc(lrcPin),
  dout(doutPin),
  led(led),
  ready(false),
  i2sActive(false),
  i2sStatus("Not initialized"),
  i2sErrorDetail("-"),
  i2sWriteErrors(0),
  i2sShortWrites(0),
  lastI2SErrorLogMs(0),
  i2sDmaErrors(0),
  audioFeedGaps(0),
  lastAudioWriteUs(0),
  i2sEventQueue(nullptr),
  commandQueue(nullptr),
  stateMutex(nullptr),
  taskHandle(nullptr),
  lastOutputSample(0),
  pendingAudioFrames(0),
  lastSoundMessage("Speaker siap."),
  pendingAnnouncementMessage(""),
  testVolumePercent(70),
  activeTest(SPEAKER_TEST_NONE),
  fullDiagnosticStep(SPEAKER_TEST_NONE),
  testRunning(false),
  fullDiagnosticRunning(false),
  stopRequested(false),
  sampleIndex(0),
  totalSamples(0),
  phase(0.0f),
  currentFreq(0.0f),
  volumePercent(0),
  currentTestName("Idle")
{
}

void SpeakerController::begin()
{
  stateMutex = xSemaphoreCreateMutex();
  commandQueue = xQueueCreate(1, sizeof(SpeakerTestKind));
  if (stateMutex && commandQueue)
  {
    i2sStatus = "Ready";
    i2sErrorDetail = "-";
    lastSoundMessage = "Speaker siap.";
    startTask();
    return;
  }

  i2sStatus = "Failed";
  i2sErrorDetail = "Audio task synchronization allocation failed";
#if ENABLE_AMPLIFIER_LOGS
  Serial.println(i2sErrorDetail);
#endif
}

// Membuat task audio lokal untuk test MAX98357A tanpa bergantung ke TTS cloud.
bool SpeakerController::startTask()
{
  if (!stateMutex || !commandQueue)
  {
    return false;
  }

  if (xTaskCreatePinnedToCore(taskEntry, "speaker-audio", 6144, this, 2, &taskHandle,
                              ARDUINO_RUNNING_CORE) == pdPASS)
  {
    return true;
  }

  taskHandle = nullptr;
  ready = false;
  i2sStatus = "Failed";
  i2sErrorDetail = "Audio task creation failed";
  if (i2sEventQueue) i2s_driver_uninstall(I2S_PORT);
  i2sEventQueue = nullptr;
#if ENABLE_AMPLIFIER_LOGS
  Serial.println(i2sErrorDetail);
#endif
  return false;
}

// Mengirim perintah test speaker ke queue task audio.
bool SpeakerController::enqueue(SpeakerTestKind command)
{
  if (!taskHandle)
  {
    return false;
  }

  xQueueOverwrite(commandQueue, &command);
  return true;
}

bool SpeakerController::enqueueByName(const String &name)
{
  SpeakerTestKind command = SPEAKER_TEST_NONE;

  if (name == "stop") command = SPEAKER_TEST_NONE;
  else if (name == "fullDiagnostic") command = SPEAKER_TEST_FULL_DIAGNOSTIC;
  else if (name == "beep440" || name == "tone440") command = SPEAKER_TEST_440HZ;
  else if (name == "beep1000" || name == "testSpeaker" || name == "tone1000") command = SPEAKER_TEST_1KHZ;
  else if (name == "tone2000") command = SPEAKER_TEST_2KHZ;
  else if (name == "sweep" || name == "sweepFull") command = SPEAKER_TEST_SWEEP;
  else if (name == "volume25") command = SPEAKER_TEST_VOLUME_25;
  else if (name == "volume50") command = SPEAKER_TEST_VOLUME_50;
  else if (name == "volume75") command = SPEAKER_TEST_VOLUME_75;
  else if (name == "volume100") command = SPEAKER_TEST_VOLUME_100;
  else if (name == "beep3x") command = SPEAKER_TEST_BEEP_3X;
  else if (name == "mute") command = SPEAKER_TEST_MUTE;
  else if (name == "pcm") command = SPEAKER_TEST_PCM_SAMPLE;
  else if (name == "musicDemo") command = SPEAKER_TEST_MUSIC_DEMO;
  else if (name == "burungKakaTua") command = SPEAKER_TEST_BURUNG_KAKA_TUA;
  else return false;

  return enqueue(command);
}

bool SpeakerController::announceAcOn()
{
  pendingAnnouncementMessage = "AC berhasil dinyalakan.";
  return enqueue(SPEAKER_NOTIFY_AC_ON);
}

bool SpeakerController::announceAcOff()
{
  pendingAnnouncementMessage = "AC berhasil dimatikan.";
  return enqueue(SPEAKER_NOTIFY_AC_OFF);
}

bool SpeakerController::announceTemperatureSet(uint8_t temperature)
{
  pendingAnnouncementMessage = "Suhu berhasil diset " + String(temperature) + " derajat.";
  return enqueue(SPEAKER_NOTIFY_TEMP_SET);
}

bool SpeakerController::announceProtocolSet(const String &protocol)
{
  pendingAnnouncementMessage = "Protocol AC berhasil diset ke " + protocol + ".";
  return enqueue(SPEAKER_NOTIFY_PROTOCOL_SET);
}

bool SpeakerController::announceWifiSaved()
{
  pendingAnnouncementMessage = "WiFi berhasil disimpan.";
  return enqueue(SPEAKER_NOTIFY_WIFI_SAVED);
}

void SpeakerController::setVolume(uint8_t volume)
{
  testVolumePercent = volume > 100 ? 100 : volume;
}

uint8_t SpeakerController::bclkPin() const { return bclk; }
uint8_t SpeakerController::lrcPin() const { return lrc; }
uint8_t SpeakerController::doutPin() const { return dout; }
bool SpeakerController::isReady() const { return ready; }
bool SpeakerController::isRunning() const { return testRunning; }
const String &SpeakerController::status() const { return i2sStatus; }
const String &SpeakerController::error() const { return i2sErrorDetail; }
const String &SpeakerController::message() const { return lastSoundMessage; }
const String &SpeakerController::currentTest() const { return currentTestName; }
uint32_t SpeakerController::currentFrequency() const { return (uint32_t)currentFreq; }
uint8_t SpeakerController::currentVolume() const { return volumePercent; }
uint32_t SpeakerController::writeErrors() const { return i2sWriteErrors; }
uint32_t SpeakerController::shortWrites() const { return i2sShortWrites; }
uint32_t SpeakerController::dmaErrors() const { return i2sDmaErrors; }
uint32_t SpeakerController::feedGaps() const { return audioFeedGaps; }

void SpeakerController::taskEntry(void *param)
{
  static_cast<SpeakerController *>(param)->runTask();
}

void SpeakerController::setupI2S()
{
  if (ready) return;

#if ENABLE_AMPLIFIER_LOGS
  Serial.println("I2S init start");
#endif
  i2sStatus = "Initializing";
  i2sErrorDetail = "-";
  i2sWriteErrors = 0;
  i2sShortWrites = 0;
  lastI2SErrorLogMs = 0;
  pendingAudioFrames = 0;

  i2s_config_t i2sConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = AUDIO_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = AUDIO_DMA_BUFFERS,
    .dma_buf_len = AUDIO_CHUNK_FRAMES,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pinConfig = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = bclk,
    .ws_io_num = lrc,
    .data_out_num = dout,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  esp_err_t driverResult = i2s_driver_install(I2S_PORT, &i2sConfig, 16, &i2sEventQueue);
  if (driverResult != ESP_OK)
  {
    i2sStatus = "Failed";
    i2sErrorDetail = "driver_install error=" + String((int)driverResult);
    lastSoundMessage = "I2S init gagal di driver_install.";
    ready = false;
#if ENABLE_AMPLIFIER_LOGS
    Serial.println("I2S init failed: " + i2sErrorDetail);
#endif
    return;
  }

  esp_err_t pinResult = i2s_set_pin(I2S_PORT, &pinConfig);
  if (pinResult != ESP_OK)
  {
    i2sStatus = "Failed";
    i2sErrorDetail = "set_pin error=" + String((int)pinResult);
    lastSoundMessage = "I2S init gagal di set_pin.";
    ready = false;
    i2s_driver_uninstall(I2S_PORT);
    i2sEventQueue = nullptr;
#if ENABLE_AMPLIFIER_LOGS
    Serial.println("I2S init failed: " + i2sErrorDetail);
#endif
    return;
  }

  esp_err_t clearResult = i2s_zero_dma_buffer(I2S_PORT);
  if (clearResult != ESP_OK)
  {
    i2sStatus = "Failed";
    i2sErrorDetail = String("zero DMA: ") + esp_err_to_name(clearResult);
#if ENABLE_AMPLIFIER_LOGS
    Serial.println("I2S INIT FAILED: " + i2sErrorDetail);
#endif
    i2s_driver_uninstall(I2S_PORT);
    i2sEventQueue = nullptr;
    ready = false;
    return;
  }

  lastAudioWriteUs = 0;
  ready = true;
  i2sActive = true;
  i2sStatus = "Initialized";
  i2sErrorDetail = "-";
  lastSoundMessage = "I2S speaker siap.";
#if ENABLE_AMPLIFIER_LOGS
  Serial.println("I2S INIT OK");
#endif
}

// Melepas driver I2S speaker saat idle agar tidak mengunci peripheral.
void SpeakerController::powerDownI2S(const char *statusText)
{
  if (ready && i2sEventQueue)
  {
    i2s_zero_dma_buffer(I2S_PORT);
    i2s_stop(I2S_PORT);
    i2s_driver_uninstall(I2S_PORT);
  }

  ready = false;
  i2sActive = false;
  i2sEventQueue = nullptr;
  lastAudioWriteUs = 0;
  lastOutputSample = 0;
  pendingAudioFrames = 0;
  i2sStatus = statusText;
  i2sErrorDetail = "-";
}

void SpeakerController::logWriteIssue(const char *message, esp_err_t errorCode, size_t bytesWritten, size_t expectedBytes)
{
#if ENABLE_AMPLIFIER_LOGS
  uint32_t now = millis();
  if (now - lastI2SErrorLogMs < 500) return;

  lastI2SErrorLogMs = now;
  Serial.print("I2S WRITE WARNING: ");
  Serial.print(message);
  Serial.print(" err=");
  Serial.print((int)errorCode);
  Serial.print(" bytes=");
  Serial.print(bytesWritten);
  Serial.print("/");
  Serial.println(expectedBytes);
#else
  (void)message;
  (void)errorCode;
  (void)bytesWritten;
  (void)expectedBytes;
#endif
}

// Menulis satu chunk PCM stereo 16-bit ke amplifier MAX98357A.
bool SpeakerController::writeStereoChunk(const int16_t *stereoSamples, uint16_t frames)
{
  if (!ready || frames == 0) return false;
  if (!i2sActive)
  {
    esp_err_t startResult = i2s_start(I2S_PORT);
    if (startResult != ESP_OK)
    {
      i2sWriteErrors++;
      i2sStatus = "Failed";
      i2sErrorDetail = String("i2s_start: ") + esp_err_to_name(startResult);
      lastSoundMessage = i2sErrorDetail;
#if ENABLE_AMPLIFIER_LOGS
      Serial.println("I2S ERROR: " + i2sErrorDetail);
#endif
      ready = false;
      return false;
    }
    i2sActive = true;
    i2sStatus = "Active";
  }

  const int64_t nowUs = esp_timer_get_time();
  const int64_t dmaDurationUs = 1000000LL * AUDIO_DMA_BUFFERS * AUDIO_CHUNK_FRAMES / AUDIO_SAMPLE_RATE;
  if (testRunning && lastAudioWriteUs != 0 && nowUs - lastAudioWriteUs > dmaDurationUs)
  {
    audioFeedGaps++;
    logWriteIssue("audio feed late; possible underrun", ESP_OK, 0, frames * 4);
  }

  const size_t expectedBytes = frames * 2 * sizeof(int16_t);
  size_t offset = 0;
  while (offset < expectedBytes)
  {
    size_t bytesWritten = 0;
    const size_t remaining = expectedBytes - offset;
    esp_err_t result = i2s_write(I2S_PORT,
      reinterpret_cast<const uint8_t *>(stereoSamples) + offset,
      remaining, &bytesWritten, pdMS_TO_TICKS(50));
    if (bytesWritten != remaining) i2sShortWrites++;
    offset += bytesWritten;

    if (result != ESP_OK || bytesWritten == 0 || bytesWritten % 4 != 0)
    {
      i2sWriteErrors++;
      i2sErrorDetail = String("i2s_write: ") + esp_err_to_name(result) +
        " bytes=" + String(offset) + "/" + String(expectedBytes);
#if ENABLE_AMPLIFIER_LOGS
      Serial.println("I2S ERROR: " + i2sErrorDetail);
#endif
      ready = false;
      i2sActive = false;
      i2sStatus = "Failed";
      testRunning = false;
      fullDiagnosticRunning = false;
      currentTestName = "I2S Failed";
      currentFreq = 0;
      volumePercent = 0;
      lastSoundMessage = i2sErrorDetail;
      i2s_driver_uninstall(I2S_PORT);
      i2sEventQueue = nullptr;
      return false;
    }
  }

  lastAudioWriteUs = esp_timer_get_time();
  lastOutputSample = stereoSamples[(frames - 1) * 2];
  return true;
}

void SpeakerController::fillStereoChunk(int16_t sample, uint16_t frames)
{
  for (uint16_t i = 0; i < frames; i++)
  {
    audioChunkBuffer[i * 2] = sample;
    audioChunkBuffer[i * 2 + 1] = sample;
  }
}

void SpeakerController::flushAudioChunk()
{
  if (pendingAudioFrames == 0) return;
  writeStereoChunk(audioChunkBuffer, pendingAudioFrames);
  pendingAudioFrames = 0;
}

const char *SpeakerController::testName(SpeakerTestKind kind) const
{
  switch (kind)
  {
    case SPEAKER_TEST_440HZ: return "440 Hz";
    case SPEAKER_TEST_1KHZ: return "1 kHz";
    case SPEAKER_TEST_2KHZ: return "2 kHz";
    case SPEAKER_TEST_SWEEP: return "Sweep 200 Hz - 5 kHz";
    case SPEAKER_TEST_VOLUME_25: return "Volume 25%";
    case SPEAKER_TEST_VOLUME_50: return "Volume 50%";
    case SPEAKER_TEST_VOLUME_75: return "Volume 75%";
    case SPEAKER_TEST_VOLUME_100: return "Volume 100%";
    case SPEAKER_TEST_BEEP_3X: return "Beep pendek 3x";
    case SPEAKER_TEST_MUTE: return "Mute";
    case SPEAKER_TEST_PCM_SAMPLE: return "Simple PCM sample";
    case SPEAKER_TEST_MUSIC_DEMO: return "Selamat Ulang Tahun";
    case SPEAKER_TEST_BURUNG_KAKA_TUA: return "Burung Kaka Tua";
    case SPEAKER_TEST_FULL_DIAGNOSTIC: return "Full Speaker Diagnostic";
    case SPEAKER_NOTIFY_AC_ON: return "AC berhasil dinyalakan";
    case SPEAKER_NOTIFY_AC_OFF: return "AC berhasil dimatikan";
    case SPEAKER_NOTIFY_TEMP_SET: return "Suhu berhasil diset";
    case SPEAKER_NOTIFY_PROTOCOL_SET: return "Protocol AC berhasil diset";
    case SPEAKER_NOTIFY_WIFI_SAVED: return "WiFi berhasil disimpan";
    default: return "Idle";
  }
}

uint8_t SpeakerController::defaultVolumeForTest(SpeakerTestKind kind) const
{
  if (kind == SPEAKER_TEST_VOLUME_25) return 25;
  if (kind == SPEAKER_TEST_VOLUME_50) return 50;
  if (kind == SPEAKER_TEST_VOLUME_75) return 75;
  if (kind == SPEAKER_TEST_VOLUME_100) return 100;
  if (kind == SPEAKER_TEST_MUSIC_DEMO || kind == SPEAKER_TEST_BURUNG_KAKA_TUA) return testVolumePercent;
  if (kind == SPEAKER_TEST_BEEP_3X || kind == SPEAKER_TEST_PCM_SAMPLE) return testVolumePercent;
  if (kind == SPEAKER_TEST_MUTE) return 0;
  if (isNotification(kind)) return testVolumePercent;
  return testVolumePercent;
}

bool SpeakerController::isNotification(SpeakerTestKind kind) const
{
  return kind == SPEAKER_NOTIFY_AC_ON ||
    kind == SPEAKER_NOTIFY_AC_OFF ||
    kind == SPEAKER_NOTIFY_TEMP_SET ||
    kind == SPEAKER_NOTIFY_PROTOCOL_SET ||
    kind == SPEAKER_NOTIFY_WIFI_SAVED;
}

uint32_t SpeakerController::demoMelodyDurationMs() const
{
  uint32_t durationMs = 0;
  for (uint8_t i = 0; i < demoMelodyLength; i++) durationMs += demoMelody[i].durationMs;
  return durationMs;
}

uint32_t SpeakerController::burungKakaTuaDurationMs() const
{
  uint32_t durationMs = 0;
  for (uint8_t i = 0; i < burungKakaTuaMelodyLength; i++) durationMs += burungKakaTuaMelody[i].durationMs;
  return durationMs;
}

uint32_t SpeakerController::durationMsForTest(SpeakerTestKind kind) const
{
  if (kind == SPEAKER_TEST_SWEEP) return 4500;
  if (kind == SPEAKER_TEST_BEEP_3X) return 900;
  if (kind == SPEAKER_TEST_MUTE) return 1200;
  if (kind == SPEAKER_TEST_PCM_SAMPLE) return 2200;
  if (kind == SPEAKER_TEST_MUSIC_DEMO) return demoMelodyDurationMs();
  if (kind == SPEAKER_TEST_BURUNG_KAKA_TUA) return burungKakaTuaDurationMs();
  if (kind == SPEAKER_NOTIFY_AC_ON || kind == SPEAKER_NOTIFY_AC_OFF) return 850;
  if (kind == SPEAKER_NOTIFY_TEMP_SET) return 1100;
  if (kind == SPEAKER_NOTIFY_PROTOCOL_SET || kind == SPEAKER_NOTIFY_WIFI_SAVED) return 750;
  return 2000;
}

void SpeakerController::logStart(SpeakerTestKind kind) const
{
#if ENABLE_AMPLIFIER_LOGS
  Serial.print("SPEAKER START: ");
  Serial.println(testName(kind));
#else
  (void)kind;
#endif
}

void SpeakerController::logDone(SpeakerTestKind kind) const
{
#if ENABLE_AMPLIFIER_LOGS
  Serial.print("SPEAKER DONE: ");
  Serial.println(testName(kind));
#else
  (void)kind;
#endif
}

// Siapkan state awal untuk satu test speaker lokal.
void SpeakerController::startTest(SpeakerTestKind kind, bool partOfFullDiagnostic)
{
  if (!ready)
  {
    setupI2S();
    if (!ready)
    {
      lastSoundMessage = "Speaker test gagal: " + i2sErrorDetail;
      currentTestName = "I2S Failed";
      testRunning = false;
      fullDiagnosticRunning = false;
#if ENABLE_AMPLIFIER_LOGS
      Serial.println(lastSoundMessage);
#endif
      return;
    }
  }
  else if (!i2sActive)
  {
    i2s_pin_config_t pinConfig = {
      .mck_io_num = I2S_PIN_NO_CHANGE,
      .bck_io_num = bclk,
      .ws_io_num = lrc,
      .data_out_num = dout,
      .data_in_num = I2S_PIN_NO_CHANGE
    };
    i2s_set_pin(I2S_PORT, &pinConfig);
  }

  stopRequested = false;
  testRunning = true;
  activeTest = kind;
  sampleIndex = 0;
  totalSamples = AUDIO_SAMPLE_RATE * durationMsForTest(kind) / 1000;
  phase = 0.0f;
  pendingAudioFrames = 0;
  volumePercent = defaultVolumeForTest(kind);
  currentFreq = 0.0f;
  currentTestName = testName(kind);
  lastSoundMessage = "Menjalankan " + currentTestName + ".";
  if (isNotification(kind) && pendingAnnouncementMessage.length() > 0)
  {
    lastSoundMessage = pendingAnnouncementMessage;
    pendingAnnouncementMessage = "";
  }

  if (!partOfFullDiagnostic)
  {
    fullDiagnosticRunning = false;
    fullDiagnosticStep = SPEAKER_TEST_NONE;
  }

  led.showSpeakerLoud();
  logStart(kind);
}

float SpeakerController::sampleEnvelope(uint32_t position, uint32_t length) const
{
  if (length < 2 || position >= length) return 0.0f;
  const uint32_t fadeSamples = min(AUDIO_SAMPLE_RATE / 100, length / 2);
  const uint32_t edge = min(position, length - 1 - position);
  return edge < fadeSamples ? (float)edge / fadeSamples : 1.0f;
}

int16_t SpeakerController::nextSample()
{
  float frequency = 1000.0f;
  float amplitude = AUDIO_BASE_AMPLITUDE * volumePercent / 100.0f;
  float noteEnvelope = 1.0f;
  bool mute = false;

  if (activeTest == SPEAKER_TEST_440HZ) frequency = 440.0f;
  else if (activeTest == SPEAKER_TEST_2KHZ) frequency = 2000.0f;
  else if (activeTest == SPEAKER_TEST_SWEEP)
  {
    float progress = totalSamples > 0 ? (float)sampleIndex / totalSamples : 0.0f;
    frequency = 200.0f + (5000.0f - 200.0f) * progress;
  }
  else if (activeTest == SPEAKER_TEST_BEEP_3X)
  {
    uint32_t sampleInCycle = sampleIndex % (AUDIO_SAMPLE_RATE * 300 / 1000);
    uint32_t beepSamples = AUDIO_SAMPLE_RATE * 120 / 1000;
    mute = sampleInCycle >= beepSamples;
    noteEnvelope = sampleEnvelope(sampleInCycle, beepSamples);
  }
  else if (activeTest == SPEAKER_TEST_MUTE) mute = true;
  else if (activeTest == SPEAKER_TEST_PCM_SAMPLE)
  {
    uint32_t segmentSamples = AUDIO_SAMPLE_RATE * 350 / 1000;
    uint8_t segment = (sampleIndex / segmentSamples) % 6;
    const float notes[] = { 523.25f, 659.25f, 783.99f, 659.25f, 523.25f, 392.00f };
    frequency = notes[segment];
    noteEnvelope = sampleEnvelope(sampleIndex % segmentSamples, segmentSamples);
  }
  else if (activeTest == SPEAKER_TEST_MUSIC_DEMO || activeTest == SPEAKER_TEST_BURUNG_KAKA_TUA)
  {
    const DemoMelodyNote *melody = activeTest == SPEAKER_TEST_MUSIC_DEMO ? demoMelody : burungKakaTuaMelody;
    const uint8_t melodyLength = activeTest == SPEAKER_TEST_MUSIC_DEMO ? demoMelodyLength : burungKakaTuaMelodyLength;
    uint32_t noteStartSample = 0;
    bool noteFound = false;

    for (uint8_t i = 0; i < melodyLength; i++)
    {
      uint32_t noteSamples = AUDIO_SAMPLE_RATE * melody[i].durationMs / 1000;
      if (sampleIndex < noteStartSample + noteSamples)
      {
        uint32_t sampleInNote = sampleIndex - noteStartSample;
        frequency = melody[i].frequency;
        volumePercent = testVolumePercent;
        amplitude = AUDIO_BASE_AMPLITUDE * volumePercent / 100.0f;
        mute = frequency <= 1.0f;
        const uint32_t soundingSamples = noteSamples - AUDIO_SAMPLE_RATE * 20 / 1000;
        noteEnvelope = sampleEnvelope(sampleInNote, soundingSamples);
        noteFound = true;
        break;
      }
      noteStartSample += noteSamples;
    }

    if (!noteFound)
    {
      mute = true;
      volumePercent = 0;
    }
  }
  else if (isNotification(activeTest))
  {
    const uint32_t segmentSamples = AUDIO_SAMPLE_RATE * 170 / 1000;
    uint8_t segment = sampleIndex / segmentSamples;
    uint32_t sampleInSegment = sampleIndex % segmentSamples;

    if (activeTest == SPEAKER_NOTIFY_AC_ON)
    {
      const float notes[] = { 659.25f, 783.99f, 1046.50f, 0.0f, 1046.50f };
      frequency = notes[segment > 4 ? 4 : segment];
    }
    else if (activeTest == SPEAKER_NOTIFY_AC_OFF)
    {
      const float notes[] = { 1046.50f, 783.99f, 523.25f, 0.0f, 392.00f };
      frequency = notes[segment > 4 ? 4 : segment];
    }
    else if (activeTest == SPEAKER_NOTIFY_TEMP_SET)
    {
      const float notes[] = { 523.25f, 659.25f, 783.99f, 659.25f, 880.00f, 0.0f };
      frequency = notes[segment > 5 ? 5 : segment];
    }
    else if (activeTest == SPEAKER_NOTIFY_PROTOCOL_SET)
    {
      const float notes[] = { 587.33f, 0.0f, 880.00f, 0.0f, 1174.66f };
      frequency = notes[segment > 4 ? 4 : segment];
    }
    else
    {
      const float notes[] = { 659.25f, 0.0f, 987.77f, 0.0f, 1318.51f };
      frequency = notes[segment > 4 ? 4 : segment];
    }

    mute = frequency <= 1.0f;
    noteEnvelope = sampleEnvelope(sampleInSegment, segmentSamples);
  }

  currentFreq = mute ? 0.0f : frequency;
  if (mute) return 0;

  float envelope = sampleEnvelope(sampleIndex, totalSamples);
  phase += AUDIO_TWO_PI * frequency / AUDIO_SAMPLE_RATE;
  if (phase > AUDIO_TWO_PI) phase -= AUDIO_TWO_PI;
  return (int16_t)(sinf(phase) * amplitude * envelope * noteEnvelope);
}

SpeakerTestKind SpeakerController::nextFullDiagnosticStep(SpeakerTestKind current) const
{
  switch (current)
  {
    case SPEAKER_TEST_NONE: return SPEAKER_TEST_440HZ;
    case SPEAKER_TEST_440HZ: return SPEAKER_TEST_1KHZ;
    case SPEAKER_TEST_1KHZ: return SPEAKER_TEST_2KHZ;
    case SPEAKER_TEST_2KHZ: return SPEAKER_TEST_SWEEP;
    case SPEAKER_TEST_SWEEP: return SPEAKER_TEST_VOLUME_25;
    case SPEAKER_TEST_VOLUME_25: return SPEAKER_TEST_VOLUME_50;
    case SPEAKER_TEST_VOLUME_50: return SPEAKER_TEST_VOLUME_75;
    case SPEAKER_TEST_VOLUME_75: return SPEAKER_TEST_VOLUME_100;
    case SPEAKER_TEST_VOLUME_100: return SPEAKER_TEST_BEEP_3X;
    case SPEAKER_TEST_BEEP_3X: return SPEAKER_TEST_MUTE;
    default: return SPEAKER_TEST_NONE;
  }
}

void SpeakerController::stopTest(const char *reason)
{
  const int16_t startSample = lastOutputSample;
  for (uint16_t i = 0; i < AUDIO_CHUNK_FRAMES; i++)
  {
    const int16_t sample = (int32_t)startSample * (AUDIO_CHUNK_FRAMES - 1 - i) / (AUDIO_CHUNK_FRAMES - 1);
    audioChunkBuffer[i * 2] = sample;
    audioChunkBuffer[i * 2 + 1] = sample;
  }
  if (!writeStereoChunk(audioChunkBuffer, AUDIO_CHUNK_FRAMES)) return;

  testRunning = false;
  fullDiagnosticRunning = false;
  stopRequested = false;
  activeTest = SPEAKER_TEST_NONE;
  fullDiagnosticStep = SPEAKER_TEST_NONE;
  currentFreq = 0.0f;
  volumePercent = 0;
  currentTestName = "Idle";
  lastSoundMessage = reason;
  powerDownI2S("Ready");
  led.showSpeakerIdle();
#if ENABLE_AMPLIFIER_LOGS
  Serial.println(reason);
#endif
}

void SpeakerController::startFullDiagnostic()
{
#if ENABLE_AMPLIFIER_LOGS
  Serial.println("FULL SPEAKER DIAGNOSTIC START");
#endif
  if (!ready)
  {
    setupI2S();
    if (!ready)
    {
      currentTestName = "I2S Failed";
      lastSoundMessage = "Full diagnostic gagal: " + i2sErrorDetail;
      return;
    }
  }

  fullDiagnosticRunning = true;
  fullDiagnosticStep = SPEAKER_TEST_NONE;
  startTest(nextFullDiagnosticStep(fullDiagnosticStep), true);
}

void SpeakerController::serviceTest()
{
  if (!testRunning) return;

  if (stopRequested)
  {
    flushAudioChunk();
    stopTest("SPEAKER TEST STOPPED");
    return;
  }

  uint16_t framesToWrite = AUDIO_CHUNK_FRAMES;
  uint16_t framesPrepared = 0;
  uint32_t remainingSamples = totalSamples - sampleIndex;
  if (remainingSamples < framesToWrite) framesToWrite = remainingSamples;

  while (framesPrepared < framesToWrite)
  {
    int16_t sample = nextSample();
    audioChunkBuffer[framesPrepared * 2] = sample;
    audioChunkBuffer[framesPrepared * 2 + 1] = sample;
    framesPrepared++;
    sampleIndex++;
  }

  if (framesPrepared > 0 && !writeStereoChunk(audioChunkBuffer, framesPrepared)) return;
  if (sampleIndex < totalSamples) return;

  SpeakerTestKind finishedTest = activeTest;
  logDone(finishedTest);

  if (fullDiagnosticRunning)
  {
    fullDiagnosticStep = finishedTest;
    SpeakerTestKind nextStep = nextFullDiagnosticStep(fullDiagnosticStep);
    if (nextStep != SPEAKER_TEST_NONE)
    {
      startTest(nextStep, true);
      return;
    }

    fullDiagnosticRunning = false;
    lastSoundMessage = "Full Speaker Diagnostic selesai.";
  }
  else
  {
    lastSoundMessage = String(testName(finishedTest)) + " selesai.";
  }

  testRunning = false;
  activeTest = SPEAKER_TEST_NONE;
  currentFreq = 0.0f;
  volumePercent = 0;
  currentTestName = "Idle";
  powerDownI2S("Ready");
  led.showSpeakerIdle();
}

void SpeakerController::runTask()
{
  for (;;)
  {
    xSemaphoreTake(stateMutex, portMAX_DELAY);

    SpeakerTestKind command;
    if (xQueueReceive(commandQueue, &command, 0) == pdTRUE)
    {
      if (testRunning) stopTest("SPEAKER TEST STOPPED");
      if (command == SPEAKER_TEST_NONE)
      {
        powerDownI2S("Ready");
        lastSoundMessage = "Speaker siap.";
        currentTestName = "Idle";
        led.showSpeakerIdle();
      }
      else if (command == SPEAKER_TEST_FULL_DIAGNOSTIC) startFullDiagnostic();
      else if (command != SPEAKER_TEST_NONE) startTest(command);
    }

    i2s_event_t event;
    while (i2sEventQueue && xQueueReceive(i2sEventQueue, &event, 0) == pdTRUE)
    {
      if (event.type == I2S_EVENT_DMA_ERROR)
      {
        i2sDmaErrors++;
        i2sErrorDetail = "I2S DMA ERROR; playback stopped";
#if ENABLE_AMPLIFIER_LOGS
        Serial.println(i2sErrorDetail);
#endif
        ready = false;
        i2sActive = false;
        i2sStatus = "Failed";
        testRunning = false;
        fullDiagnosticRunning = false;
        currentFreq = 0;
        volumePercent = 0;
        currentTestName = "I2S Failed";
        lastSoundMessage = i2sErrorDetail;
        i2s_driver_uninstall(I2S_PORT);
        i2sEventQueue = nullptr;
      }
    }

    if (testRunning)
    {
      serviceTest();
    }
    else if (ready)
    {
      powerDownI2S("Ready");
    }

    const bool stillReady = ready;
    xSemaphoreGive(stateMutex);
    if (!stillReady) vTaskDelay(pdMS_TO_TICKS(20));
    else taskYIELD();
  }
}
