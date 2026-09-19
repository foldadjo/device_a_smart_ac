#pragma once

#include <Arduino.h>
#include <Preferences.h>

class WifiConnector
{
public:
  WifiConnector(const char *apSsid, const char *apPassword);

  void begin();
  void service();
  void saveCredentials(const String &ssid, const String &password);
  void clearCredentials();

  bool hasCredentials() const;
  bool isConnected() const;
  String apIp() const;
  String staIp() const;
  const String &staSsid() const;
  const String &staPassword() const;
  const String &status() const;
  void restoreAccessPoint();

private:
  void loadCredentials();
  void connectStation(bool force);

  const char *apSsid;
  const char *apPassword;
  Preferences prefs;
  String savedSsid;
  String savedPassword;
  String wifiStatus;
  uint32_t lastConnectAttemptMs;
};
