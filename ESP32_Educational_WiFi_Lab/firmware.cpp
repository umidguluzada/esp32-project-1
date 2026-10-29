#include "firmware.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_idf_version.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "portal_html.h"

// The access point, DNS responder, and web server run only for clients that
// join this ESP32. The loop stays short so the task watchdog keeps running:
// network scans are asynchronous, and scan results are copied into a fixed
// table and then released.

static_assert(sizeof(LAB_AP_SSID) >= 2, "LAB_AP_SSID is empty");
static_assert(sizeof(LAB_AP_SSID) <= 33, "LAB_AP_SSID is longer than 32 characters");
static_assert(
    sizeof(LAB_AP_PASSWORD) == 1 || (sizeof(LAB_AP_PASSWORD) >= 9 && sizeof(LAB_AP_PASSWORD) <= 64),
    "LAB_AP_PASSWORD must be empty (open network) or 8 to 63 characters");
static_assert(LAB_AP_CHANNEL >= 1 && LAB_AP_CHANNEL <= 11, "Use an AP channel from 1 to 11");
static_assert(sizeof(LAB_ADMIN_USER) >= 2, "LAB_ADMIN_USER is empty");
static_assert(sizeof(LAB_ADMIN_PASSWORD) >= 9 && sizeof(LAB_ADMIN_PASSWORD) <= 64,
              "LAB_ADMIN_PASSWORD must be 8 to 63 characters");
static_assert(LAB_AP_MAX_CLIENTS >= 1 && LAB_AP_MAX_CLIENTS <= 10, "Max clients must be 1..10");
static_assert(LAB_NAME_MAX >= 1 && LAB_NAME_MAX <= 64, "LAB_NAME_MAX is out of range");
static_assert(LAB_NOTE_MAX >= 1 && LAB_NOTE_MAX <= 160, "LAB_NOTE_MAX is out of range");
static_assert(SCAN_MAX_RESULTS >= 1 && SCAN_MAX_RESULTS <= 32, "SCAN_MAX_RESULTS is out of range");

static const IPAddress kApIp(LAB_AP_IP_1, LAB_AP_IP_2, LAB_AP_IP_3, LAB_AP_IP_4);
static const IPAddress kSubnet(255, 255, 255, 0);

enum ScanState : uint8_t {
  SCAN_IDLE = 0,
  SCAN_RUNNING,
  SCAN_READY,
  SCAN_FAILED
};

struct ScanEntry {
  char ssid[33];
  int8_t rssi;
  uint8_t channel;
  uint8_t enc;
};

struct LabConfig {
  char deviceName[LAB_NAME_MAX + 1];
  char labNote[LAB_NOTE_MAX + 1];
  char profileName[LAB_NAME_MAX + 1];
  char preference[8];
  bool ledOn;
};

struct JsonWriter {
  char* buf;
  size_t cap;
  size_t len;
  bool ok;
};

static WebServer webServer(LAB_WEB_PORT);
static WiFiUDP dnsUdp;

static LabConfig cfg;
static ScanEntry scanList[SCAN_MAX_RESULTS];
static int scanCount = 0;
static ScanState scanState = SCAN_IDLE;
static const char* scanError = "";
static uint32_t scanStartedAt = 0;
static uint32_t scanFinishedAt = 0;
static bool apReady = false;
static char gMac[18] = "00:00:00:00:00:00";
static char gIpText[16] = "0.0.0.0";
static char gPortalLocation[40] = "http://192.168.4.1/";
static char gWifiQr[220] = "";
static char gJson[4096];

// DHCP reports the MAC and local IP before the browser sends a User-Agent.
// The portal GET looks this table up so one log line can show all of them.
struct JoinedClient {
  bool active;
  uint8_t mac[6];
  uint8_t ip[4];
  char path[48];
  char userAgent[120];
  char acceptLanguage[48];
  char host[48];
  char dns[4][64];
  uint8_t dnsCount;
  uint8_t dnsNext;
};

static JoinedClient joinedClients[LAB_AP_MAX_CLIENTS];
static uint32_t dnsQueryCount = 0;
static char dnsLastHost[96] = "";
static char dnsLoggedHost[96] = "";

// Recent lines for the password-protected admin page. The public portal does not read this.
static const int kLogLines = 20;
static const int kLogChars = 88;

struct SessionLine {
  uint32_t id;
  char text[kLogChars];
};

static SessionLine sessionLog[kLogLines];
static uint8_t sessionLogNext = 0;
static uint8_t sessionLogCount = 0;
static uint32_t sessionLogSeq = 0;

static void jwInit(JsonWriter& writer, char* buf, size_t cap) {
  writer.buf = buf;
  writer.cap = cap;
  writer.len = 0;
  writer.ok = cap > 0;
  if (cap > 0) {
    buf[0] = '\0';
  }
}

static void jwAdd(JsonWriter& writer, const char* text) {
  if (!writer.ok || text == nullptr) {
    return;
  }
  const size_t n = strlen(text);
  if (writer.len + n >= writer.cap) {
    writer.ok = false;
    return;
  }
  memcpy(writer.buf + writer.len, text, n);
  writer.len += n;
  writer.buf[writer.len] = '\0';
}

static void jwFmt(JsonWriter& writer, const char* fmt, ...) {
  if (!writer.ok) {
    return;
  }
  va_list args;
  va_start(args, fmt);
  const int wrote = vsnprintf(writer.buf + writer.len, writer.cap - writer.len, fmt, args);
  va_end(args);
  if (wrote < 0 || static_cast<size_t>(wrote) >= writer.cap - writer.len) {
    writer.ok = false;
    return;
  }
  writer.len += static_cast<size_t>(wrote);
}

static void jwEsc(JsonWriter& writer, const char* text) {
  if (!writer.ok) {
    return;
  }
  if (text == nullptr) {
    text = "";
  }
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p != 0; ++p) {
    char tmp[8];
    if (*p == '"' || *p == '\\') {
      tmp[0] = '\\';
      tmp[1] = static_cast<char>(*p);
      tmp[2] = '\0';
    } else if (*p < 0x20) {
      snprintf(tmp, sizeof(tmp), "\\u%04x", *p);
    } else {
      tmp[0] = static_cast<char>(*p);
      tmp[1] = '\0';
    }
    jwAdd(writer, tmp);
    if (!writer.ok) {
      return;
    }
  }
}

static void sendJsonRaw(int code, const char* body) {
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(code, "application/json", body);
}

static void sendWriter(int code, JsonWriter& writer, const char* fallback) {
  if (!writer.ok) {
    sendJsonRaw(500, fallback);
    return;
  }
  sendJsonRaw(code, writer.buf);
}

static const char* encName(uint8_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN:
      return "Open";
    case WIFI_AUTH_WEP:
      return "WEP";
    case WIFI_AUTH_WPA_PSK:
      return "WPA";
    case WIFI_AUTH_WPA2_PSK:
      return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return "WPA2-Enterprise";
    case WIFI_AUTH_WPA3_PSK:
      return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return "WPA2/WPA3";
    case WIFI_AUTH_WAPI_PSK:
      return "WAPI";
    default:
      return "Unknown";
  }
}

static const char* scanStateName() {
  switch (scanState) {
    case SCAN_RUNNING:
      return "running";
    case SCAN_READY:
      return "ready";
    case SCAN_FAILED:
      return "failed";
    default:
      return "idle";
  }
}

static uint32_t scanAgeSeconds() {
  if (scanState != SCAN_READY) {
    return 0;
  }
  return (millis() - scanFinishedAt) / 1000UL;
}

static void appendQrPiece(char* out, size_t cap, size_t* used, const char* src, bool escape) {
  if (src == nullptr) {
    return;
  }
  for (; *src != '\0' && *used + 2 < cap; ++src) {
    if (escape && (*src == '\\' || *src == ';' || *src == ',' || *src == ':' || *src == '"')) {
      out[(*used)++] = '\\';
    }
    if (*used + 1 >= cap) {
      break;
    }
    out[(*used)++] = *src;
  }
  out[*used] = '\0';
}

static void buildWifiQrText() {
  size_t n = 0;
  gWifiQr[0] = '\0';
  const bool open = LAB_AP_PASSWORD[0] == '\0';
  appendQrPiece(gWifiQr, sizeof(gWifiQr), &n, open ? "WIFI:T:nop;S:" : "WIFI:T:WPA;S:", false);
  appendQrPiece(gWifiQr, sizeof(gWifiQr), &n, LAB_AP_SSID, true);
  if (!open) {
    appendQrPiece(gWifiQr, sizeof(gWifiQr), &n, ";P:", false);
    appendQrPiece(gWifiQr, sizeof(gWifiQr), &n, LAB_AP_PASSWORD, true);
  }
  appendQrPiece(gWifiQr, sizeof(gWifiQr), &n, ";;", false);
}

static void cacheIdentity() {
  uint8_t mac[6] = {0};
  WiFi.softAPmacAddress(mac);
  snprintf(gMac, sizeof(gMac), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  const IPAddress ip = WiFi.softAPIP();
  snprintf(gIpText, sizeof(gIpText), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  snprintf(gPortalLocation, sizeof(gPortalLocation), "http://%s/", gIpText);
  buildWifiQrText();
}

static void applyLed() {
#if STATUS_LED_ACTIVE_HIGH
  digitalWrite(STATUS_LED_PIN, cfg.ledOn ? HIGH : LOW);
#else
  digitalWrite(STATUS_LED_PIN, cfg.ledOn ? LOW : HIGH);
#endif
}

static void resetConfig() {
  memset(&cfg, 0, sizeof(cfg));
  strncpy(cfg.deviceName, LAB_DEFAULT_NAME, sizeof(cfg.deviceName) - 1);
  strncpy(cfg.preference, "both", sizeof(cfg.preference) - 1);
  cfg.ledOn = false;
}

// Drop a trailing partial UTF-8 sequence so a truncated note still parses as JSON.
static void trimIncompleteUtf8(char* text) {
  const size_t len = strlen(text);
  size_t i = 0;
  size_t lastGood = 0;
  while (i < len) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    size_t need = 1;
    if (c < 0x80) {
      need = 1;
    } else if ((c & 0xE0) == 0xC0 && c >= 0xC2) {
      need = 2;
    } else if ((c & 0xF0) == 0xE0) {
      need = 3;
    } else if ((c & 0xF8) == 0xF0 && c <= 0xF4) {
      need = 4;
    } else {
      break;
    }
    if (i + need > len) {
      break;
    }
    bool ok = true;
    for (size_t j = 1; j < need; ++j) {
      if ((static_cast<unsigned char>(text[i + j]) & 0xC0) != 0x80) {
        ok = false;
        break;
      }
    }
    if (!ok) {
      break;
    }
    i += need;
    lastGood = i;
  }
  text[lastGood] = '\0';
}

static void copySanitized(char* dest, size_t destSize, const String& src) {
  if (destSize == 0) {
    return;
  }
  size_t out = 0;
  bool started = false;
  for (unsigned int i = 0; i < src.length() && out + 1 < destSize; ++i) {
    const unsigned char c = static_cast<unsigned char>(src[i]);
    if (c < 0x20 || c == 0x7F) {
      continue;
    }
    if (!started && c == ' ') {
      continue;
    }
    dest[out++] = static_cast<char>(c);
    started = true;
  }
  while (out > 0 && dest[out - 1] == ' ') {
    --out;
  }
  dest[out] = '\0';
  trimIncompleteUtf8(dest);
  const size_t trimmed = strlen(dest);
  size_t end = trimmed;
  while (end > 0 && dest[end - 1] == ' ') {
    --end;
  }
  dest[end] = '\0';
}

static void storePreference(const String& raw) {
  copySanitized(cfg.preference, sizeof(cfg.preference), raw);
  if (strcmp(cfg.preference, "status") != 0 && strcmp(cfg.preference, "scan") != 0 &&
      strcmp(cfg.preference, "both") != 0) {
    strncpy(cfg.preference, "both", sizeof(cfg.preference) - 1);
    cfg.preference[sizeof(cfg.preference) - 1] = '\0';
  }
}

// This board is only an access point, so it has no wall clock.
// The stamp is hours:minutes:seconds.milliseconds since boot.
static void formatBootTime(char* out, size_t cap) {
  const uint32_t ms = millis();
  const uint32_t totalSec = ms / 1000UL;
  snprintf(out, cap, "%lu:%02lu:%02lu.%03lu",
           static_cast<unsigned long>(totalSec / 3600UL),
           static_cast<unsigned long>((totalSec % 3600UL) / 60UL),
           static_cast<unsigned long>(totalSec % 60UL),
           static_cast<unsigned long>(ms % 1000UL));
}

static void formatMac(const uint8_t mac[6], char* out, size_t cap) {
  snprintf(out, cap, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void formatIpv4(uint32_t addr, char* out, size_t cap) {
  const IPAddress ip(addr);
  snprintf(out, cap, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

static bool sameClientIp(const JoinedClient& client, const IPAddress& ip) {
  return client.ip[0] == ip[0] && client.ip[1] == ip[1] && client.ip[2] == ip[2] && client.ip[3] == ip[3];
}

static bool macIsBlank(const uint8_t mac[6]) {
  for (int i = 0; i < 6; ++i) {
    if (mac[i] != 0) {
      return false;
    }
  }
  return true;
}

static void clearCaptured(JoinedClient& client) {
  client.path[0] = '\0';
  client.userAgent[0] = '\0';
  client.acceptLanguage[0] = '\0';
  client.host[0] = '\0';
  client.dnsCount = 0;
  client.dnsNext = 0;
  for (int i = 0; i < 4; ++i) {
    client.dns[i][0] = '\0';
  }
}

static void copyLimited(char* dest, size_t cap, const char* src) {
  size_t n = 0;
  if (cap == 0) {
    return;
  }
  if (src != nullptr) {
    for (; src[n] != '\0' && n + 1 < cap; ++n) {
      const unsigned char c = static_cast<unsigned char>(src[n]);
      dest[n] = (c < 0x20 || c == 0x7F) ? ' ' : static_cast<char>(c);
    }
  }
  dest[n] = '\0';
}

static int findClientByMac(const uint8_t mac[6]) {
  if (macIsBlank(mac)) {
    return -1;
  }
  for (int i = 0; i < LAB_AP_MAX_CLIENTS; ++i) {
    if (joinedClients[i].active && memcmp(joinedClients[i].mac, mac, 6) == 0) {
      return i;
    }
  }
  return -1;
}

static int findClientByIp(const IPAddress& ip) {
  for (int i = 0; i < LAB_AP_MAX_CLIENTS; ++i) {
    if (joinedClients[i].active && sameClientIp(joinedClients[i], ip)) {
      return i;
    }
  }
  return -1;
}

static int takeClientSlot(const uint8_t* mac, const IPAddress& ip) {
  if (mac != nullptr) {
    const int byMac = findClientByMac(mac);
    if (byMac >= 0) {
      for (int b = 0; b < 4; ++b) {
        joinedClients[byMac].ip[b] = ip[b];
      }
      return byMac;
    }
  }
  const int byIp = findClientByIp(ip);
  if (byIp >= 0) {
    if (mac != nullptr && !macIsBlank(mac)) {
      memcpy(joinedClients[byIp].mac, mac, 6);
    }
    return byIp;
  }
  int slot = -1;
  for (int i = 0; i < LAB_AP_MAX_CLIENTS; ++i) {
    if (!joinedClients[i].active) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    slot = 0;
  }
  joinedClients[slot].active = true;
  if (mac != nullptr) {
    memcpy(joinedClients[slot].mac, mac, 6);
  } else {
    memset(joinedClients[slot].mac, 0, 6);
  }
  for (int b = 0; b < 4; ++b) {
    joinedClients[slot].ip[b] = ip[b];
  }
  clearCaptured(joinedClients[slot]);
  return slot;
}

static void rememberJoinedClient(const uint8_t mac[6], const IPAddress& ip) {
  takeClientSlot(mac, ip);
}

static void noteClientDns(const IPAddress& ip, const char* host) {
  if (host == nullptr || host[0] == '\0') {
    return;
  }
  const int index = takeClientSlot(nullptr, ip);
  JoinedClient& client = joinedClients[index];
  if (client.dnsCount > 0) {
    const int prev = (client.dnsNext == 0 ? 4 : client.dnsNext) - 1;
    if (strcmp(client.dns[prev], host) == 0) {
      return;
    }
  }
  copyLimited(client.dns[client.dnsNext], sizeof(client.dns[client.dnsNext]), host);
  client.dnsNext = static_cast<uint8_t>((client.dnsNext + 1) % 4);
  if (client.dnsCount < 4) {
    ++client.dnsCount;
  }
}

static void captureClientHttp() {
  const IPAddress ip = webServer.client().remoteIP();
  if (ip[0] == 0) {
    return;
  }
  const int index = takeClientSlot(nullptr, ip);
  JoinedClient& client = joinedClients[index];
  const String uri = webServer.uri();
  const bool poll = uri.startsWith("/api/status") || uri.startsWith("/api/clients") ||
                    uri.startsWith("/api/log") || uri.startsWith("/api/headers") ||
                    uri.startsWith("/api/scan");
  if (!poll) {
    copyLimited(client.path, sizeof(client.path), uri.c_str());
  }
  const String agent = webServer.header("User-Agent");
  if (agent.length() > 0) {
    copyLimited(client.userAgent, sizeof(client.userAgent), agent.c_str());
  }
  const String language = webServer.header("Accept-Language");
  if (language.length() > 0) {
    copyLimited(client.acceptLanguage, sizeof(client.acceptLanguage), language.c_str());
  }
  const String host = webServer.hostHeader();
  if (host.length() > 0) {
    copyLimited(client.host, sizeof(client.host), host.c_str());
  }
}

static void forgetJoinedClient(const uint8_t mac[6]) {
  for (int i = 0; i < LAB_AP_MAX_CLIENTS; ++i) {
    if (joinedClients[i].active && memcmp(joinedClients[i].mac, mac, 6) == 0) {
      joinedClients[i].active = false;
    }
  }
}

static bool macForClientIp(const IPAddress& ip, uint8_t mac[6]) {
  for (int i = 0; i < LAB_AP_MAX_CLIENTS; ++i) {
    if (!joinedClients[i].active) {
      continue;
    }
    bool match = true;
    for (int b = 0; b < 4; ++b) {
      if (joinedClients[i].ip[b] != ip[b]) {
        match = false;
        break;
      }
    }
    if (match) {
      memcpy(mac, joinedClients[i].mac, 6);
      return true;
    }
  }
  return false;
}

static void pushLog(const char* text) {
  SessionLine& line = sessionLog[sessionLogNext];
  sessionLogNext = static_cast<uint8_t>((sessionLogNext + 1) % kLogLines);
  if (sessionLogCount < kLogLines) {
    ++sessionLogCount;
  }
  line.id = ++sessionLogSeq;
  size_t n = 0;
  if (text != nullptr) {
    for (; text[n] != '\0' && n + 1 < static_cast<size_t>(kLogChars); ++n) {
      const unsigned char c = static_cast<unsigned char>(text[n]);
      line.text[n] = (c < 0x20 || c == 0x7F) ? ' ' : static_cast<char>(c);
    }
  }
  line.text[n] = '\0';
}

static void onApEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  char stamp[24];
  formatBootTime(stamp, sizeof(stamp));

  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED: {
      char mac[18];
      formatMac(info.wifi_ap_staconnected.mac, mac, sizeof(mac));
      Serial.printf("[ap] client associated  time=%s  mac=%s\n", stamp, mac);
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED: {
      // DHCP hands out the local address after association. This is the
      // first moment the client's IP is known, so it is logged immediately.
      char ip[16];
      formatIpv4(info.wifi_ap_staipassigned.ip.addr, ip, sizeof(ip));
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
      rememberJoinedClient(info.wifi_ap_staipassigned.mac, IPAddress(info.wifi_ap_staipassigned.ip.addr));
      char mac[18];
      formatMac(info.wifi_ap_staipassigned.mac, mac, sizeof(mac));
      Serial.printf("[ap] client connected  time=%s  ip=%s  mac=%s\n", stamp, ip, mac);
      char note[kLogChars];
      snprintf(note, sizeof(note), "%s joined %s %s", stamp, ip, mac);
      pushLog(note);
#else
      Serial.printf("[ap] client connected  time=%s  ip=%s\n", stamp, ip);
      char note[kLogChars];
      snprintf(note, sizeof(note), "%s joined %s", stamp, ip);
      pushLog(note);
#endif
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: {
      forgetJoinedClient(info.wifi_ap_stadisconnected.mac);
      char mac[18];
      formatMac(info.wifi_ap_stadisconnected.mac, mac, sizeof(mac));
      Serial.printf("[ap] client left  time=%s  mac=%s\n", stamp, mac);
      char note[kLogChars];
      snprintf(note, sizeof(note), "%s left %s", stamp, mac);
      pushLog(note);
      break;
    }
    default:
      break;
  }
}

static bool startAccessPoint() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);

#if LAB_CLEAR_STORED_STA
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);
  Serial.println("[ap] cleared saved station logins on this ESP32");
#endif

  // The ESP32 scan API only runs while the station interface is on.
  // The station side is left disconnected; this firmware never joins a router.
  WiFi.mode(WIFI_AP_STA);
  delay(100);
  WiFi.setAutoReconnect(false);

  const IPAddress subnet = kSubnet;
  for (int attempt = 1; attempt <= 3; ++attempt) {
    // On core 3.x the 5th address is the DNS server offered by DHCP.
    // Clients must be told to use this board, or the portal never sees their queries.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    const bool configured = WiFi.softAPConfig(kApIp, kApIp, subnet, IPAddress(static_cast<uint32_t>(0)), kApIp);
#else
    const bool configured = WiFi.softAPConfig(kApIp, kApIp, subnet);
#endif
    if (!configured) {
      Serial.printf("[ap] address configuration failed on attempt %d\n", attempt);
    }

    const bool started = WiFi.softAP(
        LAB_AP_SSID,
        LAB_AP_PASSWORD,
        LAB_AP_CHANNEL,
        0,
        LAB_AP_MAX_CLIENTS);
    WiFi.setSleep(WIFI_PS_NONE);
    WiFi.softAPsetHostname(LAB_HOSTNAME);

    const IPAddress ip = WiFi.softAPIP();
    if (started && ip[0] != 0) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 2)
      if (WiFi.AP.enableDhcpCaptivePortal()) {
        Serial.println("[ap] DHCP captive-portal option enabled");
      } else {
        Serial.println("[ap] DHCP captive-portal option was not set");
      }
#endif
      Serial.printf("[ap] up on attempt %d\n", attempt);
      return true;
    }

    Serial.printf("[ap] attempt %d failed\n", attempt);
    WiFi.softAPdisconnect(true);
    delay(300);
  }
  return false;
}

static void sortByRssi() {
  for (int i = 1; i < scanCount; ++i) {
    const ScanEntry key = scanList[i];
    int j = i - 1;
    while (j >= 0 && scanList[j].rssi < key.rssi) {
      scanList[j + 1] = scanList[j];
      --j;
    }
    scanList[j + 1] = key;
  }
}

static void copyScanResults(int found) {
  int keep = found;
  if (keep > SCAN_MAX_RESULTS) {
    keep = SCAN_MAX_RESULTS;
  }
  if (keep < 0) {
    keep = 0;
  }

  for (int i = 0; i < keep; ++i) {
    const String raw = WiFi.SSID(i);
    if (raw.length() == 0) {
      strncpy(scanList[i].ssid, "(hidden)", sizeof(scanList[i].ssid) - 1);
      scanList[i].ssid[sizeof(scanList[i].ssid) - 1] = '\0';
    } else {
      raw.toCharArray(scanList[i].ssid, sizeof(scanList[i].ssid));
    }

    const int rssi = WiFi.RSSI(i);
    if (rssi < -120) {
      scanList[i].rssi = -120;
    } else if (rssi > 0) {
      scanList[i].rssi = 0;
    } else {
      scanList[i].rssi = static_cast<int8_t>(rssi);
    }

    const int channel = WiFi.channel(i);
    scanList[i].channel = channel < 0 ? 0 : static_cast<uint8_t>(channel);
    scanList[i].enc = static_cast<uint8_t>(WiFi.encryptionType(i));
  }

  scanCount = keep;
  sortByRssi();
  WiFi.scanDelete();
  scanState = SCAN_READY;
  scanFinishedAt = millis();
  scanError = "";
  Serial.printf("[scan] %d network(s), free heap %u\n",
                scanCount,
                static_cast<unsigned>(ESP.getFreeHeap()));
}

static void pollScan() {
  if (scanState != SCAN_RUNNING) {
    return;
  }

  if (millis() - scanStartedAt > SCAN_TIMEOUT_MS) {
    WiFi.scanDelete();
    scanState = SCAN_FAILED;
    scanError = "Scan timed out";
    Serial.println("[scan] timeout");
    return;
  }

  const int found = WiFi.scanComplete();
  if (found == WIFI_SCAN_RUNNING) {
    return;
  }
  if (found < 0) {
    WiFi.scanDelete();
    scanState = SCAN_FAILED;
    scanError = "Scan failed";
    Serial.printf("[scan] failed (%d)\n", found);
    return;
  }

  copyScanResults(found);
}

static bool startScan() {
  if (scanState == SCAN_RUNNING) {
    return true;
  }
  if (ESP.getFreeHeap() < LAB_MIN_HEAP_FOR_SCAN) {
    scanState = SCAN_FAILED;
    scanError = "Low memory";
    Serial.printf("[scan] refused, free heap %u\n", static_cast<unsigned>(ESP.getFreeHeap()));
    return false;
  }

  WiFi.scanDelete();
  const int rc = WiFi.scanNetworks(true, true);
  if (rc != WIFI_SCAN_RUNNING) {
    scanState = SCAN_FAILED;
    scanError = "Could not start scan";
    Serial.printf("[scan] start failed (%d)\n", rc);
    return false;
  }

  scanStartedAt = millis();
  scanState = SCAN_RUNNING;
  scanCount = 0;
  scanError = "";
  Serial.println("[scan] started");
  return true;
}

static void buildScanJson(JsonWriter& writer) {
  jwInit(writer, gJson, sizeof(gJson));
  jwFmt(writer, "{\"state\":\"%s\",\"count\":%d,\"age\":%lu,\"error\":\"",
        scanStateName(), scanCount, static_cast<unsigned long>(scanAgeSeconds()));
  jwEsc(writer, scanError);
  jwAdd(writer, "\",\"networks\":[");
  for (int i = 0; i < scanCount; ++i) {
    if (i > 0) {
      jwAdd(writer, ",");
    }
    jwAdd(writer, "{\"ssid\":\"");
    jwEsc(writer, scanList[i].ssid);
    jwFmt(writer, "\",\"rssi\":%d,\"channel\":%u,\"enc\":\"",
          static_cast<int>(scanList[i].rssi),
          static_cast<unsigned>(scanList[i].channel));
    jwEsc(writer, encName(scanList[i].enc));
    jwAdd(writer, "\"}");
  }
  jwAdd(writer, "]}");
}

static void sendScanJson() {
  JsonWriter writer;
  buildScanJson(writer);
  sendWriter(200, writer,
             "{\"state\":\"failed\",\"count\":0,\"age\":0,\"error\":\"response too large\",\"networks\":[]}");
}

static void buildConfigJson(JsonWriter& writer) {
  jwInit(writer, gJson, sizeof(gJson));
  jwAdd(writer, "{\"deviceName\":\"");
  jwEsc(writer, cfg.deviceName);
  jwAdd(writer, "\",\"labNote\":\"");
  jwEsc(writer, cfg.labNote);
  jwAdd(writer, "\",\"profileName\":\"");
  jwEsc(writer, cfg.profileName);
  jwAdd(writer, "\",\"preference\":\"");
  jwEsc(writer, cfg.preference);
  jwFmt(writer, "\",\"led\":%d}", cfg.ledOn ? 1 : 0);
}

static void sendConfigJson() {
  JsonWriter writer;
  buildConfigJson(writer);
  sendWriter(200, writer, "{\"deviceName\":\"\",\"labNote\":\"\",\"profileName\":\"\",\"preference\":\"both\",\"led\":0}");
}

static void sendStatusJson() {
  JsonWriter writer;
  jwInit(writer, gJson, sizeof(gJson));
  jwFmt(writer,
        "{\"heap\":%u,\"minHeap\":%u,\"uptime\":%lu,\"apChannel\":%u,\"led\":%d,\"scan\":\"%s\",\"ip\":\"%s\",\"mac\":\"%s\",\"ssid\":\"",
        static_cast<unsigned>(ESP.getFreeHeap()),
        static_cast<unsigned>(ESP.getMinFreeHeap()),
        static_cast<unsigned long>(millis() / 1000UL),
        static_cast<unsigned>(LAB_AP_CHANNEL),
        cfg.ledOn ? 1 : 0,
        scanStateName(),
        gIpText,
        gMac);
  jwEsc(writer, LAB_AP_SSID);
  jwAdd(writer, "\",\"deviceName\":\"");
  jwEsc(writer, cfg.deviceName);
  jwAdd(writer, "\",\"labNote\":\"");
  jwEsc(writer, cfg.labNote);
  jwAdd(writer, "\",\"qr\":\"");
  jwEsc(writer, gWifiQr);
  jwAdd(writer, "\"}");
  sendWriter(200, writer, "{\"error\":\"status unavailable\"}");
}

static void logPortalGet() {
  char stamp[24];
  char ipText[16];
  char macText[18];
  formatBootTime(stamp, sizeof(stamp));
  const IPAddress ip = webServer.client().remoteIP();
  snprintf(ipText, sizeof(ipText), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  uint8_t mac[6] = {0};
  if (macForClientIp(ip, mac)) {
    formatMac(mac, macText, sizeof(macText));
  } else {
    snprintf(macText, sizeof(macText), "unknown");
  }
  const String userAgent = webServer.header("User-Agent");
  // Printed before the page is sent. The User-Agent exists only on this request.
  Serial.printf("[portal] get  time=%s  ip=%s  mac=%s\n", stamp, ipText, macText);
  Serial.printf("[portal] user-agent=%s\n", userAgent.length() > 0 ? userAgent.c_str() : "(none)");
  char note[kLogChars];
  snprintf(note, sizeof(note), "%s GET / %s %s", stamp, ipText, macText);
  pushLog(note);
}

static void handleRoot() {
  if (webServer.method() != HTTP_GET) {
    webServer.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  // The portal HTML is this GET. Print before the response is sent.
  logPortalGet();
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

static void handleFavicon() {
  webServer.send(204, "text/plain", "");
}

static void handleCaptive() {
  webServer.sendHeader("Location", gPortalLocation, true);
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(302, "text/plain", "Open the lab portal");
}

static void handleStatus() {
  sendStatusJson();
}

static void handleScanGet() {
  sendScanJson();
}

static void handleScanPost() {
  char ipText[16];
  char stamp[24];
  const IPAddress ip = webServer.client().remoteIP();
  snprintf(ipText, sizeof(ipText), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  formatBootTime(stamp, sizeof(stamp));
  Serial.printf("[scan] request  time=%s  ip=%s\n", stamp, ipText);
  char note[kLogChars];
  snprintf(note, sizeof(note), "%s POST /api/scan %s", stamp, ipText);
  pushLog(note);
  startScan();
  sendScanJson();
}

static void handleConfigGet() {
  sendConfigJson();
}

static void logFormSubmission() {
  char stamp[24];
  char ipText[16];
  formatBootTime(stamp, sizeof(stamp));
  const IPAddress ip = webServer.client().remoteIP();
  snprintf(ipText, sizeof(ipText), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  Serial.printf("[form] submit  time=%s  ip=%s  fields=%d\n", stamp, ipText, webServer.args());
  for (int i = 0; i < webServer.args(); ++i) {
    Serial.printf("[form]   %s=%s\n", webServer.argName(i).c_str(), webServer.arg(i).c_str());
  }
}

static void handleConfigPost() {
  // Print the post as soon as it arrives, before the values are stored.
  logFormSubmission();
  char note[kLogChars];
  char stamp[24];
  char ipText[16];
  const IPAddress ip = webServer.client().remoteIP();
  snprintf(ipText, sizeof(ipText), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  formatBootTime(stamp, sizeof(stamp));
  snprintf(note, sizeof(note), "%s POST /api/config %s fields=%d", stamp, ipText, webServer.args());
  pushLog(note);

  if (webServer.hasArg("clear") && webServer.arg("clear") == "1") {
    resetConfig();
  } else {
    if (webServer.hasArg("deviceName")) {
      copySanitized(cfg.deviceName, sizeof(cfg.deviceName), webServer.arg("deviceName"));
      if (cfg.deviceName[0] == '\0') {
        strncpy(cfg.deviceName, LAB_DEFAULT_NAME, sizeof(cfg.deviceName) - 1);
        cfg.deviceName[sizeof(cfg.deviceName) - 1] = '\0';
      }
    }
    if (webServer.hasArg("labNote")) {
      copySanitized(cfg.labNote, sizeof(cfg.labNote), webServer.arg("labNote"));
    }
    if (webServer.hasArg("profileName")) {
      copySanitized(cfg.profileName, sizeof(cfg.profileName), webServer.arg("profileName"));
    }
    if (webServer.hasArg("preference")) {
      storePreference(webServer.arg("preference"));
    }
    // An unchecked box is omitted from a normal form post, so absence means off.
    cfg.ledOn = webServer.hasArg("led") && webServer.arg("led") == "1";
  }

  applyLed();
  Serial.printf("[config] stored label=\"%s\" note=\"%s\" led=%d profile=\"%s\" preference=\"%s\" free=%u\n",
                cfg.deviceName,
                cfg.labNote,
                cfg.ledOn ? 1 : 0,
                cfg.profileName,
                cfg.preference,
                static_cast<unsigned>(ESP.getFreeHeap()));
  sendConfigJson();
}

static void sendClientsJson() {
  JsonWriter writer;
  jwInit(writer, gJson, sizeof(gJson));
  int count = 0;
  for (int i = 0; i < LAB_AP_MAX_CLIENTS; ++i) {
    if (joinedClients[i].active) {
      ++count;
    }
  }
  jwFmt(writer, "{\"count\":%d,\"clients\":[", count);
  bool first = true;
  for (int i = 0; i < LAB_AP_MAX_CLIENTS; ++i) {
    if (!joinedClients[i].active) {
      continue;
    }
    if (!first) {
      jwAdd(writer, ",");
    }
    first = false;
    char ipText[16];
    char macText[18];
    snprintf(ipText, sizeof(ipText), "%u.%u.%u.%u",
             joinedClients[i].ip[0], joinedClients[i].ip[1],
             joinedClients[i].ip[2], joinedClients[i].ip[3]);
    formatMac(joinedClients[i].mac, macText, sizeof(macText));
    jwFmt(writer, "{\"ip\":\"%s\",\"mac\":\"%s\",\"path\":\"", ipText, macIsBlank(joinedClients[i].mac) ? "unknown" : macText);
    jwEsc(writer, joinedClients[i].path);
    jwAdd(writer, "\",\"userAgent\":\"");
    jwEsc(writer, joinedClients[i].userAgent);
    jwAdd(writer, "\",\"acceptLanguage\":\"");
    jwEsc(writer, joinedClients[i].acceptLanguage);
    jwAdd(writer, "\",\"host\":\"");
    jwEsc(writer, joinedClients[i].host);
    jwAdd(writer, "\",\"dns\":[");
    const int dnsStart = (joinedClients[i].dnsCount < 4) ? 0 : joinedClients[i].dnsNext;
    for (int d = 0; d < joinedClients[i].dnsCount; ++d) {
      if (d > 0) {
        jwAdd(writer, ",");
      }
      jwAdd(writer, "\"");
      jwEsc(writer, joinedClients[i].dns[(dnsStart + d) % 4]);
      jwAdd(writer, "\"");
    }
    jwAdd(writer, "]}");
  }
  jwAdd(writer, "]}");
  sendWriter(200, writer, "{\"count\":0,\"clients\":[]}");
}

static bool requireAdmin();

static void handleClients() {
  if (!requireAdmin()) {
    return;
  }
  sendClientsJson();
}

static void appendHeaderField(JsonWriter& writer, bool& first, const char* name, const String& value) {
  if (!first) {
    jwAdd(writer, ",");
  }
  first = false;
  jwAdd(writer, "{\"name\":\"");
  jwEsc(writer, name);
  jwAdd(writer, "\",\"value\":\"");
  jwEsc(writer, value.c_str());
  jwAdd(writer, "\"}");
}

static void sendHeadersJson() {
  JsonWriter writer;
  jwInit(writer, gJson, sizeof(gJson));
  const IPAddress ip = webServer.client().remoteIP();
  char ipText[16];
  snprintf(ipText, sizeof(ipText), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  jwFmt(writer, "{\"ip\":\"%s\",\"headers\":[", ipText);
  bool first = true;
  appendHeaderField(writer, first, "Host", webServer.hostHeader());
  appendHeaderField(writer, first, "User-Agent", webServer.header("User-Agent"));
  appendHeaderField(writer, first, "Accept", webServer.header("Accept"));
  appendHeaderField(writer, first, "Accept-Language", webServer.header("Accept-Language"));
  appendHeaderField(writer, first, "Accept-Encoding", webServer.header("Accept-Encoding"));
  jwAdd(writer, "]}");
  sendWriter(200, writer, "{\"ip\":\"\",\"headers\":[]}");
}

static void handleHeaders() {
  if (!requireAdmin()) {
    return;
  }
  sendHeadersJson();
}

static bool skipDnsName(const uint8_t* buf, int len, size_t* pos) {
  size_t p = *pos;
  int labels = 0;
  while (p < static_cast<size_t>(len) && labels < 64) {
    const uint8_t lab = buf[p];
    if (lab == 0) {
      *pos = p + 1;
      return true;
    }
    if ((lab & 0xC0) == 0xC0) {
      if (p + 1 >= static_cast<size_t>(len)) {
        return false;
      }
      *pos = p + 2;
      return true;
    }
    if ((lab & 0xC0) != 0 || p + 1U + lab >= static_cast<size_t>(len)) {
      return false;
    }
    p += 1U + lab;
    ++labels;
  }
  return false;
}

static bool copyDnsName(const uint8_t* buf, int len, char* out, size_t outCap) {
  if (outCap == 0 || len < 13) {
    return false;
  }
  size_t pos = 12;
  size_t outLen = 0;
  out[0] = '\0';
  int labels = 0;
  while (pos < static_cast<size_t>(len) && labels < 64) {
    const uint8_t lab = buf[pos];
    if (lab == 0) {
      return outLen > 0;
    }
    if ((lab & 0xC0) != 0 || pos + 1U + lab >= static_cast<size_t>(len)) {
      return false;
    }
    if (outLen > 0 && outLen + 1 < outCap) {
      out[outLen++] = '.';
    }
    for (uint8_t i = 0; i < lab && outLen + 1 < outCap; ++i) {
      unsigned char c = buf[pos + 1U + i];
      if (c < 0x20 || c > 0x7E) {
        c = '?';
      }
      out[outLen++] = static_cast<char>(c);
    }
    out[outLen] = '\0';
    pos += 1U + lab;
    ++labels;
  }
  return false;
}

static void replyDns(const uint8_t* req, size_t qEnd, bool withA, const IPAddress& to, uint16_t port) {
  uint8_t resp[512];
  const size_t extra = withA ? 16 : 0;
  if (qEnd < 12 || qEnd + extra > sizeof(resp)) {
    return;
  }
  memcpy(resp, req, qEnd);
  resp[2] = 0x85;
  resp[3] = 0x00;
  resp[6] = 0;
  resp[7] = withA ? 1 : 0;
  resp[8] = 0;
  resp[9] = 0;
  resp[10] = 0;
  resp[11] = 0;
  size_t n = qEnd;
  if (withA) {
    resp[n++] = 0xC0;
    resp[n++] = 0x0C;
    resp[n++] = 0x00;
    resp[n++] = 0x01;
    resp[n++] = 0x00;
    resp[n++] = 0x01;
    resp[n++] = 0x00;
    resp[n++] = 0x00;
    resp[n++] = 0x00;
    resp[n++] = 30;
    resp[n++] = 0x00;
    resp[n++] = 0x04;
    resp[n++] = kApIp[0];
    resp[n++] = kApIp[1];
    resp[n++] = kApIp[2];
    resp[n++] = kApIp[3];
  }
  dnsUdp.beginPacket(to, port);
  dnsUdp.write(resp, n);
  dnsUdp.endPacket();
}

static void pollDns() {
  for (int i = 0; i < 4; ++i) {
    const int packetSize = dnsUdp.parsePacket();
    if (packetSize <= 0) {
      return;
    }
    const IPAddress from = dnsUdp.remoteIP();
    const uint16_t port = dnsUdp.remotePort();
    uint8_t buf[512];
    if (packetSize > static_cast<int>(sizeof(buf))) {
      while (dnsUdp.available() > 0) {
        dnsUdp.read();
      }
      continue;
    }
    const int n = dnsUdp.read(buf, sizeof(buf));
    if (n < 12 || (buf[2] & 0x80) != 0) {
      continue;
    }

    char host[sizeof(dnsLastHost)];
    host[0] = '\0';
    const bool named = copyDnsName(buf, n, host, sizeof(host));
    ++dnsQueryCount;
    if (named) {
      strncpy(dnsLastHost, host, sizeof(dnsLastHost) - 1);
      dnsLastHost[sizeof(dnsLastHost) - 1] = '\0';
    } else if (n > 12 && buf[12] == 0) {
      strncpy(dnsLastHost, "(root)", sizeof(dnsLastHost) - 1);
      dnsLastHost[sizeof(dnsLastHost) - 1] = '\0';
    } else {
      strncpy(dnsLastHost, "(unparsed)", sizeof(dnsLastHost) - 1);
      dnsLastHost[sizeof(dnsLastHost) - 1] = '\0';
    }
    Serial.printf("[dns] #%lu from %u.%u.%u.%u %s\n",
                  static_cast<unsigned long>(dnsQueryCount),
                  from[0], from[1], from[2], from[3],
                  dnsLastHost);
    noteClientDns(from, dnsLastHost);
    if (strcmp(dnsLoggedHost, dnsLastHost) != 0) {
      strncpy(dnsLoggedHost, dnsLastHost, sizeof(dnsLoggedHost) - 1);
      dnsLoggedHost[sizeof(dnsLoggedHost) - 1] = '\0';
      char stamp[24];
      char note[kLogChars];
      formatBootTime(stamp, sizeof(stamp));
      snprintf(note, sizeof(note), "%s DNS %u.%u.%u.%u %s",
               stamp, from[0], from[1], from[2], from[3], dnsLastHost);
      pushLog(note);
    }

    const uint16_t questions = (static_cast<uint16_t>(buf[4]) << 8) | buf[5];
    if (questions != 1) {
      uint8_t hdr[12];
      memcpy(hdr, buf, 12);
      hdr[2] = 0x85;
      hdr[3] = 0x00;
      memset(hdr + 4, 0, 8);
      dnsUdp.beginPacket(from, port);
      dnsUdp.write(hdr, sizeof(hdr));
      dnsUdp.endPacket();
      continue;
    }

    size_t qEnd = 12;
    const bool question = skipDnsName(buf, n, &qEnd) && qEnd + 4 <= static_cast<size_t>(n);
    if (!question) {
      continue;
    }
    const uint16_t qtype = (static_cast<uint16_t>(buf[qEnd]) << 8) | buf[qEnd + 1];
    qEnd += 4;
    const bool wantA = qtype == 1 || qtype == 255;
    replyDns(buf, qEnd, wantA, from, port);
  }
}

static bool requireAdmin() {
  if (webServer.authenticate(LAB_ADMIN_USER, LAB_ADMIN_PASSWORD)) {
    return true;
  }
  webServer.requestAuthentication(BASIC_AUTH, "ESP32 Lab Admin", "Admin login required");
  return false;
}

static void handleAdmin() {
  if (!requireAdmin()) {
    return;
  }
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send_P(200, "text/html; charset=utf-8", ADMIN_HTML);
}

static void fillSessionLog(JsonWriter& writer) {
  jwInit(writer, gJson, sizeof(gJson));
  jwFmt(writer, "{\"count\":%u,\"lines\":[", static_cast<unsigned>(sessionLogCount));
  const int start = (sessionLogCount < kLogLines) ? 0 : sessionLogNext;
  for (int i = 0; i < sessionLogCount; ++i) {
    const SessionLine& line = sessionLog[(start + i) % kLogLines];
    if (i > 0) {
      jwAdd(writer, ",");
    }
    jwFmt(writer, "{\"id\":%lu,\"text\":\"", static_cast<unsigned long>(line.id));
    jwEsc(writer, line.text);
    jwAdd(writer, "\"}");
  }
  jwAdd(writer, "]}");
}

static void handleLog() {
  if (!requireAdmin()) {
    return;
  }
  const bool download = webServer.hasArg("dl");
  const bool asText = download && webServer.arg("dl") == "txt";
  if (asText) {
    size_t n = 0;
    gJson[0] = '\0';
    const int start = (sessionLogCount < kLogLines) ? 0 : sessionLogNext;
    for (int i = 0; i < sessionLogCount; ++i) {
      const SessionLine& line = sessionLog[(start + i) % kLogLines];
      const int wrote = snprintf(gJson + n, sizeof(gJson) - n, "%s\n", line.text);
      if (wrote < 0 || static_cast<size_t>(wrote) >= sizeof(gJson) - n) {
        break;
      }
      n += static_cast<size_t>(wrote);
    }
    webServer.sendHeader("Content-Disposition", "attachment; filename=\"esp32-session-log.txt\"");
    webServer.send(200, "text/plain; charset=utf-8", gJson);
    return;
  }
  JsonWriter writer;
  fillSessionLog(writer);
  if (download) {
    webServer.sendHeader("Content-Disposition", "attachment; filename=\"esp32-session-log.json\"");
  }
  sendWriter(200, writer, "{\"count\":0,\"lines\":[]}");
}

static void registerRoutes() {
  static const char* kProbePaths[] = {
      "/generate_204",
      "/gen_204",
      "/hotspot-detect.html",
      "/canonical.html",
      "/success.txt",
      "/ncsi.txt",
      "/connecttest.txt",
      "/redirect",
      "/fwlink",
      "/library/test/success.html",
      "/kindle-wifi/wifiredirect.html",
  };

  static const char* kRequestHeaders[] = {
      "User-Agent",
      "Accept",
      "Accept-Language",
      "Accept-Encoding",
  };
  webServer.collectHeaders(kRequestHeaders, sizeof(kRequestHeaders) / sizeof(kRequestHeaders[0]));

  webServer.addMiddleware([](WebServer& server, Middleware::Callback next) -> bool {
    (void)server;
    captureClientHttp();
    return next();
  });

  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/admin", HTTP_GET, handleAdmin);
  webServer.on("/favicon.ico", HTTP_GET, handleFavicon);
  webServer.on("/api/status", HTTP_GET, handleStatus);
  webServer.on("/api/clients", HTTP_GET, handleClients);
  webServer.on("/api/headers", HTTP_GET, handleHeaders);
  webServer.on("/api/log", HTTP_GET, handleLog);
  webServer.on("/api/scan", HTTP_GET, handleScanGet);
  webServer.on("/api/scan", HTTP_POST, handleScanPost);
  webServer.on("/api/config", HTTP_GET, handleConfigGet);
  webServer.on("/api/config", HTTP_POST, handleConfigPost);

  const size_t probeCount = sizeof(kProbePaths) / sizeof(kProbePaths[0]);
  for (size_t i = 0; i < probeCount; ++i) {
    webServer.on(kProbePaths[i], HTTP_ANY, handleCaptive);
  }
  webServer.onNotFound(handleCaptive);
}

static void blinkError() {
  static uint32_t lastToggle = 0;
  static bool on = false;
  const uint32_t now = millis();
  if (now - lastToggle < 180) {
    return;
  }
  lastToggle = now;
  on = !on;
  digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
  delay(10);
}

void labSetup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("ESP32 Educational Wi-Fi Lab");

  pinMode(STATUS_LED_PIN, OUTPUT);
  resetConfig();
  applyLed();

  WiFi.onEvent(onApEvent);
  apReady = startAccessPoint();
  if (!apReady) {
    Serial.println("[ap] could not start. Check the password length in config.h.");
    return;
  }

  cacheIdentity();
  registerRoutes();
  webServer.begin();

  if (!dnsUdp.begin(LAB_DNS_PORT)) {
    Serial.println("[dns] failed to listen on port 53");
  } else {
    Serial.println("[dns] queries from clients on this AP are counted and answered with the lab address");
  }

  Serial.println("----------------------------------------");
  Serial.printf("SSID:       %s\n", LAB_AP_SSID);
  Serial.printf("Password:   %s\n", LAB_AP_PASSWORD[0] == '\0' ? "(none, open network)" : LAB_AP_PASSWORD);
  Serial.printf("Dashboard:  %s\n", gPortalLocation);
  Serial.printf("Admin:      %sadmin\n", gPortalLocation);
  Serial.printf("Channel:    %u\n", static_cast<unsigned>(LAB_AP_CHANNEL));
  Serial.printf("MAC:        %s\n", gMac);
  Serial.println("----------------------------------------");
}

void labLoop() {
  if (!apReady) {
    blinkError();
    return;
  }

  // DNS replies are built in this loop. A few packets are handled each pass
  // so a burst of connectivity checks cannot stall the web server.
  pollDns();
  webServer.handleClient();
  pollScan();
  yield();
}
