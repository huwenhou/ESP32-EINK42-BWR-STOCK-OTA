# 4.2" 黑白红三色墨水屏 · 股市行情轮播

ESP32 驱动板 + 4.2 寸三色屏（HINK-E042A13-A0）。六个指数依次轮播：
上证指数 → 创业板指 → 深证成指 → 恒生指数 → 纳斯达克 → 道琼斯。

- 数据：腾讯行情接口，ESP32 直接拉取（分时、昨收、市场状态）
- 涨红跌黑；交易中的市场画到当前分钟并在线尾打点
- 有市场交易时 3 分钟换页，全部休市时 15 分钟换页（`config.h` 可改）
- 屏幕驱动开机自动识别（UC8176 / SSD1683），识别结果显示在开机画面左下角

## 目录
```
firmware/stock/stock.ino   主程序（取数、排版、OTA）
firmware/stock/config.h    轮播列表、刷新频率、驱动强制选择
web/index.html             网页刷机页
.github/workflows/build.yml  生成数字字体 → 编译 → 发布到 GitHub Pages
.github/workflows/probe.yml  手动运行，查看行情接口原始返回
```

## 刷机
电脑 Chrome 打开 https://io.huwenhou.com/ESP32-EINK42-BWR-STOCK-OTA ，点安装，选驱动板串口。
之后连热点 **Robbie's Stock Board** 配 Wi-Fi。

## 更新
推送到 main → Actions 自动编译发布 → 板子换页时（至少隔 10 分钟）检查新版本并自动升级。

## 本地编译
数字字体 `BarlowCondensed_Bold*pt7b.h` 由 CI 生成，不在仓库里。本地用 Arduino IDE 编译时，
先从 Actions 的步骤里照做一遍 fontconvert，或把这三个头文件换成 Adafruit GFX 自带字体。
Partition Scheme 选 **Minimal SPIFFS (1.9MB APP with OTA)**。

## 换页内容
改 `config.h` 里的 `INDICES` 即可。代码格式：A 股 `sh000001`、`sz399001`，港股 `hkHSI`，美股 `us.IXIC`、`us.DJI`、`us.INX`（标普 500）。

## 注意
- 行情仅供参考，有分钟级延迟；不构成投资建议。
- 三色屏每次刷新约 15–20 秒并会闪烁，属正常现象。
