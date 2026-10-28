// =====================================================================
//  行情板配置 —— 改轮播内容、刷新频率主要改这里
// =====================================================================
#pragma once

// 固件发布地址（GitHub Pages）
#define FW_BASE_URL "https://huwenhou.github.io/ESP32-EINK42-BWR-STOCK-OTA"

// 每页停留时间（秒）。三色屏每次刷新约 15–20 秒、会闪烁，不宜太频繁
#define PAGE_SECONDS_ACTIVE 180   // 有市场在交易时：3 分钟换一页
#define PAGE_SECONDS_IDLE   900   // 全部休市时：15 分钟换一页

// 屏幕驱动：0 = 开机自动识别；1 = UC8176 系列(GDEW042Z15)；2 = SSD1683 系列(GDEY042Z98)
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
