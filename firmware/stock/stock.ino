// =====================================================================
//  4.2" 黑白红三色墨水屏 · 股市行情轮播
//  · 数据：腾讯行情（分时 + 昨收 + 市场状态），ESP32 直接拉取
//  · 六个指数依次轮播，涨红跌黑
//  · 每 10 分钟检查 GitHub Pages 上的新固件，自动 OTA
// =====================================================================
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <SPI.h>
#include <time.h>
#include <math.h>
#include <ArduinoJson.h>
#include <GxEPD2_3C.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "epd_ssd1619.h"
#include "version.h"
#include "config.h"
// 数字字体由 CI 用 Barlow Condensed Bold 生成
#include "BarlowCondensed_Bold30pt7b.h"
#include "BarlowCondensed_Bold12pt7b.h"
#include "BarlowCondensed_Bold9pt7b.h"

typedef void (*DrawFn)();   // 放在最前面，避免 Arduino 自动生成的函数声明找不到它

#define NUM_L &BarlowCondensed_Bold30pt7b   // 大点位
#define NUM_M &BarlowCondensed_Bold12pt7b   // 涨跌
#define NUM_S &BarlowCondensed_Bold9pt7b    // 底栏数值
#define CN_L  u8g2_font_wqy16_t_gb2312      // 指数名
#define CN_S  u8g2_font_wqy12_t_gb2312      // 其余中文

// ---------- Waveshare ESP32 驱动板引脚 ----------
#define EPD_BUSY 25
#define EPD_RST  26
#define EPD_DC   27
#define EPD_CS   15
#define EPD_SCK  13
#define EPD_MOSI 14
#define AP_NAME "Robbie's Stock Board"
#define BOOT_BTN 0   // 运行中长按 5 秒 = 清除 Wi-Fi 设置

// 两种常见 4.2" 三色屏驱动，开机识别后只用其中一个
GxEPD2_3C<GxEPD2_420c, GxEPD2_420c::HEIGHT / 4>
  dispUC(GxEPD2_420c(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
GxEPD2_3C<GxEPD2_420c_GDEY042Z98, GxEPD2_420c_GDEY042Z98::HEIGHT / 4>
  dispSSD(GxEPD2_420c_GDEY042Z98(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

EPD_SSD1619 dispK(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);   // SSD1619A 专用驱动

enum { PANEL_UC = 1, PANEL_SSD = 2, PANEL_SSD_LIB = 3 };
int panel = PANEL_UC;
Adafruit_GFX* G = nullptr;
U8G2_FOR_ADAFRUIT_GFX u8f;

const uint16_t BLACK = GxEPD_BLACK, WHITE = GxEPD_WHITE, RED = GxEPD_RED;

const char* URL_CN = "https://web.ifzq.gtimg.cn/appstock/app/minute/query?code=";
const char* URL_US = "https://web.ifzq.gtimg.cn/appstock/app/UsMinute/query?code=";
const unsigned long OTA_INTERVAL_MS = 10UL * 60UL * 1000UL;

// ---------------------------------------------------------------------
//  行情数据
// ---------------------------------------------------------------------
#define MAX_PTS 400
struct Quote {
  double price, prev, open, high, low, turnover;
  char timeStr[24];
  char status[24];
  bool isOpen;
  int n;
  uint16_t mi[MAX_PTS];   // 第几分钟（相对开盘）
  float p[MAX_PTS];
};
Quote q;
String marketStr;              // 腾讯返回的全市场状态串，缓存给美股页用
int page = -1;
const IndexCfg* cfg = &INDICES[0];
char statusL1[48], statusL2[64];

unsigned long lastShow = 0, lastOta = 0;
bool everShown = false;

// ---------------------------------------------------------------------
//  时间轴
// ---------------------------------------------------------------------
int sessionTotal(MarketKind k) { return k == MK_CN ? 240 : k == MK_HK ? 330 : 390; }
int sessionMid(MarketKind k)   { return k == MK_CN ? 120 : k == MK_HK ? 150 : 0; }
int minuteIndex(MarketKind k, int hhmm) {
  int t = (hhmm / 100) * 60 + hhmm % 100;
  int idx;
  if (k == MK_CN)      idx = (t <= 690) ? t - 570 : 120 + (t - 780);
  else if (k == MK_HK) idx = (t <= 720) ? t - 570 : 150 + (t - 780);
  else                 idx = t - 570;
  int tot = sessionTotal(k);
  return idx < 0 ? 0 : (idx > tot ? tot : idx);
}

// 美国夏令时：3 月第二个周日 ~ 11 月第一个周日
bool usDst() {
  struct tm t;
  if (!getLocalTime(&t, 100)) return true;
  int m = t.tm_mon + 1, d = t.tm_mday, w = t.tm_wday;
  if (m < 3 || m > 11) return false;
  if (m > 3 && m < 11) return true;
  int prevSunday = d - w;
  if (m == 3) return prevSunday >= 8;
  return prevSunday <= 0;   // 11 月：第一个周日之前
}

// ---------------------------------------------------------------------
//  市场状态："...|SH_close_已收盘|US_open_交易中|..."
// ---------------------------------------------------------------------
bool marketInfo(const char* key, char* label, size_t n, bool& open) {
  String pat = String("|") + key + "_";
  int i = marketStr.indexOf(pat);
  if (i < 0) return false;
  i += pat.length();
  int j = marketStr.indexOf('_', i);
  if (j < 0) return false;
  open = marketStr.substring(i, j) == "open";
  int k = marketStr.indexOf('|', j + 1);
  if (k < 0) k = marketStr.length();
  strlcpy(label, marketStr.substring(j + 1, k).c_str(), n);
  return true;
}
bool anyMarketOpen() {
  const char* keys[] = { "SH", "SZ", "HK", "US" };
  char lab[24]; bool open;
  for (auto k : keys)
    if (marketInfo(k, lab, sizeof(lab), open) && open) return true;
  return false;
}

// ---------------------------------------------------------------------
//  拉取一个指数
// ---------------------------------------------------------------------
bool fetchIndex(const IndexCfg& c) {
  if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); return false; }
  String url = String(c.kind == MK_US ? URL_US : URL_CN) + c.code;

  WiFiClientSecure cli;
  cli.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);          // 避免 chunked，直接流式解析
  http.setTimeout(15000);
  if (!http.begin(cli, url)) return false;
  http.addHeader("User-Agent", "Mozilla/5.0");
  int code = http.GET();
  if (code != 200) { Serial.printf("%s HTTP %d\n", c.code, code); http.end(); return false; }

  JsonDocument filter;
  filter["data"][c.code]["data"]["data"] = true;
  filter["data"][c.code]["data"]["date"] = true;
  filter["data"][c.code]["qt"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(),
                                             DeserializationOption::Filter(filter));
  http.end();
  if (err) { Serial.printf("%s JSON %s\n", c.code, err.c_str()); return false; }

  JsonObject d = doc["data"][c.code];
  JsonArray arr = d["data"]["data"];
  if (arr.isNull() || arr.size() == 0) return false;

  // 市场状态（美股接口不带，沿用 A 股/港股页缓存的）
  const char* mk = d["qt"]["market"][0];
  if (mk && *mk) marketStr = mk;

  // 昨收、今开：qt 里那个长数组的第 4、5 项
  double qtPrev = 0, qtOpen = 0;
  for (JsonPair kv : d["qt"].as<JsonObject>()) {
    if (!strcmp(kv.key().c_str(), "market") || !strcmp(kv.key().c_str(), "zhishu")) continue;
    JsonArray a = kv.value().as<JsonArray>();
    if (!a.isNull() && a.size() > 34) {
      qtPrev = atof(a[4] | "0");
      qtOpen = atof(a[5] | "0");
      break;
    }
  }

  // 分时："0930 3676.92 4018168 7193271347.40"（美股只有三段：时间 价格 累计量）
  q.n = 0; q.high = -1e18; q.low = 1e18; q.turnover = 0;
  int lastHHMM = 0;
  for (JsonVariant v : arr) {
    const char* s = v.as<const char*>();
    if (!s) continue;
    int hhmm = 0; double pr = 0, f3 = 0, f4 = 0;
    int got = sscanf(s, "%d %lf %lf %lf", &hhmm, &pr, &f3, &f4);
    if (got < 2 || pr <= 0) continue;
    if (q.n < MAX_PTS) {
      q.mi[q.n] = minuteIndex(c.kind, hhmm);
      q.p[q.n] = pr;
      q.n++;
    }
    if (pr > q.high) q.high = pr;
    if (pr < q.low) q.low = pr;
    q.price = pr;
    lastHHMM = hhmm;
    q.turnover = (c.kind == MK_US) ? f3 : (got >= 4 ? f4 : 0);
  }
  if (q.n == 0) return false;
  q.open = qtOpen > 0 ? qtOpen : q.p[0];
  q.prev = qtPrev > 0 ? qtPrev : q.p[0];
  if (q.open > q.high) q.high = q.open;
  if (q.open < q.low) q.low = q.open;

  // 顶栏时间
  int hh = lastHHMM / 100, mm = lastHHMM % 100;
  const char* date = d["data"]["date"] | "";
  if (c.kind != MK_US && strlen(date) == 8) {
    int mon = (date[4] - '0') * 10 + (date[5] - '0');
    int day = (date[6] - '0') * 10 + (date[7] - '0');
    snprintf(q.timeStr, sizeof(q.timeStr), "%d月%d日 %02d:%02d", mon, day, hh, mm);
  } else {
    int bj = (hh * 60 + mm + (usDst() ? 720 : 780)) % 1440;
    snprintf(q.timeStr, sizeof(q.timeStr), "北京 %02d:%02d", bj / 60, bj % 60);
  }

  q.status[0] = 0; q.isOpen = false;
  marketInfo(c.mkt, q.status, sizeof(q.status), q.isOpen);
  return true;
}

// ---------------------------------------------------------------------
//  绘图工具
// ---------------------------------------------------------------------
// 中文：点阵字体没有粗体，错开 1 像素重复画来加粗
void cn(int x, int y, const char* s, const uint8_t* f, uint16_t c, bool bold = true) {
  u8f.setFont(f);
  u8f.setFontMode(1);            // setFont 会把背景重置为不透明，必须每次重新设为透明
  u8f.setForegroundColor(c);
  int dyMax = (bold && f == CN_L) ? 1 : 0;
  for (int dx = 0; dx <= (bold ? 1 : 0); dx++)
    for (int dy = 0; dy <= dyMax; dy++) {
      u8f.setCursor(x + dx, y + dy);
      u8f.print(s);
    }
}
int cnW(const char* s, const uint8_t* f) { u8f.setFont(f); return u8f.getUTF8Width(s) + 1; }

void num(int x, int y, const char* s, const GFXfont* f, uint16_t c) {
  G->setFont(f);
  G->setTextColor(c);
  G->setTextWrap(false);
  G->setCursor(x, y);
  G->print(s);
}
int numW(const char* s, const GFXfont* f) {
  int w = 0;
  for (; *s; s++) {
    uint8_t ch = *s;
    if (ch < f->first || ch > f->last) continue;
    w += f->glyph[ch - f->first].xAdvance;
  }
  return w;
}

// 千分位：3681.47 → "3,681.47"
void fmtNum(double v, int dec, char* out, size_t n) {
  char tmp[32];
  snprintf(tmp, sizeof(tmp), "%.*f", dec, fabs(v));
  const char* dot = strchr(tmp, '.');
  int intLen = dot ? dot - tmp : strlen(tmp);
  char buf[40]; int o = 0;
  if (v < 0) buf[o++] = '-';
  for (int i = 0; i < intLen; i++) {
    buf[o++] = tmp[i];
    int left = intLen - 1 - i;
    if (left > 0 && left % 3 == 0) buf[o++] = ',';
  }
  if (dot) { strcpy(buf + o, dot); o += strlen(dot); }
  buf[o] = 0;
  strlcpy(out, buf, n);
}

// ---------------------------------------------------------------------
//  一页行情（布局与设计稿一致）
// ---------------------------------------------------------------------
void drawPage() {
  G->fillScreen(WHITE);
  bool up = q.price > q.prev;
  uint16_t tone = up ? RED : BLACK;
  char buf[48];

  // --- 顶栏 ---
  G->fillRect(0, 0, 400, 34, BLACK);
  cn(10, 24, cfg->name, CN_L, WHITE);
  cn(10 + cnW(cfg->name, CN_L) + 8, 23, cfg->tag, CN_S, WHITE, false);
  snprintf(buf, sizeof(buf), "%d/%d", page + 1, N_INDICES);
  int x = 390 - cnW(buf, CN_S);
  cn(x, 23, buf, CN_S, WHITE, false);
  if (q.status[0]) {
    int cw = cnW(q.status, CN_S) + 8;
    x -= 6 + cw;
    G->fillRect(x, 9, cw, 17, q.isOpen ? RED : WHITE);
    cn(x + 4, 23, q.status, CN_S, q.isOpen ? WHITE : BLACK);
  }
  x -= 6;
  cn(x - cnW(q.timeStr, CN_S), 23, q.timeStr, CN_S, WHITE, false);

  // --- 大点位 ---
  fmtNum(q.price, 2, buf, sizeof(buf));
  num(10, 84, buf, NUM_L, tone);

  // --- 涨跌 ---
  double chg = q.price - q.prev;
  double pct = q.prev > 0 ? chg / q.prev * 100.0 : 0;
  if (up) G->fillTriangle(10, 109, 26, 109, 18, 95, tone);
  else    G->fillTriangle(10, 95, 26, 95, 18, 109, tone);
  char a[24], b[24];
  snprintf(a, sizeof(a), "%+.2f", chg);
  snprintf(b, sizeof(b), "%+.2f%%", pct);
  num(32, 110, a, NUM_M, tone);
  num(32 + numW(a, NUM_M) + 18, 110, b, NUM_M, tone);

  // --- 分时图 ---
  const int CX = 10, CY = 120, CW = 380, CH = 96;
  MarketKind k = cfg->kind;
  int tot = sessionTotal(k), mid = sessionMid(k);
  double lo = fmin(q.low, q.prev), hi = fmax(q.high, q.prev);
  double pad = (hi - lo) * 0.08;
  if (hi - lo < 1e-6) pad = 1;
  lo -= pad; hi += pad;
  auto Y = [&](double v) { return CY + (int)((hi - v) / (hi - lo) * CH); };

  int by = Y(q.prev);
  for (int xx = CX; xx < CX + CW; xx += 6) G->drawFastHLine(xx, by, 3, BLACK);
  if (mid > 0) {
    int mx = CX + mid * CW / tot;
    for (int yy = CY; yy < CY + CH; yy += 4) G->drawPixel(mx, yy, BLACK);
  }
  char pv[24];
  fmtNum(q.prev, 2, pv, sizeof(pv));
  snprintf(buf, sizeof(buf), "昨收 %s", pv);
  int ly = (by < CY + CH - 16) ? by + 14 : by - 4;
  cn(CX + 2, ly, buf, CN_S, BLACK);

  int px = -1, py = -1;
  for (int i = 0; i < q.n; i++) {
    int xx = CX + (int)q.mi[i] * CW / tot;
    int yy = Y(q.p[i]);
    if (px >= 0) {
      G->drawLine(px, py, xx, yy, tone);
      G->drawLine(px, py + 1, xx, yy + 1, tone);
      G->drawLine(px + 1, py, xx + 1, yy, tone);
    }
    px = xx; py = yy;
  }
  if (q.isOpen && px >= 0) G->fillCircle(px, py, 4, tone);

  // 时间刻度
  cn(CX, 231, "9:30", CN_S, BLACK);
  const char* midL = k == MK_CN ? "11:30/13:00" : k == MK_HK ? "12:00/13:00" : "12:45";
  const char* endL = k == MK_CN ? "15:00" : k == MK_HK ? "16:00" : "16:00 美东";
  int mlx = (k == MK_US ? CX + CW / 2 : CX + mid * CW / tot) - cnW(midL, CN_S) / 2;
  cn(mlx, 231, midL, CN_S, BLACK);
  cn(CX + CW - cnW(endL, CN_S), 231, endL, CN_S, BLACK);

  // --- 底栏 ---
  G->fillRect(0, 243, 400, 2, BLACK);
  const char* labels[4] = { "今开", "最高", "最低", k == MK_US ? "成交量" : "成交额" };
  for (int i = 0; i < 4; i++) {
    int cx = 10 + i * 96;
    cn(cx, 259, labels[i], CN_S, BLACK);
    uint16_t col = BLACK;
    const char* suffix = "";
    switch (i) {
      case 0: fmtNum(q.open, 2, buf, sizeof(buf)); break;
      case 1: fmtNum(q.high, 2, buf, sizeof(buf)); if (q.high > q.prev) col = RED; break;
      case 2: fmtNum(q.low, 2, buf, sizeof(buf)); break;
      case 3:
        if (k == MK_US) { snprintf(buf, sizeof(buf), "%.1f", q.turnover / 1e8); suffix = "亿股"; }
        else { fmtNum(round(q.turnover / 1e8), 0, buf, sizeof(buf)); suffix = k == MK_HK ? "亿港元" : "亿"; }
        break;
    }
    num(cx, 282, buf, NUM_S, col);
    if (*suffix) cn(cx + numW(buf, NUM_S) + 2, 282, suffix, CN_S, BLACK);
  }
}

void drawStatus() {
  G->fillScreen(WHITE);
  G->fillRect(0, 0, 400, 34, BLACK);
  cn(10, 24, "行情看板", CN_L, WHITE);
  char v[16]; snprintf(v, sizeof(v), "v%d", FW_VERSION);
  cn(390 - cnW(v, CN_S), 23, v, CN_S, WHITE, false);
  cn(200 - cnW(statusL1, CN_L) / 2, 140, statusL1, CN_L, BLACK);
  cn(200 - cnW(statusL2, CN_S) / 2, 172, statusL2, CN_S, RED);
  const char* pn = panel == PANEL_SSD ? "屏幕驱动 SSD1619A" : panel == PANEL_SSD_LIB ? "屏幕驱动 SSD1683" : "屏幕驱动 UC8176";
  cn(10, 290, pn, CN_S, BLACK, false);
}

// ---------------------------------------------------------------------
//  渲染（两种驱动共用一套画法）
// ---------------------------------------------------------------------
template <class D> void renderOn(D& d, DrawFn fn) {
  d.setRotation(0);
  d.setFullWindow();
  d.firstPage();
  do { fn(); } while (d.nextPage());
  d.hibernate();
}
void render(DrawFn fn) {
  if (panel == PANEL_SSD) { dispK.fillScreen(WHITE); fn(); dispK.display(); }
  else if (panel == PANEL_SSD_LIB) renderOn(dispSSD, fn);
  else renderOn(dispUC, fn);
}
void showStatus(const char* l1, const char* l2) {
  strlcpy(statusL1, l1, sizeof(statusL1));
  strlcpy(statusL2, l2, sizeof(statusL2));
  render(drawStatus);
}

// 复位后读 BUSY 空闲电平：UC8176 空闲为高，SSD1683 空闲为低
int detectPanel() {
  if (FORCE_PANEL) return FORCE_PANEL;
  pinMode(EPD_BUSY, INPUT);
  pinMode(EPD_RST, OUTPUT);
  digitalWrite(EPD_RST, HIGH); delay(20);
  digitalWrite(EPD_RST, LOW);  delay(20);
  digitalWrite(EPD_RST, HIGH); delay(400);
  int hi = 0;
  for (int i = 0; i < 10; i++) { hi += digitalRead(EPD_BUSY); delay(5); }
  Serial.printf("BUSY idle high count %d/10\n", hi);
  return hi >= 5 ? PANEL_UC : PANEL_SSD;
}

// ---------------------------------------------------------------------
//  OTA
// ---------------------------------------------------------------------
long fetchRemoteVersion() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  String url = String(FW_BASE_URL) + "/version.txt?t=" + String(millis());
  if (!http.begin(client, url)) return -1;
  long v = -1;
  if (http.GET() == 200) { String b = http.getString(); b.trim(); v = b.toInt(); }
  http.end();
  return v;
}
void checkForUpdate() {
  if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); return; }
  long remote = fetchRemoteVersion();
  Serial.printf("local v%d, remote v%ld\n", FW_VERSION, remote);
  if (remote <= FW_VERSION) return;
  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(true);
  if (httpUpdate.update(client, String(FW_BASE_URL) + "/firmware.bin") == HTTP_UPDATE_FAILED)
    Serial.printf("Update failed: %s\n", httpUpdate.getLastErrorString().c_str());
}

void checkResetButton() {
  static unsigned long pressedAt = 0;
  if (digitalRead(BOOT_BTN) == LOW) {
    if (pressedAt == 0) pressedAt = millis();
    if (millis() - pressedAt >= 5000) {
      WiFiManager wm;
      wm.resetSettings();
      showStatus("Wi-Fi 已清除", "正在重启...");
      delay(1000);
      ESP.restart();
    }
  } else {
    pressedAt = 0;
  }
}

// ---------------------------------------------------------------------
//  轮播：取下一页，失败就跳过，全部失败显示提示
// ---------------------------------------------------------------------
void showNext() {
  for (int tries = 0; tries < N_INDICES; tries++) {
    page = (page + 1) % N_INDICES;
    cfg = &INDICES[page];
    if (fetchIndex(*cfg)) {
      render(drawPage);
      if (RED_BOOST) render(drawPage);   // 第二遍让红色更深
      everShown = true;
      return;
    }
  }
  showStatus("行情获取失败", "稍后自动重试，请检查网络");
}

void setup() {
  Serial.begin(115200);
  pinMode(BOOT_BTN, INPUT_PULLUP);

  panel = detectPanel();
  SPI.end();
  SPI.begin(EPD_SCK, 12, EPD_MOSI, EPD_CS);
  if (panel == PANEL_SSD) { dispK.begin(); dispK.setWaveTemp(WAVE_TEMP_C); G = &dispK; }
  else if (panel == PANEL_SSD_LIB) { dispSSD.init(115200, true, 2, false); G = &dispSSD; }
  else                    { dispUC.init(115200, true, 2, false);  G = &dispUC; }
  u8f.begin(*G);
  u8f.setFontMode(1);
  u8f.setFontDirection(0);

  showStatus("正在连接 Wi-Fi", "首次使用请用手机连接热点配网");

  WiFiManager wm;
  wm.setAPCallback([](WiFiManager*) {
    showStatus("请连接热点 " AP_NAME, "在弹出的页面里选择 Wi-Fi 并输入密码");
  });
  wm.setConfigPortalTimeout(300);
  if (!wm.autoConnect(AP_NAME)) {
    showStatus("Wi-Fi 连接失败", "正在重启...");
    delay(3000);
    ESP.restart();
  }

  WiFi.setSleep(false);          // 关闭 Wi-Fi 省电模式：很多路由器会因此断开空闲设备
  WiFi.setAutoReconnect(true);
  configTzTime("CST-8", "ntp.aliyun.com", "ntp.tencent.com", "cn.pool.ntp.org");

  checkForUpdate();
  lastOta = millis();
  showNext();
  lastShow = millis();
}

// Wi-Fi 看门狗：断线后每 30 秒重连一次，连续断 10 分钟就重启
void wifiWatchdog() {
  static unsigned long offSince = 0, lastTry = 0;
  if (WiFi.status() == WL_CONNECTED) { offSince = 0; return; }
  unsigned long now = millis();
  if (offSince == 0) { offSince = now; lastTry = now; Serial.println("WiFi lost"); }
  if (now - lastTry >= 30000UL) {
    lastTry = now;
    WiFi.disconnect();
    WiFi.begin();
  }
  if (now - offSince >= 10UL * 60UL * 1000UL) ESP.restart();
}

void loop() {
  checkResetButton();
  wifiWatchdog();
  unsigned long interval = (anyMarketOpen() ? PAGE_SECONDS_ACTIVE : PAGE_SECONDS_IDLE) * 1000UL;
  if (!everShown) interval = 60UL * 1000UL;   // 一页都没成功时每分钟重试
  if (millis() - lastShow >= interval) {
    if (millis() - lastOta >= OTA_INTERVAL_MS) { lastOta = millis(); checkForUpdate(); }
    showNext();
    lastShow = millis();
  }
  delay(100);
}
