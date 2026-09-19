#include "TextToVoiceTask.h"

#include "DebugConfig.h"
#if ACTIVATE_TTS_MODULE
#include "WitAiSecrets.h"
#endif

TextToVoiceTask *TextToVoiceTask::activeInstance = nullptr;

TextToVoiceTask::TextToVoiceTask(WifiConnector &wifi,
                                 StatusLed &led,
                                 uint8_t bclkPin,
                                 uint8_t lrcPin,
                                 uint8_t doutPin) :
  wifi(wifi),
  led(led),
  bclk(bclkPin),
  lrc(lrcPin),
  dout(doutPin),
#if ACTIVATE_TTS_MODULE
  tts(bclkPin, lrcPin, doutPin),
#endif
  voiceQueue(nullptr),
  taskHandle(nullptr),
  voiceVolume(70),
  ready(false),
  initializationAttempted(false),
  speaking(false),
  voiceStatus("Menunggu WiFi"),
  lastSpokenText("-")
{
}

void TextToVoiceTask::begin()
{
  // Siapkan Wit.ai TTS dan queue agar request suara tidak memblokir Web UI.
  prefs.begin("tts", false);
  voiceVolume = prefs.getUChar("volume", 70);
  if (voiceVolume > 100) voiceVolume = 70;

#if !ACTIVATE_TTS_MODULE
  ready = false;
  initializationAttempted = true;
  speaking = false;
  voiceStatus = "TTS module disabled";
  return;
#else
  activeInstance = this;
  tts.setDebugLevel(2);
  tts.setVoice("wit$Remi");
  tts.setStyle("default");
  tts.setGain(voiceVolume / 100.0f);
  tts.setAudioFormat("audio/mpeg");
  tts.setErrorCallback(errorThunk);

  voiceQueue = xQueueCreate(4, sizeof(VoiceEvent));
  if (!voiceQueue)
  {
    voiceStatus = "Queue allocation failed";
    return;
  }

  if (xTaskCreatePinnedToCore(taskEntry, "witai-tts", 8192, this, 1, &taskHandle,
                              ARDUINO_RUNNING_CORE) != pdPASS)
  {
    taskHandle = nullptr;
    voiceStatus = "Task creation failed";
  }
#endif
}

bool TextToVoiceTask::announceStartup()
{
  VoiceEvent event = { VOICE_EVENT_STARTUP, 0, "" };
  return enqueue(event);
}

bool TextToVoiceTask::announceText(const String &text)
{
  if (text.length() == 0 || text.length() > 280) return false;
  VoiceEvent event = { VOICE_EVENT_STARTUP, 0, "" };
  text.toCharArray(event.detail, sizeof(event.detail));
  return enqueue(event);
}

bool TextToVoiceTask::announceAcOn()
{
  VoiceEvent event = { VOICE_EVENT_AC_ON, 0, "" };
  return enqueue(event);
}

bool TextToVoiceTask::announceAcOff()
{
  VoiceEvent event = { VOICE_EVENT_AC_OFF, 0, "" };
  return enqueue(event);
}

bool TextToVoiceTask::announceTemperatureSet(uint8_t temperature)
{
  VoiceEvent event = { VOICE_EVENT_TEMP_SET, temperature, "" };
  return enqueue(event);
}

bool TextToVoiceTask::announceProtocolSet(const String &protocol)
{
  VoiceEvent event = { VOICE_EVENT_PROTOCOL_SET, 0, "" };
  protocol.toCharArray(event.detail, sizeof(event.detail));
  return enqueue(event);
}

bool TextToVoiceTask::announceWifiSaved()
{
  VoiceEvent event = { VOICE_EVENT_WIFI_SAVED, 0, "" };
  return enqueue(event);
}

void TextToVoiceTask::stop()
{
#if !ACTIVATE_TTS_MODULE
  speaking = false;
  voiceStatus = "TTS module disabled";
  led.showSpeakerIdle();
  return;
#else
  if (!ready) return;
  tts.stop();
  speaking = false;
  voiceStatus = "Ready";
  led.showSpeakerIdle();
#endif
}

void TextToVoiceTask::setVolume(uint8_t volume)
{
  voiceVolume = volume > 100 ? 100 : volume;
  prefs.putUChar("volume", voiceVolume);
#if ACTIVATE_TTS_MODULE
  tts.setGain(voiceVolume / 100.0f);
#endif
}

uint8_t TextToVoiceTask::bclkPin() const { return bclk; }
uint8_t TextToVoiceTask::lrcPin() const { return lrc; }
uint8_t TextToVoiceTask::doutPin() const { return dout; }
uint8_t TextToVoiceTask::volume() const { return voiceVolume; }
bool TextToVoiceTask::isReady() const { return ready; }
bool TextToVoiceTask::isSpeaking() const { return speaking; }
const String &TextToVoiceTask::status() const { return voiceStatus; }
const String &TextToVoiceTask::lastText() const { return lastSpokenText; }

void TextToVoiceTask::taskEntry(void *param)
{
  static_cast<TextToVoiceTask *>(param)->runTask();
}

void TextToVoiceTask::errorThunk(String error)
{
#if !ACTIVATE_TTS_MODULE
  (void)error;
  return;
#else
  if (!activeInstance) return;
  activeInstance->voiceStatus = error;
  activeInstance->speaking = false;
  activeInstance->led.showSpeakerIdle();
#endif
}

bool TextToVoiceTask::enqueue(const VoiceEvent &event)
{
  // Masukkan event suara ke task background.
#if !ACTIVATE_TTS_MODULE
  (void)event;
  voiceStatus = "TTS module disabled";
  return false;
#else
  if (!voiceQueue || !taskHandle)
  {
    voiceStatus = "Task belum siap";
    return false;
  }

  if (xQueueSend(voiceQueue, &event, 0) != pdTRUE)
  {
    voiceStatus = "Queue penuh";
    return false;
  }

  voiceStatus = ready ? "Queued" : "Menunggu WiFi";
  return true;
#endif
}

bool TextToVoiceTask::initializeTts()
{
  // Init dilakukan setelah WiFi STA aktif karena library TTS butuh koneksi internet.
#if !ACTIVATE_TTS_MODULE
  initializationAttempted = true;
  voiceStatus = "TTS module disabled";
  return false;
#else
  initializationAttempted = true;

  if (strlen(WITAI_SERVER_TOKEN) == 0 || String(WITAI_SERVER_TOKEN) == "YOUR_WITAI_SERVER_ACCESS_TOKEN")
  {
    voiceStatus = "Server token belum diisi";
    return false;
  }

  voiceStatus = "Initializing Wit.ai";
  bool initialized = tts.begin(wifi.staSsid().c_str(),
                               wifi.staPassword().c_str(),
                               WITAI_SERVER_TOKEN);
  wifi.restoreAccessPoint();

  if (!initialized)
  {
    voiceStatus = "Wit.ai init gagal";
    return false;
  }

  ready = true;
  voiceStatus = "Ready";
#if ENABLE_TTS_LOGS
  Serial.println("WitAITTS READY");
#endif
  return true;
#endif
}

String TextToVoiceTask::textForEvent(const VoiceEvent &event) const
{
  // Pusat kalimat suara agar Web/MQTT/IR memakai narasi yang konsisten.
  switch (event.kind)
  {
    case VOICE_EVENT_STARTUP:
      return strlen(event.detail) > 0
        ? String(event.detail)
        : "Saya aktif. Katakan hello Stroomer untuk memberi perintah";
    case VOICE_EVENT_AC_ON:
      return "AC berhasil dinyalakan";
    case VOICE_EVENT_AC_OFF:
      return "AC berhasil dimatikan";
    case VOICE_EVENT_TEMP_SET:
      return "Suhu berhasil diset " + numberToWords(event.temperature) + " derajat";
    case VOICE_EVENT_PROTOCOL_SET:
      return "Protokol AC berhasil diset ke " + String(event.detail);
    case VOICE_EVENT_WIFI_SAVED:
      return "WiFi berhasil tersambung";
    default:
      return "Perintah berhasil";
  }
}

String TextToVoiceTask::numberToWords(uint8_t value) const
{
  switch (value)
  {
    case 16: return "enam belas";
    case 17: return "tujuh belas";
    case 18: return "delapan belas";
    case 19: return "sembilan belas";
    case 20: return "dua puluh";
    case 21: return "dua puluh satu";
    case 22: return "dua puluh dua";
    case 23: return "dua puluh tiga";
    case 24: return "dua puluh empat";
    case 25: return "dua puluh lima";
    case 26: return "dua puluh enam";
    case 27: return "dua puluh tujuh";
    case 28: return "dua puluh delapan";
    case 29: return "dua puluh sembilan";
    case 30: return "tiga puluh";
    default: return String(value);
  }
}

void TextToVoiceTask::speak(const String &text)
{
  // Mulai streaming suara melalui MAX98357A via library WitAITTS.
#if !ACTIVATE_TTS_MODULE
  lastSpokenText = text;
  voiceStatus = "TTS module disabled";
  speaking = false;
  led.showSpeakerIdle();
  return;
#else
  lastSpokenText = text;
  voiceStatus = "Requesting Wit.ai";
  led.showSpeakerLoud();

  if (!tts.speak(text))
  {
    voiceStatus = "Request TTS gagal";
    speaking = false;
    led.showSpeakerIdle();
    return;
  }

  speaking = true;
  voiceStatus = "Streaming";
#endif
}

void TextToVoiceTask::runTask()
{
  // Loop background: init TTS saat WiFi siap, proses queue, dan update status speaking.
#if !ACTIVATE_TTS_MODULE
  vTaskDelete(nullptr);
#else
  for (;;)
  {
    if (!ready && !initializationAttempted && wifi.isConnected())
    {
      initializeTts();
    }

    if (ready)
    {
      tts.loop();

      VoiceEvent event;
      if (wifi.isConnected() && !tts.isBusy() && xQueueReceive(voiceQueue, &event, 0) == pdTRUE)
      {
        speak(textForEvent(event));
      }

      bool busyNow = tts.isBusy();
      if (speaking && !busyNow)
      {
        speaking = false;
        voiceStatus = "Ready";
        led.showSpeakerIdle();
      }
      else if (busyNow)
      {
        speaking = true;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(2));
  }
#endif
}
