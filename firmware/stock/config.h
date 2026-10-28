// =====================================================================
//  行情板配置 —— 改轮播内容、刷新频率主要改这里
// =====================================================================
#pragma once

// 固件发布地址（GitHub Pages）
#define FW_BASE_URL "https://huwenhou.github.io/ESP32-EINK42-BWR-STOCK-OTA"

// 每页停留时间（秒）。三色屏每次刷新约 15–20 秒、会闪烁，不宜太频繁
#define PAGE_SECONDS_ACTIVE 180   // 有市场在交易时：3 分钟换一页
#define PAGE_SECONDS_IDLE   900   // 全部休市时：15 分钟换一页

// 红色深度：强制控制器按这个温度（℃）选择刷新波形。
// 数值越低，波形越长，红色越深、刷新越慢；设 0 则使用屏内温度传感器
#define WAVE_TEMP_C 10

// 每页连续刷新两遍（红色还不够深时再打开，闪烁时间加倍）
#define RED_BOOST 0

// 屏幕驱动：0 = 开机自动识别；1 = UC8176 系列；2 = SSD1619A 专用驱动（HINK-E042A13）；3 = GxEPD2 的 SSD1683 驱动
#define FORCE_PANEL 0

// 市场类型：决定分时图的时间轴
enum MarketKind { MK_CN, MK_HK, MK_US };

struct IndexCfg {
  const char* code;   // 腾讯行情代码
  const char* name;   // 顶栏大字
  const char* tag;    // 顶栏小字
  const char* mkt;    // 市场状态键：SH / SZ / HK / US
  MarketKind  kind;
};

// 轮播顺序
static const IndexCfg INDICES[] = {
  { "sh000001", "上证指数", "A股 000001",  "SH", MK_CN },
  { "sz399006", "创业板指", "A股 399006",  "SZ", MK_CN },
  { "sz399001", "深证成指", "A股 399001",  "SZ", MK_CN },
  { "hkHSI",    "恒生指数", "港股 HSI",    "HK", MK_HK },
  { "us.IXIC",  "纳斯达克", "美股 IXIC",   "US", MK_US },
  { "us.DJI",   "道琼斯",   "纽交所 DJI",  "US", MK_US },
};
static const int N_INDICES = sizeof(INDICES) / sizeof(INDICES[0]);
