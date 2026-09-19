#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_SSD1306.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <math.h>
#include "voice_sample.h"
#include "web_ui.h"

// ============================================================
// DEVICE A - FACTORY / MODBUS TEST FIRMWARE
// ESP32-S3 + OLED + RGB + RS485 DDS3366D-1P / Kehua-compatible meter
//
// RS485-to-TTL 4-pin wiring, auto direction:
//   VCC -> 3V3 or 5V sesuai modul
//   GND -> GND ESP32 dan common ground meter side
//   TXD -> GPIO18 (ESP32 RX)
//   RXD -> GPIO17 (ESP32 TX)
//
// OLED:
//   SDA -> GPIO11
//   SCL -> GPIO21
//
// Meter default per protocol:
//   Slave     : 1
//   Baud      : 2400
//   Format    : 8E1
//   Function  : 0x03
//   Registers : 0..16 (17 registers)
// ============================================================

static const char *AP_SSID = "AQU-AC-Remote";
static const char *AP_PASSWORD = "12345678";
static const char *DEFAULT_DEVICE_PREFIX = "hems/ac";
static const char *MQTT_HOST = "broker.emqx.io";
static const uint16_t MQTT_PORT = 1883;

static const uint8_t PIN_LED_RGB = 48;
static const uint8_t PIN_IR_RX = 4;
static const uint8_t PIN_IR_TX = 10;
static const uint8_t PIN_RS485_RX = 18;
static const uint8_t PIN_RS485_TX = 17;
static const uint8_t PIN_OLED_SDA = 11;
static const uint8_t PIN_OLED_SCL = 21;
static const uint8_t PIN_I2S_BCLK = 8;
static const uint8_t PIN_I2S_LRC = 9;
static const uint8_t PIN_I2S_DOUT = 11;
static const uint8_t PIN_MIC_BCLK = 6;
static const uint8_t PIN_MIC_WS = 7;
static const uint8_t PIN_MIC_DIN = 15;
static const bool OLED_ENABLED = false; // Hardware OLED rusak; firmware lain tetap berjalan.

#ifndef RS485_BAUD
#define RS485_BAUD 2400
#endif

#ifndef MODBUS_SLAVE_ID
#define MODBUS_SLAVE_ID 1
#endif

static const uint32_t METER_RS485_BAUD = RS485_BAUD;
static const uint8_t METER_MODBUS_SLAVE_ID = MODBUS_SLAVE_ID;
static const uint32_t MODBUS_RESPONSE_TIMEOUT_MS = 900;
static const uint32_t MODBUS_INTERBYTE_TIMEOUT_MS = 100;
static const uint32_t METER_POLL_INTERVAL_MS = 3000;
static const uint32_t WIFI_STATUS_INTERVAL_MS = 1000;
static const uint32_t WIFI_RETRY_INTERVAL_MS = 12000;
static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
static const uint32_t MQTT_RETRY_INTERVAL_MS = 5000;
static const uint16_t MQTT_BUFFER_SIZE = 1536;
static const float SESSION_ACTIVE_POWER_THRESHOLD_KW = 0.05f;

static const uint8_t MODBUS_FUNCTION_READ = 0x03;
static const uint16_t MODBUS_START_REGISTER = 0;
static const uint16_t MODBUS_REGISTER_COUNT = 17;
static const uint8_t IR_SEND_KHZ = 38;
static const uint16_t IR_CAPTURE_BUFFER_SIZE = 1024;
static const uint8_t IR_TIMEOUT_MS = 50;
static const uint16_t IR_MIN_RAW_LEN = 12;
static const uint16_t IR_MAX_RAW_LEN = 750;
static const uint32_t AMP_SAMPLE_RATE = 22050;
static const uint16_t AMP_CHUNK_FRAMES = 96;
static const i2s_port_t AMP_I2S_PORT = I2S_NUM_0;
static const i2s_port_t MIC_I2S_PORT = I2S_NUM_1;
static const float AMP_TWO_PI = 6.28318530718f;
static const uint32_t MIC_SAMPLE_RATE = 16000;
static const uint16_t MIC_CHUNK_SAMPLES = 128;
static const uint32_t MIC_TEST_DURATION_MS = 4000;
static const uint16_t MIC_SPEECH_RMS_THRESHOLD = 30;

// Jika pembacaan 32-bit terlihat tidak masuk akal, ubah menjadi true.
static const bool MODBUS_SWAP_32BIT_WORDS = false;

Adafruit_NeoPixel led(1, PIN_LED_RGB, NEO_GRB + NEO_KHZ800);
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
HardwareSerial meterSerial(1);
WebServer server(80);
Preferences prefs;
WiFiClient mqttWifiClient;
PubSubClient mqttClient(mqttWifiClient);
IRrecv irReceiver(PIN_IR_RX, IR_CAPTURE_BUFFER_SIZE, IR_TIMEOUT_MS, true);
IRsend irSender(PIN_IR_TX);
decode_results irResults;

enum IrLearnSlot
{
  IR_LEARN_NONE,
  IR_LEARN_ON,
  IR_LEARN_OFF
};

struct MeterData
{
  bool valid = false;
  float totalActiveEnergy = 0.0f;
  float forwardEnergy = 0.0f;
  float reverseEnergy = 0.0f;
  float voltage = 0.0f;
  float current = 0.0f;
  float activePower = 0.0f;
  float powerFactor = 0.0f;
  float frequency = 0.0f;
  float reactivePower = 0.0f;
  float apparentPower = 0.0f;
  uint32_t lastSuccessMs = 0;
};

MeterData meterData;
bool oledReady = false;
uint8_t oledAddress = 0;
uint32_t lastMeterPollMs = 0;
uint32_t meterOkCount = 0;
uint32_t meterErrCount = 0;
uint32_t lastPollDurationMs = 0;
String meterStatus = "Belum dibaca";
String meterErrorDetail = "-";
String lastRequestHex = "-";
String lastResponseHex = "-";
String lastRegistersHex = "-";
uint16_t lastRegisters[MODBUS_REGISTER_COUNT] = {0};
uint8_t lastResponseBytes = 0;
uint16_t rawIrOn[IR_MAX_RAW_LEN] = {0};
uint16_t rawIrOff[IR_MAX_RAW_LEN] = {0};
uint16_t rawIrOnLen = 0;
uint16_t rawIrOffLen = 0;
IrLearnSlot irLearnSlot = IR_LEARN_NONE;
bool irReceiverActive = false;
String irStatus = "IR belum init";
String irMessage = "-";
uint32_t irTestTransitions = 0;
uint32_t irTestLowSamples = 0;
uint32_t irTestTotalSamples = 0;
String irTestResult = "Belum dites";
String i2cScanResult = "Belum scan";
String oledStatusDetail = "Belum init";
bool amplifierReady = false;
String amplifierStatus = "Belum init";
String amplifierMessage = "-";
String amplifierLastTest = "Belum dites";
uint32_t amplifierTestCount = 0;
uint32_t amplifierWriteErrors = 0;
uint32_t amplifierShortWrites = 0;
bool microphoneReady = false;
bool microphoneTestRunning = false;
String microphoneStatus = "Belum init";
String microphoneTestResult = "Belum dites";
uint32_t microphoneRms = 0;
uint32_t microphonePeak = 0;
uint32_t microphoneTestMaxRms = 0;
uint32_t microphoneTestMaxPeak = 0;
uint32_t microphoneChunks = 0;
uint32_t microphoneErrors = 0;
uint32_t microphoneTestStartedMs = 0;
String devicePrefix = DEFAULT_DEVICE_PREFIX;
String wifiStaSsid = "";
String wifiStaPassword = "";
String wifiStaStatus = "WiFi STA belum dikonfigurasi";
bool wifiStaConnecting = false;
uint32_t lastWifiStatusMs = 0;
uint32_t lastWifiConnectAttemptMs = 0;
String mqttDeviceId = "";
String mqttStateTopic = "";
String mqttStatus = "MQTT belum init";
String lastMqttPayload = "-";
uint32_t lastMqttConnectAttemptMs = 0;
uint32_t lastPublishedMeterReadMs = 0;
uint32_t mqttOkCount = 0;
uint32_t mqttErrCount = 0;
bool energySessionActive = false;
float energySessionStartKwh = 0.0f;
float energySessionKwh = 0.0f;
uint32_t countSession = 0;

void drawOledText(const String &line1, const String &line2, const String &line3, const String &line4);
void setIrReceiverActive(bool active);

String jsonEscape(const String &value)
{
  String out;
  out.reserve(value.length() + 12);
  for (size_t i = 0; i < value.length(); i++)
  {
    const char c = value[i];
    if (c == '"' || c == '\\')
    {
      out += '\\';
      out += c;
    }
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else out += c;
  }
  return out;
}

String normalizePrefix(String value)
{
  value.trim();
  value.replace('\\', '/');
  while (value.startsWith("/")) value.remove(0, 1);
  while (value.endsWith("/")) value.remove(value.length() - 1);

  String out;
  out.reserve(min((size_t)value.length(), (size_t)48));
  for (size_t i = 0; i < value.length() && out.length() < 48; i++)
  {
    const char c = value[i];
    const bool allowed =
      (c >= 'a' && c <= 'z') ||
      (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') ||
      c == '_' || c == '-' || c == '/';
    if (allowed) out += c;
  }

  if (out.length() == 0) return String(DEFAULT_DEVICE_PREFIX);
  return out;
}

String wifiStatusName(wl_status_t status)
{
  switch (status)
  {
    case WL_IDLE_STATUS: return "Idle";
    case WL_NO_SSID_AVAIL: return "SSID tidak ditemukan";
    case WL_SCAN_COMPLETED: return "Scan selesai";
    case WL_CONNECTED: return "Connected";
    case WL_CONNECT_FAILED: return "Connect failed";
    case WL_CONNECTION_LOST: return "Koneksi terputus";
    case WL_DISCONNECTED: return "Disconnected";
    default: return "Status " + String((int)status);
  }
}

void startWifiStaConnect(bool force)
{
  if (wifiStaSsid.length() == 0)
  {
    WiFi.disconnect(false, false);
    wifiStaConnecting = false;
    wifiStaStatus = "WiFi STA belum dikonfigurasi";
    return;
  }

  if (!force && WiFi.status() == WL_CONNECTED)
  {
    wifiStaConnecting = false;
    wifiStaStatus = "Connected ke " + wifiStaSsid;
    return;
  }

  WiFi.disconnect(false, false);
  WiFi.begin(wifiStaSsid.c_str(), wifiStaPassword.c_str());
  wifiStaConnecting = true;
  lastWifiConnectAttemptMs = millis();
  wifiStaStatus = "Menghubungkan ke " + wifiStaSsid;
}

void saveWifiStaConfig(const String &ssid, const String &password)
{
  String newSsid = ssid;
  newSsid.trim();
  if (newSsid.length() > 0) wifiStaSsid = newSsid;
  if (password.length() > 0 || wifiStaPassword.length() == 0) wifiStaPassword = password;
  prefs.putString("staSsid", wifiStaSsid);
  prefs.putString("staPass", wifiStaPassword);
  startWifiStaConnect(true);
}

String webArg(const char *name)
{
  for (uint8_t i = 0; i < server.args(); i++)
  {
    if (server.argName(i) == name) return server.arg(i);
  }
  return "";
}

bool internetReady()
{
  return WiFi.status() == WL_CONNECTED;
}

bool meterPollingAllowed()
{
  return internetReady();
}

bool i2cDevicePresent(uint8_t address)
{
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool initOledAt(uint8_t address)
{
  if (!i2cDevicePresent(address)) return false;
  if (!oled.begin(SSD1306_SWITCHCAPVCC, address)) return false;
  oledAddress = address;
  oledReady = true;
  oledStatusDetail = "OLED active at 0x" + String(address, HEX);
  return true;
}

String scanI2cBus()
{
  uint8_t found = 0;
  String result;

  for (uint8_t address = 1; address < 127; address++)
  {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0)
    {
      if (found > 0) result += ", ";
      if (address < 16) result += "0x0";
      else result += "0x";
      result += String(address, HEX);
      found++;
    }
    delay(1);
  }

  if (found == 0) result = "Tidak ada device I2C di SDA11/SCL21";
  i2cScanResult = result;
  return result;
}

bool initOled()
{
  oledReady = false;
  oledAddress = 0;
  if (!OLED_ENABLED)
  {
    i2cScanResult = "Dinonaktifkan";
    oledStatusDetail = "OLED nonaktif (hardware rusak)";
    return false;
  }
  scanI2cBus();

  if (initOledAt(0x3C) || initOledAt(0x3D))
  {
    return true;
  }

  oledStatusDetail = "OLED tidak terdeteksi di 0x3C/0x3D";
  return false;
}

void updateMqttTopics()
{
  if (mqttDeviceId.length() == 0)
  {
    uint64_t mac = ESP.getEfuseMac();
    char id[16];
    snprintf(id, sizeof(id), "%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac);
    mqttDeviceId = id;
  }

  mqttStateTopic = devicePrefix + "/" + mqttDeviceId + "/state";
}

String buildMqttPayload()
{
  String json = "{";
  json += "\"sn\":\"" + jsonEscape(mqttDeviceId) + "\",";
  json += "\"prefix\":\"" + jsonEscape(devicePrefix) + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"totalActiveEnergy\":" + String(meterData.totalActiveEnergy, 2) + ",";
  json += "\"EnergySession\":" + String(energySessionKwh, 4) + ",";
  json += "\"CountSession\":" + String(countSession) + ",";
  json += "\"voltage\":" + String(meterData.voltage, 1) + ",";
  json += "\"current\":" + String(meterData.current, 3) + ",";
  json += "\"activePower\":" + String(meterData.activePower, 4) + ",";
  json += "\"powerFactor\":" + String(meterData.powerFactor, 3) + ",";
  json += "\"frequency\":" + String(meterData.frequency, 2) + ",";
  json += "\"reactivePower\":" + String(meterData.reactivePower, 4) + ",";
  json += "\"apparentPower\":" + String(meterData.apparentPower, 4) + ",";
  json += "\"uptimeMs\":" + String(millis());
  json += "}";
  return json;
}

void connectMqtt()
{
  lastMqttConnectAttemptMs = millis();
  if (!internetReady())
  {
    mqttStatus = "Menunggu internet";
    return;
  }

  updateMqttTopics();
  String clientId = "device-a-" + mqttDeviceId;
  mqttStatus = "MQTT connecting";

  if (mqttClient.connect(clientId.c_str()))
  {
    mqttStatus = "MQTT connected";
    return;
  }

  mqttStatus = "MQTT gagal rc=" + String(mqttClient.state());
}

void serviceMqtt()
{
  if (!internetReady())
  {
    if (mqttClient.connected()) mqttClient.disconnect();
    mqttStatus = "Menunggu internet";
    return;
  }

  if (mqttClient.connected())
  {
    mqttClient.loop();
    return;
  }

  if (millis() - lastMqttConnectAttemptMs >= MQTT_RETRY_INTERVAL_MS)
  {
    connectMqtt();
  }
}

bool publishMeterMqtt()
{
  if (!meterData.valid || meterData.lastSuccessMs == 0)
  {
    mqttStatus = "Meter belum valid";
    return false;
  }

  serviceMqtt();
  if (!mqttClient.connected())
  {
    mqttErrCount++;
    return false;
  }

  updateMqttTopics();
  String payload = buildMqttPayload();
  const bool ok = mqttClient.publish(mqttStateTopic.c_str(), payload.c_str(), true);
  if (ok)
  {
    lastPublishedMeterReadMs = meterData.lastSuccessMs;
    lastMqttPayload = payload;
    mqttOkCount++;
    mqttStatus = "Publish OK";
  }
  else
  {
    mqttErrCount++;
    mqttStatus = "Publish gagal";
  }

  return ok;
}

void updateEnergySession()
{
  const bool activeNow = meterData.activePower >= SESSION_ACTIVE_POWER_THRESHOLD_KW;

  if (activeNow && !energySessionActive)
  {
    energySessionActive = true;
    energySessionStartKwh = meterData.totalActiveEnergy;
    energySessionKwh = 0.0f;
    countSession++;
    prefs.putUInt("cntSess", countSession);
  }
  else if (activeNow)
  {
    energySessionKwh = meterData.totalActiveEnergy - energySessionStartKwh;
    if (energySessionKwh < 0.0f) energySessionKwh = 0.0f;
  }
  else
  {
    energySessionActive = false;
    energySessionStartKwh = meterData.totalActiveEnergy;
    energySessionKwh = 0.0f;
  }
}

void clearWifiStaConfig()
{
  prefs.remove("staSsid");
  prefs.remove("staPass");
  wifiStaSsid = "";
  wifiStaPassword = "";
  WiFi.disconnect(false, false);
  wifiStaConnecting = false;
  wifiStaStatus = "WiFi STA dihapus";
}

void serviceWifiSta()
{
  const uint32_t now = millis();
  if ((now - lastWifiStatusMs) < WIFI_STATUS_INTERVAL_MS) return;
  lastWifiStatusMs = now;

  if (wifiStaSsid.length() == 0)
  {
    wifiStaStatus = "WiFi STA belum dikonfigurasi";
    return;
  }

  const wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED)
  {
    wifiStaConnecting = false;
    wifiStaStatus = "Connected ke " + wifiStaSsid;
    return;
  }

  if (wifiStaConnecting && (now - lastWifiConnectAttemptMs) < WIFI_CONNECT_TIMEOUT_MS)
  {
    wifiStaStatus = "Menghubungkan: " + wifiStatusName(status);
    return;
  }

  wifiStaConnecting = false;
  wifiStaStatus = wifiStatusName(status);
  if ((now - lastWifiConnectAttemptMs) >= WIFI_RETRY_INTERVAL_MS)
  {
    startWifiStaConnect(true);
  }
}

uint16_t modbusCrc(const uint8_t *buffer, size_t length)
{
  uint16_t crc = 0xFFFF;
  for (size_t pos = 0; pos < length; pos++)
  {
    crc ^= buffer[pos];
    for (uint8_t i = 0; i < 8; i++)
    {
      if (crc & 0x0001)
      {
        crc >>= 1;
        crc ^= 0xA001;
      }
      else crc >>= 1;
    }
  }
  return crc;
}

String bytesToHex(const uint8_t *buffer, size_t length)
{
  const char *hex = "0123456789ABCDEF";
  String out;
  out.reserve(length * 3);
  for (size_t i = 0; i < length; i++)
  {
    if (i > 0) out += ' ';
    out += hex[(buffer[i] >> 4) & 0x0F];
    out += hex[buffer[i] & 0x0F];
  }
  return out;
}

String registersToHex(const uint16_t *regs, uint16_t count)
{
  char temp[16];
  String out;
  out.reserve(count * 11);
  for (uint16_t i = 0; i < count; i++)
  {
    if (i > 0) out += ' ';
    snprintf(temp, sizeof(temp), "R%u=%04X", i, regs[i]);
    out += temp;
  }
  return out;
}

void setLed(uint8_t r, uint8_t g, uint8_t b)
{
  led.setPixelColor(0, led.Color(r, g, b));
  led.show();
}

bool setupMicrophone()
{
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = MIC_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 4,
    .dma_buf_len = MIC_CHUNK_SAMPLES,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num = PIN_MIC_BCLK,
    .ws_io_num = PIN_MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = PIN_MIC_DIN
  };

  esp_err_t result = i2s_driver_install(MIC_I2S_PORT, &config, 0, nullptr);
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE)
  {
    microphoneStatus = "I2S install gagal " + String((int)result);
    return false;
  }

  result = i2s_set_pin(MIC_I2S_PORT, &pins);
  if (result != ESP_OK)
  {
    microphoneStatus = "I2S pin gagal " + String((int)result);
    return false;
  }

  microphoneReady = true;
  microphoneStatus = "Siap - silakan bicara";
  Serial.println("[MIC] I2S siap: BCLK6 WS7 DIN15");
  return true;
}

void startMicrophoneTest()
{
  if (!microphoneReady && !setupMicrophone())
  {
    microphoneTestResult = microphoneStatus;
    return;
  }

  microphoneTestRunning = true;
  microphoneTestStartedMs = millis();
  microphoneTestMaxRms = 0;
  microphoneTestMaxPeak = 0;
  microphoneChunks = 0;
  microphoneErrors = 0;
  microphoneTestResult = "BICARA sekarang selama 4 detik...";
  setLed(55, 25, 0);
  drawOledText("TEST MICROPHONE", "Bicara 4 detik", "BCLK6 WS7 DIN15", "Mendengarkan...");
}

void serviceMicrophone()
{
  if (!microphoneReady) return;

  int32_t raw[MIC_CHUNK_SAMPLES];
  size_t bytesRead = 0;
  const esp_err_t result = i2s_read(MIC_I2S_PORT, raw, sizeof(raw), &bytesRead, 0);
  const size_t samples = bytesRead / sizeof(raw[0]);

  if (result == ESP_OK && samples > 0)
  {
    int64_t sum = 0;
    uint64_t sumSquares = 0;
    uint32_t peak = 0;
    for (size_t i = 0; i < samples; i++)
    {
      const int32_t value = raw[i] >> 16;
      const uint32_t magnitude = value < 0 ? (uint32_t)-value : (uint32_t)value;
      sum += value;
      sumSquares += (int64_t)value * value;
      if (magnitude > peak) peak = magnitude;
    }

    const double mean = (double)sum / samples;
    double variance = ((double)sumSquares / samples) - (mean * mean);
    if (variance < 0) variance = 0;
    microphoneRms = (uint32_t)sqrt(variance);
    microphonePeak = peak;
    microphoneChunks++;
    if (microphoneRms > microphoneTestMaxRms) microphoneTestMaxRms = microphoneRms;
    if (microphonePeak > microphoneTestMaxPeak) microphoneTestMaxPeak = microphonePeak;
    microphoneStatus = microphoneRms > MIC_SPEECH_RMS_THRESHOLD ? "Suara terdeteksi" : "Siap - menunggu suara";
  }
  else if (result != ESP_OK && result != ESP_ERR_TIMEOUT)
  {
    microphoneErrors++;
    microphoneStatus = "Read error " + String((int)result);
  }

  if (microphoneTestRunning && millis() - microphoneTestStartedMs >= MIC_TEST_DURATION_MS)
  {
    microphoneTestRunning = false;
    const bool ok = microphoneChunks > 5 && microphoneTestMaxRms >= MIC_SPEECH_RMS_THRESHOLD;
    microphoneTestResult = ok
      ? "OK - suara bicara terdeteksi"
      : "GAGAL - tidak ada suara, cek L/R=GND dan kabel";
    setLed(ok ? 0 : 70, ok ? 55 : 0, 0);
    drawOledText(ok ? "MIC TEST OK" : "MIC TEST GAGAL",
                 "RMS max " + String(microphoneTestMaxRms),
                 "Peak " + String(microphoneTestMaxPeak),
                 ok ? "Suara terdeteksi" : "Cek koneksi mic");
  }
}

bool setupAmplifier()
{
  if (amplifierReady) return true;

  amplifierStatus = "Initializing";
  amplifierMessage = "-";

  i2s_config_t i2sConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = AMP_SAMPLE_RATE,
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
    .bck_io_num = PIN_I2S_BCLK,
    .ws_io_num = PIN_I2S_LRC,
    .data_out_num = PIN_I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  esp_err_t result = i2s_driver_install(AMP_I2S_PORT, &i2sConfig, 0, nullptr);
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE)
  {
    amplifierReady = false;
    amplifierStatus = "I2S gagal";
    amplifierMessage = "driver_install error " + String((int)result);
    Serial.println("[AMP] " + amplifierMessage);
    return false;
  }

  result = i2s_set_pin(AMP_I2S_PORT, &pinConfig);
  if (result != ESP_OK)
  {
    amplifierReady = false;
    amplifierStatus = "I2S gagal";
    amplifierMessage = "set_pin error " + String((int)result);
    Serial.println("[AMP] " + amplifierMessage);
    return false;
  }

  i2s_zero_dma_buffer(AMP_I2S_PORT);
  amplifierReady = true;
  amplifierStatus = "I2S siap";
  amplifierMessage = "MAX98357A BCLK8 LRC9 DIN11";
  Serial.println("[AMP] I2S siap: BCLK8 LRC9 DIN11");
  return true;
}

bool writeAmplifierChunk(const int16_t *samples, uint16_t frames)
{
  size_t bytesWritten = 0;
  const size_t expectedBytes = frames * 2 * sizeof(int16_t);
  const esp_err_t result = i2s_write(AMP_I2S_PORT, samples, expectedBytes, &bytesWritten, pdMS_TO_TICKS(200));

  if (result != ESP_OK)
  {
    amplifierWriteErrors++;
    amplifierMessage = "i2s_write error " + String((int)result);
    return false;
  }

  if (bytesWritten != expectedBytes)
  {
    amplifierShortWrites++;
    amplifierMessage = "short write " + String(bytesWritten) + "/" + String(expectedBytes);
  }

  return true;
}

bool playAmplifierTone(float startFrequency, float endFrequency, uint16_t durationMs, uint8_t volumePercent)
{
  if (!setupAmplifier()) return false;

  int16_t chunk[AMP_CHUNK_FRAMES * 2];
  const uint32_t totalSamples = (AMP_SAMPLE_RATE * durationMs) / 1000;
  const float amplitude = 22000.0f * min(volumePercent, (uint8_t)100) / 100.0f;
  float phase = 0.0f;
  uint32_t sampleIndex = 0;

  while (sampleIndex < totalSamples)
  {
    uint16_t frames = 0;
    while (frames < AMP_CHUNK_FRAMES && sampleIndex < totalSamples)
    {
      const float progress = totalSamples > 1 ? (float)sampleIndex / (float)(totalSamples - 1) : 0.0f;
      const float frequency = startFrequency + ((endFrequency - startFrequency) * progress);
      float envelope = 1.0f;
      const uint32_t fadeSamples = AMP_SAMPLE_RATE / 100;
      if (sampleIndex < fadeSamples) envelope = (float)sampleIndex / (float)fadeSamples;
      else if (totalSamples - sampleIndex < fadeSamples) envelope = (float)(totalSamples - sampleIndex) / (float)fadeSamples;

      const int16_t sample = (int16_t)(sinf(phase) * amplitude * envelope);
      chunk[frames * 2] = sample;
      chunk[(frames * 2) + 1] = sample;
      phase += AMP_TWO_PI * frequency / AMP_SAMPLE_RATE;
      if (phase > AMP_TWO_PI) phase -= AMP_TWO_PI;
      frames++;
      sampleIndex++;
    }

    if (!writeAmplifierChunk(chunk, frames)) return false;
    yield();
  }

  i2s_zero_dma_buffer(AMP_I2S_PORT);
  return true;
}

bool playVoiceSample()
{
  if (!setupAmplifier()) return false;
  if (i2s_set_clk(AMP_I2S_PORT, 11025, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO) != ESP_OK)
  {
    amplifierMessage = "Gagal set sample rate suara";
    return false;
  }

  int16_t chunk[AMP_CHUNK_FRAMES * 2];
  size_t offset = 0;
  bool ok = true;
  while (offset < DEVICE_A_VOICE_U8_LEN)
  {
    const uint16_t frames = min((size_t)AMP_CHUNK_FRAMES, DEVICE_A_VOICE_U8_LEN - offset);
    for (uint16_t i = 0; i < frames; i++)
    {
      const int16_t sample = ((int16_t)pgm_read_byte(DEVICE_A_VOICE_U8 + offset + i) - 128) << 8;
      chunk[i * 2] = sample;
      chunk[(i * 2) + 1] = sample;
    }
    if (!writeAmplifierChunk(chunk, frames))
    {
      ok = false;
      break;
    }
    offset += frames;
    yield();
  }

  i2s_zero_dma_buffer(AMP_I2S_PORT);
  i2s_set_clk(AMP_I2S_PORT, AMP_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
  return ok;
}

bool runAmplifierTest(const String &action)
{
  setIrReceiverActive(false);
  setLed(0, 65, 40);
  amplifierTestCount++;

  String testName = action;
  bool ok = false;
  if (action == "tone" || action == "tone1000" || action.length() == 0)
  {
    testName = "Tone 1 kHz";
    drawOledText("AMP Test", "Tone 1 kHz", "BCLK8 LRC9 DIN11", "Dengarkan speaker");
    ok = playAmplifierTone(1000.0f, 1000.0f, 900, 35);
  }
  else if (action == "beep" || action == "beep3x")
  {
    testName = "Beep 3x";
    drawOledText("AMP Test", "Beep 3x", "BCLK8 LRC9 DIN11", "Dengarkan speaker");
    ok = true;
    for (uint8_t i = 0; i < 3; i++)
    {
      ok = playAmplifierTone(880.0f, 880.0f, 160, 40) && ok;
      delay(120);
    }
  }
  else if (action == "sweep")
  {
    testName = "Sweep";
    drawOledText("AMP Test", "Sweep 300-2500Hz", "BCLK8 LRC9 DIN11", "Dengarkan speaker");
    ok = playAmplifierTone(300.0f, 2500.0f, 1400, 35);
  }
  else if (action == "voice")
  {
    testName = "Ucapan Indonesia";
    drawOledText("SPEAKER TEST", "Memutar ucapan", "Perangkat siap", "Dengarkan speaker");
    ok = playVoiceSample();
  }
  else
  {
    amplifierStatus = "Aksi tidak dikenal";
    amplifierMessage = action;
    ok = false;
  }

  amplifierLastTest = testName;
  if (ok)
  {
    amplifierStatus = "Test OK";
    amplifierMessage = testName + " selesai";
    setLed(0, 45, 0);
    drawOledText("AMP Test OK", testName, "MAX98357A", "Selesai");
  }
  else
  {
    if (amplifierMessage.length() == 0) amplifierMessage = "Test gagal";
    setLed(70, 0, 0);
    drawOledText("AMP Test FAIL", testName, amplifierStatus, amplifierMessage);
  }

  Serial.println("[AMP] " + amplifierStatus + ": " + amplifierMessage);
  return ok;
}

void loadIrRaw(const char *lenKey, const char *rawKey, uint16_t *buffer, uint16_t &length)
{
  length = prefs.getUShort(lenKey, 0);
  if (length == 0 || length > IR_MAX_RAW_LEN)
  {
    length = 0;
    return;
  }

  const size_t expectedBytes = length * sizeof(uint16_t);
  if (prefs.getBytesLength(rawKey) != expectedBytes)
  {
    length = 0;
    return;
  }

  prefs.getBytes(rawKey, buffer, expectedBytes);
}

void saveIrRaw(const char *lenKey, const char *rawKey, const uint16_t *buffer, uint16_t length)
{
  prefs.putUShort(lenKey, length);
  prefs.putBytes(rawKey, buffer, length * sizeof(uint16_t));
}

void setIrReceiverActive(bool active)
{
  if (active == irReceiverActive) return;
  irReceiverActive = active;

  if (active)
  {
    pinMode(PIN_IR_RX, INPUT_PULLUP);
    irReceiver.enableIRIn(true);
  }
  else
  {
    irReceiver.disableIRIn();
    pinMode(PIN_IR_RX, INPUT_PULLUP);
  }
}

String irLearnLabel()
{
  if (irLearnSlot == IR_LEARN_ON) return "LEARN ON";
  if (irLearnSlot == IR_LEARN_OFF) return "LEARN OFF";
  return "IDLE";
}

void loadIrStorage()
{
  prefs.begin("simple-ir", false);
  devicePrefix = normalizePrefix(prefs.getString("prefix", DEFAULT_DEVICE_PREFIX));
  wifiStaSsid = prefs.getString("staSsid", "");
  wifiStaPassword = prefs.getString("staPass", "");
  countSession = prefs.getUInt("cntSess", 0);
  loadIrRaw("onLen", "onRaw", rawIrOn, rawIrOnLen);
  loadIrRaw("offLen", "offRaw", rawIrOff, rawIrOffLen);
  irStatus = "IR siap";
  irMessage = "Loaded ON " + String(rawIrOnLen) + ", OFF " + String(rawIrOffLen);
}

void saveDevicePrefix(const String &prefix)
{
  devicePrefix = normalizePrefix(prefix);
  prefs.putString("prefix", devicePrefix);
  updateMqttTopics();
}

void startIrLearn(IrLearnSlot slot)
{
  irLearnSlot = slot;
  irMessage = slot == IR_LEARN_ON
    ? "Arahkan remote lalu tekan tombol ON sekali"
    : "Arahkan remote lalu tekan tombol OFF sekali";
  irStatus = irLearnLabel();
  setIrReceiverActive(true);
  setLed(80, 45, 0);
  drawOledText("IR " + irStatus, "RX GPIO4", "Tekan remote asli", slot == IR_LEARN_ON ? "Belajar ON" : "Belajar OFF");
}

bool captureIrSignal()
{
  if (irLearnSlot == IR_LEARN_NONE || !irReceiverActive || !irReceiver.decode(&irResults))
  {
    return false;
  }

  if (irResults.overflow)
  {
    irMessage = "IR overflow. Coba dekatkan remote dan ulangi.";
    irReceiver.resume();
    return false;
  }

  if (irResults.rawlen <= IR_MIN_RAW_LEN)
  {
    irMessage = "IR terlalu pendek/noise. Ulangi tekan tombol.";
    irReceiver.resume();
    return false;
  }

  const uint16_t correctedLength = getCorrectedRawLength(&irResults);
  if (correctedLength == 0 || correctedLength > IR_MAX_RAW_LEN)
  {
    irMessage = "Raw IR terlalu panjang: " + String(correctedLength);
    irReceiver.resume();
    return false;
  }

  uint16_t *raw = resultToRawArray(&irResults);
  if (raw == nullptr)
  {
    irMessage = "Memori tidak cukup untuk raw IR";
    irReceiver.resume();
    return false;
  }

  if (irLearnSlot == IR_LEARN_ON)
  {
    memcpy(rawIrOn, raw, correctedLength * sizeof(uint16_t));
    rawIrOnLen = correctedLength;
    saveIrRaw("onLen", "onRaw", rawIrOn, rawIrOnLen);
    irMessage = "Berhasil simpan IR ON, len " + String(rawIrOnLen);
  }
  else
  {
    memcpy(rawIrOff, raw, correctedLength * sizeof(uint16_t));
    rawIrOffLen = correctedLength;
    saveIrRaw("offLen", "offRaw", rawIrOff, rawIrOffLen);
    irMessage = "Berhasil simpan IR OFF, len " + String(rawIrOffLen);
  }

  delete[] raw;
  irReceiver.resume();
  irLearnSlot = IR_LEARN_NONE;
  setIrReceiverActive(false);
  irStatus = "IR siap";
  setLed(0, 60, 0);
  drawOledText("IR tersimpan", "ON " + String(rawIrOnLen), "OFF " + String(rawIrOffLen), "Siap test kirim");
  return true;
}

bool sendIrRaw(const String &slot)
{
  const uint16_t *raw = nullptr;
  uint16_t length = 0;

  if (slot == "on")
  {
    raw = rawIrOn;
    length = rawIrOnLen;
  }
  else if (slot == "off")
  {
    raw = rawIrOff;
    length = rawIrOffLen;
  }

  if (raw == nullptr || length == 0)
  {
    irMessage = "IR " + slot + " belum direkam";
    return false;
  }

  setIrReceiverActive(false);
  setLed(0, 0, 80);
  irSender.sendRaw(raw, length, IR_SEND_KHZ);
  irStatus = "IR sent";
  irMessage = "Kirim IR " + slot + ", len " + String(length);
  drawOledText("Kirim IR " + slot, "TX GPIO10", "Len " + String(length), "38 kHz raw");
  delay(120);
  setLed(0, 25, 0);
  return true;
}

void clearIrStorage()
{
  prefs.remove("onLen");
  prefs.remove("onRaw");
  prefs.remove("offLen");
  prefs.remove("offRaw");
  rawIrOnLen = 0;
  rawIrOffLen = 0;
  irLearnSlot = IR_LEARN_NONE;
  setIrReceiverActive(false);
  irStatus = "IR cleared";
  irMessage = "Rekaman IR ON/OFF dihapus";
}

bool runIrTransmitterTest()
{
  setIrReceiverActive(false);
  pinMode(PIN_IR_RX, INPUT_PULLUP);
  setLed(80, 45, 0);
  drawOledText("Test IR TX", "Hadapkan TX ke RX", "GPIO10 -> GPIO4", "Mengirim sinyal");

  irTestTransitions = 0;
  irTestLowSamples = 0;
  irTestTotalSamples = 0;

  // First sniff the receiver idle state briefly, then transmit a known NEC frame.
  setIrReceiverActive(true);
  irReceiver.resume();
  delay(60);

  irSender.sendNEC(0x00FF00FFUL, 32, 2);
  delay(160);

  bool decoded = false;
  uint16_t decodedRawLen = 0;
  if (irReceiver.decode(&irResults))
  {
    decoded = irResults.rawlen > IR_MIN_RAW_LEN;
    decodedRawLen = irResults.rawlen;
    irReceiver.resume();
  }

  setIrReceiverActive(false);

  // Also sample GPIO4 for 1 second. This catches activity even if protocol decode fails.
  uint8_t lastLevel = digitalRead(PIN_IR_RX);
  const uint32_t startedAt = millis();

  while (millis() - startedAt < 1000)
  {
    const uint8_t level = digitalRead(PIN_IR_RX);
    if (level == LOW) irTestLowSamples++;
    if (level != lastLevel)
    {
      irTestTransitions++;
      lastLevel = level;
    }
    irTestTotalSamples++;
    delayMicroseconds(80);
    yield();
  }

  const bool stuckLow = irTestLowSamples == irTestTotalSamples;
  const bool stuckHigh = irTestLowSamples == 0;
  const bool detected = decoded || (irTestTransitions > 20 && !stuckLow && !stuckHigh);

  if (detected)
  {
    irTestResult = "OK: IR TX terdeteksi";
    setLed(0, 70, 0);
  }
  else if (stuckLow)
  {
    irTestResult = "Gagal: RX LOW terus";
    setLed(80, 0, 0);
  }
  else if (stuckHigh)
  {
    irTestResult = "Gagal: tidak ada sinyal TX";
    setLed(80, 0, 0);
  }
  else
  {
    irTestResult = "Gagal: sinyal TX lemah";
    setLed(80, 30, 0);
  }

  irStatus = "IR transmitter test";
  irMessage = irTestResult + ", raw " + String(decodedRawLen) + ", transisi " + String(irTestTransitions);
  drawOledText("IR TX Test", irTestResult.substring(0, 21), "Raw " + String(decodedRawLen), "Trans " + String(irTestTransitions));
  return detected;
}

void runIrTxBlinkTest()
{
  setIrReceiverActive(false);
  setLed(0, 0, 80);
  drawOledText("IR TX Blink", "GPIO10 kirim NEC", "Cek kamera HP", "3 detik");

  const uint32_t startedAt = millis();
  uint16_t sent = 0;
  while (millis() - startedAt < 3000)
  {
    irSender.sendNEC(0x00FF00FFUL, 32, 0);
    sent++;
    delay(120);
    yield();
  }

  irStatus = "IR TX blink";
  irMessage = "GPIO10 kirim " + String(sent) + " frame NEC 38 kHz";
  irTestResult = "IR TX blink selesai";
  setLed(0, 25, 0);
  drawOledText("IR TX Blink OK", "Frame " + String(sent), "GPIO10 38 kHz", "Selesai");
}

uint32_t combineU32(uint16_t firstWord, uint16_t secondWord)
{
  if (MODBUS_SWAP_32BIT_WORDS)
    return ((uint32_t)secondWord << 16) | firstWord;
  return ((uint32_t)firstWord << 16) | secondWord;
}

int32_t combineS32(uint16_t firstWord, uint16_t secondWord)
{
  return (int32_t)combineU32(firstWord, secondWord);
}

const char *modbusExceptionText(uint8_t code)
{
  switch (code)
  {
    case 0x01: return "Illegal Function";
    case 0x02: return "Illegal Data Address";
    case 0x03: return "Illegal Data Value";
    case 0x04: return "Slave Device Failure";
    case 0x05: return "Acknowledge";
    case 0x06: return "Slave Device Busy";
    case 0x08: return "Memory Parity Error";
    case 0x0A: return "Gateway Path Unavailable";
    case 0x0B: return "Gateway Target No Response";
    default: return "Unknown exception";
  }
}

void drawOledText(const String &line1, const String &line2, const String &line3, const String &line4)
{
  if (!oledReady) return;
  if (oledAddress == 0 || !i2cDevicePresent(oledAddress))
  {
    oledReady = false;
    return;
  }

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("DEVICE A");
  oled.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  oled.setCursor(0, 16); oled.println(line1.substring(0, 21));
  oled.setCursor(0, 28); oled.println(line2.substring(0, 21));
  oled.setCursor(0, 40); oled.println(line3.substring(0, 21));
  oled.setCursor(0, 52); oled.println(line4.substring(0, 21));
  oled.display();
}

void drawMeterOled()
{
  if (!oledReady) return;
  if (!meterData.valid)
  {
    drawOledText(
      "Meter: " + meterStatus,
      "2400 8E1 ID:1",
      "RX " + String(lastResponseBytes) + " byte",
      "ERR " + String(meterErrCount)
    );
    return;
  }

  drawOledText(
    "V " + String(meterData.voltage, 1) + " I " + String(meterData.current, 3),
    "P " + String(meterData.activePower, 3) + " kW",
    "PF " + String(meterData.powerFactor, 3) + " " + String(meterData.frequency, 2) + "Hz",
    "E " + String(meterData.totalActiveEnergy, 2) + " kWh"
  );
}

size_t readModbusFrame(uint8_t *buffer, size_t maxLength)
{
  size_t index = 0;
  size_t targetLength = 0;
  const uint32_t startedAt = millis();
  uint32_t lastByteAt = startedAt;

  while ((millis() - startedAt) < MODBUS_RESPONSE_TIMEOUT_MS)
  {
    while (meterSerial.available() && index < maxLength)
    {
      buffer[index++] = (uint8_t)meterSerial.read();
      lastByteAt = millis();

      if (index >= 2 && (buffer[1] & 0x80))
        targetLength = 5;
      else if (index >= 3 && buffer[1] == MODBUS_FUNCTION_READ)
        targetLength = 5 + buffer[2];

      if (targetLength > 0 && index >= targetLength)
        return index;
    }

    if (index > 0 && (millis() - lastByteAt) > MODBUS_INTERBYTE_TIMEOUT_MS)
      break;

    delay(1);
    yield();
  }

  return index;
}

bool validateAndParseMeterResponse(const uint8_t *response, size_t length)
{
  if (length < 5)
  {
    meterStatus = "Tidak ada response";
    meterErrorDetail = "RX hanya " + String(length) + " byte";
    return false;
  }

  const uint16_t receivedCrc = response[length - 2] | ((uint16_t)response[length - 1] << 8);
  const uint16_t calculatedCrc = modbusCrc(response, length - 2);

  if (receivedCrc != calculatedCrc)
  {
    meterStatus = "CRC ERROR";
    meterErrorDetail = "CRC RX=0x" + String(receivedCrc, HEX) + " CALC=0x" + String(calculatedCrc, HEX);
    return false;
  }

  if (response[0] != METER_MODBUS_SLAVE_ID)
  {
    meterStatus = "Slave ID salah";
    meterErrorDetail = "RX ID=" + String(response[0]) + ", expected=" + String(METER_MODBUS_SLAVE_ID);
    return false;
  }

  if (response[1] & 0x80)
  {
    const uint8_t exceptionCode = response[2];
    meterStatus = "Modbus Exception";
    meterErrorDetail = "Code 0x" + String(exceptionCode, HEX) + " - " + modbusExceptionText(exceptionCode);
    return false;
  }

  if (response[1] != MODBUS_FUNCTION_READ)
  {
    meterStatus = "Function salah";
    meterErrorDetail = "FC RX=0x" + String(response[1], HEX);
    return false;
  }

  const uint8_t byteCount = response[2];
  if (byteCount == 0 || (byteCount % 2) != 0)
  {
    meterStatus = "Byte count salah";
    meterErrorDetail = "RX=" + String(byteCount) + ", harus genap";
    return false;
  }

  const uint16_t registerCount = byteCount / 2;
  if (registerCount < 13)
  {
    meterStatus = "Register kurang";
    meterErrorDetail = "RX=" + String(registerCount) + " register, minimal 13";
    return false;
  }

  if (registerCount > MODBUS_REGISTER_COUNT)
  {
    meterStatus = "Register terlalu banyak";
    meterErrorDetail = "RX=" + String(registerCount) + ", max parser=" + String(MODBUS_REGISTER_COUNT);
    return false;
  }

  const size_t expectedLength = 5 + byteCount;
  if (length != expectedLength)
  {
    meterStatus = "Panjang frame salah";
    meterErrorDetail = "RX=" + String(length) + ", expected=" + String(expectedLength);
    return false;
  }

  for (uint16_t i = 0; i < MODBUS_REGISTER_COUNT; i++)
  {
    lastRegisters[i] = 0;
  }

  for (uint16_t i = 0; i < registerCount; i++)
  {
    const size_t offset = 3 + (i * 2);
    lastRegisters[i] = ((uint16_t)response[offset] << 8) | response[offset + 1];
  }

  lastRegistersHex = registersToHex(lastRegisters, registerCount);

  meterData.totalActiveEnergy = combineS32(lastRegisters[0], lastRegisters[1]) * 0.01f;
  meterData.forwardEnergy = combineU32(lastRegisters[2], lastRegisters[3]) * 0.01f;
  meterData.reverseEnergy = combineU32(lastRegisters[4], lastRegisters[5]) * 0.01f;
  meterData.voltage = lastRegisters[6] * 0.1f;
  meterData.current = combineU32(lastRegisters[7], lastRegisters[8]) * 0.001f;
  meterData.activePower = combineS32(lastRegisters[9], lastRegisters[10]) * 0.0001f;
  meterData.powerFactor = ((int16_t)lastRegisters[11]) * 0.001f;
  meterData.frequency = lastRegisters[12] * 0.01f;
  meterData.reactivePower = registerCount >= 15 ? combineS32(lastRegisters[13], lastRegisters[14]) * 0.0001f : 0.0f;
  meterData.apparentPower = registerCount >= 17 ? combineU32(lastRegisters[15], lastRegisters[16]) * 0.0001f : 0.0f;

  meterData.valid = true;
  meterData.lastSuccessMs = millis();
  updateEnergySession();
  meterStatus = "ONLINE";
  meterErrorDetail = "RX " + String(registerCount) + " register";
  return true;
}

bool readMeter()
{
  const uint32_t pollStarted = millis();
  while (meterSerial.available()) meterSerial.read();

  uint8_t request[8];
  request[0] = METER_MODBUS_SLAVE_ID;
  request[1] = MODBUS_FUNCTION_READ;
  request[2] = highByte(MODBUS_START_REGISTER);
  request[3] = lowByte(MODBUS_START_REGISTER);
  request[4] = highByte(MODBUS_REGISTER_COUNT);
  request[5] = lowByte(MODBUS_REGISTER_COUNT);

  const uint16_t crc = modbusCrc(request, 6);
  request[6] = lowByte(crc);
  request[7] = highByte(crc);
  lastRequestHex = bytesToHex(request, sizeof(request));

  meterSerial.write(request, sizeof(request));
  meterSerial.flush();

  uint8_t response[96] = {0};
  const size_t responseLength = readModbusFrame(response, sizeof(response));

  lastResponseBytes = (uint8_t)min(responseLength, (size_t)255);
  lastResponseHex = responseLength > 0 ? bytesToHex(response, responseLength) : "-";

  const bool ok = validateAndParseMeterResponse(response, responseLength);
  lastPollDurationMs = millis() - pollStarted;

  if (ok)
  {
    meterOkCount++;
    publishMeterMqtt();
    setLed(0, 35, 0);
    Serial.println();
    Serial.println("[MODBUS] OK");
    Serial.println("[MODBUS] TX: " + lastRequestHex);
    Serial.println("[MODBUS] RX: " + lastResponseHex);
    Serial.printf("[METER] V=%.1fV I=%.3fA P=%.4fkW PF=%.3f F=%.2fHz E=%.2fkWh\n",
      meterData.voltage, meterData.current, meterData.activePower,
      meterData.powerFactor, meterData.frequency, meterData.totalActiveEnergy);
  }
  else
  {
    meterErrCount++;
    setLed(45, 0, 0);
    Serial.println();
    Serial.println("[MODBUS] ERROR: " + meterStatus);
    Serial.println("[MODBUS] DETAIL: " + meterErrorDetail);
    Serial.println("[MODBUS] TX: " + lastRequestHex);
    Serial.println("[MODBUS] RX: " + lastResponseHex);
  }

  drawMeterOled();
  return ok;
}

String statusJson()
{
  const uint32_t ageMs = meterData.valid ? millis() - meterData.lastSuccessMs : 0;
  const bool meterOnline = meterData.valid && ageMs < 10000;

  String json = "{";
  json += "\"ip\":\"" + WiFi.softAPIP().toString() + "\",";
  json += "\"apIp\":\"" + WiFi.softAPIP().toString() + "\",";
  json += "\"staIp\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"ssid\":\"" + String(AP_SSID) + "\",";
  json += "\"staSsid\":\"" + jsonEscape(wifiStaSsid) + "\",";
  json += "\"staPasswordSet\":" + String(wifiStaPassword.length() > 0 ? "true" : "false") + ",";
  json += "\"staPasswordLength\":" + String(wifiStaPassword.length()) + ",";
  json += "\"wifiConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"wifiStatus\":\"" + jsonEscape(wifiStaStatus) + "\",";
  json += "\"wifiRssi\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0) + ",";
  json += "\"modbusPolling\":" + String(meterPollingAllowed() ? "true" : "false") + ",";
  json += "\"devicePrefix\":\"" + jsonEscape(devicePrefix) + "\",";
  json += "\"mqttConnected\":" + String(mqttClient.connected() ? "true" : "false") + ",";
  json += "\"mqttHost\":\"" + String(MQTT_HOST) + "\",";
  json += "\"mqttPort\":" + String(MQTT_PORT) + ",";
  json += "\"mqttDeviceId\":\"" + jsonEscape(mqttDeviceId) + "\",";
  json += "\"mqttStateTopic\":\"" + jsonEscape(mqttStateTopic) + "\",";
  json += "\"mqttStatus\":\"" + jsonEscape(mqttStatus) + "\",";
  json += "\"mqttOk\":" + String(mqttOkCount) + ",";
  json += "\"mqttErr\":" + String(mqttErrCount) + ",";
  json += "\"mqttLastPayload\":\"" + jsonEscape(lastMqttPayload) + "\",";
  json += "\"stations\":" + String(WiFi.softAPgetStationNum()) + ",";
  json += "\"oledReady\":" + String(oledReady ? "true" : "false") + ",";
  String oledAddressText = "-";
  if (oledAddress > 0) oledAddressText = "0x" + String(oledAddress, HEX);
  json += "\"oledAddress\":\"" + oledAddressText + "\",";
  json += "\"oledStatusDetail\":\"" + jsonEscape(oledStatusDetail) + "\",";
  json += "\"i2cScan\":\"" + jsonEscape(i2cScanResult) + "\",";
  json += "\"microphoneReady\":" + String(microphoneReady ? "true" : "false") + ",";
  json += "\"microphoneTestRunning\":" + String(microphoneTestRunning ? "true" : "false") + ",";
  json += "\"microphoneStatus\":\"" + jsonEscape(microphoneStatus) + "\",";
  json += "\"microphoneTestResult\":\"" + jsonEscape(microphoneTestResult) + "\",";
  json += "\"microphoneRms\":" + String(microphoneRms) + ",";
  json += "\"microphonePeak\":" + String(microphonePeak) + ",";
  json += "\"microphoneMaxRms\":" + String(microphoneTestMaxRms) + ",";
  json += "\"microphoneMaxPeak\":" + String(microphoneTestMaxPeak) + ",";
  json += "\"microphoneChunks\":" + String(microphoneChunks) + ",";
  json += "\"microphoneErrors\":" + String(microphoneErrors) + ",";
  json += "\"amplifierReady\":" + String(amplifierReady ? "true" : "false") + ",";
  json += "\"amplifierStatus\":\"" + jsonEscape(amplifierStatus) + "\",";
  json += "\"amplifierMessage\":\"" + jsonEscape(amplifierMessage) + "\",";
  json += "\"amplifierLastTest\":\"" + jsonEscape(amplifierLastTest) + "\",";
  json += "\"amplifierTestCount\":" + String(amplifierTestCount) + ",";
  json += "\"amplifierWriteErrors\":" + String(amplifierWriteErrors) + ",";
  json += "\"amplifierShortWrites\":" + String(amplifierShortWrites) + ",";
  json += "\"irTxReady\":true,";
  json += "\"meterOnline\":" + String(meterOnline ? "true" : "false") + ",";
  json += "\"meterStatus\":\"" + jsonEscape(meterStatus) + "\",";
  json += "\"meterError\":\"" + jsonEscape(meterErrorDetail) + "\",";
  json += "\"irStatus\":\"" + jsonEscape(irStatus) + "\",";
  json += "\"irMessage\":\"" + jsonEscape(irMessage) + "\",";
  json += "\"irLearn\":\"" + jsonEscape(irLearnLabel()) + "\",";
  json += "\"irOnLen\":" + String(rawIrOnLen) + ",";
  json += "\"irOffLen\":" + String(rawIrOffLen) + ",";
  json += "\"irTestResult\":\"" + jsonEscape(irTestResult) + "\",";
  json += "\"irTestTransitions\":" + String(irTestTransitions) + ",";
  json += "\"irTestLowSamples\":" + String(irTestLowSamples) + ",";
  json += "\"irTestTotalSamples\":" + String(irTestTotalSamples) + ",";
  json += "\"meterOk\":" + String(meterOkCount) + ",";
  json += "\"meterErr\":" + String(meterErrCount) + ",";
  json += "\"pollMs\":" + String(lastPollDurationMs) + ",";
  json += "\"ageMs\":" + String(ageMs) + ",";
  json += "\"totalEnergy\":" + String(meterData.totalActiveEnergy, 2) + ",";
  json += "\"EnergySession\":" + String(energySessionKwh, 4) + ",";
  json += "\"CountSession\":" + String(countSession) + ",";
  json += "\"energySessionActive\":" + String(energySessionActive ? "true" : "false") + ",";
  json += "\"forwardEnergy\":" + String(meterData.forwardEnergy, 2) + ",";
  json += "\"reverseEnergy\":" + String(meterData.reverseEnergy, 2) + ",";
  json += "\"voltage\":" + String(meterData.voltage, 1) + ",";
  json += "\"current\":" + String(meterData.current, 3) + ",";
  json += "\"activePower\":" + String(meterData.activePower, 4) + ",";
  json += "\"powerFactor\":" + String(meterData.powerFactor, 3) + ",";
  json += "\"frequency\":" + String(meterData.frequency, 2) + ",";
  json += "\"reactivePower\":" + String(meterData.reactivePower, 4) + ",";
  json += "\"apparentPower\":" + String(meterData.apparentPower, 4) + ",";
  json += "\"lastRequestHex\":\"" + jsonEscape(lastRequestHex) + "\",";
  json += "\"lastResponseHex\":\"" + jsonEscape(lastResponseHex) + "\",";
  json += "\"lastRegistersHex\":\"" + jsonEscape(lastRegistersHex) + "\",";
  json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"sketchSize\":" + String(ESP.getSketchSize()) + ",";
  json += "\"freeSketchSpace\":" + String(ESP.getFreeSketchSpace()) + ",";
  json += "\"lastResponseBytes\":" + String(lastResponseBytes);
  json += "}";
  return json;
}

const char *pageHtml()
{
  return WEB_UI_HTML;
#if 0
  return R"HTML(
<!doctype html>
<html lang="id">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <meta name="theme-color" content="#0b1728">
  <title>Device A • Energy Monitor</title>
  <style>
    :root{--bg:#07111f;--panel:#0d1b2d;--panel2:#12243a;--border:#203751;--text:#f4f7fb;--muted:#8fa3b8;--green:#40d98b;--red:#ff6470;--blue:#55a7ff;--orange:#ffbd66;--shadow:0 18px 40px rgba(0,0,0,.20)}
    *{box-sizing:border-box}
    body{margin:0;font-family:Inter,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:radial-gradient(circle at 10% 0%,rgba(50,100,160,.22),transparent 32%),var(--bg);color:var(--text)}
    main{width:min(1180px,100%);margin:auto;padding:22px}
    header{display:flex;justify-content:space-between;align-items:flex-start;gap:18px;margin-bottom:22px}
    h1{margin:0;font-size:clamp(26px,4vw,42px);letter-spacing:-.04em}.sub{color:var(--muted);margin-top:6px;font-size:14px}
    .badge{display:inline-flex;align-items:center;gap:8px;padding:8px 12px;border-radius:999px;background:#17253a;color:var(--muted);border:1px solid var(--border);font-size:13px;font-weight:700;white-space:nowrap}.dot{width:9px;height:9px;border-radius:50%;background:var(--red);box-shadow:0 0 14px rgba(255,100,112,.7)}.badge.online .dot{background:var(--green);box-shadow:0 0 14px rgba(64,217,139,.8)}
    .grid{display:grid;grid-template-columns:repeat(12,1fr);gap:14px}.card{background:linear-gradient(180deg,var(--panel2),var(--panel));border:1px solid var(--border);border-radius:18px;box-shadow:var(--shadow);padding:18px}.metric{grid-column:span 3}.half{grid-column:span 6}.full{grid-column:1/-1}
    .label{color:var(--muted);text-transform:uppercase;letter-spacing:.09em;font-size:11px;font-weight:800}.value{margin-top:10px;font-weight:800;font-size:clamp(26px,4vw,38px);letter-spacing:-.035em}.unit{color:var(--muted);font-size:.46em;font-weight:700;margin-left:4px}.mini{margin-top:8px;color:var(--muted);font-size:13px}.section-title{margin:0 0 14px;font-size:17px}
    button{appearance:none;border:0;border-radius:12px;background:var(--blue);color:#06101c;min-height:44px;padding:0 15px;font-size:14px;font-weight:850;cursor:pointer}button.secondary{background:#1a304b;border:1px solid #2b4a69;color:var(--text)}button.danger{background:#472033;border:1px solid #793550;color:var(--text)}button:hover{filter:brightness(1.08)}.actions{display:flex;flex-wrap:wrap;gap:10px}.config-form{display:grid;grid-template-columns:minmax(180px,1fr) minmax(180px,1fr) auto;gap:10px;align-items:end}.config-form.compact{grid-template-columns:minmax(180px,1fr) auto}.field label{display:block;color:var(--muted);text-transform:uppercase;letter-spacing:.09em;font-size:11px;font-weight:800;margin-bottom:8px}input{width:100%;min-height:44px;border-radius:12px;border:1px solid var(--border);background:#07111f;color:var(--text);padding:0 12px;font-size:15px;font-weight:750;outline:0}input:focus{border-color:var(--blue);box-shadow:0 0 0 3px rgba(85,167,255,.18)}
    .statrow{display:grid;grid-template-columns:1fr auto;gap:10px;padding:10px 0;border-bottom:1px solid rgba(255,255,255,.06);font-size:14px}.statrow:last-child{border-bottom:0}.statrow span:first-child{color:var(--muted)}.meter{height:12px;background:#07111f;border:1px solid var(--border);border-radius:999px;overflow:hidden;margin:12px 0}.meter>div{height:100%;width:0;background:linear-gradient(90deg,var(--green),var(--orange),var(--red));transition:width .15s}.test-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}.test-item{padding:12px;background:#091626;border:1px solid var(--border);border-radius:12px}.test-item span{display:block;color:var(--muted);font-size:12px;margin-bottom:5px}.test-item strong{font-size:13px}.busy{opacity:.6;pointer-events:none}
    code{display:block;white-space:pre-wrap;overflow-wrap:anywhere;background:#07111f;border:1px solid var(--border);border-radius:12px;padding:12px;color:#a8d3ff;font-size:12px;line-height:1.55}details summary{cursor:pointer;font-weight:800;margin-bottom:12px}
    @media(max-width:780px){main{padding:14px}header{flex-direction:column}.metric,.half{grid-column:1/-1}.metric{display:grid;grid-template-columns:1fr auto;align-items:center}.metric .value{margin:0;text-align:right}.metric .mini{grid-column:1/-1}.config-form,.config-form.compact,.test-grid{grid-template-columns:1fr}}
  </style>
</head>
<body>
<main>
<header><div><h1>Device A Energy Monitor</h1><div class="sub">ESP32-S3 • DDS3366D-1P • RS485 Modbus RTU • Prefix <span id="prefixText">-</span></div></div><div id="statusBadge" class="badge"><span class="dot"></span><span id="statusText">Menghubungkan...</span></div></header>
<section class="grid">
  <article class="card metric"><div><div class="label">Voltage</div><div id="voltage" class="value">--</div></div><div class="mini">Register 6 • gain 0.1</div></article>
  <article class="card metric"><div><div class="label">Current</div><div id="current" class="value">--</div></div><div class="mini">Register 7–8 • gain 0.001</div></article>
  <article class="card metric"><div><div class="label">Active Power</div><div id="power" class="value">--</div></div><div class="mini">Register 9–10 • signed</div></article>
  <article class="card metric"><div><div class="label">Power Factor</div><div id="pf" class="value">--</div></div><div class="mini">Register 11 • gain 0.001</div></article>
  <article class="card metric"><div><div class="label">Frequency</div><div id="frequency" class="value">--</div></div><div class="mini">Register 12</div></article>
  <article class="card metric"><div><div class="label">Total Energy</div><div id="energy" class="value">--</div></div><div class="mini">Combined active energy</div></article>
  <article class="card metric"><div><div class="label">Reactive Power</div><div id="reactive" class="value">--</div></div><div class="mini">Register 13–14</div></article>
  <article class="card metric"><div><div class="label">Apparent Power</div><div id="apparent" class="value">--</div></div><div class="mini">Register 15–16</div></article>

  <article class="card half"><h2 class="section-title">Energy Counter</h2><div class="statrow"><span>Total active</span><strong id="totalEnergy">--</strong></div><div class="statrow"><span>Forward active</span><strong id="forwardEnergy">--</strong></div><div class="statrow"><span>Reverse active</span><strong id="reverseEnergy">--</strong></div></article>

  <article class="card half"><h2 class="section-title">Komunikasi</h2><div class="statrow"><span>Internet WiFi</span><strong id="wifiStatus">--</strong></div><div class="statrow"><span>SSID target</span><strong id="staTarget">--</strong></div><div class="statrow"><span>Password WiFi</span><strong id="staPassStatus">--</strong></div><div class="statrow"><span>IP internet</span><strong id="staIp">--</strong></div><div class="statrow"><span>MQTT</span><strong id="mqttStatus">--</strong></div><div class="statrow"><span>MQTT topic</span><strong id="mqttTopic">--</strong></div><div class="statrow"><span>Modbus auto</span><strong id="modbusPolling">--</strong></div><div class="statrow"><span>OLED</span><strong id="oledStatus">--</strong></div><div class="statrow"><span>Microphone</span><strong id="microphoneStatus">--</strong></div><div class="statrow"><span>Amplifier</span><strong id="amplifierStatus">--</strong></div><div class="statrow"><span>IR transmitter</span><strong id="irTxReady">--</strong></div><div class="statrow"><span>Modbus</span><strong>2400 bps • 8E1 • Slave 1</strong></div><div class="statrow"><span>ESP32 pins</span><strong>RX18 • TX17 • auto direction</strong></div><div class="statrow"><span>Response</span><strong id="responseStatus">--</strong></div><div class="statrow"><span>OK / Error</span><strong id="counter">--</strong></div><div class="statrow"><span>Memori bebas</span><strong id="memoryStatus">--</strong></div></article>

  <article class="card full" id="allTests"><h2 class="section-title">Uji Keseluruhan Perangkat</h2><div class="actions"><button id="runAllButton" onclick="runAllTests()">Jalankan Semua Uji</button></div><div class="test-grid" style="margin-top:14px"><div class="test-item"><span>1. LED RGB</span><strong id="testLed">Belum diuji</strong></div><div class="test-item"><span>2. OLED / LCD</span><strong id="testOled">Belum diuji</strong></div><div class="test-item"><span>3. Microphone</span><strong id="testMic">Belum diuji</strong></div><div class="test-item"><span>4. Speaker bicara</span><strong id="testSpeaker">Belum diuji</strong></div><div class="test-item"><span>5. Infrared</span><strong id="testIr">Belum diuji</strong></div><div class="test-item"><span>6. Modbus</span><strong id="testModbus">Belum diuji</strong></div></div><div class="mini">Saat tes microphone dimulai, ucapkan sesuatu dekat microphone selama 4 detik. Tes IR memerlukan transmitter diarahkan ke receiver.</div></article>

  <article class="card full"><h2 class="section-title">Microphone I2S</h2><div class="actions"><button onclick="micTest()">Mulai Uji Bicara 4 Detik</button></div><div class="meter"><div id="micBar"></div></div><div class="statrow"><span>Level saat ini</span><strong id="micLevel">--</strong></div><div class="statrow"><span>Hasil uji</span><strong id="micResult">--</strong></div><div class="mini">BCLK GPIO6 • WS/LRC GPIO7 • SD/DATA GPIO15 • L/R ke GND (channel kiri).</div></article>

  <article class="card full"><h2 class="section-title">Kontrol & Test Manual</h2><div class="actions"><button onclick="readMeterNow()">Baca Meter Sekarang</button><button onclick="ampTest('voice')">Speaker Bicara</button><button class="secondary" onclick="ampTest('tone')">AMP 1 kHz</button><button class="secondary" onclick="ampTest('beep')">AMP Beep 3x</button><button class="secondary" onclick="ampTest('sweep')">AMP Sweep</button><button class="secondary" onclick="led('red')">LED Merah</button><button class="secondary" onclick="led('green')">LED Hijau</button><button class="secondary" onclick="led('blue')">LED Biru</button><button class="secondary" onclick="led('off')">LED Mati</button></div><div class="statrow"><span>Amplifier test</span><strong id="amplifierTest">--</strong></div><div class="mini">Ucapan Indonesia tersimpan di flash dan tetap bekerja tanpa internet. MAX98357A: BCLK GPIO8, LRC/WS GPIO9, DIN GPIO11.</div></article>

  <article class="card full"><h2 class="section-title">Internet WiFi</h2><form class="config-form" onsubmit="saveWifi(event)"><div class="field"><label for="wifiSsidInput">SSID Router</label><input id="wifiSsidInput" name="ssid" maxlength="32" autocomplete="off" placeholder="Belum diset"></div><div class="field"><label for="wifiPassInput">Password</label><input id="wifiPassInput" name="password" type="password" maxlength="64" autocomplete="off" placeholder="Kosongkan untuk pakai password tersimpan"></div><button type="submit">Connect</button></form><div class="actions" style="margin-top:10px"><button class="secondary" onclick="wifiReconnect()">Reconnect</button><button class="danger" onclick="wifiClear()">Hapus WiFi</button></div><div class="mini">AP konfigurasi tetap aktif: <strong>AQU-AC-Remote</strong>. Isi password hanya saat pertama kali atau saat ingin mengganti password.</div></article>

  <article class="card full"><h2 class="section-title">Prefix</h2><form class="config-form compact" onsubmit="savePrefix(event)"><div class="field"><label for="prefixInput">Device / Topic Prefix</label><input id="prefixInput" name="prefix" maxlength="48" autocomplete="off" placeholder="hems/ac"></div><button type="submit">Simpan Prefix</button></form><div class="mini">Karakter yang disimpan: huruf, angka, garis miring, minus, dan underscore. Contoh: hems/ac/lantai1</div></article>

  <article class="card full"><h2 class="section-title">Infrared Remote</h2><div class="actions"><button onclick="irLearn('on')">Learn ON</button><button onclick="irLearn('off')">Learn OFF</button><button class="secondary" onclick="irSend('on')">Send ON</button><button class="secondary" onclick="irSend('off')">Send OFF</button><button class="secondary" onclick="irTxTest()">Test TX</button><button class="secondary" onclick="irBlink()">IR Blink 3s</button><button class="secondary" onclick="irClear()">Clear IR</button></div><div class="statrow"><span>Mode</span><strong id="irLearn">--</strong></div><div class="statrow"><span>Rekaman ON / OFF</span><strong id="irLens">--</strong></div><div class="statrow"><span>Status</span><strong id="irStatus">--</strong></div><div class="statrow"><span>Test TX</span><strong id="irTxTest">--</strong></div><div class="mini">IR receiver OUT→GPIO4, IR transmitter SIG→GPIO10. Untuk Test TX, hadapkan LED transmitter ke receiver dari jarak 2-10 cm.</div></article>

  <article class="card full"><details><summary>Diagnostik Modbus</summary><div class="statrow"><span>Status</span><strong id="detailStatus">--</strong></div><div class="statrow"><span>Error detail</span><strong id="errorDetail">--</strong></div><p class="label">TX</p><code id="tx">-</code><p class="label">RX</p><code id="rx">-</code><p class="label">Raw registers</p><code id="registers">-</code></details></article>

  <article class="card full"><div class="mini">Jika RX selalu kosong: cek A/B RS485 ke meter, VCC/GND converter, TXD modul→GPIO18, RXD modul→GPIO17, Slave ID 1, serta pastikan meter menggunakan 2400 bps even parity.</div></article>
</section>
</main>
<script>
const $=id=>document.getElementById(id);function n(v,d){const x=Number(v);return Number.isFinite(x)?x.toFixed(d):'--'}function setMetric(id,value,unit){$(id).innerHTML=value+`<span class="unit">${unit}</span>`}
async function refresh(){try{const response=await fetch('/status',{cache:'no-store'});const d=await response.json();const online=!!d.meterOnline;$('statusBadge').className=online?'badge online':'badge';$('statusText').textContent=online?'Meter Online':d.meterStatus;$('prefixText').textContent=d.devicePrefix||'-';if(document.activeElement!==$('prefixInput'))$('prefixInput').value=d.devicePrefix||'';if(document.activeElement!==$('wifiSsidInput'))$('wifiSsidInput').value=d.staSsid||'';$('wifiStatus').textContent=d.wifiConnected?`Connected ${d.wifiRssi} dBm`:d.wifiStatus;$('staTarget').textContent=d.staSsid||'Belum diset';$('staPassStatus').textContent=d.staPasswordSet?`Tersimpan (${d.staPasswordLength} char)`:'Belum diset';$('staIp').textContent=d.wifiConnected?d.staIp:'-';$('mqttStatus').textContent=d.mqttConnected?`Connected, pub ${d.mqttOk}/${d.mqttErr}`:d.mqttStatus;$('mqttTopic').textContent=d.mqttStateTopic||'-';$('modbusPolling').textContent=d.modbusPolling?'Aktif':'Ditahan sampai internet';$('oledStatus').textContent=d.oledReady?`${d.oledStatusDetail} (${d.oledAddress})`:d.oledStatusDetail;$('i2cScan').textContent=d.i2cScan;$('microphoneStatus').textContent=d.microphoneReady?d.microphoneStatus:'Tidak siap';$('micLevel').textContent=`RMS ${d.microphoneRms} • peak ${d.microphonePeak}`;$('micResult').textContent=d.microphoneTestResult;$('micBar').style.width=`${Math.min(100,Math.sqrt(Math.max(0,d.microphoneRms))*3)}%`;$('amplifierStatus').textContent=d.amplifierReady?d.amplifierStatus:`${d.amplifierStatus}: ${d.amplifierMessage}`;$('amplifierTest').textContent=`${d.amplifierLastTest} | ${d.amplifierMessage} | err ${d.amplifierWriteErrors}/${d.amplifierShortWrites}`;$('irTxReady').textContent=d.irTxReady?'GPIO10 siap':'Tidak siap';$('memoryStatus').textContent=`Heap ${Math.round(d.freeHeap/1024)} KB • Flash bebas ${Math.round(d.freeSketchSpace/1024)} KB`;setMetric('voltage',n(d.voltage,1),'V');setMetric('current',n(d.current,3),'A');setMetric('power',n(d.activePower,4),'kW');setMetric('pf',n(d.powerFactor,3),'');setMetric('frequency',n(d.frequency,2),'Hz');setMetric('energy',n(d.totalEnergy,2),'kWh');setMetric('reactive',n(d.reactivePower,4),'kvar');setMetric('apparent',n(d.apparentPower,4),'kVA');$('totalEnergy').textContent=`${n(d.totalEnergy,2)} kWh`;$('forwardEnergy').textContent=`${n(d.forwardEnergy,2)} kWh`;$('reverseEnergy').textContent=`${n(d.reverseEnergy,2)} kWh`;$('responseStatus').textContent=`RX ${d.lastResponseBytes} byte`;$('counter').textContent=`${d.meterOk} OK / ${d.meterErr} ERR`;$('detailStatus').textContent=d.meterStatus;$('errorDetail').textContent=d.meterError;$('tx').textContent=d.lastRequestHex;$('rx').textContent=d.lastResponseHex;$('registers').textContent=d.lastRegistersHex;$('irLearn').textContent=d.irLearn;$('irLens').textContent=`ON ${d.irOnLen} / OFF ${d.irOffLen}`;$('irStatus').textContent=`${d.irStatus}: ${d.irMessage}`;$('irTxTest').textContent=`${d.irTestResult} (${d.irTestTransitions} transisi)`;return d}catch(err){$('statusBadge').className='badge';$('statusText').textContent='UI disconnected';return null}}
const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));async function readMeterNow(){await fetch('/meter-read',{cache:'no-store'});await refresh()}async function testOLED(){await fetch('/oled-test',{cache:'no-store'});await refresh()}async function scanI2C(){await fetch('/i2c-scan',{cache:'no-store'});await refresh()}async function reinitOLED(){await fetch('/oled-init',{cache:'no-store'});await refresh()}async function micTest(){await fetch('/mic-test',{cache:'no-store'});await refresh()}async function ampTest(action){$('amplifierTest').textContent='Testing...';await fetch(`/amp-test?action=${action}`,{cache:'no-store'});await refresh()}async function led(color){await fetch(`/led?color=${color}`,{cache:'no-store'})}async function runAllTests(){const b=$('runAllButton');b.disabled=true;$('allTests').classList.add('busy');try{$('testLed').textContent='Merah → hijau → biru';for(const c of ['red','green','blue']){await led(c);await wait(450)}$('testLed').textContent='Selesai (cek visual)';$('testOled').textContent='Menguji...';await testOLED();$('testOled').textContent='Selesai (cek layar)';$('testMic').textContent='BICARA SEKARANG...';await micTest();await wait(4500);let d=await refresh();$('testMic').textContent=d?.microphoneTestResult||'Gagal membaca status';$('testSpeaker').textContent='Dengarkan ucapan...';await ampTest('voice');$('testSpeaker').textContent='Selesai (cek suara)';$('testIr').textContent='Menguji loop TX/RX...';await irTxTest();d=await refresh();$('testIr').textContent=d?.irTestResult||'Gagal membaca status';$('testModbus').textContent='Membaca meter...';await readMeterNow();d=await refresh();$('testModbus').textContent=d?.meterOnline?`OK • ${d.voltage} V`:d?.meterStatus||'Gagal'}finally{await led('off');b.disabled=false;$('allTests').classList.remove('busy')}}async function saveWifi(event){event.preventDefault();const body=new URLSearchParams();body.set('ssid',$('wifiSsidInput').value);body.set('password',$('wifiPassInput').value);const response=await fetch('/wifi-save',{method:'POST',body,cache:'no-store'});const d=await response.json();$('wifiPassInput').value='';$('staPassStatus').textContent=d.staPasswordSet?`Tersimpan (${d.staPasswordLength} char)`:'Belum diset';await refresh()}async function wifiReconnect(){await fetch('/wifi-reconnect',{cache:'no-store'});await refresh()}async function wifiClear(){await fetch('/wifi-clear',{cache:'no-store'});$('wifiPassInput').value='';await refresh()}async function savePrefix(event){event.preventDefault();const prefix=encodeURIComponent($('prefixInput').value);await fetch(`/prefix-save?value=${prefix}`,{cache:'no-store'});await refresh()}async function irLearn(slot){await fetch(`/ir-learn?slot=${slot}`,{cache:'no-store'});await refresh()}async function irSend(slot){await fetch(`/ir-send?slot=${slot}`,{cache:'no-store'});await refresh()}async function irTxTest(){$('irTxTest').textContent='Testing...';await fetch('/ir-tx-test',{cache:'no-store'});await refresh()}async function irBlink(){$('irTxTest').textContent='Blink 3 detik...';await fetch('/ir-blink',{cache:'no-store'});await refresh()}async function irClear(){await fetch('/ir-clear',{cache:'no-store'});await refresh()}refresh();setInterval(refresh,1000);
</script>
</body>
</html>
)HTML";
#endif
}

void sendStatus()
{
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", statusJson());
}

void setupWebServer()
{
  server.on("/", [](){ server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "text/html", pageHtml()); });
  server.on("/status", sendStatus);
  server.on("/meter-read", [](){
    if (meterPollingAllowed())
    {
      readMeter();
    }
    else
    {
      meterStatus = "Menunggu internet";
      meterErrorDetail = "Modbus ditahan sampai WiFi STA connected";
    }
    sendStatus();
  });
  server.on("/oled-test", [](){
    if (oledReady)
      drawOledText("OLED OK", "SDA GPIO11", "SCL GPIO21", "Addr 0x3C/0x3D");
    sendStatus();
  });
  server.on("/i2c-scan", [](){
    scanI2cBus();
    sendStatus();
  });
  server.on("/oled-init", [](){
    initOled();
    if (oledReady)
      drawOledText("OLED re-init OK", oledStatusDetail, "SDA11 SCL21", i2cScanResult);
    sendStatus();
  });
  server.on("/amp-test", [](){
    runAmplifierTest(server.arg("action"));
    sendStatus();
  });
  server.on("/mic-test", [](){
    startMicrophoneTest();
    sendStatus();
  });
  server.on("/wifi-save", HTTP_ANY, [](){
    const String ssid = webArg("ssid");
    const String password = webArg("password");
    saveWifiStaConfig(ssid, password);
    drawOledText("WiFi connect", wifiStaSsid, "AP " + WiFi.softAPIP().toString(), "STA tunggu IP");
    sendStatus();
  });
  server.on("/wifi-reconnect", [](){
    startWifiStaConnect(true);
    sendStatus();
  });
  server.on("/wifi-clear", [](){
    clearWifiStaConfig();
    drawOledText("WiFi STA hapus", "AP tetap aktif", WiFi.softAPIP().toString(), String(AP_SSID));
    sendStatus();
  });
  server.on("/prefix-save", [](){
    saveDevicePrefix(server.arg("value"));
    drawOledText("Prefix disimpan", devicePrefix, WiFi.softAPIP().toString(), "Buka /status");
    sendStatus();
  });
  server.on("/led", [](){ const String color=server.arg("color"); if(color=="red")setLed(80,0,0); else if(color=="green")setLed(0,80,0); else if(color=="blue")setLed(0,0,80); else setLed(0,0,0); sendStatus(); });
  server.on("/ir-learn", [](){
    const String slot = server.arg("slot");
    if (slot == "on") startIrLearn(IR_LEARN_ON);
    else if (slot == "off") startIrLearn(IR_LEARN_OFF);
    else irMessage = "Slot learn tidak dikenal";
    sendStatus();
  });
  server.on("/ir-send", [](){
    sendIrRaw(server.arg("slot"));
    sendStatus();
  });
  server.on("/ir-tx-test", [](){
    runIrTransmitterTest();
    sendStatus();
  });
  server.on("/ir-blink", [](){
    runIrTxBlinkTest();
    sendStatus();
  });
  server.on("/ir-clear", [](){
    clearIrStorage();
    sendStatus();
  });
  server.onNotFound([](){ server.send(404, "text/plain", "Not found"); });
  server.begin();
}

void setup()
{
  Serial.begin(115200);
  delay(800);

  led.begin();
  led.clear();
  led.show();
  setLed(0, 0, 20);

  pinMode(PIN_IR_RX, INPUT_PULLUP);
  irSender.begin();
  loadIrStorage();
  irMessage += " | TX GPIO10 siap";

  if (OLED_ENABLED)
  {
    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  }
  initOled();
  setupMicrophone();
  setupAmplifier();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  startWifiStaConnect(false);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setBufferSize(MQTT_BUFFER_SIZE);
  updateMqttTopics();

  meterSerial.begin(METER_RS485_BAUD, SERIAL_8E1, PIN_RS485_RX, PIN_RS485_TX);

  setupWebServer();

  drawOledText(
    "WiFi " + String(AP_SSID),
    "IP " + WiFi.softAPIP().toString(),
    "Prefix " + devicePrefix,
    "Slave 1 Ready"
  );

  Serial.println();
  Serial.println("==========================================");
  Serial.println("DEVICE A - DDS3366D MODBUS FACTORY TEST");
  Serial.println("==========================================");
  Serial.print("AP       : "); Serial.println(AP_SSID);
  Serial.print("AP IP    : "); Serial.println(WiFi.softAPIP());
  Serial.print("STA SSID : "); Serial.println(wifiStaSsid.length() ? wifiStaSsid : "(belum diset)");
  Serial.print("Prefix   : "); Serial.println(devicePrefix);
  Serial.print("MQTT     : "); Serial.print(MQTT_HOST); Serial.print(":"); Serial.println(MQTT_PORT);
  Serial.print("MQTT SN  : "); Serial.println(mqttDeviceId);
  Serial.print("MQTT pub : "); Serial.println(mqttStateTopic);
  Serial.print("Web UI   : http://"); Serial.println(WiFi.softAPIP());
  Serial.print("OLED     : "); Serial.println(oledReady ? "active" : "not detected at 0x3C/0x3D");
  Serial.print("AMP I2S  : "); Serial.println(amplifierReady ? "ready on BCLK8 LRC9 DIN11" : amplifierMessage);
  Serial.print("MIC I2S  : "); Serial.println(microphoneReady ? "ready on BCLK6 WS7 DIN15" : microphoneStatus);
  Serial.println("IR TX    : GPIO10 output, aktif hanya saat Send/Test TX");
  Serial.println("Modbus   : 2400 8E1");
  Serial.println("Slave ID : 1");
  Serial.println("FC03     : register 0..16");
  Serial.println("RS485    : module TXD->GPIO18, RXD->GPIO17, auto direction");
  Serial.println("==========================================");

  delay(500);
  if (meterPollingAllowed())
  {
    readMeter();
  }
  else
  {
    meterStatus = "Menunggu internet";
    meterErrorDetail = "Auto Modbus belum jalan sampai WiFi STA connected";
    Serial.println("[MODBUS] SKIP: menunggu WiFi STA connected");
  }
  lastMeterPollMs = millis();
}

void loop()
{
  server.handleClient();
  serviceWifiSta();
  serviceMqtt();
  captureIrSignal();
  serviceMicrophone();

  const uint32_t now = millis();
  if (irLearnSlot == IR_LEARN_NONE && meterPollingAllowed() && (now - lastMeterPollMs) >= METER_POLL_INTERVAL_MS)
  {
    lastMeterPollMs = now;
    readMeter();
  }

  delay(2);
}
