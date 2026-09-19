#pragma once

// Nilai default aktivasi modul kalau build_flags belum mendefinisikan switch.
#ifndef ACTIVATE_WIFI_MODULE
#define ACTIVATE_WIFI_MODULE 1
#endif

#ifndef ACTIVATE_WEB_MODULE
#define ACTIVATE_WEB_MODULE 1
#endif

#ifndef ACTIVATE_MQTT_MODULE
#define ACTIVATE_MQTT_MODULE 1
#endif

#ifndef ACTIVATE_IR_MODULE
#define ACTIVATE_IR_MODULE 1
#endif

#ifndef ACTIVATE_OLED_MODULE
#define ACTIVATE_OLED_MODULE 1
#endif

#ifndef ACTIVATE_MODBUS_MODULE
#define ACTIVATE_MODBUS_MODULE 1
#endif

#ifndef ACTIVATE_MIC_MODULE
#define ACTIVATE_MIC_MODULE 1
#endif

#ifndef ACTIVATE_SPEAKER_MODULE
#define ACTIVATE_SPEAKER_MODULE 1
#endif

#ifndef ACTIVATE_TTS_MODULE
#define ACTIVATE_TTS_MODULE 1
#endif

// Nilai default kalau build_flags di platformio.ini belum mendefinisikan switch.
#ifndef ENABLE_BOOT_LOGS
#define ENABLE_BOOT_LOGS 1
#endif

#ifndef ENABLE_MODBUS_LOGS
#define ENABLE_MODBUS_LOGS 1
#endif

#ifndef ENABLE_IR_LOGS
#define ENABLE_IR_LOGS 1
#endif

#ifndef ENABLE_MIC_LOGS
#define ENABLE_MIC_LOGS 1
#endif

#ifndef ENABLE_AMPLIFIER_LOGS
#define ENABLE_AMPLIFIER_LOGS 1
#endif

#ifndef ENABLE_MQTT_LOGS
#define ENABLE_MQTT_LOGS 1
#endif

#ifndef ENABLE_WIFI_LOGS
#define ENABLE_WIFI_LOGS 1
#endif

#ifndef ENABLE_OLED_LOGS
#define ENABLE_OLED_LOGS 1
#endif

#ifndef ENABLE_TTS_LOGS
#define ENABLE_TTS_LOGS 1
#endif
