#include "AcIrController.h"

#include "DebugConfig.h"

namespace
{
const uint8_t IR_SEND_KHZ = 38;
const uint8_t AC_TEMP_MIN = 16;
const uint8_t AC_TEMP_MAX = 30;

void logIrLine(const String &message)
{
#if ENABLE_IR_LOGS
  Serial.println(message);
#else
  (void)message;
#endif
}

void logIrLine(const char *message)
{
#if ENABLE_IR_LOGS
  Serial.println(message);
#else
  (void)message;
#endif
}
}

AcIrController::AcIrController(uint8_t rxPin, uint8_t txPin, StatusLed &led) :
  rxPin(rxPin),
  txPin(txPin),
  led(led),
  receiver(rxPin, IR_CAPTURE_BUFFER_SIZE, IR_TIMEOUT_MS, true),
  transmitter(txPin),
  learnSlot(LEARN_NONE),
  receiverActive(false),
  rawPowerOnLen(0),
  rawPowerOffLen(0),
  learnedSequence(0),
  selectedAcProtocol(decode_type_t::UNKNOWN),
  selectedAcModel(-1),
  lastMessage("IR siap. Rekam tombol ON/OFF remote AC asli dulu.")
{
}

void AcIrController::begin()
{
  // Siapkan receiver/transmitter IR dan muat hasil learning dari NVS.
  pinMode(rxPin, INPUT_PULLUP);
  pinMode(txPin, INPUT);
  loadLearnedSignals();
  loadAcConfig();
  setReceiverActive(false);
}

void AcIrController::service()
{
  // Poll receiver IR; saat learn aktif, frame disimpan, selain itu hanya diagnostic.
  if (!receiverActive || !receiver.decode(&results))
  {
    return;
  }

  led.showIrReceive();

  if (!captureLearnedSignal(&results))
  {
    printDiagnostic(&results);
    lastMessage = "IR diterima. RawLen " + String(results.rawlen) + ".";
    delay(80);
    led.showIrLearn();
  }

  receiver.resume();
}

void AcIrController::startLearning(LearnSlot slot)
{
  // Aktifkan mode belajar untuk tombol ON/OFF remote AC asli.
  learnSlot = slot;
  setReceiverActive(true);
  led.showIrLearn();

  if (slot == LEARN_POWER_ON)
  {
    lastMessage = "Mode Learn ON aktif. Tekan tombol ON di remote asli sekarang.";
  }
  else if (slot == LEARN_POWER_OFF)
  {
    lastMessage = "Mode Learn OFF aktif. Tekan tombol OFF di remote asli sekarang.";
  }
  else
  {
    learnSlot = LEARN_NONE;
    setReceiverActive(false);
    led.showIrIdle();
    lastMessage = "Slot learn tidak dikenal.";
  }

  logIrLine(lastMessage);
}

bool AcIrController::sendPowerOn()
{
  return sendRawSignal(rawPowerOn, rawPowerOnLen, "ON");
}

bool AcIrController::sendPowerOff()
{
  return sendRawSignal(rawPowerOff, rawPowerOffLen, "OFF");
}

bool AcIrController::sendAcState(uint8_t temperature, const String &mode, const String &fan)
{
  // Kirim state AC via IRac jika protocol sudah dipilih atau terdeteksi saat learn.
  if (!isValidTemperature(temperature))
  {
    lastMessage = "Suhu tidak valid. Pilih 16 sampai 30.";
    logIrLine(lastMessage);
    return false;
  }

  if (!acProtocolReady())
  {
    lastMessage = "Protocol AC belum diset. Pilih protocol di menu Config atau Learn ON/OFF dulu.";
    logIrLine(lastMessage);
    return false;
  }

  learnSlot = LEARN_NONE;
  setReceiverActive(false);
  led.showIrTransmit();
  pinMode(txPin, OUTPUT);

  IRac ac(txPin);
  bool sent = ac.sendAc(selectedAcProtocol,
                        selectedAcModel,
                        true,
                        parseMode(mode),
                        temperature,
                        true,
                        parseFan(fan),
                        stdAc::swingv_t::kOff,
                        stdAc::swingh_t::kOff,
                        false,
                        false,
                        false,
                        false,
                        false,
                        false,
                        true);

  delay(180);
  pinMode(txPin, INPUT);
  led.showIrIdle();

  if (sent)
  {
    lastMessage = "AC state dikirim: " + acProtocolName() + ", " + String(temperature) + "C, " + mode + ", fan " + fan + ".";
  }
  else
  {
    lastMessage = "Gagal kirim AC state. Protocol belum didukung IRac atau pilihan tidak cocok.";
  }

  logIrLine(lastMessage);
  return sent;
}

void AcIrController::setAcProtocol(decode_type_t protocol)
{
  // Simpan protocol AC pilihan user untuk pengiriman state IR berikutnya.
  if (!isAcProtocolSupported(protocol))
  {
    lastMessage = "Protocol tidak didukung IRac.";
    logIrLine(lastMessage);
    return;
  }

  selectedAcProtocol = protocol;
  selectedAcModel = -1;
  saveAcProtocol();
  lastMessage = "Protocol AC diset ke " + acProtocolName() + ".";
  logIrLine(lastMessage);
}

void AcIrController::clearLearnedSignals()
{
  // Bersihkan raw ON/OFF yang tersimpan di NVS.
  clearRaw("onLen", "onRaw", rawPowerOnLen);
  clearRaw("offLen", "offRaw", rawPowerOffLen);
  learnSlot = LEARN_NONE;
  setReceiverActive(false);
  led.showIrIdle();
  lastMessage = "Semua rekaman IR dihapus.";
  logIrLine(lastMessage);
}

String AcIrController::runSensorTest()
{
  // Tes sensor IR dengan sampling GPIO singkat untuk mendeteksi perubahan level.
  setReceiverActive(false);
  led.showIrLearn();

  uint32_t transitions = 0;
  uint32_t lowSamples = 0;
  uint32_t totalSamples = 0;
  uint8_t lastLevel = digitalRead(rxPin);
  uint32_t startedAt = millis();

  while (millis() - startedAt < 4000)
  {
    uint8_t level = digitalRead(rxPin);

    if (level == LOW) lowSamples++;
    if (level != lastLevel)
    {
      transitions++;
      lastLevel = level;
    }

    totalSamples++;
    delayMicroseconds(80);
    yield();
  }

  pinMode(rxPin, INPUT_PULLUP);
  if (learnSlot == LEARN_NONE)
  {
    led.showIrIdle();
  }
  else
  {
    setReceiverActive(true);
    led.showIrLearn();
  }

  bool stuckLow = lowSamples == totalSamples;
  bool stuckHigh = lowSamples == 0;
  bool detected = transitions > 20 && !stuckLow && !stuckHigh;

  if (detected)
  {
    lastMessage = "Test sensor OK: ada perubahan sinyal di GPIO " + String(rxPin) + ".";
  }
  else if (stuckLow)
  {
    lastMessage = "Test sensor gagal: GPIO " + String(rxPin) + " LOW terus. Cek VCC, GND, dan pin data.";
  }
  else if (stuckHigh)
  {
    lastMessage = "Test sensor gagal: GPIO " + String(rxPin) + " HIGH terus. Tekan remote lebih dekat ke receiver.";
  }
  else
  {
    lastMessage = "Test sensor gagal: perubahan sinyal terlalu sedikit di GPIO " + String(rxPin) + ".";
  }

  logIrLine(lastMessage);
#if ENABLE_IR_LOGS
  Serial.print("Sensor test transitions=");
  Serial.print(transitions);
  Serial.print(" lowSamples=");
  Serial.print(lowSamples);
  Serial.print(" totalSamples=");
  Serial.println(totalSamples);
#endif

  String json = "{";
  json += "\"detected\":" + String(detected ? "true" : "false") + ",";
  json += "\"pin\":" + String(rxPin) + ",";
  json += "\"transitions\":" + String(transitions) + ",";
  json += "\"lowSamples\":" + String(lowSamples) + ",";
  json += "\"totalSamples\":" + String(totalSamples) + ",";
  json += "\"message\":\"" + jsonEscape(lastMessage) + "\"";
  json += "}";

  return json;
}

String AcIrController::learningLabel() const
{
  return slotLabel(learnSlot);
}

bool AcIrController::hasPowerOn() const
{
  return rawPowerOnLen > 0;
}

bool AcIrController::hasPowerOff() const
{
  return rawPowerOffLen > 0;
}

uint16_t AcIrController::powerOnLength() const
{
  return rawPowerOnLen;
}

uint16_t AcIrController::powerOffLength() const
{
  return rawPowerOffLen;
}

uint32_t AcIrController::learnSequence() const
{
  return learnedSequence;
}

uint8_t AcIrController::minTemperature() const
{
  return AC_TEMP_MIN;
}

uint8_t AcIrController::maxTemperature() const
{
  return AC_TEMP_MAX;
}

decode_type_t AcIrController::acProtocol() const
{
  return selectedAcProtocol;
}

String AcIrController::acProtocolName() const
{
  return typeToString(selectedAcProtocol);
}

bool AcIrController::acProtocolReady() const
{
  return isAcProtocolSupported(selectedAcProtocol);
}

bool AcIrController::isAcProtocolSupported(decode_type_t protocol) const
{
  return protocol != decode_type_t::UNKNOWN && IRac::isProtocolSupported(protocol);
}

uint8_t AcIrController::receiverPin() const
{
  return rxPin;
}

uint8_t AcIrController::transmitterPin() const
{
  return txPin;
}

const String &AcIrController::message() const
{
  return lastMessage;
}

void AcIrController::setReceiverActive(bool active)
{
  // Receiver dimatikan saat transmit supaya pin/peripheral IR tidak saling mengganggu.
  if (active == receiverActive)
  {
    return;
  }

  receiverActive = active;

  if (active)
  {
    pinMode(rxPin, INPUT_PULLUP);
    receiver.enableIRIn(true);
  }
  else
  {
    receiver.disableIRIn();
    pinMode(rxPin, INPUT_PULLUP);
  }
}

void AcIrController::loadLearnedSignals()
{
  // Load raw IR tombol ON/OFF dari NVS.
  prefs.begin("ac-ir", false);
  loadRaw("onLen", "onRaw", rawPowerOn, rawPowerOnLen);
  loadRaw("offLen", "offRaw", rawPowerOff, rawPowerOffLen);

  lastMessage = "IR siap. Rekaman ON: " + String(rawPowerOnLen) +
    ", OFF: " + String(rawPowerOffLen) + ".";
}

void AcIrController::loadAcConfig()
{
  // Load protocol AC yang dipilih user atau hasil decode saat learning.
  int16_t storedProtocol = prefs.getShort("acProto", (int16_t)decode_type_t::UNKNOWN);
  decode_type_t protocol = (decode_type_t)storedProtocol;

  if (isAcProtocolSupported(protocol))
  {
    selectedAcProtocol = protocol;
  }
  else
  {
    selectedAcProtocol = decode_type_t::UNKNOWN;
  }

  selectedAcModel = prefs.getShort("acModel", -1);
}

void AcIrController::saveAcProtocol()
{
  // Simpan protocol/model AC ke NVS.
  prefs.putShort("acProto", (int16_t)selectedAcProtocol);
  prefs.putShort("acModel", selectedAcModel);
}

void AcIrController::loadRaw(const char *lenKey, const char *rawKey, uint16_t *buffer, uint16_t &length)
{
  // Ambil buffer raw IR dan validasi ukuran byte sebelum dipakai.
  length = prefs.getUShort(lenKey, 0);

  if (length == 0 || length > MAX_IR_RAW_LEN)
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

void AcIrController::saveRaw(const char *lenKey, const char *rawKey, const uint16_t *buffer, uint16_t length)
{
  // Simpan raw IR sebagai array durasi mark/space mikrodetik.
  prefs.putUShort(lenKey, length);
  prefs.putBytes(rawKey, buffer, length * sizeof(uint16_t));
}

void AcIrController::clearRaw(const char *lenKey, const char *rawKey, uint16_t &length)
{
  prefs.remove(lenKey);
  prefs.remove(rawKey);
  length = 0;
}

bool AcIrController::isValidTemperature(uint8_t temperature) const
{
  return temperature >= AC_TEMP_MIN && temperature <= AC_TEMP_MAX;
}

bool AcIrController::captureLearnedSignal(decode_results *decode)
{
  // Saat mode learn aktif, konversi hasil decode ke raw dan simpan ke slot ON/OFF.
  if (learnSlot == LEARN_NONE) return false;

  if (decode->overflow)
  {
    lastMessage = "Gagal belajar: buffer overflow. Jauhkan noise lalu coba lagi.";
    logIrLine(lastMessage);
    return true;
  }

  if (decode->rawlen <= IR_MIN_UNKNOWN_SIZE)
  {
    lastMessage = "Sinyal terlalu pendek/noise. Tekan tombol remote AC sekali lagi.";
    logIrLine(lastMessage);
    return true;
  }

  uint16_t correctedLength = getCorrectedRawLength(decode);

  if (correctedLength == 0 || correctedLength > MAX_IR_RAW_LEN)
  {
    lastMessage = "Gagal belajar: raw IR terlalu panjang untuk buffer.";
    logIrLine(lastMessage);
    return true;
  }

  uint16_t *raw = resultToRawArray(decode);

  if (raw == nullptr)
  {
    lastMessage = "Gagal belajar: memori tidak cukup.";
    logIrLine(lastMessage);
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
  if (isAcProtocolSupported(decode->decode_type))
  {
    selectedAcProtocol = decode->decode_type;
    selectedAcModel = -1;
    saveAcProtocol();
    learnedSlot += " / protocol " + acProtocolName();
  }
  learnSlot = LEARN_NONE;
  learnedSequence++;
  delete[] raw;

  setReceiverActive(false);
  led.showIrIdle();
  lastMessage = "Berhasil belajar tombol " + learnedSlot + ".";
  logIrLine(lastMessage);
  return true;
}

void AcIrController::printDiagnostic(decode_results *decode)
{
#if ENABLE_IR_LOGS
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
  Serial.println(decode->rawlen <= IR_MIN_UNKNOWN_SIZE ? "Status   : pendek/noise." : "Status   : frame cukup panjang.");
  Serial.println("*****************************");
#else
  (void)decode;
#endif
}

bool AcIrController::sendRawSignal(const uint16_t *raw, uint16_t length, const char *name)
{
  // Kirim ulang raw IR hasil learning melalui LED/transmitter IR.
  if (length == 0)
  {
    lastMessage = "Belum ada sinyal " + String(name) + ". Tekan Learn dulu.";
    logIrLine(lastMessage);
    return false;
  }

  learnSlot = LEARN_NONE;
  setReceiverActive(false);
  led.showIrTransmit();

  pinMode(txPin, OUTPUT);
  transmitter.begin();

#if ENABLE_IR_LOGS
  Serial.print("Mengirim IR ");
  Serial.print(name);
  Serial.print(" rawlen=");
  Serial.println(length);
#endif

  transmitter.sendRaw(raw, length, IR_SEND_KHZ);

  delay(180);
  pinMode(txPin, INPUT);
  led.showIrIdle();

  lastMessage = "Sinyal " + String(name) + " dikirim.";
  logIrLine(lastMessage);
  return true;
}

stdAc::opmode_t AcIrController::parseMode(const String &mode) const
{
  if (mode == "cool") return stdAc::opmode_t::kCool;
  if (mode == "dry") return stdAc::opmode_t::kDry;
  if (mode == "fan") return stdAc::opmode_t::kFan;
  if (mode == "heat") return stdAc::opmode_t::kHeat;
  return stdAc::opmode_t::kAuto;
}

stdAc::fanspeed_t AcIrController::parseFan(const String &fan) const
{
  if (fan == "min") return stdAc::fanspeed_t::kMin;
  if (fan == "low") return stdAc::fanspeed_t::kLow;
  if (fan == "medium") return stdAc::fanspeed_t::kMedium;
  if (fan == "high") return stdAc::fanspeed_t::kHigh;
  if (fan == "max") return stdAc::fanspeed_t::kMax;
  return stdAc::fanspeed_t::kAuto;
}

String AcIrController::slotLabel(LearnSlot slot) const
{
  if (slot == LEARN_POWER_ON) return "ON";
  if (slot == LEARN_POWER_OFF) return "OFF";
  return "-";
}

String AcIrController::jsonEscape(const String &value) const
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
