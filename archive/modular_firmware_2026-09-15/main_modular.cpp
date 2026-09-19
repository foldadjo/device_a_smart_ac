#include <Arduino.h>

#include "DebugConfig.h"
#include "config.h"
#include "modules/AcIrController.h"
#include "modules/EnergyMeter.h"
#include "modules/MicrophoneMonitor.h"
#include "modules/SpeakerController.h"
#include "modules/StatusDisplay.h"
#include "modules/StatusLed.h"
#include "modules/WifiConnector.h"
#include "tasks/MqttTask.h"
#include "tasks/TextToVoiceTask.h"
#include "tasks/WebTask.h"

namespace
{
const uint8_t RGB_PIN = 48;
const char *AP_SSID = "AQU-AC-Remote";
const char *AP_PASSWORD = "12345678";

StatusLed statusLed(RGB_PIN);
WifiConnector wifiConnector(AP_SSID, AP_PASSWORD);
AcIrController acIr(PIN_IR_RX, PIN_IR_TX, statusLed);
StatusDisplay statusDisplay(PIN_OLED_SDA, PIN_OLED_SCL);
EnergyMeter energyMeter(PIN_RS485_RX, PIN_RS485_TX, PIN_RS485_DE_RE, RS485_BAUD, RS485_SERIAL_CONFIG, MODBUS_SLAVE_ID);
MicrophoneMonitor microphone(PIN_MIC_I2S_BCLK, PIN_MIC_I2S_WS, PIN_MIC_I2S_DIN, MIC_SAMPLE_RATE_HZ);
SpeakerController speaker(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DOUT, statusLed);
TextToVoiceTask voiceTask(wifiConnector, statusLed, PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DOUT);
MqttTask mqttTask(wifiConnector, acIr, energyMeter, voiceTask);
WebTask webTask(AP_SSID, AP_PASSWORD, acIr, statusDisplay, energyMeter, microphone, speaker, statusLed, wifiConnector, mqttTask, voiceTask);

void printBootLog()
{
#if ENABLE_BOOT_LOGS
  Serial.println();
  Serial.println("======================================");
  Serial.println("ESP32-S3 AQU AC REMOTE + SPEAKER TEST");
  Serial.println("======================================");
  Serial.print("WiFi SSID        : ");
  Serial.println(AP_SSID);
  Serial.print("WiFi Password    : ");
  Serial.println(AP_PASSWORD);
  Serial.print("Web UI           : http://");
  Serial.println(webTask.ip());
  Serial.print("WiFi STA Status  : ");
  Serial.println(wifiConnector.status());
  Serial.print("WiFi STA IP      : ");
  Serial.println(wifiConnector.staIp());
  Serial.print("MQTT Broker      : broker.emqx.io:1883");
  Serial.println();
  Serial.print("MQTT Topic       : ");
  Serial.println(mqttTask.controlTopic());
  Serial.print("MQTT State Topic : ");
  Serial.println(mqttTask.stateTopic());
  Serial.print("IR Receiver      : GPIO");
  Serial.println(acIr.receiverPin());
  Serial.print("IR Transmitter   : GPIO");
  Serial.println(acIr.transmitterPin());
  Serial.print("Saved ON rawlen  : ");
  Serial.println(acIr.powerOnLength());
  Serial.print("Saved OFF rawlen : ");
  Serial.println(acIr.powerOffLength());
  Serial.print("OLED SDA         : GPIO");
  Serial.println(statusDisplay.sdaPin());
  Serial.print("OLED SCL         : GPIO");
  Serial.println(statusDisplay.sclPin());
  Serial.print("OLED Status      : ");
  Serial.println(statusDisplay.status());
  Serial.print("RS485 RX         : GPIO");
  Serial.println(energyMeter.rxPin());
  Serial.print("RS485 TX         : GPIO");
  Serial.println(energyMeter.txPin());
  Serial.print("RS485 DE/RE      : GPIO");
  Serial.println(energyMeter.deRePin());
  Serial.print("RS485 UART       : ");
  Serial.print(energyMeter.baudRate());
  Serial.println(" 8E1");
  Serial.print("I2S BCLK         : GPIO");
  Serial.println(voiceTask.bclkPin());
  Serial.print("I2S LRC          : GPIO");
  Serial.println(voiceTask.lrcPin());
  Serial.print("I2S DIN          : GPIO");
  Serial.println(voiceTask.doutPin());
  Serial.print("MAX98357A Test   : ");
  Serial.println(speaker.status());
  Serial.print("I2S MIC BCLK     : GPIO");
  Serial.println(microphone.bclkPin());
  Serial.print("I2S MIC WS       : GPIO");
  Serial.println(microphone.wsPin());
  Serial.print("I2S MIC SD       : GPIO");
  Serial.println(microphone.dinPin());
  Serial.print("I2S MIC Status   : ");
  Serial.println(microphone.status());
  Serial.println();
  Serial.println("Wiring:");
  Serial.println("IR Receiver S/OUT -> GPIO4");
  Serial.println("IR Transmitter SIG -> GPIO5");
  Serial.println("OLED SDA -> GPIO8");
  Serial.println("OLED SCL -> GPIO9");
  Serial.println("RS485 RO -> GPIO16");
  Serial.println("RS485 DI -> GPIO17");
  Serial.println("RS485 DE+RE -> GPIO18");
  Serial.println("MAX98357A BCLK -> GPIO12");
  Serial.println("MAX98357A LRC  -> GPIO13");
  Serial.println("MAX98357A DIN  -> GPIO14");
  Serial.println("I2S Mic BCLK -> GPIO6");
  Serial.println("I2S Mic WS   -> GPIO7");
  Serial.println("I2S Mic SD   -> GPIO15");
  Serial.println("MAX98357A VIN  -> 3V3");
  Serial.println("MAX98357A GND  -> GND");
  Serial.println("======================================");
#endif
}
}

void setup()
{
  // Urutan init menjaga peripheral utama aktif sebelum Web UI dan MQTT mulai dipakai.
  Serial.begin(115200);
  delay(1200);

  statusLed.begin();
  statusLed.showIrIdle();

#if ACTIVATE_IR_MODULE
  acIr.begin();
#endif
#if ACTIVATE_OLED_MODULE
  statusDisplay.begin();
#endif
#if ACTIVATE_MODBUS_MODULE
  energyMeter.begin();
#endif
#if ACTIVATE_MIC_MODULE
  microphone.begin();
#endif
#if ACTIVATE_SPEAKER_MODULE
  speaker.begin();
#endif
#if ACTIVATE_WIFI_MODULE
  wifiConnector.begin();
#endif
#if ACTIVATE_TTS_MODULE
  voiceTask.begin();
#endif
#if ACTIVATE_SPEAKER_MODULE && ACTIVATE_TTS_MODULE
  speaker.setVolume(voiceTask.volume());
#endif
#if ACTIVATE_TTS_MODULE
  voiceTask.announceStartup();
#endif
#if ACTIVATE_WEB_MODULE
  webTask.begin();
#endif
#if ACTIVATE_MQTT_MODULE
  mqttTask.begin();
#endif

  printBootLog();
}

void loop()
{
  // Loop utama sengaja ringan; pekerjaan berat berada di task/module service masing-masing.
#if ACTIVATE_WEB_MODULE
  webTask.service();
#endif
#if ACTIVATE_WIFI_MODULE
  wifiConnector.service();
#endif
#if ACTIVATE_MQTT_MODULE
  mqttTask.service();
#endif
#if ACTIVATE_MODBUS_MODULE
  energyMeter.service();
#endif
#if ACTIVATE_IR_MODULE
  acIr.service();
#endif
#if ACTIVATE_OLED_MODULE
  statusDisplay.update(webTask.ip(),
                       acIr.message(),
                       acIr.learningLabel(),
                       acIr.hasPowerOn(),
                       acIr.hasPowerOff(),
                       voiceTask.status(),
                       voiceTask.isSpeaking(),
                       energyMeter.data());
#endif
  delay(2);
}
