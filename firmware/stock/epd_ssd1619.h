// =====================================================================
//  SSD1619A 4.2" 400×300 黑白红屏专用驱动（HINK-E042A13-A0）
//  · 整屏缓冲（黑白层 + 红层各 15000 字节），一次性刷新
//  · 可强制指定"波形温度"：温度设得越低，控制器选用的驱动波形越长，
//    红色粒子被推得越充分，颜色越深；代价是刷新时间变长
// =====================================================================
#pragma once
#include <Adafruit_GFX.h>
#include <SPI.h>
#include <GxEPD2.h>   // 只借用 GxEPD_BLACK / WHITE / RED 颜色常量

class EPD_SSD1619 : public Adafruit_GFX {
 public:
  static const int W = 400, H = 300, BYTES = W / 8 * H;

  EPD_SSD1619(int8_t cs, int8_t dc, int8_t rst, int8_t busy)
    : Adafruit_GFX(W, H), _cs(cs), _dc(dc), _rst(rst), _busy(busy) {}

  void begin() {
    pinMode(_cs, OUTPUT);  digitalWrite(_cs, HIGH);
    pinMode(_dc, OUTPUT);  digitalWrite(_dc, HIGH);
    pinMode(_rst, OUTPUT); digitalWrite(_rst, HIGH);
    pinMode(_busy, INPUT);
    fillScreen(GxEPD_WHITE);
  }

  // tempC > 0：强制按该温度选波形；0：用屏内温度传感器
  void setWaveTemp(int tempC) { _temp = tempC; }

  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    uint32_t i = (uint32_t)y * (W / 8) + (x >> 3);
    uint8_t m = 0x80 >> (x & 7);
    if (c == GxEPD_RED)        { _bw[i] |= m;  _rd[i] |= m;  }   // 红：黑白层=白，红层=1
    else if (c == GxEPD_BLACK) { _bw[i] &= ~m; _rd[i] &= ~m; }
    else                       { _bw[i] |= m;  _rd[i] &= ~m; }   // 白
  }
  void fillScreen(uint16_t c) override {
    memset(_bw, c == GxEPD_BLACK ? 0x00 : 0xFF, BYTES);
    memset(_rd, c == GxEPD_RED ? 0xFF : 0x00, BYTES);
  }

  // 把缓冲刷到屏上（约 15–30 秒，取决于波形温度）
  bool display() {
    _reset();
    if (!_wait(3000)) return false;
    _cmd(0x12);                      // 软复位
    if (!_wait(3000)) return false;

    _cmd(0x74); _data(0x54);         // 模拟电路控制（SSD1619 必设）
    _cmd(0x7E); _data(0x3B);         // 数字电路控制
    _cmd(0x01); _data((H - 1) & 0xFF); _data((H - 1) >> 8); _data(0x00);  // 300 行
    _cmd(0x11); _data(0x03);         // X、Y 递增
    _cmd(0x44); _data(0x00); _data(W / 8 - 1);
    _cmd(0x45); _data(0x00); _data(0x00); _data((H - 1) & 0xFF); _data((H - 1) >> 8);
    _cmd(0x3C); _data(0x05);         // 边框白
    _cmd(0x18); _data(0x80);         // 内部温度传感器

    _cmd(0x4E); _data(0x00);
    _cmd(0x4F); _data(0x00); _data(0x00);
    _cmd(0x24); _bulk(_bw, BYTES);   // 黑白层：1=白 0=黑
    _cmd(0x4E); _data(0x00);
    _cmd(0x4F); _data(0x00); _data(0x00);
    _cmd(0x26); _bulk(_rd, BYTES);   // 红层：1=红

    if (_temp > 0) {
      _cmd(0x1A); _data((uint8_t)_temp); _data(0x00);   // 写入假定温度
      _cmd(0x22); _data(0xD7);       // 不读传感器，按写入的温度加载波形并刷新
    } else {
      _cmd(0x22); _data(0xF7);       // 读传感器温度、加载波形并刷新
    }
    _cmd(0x20);
    bool ok = _wait(60000);
    _cmd(0x10); _data(0x01);         // 深度睡眠
    return ok;
  }

 private:
  int8_t _cs, _dc, _rst, _busy;
  int _temp = 0;
  uint8_t _bw[BYTES], _rd[BYTES];
  const SPISettings _spi = SPISettings(4000000, MSBFIRST, SPI_MODE0);

  void _reset() {
    digitalWrite(_rst, LOW);  delay(20);
    digitalWrite(_rst, HIGH); delay(20);
  }
  bool _wait(uint32_t ms) {          // BUSY 高 = 忙
    uint32_t t = millis();
    delay(2);
    while (digitalRead(_busy) == HIGH) {
      if (millis() - t > ms) { Serial.println("EPD busy timeout"); return false; }
      delay(5);
    }
    return true;
  }
  void _cmd(uint8_t c) {
    SPI.beginTransaction(_spi);
    digitalWrite(_dc, LOW); digitalWrite(_cs, LOW);
    SPI.transfer(c);
    digitalWrite(_cs, HIGH); digitalWrite(_dc, HIGH);
    SPI.endTransaction();
  }
  void _data(uint8_t d) {
    SPI.beginTransaction(_spi);
    digitalWrite(_cs, LOW);
    SPI.transfer(d);
    digitalWrite(_cs, HIGH);
    SPI.endTransaction();
  }
  void _bulk(const uint8_t* p, uint32_t n) {
    SPI.beginTransaction(_spi);
    digitalWrite(_cs, LOW);
    for (uint32_t i = 0; i < n; i++) SPI.transfer(p[i]);
    digitalWrite(_cs, HIGH);
    SPI.endTransaction();
  }
};
