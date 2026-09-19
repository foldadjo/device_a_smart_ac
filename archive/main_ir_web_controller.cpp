#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>

#include "config.h"

// =====================================================
// PIN
// =====================================================

#define RGB_PIN    48

const uint8_t IR_RX_PIN = PIN_IR_RX; // KY-022 Receiver
const uint8_t IR_TX_PIN = PIN_IR_TX; // KY-005 Transmitter

// =====================================================
// CONFIG
// =====================================================

const char *AP_SSID = "AQU-AC-Remote";
const char *AP_PASSWORD = "12345678";

const uint16_t IR_RX_BUFFER_SIZE = IR_CAPTURE_BUFFER_SIZE;
const uint16_t MAX_RAW_SAMPLES = MAX_IR_RAW_LEN;
const uint8_t IR_RX_TIMEOUT_MS = IR_TIMEOUT_MS;
const uint16_t MIN_VALID_RAWLEN = IR_MIN_UNKNOWN_SIZE;
const uint8_t IR_SEND_KHZ = 38;

// =====================================================
// OBJECT
// =====================================================

Adafruit_NeoPixel rgb(
  1,
  RGB_PIN,
  NEO_GRB + NEO_KHZ800
);

IRrecv irrecv(
  IR_RX_PIN,
  IR_RX_BUFFER_SIZE,
  IR_RX_TIMEOUT_MS,
  true
);

IRsend irsend(IR_TX_PIN);
decode_results results;

WebServer server(80);
Preferences prefs;

// =====================================================
// LEARNED IR STORAGE
// =====================================================

enum LearnSlot
{
  LEARN_NONE,
  LEARN_POWER_ON,
  LEARN_POWER_OFF
};

LearnSlot learnSlot = LEARN_NONE;

uint16_t rawPowerOn[MAX_RAW_SAMPLES];
uint16_t rawPowerOff[MAX_RAW_SAMPLES];

uint16_t rawPowerOnLen = 0;
uint16_t rawPowerOffLen = 0;

String lastMessage = "Belum ada aksi.";

// =====================================================
// RGB
// =====================================================

void setRGB(uint8_t r, uint8_t g, uint8_t b)
{
  rgb.setPixelColor(
    0,
    rgb.Color(r, g, b)
  );

  rgb.show();
}

void idleLED()
{
  // MERAH
  setRGB(255, 0, 0);
}

void receiveLED()
{
  // HIJAU
  setRGB(0, 255, 0);
}

void transmitLED()
{
  // BIRU
  setRGB(0, 0, 255);
}

void learnLED()
{
  // KUNING
  setRGB(255, 180, 0);
}

// =====================================================
// HELPERS
// =====================================================

const char *slotLabel(LearnSlot slot)
{
  if (slot == LEARN_POWER_ON) return "ON";
  if (slot == LEARN_POWER_OFF) return "OFF";
  return "-";
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

// =====================================================
// NVS STORAGE
// =====================================================

void loadRaw(const char *lenKey, const char *rawKey, uint16_t *buffer, uint16_t &length)
{
  length = prefs.getUShort(lenKey, 0);

  if (length == 0 || length > MAX_RAW_SAMPLES)
  {
    length = 0;
    return;
  }

  size_t expectedBytes = length * sizeof(uint16_t);
  size_t actualBytes = prefs.getBytesLength(rawKey);

  if (actualBytes != expectedBytes)
  {
    length = 0;
    return;
  }

  prefs.getBytes(rawKey, buffer, expectedBytes);
}

void saveRaw(const char *lenKey, const char *rawKey, const uint16_t *buffer, uint16_t length)
{
  prefs.putUShort(lenKey, length);
  prefs.putBytes(rawKey, buffer, length * sizeof(uint16_t));
}

void clearRaw(const char *lenKey, const char *rawKey, uint16_t &length)
{
  prefs.remove(lenKey);
  prefs.remove(rawKey);
  length = 0;
}

void loadLearnedSignals()
{
  prefs.begin("ac-ir", false);

  loadRaw("onLen", "onRaw", rawPowerOn, rawPowerOnLen);
  loadRaw("offLen", "offRaw", rawPowerOff, rawPowerOffLen);
}

// =====================================================
// IR
// =====================================================

bool captureLearnedSignal(decode_results *decode)
{
  if (learnSlot == LEARN_NONE) return false;

  if (decode->overflow)
  {
    lastMessage = "Gagal belajar: buffer overflow. Jauhkan noise lalu coba lagi.";
    Serial.println(lastMessage);
    return true;
  }

  if (decode->rawlen <= MIN_VALID_RAWLEN)
  {
    lastMessage = "Sinyal terlalu pendek/noise. Tekan tombol remote AC sekali lagi.";
    Serial.println(lastMessage);
    return true;
  }

  uint16_t correctedLength = getCorrectedRawLength(decode);

  if (correctedLength == 0 || correctedLength > MAX_RAW_SAMPLES)
  {
    lastMessage = "Gagal belajar: raw terlalu panjang untuk buffer.";
    Serial.println(lastMessage);
    return true;
  }

  uint16_t *raw = resultToRawArray(decode);

  if (raw == nullptr)
  {
    lastMessage = "Gagal belajar: memori tidak cukup.";
    Serial.println(lastMessage);
    return true;
  }

  if (learnSlot == LEARN_POWER_ON)
  {
    memcpy(rawPowerOn, raw, correctedLength * sizeof(uint16_t));
    rawPowerOnLen = correctedLength;
    saveRaw("onLen", "onRaw", rawPowerOn, rawPowerOnLen);
  }
  else if (learnSlot == LEARN_POWER_OFF)
  {
    memcpy(rawPowerOff, raw, correctedLength * sizeof(uint16_t));
    rawPowerOffLen = correctedLength;
    saveRaw("offLen", "offRaw", rawPowerOff, rawPowerOffLen);
  }

  String learnedSlot = slotLabel(learnSlot);
  learnSlot = LEARN_NONE;
  delete[] raw;

  idleLED();

  lastMessage = "Berhasil belajar tombol " + learnedSlot + ".";
  Serial.println(lastMessage);

  return true;
}

void printIrDiagnostic(decode_results *decode)
{
  Serial.println();
  Serial.println("******** IR RECEIVED ********");

  Serial.print("Protocol : ");
  Serial.println(typeToString(decode->decode_type));

  Serial.print("Bits     : ");
  Serial.println(decode->bits);

  Serial.print("RawLen   : ");
  Serial.println(decode->rawlen);

  Serial.print("Overflow : ");
  Serial.println(decode->overflow ? "YES" : "NO");

  Serial.print("Value    : 0x");
  serialPrintUint64(decode->value, HEX);
  Serial.println();

  if (decode->rawlen <= MIN_VALID_RAWLEN)
  {
    Serial.println("Status   : pendek/noise, tapi receiver mendeteksi sinyal.");
  }
  else
  {
    Serial.println("Status   : frame cukup panjang.");
  }

  Serial.println("*****************************");
}

bool sendRawSignal(const uint16_t *raw, uint16_t length, const char *name)
{
  if (length == 0)
  {
    lastMessage = "Belum ada sinyal " + String(name) + ". Tekan Learn dulu.";
    return false;
  }

  irrecv.disableIRIn();
  transmitLED();

  Serial.print("Mengirim IR ");
  Serial.print(name);
  Serial.print(" rawlen=");
  Serial.println(length);

  irsend.sendRaw(raw, length, IR_SEND_KHZ);

  delay(180);
  idleLED();
  irrecv.enableIRIn(true);

  lastMessage = "Sinyal " + String(name) + " dikirim.";
  return true;
}

String runSensorTest()
{
  irrecv.disableIRIn();
  learnLED();

  uint32_t transitions = 0;
  uint32_t lowSamples = 0;
  uint32_t totalSamples = 0;
  uint8_t lastLevel = digitalRead(IR_RX_PIN);
  uint32_t startedAt = millis();

  while (millis() - startedAt < 4000)
  {
    uint8_t level = digitalRead(IR_RX_PIN);

    if (level == LOW)
    {
      lowSamples++;
    }

    if (level != lastLevel)
    {
      transitions++;
      lastLevel = level;
    }

    totalSamples++;
    delayMicroseconds(80);
    yield();
  }

  idleLED();
  pinMode(IR_RX_PIN, INPUT_PULLUP);
  irrecv.enableIRIn(true);

  bool stuckLow = lowSamples == totalSamples;
  bool stuckHigh = lowSamples == 0;
  bool detected = transitions > 20 && !stuckLow && !stuckHigh;

  if (detected)
  {
    lastMessage = "Test sensor OK: ada perubahan sinyal di GPIO " + String(IR_RX_PIN) + ".";
  }
  else if (stuckLow)
  {
    lastMessage = "Test sensor gagal: GPIO " + String(IR_RX_PIN) + " LOW terus. Cek VCC, GND, pin S, atau modul KY-022.";
  }
  else if (stuckHigh)
  {
    lastMessage = "Test sensor gagal: GPIO " + String(IR_RX_PIN) + " HIGH terus. Tekan remote lebih dekat atau cek arah receiver.";
  }
  else
  {
    lastMessage = "Test sensor gagal: perubahan sinyal terlalu sedikit di GPIO " + String(IR_RX_PIN) + ". Cek jarak, arah, dan wiring.";
  }

  Serial.println(lastMessage);
  Serial.print("Sensor test transitions=");
  Serial.print(transitions);
  Serial.print(" lowSamples=");
  Serial.print(lowSamples);
  Serial.print(" totalSamples=");
  Serial.println(totalSamples);

  String json = "{";
  json += "\"detected\":" + String(detected ? "true" : "false") + ",";
  json += "\"pin\":" + String(IR_RX_PIN) + ",";
  json += "\"transitions\":" + String(transitions) + ",";
  json += "\"lowSamples\":" + String(lowSamples) + ",";
  json += "\"totalSamples\":" + String(totalSamples) + ",";
  json += "\"message\":\"" + jsonEscape(lastMessage) + "\"";
  json += "}";

  return json;
}

// =====================================================
// WEB UI
// =====================================================

String buildStatusJson()
{
  String json = "{";
  json += "\"ip\":\"" + WiFi.softAPIP().toString() + "\",";
  json += "\"learning\":\"" + String(slotLabel(learnSlot)) + "\",";
  json += "\"hasOn\":" + String(rawPowerOnLen > 0 ? "true" : "false") + ",";
  json += "\"hasOff\":" + String(rawPowerOffLen > 0 ? "true" : "false") + ",";
  json += "\"onLen\":" + String(rawPowerOnLen) + ",";
  json += "\"offLen\":" + String(rawPowerOffLen) + ",";
  json += "\"rxPin\":" + String(IR_RX_PIN) + ",";
  json += "\"txPin\":" + String(IR_TX_PIN) + ",";
  json += "\"message\":\"" + jsonEscape(lastMessage) + "\"";
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
  <title>AQU AC Remote</title>
  <style>
    :root {
      color-scheme: light;
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: #f4f7f8;
      color: #172126;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      display: flex;
      align-items: stretch;
      justify-content: center;
      background: linear-gradient(180deg, #eef6f7 0%, #f8faf7 100%);
    }
    main {
      width: min(100%, 520px);
      padding: 24px 18px 32px;
    }
    header {
      margin-bottom: 18px;
    }
    h1 {
      margin: 0;
      font-size: 30px;
      line-height: 1.1;
      letter-spacing: 0;
    }
    .sub {
      margin: 8px 0 0;
      color: #53636b;
      font-size: 15px;
    }
    .panel {
      background: #ffffff;
      border: 1px solid #dce6e8;
      border-radius: 8px;
      padding: 16px;
      box-shadow: 0 10px 30px rgba(33, 54, 61, 0.08);
      margin-bottom: 14px;
    }
    .grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
    }
    button {
      width: 100%;
      min-height: 58px;
      border: 0;
      border-radius: 8px;
      color: #fff;
      font-size: 16px;
      font-weight: 750;
      letter-spacing: 0;
      touch-action: manipulation;
    }
    button:active { transform: translateY(1px); }
    .on { background: #157f56; }
    .off { background: #be3d35; }
    .learn { background: #285f8f; }
    .clear { background: #64727a; }
    .full { grid-column: 1 / -1; }
    .status {
      display: grid;
      gap: 8px;
      font-size: 15px;
    }
    .row {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
      border-bottom: 1px solid #eef2f3;
      padding-bottom: 8px;
    }
    .row:last-child {
      border-bottom: 0;
      padding-bottom: 0;
    }
    .label { color: #53636b; }
    .value {
      font-weight: 700;
      text-align: right;
      overflow-wrap: anywhere;
    }
    .message {
      margin-top: 12px;
      padding: 12px;
      border-radius: 8px;
      background: #edf5f1;
      color: #183f30;
      font-weight: 650;
      min-height: 44px;
    }
  </style>
</head>
<body>
  <main>
    <header>
      <h1>AQU AC Remote</h1>
      <p class="sub">Hotspot mode. Buka 192.168.4.1 dari HP yang tersambung ke WiFi alat.</p>
    </header>

    <section class="panel">
      <div class="grid">
        <button class="on" onclick="sendCmd('/send?slot=on')">Nyalakan AC</button>
        <button class="off" onclick="sendCmd('/send?slot=off')">Matikan AC</button>
        <button class="learn" onclick="sendCmd('/learn?slot=on')">Learn ON</button>
        <button class="learn" onclick="sendCmd('/learn?slot=off')">Learn OFF</button>
        <button class="learn full" onclick="sensorTest()">Test Sensor 4 Detik</button>
        <button class="clear full" onclick="sendCmd('/clear')">Hapus Rekaman</button>
      </div>
      <div class="message" id="message">Memuat status...</div>
    </section>

    <section class="panel status">
      <div class="row"><span class="label">IP alat</span><span class="value" id="ip">-</span></div>
      <div class="row"><span class="label">Pin RX/TX</span><span class="value" id="pins">-</span></div>
      <div class="row"><span class="label">Mode learn</span><span class="value" id="learning">-</span></div>
      <div class="row"><span class="label">Rekaman ON</span><span class="value" id="on">-</span></div>
      <div class="row"><span class="label">Rekaman OFF</span><span class="value" id="off">-</span></div>
    </section>
  </main>

  <script>
    async function refresh() {
      const res = await fetch('/status', { cache: 'no-store' });
      const data = await res.json();
      document.getElementById('ip').textContent = data.ip;
      document.getElementById('pins').textContent = `RX ${data.rxPin} / TX ${data.txPin}`;
      document.getElementById('learning').textContent = data.learning;
      document.getElementById('on').textContent = data.hasOn ? `Ada (${data.onLen})` : 'Belum ada';
      document.getElementById('off').textContent = data.hasOff ? `Ada (${data.offLen})` : 'Belum ada';
      document.getElementById('message').textContent = data.message;
    }
    async function sendCmd(url) {
      document.getElementById('message').textContent = 'Memproses...';
      await fetch(url, { cache: 'no-store' });
      await refresh();
    }
    async function sensorTest() {
      document.getElementById('message').textContent = 'Mulai test 4 detik. Tekan dan tahan tombol remote ke receiver sekarang.';
      const res = await fetch('/sensor-test', { cache: 'no-store' });
      const data = await res.json();
      document.getElementById('message').textContent = `${data.message} Transisi: ${data.transitions}`;
      await refresh();
    }
    refresh();
    setInterval(refresh, 1500);
  </script>
</body>
</html>
)HTML";

  return page;
}

void sendJsonResponse()
{
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", buildStatusJson());
}

void handleRoot()
{
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", buildPage());
}

void handleStatus()
{
  sendJsonResponse();
}

void handleFavicon()
{
  server.send(204);
}

void handleLearn()
{
  String slot = server.arg("slot");

  if (slot == "on")
  {
    learnSlot = LEARN_POWER_ON;
    learnLED();
    lastMessage = "Mode Learn ON aktif. Tekan tombol ON di remote asli sekarang.";
  }
  else if (slot == "off")
  {
    learnSlot = LEARN_POWER_OFF;
    learnLED();
    lastMessage = "Mode Learn OFF aktif. Tekan tombol OFF di remote asli sekarang.";
  }
  else
  {
    lastMessage = "Slot learn tidak dikenal.";
  }

  Serial.println(lastMessage);
  sendJsonResponse();
}

void handleSend()
{
  String slot = server.arg("slot");

  if (slot == "on")
  {
    sendRawSignal(rawPowerOn, rawPowerOnLen, "ON");
  }
  else if (slot == "off")
  {
    sendRawSignal(rawPowerOff, rawPowerOffLen, "OFF");
  }
  else
  {
    lastMessage = "Slot kirim tidak dikenal.";
  }

  sendJsonResponse();
}

void handleClear()
{
  clearRaw("onLen", "onRaw", rawPowerOnLen);
  clearRaw("offLen", "offRaw", rawPowerOffLen);

  learnSlot = LEARN_NONE;
  idleLED();

  lastMessage = "Semua rekaman IR dihapus.";
  Serial.println(lastMessage);

  sendJsonResponse();
}

void handleSensorTest()
{
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", runSensorTest());
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
  server.on("/favicon.ico", handleFavicon);
  server.on("/status", handleStatus);
  server.on("/learn", handleLearn);
  server.on("/send", handleSend);
  server.on("/clear", handleClear);
  server.on("/sensor-test", handleSensorTest);
  server.onNotFound(handleNotFound);
  server.begin();
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
  Serial.begin(115200);
  delay(1500);

  rgb.begin();
  rgb.clear();
  rgb.show();
  idleLED();

  irsend.begin();

  pinMode(IR_RX_PIN, INPUT_PULLUP);
  irrecv.enableIRIn(true);

  loadLearnedSignals();
  setupWebServer();

  Serial.println();
  Serial.println("======================================");
  Serial.println("ESP32-S3 AQU AC IR WEB REMOTE");
  Serial.println("======================================");

  Serial.print("WiFi SSID               : ");
  Serial.println(AP_SSID);

  Serial.print("WiFi Password           : ");
  Serial.println(AP_PASSWORD);

  Serial.print("Web UI                  : http://");
  Serial.println(WiFi.softAPIP());

  Serial.print("KY-022 Receiver GPIO    : ");
  Serial.println(IR_RX_PIN);

  Serial.print("KY-005 Transmitter GPIO : ");
  Serial.println(IR_TX_PIN);

  Serial.print("Saved ON rawlen         : ");
  Serial.println(rawPowerOnLen);

  Serial.print("Saved OFF rawlen        : ");
  Serial.println(rawPowerOffLen);

  Serial.println();
  Serial.println("Cara pakai:");
  Serial.println("1. Sambungkan HP ke WiFi AQU-AC-Remote.");
  Serial.println("2. Buka http://192.168.4.1");
  Serial.println("3. Jika perlu, tekan Test Sensor lalu tekan remote selama 4 detik.");
  Serial.println("4. Tekan Learn ON, lalu tekan tombol ON remote asli ke KY-022.");
  Serial.println("5. Tekan Learn OFF, lalu tekan tombol OFF remote asli ke KY-022.");
  Serial.println("6. Pakai tombol Nyalakan AC / Matikan AC.");
  Serial.println("======================================");
}

// =====================================================
// LOOP
// =====================================================

void loop()
{
  server.handleClient();

  if (irrecv.decode(&results))
  {
    receiveLED();

    if (!captureLearnedSignal(&results))
    {
      printIrDiagnostic(&results);
      delay(120);
      idleLED();
    }

    irrecv.resume();
  }

  delay(2);
}
