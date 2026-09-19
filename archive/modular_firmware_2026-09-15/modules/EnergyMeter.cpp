#include "EnergyMeter.h"

#include "config.h"
#include "DebugConfig.h"

namespace
{
HardwareSerial meterSerial(1);
}

EnergyMeter::EnergyMeter(uint8_t rxPin,
                         uint8_t txPin,
                         uint8_t deRePin,
                         uint32_t baud,
                         uint32_t serialConfig,
                         uint8_t slaveId) :
  rx(rxPin),
  tx(txPin),
  deRe(deRePin),
  baud(baud),
  serialConfig(serialConfig)
{
  meterData.valid = false;
  meterData.lastReadMs = 0;
  meterData.successCount = 0;
  meterData.errorCount = 0;
  meterData.slaveId = slaveId;
  meterData.status = "Belum dibaca";
  meterData.lastError = "-";
  meterData.lastRequestHex = "-";
  meterData.lastResponseHex = "-";
  meterData.lastResponseBytes = 0;
  meterData.rawTotalActiveEnergy = 0;
  meterData.rawForwardActiveEnergy = 0;
  meterData.rawReverseActiveEnergy = 0;
  meterData.rawVoltage = 0;
  meterData.rawCurrent = 0;
  meterData.rawActivePower = 0;
  meterData.rawPowerFactor = 0;
  meterData.rawFrequency = 0;
  meterData.rawReactivePower = 0;
  meterData.rawApparentPower = 0;
  meterData.totalActiveEnergyKwh = 0;
  meterData.forwardActiveEnergyKwh = 0;
  meterData.reverseActiveEnergyKwh = 0;
  meterData.voltageV = 0;
  meterData.currentA = 0;
  meterData.activePowerKw = 0;
  meterData.powerFactor = 0;
  meterData.frequencyHz = 0;
  meterData.reactivePowerKvar = 0;
  meterData.apparentPowerKva = 0;
}

void EnergyMeter::begin()
{
  pinMode(deRe, OUTPUT);
  setTransmitMode(false);
  meterSerial.begin(baud, serialConfig, rx, tx);
  meterData.status = "UART siap";

#if ENABLE_MODBUS_LOGS
  Serial.println("Energy meter Modbus RTU init");
  Serial.print("RS485 RX GPIO");
  Serial.print(rx);
  Serial.print(" TX GPIO");
  Serial.print(tx);
  Serial.print(" DE/RE GPIO");
  Serial.print(deRe);
  Serial.print(" baud=");
  Serial.print(baud);
  Serial.print(" slave=");
  Serial.println(meterData.slaveId);
#endif
}

// Dipanggil dari loop utama untuk polling meter sesuai interval.
void EnergyMeter::service()
{
  uint32_t now = millis();
  if (now - meterData.lastReadMs < METER_POLL_INTERVAL_MS)
  {
    return;
  }

  meterData.lastReadMs = now;
  readRegisters();
}

const EnergyMeterData &EnergyMeter::data() const
{
  return meterData;
}

String EnergyMeter::buildJson() const
{
  String json = "{";
  json += "\"valid\":" + String(meterData.valid ? "true" : "false") + ",";
  json += "\"status\":\"" + jsonEscape(meterData.status) + "\",";
  json += "\"lastError\":\"" + jsonEscape(meterData.lastError) + "\",";
  json += "\"lastRequestHex\":\"" + jsonEscape(meterData.lastRequestHex) + "\",";
  json += "\"lastResponseHex\":\"" + jsonEscape(meterData.lastResponseHex) + "\",";
  json += "\"lastResponseBytes\":" + String(meterData.lastResponseBytes) + ",";
  json += "\"lastReadMs\":" + String(meterData.lastReadMs) + ",";
  json += "\"successCount\":" + String(meterData.successCount) + ",";
  json += "\"errorCount\":" + String(meterData.errorCount) + ",";
  json += "\"slaveId\":" + String(meterData.slaveId) + ",";
  json += "\"rxPin\":" + String(rx) + ",";
  json += "\"txPin\":" + String(tx) + ",";
  json += "\"deRePin\":" + String(deRe) + ",";
  json += "\"baud\":" + String(baud) + ",";
  json += "\"rawTotalActiveEnergy\":" + String(meterData.rawTotalActiveEnergy) + ",";
  json += "\"rawForwardActiveEnergy\":" + String(meterData.rawForwardActiveEnergy) + ",";
  json += "\"rawReverseActiveEnergy\":" + String(meterData.rawReverseActiveEnergy) + ",";
  json += "\"rawVoltage\":" + String(meterData.rawVoltage) + ",";
  json += "\"rawCurrent\":" + String(meterData.rawCurrent) + ",";
  json += "\"rawActivePower\":" + String(meterData.rawActivePower) + ",";
  json += "\"rawPowerFactor\":" + String(meterData.rawPowerFactor) + ",";
  json += "\"rawFrequency\":" + String(meterData.rawFrequency) + ",";
  json += "\"rawReactivePower\":" + String(meterData.rawReactivePower) + ",";
  json += "\"rawApparentPower\":" + String(meterData.rawApparentPower) + ",";
  json += "\"totalActiveEnergyKwh\":" + String(meterData.totalActiveEnergyKwh, 2) + ",";
  json += "\"forwardActiveEnergyKwh\":" + String(meterData.forwardActiveEnergyKwh, 2) + ",";
  json += "\"reverseActiveEnergyKwh\":" + String(meterData.reverseActiveEnergyKwh, 2) + ",";
  json += "\"voltageV\":" + String(meterData.voltageV, 1) + ",";
  json += "\"currentA\":" + String(meterData.currentA, 3) + ",";
  json += "\"activePowerKw\":" + String(meterData.activePowerKw, 4) + ",";
  json += "\"powerFactor\":" + String(meterData.powerFactor, 3) + ",";
  json += "\"frequencyHz\":" + String(meterData.frequencyHz, 2) + ",";
  json += "\"reactivePowerKvar\":" + String(meterData.reactivePowerKvar, 4) + ",";
  json += "\"apparentPowerKva\":" + String(meterData.apparentPowerKva, 4);
  json += "}";
  return json;
}

uint8_t EnergyMeter::rxPin() const { return rx; }
uint8_t EnergyMeter::txPin() const { return tx; }
uint8_t EnergyMeter::deRePin() const { return deRe; }
uint32_t EnergyMeter::baudRate() const { return baud; }

// Kirim Function 03, baca 17 register, validasi CRC, lalu parse data meter.
bool EnergyMeter::readRegisters()
{
  while (meterSerial.available()) meterSerial.read();

  uint8_t request[8];
  request[0] = meterData.slaveId;
  request[1] = 0x03;
  request[2] = START_REGISTER >> 8;
  request[3] = START_REGISTER & 0xFF;
  request[4] = 0;
  request[5] = REGISTER_COUNT;
  uint16_t requestCrc = crc16(request, 6);
  request[6] = requestCrc & 0xFF;
  request[7] = requestCrc >> 8;
  meterData.lastRequestHex = bytesToHex(request, sizeof(request));

  setTransmitMode(true);
  meterSerial.write(request, sizeof(request));
  meterSerial.flush();
  setTransmitMode(false);

  const uint8_t responseLength = 5 + (REGISTER_COUNT * 2);
  uint8_t response[responseLength];
  uint8_t bytesRead = readResponse(response, responseLength, RESPONSE_TIMEOUT_MS);
  meterData.lastResponseBytes = bytesRead;
  meterData.lastResponseHex = bytesRead > 0 ? bytesToHex(response, bytesRead) : "-";

  if (bytesRead == 5 && response[0] == meterData.slaveId && response[1] == (0x03 | 0x80))
  {
    setError("Exception Modbus code " + String(response[2]) + " response=" + meterData.lastResponseHex);
    return false;
  }

  if (bytesRead != responseLength)
  {
    setError("Timeout Modbus, rx " + String(bytesRead) + "/" + String(responseLength) + " byte, request=" + meterData.lastRequestHex + ", response=" + meterData.lastResponseHex);
    return false;
  }

  uint16_t actualCrc = (uint16_t)response[responseLength - 2] |
                       ((uint16_t)response[responseLength - 1] << 8);
  uint16_t expectedCrc = crc16(response, responseLength - 2);
  if (actualCrc != expectedCrc)
  {
    setError("CRC response Modbus salah, response=" + meterData.lastResponseHex);
    return false;
  }

  if (response[0] != meterData.slaveId || response[1] != 0x03 || response[2] != REGISTER_COUNT * 2)
  {
    setError("Header response Modbus tidak sesuai, response=" + meterData.lastResponseHex);
    return false;
  }

  uint16_t registers[REGISTER_COUNT];
  for (uint8_t i = 0; i < REGISTER_COUNT; i++)
  {
    uint8_t offset = 3 + (i * 2);
    registers[i] = ((uint16_t)response[offset] << 8) | response[offset + 1];
  }

  parseRegisters(registers);
  logValues(registers);
  meterData.valid = true;
  meterData.successCount++;
  meterData.status = "OK";
  meterData.lastError = "-";
  return true;
}

uint8_t EnergyMeter::readResponse(uint8_t *buffer, uint8_t maxLength, uint32_t timeoutMs)
{
  uint8_t index = 0;
  uint32_t startedAt = millis();
  uint32_t lastByteAt = startedAt;

  while (index < maxLength && millis() - startedAt < timeoutMs)
  {
    while (meterSerial.available() && index < maxLength)
    {
      buffer[index++] = (uint8_t)meterSerial.read();
      lastByteAt = millis();
    }

    if (index > 0 && millis() - lastByteAt > 80)
    {
      break;
    }

    delay(1);
    yield();
  }

  return index;
}

void EnergyMeter::setTransmitMode(bool transmit)
{
  digitalWrite(deRe, transmit ? HIGH : LOW);
  delayMicroseconds(120);
}

// Ubah register mentah menjadi satuan engineering sesuai datasheet DDS3366D-1P.
void EnergyMeter::parseRegisters(const uint16_t *registers)
{
  meterData.rawTotalActiveEnergy = combineS32(registers[0], registers[1]);
  meterData.rawForwardActiveEnergy = combineU32(registers[2], registers[3]);
  meterData.rawReverseActiveEnergy = combineU32(registers[4], registers[5]);
  meterData.rawVoltage = registers[6];
  meterData.rawCurrent = combineU32(registers[7], registers[8]);
  meterData.rawActivePower = combineS32(registers[9], registers[10]);
  meterData.rawPowerFactor = (int16_t)registers[11];
  meterData.rawFrequency = registers[12];
  meterData.rawReactivePower = combineS32(registers[13], registers[14]);
  meterData.rawApparentPower = combineU32(registers[15], registers[16]);

  meterData.totalActiveEnergyKwh = meterData.rawTotalActiveEnergy * 0.01f;
  meterData.forwardActiveEnergyKwh = meterData.rawForwardActiveEnergy * 0.01f;
  meterData.reverseActiveEnergyKwh = meterData.rawReverseActiveEnergy * 0.01f;
  meterData.voltageV = meterData.rawVoltage * 0.1f;
  meterData.currentA = meterData.rawCurrent * 0.001f;
  meterData.activePowerKw = meterData.rawActivePower * 0.0001f;
  meterData.powerFactor = meterData.rawPowerFactor * 0.001f;
  meterData.frequencyHz = meterData.rawFrequency * 0.01f;
  meterData.reactivePowerKvar = meterData.rawReactivePower * 0.0001f;
  meterData.apparentPowerKva = meterData.rawApparentPower * 0.0001f;
}

void EnergyMeter::logValues(const uint16_t *registers) const
{
#if ENABLE_MODBUS_LOGS
  Serial.println();
  Serial.println("******** ENERGY METER MODBUS OK ********");
  Serial.print("Raw registers:");
  for (uint8_t i = 0; i < REGISTER_COUNT; i++)
  {
    Serial.print(" R");
    Serial.print(i);
    Serial.print("=");
    Serial.print(registers[i]);
  }
  Serial.println();
  Serial.print("Total Active Energy raw=");
  Serial.print(meterData.rawTotalActiveEnergy);
  Serial.print(" value=");
  Serial.print(meterData.totalActiveEnergyKwh, 2);
  Serial.println(" kWh");
  Serial.print("Forward Active Energy raw=");
  Serial.print(meterData.rawForwardActiveEnergy);
  Serial.print(" value=");
  Serial.print(meterData.forwardActiveEnergyKwh, 2);
  Serial.println(" kWh");
  Serial.print("Reverse Active Energy raw=");
  Serial.print(meterData.rawReverseActiveEnergy);
  Serial.print(" value=");
  Serial.print(meterData.reverseActiveEnergyKwh, 2);
  Serial.println(" kWh");
  Serial.print("Voltage raw=");
  Serial.print(meterData.rawVoltage);
  Serial.print(" value=");
  Serial.print(meterData.voltageV, 1);
  Serial.println(" V");
  Serial.print("Current raw=");
  Serial.print(meterData.rawCurrent);
  Serial.print(" value=");
  Serial.print(meterData.currentA, 3);
  Serial.println(" A");
  Serial.print("Active Power raw=");
  Serial.print(meterData.rawActivePower);
  Serial.print(" value=");
  Serial.print(meterData.activePowerKw, 4);
  Serial.println(" kW");
  Serial.print("Power Factor raw=");
  Serial.print(meterData.rawPowerFactor);
  Serial.print(" value=");
  Serial.println(meterData.powerFactor, 3);
  Serial.print("Frequency raw=");
  Serial.print(meterData.rawFrequency);
  Serial.print(" value=");
  Serial.print(meterData.frequencyHz, 2);
  Serial.println(" Hz");
  Serial.print("Reactive Power raw=");
  Serial.print(meterData.rawReactivePower);
  Serial.print(" value=");
  Serial.print(meterData.reactivePowerKvar, 4);
  Serial.println(" kvar");
  Serial.print("Apparent Power raw=");
  Serial.print(meterData.rawApparentPower);
  Serial.print(" value=");
  Serial.print(meterData.apparentPowerKva, 4);
  Serial.println(" kVA");
  Serial.println("***************************************");
#else
  (void)registers;
#endif
}

void EnergyMeter::setError(const String &message)
{
  meterData.valid = false;
  meterData.errorCount++;
  meterData.status = "Error";
  meterData.lastError = message;
#if ENABLE_MODBUS_LOGS
  Serial.print("ENERGY METER ERROR: ");
  Serial.println(message);
#endif
}

String EnergyMeter::bytesToHex(const uint8_t *buffer, uint8_t length) const
{
  const char *hex = "0123456789ABCDEF";
  String out;
  out.reserve((length * 3) + 1);

  for (uint8_t i = 0; i < length; i++)
  {
    if (i > 0) out += ' ';
    out += hex[(buffer[i] >> 4) & 0x0F];
    out += hex[buffer[i] & 0x0F];
  }

  return out;
}

uint16_t EnergyMeter::crc16(const uint8_t *buffer, uint8_t length) const
{
  uint16_t crc = 0xFFFF;
  for (uint8_t pos = 0; pos < length; pos++)
  {
    crc ^= buffer[pos];
    for (uint8_t i = 0; i < 8; i++)
    {
      if (crc & 0x0001)
      {
        crc >>= 1;
        crc ^= 0xA001;
      }
      else
      {
        crc >>= 1;
      }
    }
  }
  return crc;
}

uint32_t EnergyMeter::combineU32(uint16_t highWord, uint16_t lowWord) const
{
  return ((uint32_t)highWord << 16) | lowWord;
}

int32_t EnergyMeter::combineS32(uint16_t highWord, uint16_t lowWord) const
{
  return (int32_t)combineU32(highWord, lowWord);
}

String EnergyMeter::jsonEscape(const String &value) const
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
