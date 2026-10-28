// Fog Timer, CYD version: ESP32 2.8" touch display (Freenove FNK0103 / "ESP32-32E" CYD)
// + 433 MHz transmitter and receiver.
//   - Touchscreen: mode, blast/pause +/-, Fog now, Stop, countdown
//   - Phone web page: same controls, weekly schedule, code learning, WiFi setup
//   - Learns the remote's ON/OFF codes itself (no capture sketch needed)
//   - Settings survive power loss
//
// Libraries: LovyanGFX, rc-switch
// Board: "ESP32 Dev Module", Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <RCSwitch.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <time.h>
#include "web_page.h"

// ---------- Settings you may want to change ----------
const char* WIFI_SSID = "";            // optional; can also be set from the web page
const char* WIFI_PASS = "";
const char* AP_SSID   = "FogTimer";    // fallback hotspot when home WiFi isn't reachable
const char* AP_PASS   = "fogmachine";  // 8+ characters
const char* HOSTNAME  = "fogtimer";    // http://fogtimer.local
const char* TZ_INFO   = "EST5EDT,M3.2.0,M11.1.0";  // US Eastern. Central: CST6CDT,M3.2.0,M11.1.0

// ---------- Pins ----------
const int RX_PIN = 35;                         // Input pin port: GND / IO35 / 3.3V
const int TX_PIN = 27;                         // SPI port, IO27 (labelled CS)
const int LED_R = 22, LED_G = 16, LED_B = 17;  // onboard RGB LED, active-low (generic CYDs use 4/16/17)

// ---------- Display + touch ----------
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
  lgfx::Touch_XPT2046 _touch;

 public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;  // HSPI
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 14;
      cfg.pin_mosi = 13;
      cfg.pin_miso = 12;
      cfg.pin_dc = 2;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = 15;
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.readable = true;
      cfg.invert = false;  // set true if colours look inverted
      cfg.rgb_order = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = 21;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {
      auto cfg = _touch.config();
      cfg.x_min = 300;  // rough defaults; replaced by on-screen calibration
      cfg.x_max = 3900;
      cfg.y_min = 200;
      cfg.y_max = 3700;
      cfg.pin_int = 36;
      cfg.bus_shared = false;
      cfg.spi_host = SPI3_HOST;  // VSPI
      cfg.freq = 1000000;
      cfg.pin_sclk = 25;
      cfg.pin_mosi = 32;
      cfg.pin_miso = 39;
      cfg.pin_cs = 33;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

// ---------- State ----------
enum { MODE_OFF, MODE_ON, MODE_SCHED };
const char* MODE_NAMES[] = {"OFF", "ON", "SCHED"};

struct Code {
  uint32_t value;
  uint8_t bits, protocol;
  uint16_t pulse;
};
struct Slot {  // days: bit 0 = Sunday ... bit 6 = Saturday; times in minutes after midnight
  uint8_t days, enabled;
  uint16_t start, end;
};
const int MAX_SLOTS = 8;
const uint8_t CFG_VERSION = 1;

struct Config {
  uint8_t version = CFG_VERSION;
  uint8_t mode = MODE_OFF;
  float blastS = 5, pauseS = 30;
  Code on{}, off{};
  uint8_t nSlots = 0;
  Slot slots[MAX_SLOTS]{};
};

struct Btn {  // touchscreen button hit area
  int16_t x, y, w, h;
};

LGFX lcd;
LGFX_Sprite top(&lcd);
RCSwitch rf;
WebServer server(80);
Preferences prefs;
Config cfg;

bool fogging = false, manualBlast = false, wasActive = false, apMode = false;
uint32_t phaseStart = 0, saveAt = 0, learnUntil = 0, lastDraw = 0;
uint8_t learnTarget = 0;  // 1 = ON, 2 = OFF
Code lastRx{};
bool valuesDirty = true, modeDirty = true;

// ---------- Config storage ----------
void loadConfig() {
  Config c;
  if (prefs.getBytes("cfg", &c, sizeof c) == sizeof c && c.version == CFG_VERSION) cfg = c;
}

// Debounced so holding +/- doesn't hammer the flash.
void saveSoon() { saveAt = millis() + 2000; }
void saveNow() {
  prefs.putBytes("cfg", &cfg, sizeof cfg);
  saveAt = 0;
}

// ---------- Clock + schedule ----------
bool localNow(tm& t) {
  time_t n = time(nullptr);
  if (n < 1700000000) return false;  // not synced yet
  localtime_r(&n, &t);
  return true;
}

String fmtClock(const tm& t, bool withDay) {
  char buf[24];
  strftime(buf, sizeof buf, withDay ? "%a %I:%M %p" : "%I:%M %p", &t);
  String s(buf);
  int i = withDay ? 4 : 0;
  if (s[i] == '0') s.remove(i, 1);
  return s;
}

bool inSlot(const Slot& s, const tm& t) {
  if (!s.enabled || !s.days) return false;
  int m = t.tm_hour * 60 + t.tm_min;
  bool today = s.days >> t.tm_wday & 1;
  bool yesterday = s.days >> ((t.tm_wday + 6) % 7) & 1;
  if (s.start == s.end) return today;  // all day
  if (s.start < s.end) return today && m >= s.start && m < s.end;
  return (today && m >= s.start) || (yesterday && m < s.end);  // runs past midnight
}

bool cycleWanted() {
  if (cfg.mode == MODE_ON) return true;
  if (cfg.mode != MODE_SCHED) return false;
  tm t;
  if (!localNow(t)) return false;
  for (int i = 0; i < cfg.nSlots; i++)
    if (inSlot(cfg.slots[i], t)) return true;
  return false;
}

String nextStart() {
  tm now;
  if (!localNow(now)) return "";
  time_t nowT = time(nullptr), best = 0;
  for (int off = 0; off <= 7; off++) {
    tm d = now;
    d.tm_mday += off;
    d.tm_hour = d.tm_min = d.tm_sec = 0;
    d.tm_isdst = -1;
    time_t midnight = mktime(&d);
    localtime_r(&midnight, &d);
    for (int i = 0; i < cfg.nSlots; i++) {
      const Slot& s = cfg.slots[i];
      if (!s.enabled || !(s.days >> d.tm_wday & 1)) continue;
      time_t start = midnight + s.start * 60;
      if (start > nowT && (!best || start < best)) best = start;
    }
  }
  if (!best) return "";
  tm b;
  localtime_r(&best, &b);
  return fmtClock(b, true);
}

// ---------- Radio + fog control ----------
bool haveCodes() { return cfg.on.value && cfg.off.value; }

void sendCode(const Code& c) {
  if (!c.value) return;
  rf.setProtocol(c.protocol);  // must come before setPulseLength
  rf.setPulseLength(c.pulse);
  rf.send(c.value, c.bits);
}

void setLed(bool r, bool g, bool b) {
  digitalWrite(LED_R, !r);
  digitalWrite(LED_G, !g);
  digitalWrite(LED_B, !b);
}

void fogOn() {
  sendCode(cfg.on);
  fogging = true;
  phaseStart = millis();
}

void fogOff() {
  sendCode(cfg.off);
  fogging = false;
  phaseStart = millis();
}

void updateFog() {
  bool active = cycleWanted();
  uint32_t el = millis() - phaseStart;
  if (fogging) {
    if (el >= cfg.blastS * 1000 || (!active && !manualBlast)) {
      fogOff();
      manualBlast = false;
    }
  } else if (active && (!wasActive || el >= cfg.pauseS * 1000)) {
    fogOn();  // cycle starts with a blast
  }
  wasActive = active;
}

void setMode(int m) {
  cfg.mode = constrain(m, 0, 2);
  modeDirty = true;
  saveSoon();
  if (cfg.mode == MODE_OFF) {
    manualBlast = false;
    if (fogging) fogOff();
  }
}

void fogNow() {
  if (fogging) return;
  manualBlast = true;
  fogOn();
}

void stopAll() {
  manualBlast = false;
  bool was = fogging;
  setMode(MODE_OFF);
  if (!was) sendCode(cfg.off);  // send OFF anyway, just in case
}

float stepPause(float p, int dir) {
  float s = dir > 0 ? (p < 60 ? 5 : p < 300 ? 15 : 60) : (p <= 60 ? 5 : p <= 300 ? 15 : 60);
  return constrain(p + dir * s, 5.0f, 1800.0f);
}

void adjust(bool blast, int dir) {
  if (blast) cfg.blastS = constrain(cfg.blastS + 0.5f * dir, 0.5f, 30.0f);
  else cfg.pauseS = stepPause(cfg.pauseS, dir);
  valuesDirty = true;
  saveSoon();
}

void handleRx() {
  if (!rf.available()) return;
  Code c{(uint32_t)rf.getReceivedValue(), (uint8_t)rf.getReceivedBitlength(),
         (uint8_t)rf.getReceivedProtocol(), (uint16_t)rf.getReceivedDelay()};
  rf.resetAvailable();
  if (!c.value) return;
  lastRx = c;
  Serial.printf("RX code=%lu bits=%u protocol=%u pulse=%u\n",
                (unsigned long)c.value, c.bits, c.protocol, c.pulse);
  if (learnTarget) {
    (learnTarget == 1 ? cfg.on : cfg.off) = c;
    learnTarget = 0;
    saveSoon();
  }
}

// ---------- WiFi ----------
String ipString() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  if (apMode) return String(AP_SSID) + " " + WiFi.softAPIP().toString();
  return "no WiFi";
}

void message(const char* msg) {
  lcd.fillScreen(TFT_BLACK);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextDatum(middle_center);
  lcd.drawString(msg, 160, 120);
}

void startWifi() {
  String ssid = prefs.getString("ssid", WIFI_SSID);
  String pass = prefs.getString("pass", WIFI_PASS);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  if (ssid.length()) {
    message("Connecting to WiFi...");
    WiFi.begin(ssid.c_str(), pass.c_str());
    uint32_t t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) delay(250);
  }
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_AP_STA);  // keep trying home WiFi in the background
    WiFi.softAP(AP_SSID, AP_PASS);
    apMode = true;
  }
  configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");
  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
}

// ---------- Web API ----------
String jsonEsc(const String& s) {
  String o;
  for (char ch : s) {
    if (ch == '"' || ch == '\\') o += '\\';
    o += ch;
  }
  return o;
}

String codeJson(const Code& c) {
  return "{\"v\":" + String(c.value) + ",\"b\":" + String(c.bits) + ",\"p\":" + String(c.protocol) +
         ",\"l\":" + String(c.pulse) + "}";
}

String stateJson() {
  bool active = cycleWanted();
  float len = fogging ? cfg.blastS : cfg.pauseS;
  float el = (millis() - phaseStart) / 1000.0f;
  float left = (fogging || active) ? max(0.0f, len - el) : 0.0f;
  tm t;
  bool tv = localNow(t);

  String j;
  j.reserve(1200);
  j += "{\"mode\":" + String(cfg.mode);
  j += ",\"fogging\":";
  j += fogging ? "true" : "false";
  j += ",\"active\":";
  j += active ? "true" : "false";
  j += ",\"blast\":" + String(cfg.blastS, 1);
  j += ",\"pause\":" + String(cfg.pauseS, 0);
  j += ",\"left\":" + String(left, 1);
  j += ",\"len\":" + String(len, 1);
  j += ",\"time\":\"" + (tv ? fmtClock(t, true) : String("")) + "\"";
  j += ",\"next\":\"" + nextStart() + "\"";
  j += ",\"slots\":[";
  for (int i = 0; i < cfg.nSlots; i++) {
    const Slot& s = cfg.slots[i];
    if (i) j += ",";
    j += "[" + String(s.days) + "," + String(s.start) + "," + String(s.end) + "," + String(s.enabled) + "]";
  }
  j += "],\"on\":" + codeJson(cfg.on);
  j += ",\"off\":" + codeJson(cfg.off);
  j += ",\"last\":" + codeJson(lastRx);
  j += ",\"learn\":" + String(learnTarget);
  j += ",\"learnLeft\":" + String(learnTarget ? (int)((learnUntil - millis()) / 1000) : 0);
  j += ",\"ip\":\"" + jsonEsc(ipString()) + "\"";
  j += ",\"ssid\":\"" + jsonEsc(WiFi.SSID()) + "\"}";
  return j;
}

// Schedule arrives as "days,start,end,enabled;days,start,end,enabled;..."
void parseSched(const String& s) {
  cfg.nSlots = 0;
  int i = 0;
  while (i < (int)s.length() && cfg.nSlots < MAX_SLOTS) {
    int j = s.indexOf(';', i);
    if (j < 0) j = s.length();
    int d, a, b, e;
    if (sscanf(s.substring(i, j).c_str(), "%d,%d,%d,%d", &d, &a, &b, &e) == 4) {
      Slot& sl = cfg.slots[cfg.nSlots++];
      sl.days = d & 0x7F;
      sl.start = constrain(a, 0, 1439);
      sl.end = constrain(b, 0, 1439);
      sl.enabled = e ? 1 : 0;
    }
    i = j + 1;
  }
}

void sendState() { server.send(200, "application/json", stateJson()); }

void handleCmd() {
  String a = server.arg("a");
  if (a == "mode") {
    setMode(server.arg("v").toInt());
  } else if (a == "fog") {
    fogNow();
  } else if (a == "stop") {
    stopAll();
  } else if (a == "set") {
    if (server.hasArg("blast")) cfg.blastS = constrain(server.arg("blast").toFloat(), 0.5f, 30.0f);
    if (server.hasArg("pause")) cfg.pauseS = constrain(server.arg("pause").toFloat(), 5.0f, 1800.0f);
    valuesDirty = true;
    saveSoon();
  } else if (a == "sched") {
    parseSched(server.arg("s"));
    saveSoon();
  } else if (a == "learn") {
    learnTarget = constrain(server.arg("v").toInt(), 0, 2);
    learnUntil = millis() + 20000;
  } else if (a == "test") {
    sendCode(server.arg("v") == "1" ? cfg.on : cfg.off);
  } else if (a == "wifi") {
    prefs.putString("ssid", server.arg("ssid"));
    prefs.putString("pass", server.arg("pass"));
    if (saveAt) saveNow();
    server.send(200, "application/json", "{\"restart\":true}");
    delay(500);
    ESP.restart();
  }
  sendState();
}

// ---------- Touchscreen UI (320 x 240, landscape) ----------
const Btn B_BLAST_DN{118, 72, 58, 44}, B_BLAST_UP{260, 72, 58, 44};
const Btn B_PAUSE_DN{118, 124, 58, 44}, B_PAUSE_UP{260, 124, 58, 44};
const Btn B_MODE{2, 180, 102, 58}, B_FOG{109, 180, 102, 58}, B_STOP{216, 180, 102, 58};

const uint16_t C_GRAY = 0x4208;
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return lcd.color565(r, g, b); }

bool hit(const Btn& b, int x, int y) { return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h; }

void drawButton(const Btn& b, uint16_t color, const char* label, const lgfx::IFont* font) {
  lcd.fillRoundRect(b.x, b.y, b.w, b.h, 8, color);
  lcd.setFont(font);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextDatum(middle_center);
  lcd.drawString(label, b.x + b.w / 2, b.y + b.h / 2);
}

void drawStatic() {
  lcd.fillScreen(TFT_BLACK);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextDatum(middle_left);
  lcd.drawString("Blast", 8, 94);
  lcd.drawString("Pause", 8, 146);
  drawButton(B_BLAST_DN, C_GRAY, "-", &fonts::Font4);
  drawButton(B_BLAST_UP, C_GRAY, "+", &fonts::Font4);
  drawButton(B_PAUSE_DN, C_GRAY, "-", &fonts::Font4);
  drawButton(B_PAUSE_UP, C_GRAY, "+", &fonts::Font4);
  drawButton(B_FOG, rgb(220, 110, 20), "FOG NOW", &fonts::FreeSansBold9pt7b);
  drawButton(B_STOP, rgb(170, 30, 30), "STOP", &fonts::FreeSansBold9pt7b);
  valuesDirty = modeDirty = true;
}

String fmtDur(float s, bool tenths) {
  char b[16];
  if (s >= 60) snprintf(b, sizeof b, "%d:%02d", (int)s / 60, (int)s % 60);
  else if (tenths) snprintf(b, sizeof b, "%.1f s", s);
  else snprintf(b, sizeof b, "%d s", (int)s);
  return b;
}

void drawValues() {
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextDatum(middle_center);
  lcd.fillRect(177, 72, 82, 44, TFT_BLACK);
  lcd.drawString(fmtDur(cfg.blastS, true).c_str(), 218, 94);
  lcd.fillRect(177, 124, 82, 44, TFT_BLACK);
  lcd.drawString(fmtDur(cfg.pauseS, false).c_str(), 218, 146);
}

void drawMode() {
  uint16_t c = cfg.mode == MODE_ON ? rgb(30, 140, 60) : cfg.mode == MODE_SCHED ? rgb(110, 70, 190) : C_GRAY;
  lcd.fillRoundRect(B_MODE.x, B_MODE.y, B_MODE.w, B_MODE.h, 8, c);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextDatum(middle_center);
  lcd.setFont(&fonts::Font2);
  lcd.drawString("MODE", B_MODE.x + B_MODE.w / 2, B_MODE.y + 15);
  lcd.setFont(&fonts::FreeSansBold9pt7b);
  lcd.drawString(MODE_NAMES[cfg.mode], B_MODE.x + B_MODE.w / 2, B_MODE.y + 38);
}

// Status bar, progress bar and info line, drawn off-screen to avoid flicker.
void drawTop() {
  bool active = cycleWanted();
  tm t;
  bool tv = localNow(t);
  const char* label;
  uint16_t bg;
  if (fogging) {
    label = "FOGGING";
    bg = rgb(200, 50, 40);
  } else if (active) {
    label = "WAITING";
    bg = rgb(30, 100, 200);
  } else if (cfg.mode == MODE_SCHED) {
    label = tv ? "SCHEDULED" : "NO CLOCK";
    bg = rgb(110, 70, 190);
  } else {
    label = "OFF";
    bg = C_GRAY;
  }

  top.fillSprite(TFT_BLACK);
  top.fillRoundRect(0, 0, 320, 38, 6, bg);
  top.setTextColor(TFT_WHITE);
  top.setFont(&fonts::Font4);
  top.setTextDatum(middle_left);
  top.drawString(label, 10, 20);

  if (fogging || active) {
    float len = fogging ? cfg.blastS : cfg.pauseS;
    float el = (millis() - phaseStart) / 1000.0f;
    top.setTextDatum(middle_right);
    top.drawString(fmtDur(ceilf(max(0.0f, len - el)), false).c_str(), 310, 20);
    top.drawRect(0, 42, 320, 8, C_GRAY);
    top.fillRect(2, 44, (int)(316 * min(1.0f, el / len)), 4, bg);
  } else if (cfg.mode == MODE_SCHED) {
    String n = nextStart();
    if (n.length()) {
      top.setFont(&fonts::Font2);
      top.setTextDatum(middle_right);
      top.drawString(("next " + n).c_str(), 310, 20);
    }
  }

  top.setFont(&fonts::Font2);
  top.setTextDatum(middle_left);
  top.setTextColor(rgb(160, 160, 160));
  String info = (tv ? fmtClock(t, true) : String("--:--")) + "   " + ipString();
  top.drawString(info.c_str(), 2, 58);
  top.setTextDatum(middle_right);
  top.setTextColor(rgb(240, 170, 40));
  if (learnTarget) top.drawString(learnTarget == 1 ? "press remote ON" : "press remote OFF", 318, 58);
  else if (!haveCodes()) top.drawString("learn codes", 318, 58);

  top.pushSprite(0, 0);
  setLed(fogging, !fogging && active, !fogging && !active && cfg.mode == MODE_SCHED);
}

bool touchDown = false;
uint32_t touchStart = 0, lastRepeat = 0, lastSeen = 0;

void handleTouch() {
  int32_t x, y;
  uint32_t now = millis();
  if (!lcd.getTouch(&x, &y)) {
    if (now - lastSeen > 80) touchDown = false;  // resistive panels flicker; debounce release
    return;
  }
  lastSeen = now;
  bool first = !touchDown;
  if (first) {
    touchDown = true;
    touchStart = lastRepeat = now;
  }
  bool repeat = !first && now - touchStart > 500 && now - lastRepeat > 120;
  if (repeat) lastRepeat = now;

  if (first || repeat) {  // +/- auto-repeat while held
    if (hit(B_BLAST_DN, x, y)) adjust(true, -1);
    else if (hit(B_BLAST_UP, x, y)) adjust(true, 1);
    else if (hit(B_PAUSE_DN, x, y)) adjust(false, -1);
    else if (hit(B_PAUSE_UP, x, y)) adjust(false, 1);
  }
  if (!first) return;
  if (hit(B_MODE, x, y)) setMode((cfg.mode + 1) % 3);
  else if (hit(B_FOG, x, y)) fogNow();
  else if (hit(B_STOP, x, y)) stopAll();
}

// Calibrates on first boot, or when the screen is held while powering on.
void calibrateTouchIfNeeded() {
  uint16_t cal[8];
  int32_t x, y;
  bool have = prefs.getBytes("tcal", cal, sizeof cal) == sizeof cal;
  if (have && !lcd.getTouch(&x, &y)) {
    lcd.setTouchCalibrate(cal);
    return;
  }
  message("Touch calibration");
  while (lcd.getTouch(&x, &y)) delay(10);
  delay(1000);
  lcd.fillScreen(TFT_BLACK);
  lcd.calibrateTouch(cal, TFT_WHITE, TFT_BLACK, 20);
  prefs.putBytes("tcal", cal, sizeof cal);
  lcd.setTouchCalibrate(cal);
}

// ---------- Main ----------
void setup() {
  Serial.begin(115200);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  setLed(false, false, false);

  prefs.begin("fog", false);
  loadConfig();

  lcd.init();
  lcd.setRotation(1);
  lcd.setBrightness(200);
  calibrateTouchIfNeeded();
  top.setColorDepth(16);
  top.createSprite(320, 64);

  rf.enableReceive(digitalPinToInterrupt(RX_PIN));
  rf.enableTransmit(TX_PIN);
  rf.setRepeatTransmit(8);
  sendCode(cfg.off);  // known state at power-up

  startWifi();
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/state", HTTP_GET, sendState);
  server.on("/api/cmd", HTTP_POST, handleCmd);
  server.begin();

  drawStatic();
}

void loop() {
  server.handleClient();
  handleRx();
  updateFog();
  handleTouch();

  uint32_t now = millis();
  if (learnTarget && (int32_t)(now - learnUntil) > 0) learnTarget = 0;
  if (saveAt && (int32_t)(now - saveAt) > 0) saveNow();
  if (valuesDirty) {
    drawValues();
    valuesDirty = false;
  }
  if (modeDirty) {
    drawMode();
    modeDirty = false;
  }
  if (now - lastDraw > 250) {
    lastDraw = now;
    drawTop();
  }
}
