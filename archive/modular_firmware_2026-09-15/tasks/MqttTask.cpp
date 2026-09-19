#include "MqttTask.h"

#include "DebugConfig.h"

#include <WiFi.h>
#include <time.h>

namespace
{
const char *MQTT_HOST = "broker.emqx.io";
const uint16_t MQTT_PORT = 1883;
const uint32_t MQTT_RETRY_INTERVAL_MS = 5000;
const uint16_t MQTT_BUFFER_SIZE = 1536;
const time_t MIN_VALID_TIME = 1609459200;
}

MqttTask *MqttTask::activeInstance = nullptr;

MqttTask::MqttTask(WifiConnector &wifi, AcIrController &acIr, EnergyMeter &meter, TextToVoiceTask &voice) :
  wifi(wifi),
  acIr(acIr),
  meter(meter),
  voice(voice),
  client(wifiClient),
  mqttStatus("Not initialized"),
  lastConnectAttemptMs(0),
  lastPublishedMeterReadMs(0),
  timeConfigured(false)
{
}

void MqttTask::begin()
{
  // Susun device ID dan topic MQTT dari MAC address ESP32 agar tidak hardcoded.
  uint64_t mac = ESP.getEfuseMac();
  char id[16];
  snprintf(id, sizeof(id), "%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac);
  mqttDeviceId = id;
  mqttControlTopic = "hems/ac/" + mqttDeviceId + "/ctr";
  mqttStateTopic = "hems/ac/" + mqttDeviceId + "/state";

  activeInstance = this;
  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setBufferSize(MQTT_BUFFER_SIZE);
  client.setCallback(callbackThunk);
  mqttStatus = "Menunggu WiFi STA";
}

void MqttTask::service()
{
  // Jaga koneksi MQTT tetap hidup dan publish meter hanya saat ada data baru valid.
#if !ACTIVATE_MQTT_MODULE
  mqttStatus = "MQTT module disabled";
  return;
#endif

  if (!wifi.isConnected())
  {
    mqttStatus = "Menunggu WiFi STA";
    return;
  }

  if (!timeConfigured)
  {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    timeConfigured = true;
  }

  if (!client.connected())
  {
    uint32_t now = millis();
    if (now - lastConnectAttemptMs >= MQTT_RETRY_INTERVAL_MS)
    {
      connect();
    }
    return;
  }

  client.loop();
  publishMeterState();
}

String MqttTask::deviceId() const
{
  return mqttDeviceId;
}

String MqttTask::controlTopic() const
{
  return mqttControlTopic;
}

String MqttTask::stateTopic() const
{
  return mqttStateTopic;
}

bool MqttTask::isConnected()
{
  return client.connected();
}

const String &MqttTask::status() const
{
  return mqttStatus;
}

const String &MqttTask::lastPayload() const
{
  return lastMqttPayload;
}

void MqttTask::callbackThunk(char *topic, byte *payload, unsigned int length)
{
  if (activeInstance)
  {
    activeInstance->handleMessage(topic, payload, length);
  }
}

void MqttTask::connect()
{
  lastConnectAttemptMs = millis();
  String clientId = "hems-ac-" + mqttDeviceId;

#if ENABLE_MQTT_LOGS
  Serial.print("MQTT connecting to ");
  Serial.print(MQTT_HOST);
  Serial.print(":");
  Serial.println(MQTT_PORT);
#endif

  if (client.connect(clientId.c_str()))
  {
    client.subscribe(mqttControlTopic.c_str());
    mqttStatus = "Connected";
#if ENABLE_MQTT_LOGS
    Serial.print("MQTT subscribed: ");
    Serial.println(mqttControlTopic);
    Serial.print("MQTT state topic: ");
    Serial.println(mqttStateTopic);
#endif
    lastPublishedMeterReadMs = 0;
    return;
  }

  mqttStatus = "Connect failed rc=" + String(client.state());
#if ENABLE_MQTT_LOGS
  Serial.println(mqttStatus);
#endif
}

void MqttTask::handleMessage(char *topic, byte *payload, unsigned int length)
{
  // Terima payload command dari MQTT lalu teruskan ke handler IR/voice.
  String body;
  body.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++)
  {
    body += (char)payload[i];
  }

  lastMqttPayload = body;
#if ENABLE_MQTT_LOGS
  Serial.print("MQTT message ");
  Serial.print(topic);
  Serial.print(" => ");
  Serial.println(body);
#else
  (void)topic;
#endif
  executeCommand(body);
}

void MqttTask::publishMeterState()
{
  // Error Modbus tidak dipublish; hanya data valid yang masuk ke topic state.
#if !ACTIVATE_MODBUS_MODULE
  return;
#endif

  const EnergyMeterData &meterData = meter.data();
  if (meterData.lastReadMs == 0 || meterData.lastReadMs == lastPublishedMeterReadMs)
  {
    return;
  }

  if (!meterData.valid)
  {
    lastPublishedMeterReadMs = meterData.lastReadMs;
#if ENABLE_MQTT_LOGS
    Serial.print("MQTT state skipped, meter invalid: ");
    Serial.println(meterData.lastError);
#endif
    return;
  }

  String payload = buildMeterStateJson();
  bool published = client.publish(mqttStateTopic.c_str(), payload.c_str(), true);
  if (published)
  {
    lastPublishedMeterReadMs = meterData.lastReadMs;
#if ENABLE_MQTT_LOGS
    Serial.print("MQTT published state ");
    Serial.print(mqttStateTopic);
    Serial.print(" => ");
    Serial.println(payload);
#endif
  }
  else
  {
    mqttStatus = "Publish state gagal";
#if ENABLE_MQTT_LOGS
    Serial.println(mqttStatus);
#endif
  }
}

String MqttTask::buildMeterStateJson() const
{
  // Payload ringkas sesuai format HEMS: SN, RSSI, nilai meter, dan waktu UTC.
  const EnergyMeterData &m = meter.data();
  String json = "{";
  json += "\"sn\":\"" + mqttDeviceId + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"totalActiveEnergy\":" + String(m.totalActiveEnergyKwh, 2) + ",";
  json += "\"forwardActiveEnergy\":" + String(m.forwardActiveEnergyKwh, 2) + ",";
  json += "\"reverseActiveEnergy\":" + String(m.reverseActiveEnergyKwh, 2) + ",";
  json += "\"voltage\":" + String(m.voltageV, 1) + ",";
  json += "\"current\":" + String(m.currentA, 3) + ",";
  json += "\"activePower\":" + String(m.activePowerKw, 4) + ",";
  json += "\"powerFactor\":" + String(m.powerFactor, 3) + ",";
  json += "\"frequency\":" + String(m.frequencyHz, 2) + ",";
  json += "\"reactivePower\":" + String(m.reactivePowerKvar, 4) + ",";
  json += "\"apparentPower\":" + String(m.apparentPowerKva, 4) + ",";
  json += "\"time\":\"" + utcTimestamp() + "\"";
  json += "}";
  return json;
}

String MqttTask::utcTimestamp() const
{
  // Gunakan ISO-8601 UTC jika NTP sudah valid, fallback "UTC" saat belum sinkron.
  time_t now = time(nullptr);
  if (now < MIN_VALID_TIME)
  {
    return "UTC";
  }

  struct tm timeInfo;
  gmtime_r(&now, &timeInfo);

  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeInfo);
  return String(buffer);
}

void MqttTask::executeCommand(const String &payload)
{
  // Command sederhana tetap kompatibel dengan teks biasa atau field JSON ringan.
  String lower = payload;
  lower.toLowerCase();

  String cmd = readToken(lower, "cmd", readToken(lower, "command", ""));
  String power = readToken(lower, "power", "");
  if (cmd.length() == 0) cmd = lower;
  cmd.trim();

  if (cmd == "on" || cmd == "power_on" || power == "on")
  {
#if ACTIVATE_IR_MODULE
#if ACTIVATE_TTS_MODULE
    if (acIr.sendPowerOn()) voice.announceAcOn();
#else
    acIr.sendPowerOn();
#endif
#endif
    return;
  }

  if (cmd == "off" || cmd == "power_off" || power == "off")
  {
#if ACTIVATE_IR_MODULE
#if ACTIVATE_TTS_MODULE
    if (acIr.sendPowerOff()) voice.announceAcOff();
#else
    acIr.sendPowerOff();
#endif
#endif
    return;
  }

  int temp = readIntToken(lower, "temp", readIntToken(lower, "temperature", -1));
  if (temp < 0 && lower.startsWith("temp="))
  {
    temp = lower.substring(5).toInt();
  }

  String protocolToken = readToken(lower, "protocol", readToken(lower, "proto", ""));
  if (protocolToken.length() > 0 && (cmd == "config" || cmd == "protocol" || cmd == "proto" || cmd == lower))
  {
    if (!applyProtocolToken(protocolToken))
    {
#if ENABLE_MQTT_LOGS
      Serial.println("MQTT protocol tidak dikenal.");
#endif
    }
    return;
  }

  if (cmd == "set" || cmd == "state" || cmd == "ac" || temp > 0)
  {
    String mode = readToken(lower, "mode", "cool");
    String fan = readToken(lower, "fan", "auto");
    if (protocolToken.length() > 0)
    {
      applyProtocolToken(protocolToken);
    }
#if ACTIVATE_IR_MODULE
#if ACTIVATE_TTS_MODULE
    if (acIr.sendAcState((uint8_t)temp, mode, fan)) voice.announceTemperatureSet((uint8_t)temp);
#else
    acIr.sendAcState((uint8_t)temp, mode, fan);
#endif
#endif
    return;
  }

  if (cmd.startsWith("protocol=") || cmd.startsWith("proto="))
  {
    int equalsPos = cmd.indexOf('=');
    applyProtocolToken(cmd.substring(equalsPos + 1));
    return;
  }

#if ENABLE_MQTT_LOGS
  Serial.println("MQTT command tidak dikenal.");
#endif
}

bool MqttTask::applyProtocolToken(const String &token)
{
#if !ACTIVATE_IR_MODULE
  (void)token;
  return false;
#else
  String clean = token;
  clean.trim();
  if (clean.length() == 0) return false;

  bool numeric = true;
  for (size_t i = 0; i < clean.length(); i++)
  {
    if (!isDigit(clean[i]) && !(i == 0 && clean[i] == '-'))
    {
      numeric = false;
      break;
    }
  }

  decode_type_t protocol = numeric ? (decode_type_t)clean.toInt() : protocolFromName(clean);
  if (!acIr.isAcProtocolSupported(protocol)) return false;

  acIr.setAcProtocol(protocol);
#if ACTIVATE_TTS_MODULE
  voice.announceProtocolSet(acIr.acProtocolName());
#endif
  return true;
#endif
}

decode_type_t MqttTask::protocolFromName(const String &name) const
{
  String normalized = name;
  normalized.toLowerCase();
  normalized.replace("-", "_");
  normalized.replace(" ", "_");

  if (normalized == "midea") return decode_type_t::MIDEA;
  if (normalized == "haier" || normalized == "haier_ac") return decode_type_t::HAIER_AC;
  if (normalized == "haier_yrw02" || normalized == "haier_ac_yrw02") return decode_type_t::HAIER_AC_YRW02;
  if (normalized == "gree") return decode_type_t::GREE;
  if (normalized == "coolix") return decode_type_t::COOLIX;
  if (normalized == "daikin") return decode_type_t::DAIKIN;
  if (normalized == "daikin2") return decode_type_t::DAIKIN2;
  if (normalized == "panasonic" || normalized == "panasonic_ac") return decode_type_t::PANASONIC_AC;
  if (normalized == "lg") return decode_type_t::LG;
  if (normalized == "samsung" || normalized == "samsung_ac") return decode_type_t::SAMSUNG_AC;
  if (normalized == "toshiba" || normalized == "toshiba_ac") return decode_type_t::TOSHIBA_AC;
  if (normalized == "sharp" || normalized == "sharp_ac") return decode_type_t::SHARP_AC;
  if (normalized == "mitsubishi" || normalized == "mitsubishi_ac") return decode_type_t::MITSUBISHI_AC;

  return decode_type_t::UNKNOWN;
}

String MqttTask::readToken(const String &payload, const String &key, const String &fallback) const
{
  int keyPos = payload.indexOf("\"" + key + "\"");
  int valuePos = -1;

  if (keyPos >= 0)
  {
    valuePos = payload.indexOf(':', keyPos);
  }
  else
  {
    keyPos = payload.indexOf(key + "=");
    if (keyPos >= 0) valuePos = keyPos + key.length();
  }

  if (valuePos < 0) return fallback;
  valuePos++;

  while (valuePos < (int)payload.length() && (payload[valuePos] == ' ' || payload[valuePos] == '"' || payload[valuePos] == '='))
  {
    valuePos++;
  }

  int endPos = valuePos;
  while (endPos < (int)payload.length())
  {
    char c = payload[endPos];
    if (c == '"' || c == ',' || c == '}' || c == ' ') break;
    endPos++;
  }

  if (endPos <= valuePos) return fallback;
  return payload.substring(valuePos, endPos);
}

int MqttTask::readIntToken(const String &payload, const String &key, int fallback) const
{
  String token = readToken(payload, key, "");
  if (token.length() == 0) return fallback;
  return token.toInt();
}
