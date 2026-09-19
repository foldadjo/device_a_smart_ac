#include "WifiConnector.h"

#include "DebugConfig.h"

#include <WiFi.h>

namespace
{
const uint32_t WIFI_RETRY_INTERVAL_MS = 15000;
}

WifiConnector::WifiConnector(const char *apSsid, const char *apPassword) :
  apSsid(apSsid),
  apPassword(apPassword),
  wifiStatus("Not initialized"),
  lastConnectAttemptMs(0)
{
}

void WifiConnector::begin()
{
  // Nyalakan mode AP+STA agar Web UI tetap bisa dibuka saat STA belum terkoneksi.
  prefs.begin("wifi", false);
  loadCredentials();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid, apPassword);
  wifiStatus = "AP aktif";

  if (hasCredentials())
  {
    connectStation(true);
  }
}

void WifiConnector::service()
{
  // Retry koneksi STA berkala tanpa memblokir loop utama.
  if (!hasCredentials())
  {
    return;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    wifiStatus = "STA connected";
    return;
  }

  uint32_t now = millis();
  if (now - lastConnectAttemptMs >= WIFI_RETRY_INTERVAL_MS)
  {
    connectStation(false);
  }
}

void WifiConnector::saveCredentials(const String &ssid, const String &password)
{
  // Simpan credential dari Web UI lalu langsung coba sambungkan STA.
  savedSsid = ssid;
  savedPassword = password;
  savedSsid.trim();

  prefs.putString("ssid", savedSsid);
  prefs.putString("pass", savedPassword);
  connectStation(true);
}

void WifiConnector::clearCredentials()
{
  // Hapus credential tersimpan tanpa mematikan access point konfigurasi.
  prefs.remove("ssid");
  prefs.remove("pass");
  savedSsid = "";
  savedPassword = "";
  WiFi.disconnect(false, false);
  wifiStatus = "Credential WiFi dihapus";
}

bool WifiConnector::hasCredentials() const
{
  return savedSsid.length() > 0;
}

bool WifiConnector::isConnected() const
{
  return WiFi.status() == WL_CONNECTED;
}

String WifiConnector::apIp() const
{
  return WiFi.softAPIP().toString();
}

String WifiConnector::staIp() const
{
  return isConnected() ? WiFi.localIP().toString() : "-";
}

const String &WifiConnector::staSsid() const
{
  return savedSsid;
}

const String &WifiConnector::staPassword() const
{
  return savedPassword;
}

const String &WifiConnector::status() const
{
  return wifiStatus;
}

void WifiConnector::restoreAccessPoint()
{
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid, apPassword);
}

void WifiConnector::loadCredentials()
{
  // Ambil credential WiFi dari NVS.
  savedSsid = prefs.getString("ssid", "");
  savedPassword = prefs.getString("pass", "");
}

void WifiConnector::connectStation(bool force)
{
  // Mulai koneksi STA; force dipakai setelah user menyimpan credential baru.
  if (!hasCredentials())
  {
    wifiStatus = "Belum ada credential WiFi";
    return;
  }

  uint32_t now = millis();
  if (!force && now - lastConnectAttemptMs < WIFI_RETRY_INTERVAL_MS)
  {
    return;
  }

  lastConnectAttemptMs = now;
  WiFi.begin(savedSsid.c_str(), savedPassword.c_str());
  wifiStatus = "Connecting to " + savedSsid;
#if ENABLE_WIFI_LOGS
  Serial.print("WiFi STA connecting to ");
  Serial.println(savedSsid);
#endif
}
