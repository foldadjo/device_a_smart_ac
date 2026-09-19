#include "WebTask.h"

#include "DebugConfig.h"

WebTask::WebTask(const char *ssid,
                 const char *password,
                 AcIrController &acIr,
                 StatusDisplay &display,
                 EnergyMeter &meter,
                 MicrophoneMonitor &microphone,
                 SpeakerController &speaker,
                 StatusLed &led,
                 WifiConnector &wifi,
                 MqttTask &mqtt,
                 TextToVoiceTask &voice) :
  ssid(ssid),
  password(password),
  acIr(acIr),
  display(display),
  meter(meter),
  microphone(microphone),
  speaker(speaker),
  led(led),
  wifi(wifi),
  mqtt(mqtt),
  voice(voice),
  server(80),
  voiceWakeUntilMs(0),
  remoteSetupStep(0),
  remoteSetupLearnSequence(0)
{
}

void WebTask::begin()
{
  server.on("/", [this]() { handleRoot(); });
  server.on("/status", [this]() { handleStatus(); });
  server.on("/voice", [this]() { handleVoice(); });
  server.on("/voice-command", [this]() { handleVoiceCommand(); });
  server.on("/microphone", [this]() { handleMicrophone(); });
  server.on("/speaker-test", [this]() { handleSpeakerTest(); });
  server.on("/display-test", [this]() { handleDisplayTest(); });
  server.on("/led-test", [this]() { handleLedTest(); });
  server.on("/learn", [this]() { handleLearn(); });
  server.on("/send", [this]() { handleSend(); });
  server.on("/clear-ir", [this]() { handleClearIr(); });
  server.on("/sensor-test", [this]() { handleSensorTest(); });
  server.on("/temperature", [this]() { handleTemperature(); });
  server.on("/protocol", [this]() { handleProtocol(); });
  server.on("/wifi", [this]() { handleWifi(); });
  server.on("/favicon.ico", [this]() { handleFavicon(); });
  server.onNotFound([this]() { handleNotFound(); });
  server.begin();
}

void WebTask::service()
{
  server.handleClient();

#if ACTIVATE_IR_MODULE && ACTIVATE_TTS_MODULE
  if (remoteSetupStep == 1 && acIr.learnSequence() != remoteSetupLearnSequence)
  {
    remoteSetupLearnSequence = acIr.learnSequence();
    remoteSetupStep = 2;
    acIr.startLearning(LEARN_POWER_ON);
    voice.announceText("Sinyal matikan AC tersimpan. Sekarang nyalakan AC.");
  }
  else if (remoteSetupStep == 2 && acIr.learnSequence() != remoteSetupLearnSequence)
  {
    remoteSetupStep = 0;
    voice.announceText("Berhasil diatur");
  }
#endif
}

String WebTask::ip() const
{
  return wifi.apIp();
}

String WebTask::jsonEscape(const String &value) const
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

String WebTask::buildStatusJson()
{
  String json = "{";
  json += "\"ip\":\"" + ip() + "\",";
  json += "\"activeModules\":{";
  json += "\"wifi\":" + String(ACTIVATE_WIFI_MODULE ? "true" : "false") + ",";
  json += "\"web\":" + String(ACTIVATE_WEB_MODULE ? "true" : "false") + ",";
  json += "\"mqtt\":" + String(ACTIVATE_MQTT_MODULE ? "true" : "false") + ",";
  json += "\"ir\":" + String(ACTIVATE_IR_MODULE ? "true" : "false") + ",";
  json += "\"oled\":" + String(ACTIVATE_OLED_MODULE ? "true" : "false") + ",";
  json += "\"modbus\":" + String(ACTIVATE_MODBUS_MODULE ? "true" : "false") + ",";
  json += "\"mic\":" + String(ACTIVATE_MIC_MODULE ? "true" : "false") + ",";
  json += "\"speaker\":" + String(ACTIVATE_SPEAKER_MODULE ? "true" : "false") + ",";
  json += "\"tts\":" + String(ACTIVATE_TTS_MODULE ? "true" : "false");
  json += "},";
  json += "\"wifiConfigured\":" + String(wifi.hasCredentials() ? "true" : "false") + ",";
  json += "\"wifiConnected\":" + String(wifi.isConnected() ? "true" : "false") + ",";
  json += "\"wifiSsid\":\"" + jsonEscape(wifi.staSsid()) + "\",";
  json += "\"wifiStatus\":\"" + jsonEscape(wifi.status()) + "\",";
  json += "\"staIp\":\"" + wifi.staIp() + "\",";
  json += "\"mqttConnected\":" + String(mqtt.isConnected() ? "true" : "false") + ",";
  json += "\"mqttStatus\":\"" + jsonEscape(mqtt.status()) + "\",";
  json += "\"mqttDeviceId\":\"" + jsonEscape(mqtt.deviceId()) + "\",";
  json += "\"mqttTopic\":\"" + jsonEscape(mqtt.controlTopic()) + "\",";
  json += "\"mqttStateTopic\":\"" + jsonEscape(mqtt.stateTopic()) + "\",";
  json += "\"mqttLastPayload\":\"" + jsonEscape(mqtt.lastPayload()) + "\",";
  json += "\"irRxPin\":" + String(acIr.receiverPin()) + ",";
  json += "\"irTxPin\":" + String(acIr.transmitterPin()) + ",";
  json += "\"irLearning\":\"" + String(acIr.learningLabel()) + "\",";
  json += "\"hasIrOn\":" + String(acIr.hasPowerOn() ? "true" : "false") + ",";
  json += "\"hasIrOff\":" + String(acIr.hasPowerOff() ? "true" : "false") + ",";
  json += "\"irOnLen\":" + String(acIr.powerOnLength()) + ",";
  json += "\"irOffLen\":" + String(acIr.powerOffLength()) + ",";
  json += "\"irMessage\":\"" + jsonEscape(acIr.message()) + "\",";
  json += "\"acProtocol\":" + String((int16_t)acIr.acProtocol()) + ",";
  json += "\"acProtocolName\":\"" + jsonEscape(acIr.acProtocolName()) + "\",";
  json += "\"acProtocolReady\":" + String(acIr.acProtocolReady() ? "true" : "false") + ",";
  json += "\"protocols\":[";
  const decode_type_t protocols[] = {
    decode_type_t::MIDEA,
    decode_type_t::HAIER_AC,
    decode_type_t::HAIER_AC_YRW02,
    decode_type_t::GREE,
    decode_type_t::COOLIX,
    decode_type_t::DAIKIN,
    decode_type_t::DAIKIN2,
    decode_type_t::PANASONIC_AC,
    decode_type_t::LG,
    decode_type_t::SAMSUNG_AC,
    decode_type_t::TOSHIBA_AC,
    decode_type_t::SHARP_AC,
    decode_type_t::MITSUBISHI_AC
  };
  for (uint8_t i = 0; i < sizeof(protocols) / sizeof(protocols[0]); i++)
  {
    if (i > 0) json += ",";
    json += "{\"value\":" + String((int16_t)protocols[i]) + ",";
    json += "\"name\":\"" + jsonEscape(typeToString(protocols[i])) + "\"}";
  }
  json += "],";
  json += "\"tempMin\":" + String(acIr.minTemperature()) + ",";
  json += "\"tempMax\":" + String(acIr.maxTemperature()) + ",";
  json += "\"oledSdaPin\":" + String(display.sdaPin()) + ",";
  json += "\"oledSclPin\":" + String(display.sclPin()) + ",";
  json += "\"oledReady\":" + String(display.isReady() ? "true" : "false") + ",";
  json += "\"oledStatus\":\"" + jsonEscape(display.status()) + "\",";
  json += "\"oledError\":\"" + jsonEscape(display.error()) + "\",";
  json += "\"bclkPin\":" + String(voice.bclkPin()) + ",";
  json += "\"lrcPin\":" + String(voice.lrcPin()) + ",";
  json += "\"doutPin\":" + String(voice.doutPin()) + ",";
  json += "\"voiceReady\":" + String(voice.isReady() ? "true" : "false") + ",";
  json += "\"voiceStatus\":\"" + jsonEscape(voice.status()) + "\",";
  json += "\"voiceLastText\":\"" + jsonEscape(voice.lastText()) + "\",";
  json += "\"voiceVolume\":" + String(voice.volume()) + ",";
  json += "\"voiceSpeaking\":" + String(voice.isSpeaking() ? "true" : "false") + ",";
  json += "\"voiceWakeActive\":" + String(millis() < voiceWakeUntilMs ? "true" : "false") + ",";
  json += "\"speakerReady\":" + String(speaker.isReady() ? "true" : "false") + ",";
  json += "\"speakerRunning\":" + String(speaker.isRunning() ? "true" : "false") + ",";
  json += "\"speakerStatus\":\"" + jsonEscape(speaker.status()) + "\",";
  json += "\"speakerError\":\"" + jsonEscape(speaker.error()) + "\",";
  json += "\"speakerMessage\":\"" + jsonEscape(speaker.message()) + "\",";
  json += "\"speakerCurrentTest\":\"" + jsonEscape(speaker.currentTest()) + "\",";
  json += "\"speakerFrequency\":" + String(speaker.currentFrequency()) + ",";
  json += "\"speakerVolume\":" + String(speaker.currentVolume()) + ",";
  json += "\"speakerWriteErrors\":" + String(speaker.writeErrors()) + ",";
  json += "\"speakerShortWrites\":" + String(speaker.shortWrites()) + ",";
  json += "\"micReady\":" + String(microphone.isReady() ? "true" : "false") + ",";
  json += "\"micStatus\":\"" + jsonEscape(microphone.status()) + "\",";
  json += "\"micError\":\"" + jsonEscape(microphone.error()) + "\",";
  json += "\"micBclkPin\":" + String(microphone.bclkPin()) + ",";
  json += "\"micWsPin\":" + String(microphone.wsPin()) + ",";
  json += "\"micDinPin\":" + String(microphone.dinPin()) + ",";
  json += "\"micSampleRate\":" + String(microphone.sampleRate()) + ",";
  json += "\"micRms\":" + String(microphone.rms()) + ",";
  json += "\"micPeak\":" + String(microphone.peak()) + ",";
  json += "\"micChunks\":" + String(microphone.chunksRead()) + ",";
  json += "\"micReadErrors\":" + String(microphone.readErrors()) + ",";
  json += "\"micLastReadMs\":" + String(microphone.lastReadMs()) + ",";
  json += "\"meter\":" + meter.buildJson();
  json += "}";
  return json;
}

String WebTask::buildPage() const
{
  String page = R"HTML(
<!doctype html>
<html lang="id">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>AQU AC Remote</title>
  <style>
    :root {
      color-scheme: light;
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: #f5f7f7;
      color: #172126;
    }
    * { box-sizing: border-box; }
    body { margin: 0; min-height: 100vh; display: flex; justify-content: center; background: #f5f7f7; }
    main { width: min(100%, 560px); padding: 18px 14px 24px; }
    h1 { margin: 0 0 6px; font-size: 28px; line-height: 1.1; letter-spacing: 0; }
    .sub { margin: 0 0 14px; color: #59676d; font-size: 14px; }
    .menu {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(76px, 1fr));
      gap: 8px;
      margin-bottom: 12px;
    }
    .menu button {
      min-height: 44px;
      background: #e7eef0;
      color: #243238;
      font-size: 13px;
      font-weight: 800;
    }
    .menu button.active { background: #285f8f; color: #fff; }
    .panel {
      background: #fff;
      border: 1px solid #dce5e7;
      border-radius: 8px;
      padding: 16px;
      box-shadow: 0 10px 28px rgba(29, 47, 54, 0.08);
      margin-bottom: 14px;
    }
    .view { display: none; }
    .view.active { display: block; }
    .grid, .actions { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
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
    select, input, textarea {
      width: 100%;
      min-height: 52px;
      border: 1px solid #cdd9dc;
      border-radius: 8px;
      background: #fff;
      color: #172126;
      font-size: 18px;
      font-weight: 800;
      padding: 0 12px;
    }
    textarea { min-height: 110px; padding: 12px; resize: vertical; }
    input[type="range"] {
      padding: 0;
      min-height: 36px;
      accent-color: #285f8f;
    }
    .password-wrap {
      position: relative;
      display: grid;
      grid-template-columns: 1fr 52px;
      gap: 8px;
    }
    .password-wrap input { min-width: 0; }
    .icon-btn {
      min-height: 52px;
      width: 52px;
      padding: 0;
      display: grid;
      place-items: center;
      font-size: 22px;
      line-height: 1;
    }
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
    .temp-list {
      display: grid;
      grid-template-columns: repeat(5, 1fr);
      gap: 8px;
    }
    .pill {
      min-height: 38px;
      border: 1px solid #dce5e7;
      border-radius: 8px;
      display: grid;
      place-items: center;
      font-weight: 800;
      color: #68767c;
    }
    .pill.ready { color: #12633e; background: #eaf6ef; border-color: #b8dcc8; }
    .level-box {
      border: 1px solid #e3eaec;
      border-radius: 8px;
      padding: 14px;
      grid-column: 1 / -1;
    }
    .level-bar {
      width: 100%;
      height: 18px;
      border-radius: 999px;
      background: #e7eef0;
      overflow: hidden;
      margin-top: 8px;
    }
    .level-fill {
      width: 0%;
      height: 100%;
      background: #1f7a55;
      transition: width 180ms ease;
    }
    .status-ok { color: #12633e; }
    .status-warn { color: #996500; }
    .status-error { color: #a32828; }
  </style>
</head>
<body>
  <main>
    <h1>AQU AC Remote</h1>
    <p class="sub">Hotspot mode. Buka 192.168.4.1 dari HP yang tersambung ke WiFi alat.</p>

    <nav class="menu">
      <button id="tab-control" class="active" onclick="showView('control')">Control</button>
      <button id="tab-config" onclick="showView('config')">Config</button>
      <button id="tab-wifi" onclick="showView('wifi')">WiFi</button>
      <button id="tab-diag" onclick="showView('diag')">Status</button>
      <button id="tab-factory" onclick="showView('factory')">Factory</button>
      <button id="tab-speaker" onclick="showView('speaker')">Sound</button>
      <button id="tab-microphone" onclick="showView('microphone')">Microphone</button>
      <button id="tab-modbus" onclick="showView('modbus')">Modbus</button>
    </nav>

    <section id="view-control" class="view active">
      <div class="panel actions">
      <button onclick="irCmd('/send?slot=on')">Nyalakan AC</button>
      <button onclick="irCmd('/send?slot=off')">Matikan AC</button>
      <select id="controlTemp" class="wide"></select>
      <select id="controlMode" class="wide">
        <option value="cool" selected>Mode Cool</option>
        <option value="auto">Mode Auto</option>
        <option value="dry">Mode Dry</option>
        <option value="fan">Mode Fan</option>
        <option value="heat">Mode Heat</option>
      </select>
      <select id="controlFan" class="wide">
        <option value="auto" selected>Fan Auto</option>
        <option value="low">Fan Low</option>
        <option value="medium">Fan Medium</option>
        <option value="high">Fan High</option>
        <option value="max">Fan Max</option>
      </select>
      <button class="wide" onclick="sendAcState()">Kirim State AC</button>
      </div>
      <div class="panel grid">
        <div class="cell"><span class="label">Rekaman ON</span><span class="value" id="irOn">-</span></div>
        <div class="cell"><span class="label">Rekaman OFF</span><span class="value" id="irOff">-</span></div>
        <div class="cell wide"><span class="label">Log IR</span><span class="value" id="irMessage">-</span></div>
      </div>
    </section>

    <section id="view-config" class="view">
      <div class="panel actions">
        <button onclick="irCmd('/learn?slot=on')">Learn ON</button>
        <button onclick="irCmd('/learn?slot=off')">Learn OFF</button>
        <select id="protocolSelect" class="wide"></select>
        <button class="wide" onclick="saveProtocol()">Simpan Protocol AC</button>
        <button class="wide" onclick="irCmd('/clear-ir')">Hapus Semua Rekaman IR</button>
      </div>
      <div class="panel">
        <div class="label">Protocol aktif</div>
        <div class="value" id="protocolStatus">-</div>
      </div>
      <div class="panel note">
        Kalau Learn ON/OFF terbaca sebagai protocol AC yang didukung, protocol akan otomatis tersimpan. Jika suhu belum bereaksi, coba pilih protocol lain yang dekat dengan merek AC kamu.
      </div>
    </section>

    <section id="view-wifi" class="view">
      <div class="panel actions">
        <input id="wifiSsid" class="wide" placeholder="SSID WiFi rumah">
        <div class="password-wrap wide">
          <input id="wifiPassword" type="password" placeholder="Password WiFi">
          <button class="icon-btn" onclick="togglePassword()" title="Lihat password" type="button">&#128065;</button>
        </div>
        <button class="wide" onclick="saveWifi()">Simpan WiFi</button>
        <button class="wide" onclick="clearWifi()">Hapus WiFi</button>
      </div>
      <div class="panel grid">
        <div class="cell"><span class="label">WiFi STA</span><span class="value" id="wifiStatus">-</span></div>
        <div class="cell"><span class="label">STA IP</span><span class="value" id="staIp">-</span></div>
        <div class="cell"><span class="label">MQTT</span><span class="value" id="mqttStatus">-</span></div>
        <div class="cell"><span class="label">Device ID</span><span class="value" id="mqttDeviceId">-</span></div>
        <div class="cell wide"><span class="label">Topic control</span><span class="value" id="mqttTopic">-</span></div>
        <div class="cell wide"><span class="label">Topic state</span><span class="value" id="mqttStateTopic">-</span></div>
        <div class="cell wide"><span class="label">Payload terakhir</span><span class="value" id="mqttLastPayload">-</span></div>
      </div>
      <div class="panel note">
        MQTT broker: broker.emqx.io port 1883. Payload contoh: on, off, temp=24, protocol=midea, atau {"cmd":"set","temp":24,"mode":"cool","fan":"auto","protocol":"midea"}.
      </div>
    </section>

    <section id="view-diag" class="view">
      <div class="panel actions">
        <button class="wide" onclick="sensorTest()">Test IR Receiver 4 Detik</button>
      </div>
      <div class="panel grid">
        <div class="cell"><span class="label">IP alat</span><span class="value" id="ip">-</span></div>
        <div class="cell"><span class="label">Pin IR</span><span class="value" id="irPins">-</span></div>
        <div class="cell"><span class="label">Mode learn</span><span class="value" id="irLearning">-</span></div>
        <div class="cell"><span class="label">Pin OLED</span><span class="value" id="oledPins">-</span></div>
        <div class="cell wide"><span class="label">OLED Status</span><span class="value" id="oledStatus">-</span></div>
      </div>
    </section>

    <section id="view-factory" class="view">
      <div class="panel actions">
        <button onclick="displayTest()">Test OLED</button>
        <button onclick="ledTest('white')">LED White</button>
        <button onclick="ledTest('red')">LED Red</button>
        <button onclick="ledTest('green')">LED Green</button>
        <button onclick="ledTest('blue')">LED Blue</button>
        <button onclick="ledTest('off')">LED Off</button>
        <button class="wide" onclick="sensorTest()">IR Receiver 4 Detik</button>
        <button onclick="irCmd('/learn?slot=on')">Learn IR ON</button>
        <button onclick="irCmd('/learn?slot=off')">Learn IR OFF</button>
        <button onclick="speakerTest('tone1000')">Speaker 1 kHz</button>
        <button onclick="speakerTest('beep3x')">Speaker Beep</button>
        <button onclick="startMicTest()">Mic 3 Detik</button>
        <button onclick="refresh()">Baca Meter</button>
      </div>
      <div class="panel grid">
        <div class="cell"><span class="label">OLED</span><span class="value" id="factoryOled">-</span></div>
        <div class="cell"><span class="label">RGB LED</span><span class="value" id="factoryLed">-</span></div>
        <div class="cell"><span class="label">IR Receiver</span><span class="value" id="factoryIr">-</span></div>
        <div class="cell"><span class="label">Speaker</span><span class="value" id="factorySpeaker">-</span></div>
        <div class="cell"><span class="label">Microphone</span><span class="value" id="factoryMic">-</span></div>
        <div class="cell"><span class="label">Energy Meter</span><span class="value" id="factoryMeter">-</span></div>
        <div class="cell wide"><span class="label">Catatan wiring</span><span class="value">GPIO4 IR RX, GPIO5 IR TX, GPIO8/9 OLED, GPIO6/7/15 mic, GPIO12/13/14 speaker, GPIO16/17/18 RS485.</span></div>
      </div>
    </section>

    <section id="view-speaker" class="view">
      <div class="panel actions">
        <textarea id="voiceText" class="wide" maxlength="280" placeholder="Teks yang ingin diucapkan">AC berhasil dinyalakan</textarea>
        <input id="voiceCommandText" class="wide" placeholder="Simulasi kalimat: Halo Stroomer">
        <button class="wide" onclick="sendVoiceCommand()">Kirim Perintah Suara</button>
        <input id="voiceVolume" class="wide" type="range" min="0" max="100" step="5" oninput="showVoiceVolume()" onchange="saveVoiceVolume()">
        <button class="wide" onclick="speakText()">Ucapkan Teks</button>
        <button onclick="voiceAction('startup')">Test Startup</button>
        <button onclick="voiceAction('stop')">Stop</button>
      </div>
      <div class="panel actions">
        <button onclick="speakerTest('tone1000')">Tone 1 kHz</button>
        <button onclick="speakerTest('beep3x')">Beep 3x</button>
        <button onclick="speakerTest('sweep')">Sweep</button>
        <button onclick="speakerTest('musicDemo')">Selamat Ulang Tahun</button>
        <button class="wide" onclick="speakerTest('burungKakaTua')">Burung Kaka Tua</button>
        <button class="wide" onclick="speakerTest('stop')">Stop Test Lokal</button>
      </div>
      <div class="panel grid">
        <div class="cell"><span class="label">Wit.ai TTS</span><span class="value" id="voiceStatus">-</span></div>
        <div class="cell"><span class="label">Volume</span><span class="value" id="voiceVolumeLabel">-</span></div>
        <div class="cell"><span class="label">Pin I2S</span><span class="value" id="i2sPins">-</span></div>
        <div class="cell"><span class="label">Wake Word</span><span class="value" id="voiceWake">-</span></div>
        <div class="cell wide"><span class="label">Teks terakhir</span><span class="value" id="voiceLastText">-</span></div>
      </div>
      <div class="panel grid">
        <div class="cell"><span class="label">MAX98357A</span><span class="value" id="speakerStatus">-</span></div>
        <div class="cell"><span class="label">Test Lokal</span><span class="value" id="speakerTest">-</span></div>
        <div class="cell"><span class="label">Freq / Vol</span><span class="value" id="speakerFreqVol">-</span></div>
        <div class="cell"><span class="label">Write Errors</span><span class="value" id="speakerErrors">-</span></div>
        <div class="cell wide"><span class="label">Log Speaker</span><span class="value" id="speakerMessage">-</span></div>
      </div>
    </section>

    <section id="view-microphone" class="view">
      <div class="panel actions">
        <button class="wide" onclick="startMicTest()">Tes Bicara 3 Detik</button>
        <button onclick="resetMic()">Reset</button>
        <button onclick="refresh()">Refresh</button>
      </div>
      <div class="panel grid">
        <div class="level-box">
          <span class="label">Level Suara</span>
          <div class="value" id="micLevelText">-</div>
          <div class="level-bar"><div class="level-fill" id="micLevelFill"></div></div>
        </div>
        <div class="cell"><span class="label">Microphone</span><span class="value" id="micStatus">-</span></div>
        <div class="cell"><span class="label">RMS</span><span class="value" id="micRms">-</span></div>
        <div class="cell"><span class="label">Peak</span><span class="value" id="micPeak">-</span></div>
        <div class="cell"><span class="label">Sample Rate</span><span class="value" id="micSampleRate">-</span></div>
        <div class="cell wide"><span class="label">Pin Mic</span><span class="value" id="micPins">-</span></div>
        <div class="cell wide"><span class="label">Mic Counter</span><span class="value" id="micCounters">-</span></div>
        <div class="cell wide"><span class="label">Hasil Test</span><span class="value" id="micTestResult">Tekan tombol test, lalu bicara dekat microphone.</span></div>
      </div>
    </section>

    <section id="view-modbus" class="view">
      <div class="panel grid">
        <div class="cell"><span class="label">Meter</span><span class="value" id="meterStatus">-</span></div>
        <div class="cell"><span class="label">RS485</span><span class="value" id="meterBus">-</span></div>
        <div class="cell"><span class="label">Total Active Energy</span><span class="value" id="meterEnergy">-</span></div>
        <div class="cell"><span class="label">Forward Energy</span><span class="value" id="meterForwardEnergy">-</span></div>
        <div class="cell"><span class="label">Reverse Energy</span><span class="value" id="meterReverseEnergy">-</span></div>
        <div class="cell"><span class="label">Tegangan</span><span class="value" id="meterVoltage">-</span></div>
        <div class="cell"><span class="label">Arus</span><span class="value" id="meterCurrent">-</span></div>
        <div class="cell"><span class="label">Daya Aktif</span><span class="value" id="meterPower">-</span></div>
        <div class="cell"><span class="label">Power Factor</span><span class="value" id="meterPowerFactor">-</span></div>
        <div class="cell"><span class="label">Frequency</span><span class="value" id="meterFrequency">-</span></div>
        <div class="cell"><span class="label">Reactive Power</span><span class="value" id="meterReactivePower">-</span></div>
        <div class="cell"><span class="label">Apparent Power</span><span class="value" id="meterApparentPower">-</span></div>
        <div class="cell wide"><span class="label">Modbus TX/RX</span><span class="value" id="meterTxRx">-</span></div>
        <div class="cell wide"><span class="label">Raw Register Values</span><span class="value" id="meterRaw">-</span></div>
      </div>
    </section>
  </main>

  <script>
    let tempRangeReady = false;
    let wifiFormDirty = false;
    let voiceVolumeDirty = false;
    let micTestUntil = 0;
    let micTestPeak = 0;
    let micTestBestRms = 0;

    function showView(name) {
      for (const view of document.querySelectorAll('.view')) view.classList.remove('active');
      for (const tab of document.querySelectorAll('.menu button')) tab.classList.remove('active');
      document.getElementById(`view-${name}`).classList.add('active');
      document.getElementById(`tab-${name}`).classList.add('active');
    }

    function setupControls(data) {
      if (tempRangeReady) return;
      for (const id of ['controlTemp']) {
        const select = document.getElementById(id);
        select.innerHTML = '';
        for (let temp = data.tempMin; temp <= data.tempMax; temp++) {
          const option = document.createElement('option');
          option.value = temp;
          option.textContent = `${temp} C`;
          if (temp === 24) option.selected = true;
          select.appendChild(option);
        }
      }
      const protocolSelect = document.getElementById('protocolSelect');
      protocolSelect.innerHTML = '';
      for (const protocol of data.protocols) {
        const option = document.createElement('option');
        option.value = protocol.value;
        option.textContent = protocol.name;
        protocolSelect.appendChild(option);
      }
      tempRangeReady = true;
    }

    async function refresh() {
      const res = await fetch('/status', { cache: 'no-store' });
      const data = await res.json();
      setupControls(data);

      document.getElementById('irPins').textContent = `RX ${data.irRxPin} / TX ${data.irTxPin}`;
      document.getElementById('irLearning').textContent = data.irLearning;
      document.getElementById('irOn').textContent = data.hasIrOn ? `Ada (${data.irOnLen})` : 'Belum ada';
      document.getElementById('irOff').textContent = data.hasIrOff ? `Ada (${data.irOffLen})` : 'Belum ada';
      document.getElementById('irMessage').textContent = data.irMessage;
      const meter = data.meter;
      document.getElementById('meterStatus').textContent = meter.valid ? 'OK' : `${meter.status}: ${meter.lastError}`;
      document.getElementById('meterBus').textContent = `ID ${meter.slaveId}, RX ${meter.rxPin}, TX ${meter.txPin}, DE/RE ${meter.deRePin}, ${meter.baud} 8E1, OK ${meter.successCount}, Err ${meter.errorCount}`;
      document.getElementById('meterEnergy').textContent = meter.valid ? `${meter.totalActiveEnergyKwh.toFixed(2)} kWh` : '-';
      document.getElementById('meterForwardEnergy').textContent = meter.valid ? `${meter.forwardActiveEnergyKwh.toFixed(2)} kWh` : '-';
      document.getElementById('meterReverseEnergy').textContent = meter.valid ? `${meter.reverseActiveEnergyKwh.toFixed(2)} kWh` : '-';
      document.getElementById('meterVoltage').textContent = meter.valid ? `${meter.voltageV.toFixed(1)} V` : '-';
      document.getElementById('meterCurrent').textContent = meter.valid ? `${meter.currentA.toFixed(3)} A` : '-';
      document.getElementById('meterPower').textContent = meter.valid ? `${meter.activePowerKw.toFixed(4)} kW` : '-';
      document.getElementById('meterPowerFactor').textContent = meter.valid ? meter.powerFactor.toFixed(3) : '-';
      document.getElementById('meterFrequency').textContent = meter.valid ? `${meter.frequencyHz.toFixed(2)} Hz` : '-';
      document.getElementById('meterReactivePower').textContent = meter.valid ? `${meter.reactivePowerKvar.toFixed(4)} kvar` : '-';
      document.getElementById('meterApparentPower').textContent = meter.valid ? `${meter.apparentPowerKva.toFixed(4)} kVA` : '-';
      document.getElementById('meterTxRx').textContent = `TX ${meter.lastRequestHex || '-'} / RX(${meter.lastResponseBytes}) ${meter.lastResponseHex || '-'}`;
      document.getElementById('meterRaw').textContent = `E ${meter.rawTotalActiveEnergy}, Fwd ${meter.rawForwardActiveEnergy}, Rev ${meter.rawReverseActiveEnergy}, V ${meter.rawVoltage}, I ${meter.rawCurrent}, P ${meter.rawActivePower}, PF ${meter.rawPowerFactor}, Hz ${meter.rawFrequency}, Q ${meter.rawReactivePower}, S ${meter.rawApparentPower}`;
      document.getElementById('protocolStatus').textContent = data.acProtocolReady ? data.acProtocolName : 'Belum diset';
      document.getElementById('protocolSelect').value = data.acProtocol;
      const wifiSsidInput = document.getElementById('wifiSsid');
      if (!wifiFormDirty) wifiSsidInput.value = data.wifiSsid;
      document.getElementById('wifiStatus').textContent = data.wifiConnected ? `Connected: ${data.wifiSsid}` : data.wifiStatus;
      document.getElementById('staIp').textContent = data.staIp;
      document.getElementById('mqttStatus').textContent = data.mqttConnected ? 'Connected' : data.mqttStatus;
      document.getElementById('mqttDeviceId').textContent = data.mqttDeviceId;
      document.getElementById('mqttTopic').textContent = data.mqttTopic;
      document.getElementById('mqttStateTopic').textContent = data.mqttStateTopic;
      document.getElementById('mqttLastPayload').textContent = data.mqttLastPayload || '-';
      document.getElementById('oledPins').textContent = `SDA ${data.oledSdaPin} / SCL ${data.oledSclPin}`;
      document.getElementById('oledStatus').textContent = `${data.oledStatus}${data.oledReady ? '' : ` (${data.oledError})`}`;
      document.getElementById('i2sPins').textContent = `BCLK ${data.bclkPin} / LRC ${data.lrcPin} / DIN ${data.doutPin}`;
      document.getElementById('ip').textContent = data.ip;
      if (!voiceVolumeDirty) document.getElementById('voiceVolume').value = data.voiceVolume;
      document.getElementById('voiceStatus').textContent = data.voiceSpeaking ? 'Speaking' : data.voiceStatus;
      document.getElementById('voiceVolumeLabel').textContent = `${data.voiceVolume}%`;
      document.getElementById('voiceLastText').textContent = data.voiceLastText || '-';
      document.getElementById('voiceWake').textContent = data.voiceWakeActive ? 'Aktif' : 'Standby';
      document.getElementById('speakerStatus').textContent = data.speakerRunning ? 'Playing' : data.speakerStatus;
      document.getElementById('speakerTest').textContent = data.speakerCurrentTest;
      document.getElementById('speakerFreqVol').textContent = `${data.speakerFrequency} Hz / ${data.speakerVolume}%`;
      document.getElementById('speakerErrors').textContent = `${data.speakerWriteErrors} write, ${data.speakerShortWrites} short`;
      document.getElementById('speakerMessage').textContent = data.speakerMessage || data.speakerError || '-';
      const micStatusText = data.micReady ? data.micStatus : `${data.micStatus}: ${data.micError}`;
      const micStatusEl = document.getElementById('micStatus');
      micStatusEl.textContent = micStatusText;
      micStatusEl.className = `value ${data.micStatus === 'MIC OK' ? 'status-ok' : (data.micStatus === 'ERROR' ? 'status-error' : 'status-warn')}`;
      document.getElementById('micRms').textContent = data.micRms;
      document.getElementById('micPeak').textContent = data.micPeak;
      document.getElementById('micSampleRate').textContent = `${data.micSampleRate} Hz`;
      document.getElementById('micPins').textContent = `BCLK ${data.micBclkPin} / WS ${data.micWsPin} / DIN ${data.micDinPin}`;
      document.getElementById('micCounters').textContent = `Chunks ${data.micChunks}, Errors ${data.micReadErrors}, Last ${data.micLastReadMs} ms`;
      const levelPercent = Math.max(0, Math.min(100, Math.round((data.micRms / 2500) * 100)));
      document.getElementById('micLevelFill').style.width = `${levelPercent}%`;
      document.getElementById('micLevelText').textContent = `${levelPercent}% (${data.micStatus})`;
      document.getElementById('factoryOled').textContent = data.oledReady ? 'OK' : `Error: ${data.oledError}`;
      document.getElementById('factoryIr').textContent = `${data.hasIrOn ? 'ON ada' : 'ON belum'} / ${data.hasIrOff ? 'OFF ada' : 'OFF belum'}`;
      document.getElementById('factorySpeaker').textContent = data.speakerReady ? (data.speakerRunning ? 'Playing' : 'OK') : `Error: ${data.speakerError}`;
      document.getElementById('factoryMic').textContent = data.micReady ? `OK RMS ${data.micRms}` : `Error: ${data.micError}`;
      document.getElementById('factoryMeter').textContent = meter.valid ? `${meter.voltageV.toFixed(1)} V, ${meter.currentA.toFixed(3)} A` : `${meter.status}: ${meter.lastError}`;
      updateMicTest(data);
    }

    function updateMicTest(data) {
      const now = Date.now();
      if (micTestUntil <= 0) return;

      micTestPeak = Math.max(micTestPeak, data.micPeak);
      micTestBestRms = Math.max(micTestBestRms, data.micRms);
      const remainingMs = micTestUntil - now;

      if (remainingMs > 0) {
        document.getElementById('micTestResult').textContent = `Bicara sekarang... ${Math.ceil(remainingMs / 1000)} detik. RMS max ${micTestBestRms}, peak ${micTestPeak}.`;
        return;
      }

      const passed = micTestBestRms > 80 || micTestPeak > 500;
      document.getElementById('micTestResult').textContent = passed
        ? `MIC OK. Sinyal terdeteksi. RMS max ${micTestBestRms}, peak ${micTestPeak}.`
        : `NO SIGNAL. RMS max ${micTestBestRms}, peak ${micTestPeak}. Cek VDD, GND, BCLK GPIO6, WS GPIO7, SD GPIO15, dan L/R ke GND.`;
      micTestUntil = 0;
    }

    async function startMicTest() {
      micTestPeak = 0;
      micTestBestRms = 0;
      micTestUntil = Date.now() + 3000;
      document.getElementById('micTestResult').textContent = 'Bicara sekarang...';
      await refresh();
    }

    async function resetMic() {
      await fetch('/microphone?action=reset', { cache: 'no-store' });
      micTestUntil = 0;
      micTestPeak = 0;
      micTestBestRms = 0;
      document.getElementById('micTestResult').textContent = 'Counter microphone direset. Tekan tombol test, lalu bicara dekat microphone.';
      await refresh();
    }

    function showVoiceVolume() {
      voiceVolumeDirty = true;
      document.getElementById('voiceVolumeLabel').textContent = `${document.getElementById('voiceVolume').value}%`;
    }

    async function saveVoiceVolume() {
      const value = document.getElementById('voiceVolume').value;
      await fetch(`/voice?action=volume&value=${value}`, { cache: 'no-store' });
      voiceVolumeDirty = false;
      await refresh();
    }

    async function voiceAction(action) {
      document.getElementById('voiceStatus').textContent = 'Queued';
      await fetch(`/voice?action=${action}`, { cache: 'no-store' });
      await refresh();
    }

    async function speakText() {
      const text = document.getElementById('voiceText').value.trim();
      if (!text) return;
      const query = new URLSearchParams({ action: 'speak', text });
      document.getElementById('voiceStatus').textContent = 'Queued';
      await fetch(`/voice?${query}`, { cache: 'no-store' });
      await refresh();
    }

    async function speakerTest(name) {
      document.getElementById('speakerMessage').textContent = 'Memulai test lokal...';
      await fetch(`/speaker-test?action=${name}`, { cache: 'no-store' });
      await refresh();
    }

    async function displayTest() {
      const res = await fetch('/display-test', { cache: 'no-store' });
      const data = await res.json();
      document.getElementById('factoryOled').textContent = data.ok ? 'Test tampil di OLED' : data.error;
      await refresh();
    }

    async function ledTest(color) {
      const res = await fetch(`/led-test?color=${color}`, { cache: 'no-store' });
      const data = await res.json();
      document.getElementById('factoryLed').textContent = data.message || color;
    }

    async function sendVoiceCommand() {
      const text = document.getElementById('voiceCommandText').value.trim();
      if (!text) return;
      const query = new URLSearchParams({ text });
      document.getElementById('voiceStatus').textContent = 'Memproses perintah';
      await fetch(`/voice-command?${query}`, { cache: 'no-store' });
      await refresh();
    }

    async function irCmd(url) {
      document.getElementById('irMessage').textContent = 'Memproses...';
      await fetch(url, { cache: 'no-store' });
      await refresh();
    }

    async function sensorTest() {
      document.getElementById('irMessage').textContent = 'Mulai test 4 detik. Tekan dan tahan tombol remote ke receiver sekarang.';
      const res = await fetch('/sensor-test', { cache: 'no-store' });
      const data = await res.json();
      document.getElementById('irMessage').textContent = `${data.message} Transisi: ${data.transitions}`;
      await refresh();
    }

    async function sendAcState() {
      const temp = document.getElementById('controlTemp').value;
      const mode = document.getElementById('controlMode').value;
      const fan = document.getElementById('controlFan').value;
      await irCmd(`/temperature?action=send&value=${temp}&mode=${mode}&fan=${fan}`);
    }

    async function saveProtocol() {
      const protocol = document.getElementById('protocolSelect').value;
      await irCmd(`/protocol?value=${protocol}`);
    }

    async function saveWifi() {
      const ssid = encodeURIComponent(document.getElementById('wifiSsid').value);
      const password = encodeURIComponent(document.getElementById('wifiPassword').value);
      await irCmd(`/wifi?action=save&ssid=${ssid}&password=${password}`);
      document.getElementById('wifiPassword').value = '';
      wifiFormDirty = false;
    }

    async function clearWifi() {
      await irCmd('/wifi?action=clear');
      document.getElementById('wifiSsid').value = '';
      document.getElementById('wifiPassword').value = '';
      wifiFormDirty = false;
    }

    function togglePassword() {
      const input = document.getElementById('wifiPassword');
      input.type = input.type === 'password' ? 'text' : 'password';
      input.focus();
    }

    document.getElementById('wifiSsid').addEventListener('input', () => { wifiFormDirty = true; });
    document.getElementById('wifiPassword').addEventListener('input', () => { wifiFormDirty = true; });
    refresh();
    setInterval(refresh, 1500);
  </script>
</body>
</html>
)HTML";

  return page;
}

void WebTask::sendJsonResponse()
{
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", buildStatusJson());
}

void WebTask::handleRoot()
{
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", buildPage());
}

void WebTask::handleStatus()
{
  sendJsonResponse();
}

void WebTask::handleVoice()
{
#if !ACTIVATE_TTS_MODULE
  server.send(409, "application/json", "{\"error\":\"Module TTS sedang nonaktif.\"}");
  return;
#else
  String action = server.arg("action");
  bool accepted = true;

  if (action == "speak")
  {
    accepted = voice.announceText(server.arg("text"));
  }
  else if (action == "startup")
  {
    accepted = voice.announceStartup();
  }
  else if (action == "volume")
  {
    uint8_t volume = (uint8_t)server.arg("value").toInt();
    voice.setVolume(volume);
    speaker.setVolume(volume);
  }
  else if (action == "stop")
  {
    voice.stop();
  }
  else
  {
    server.send(400, "application/json", "{\"error\":\"Aksi voice tidak dikenal.\"}");
    return;
  }

  if (!accepted)
  {
    server.send(409, "application/json", "{\"error\":\"TTS belum siap atau teks tidak valid.\"}");
    return;
  }

  sendJsonResponse();
#endif
}

void WebTask::handleVoiceCommand()
{
#if !ACTIVATE_TTS_MODULE
  server.send(409, "application/json", "{\"error\":\"Module TTS sedang nonaktif.\"}");
  return;
#else
  String text = server.arg("text");
  text.toLowerCase();
  text.trim();

  if (text.length() == 0)
  {
    server.send(400, "application/json", "{\"error\":\"Teks perintah kosong.\"}");
    return;
  }

  bool wakePhrase = text.indexOf("halo stroomer") >= 0 ||
                    text.indexOf("hello stroomer") >= 0;
  bool wakeActive = millis() < voiceWakeUntilMs;

  if (wakePhrase)
  {
    voiceWakeUntilMs = millis() + 15000;
    voice.announceText("IYA KAKA");
    sendJsonResponse();
    return;
  }

  if (!wakeActive)
  {
#if ENABLE_TTS_LOGS
    Serial.print("Voice command ignored while standby: ");
    Serial.println(text);
#endif
    sendJsonResponse();
    return;
  }

  voiceWakeUntilMs = millis() + 15000;

  if (text.indexOf("nyalakan ac") >= 0 || text.indexOf("hidupkan ac") >= 0)
  {
#if ACTIVATE_IR_MODULE
    if (acIr.sendPowerOn()) voice.announceAcOn();
#else
    voice.announceText("Module IR sedang nonaktif");
#endif
  }
  else if (text.indexOf("matikan ac") >= 0)
  {
#if ACTIVATE_IR_MODULE
    if (acIr.sendPowerOff()) voice.announceAcOff();
#else
    voice.announceText("Module IR sedang nonaktif");
#endif
  }
  else if (text.indexOf("atur remote") >= 0)
  {
#if ACTIVATE_IR_MODULE
    remoteSetupStep = 1;
    remoteSetupLearnSequence = acIr.learnSequence();
    acIr.startLearning(LEARN_POWER_OFF);
    voice.announceText("Arahkan remote ke saya dan jalankan perintah saya. Matikan AC.");
#else
    voice.announceText("Module IR sedang nonaktif");
#endif
  }
  else
  {
    voice.announceText("Perintah belum dikenali");
  }

  sendJsonResponse();
#endif
}

void WebTask::handleMicrophone()
{
#if !ACTIVATE_MIC_MODULE
  server.send(409, "application/json", "{\"error\":\"Module microphone sedang nonaktif.\"}");
  return;
#else
  String action = server.arg("action");

  if (action == "reset")
  {
    microphone.resetStats();
  }
  else
  {
    server.send(400, "application/json", "{\"error\":\"Aksi microphone tidak dikenal.\"}");
    return;
  }

  sendJsonResponse();
#endif
}

void WebTask::handleSpeakerTest()
{
#if !ACTIVATE_SPEAKER_MODULE
  server.send(409, "application/json", "{\"error\":\"Module speaker sedang nonaktif.\"}");
  return;
#else
  String action = server.arg("action");
  action.trim();

  if (action.length() == 0)
  {
    server.send(400, "application/json", "{\"error\":\"Aksi speaker kosong.\"}");
    return;
  }

#if ACTIVATE_TTS_MODULE
  voice.stop();
  speaker.setVolume(voice.volume());
#endif
  bool accepted = speaker.enqueueByName(action);
  if (!accepted)
  {
    server.send(400, "application/json", "{\"error\":\"Aksi speaker tidak dikenal.\"}");
    return;
  }

  sendJsonResponse();
#endif
}

void WebTask::handleDisplayTest()
{
#if !ACTIVATE_OLED_MODULE
  server.send(409, "application/json", "{\"ok\":false,\"error\":\"Module OLED sedang nonaktif.\"}");
  return;
#else
  bool ok = display.showTestScreen("Device A Test",
                                   "OLED GPIO8/GPIO9",
                                   "IR 4/5  MIC 6/7/15",
                                   "SPK 12/13/14 RS485",
                                   7000);
  server.sendHeader("Cache-Control", "no-store");
  if (ok)
  {
    server.send(200, "application/json", "{\"ok\":true,\"message\":\"OLED test tampil 7 detik.\"}");
  }
  else
  {
    server.send(409, "application/json", "{\"ok\":false,\"error\":\"OLED belum siap atau tidak terdeteksi.\"}");
  }
#endif
}

void WebTask::handleLedTest()
{
  String color = server.arg("color");
  color.toLowerCase();
  color.trim();

  if (color == "red")
  {
    led.set(255, 0, 0);
  }
  else if (color == "green")
  {
    led.set(0, 255, 0);
  }
  else if (color == "blue")
  {
    led.set(0, 0, 255);
  }
  else if (color == "white")
  {
    led.set(80, 80, 80);
  }
  else if (color == "yellow")
  {
    led.set(255, 160, 0);
  }
  else if (color == "off")
  {
    led.set(0, 0, 0);
  }
  else
  {
    server.send(400, "application/json", "{\"error\":\"Warna LED tidak dikenal.\"}");
    return;
  }

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", "{\"ok\":true,\"message\":\"LED RGB berubah.\"}");
}

void WebTask::handleLearn()
{
#if !ACTIVATE_IR_MODULE
  server.send(409, "application/json", "{\"error\":\"Module IR sedang nonaktif.\"}");
  return;
#else
  String slot = server.arg("slot");

  if (slot == "on") acIr.startLearning(LEARN_POWER_ON);
  else if (slot == "off") acIr.startLearning(LEARN_POWER_OFF);
  else acIr.startLearning(LEARN_NONE);

  sendJsonResponse();
#endif
}

void WebTask::handleSend()
{
#if !ACTIVATE_IR_MODULE
  server.send(409, "application/json", "{\"error\":\"Module IR sedang nonaktif.\"}");
  return;
#else
  String slot = server.arg("slot");

  if (slot == "on")
  {
#if ACTIVATE_TTS_MODULE
    if (acIr.sendPowerOn()) voice.announceAcOn();
#else
    acIr.sendPowerOn();
#endif
  }
  else if (slot == "off")
  {
#if ACTIVATE_TTS_MODULE
    if (acIr.sendPowerOff()) voice.announceAcOff();
#else
    acIr.sendPowerOff();
#endif
  }
  else
  {
#if ENABLE_IR_LOGS
    Serial.println("Slot kirim tidak dikenal.");
#endif
  }

  sendJsonResponse();
#endif
}

void WebTask::handleClearIr()
{
#if !ACTIVATE_IR_MODULE
  server.send(409, "application/json", "{\"error\":\"Module IR sedang nonaktif.\"}");
  return;
#else
  acIr.clearLearnedSignals();
  sendJsonResponse();
#endif
}

void WebTask::handleSensorTest()
{
#if !ACTIVATE_IR_MODULE
  server.send(409, "application/json", "{\"error\":\"Module IR sedang nonaktif.\"}");
  return;
#else
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", acIr.runSensorTest());
#endif
}

void WebTask::handleTemperature()
{
#if !ACTIVATE_IR_MODULE
  server.send(409, "application/json", "{\"error\":\"Module IR sedang nonaktif.\"}");
  return;
#else
  uint8_t temperature = (uint8_t)server.arg("value").toInt();
  String action = server.arg("action");

  if (action == "send")
  {
    if (acIr.sendAcState(temperature, server.arg("mode"), server.arg("fan")))
    {
#if ACTIVATE_TTS_MODULE
      voice.announceTemperatureSet(temperature);
#endif
    }
  }
  else
  {
    server.send(400, "application/json", "{\"error\":\"Aksi suhu tidak dikenal.\"}");
    return;
  }

  sendJsonResponse();
#endif
}

void WebTask::handleProtocol()
{
#if !ACTIVATE_IR_MODULE
  server.send(409, "application/json", "{\"error\":\"Module IR sedang nonaktif.\"}");
  return;
#else
  decode_type_t protocol = (decode_type_t)server.arg("value").toInt();
  acIr.setAcProtocol(protocol);
#if ACTIVATE_TTS_MODULE
  if (acIr.acProtocol() == protocol) voice.announceProtocolSet(acIr.acProtocolName());
#endif
  sendJsonResponse();
#endif
}

void WebTask::handleWifi()
{
  String action = server.arg("action");

  if (action == "save")
  {
    wifi.saveCredentials(server.arg("ssid"), server.arg("password"));
#if ACTIVATE_TTS_MODULE
    voice.announceWifiSaved();
#endif
  }
  else if (action == "clear")
  {
    wifi.clearCredentials();
  }
  else
  {
    server.send(400, "application/json", "{\"error\":\"Aksi WiFi tidak dikenal.\"}");
    return;
  }

  sendJsonResponse();
}

void WebTask::handleFavicon()
{
  server.send(204);
}

void WebTask::handleNotFound()
{
  server.send(404, "text/plain", "Not found");
}
