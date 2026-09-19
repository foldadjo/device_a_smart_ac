#include "MicrophoneMonitor.h"

#include <driver/i2s.h>
#include <esp_err.h>
#include <math.h>

#include "config.h"
#include "DebugConfig.h"

namespace
{
const size_t PCM_SAMPLES_PER_CHUNK = 128;
const size_t RAW_WORDS_PER_CHUNK = PCM_SAMPLES_PER_CHUNK * 2;
const uint32_t MIC_NO_SIGNAL_RMS = 4;
const i2s_port_t MIC_I2S_PORT = I2S_NUM_1;
}

MicrophoneMonitor::MicrophoneMonitor(uint8_t bclkPin, uint8_t wsPin, uint8_t dinPin, uint32_t sampleRate) :
  bclk(bclkPin),
  ws(wsPin),
  din(dinPin),
  rate(sampleRate),
  i2sInstalled(false),
  taskHandle(nullptr),
  ready(false),
  micStatus("Not initialized"),
  errorDetail("-"),
  levelRms(0),
  levelPeak(0),
  totalChunksRead(0),
  totalReadErrors(0),
  lastAudioReadMs(0),
  lastSerialLogMs(0)
{
}

void MicrophoneMonitor::begin()
{
  if (!initI2s())
  {
    return;
  }

  if (xTaskCreatePinnedToCore(taskEntry, "i2s-mic-rx", 4096, this, 1, &taskHandle,
                              ARDUINO_RUNNING_CORE) != pdPASS)
  {
    taskHandle = nullptr;
    setError("Task creation failed");
    return;
  }

  ready = true;
  micStatus = "MIC OK";
#if ENABLE_MIC_LOGS
  Serial.println("I2S microphone monitor started");
#endif
}

// Reset counter dan level untuk uji microphone dari Web UI.
void MicrophoneMonitor::resetStats()
{
  levelRms = 0;
  levelPeak = 0;
  totalChunksRead = 0;
  totalReadErrors = 0;
  lastAudioReadMs = 0;
  errorDetail = "-";
  micStatus = ready ? "MIC OK" : micStatus;
}

bool MicrophoneMonitor::isReady() const { return ready; }
const String &MicrophoneMonitor::status() const { return micStatus; }
const String &MicrophoneMonitor::error() const { return errorDetail; }
uint8_t MicrophoneMonitor::bclkPin() const { return bclk; }
uint8_t MicrophoneMonitor::wsPin() const { return ws; }
uint8_t MicrophoneMonitor::dinPin() const { return din; }
uint32_t MicrophoneMonitor::sampleRate() const { return rate; }
uint32_t MicrophoneMonitor::rms() const { return levelRms; }
uint32_t MicrophoneMonitor::peak() const { return levelPeak; }
uint32_t MicrophoneMonitor::chunksRead() const { return totalChunksRead; }
uint32_t MicrophoneMonitor::readErrors() const { return totalReadErrors; }
uint32_t MicrophoneMonitor::lastReadMs() const { return lastAudioReadMs; }

void MicrophoneMonitor::taskEntry(void *param)
{
  static_cast<MicrophoneMonitor *>(param)->runTask();
}

bool MicrophoneMonitor::initI2s()
{
  i2s_config_t i2sConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = rate,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 4,
    .dma_buf_len = RAW_WORDS_PER_CHUNK,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pinConfig = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = bclk,
    .ws_io_num = ws,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = din
  };

  esp_err_t err = i2s_driver_install(MIC_I2S_PORT, &i2sConfig, 0, nullptr);
  if (err != ESP_OK)
  {
    setError("i2s_driver_install: " + String(esp_err_to_name(err)));
    return false;
  }
  i2sInstalled = true;

  err = i2s_set_pin(MIC_I2S_PORT, &pinConfig);
  if (err != ESP_OK)
  {
    setError("i2s_set_pin: " + String(esp_err_to_name(err)));
    return false;
  }

  err = i2s_zero_dma_buffer(MIC_I2S_PORT);
  if (err != ESP_OK)
  {
    setError("i2s_zero_dma_buffer: " + String(esp_err_to_name(err)));
    return false;
  }

#if ENABLE_MIC_LOGS
  Serial.print("I2S microphone RX legacy init OK. BCLK GPIO");
  Serial.print(bclk);
  Serial.print(" WS GPIO");
  Serial.print(ws);
  Serial.print(" DIN GPIO");
  Serial.print(din);
  Serial.print(" rate=");
  Serial.println(rate);
#endif
  return true;
}

// Task kecil yang terus membaca I2S mic dan menghitung level RMS.
void MicrophoneMonitor::runTask()
{
  int32_t rawSamples[RAW_WORDS_PER_CHUNK];

  for (;;)
  {
    size_t bytesRead = 0;
    esp_err_t err = i2s_read(MIC_I2S_PORT, rawSamples, sizeof(rawSamples), &bytesRead, pdMS_TO_TICKS(120));

    if (err == ESP_OK && bytesRead >= sizeof(int32_t))
    {
      updateAudioStats(rawSamples, bytesRead / sizeof(int32_t));
    }
    else if (err == ESP_ERR_TIMEOUT || bytesRead == 0)
    {
      levelRms = 0;
      levelPeak = 0;
      micStatus = "NO SIGNAL";
    }
    else
    {
      totalReadErrors = totalReadErrors + 1;
      setError("i2s read: " + String(esp_err_to_name(err)));
    }

#if ENABLE_MIC_LOGS
    uint32_t now = millis();
    if (now - lastSerialLogMs >= MIC_LOG_INTERVAL_MS)
    {
      lastSerialLogMs = now;
      Serial.print("MIC ");
      Serial.print(micStatus);
      Serial.print(" rms=");
      Serial.print(levelRms);
      Serial.print(" peak=");
      Serial.print(levelPeak);
      Serial.print(" chunks=");
      Serial.print(totalChunksRead);
      Serial.print(" errors=");
      Serial.println(totalReadErrors);
    }
#endif

    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

// Konversi chunk I2S 32-bit mono menjadi level PCM 16-bit untuk RMS/peak.
void MicrophoneMonitor::updateAudioStats(const int32_t *rawSamples, size_t rawWordCount)
{
  uint64_t sumSquares = 0;
  uint32_t peak = 0;
  uint32_t samples = 0;

  for (size_t i = 0; i < rawWordCount; i++)
  {
    int16_t pcm = (int16_t)(rawSamples[i] >> 16);
    int32_t value = pcm;
    uint32_t magnitude = value < 0 ? (uint32_t)(-value) : (uint32_t)value;

    sumSquares += (uint64_t)value * (uint64_t)value;
    if (magnitude > peak) peak = magnitude;
    samples++;
  }

  if (samples == 0)
  {
    micStatus = "NO SIGNAL";
    return;
  }

  levelRms = (uint32_t)sqrt((double)sumSquares / (double)samples);
  levelPeak = peak;
  totalChunksRead = totalChunksRead + 1;
  lastAudioReadMs = millis();
  errorDetail = "-";
  micStatus = levelRms > MIC_NO_SIGNAL_RMS ? "MIC OK" : "NO SIGNAL";
}

void MicrophoneMonitor::setError(const String &message)
{
  if (i2sInstalled)
  {
    i2s_driver_uninstall(MIC_I2S_PORT);
    i2sInstalled = false;
  }
  ready = false;
  micStatus = "ERROR";
  errorDetail = message;
#if ENABLE_MIC_LOGS
  Serial.print("MIC ERROR: ");
  Serial.println(message);
#endif
}
