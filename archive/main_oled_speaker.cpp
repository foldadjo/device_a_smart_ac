#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_SSD1306.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <math.h>

#include "config.h"

#define RGB_PIN 48

const uint8_t I2S_BCLK_PIN = PIN_I2S_BCLK;
const uint8_t I2S_LRC_PIN = PIN_I2S_LRC;
const uint8_t I2S_DOUT_PIN = PIN_I2S_DOUT;
const uint8_t OLED_SDA_PIN = PIN_OLED_SDA;
const uint8_t OLED_SCL_PIN = PIN_OLED_SCL;

const char *AP_SSID = "SPEAKER-TEST-ESP32";
const char *AP_PASSWORD = "12345678";

const uint32_t AUDIO_SAMPLE_RATE = 22050;
const uint16_t AUDIO_BIT_DEPTH = 16;
const uint16_t AUDIO_CHUNK_FRAMES = 96;
const i2s_port_t I2S_PORT = I2S_NUM_0;
const float AUDIO_TWO_PI = 6.28318530718f;
const uint8_t OLED_WIDTH = 128;
const uint8_t OLED_HEIGHT = 64;
const int8_t OLED_RESET_PIN = -1;

Adafruit_NeoPixel rgb(
  1,
  RGB_PIN,
  NEO_GRB + NEO_KHZ800
);

WebServer server(80);
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);

bool i2sReady = false;
String i2sStatus = "Not initialized";
String i2sErrorDetail = "-";
bool oledReady = false;
String oledStatus = "Not initialized";
String oledErrorDetail = "-";
uint32_t lastOledUpdateMs = 0;

String lastSoundMessage = "Speaker siap.";
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
  SPEAKER_TEST_FULL_DIAGNOSTIC
};

SpeakerTestKind activeSpeakerTest = SPEAKER_TEST_NONE;
SpeakerTestKind fullDiagnosticStep = SPEAKER_TEST_NONE;

bool speakerTestRunning = false;
bool fullDiagnosticRunning = false;
bool speakerStopRequested = false;
uint32_t speakerSampleIndex = 0;
uint32_t speakerTotalSamples = 0;
float speakerPhase = 0.0f;
float currentFrequency = 0.0f;
uint8_t currentVolumePercent = 0;
String currentTestName = "Idle";

void setRGB(uint8_t r, uint8_t g, uint8_t b)
{
  rgb.setPixelColor(0, rgb.Color(r, g, b));
  rgb.show();
}

void showQuiet()
{
  setRGB(0, 30, 255);
}

void showSound()
{
  setRGB(0, 255, 60);
}

void showLoud()
{
  setRGB(255, 80, 0);
}

void drawOledStatus(bool force = false)
{
  if (!oledReady)
  {
    return;
  }

  uint32_t now = millis();

  if (!force && now - lastOledUpdateMs < 300)
  {
    return;
  }

  lastOledUpdateMs = now;

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("OLED x Speaker");

  oled.setCursor(0, 12);
  oled.print("IP: ");
  oled.println(WiFi.softAPIP());

  oled.setCursor(0, 24);
  oled.print("I2S: ");
  oled.println(i2sStatus);

  oled.setCursor(0, 36);
  oled.print("Test: ");
  oled.println(currentTestName);

  oled.setCursor(0, 48);
  oled.print((uint32_t)currentFrequency);
  oled.print("Hz  Vol ");
  oled.print(currentVolumePercent);
  oled.println("%");

  oled.display();
}

void setupOled()
{
  Serial.println("OLED init start");
  oledStatus = "Initializing";
  oledErrorDetail = "-";

  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR))
  {
    Serial.println("OLED init failed at 0x3C, trying 0x3D");

    if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3D))
    {
      Serial.println("OLED INIT FAILED");
      oledReady = false;
      oledStatus = "Failed";
      oledErrorDetail = "SSD1306 not found at 0x3C/0x3D";
      return;
    }
  }

  oledReady = true;
  oledStatus = "Initialized";
  oledErrorDetail = "-";
  oled.clearDisplay();
  oled.display();
  drawOledStatus(true);
  Serial.println("OLED INIT OK");
}

String jsonEscape(const String &value)
{
  String out;
  out.reserve(value.length() + 8);

  for (size_t i = 0; i < value.length(); i++)
  {
    char c = value[i];

    if (c == '"' || c == '\\')
    {
      out += '\\';
      out += c;
    }
    else if (c == '\n')
    {
      out += "\\n";
    }
    else
    {
      out += c;
    }
  }

  return out;
}

void setupSpeaker()
{
  Serial.println("I2S init start");
  i2sStatus = "Initializing";
  i2sErrorDetail = "-";

  i2s_config_t i2sConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = AUDIO_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 128,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pinConfig = {
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_LRC_PIN,
    .data_out_num = I2S_DOUT_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  esp_err_t driverResult = i2s_driver_install(I2S_PORT, &i2sConfig, 0, nullptr);

  if (driverResult != ESP_OK)
  {
    Serial.print("I2S init failed: driver_install error=");
    Serial.println((int)driverResult);
    i2sStatus = "Failed";
    i2sErrorDetail = "driver_install error=" + String((int)driverResult);
    lastSoundMessage = "I2S init gagal di driver_install.";
    i2sReady = false;
    drawOledStatus(true);
    return;
  }

  esp_err_t pinResult = i2s_set_pin(I2S_PORT, &pinConfig);

  if (pinResult != ESP_OK)
  {
    Serial.print("I2S init failed: set_pin error=");
    Serial.println((int)pinResult);
    i2sStatus = "Failed";
    i2sErrorDetail = "set_pin error=" + String((int)pinResult);
    lastSoundMessage = "I2S init gagal di set_pin.";
    i2sReady = false;
    drawOledStatus(true);
    return;
  }

  i2s_zero_dma_buffer(I2S_PORT);
  i2sReady = true;
  i2sStatus = "Initialized";
  i2sErrorDetail = "-";
  lastSoundMessage = "I2S speaker siap.";
  drawOledStatus(true);
  Serial.println("I2S init done");
  Serial.println("I2S INIT OK");
}

void writeSample(int16_t sample)
{
  if (!i2sReady)
  {
    return;
  }

  size_t bytesWritten = 0;
  int16_t stereoSample[2] = {
    sample,
    sample
  };

  i2s_write(
    I2S_PORT,
    stereoSample,
    sizeof(stereoSample),
    &bytesWritten,
    portMAX_DELAY
  );
}

void playSilence(uint16_t durationMs)
{
  uint32_t sampleCount = AUDIO_SAMPLE_RATE * durationMs / 1000;

  for (uint32_t i = 0; i < sampleCount; i++)
  {
    writeSample(0);
  }
}

void playTone(float frequency, uint16_t durationMs, int16_t amplitude)
{
  uint32_t sampleCount = AUDIO_SAMPLE_RATE * durationMs / 1000;
  float phase = 0.0f;
  float phaseStep = AUDIO_TWO_PI * frequency / AUDIO_SAMPLE_RATE;

  for (uint32_t i = 0; i < sampleCount; i++)
  {
    float envelope = 1.0f;
    uint32_t fadeSamples = AUDIO_SAMPLE_RATE / 100;

    if (i < fadeSamples)
    {
      envelope = (float)i / fadeSamples;
    }
    else if (sampleCount - i < fadeSamples)
    {
      envelope = (float)(sampleCount - i) / fadeSamples;
    }

    int16_t sample = (int16_t)(sinf(phase) * amplitude * envelope);
    writeSample(sample);

    phase += phaseStep;

    if ((i & 0x7F) == 0)
    {
      server.handleClient();
    }
  }
}

void playChirp(float startFrequency, float endFrequency, uint16_t durationMs, int16_t amplitude)
{
  uint32_t sampleCount = AUDIO_SAMPLE_RATE * durationMs / 1000;
  float phase = 0.0f;

  for (uint32_t i = 0; i < sampleCount; i++)
  {
    float progress = (float)i / sampleCount;
    float frequency = startFrequency + (endFrequency - startFrequency) * progress;
    float envelope = sinf(progress * 3.14159265f);
    phase += AUDIO_TWO_PI * frequency / AUDIO_SAMPLE_RATE;

    int16_t sample = (int16_t)(sinf(phase) * amplitude * envelope);
    writeSample(sample);

    if ((i & 0x7F) == 0)
    {
      server.handleClient();
    }
  }
}

void playNoiseBurst(uint16_t durationMs, int16_t amplitude)
{
  uint32_t sampleCount = AUDIO_SAMPLE_RATE * durationMs / 1000;
  uint32_t randomState = 0x12345678;

  for (uint32_t i = 0; i < sampleCount; i++)
  {
    float progress = (float)i / sampleCount;
    float envelope = sinf(progress * 3.14159265f);

    randomState = randomState * 1664525UL + 1013904223UL;
    int16_t noise = (int16_t)((randomState >> 16) & 0xFFFF);
    noise = noise / 2;

    writeSample((int16_t)(noise * envelope * amplitude / 32768));

    if ((i & 0x7F) == 0)
    {
      server.handleClient();
    }
  }
}

void playVoiceSyllable(float baseFrequency, float formantFrequency, uint16_t durationMs, int16_t amplitude)
{
  uint32_t sampleCount = AUDIO_SAMPLE_RATE * durationMs / 1000;
  float basePhase = 0.0f;
  float formantPhase = 0.0f;

  for (uint32_t i = 0; i < sampleCount; i++)
  {
    float progress = (float)i / sampleCount;
    float envelope = sinf(progress * 3.14159265f);

    basePhase += AUDIO_TWO_PI * baseFrequency / AUDIO_SAMPLE_RATE;
    formantPhase += AUDIO_TWO_PI * formantFrequency / AUDIO_SAMPLE_RATE;

    float carrier = sinf(basePhase) * 0.65f;
    float vowel = sinf(formantPhase) * 0.25f + sinf(formantPhase * 2.0f) * 0.10f;
    int16_t sample = (int16_t)((carrier + vowel) * amplitude * envelope);

    writeSample(sample);

    if ((i & 0x7F) == 0)
    {
      server.handleClient();
    }
  }
}

void playBeep440()
{
  showLoud();
  lastSoundMessage = "Memutar test tone 440Hz.";
  Serial.println(lastSoundMessage);

  playTone(440, 900, 22000);
  playSilence(120);

  showQuiet();
  lastSoundMessage = "Test tone 440Hz selesai.";
}

void playBeep1000()
{
  if (!i2sReady)
  {
    Serial.println("speaker test failed: I2S not ready");
    lastSoundMessage = "Speaker test gagal: I2S belum siap.";
    return;
  }

  showLoud();
  lastSoundMessage = "Speaker test start: 1kHz.";
  Serial.println("speaker test start");

  playTone(1000, 2000, 22000);
  playSilence(120);

  showQuiet();
  lastSoundMessage = "Speaker test done.";
  Serial.println("speaker test done");
}

void playSweepSound()
{
  showLoud();
  lastSoundMessage = "Memutar sweep speaker.";
  Serial.println(lastSoundMessage);

  playChirp(180, 2200, 1600, 22000);
  playSilence(120);

  showQuiet();
  lastSoundMessage = "Sweep speaker selesai.";
}

const char *speakerTestName(SpeakerTestKind kind)
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
    case SPEAKER_TEST_FULL_DIAGNOSTIC: return "Full Speaker Diagnostic";
    default: return "Idle";
  }
}

uint8_t defaultVolumeForTest(SpeakerTestKind kind)
{
  if (kind == SPEAKER_TEST_VOLUME_25) return 25;
  if (kind == SPEAKER_TEST_VOLUME_50) return 50;
  if (kind == SPEAKER_TEST_VOLUME_75) return 75;
  return 100;
}

uint32_t durationMsForTest(SpeakerTestKind kind)
{
  if (kind == SPEAKER_TEST_SWEEP) return 4500;
  if (kind == SPEAKER_TEST_BEEP_3X) return 900;
  if (kind == SPEAKER_TEST_MUTE) return 1200;
  if (kind == SPEAKER_TEST_PCM_SAMPLE) return 2200;
  return 2000;
}

void logSpeakerStart(SpeakerTestKind kind)
{
  switch (kind)
  {
    case SPEAKER_TEST_440HZ:
      Serial.println("TEST 440Hz START");
      break;
    case SPEAKER_TEST_1KHZ:
      Serial.println("TEST 1kHz START");
      break;
    case SPEAKER_TEST_2KHZ:
      Serial.println("TEST 2kHz START");
      break;
    case SPEAKER_TEST_SWEEP:
      Serial.println("SWEEP START");
      break;
    case SPEAKER_TEST_VOLUME_25:
    case SPEAKER_TEST_VOLUME_50:
    case SPEAKER_TEST_VOLUME_75:
    case SPEAKER_TEST_VOLUME_100:
      Serial.print("VOLUME ");
      Serial.print(defaultVolumeForTest(kind));
      Serial.println("%");
      break;
    case SPEAKER_TEST_BEEP_3X:
      Serial.println("BEEP 3X START");
      break;
    case SPEAKER_TEST_MUTE:
      Serial.println("MUTE START");
      break;
    case SPEAKER_TEST_PCM_SAMPLE:
      Serial.println("PCM SAMPLE START");
      break;
    default:
      break;
  }
}

void logSpeakerDone(SpeakerTestKind kind)
{
  switch (kind)
  {
    case SPEAKER_TEST_440HZ:
      Serial.println("TEST 440Hz DONE");
      break;
    case SPEAKER_TEST_1KHZ:
      Serial.println("TEST 1kHz DONE");
      break;
    case SPEAKER_TEST_2KHZ:
      Serial.println("TEST 2kHz DONE");
      break;
    case SPEAKER_TEST_SWEEP:
      Serial.println("SWEEP DONE");
      break;
    case SPEAKER_TEST_BEEP_3X:
      Serial.println("BEEP 3X DONE");
      break;
    case SPEAKER_TEST_MUTE:
      Serial.println("MUTE DONE");
      break;
    case SPEAKER_TEST_PCM_SAMPLE:
      Serial.println("PCM SAMPLE DONE");
      break;
    default:
      break;
  }
}

void startSpeakerTest(SpeakerTestKind kind, bool partOfFullDiagnostic = false)
{
  if (!i2sReady)
  {
    Serial.print("SPEAKER TEST FAILED: I2S not ready. ");
    Serial.println(i2sErrorDetail);
    lastSoundMessage = "Speaker test gagal: " + i2sErrorDetail;
    currentTestName = "I2S Failed";
    speakerTestRunning = false;
    fullDiagnosticRunning = false;
    drawOledStatus(true);
    return;
  }

  speakerStopRequested = false;
  speakerTestRunning = true;
  activeSpeakerTest = kind;
  speakerSampleIndex = 0;
  speakerTotalSamples = AUDIO_SAMPLE_RATE * durationMsForTest(kind) / 1000;
  speakerPhase = 0.0f;
  currentVolumePercent = defaultVolumeForTest(kind);
  currentFrequency = 0.0f;
  currentTestName = speakerTestName(kind);
  lastSoundMessage = "Menjalankan " + currentTestName + ".";

  if (!partOfFullDiagnostic)
  {
    fullDiagnosticRunning = false;
    fullDiagnosticStep = SPEAKER_TEST_NONE;
  }

  logSpeakerStart(kind);
  drawOledStatus(true);
}

int16_t nextSpeakerSample()
{
  float frequency = 1000.0f;
  float amplitude = 22000.0f * currentVolumePercent / 100.0f;
  bool mute = false;

  if (activeSpeakerTest == SPEAKER_TEST_440HZ)
  {
    frequency = 440.0f;
  }
  else if (activeSpeakerTest == SPEAKER_TEST_2KHZ)
  {
    frequency = 2000.0f;
  }
  else if (activeSpeakerTest == SPEAKER_TEST_SWEEP)
  {
    float progress = speakerTotalSamples > 0 ? (float)speakerSampleIndex / speakerTotalSamples : 0.0f;
    frequency = 200.0f + (5000.0f - 200.0f) * progress;
  }
  else if (activeSpeakerTest == SPEAKER_TEST_BEEP_3X)
  {
    uint32_t sampleInCycle = speakerSampleIndex % (AUDIO_SAMPLE_RATE * 300 / 1000);
    uint32_t beepSamples = AUDIO_SAMPLE_RATE * 120 / 1000;
    frequency = 1000.0f;
    mute = sampleInCycle >= beepSamples;
  }
  else if (activeSpeakerTest == SPEAKER_TEST_MUTE)
  {
    mute = true;
  }
  else if (activeSpeakerTest == SPEAKER_TEST_PCM_SAMPLE)
  {
    uint32_t segmentSamples = AUDIO_SAMPLE_RATE * 350 / 1000;
    uint8_t segment = (speakerSampleIndex / segmentSamples) % 6;
    const float notes[] = { 523.25f, 659.25f, 783.99f, 659.25f, 523.25f, 392.00f };
    frequency = notes[segment];
  }

  currentFrequency = frequency;

  if (mute)
  {
    return 0;
  }

  float envelope = 1.0f;
  uint32_t fadeSamples = AUDIO_SAMPLE_RATE / 100;

  if (speakerSampleIndex < fadeSamples)
  {
    envelope = (float)speakerSampleIndex / fadeSamples;
  }
  else if (speakerTotalSamples - speakerSampleIndex < fadeSamples)
  {
    envelope = (float)(speakerTotalSamples - speakerSampleIndex) / fadeSamples;
  }

  speakerPhase += AUDIO_TWO_PI * frequency / AUDIO_SAMPLE_RATE;

  if (speakerPhase > AUDIO_TWO_PI)
  {
    speakerPhase -= AUDIO_TWO_PI;
  }

  return (int16_t)(sinf(speakerPhase) * amplitude * envelope);
}

SpeakerTestKind nextFullDiagnosticStep(SpeakerTestKind current)
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

void stopSpeakerTest(const char *reason)
{
  playSilence(40);
  speakerTestRunning = false;
  fullDiagnosticRunning = false;
  speakerStopRequested = false;
  activeSpeakerTest = SPEAKER_TEST_NONE;
  fullDiagnosticStep = SPEAKER_TEST_NONE;
  currentFrequency = 0.0f;
  currentVolumePercent = 0;
  currentTestName = "Idle";
  lastSoundMessage = reason;
  Serial.println(reason);
  drawOledStatus(true);
}

void startFullSpeakerDiagnostic()
{
  Serial.println("FULL SPEAKER DIAGNOSTIC START");

  if (i2sReady)
  {
    Serial.println("I2S INIT OK");
  }
  else
  {
    setupSpeaker();

    if (!i2sReady)
    {
      Serial.print("FULL SPEAKER DIAGNOSTIC FAILED: ");
      Serial.println(i2sErrorDetail);
      currentTestName = "I2S Failed";
      lastSoundMessage = "Full diagnostic gagal: " + i2sErrorDetail;
      drawOledStatus(true);
      return;
    }
  }

  fullDiagnosticRunning = true;
  fullDiagnosticStep = SPEAKER_TEST_NONE;
  startSpeakerTest(nextFullDiagnosticStep(fullDiagnosticStep), true);
}

void serviceSpeakerTest()
{
  if (!speakerTestRunning)
  {
    return;
  }

  if (speakerStopRequested)
  {
    stopSpeakerTest("SPEAKER TEST STOPPED");
    return;
  }

  uint16_t framesToWrite = AUDIO_CHUNK_FRAMES;

  while (framesToWrite-- > 0 && speakerSampleIndex < speakerTotalSamples)
  {
    writeSample(nextSpeakerSample());
    speakerSampleIndex++;
  }

  if (speakerSampleIndex < speakerTotalSamples)
  {
    return;
  }

  SpeakerTestKind finishedTest = activeSpeakerTest;
  logSpeakerDone(finishedTest);

  if (fullDiagnosticRunning)
  {
    fullDiagnosticStep = finishedTest;
    SpeakerTestKind nextStep = nextFullDiagnosticStep(fullDiagnosticStep);

    if (nextStep != SPEAKER_TEST_NONE)
    {
      startSpeakerTest(nextStep, true);
      return;
    }

    Serial.println("SPEAKER TEST COMPLETE");
    speakerTestRunning = false;
    fullDiagnosticRunning = false;
    activeSpeakerTest = SPEAKER_TEST_NONE;
    fullDiagnosticStep = SPEAKER_TEST_NONE;
    currentFrequency = 0.0f;
    currentVolumePercent = 0;
    currentTestName = "Idle";
    lastSoundMessage = "Full Speaker Diagnostic selesai.";
    showQuiet();
    drawOledStatus(true);
    return;
  }

  speakerTestRunning = false;
  activeSpeakerTest = SPEAKER_TEST_NONE;
  currentFrequency = 0.0f;
  currentVolumePercent = 0;
  currentTestName = "Idle";
  lastSoundMessage = String(speakerTestName(finishedTest)) + " selesai.";
  showQuiet();
  drawOledStatus(true);
}

String buildStatusJson()
{
  String json = "{";
  json += "\"ip\":\"" + WiFi.softAPIP().toString() + "\",";
  json += "\"bclkPin\":" + String(I2S_BCLK_PIN) + ",";
  json += "\"lrcPin\":" + String(I2S_LRC_PIN) + ",";
  json += "\"doutPin\":" + String(I2S_DOUT_PIN) + ",";
  json += "\"oledSdaPin\":" + String(OLED_SDA_PIN) + ",";
  json += "\"oledSclPin\":" + String(OLED_SCL_PIN) + ",";
  json += "\"oledReady\":" + String(oledReady ? "true" : "false") + ",";
  json += "\"oledStatus\":\"" + jsonEscape(oledStatus) + "\",";
  json += "\"oledError\":\"" + jsonEscape(oledErrorDetail) + "\",";
  json += "\"i2sReady\":" + String(i2sReady ? "true" : "false") + ",";
  json += "\"i2sStatus\":\"" + jsonEscape(i2sStatus) + "\",";
  json += "\"i2sError\":\"" + jsonEscape(i2sErrorDetail) + "\",";
  json += "\"sampleRate\":" + String(AUDIO_SAMPLE_RATE) + ",";
  json += "\"bitDepth\":" + String(AUDIO_BIT_DEPTH) + ",";
  json += "\"currentTest\":\"" + jsonEscape(currentTestName) + "\",";
  json += "\"currentFrequency\":" + String((uint32_t)currentFrequency) + ",";
  json += "\"currentVolume\":" + String(currentVolumePercent) + ",";
  json += "\"speakerRunning\":" + String(speakerTestRunning ? "true" : "false") + ",";
  json += "\"soundMessage\":\"" + jsonEscape(lastSoundMessage) + "\"";
  json += "}";

  return json;
}

String buildPage()
{
  String page = R"HTML(
<!doctype html>
<html lang="id">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>OLED x Speaker</title>
  <style>
    :root {
      color-scheme: light;
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: #f5f7f7;
      color: #172126;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      display: flex;
      justify-content: center;
      background: #f5f7f7;
    }
    main {
      width: min(100%, 560px);
      padding: 22px 16px 30px;
    }
    h1 {
      margin: 0 0 6px;
      font-size: 28px;
      line-height: 1.1;
      letter-spacing: 0;
    }
    .sub {
      margin: 0 0 16px;
      color: #59676d;
      font-size: 15px;
    }
    .panel {
      background: #fff;
      border: 1px solid #dce5e7;
      border-radius: 8px;
      padding: 16px;
      box-shadow: 0 10px 28px rgba(29, 47, 54, 0.08);
      margin-bottom: 14px;
    }
    .grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
      font-size: 15px;
    }
    .actions {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
    }
    button {
      min-height: 56px;
      border: 0;
      border-radius: 8px;
      background: #285f8f;
      color: #fff;
      font-size: 16px;
      font-weight: 800;
      letter-spacing: 0;
      touch-action: manipulation;
    }
    button:active { transform: translateY(1px); }
    .wide { grid-column: 1 / -1; }
    .cell {
      border: 1px solid #e3eaec;
      border-radius: 8px;
      padding: 12px;
      min-height: 68px;
    }
    .label {
      color: #5f6d73;
      display: block;
      margin-bottom: 6px;
    }
    .value {
      font-size: 20px;
      font-weight: 800;
      overflow-wrap: anywhere;
    }
    .note {
      color: #5f6d73;
      font-size: 14px;
      line-height: 1.45;
    }
  </style>
</head>
<body>
  <main>
    <h1>OLED x Speaker</h1>
    <p class="sub">Hotspot mode. Buka 192.168.4.1 dari HP yang tersambung ke WiFi alat.</p>

    <section class="panel actions">
      <button class="wide" onclick="playSound('testSpeaker')">Test Speaker 1kHz</button>
      <button onclick="playSound('beep440')">Tone 440Hz</button>
      <button onclick="playSound('beep1000')">Tone 1kHz</button>
      <button class="wide" onclick="playSound('sweep')">Sweep Speaker</button>
    </section>

    <section class="panel actions">
      <button onclick="playSound('tone440')">Test 440 Hz</button>
      <button onclick="playSound('tone1000')">Test 1 kHz</button>
      <button onclick="playSound('tone2000')">Test 2 kHz</button>
      <button onclick="playSound('sweepFull')">Sweep 200-5k</button>
      <button onclick="playSound('volume25')">Volume 25%</button>
      <button onclick="playSound('volume50')">Volume 50%</button>
      <button onclick="playSound('volume75')">Volume 75%</button>
      <button onclick="playSound('volume100')">Volume 100%</button>
      <button onclick="playSound('beep3x')">Beep 3x</button>
      <button onclick="playSound('mute')">Mute</button>
      <button onclick="playSound('pcm')">PCM Sample</button>
      <button onclick="playSound('stop')">Stop Speaker</button>
      <button class="wide" onclick="playSound('fullDiagnostic')">Full Speaker Diagnostic</button>
    </section>

    <section class="panel grid">
      <div class="cell"><span class="label">Pin I2S</span><span class="value" id="i2sPins">-</span></div>
      <div class="cell"><span class="label">Pin OLED</span><span class="value" id="oledPins">-</span></div>
      <div class="cell"><span class="label">IP alat</span><span class="value" id="ip">-</span></div>
      <div class="cell"><span class="label">Speaker</span><span class="value" id="speaker">-</span></div>
      <div class="cell"><span class="label">OLED Status</span><span class="value" id="oledStatus">-</span></div>
      <div class="cell"><span class="label">I2S Status</span><span class="value" id="i2sStatus">-</span></div>
      <div class="cell"><span class="label">Audio format</span><span class="value" id="audioFormat">-</span></div>
      <div class="cell"><span class="label">Current test</span><span class="value" id="currentTest">-</span></div>
      <div class="cell"><span class="label">Freq / Volume</span><span class="value" id="freqVolume">-</span></div>
    </section>

    <section class="panel note">
      Halaman ini fokus untuk uji OLED I2C dan MAX98357A speaker via I2S.
    </section>
  </main>

  <script>
    async function refresh() {
      const res = await fetch('/status', { cache: 'no-store' });
      const data = await res.json();

      document.getElementById('i2sPins').textContent = `BCLK ${data.bclkPin} / LRC ${data.lrcPin} / DIN ${data.doutPin}`;
      document.getElementById('oledPins').textContent = `SDA ${data.oledSdaPin} / SCL ${data.oledSclPin}`;
      document.getElementById('ip').textContent = data.ip;
      document.getElementById('speaker').textContent = data.soundMessage;
      document.getElementById('oledStatus').textContent = `${data.oledStatus}${data.oledReady ? '' : ` (${data.oledError})`}`;
      document.getElementById('i2sStatus').textContent = `${data.i2sStatus}${data.i2sReady ? '' : ` (${data.i2sError})`}`;
      document.getElementById('audioFormat').textContent = `${data.sampleRate} Hz / ${data.bitDepth} bit`;
      document.getElementById('currentTest').textContent = data.currentTest;
      document.getElementById('freqVolume').textContent = `${data.currentFrequency} Hz / ${data.currentVolume}%`;
    }

    async function playSound(name) {
      document.getElementById('speaker').textContent = 'Memutar...';
      await fetch(`/sound?name=${name}`, { cache: 'no-store' });
      await refresh();
    }

    refresh();
    setInterval(refresh, 180);
  </script>
</body>
</html>
)HTML";

  return page;
}

void handleRoot()
{
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", buildPage());
}

void handleStatus()
{
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", buildStatusJson());
}

void handleFavicon()
{
  server.send(204);
}

void handleSound()
{
  String name = server.arg("name");

  if (name == "stop")
  {
    speakerStopRequested = true;
    lastSoundMessage = "Stop speaker diminta.";
  }
  else if (name == "fullDiagnostic")
  {
    startFullSpeakerDiagnostic();
  }
  else if (name == "beep440" || name == "tone440")
  {
    startSpeakerTest(SPEAKER_TEST_440HZ);
  }
  else if (name == "beep1000" || name == "testSpeaker" || name == "tone1000")
  {
    startSpeakerTest(SPEAKER_TEST_1KHZ);
  }
  else if (name == "tone2000")
  {
    startSpeakerTest(SPEAKER_TEST_2KHZ);
  }
  else if (name == "sweep" || name == "sweepFull")
  {
    startSpeakerTest(SPEAKER_TEST_SWEEP);
  }
  else if (name == "volume25")
  {
    startSpeakerTest(SPEAKER_TEST_VOLUME_25);
  }
  else if (name == "volume50")
  {
    startSpeakerTest(SPEAKER_TEST_VOLUME_50);
  }
  else if (name == "volume75")
  {
    startSpeakerTest(SPEAKER_TEST_VOLUME_75);
  }
  else if (name == "volume100")
  {
    startSpeakerTest(SPEAKER_TEST_VOLUME_100);
  }
  else if (name == "beep3x")
  {
    startSpeakerTest(SPEAKER_TEST_BEEP_3X);
  }
  else if (name == "mute")
  {
    startSpeakerTest(SPEAKER_TEST_MUTE);
  }
  else if (name == "pcm")
  {
    startSpeakerTest(SPEAKER_TEST_PCM_SAMPLE);
  }
  else
  {
    lastSoundMessage = "Pilihan suara tidak dikenal.";
  }

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", buildStatusJson());
}

void handleNotFound()
{
  server.send(404, "text/plain", "Not found");
}

void setupWebServer()
{
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/sound", handleSound);
  server.on("/favicon.ico", handleFavicon);
  server.onNotFound(handleNotFound);
  server.begin();
}

void setup()
{
  Serial.begin(115200);
  delay(1200);

  rgb.begin();
  rgb.clear();
  rgb.show();
  showQuiet();

  setupOled();
  setupSpeaker();

  setupWebServer();
  drawOledStatus(true);

  Serial.println();
  Serial.println("======================================");
  Serial.println("ESP32-S3 SPEAKER I2S TEST");
  Serial.println("======================================");
  Serial.print("WiFi SSID        : ");
  Serial.println(AP_SSID);
  Serial.print("WiFi Password    : ");
  Serial.println(AP_PASSWORD);
  Serial.print("Web UI           : http://");
  Serial.println(WiFi.softAPIP());
  Serial.print("I2S BCLK         : GPIO");
  Serial.println(I2S_BCLK_PIN);
  Serial.print("I2S LRC          : GPIO");
  Serial.println(I2S_LRC_PIN);
  Serial.print("I2S DIN          : GPIO");
  Serial.println(I2S_DOUT_PIN);
  Serial.print("OLED SDA         : GPIO");
  Serial.println(OLED_SDA_PIN);
  Serial.print("OLED SCL         : GPIO");
  Serial.println(OLED_SCL_PIN);
  Serial.print("OLED Status      : ");
  Serial.println(oledStatus);
  Serial.println();
  Serial.println("Wiring:");
  Serial.println("MAX98357A BCLK -> GPIO12");
  Serial.println("MAX98357A LRC  -> GPIO13");
  Serial.println("MAX98357A DIN  -> GPIO14");
  Serial.println("MAX98357A VIN  -> 5V");
  Serial.println("MAX98357A GND  -> GND");
  Serial.println("OLED VCC       -> 3V3");
  Serial.println("OLED GND       -> GND");
  Serial.println("OLED SCL       -> GPIO9");
  Serial.println("OLED SDA       -> GPIO8");
  Serial.println("======================================");
}

void loop()
{
  server.handleClient();
  serviceSpeakerTest();
  drawOledStatus();

  delay(2);
}
