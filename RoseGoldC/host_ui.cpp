#include "interp.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CGGeometry.h>
#include <TargetConditionals.h>
#include <objc/message.h>
#include <objc/objc.h>
#include <objc/runtime.h>
#elif defined(__unix__)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cstdlib>
#include <cstring>
#ifdef ROSEGOLD_WAYLAND
#include <poll.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
extern "C" {
#include <wayland-client.h>
}
#endif
#undef Bool
#endif

namespace {

struct HostWin {
  bool alive = false;
  bool mapped = false;
  int width = 0;
  int height = 0;
  long long id = 0;
  std::string title;
#ifdef _WIN32
  HWND hwnd = nullptr;
#elif defined(__APPLE__)
  void *nswin = nullptr;
#elif defined(__unix__)
  unsigned long xid = 0;
  void *wls = nullptr;
  void *xdg_s = nullptr;
  void *xdg_t = nullptr;
  void *buf = nullptr;
  void *pixels = nullptr;
  size_t buf_size = 0;
  int buf_w = 0;
  int buf_h = 0;
#endif
  std::vector<uint32_t> fb;
  int fb_w = 0;
  int fb_h = 0;
  int mouse_x = 0;
  int mouse_y = 0;
  bool mouse_down = false;
  bool mouse_click = false;
};

std::map<long long, HostWin> gWins;
long long gNext = 1;
Interpreter *gUiInterp = nullptr;
std::map<long long, Value> gFrameFns;
bool gInFrame = false;

bool anyAlive() {
  for (const auto &kv : gWins) {
    if (kv.second.alive)
      return true;
  }
  return false;
}

HostWin *findAlive(long long id) {
  auto it = gWins.find(id);
  if (it == gWins.end() || !it->second.alive)
    return nullptr;
  return &it->second;
}

uint32_t packRgb(long long color) {
  const unsigned r = static_cast<unsigned>(color >> 16) & 255u;
  const unsigned g = static_cast<unsigned>(color >> 8) & 255u;
  const unsigned b = static_cast<unsigned>(color) & 255u;
  return b | (g << 8) | (r << 16);
}

void ensureFb(HostWin &win) {
  int w = win.width > 0 ? win.width : 1;
  int h = win.height > 0 ? win.height : 1;
  if (w > 16384)
    w = 16384;
  if (h > 16384)
    h = 16384;
  if (win.fb_w == w && win.fb_h == h &&
      win.fb.size() == static_cast<size_t>(w) * static_cast<size_t>(h))
    return;
  win.fb.assign(static_cast<size_t>(w) * static_cast<size_t>(h), packRgb(0xF2F2F2));
  win.fb_w = w;
  win.fb_h = h;
}

void applyClientSize(HostWin &win, int w, int h) {
  if (w < 1 || h < 1)
    return;
  if (w > 16384)
    w = 16384;
  if (h > 16384)
    h = 16384;
  win.width = w;
  win.height = h;
  ensureFb(win);
}

void runFrame(long long id) {
  if (!gUiInterp || gInFrame || id <= 0)
    return;
  auto it = gFrameFns.find(id);
  if (it == gFrameFns.end() || it->second.kind != Value::Kind::FnRef)
    return;
  struct Guard {
    bool &flag;
    explicit Guard(bool &f) : flag(f) { flag = true; }
    ~Guard() { flag = false; }
  } guard(gInFrame);
  try {
    gUiInterp->callFnValue(it->second, {}, 1, 1);
  } catch (const ThrowEscape &) {
  } catch (const std::runtime_error &) {
  }
}

void fbClear(HostWin &win, long long color) {
  ensureFb(win);
  const uint32_t p = packRgb(color);
  std::fill(win.fb.begin(), win.fb.end(), p);
}

void fbFill(HostWin &win, int x, int y, int w, int h, long long color) {
  ensureFb(win);
  if (w < 1 || h < 1)
    return;
  int x0 = x;
  int y0 = y;
  int x1 = x + w;
  int y1 = y + h;
  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 > win.fb_w)
    x1 = win.fb_w;
  if (y1 > win.fb_h)
    y1 = win.fb_h;
  if (x0 >= x1 || y0 >= y1)
    return;
  const uint32_t p = packRgb(color);
  for (int row = y0; row < y1; ++row) {
    uint32_t *dst = win.fb.data() + static_cast<size_t>(row) * win.fb_w + x0;
    for (int col = x0; col < x1; ++col)
      *dst++ = p;
  }
}

const uint8_t kFont8[96][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00},
    {0x6C, 0x6C, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x6C, 0x6C, 0xFE, 0x6C, 0xFE, 0x6C, 0x6C, 0x00},
    {0x18, 0x7E, 0xC0, 0x7C, 0x06, 0xFC, 0x18, 0x00}, {0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6, 0x00, 0x00},
    {0x38, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0x00}, {0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00}, {0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00},
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, {0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30}, {0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00}, {0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x00, 0x00},
    {0x7C, 0xC6, 0xCE, 0xD6, 0xE6, 0xC6, 0x7C, 0x00}, {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00},
    {0x7C, 0xC6, 0x06, 0x1C, 0x70, 0xC0, 0xFE, 0x00}, {0x7C, 0xC6, 0x06, 0x3C, 0x06, 0xC6, 0x7C, 0x00},
    {0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x0C, 0x00}, {0xFE, 0xC0, 0xFC, 0x06, 0x06, 0xC6, 0x7C, 0x00},
    {0x3C, 0x60, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C, 0x00}, {0xFE, 0xC6, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00},
    {0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0x7C, 0x00}, {0x7C, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0x78, 0x00},
    {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00}, {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x30},
    {0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0x00}, {0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00},
    {0x60, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x60, 0x00}, {0x7C, 0xC6, 0x06, 0x1C, 0x18, 0x00, 0x18, 0x00},
    {0x7C, 0xC6, 0xDE, 0xDE, 0xDE, 0xC0, 0x7C, 0x00}, {0x38, 0x6C, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0x00},
    {0xFC, 0xC6, 0xC6, 0xFC, 0xC6, 0xC6, 0xFC, 0x00}, {0x7C, 0xC6, 0xC0, 0xC0, 0xC0, 0xC6, 0x7C, 0x00},
    {0xF8, 0xCC, 0xC6, 0xC6, 0xC6, 0xCC, 0xF8, 0x00}, {0xFE, 0xC0, 0xC0, 0xFC, 0xC0, 0xC0, 0xFE, 0x00},
    {0xFE, 0xC0, 0xC0, 0xFC, 0xC0, 0xC0, 0xC0, 0x00}, {0x7C, 0xC6, 0xC0, 0xCE, 0xC6, 0xC6, 0x7C, 0x00},
    {0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00}, {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00},
    {0x06, 0x06, 0x06, 0x06, 0xC6, 0xC6, 0x7C, 0x00}, {0xC6, 0xCC, 0xD8, 0xF0, 0xD8, 0xCC, 0xC6, 0x00},
    {0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xFE, 0x00}, {0xC6, 0xEE, 0xFE, 0xD6, 0xC6, 0xC6, 0xC6, 0x00},
    {0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0x00}, {0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00},
    {0xFC, 0xC6, 0xC6, 0xFC, 0xC0, 0xC0, 0xC0, 0x00}, {0x7C, 0xC6, 0xC6, 0xC6, 0xD6, 0xCC, 0x76, 0x00},
    {0xFC, 0xC6, 0xC6, 0xFC, 0xD8, 0xCC, 0xC6, 0x00}, {0x7C, 0xC6, 0xC0, 0x7C, 0x06, 0xC6, 0x7C, 0x00},
    {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00}, {0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00},
    {0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00}, {0xC6, 0xC6, 0xC6, 0xD6, 0xFE, 0xEE, 0xC6, 0x00},
    {0xC6, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0xC6, 0x00}, {0xC6, 0xC6, 0x6C, 0x38, 0x18, 0x18, 0x18, 0x00},
    {0xFE, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xFE, 0x00}, {0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00},
    {0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x00, 0x00}, {0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00},
    {0x18, 0x3C, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
    {0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x7C, 0x06, 0x7E, 0xC6, 0x7E, 0x00},
    {0xC0, 0xC0, 0xFC, 0xC6, 0xC6, 0xC6, 0xFC, 0x00}, {0x00, 0x00, 0x7C, 0xC6, 0xC0, 0xC6, 0x7C, 0x00},
    {0x06, 0x06, 0x7E, 0xC6, 0xC6, 0xC6, 0x7E, 0x00}, {0x00, 0x00, 0x7C, 0xC6, 0xFE, 0xC0, 0x7C, 0x00},
    {0x1C, 0x30, 0x30, 0xFC, 0x30, 0x30, 0x30, 0x00}, {0x00, 0x00, 0x7E, 0xC6, 0xC6, 0x7E, 0x06, 0x7C},
    {0xC0, 0xC0, 0xFC, 0xC6, 0xC6, 0xC6, 0xC6, 0x00}, {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00},
    {0x06, 0x00, 0x06, 0x06, 0x06, 0xC6, 0xC6, 0x7C}, {0xC0, 0xC0, 0xCC, 0xD8, 0xF0, 0xD8, 0xCC, 0x00},
    {0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00}, {0x00, 0x00, 0xCC, 0xFE, 0xD6, 0xC6, 0xC6, 0x00},
    {0x00, 0x00, 0xFC, 0xC6, 0xC6, 0xC6, 0xC6, 0x00}, {0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0x7C, 0x00},
    {0x00, 0x00, 0xFC, 0xC6, 0xC6, 0xFC, 0xC0, 0xC0}, {0x00, 0x00, 0x7E, 0xC6, 0xC6, 0x7E, 0x06, 0x06},
    {0x00, 0x00, 0xDC, 0xE6, 0xC0, 0xC0, 0xC0, 0x00}, {0x00, 0x00, 0x7E, 0xC0, 0x7C, 0x06, 0xFC, 0x00},
    {0x30, 0x30, 0xFC, 0x30, 0x30, 0x30, 0x1C, 0x00}, {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0x7E, 0x00},
    {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00}, {0x00, 0x00, 0xC6, 0xC6, 0xD6, 0xFE, 0x6C, 0x00},
    {0x00, 0x00, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0x00}, {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0x7C},
    {0x00, 0x00, 0xFE, 0x0C, 0x18, 0x30, 0xFE, 0x00}, {0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E, 0x00},
    {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00}, {0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70, 0x00},
    {0x76, 0xDC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

void fbTextBitmap(HostWin &win, int x, int y, const std::string &s, long long color);

#ifdef _WIN32
HDC gFontDc = nullptr;
HBITMAP gFontBmp = nullptr;
void *gFontBits = nullptr;
int gFontBw = 0;
int gFontBh = 0;
HFONT gFont = nullptr;
int gSysFontH = 8;
bool gSysFontOk = false;
bool gSysFontTried = false;

std::wstring utf8ToWide(const std::string &s) {
  if (s.empty())
    return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                    static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0)
    return {};
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                      w.data(), n);
  return w;
}

bool ensureSysFont() {
  if (gSysFontTried)
    return gSysFontOk;
  gSysFontTried = true;
  NONCLIENTMETRICSW ncm{};
  ncm.cbSize = sizeof(ncm);
  if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
    ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
    gFont = CreateFontIndirectW(&ncm.lfMessageFont);
  }
  if (!gFont)
    gFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS,
                        L"Segoe UI");
  if (!gFont)
    return false;
  gFontDc = CreateCompatibleDC(nullptr);
  if (!gFontDc)
    return false;
  SelectObject(gFontDc, gFont);
  TEXTMETRICW tm{};
  GetTextMetricsW(gFontDc, &tm);
  gSysFontH = tm.tmHeight > 0 ? static_cast<int>(tm.tmHeight) : 8;
  gSysFontOk = true;
  return true;
}

void ensureFontDib(int w, int h) {
  if (w < 1)
    w = 1;
  if (h < 1)
    h = 1;
  if (gFontBmp && gFontBw >= w && gFontBh >= h)
    return;
  if (gFontBmp) {
    SelectObject(gFontDc, GetStockObject(SYSTEM_FONT));
    DeleteObject(gFontBmp);
    gFontBmp = nullptr;
    gFontBits = nullptr;
  }
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  gFontBmp = CreateDIBSection(gFontDc, &bi, DIB_RGB_COLORS, &gFontBits, nullptr,
                              0);
  gFontBw = w;
  gFontBh = h;
  if (gFontBmp)
    SelectObject(gFontDc, gFontBmp);
  SelectObject(gFontDc, gFont);
}

int sysTextWidth(const std::string &s) {
  if (!ensureSysFont())
    return static_cast<int>(s.size()) * 8;
  const std::wstring w = utf8ToWide(s);
  SIZE sz{};
  if (!GetTextExtentPoint32W(gFontDc, w.c_str(), static_cast<int>(w.size()),
                             &sz))
    return static_cast<int>(s.size()) * 8;
  return sz.cx;
}

int sysFontHeight() {
  if (!ensureSysFont())
    return 8;
  return gSysFontH;
}

bool sysText(HostWin &win, int x, int y, const std::string &s, long long color) {
  if (!ensureSysFont())
    return false;
  const std::wstring w = utf8ToWide(s);
  if (w.empty())
    return true;
  int cy = y;
  size_t start = 0;
  const uint32_t packed = packRgb(color);
  const COLORREF gdiColor =
      RGB(static_cast<BYTE>(color >> 16), static_cast<BYTE>(color >> 8),
          static_cast<BYTE>(color));
  while (start <= w.size()) {
    size_t end = w.find(L'\n', start);
    if (end == std::wstring::npos)
      end = w.size();
    const int nch = static_cast<int>(end - start);
    const wchar_t *line = nch ? w.c_str() + start : L"";
    SIZE sz{};
    if (nch == 0)
      sz.cx = 0;
    else
      GetTextExtentPoint32W(gFontDc, line, nch, &sz);
    sz.cy = gSysFontH;
    if (nch > 0 && sz.cx > 0) {
      ensureFontDib(sz.cx, sz.cy);
      if (!gFontBmp || !gFontBits)
        return false;
      auto *dst = static_cast<uint32_t *>(gFontBits);
      for (int row = 0; row < sz.cy; ++row) {
        const int fy = cy + row;
        for (int col = 0; col < sz.cx; ++col) {
          const int fx = x + col;
          uint32_t p = packed;
          if (fx >= 0 && fy >= 0 && fx < win.fb_w && fy < win.fb_h)
            p = win.fb[static_cast<size_t>(fy) * win.fb_w + fx];
          dst[static_cast<size_t>(row) * gFontBw + col] = p;
        }
      }
      SetBkMode(gFontDc, TRANSPARENT);
      SetTextColor(gFontDc, gdiColor);
      TextOutW(gFontDc, 0, 0, line, nch);
      for (int row = 0; row < sz.cy; ++row) {
        const int fy = cy + row;
        if (fy < 0 || fy >= win.fb_h)
          continue;
        for (int col = 0; col < sz.cx; ++col) {
          const int fx = x + col;
          if (fx < 0 || fx >= win.fb_w)
            continue;
          win.fb[static_cast<size_t>(fy) * win.fb_w + fx] =
              dst[static_cast<size_t>(row) * gFontBw + col];
        }
      }
    }
    if (end == w.size())
      break;
    cy += gSysFontH;
    start = end + 1;
  }
  return true;
}
#else
int sysTextWidth(const std::string &s) {
  int n = 0;
  for (unsigned char ch : s) {
    if (ch != '\n')
      ++n;
  }
  return n * 8;
}

int sysFontHeight() { return 8; }

bool sysText(HostWin &, int, int, const std::string &, long long) {
  return false;
}
#endif

void fbTextBitmap(HostWin &win, int x, int y, const std::string &s, long long color) {
  const uint32_t p = packRgb(color);
  int cx = x;
  int cy = y;
  for (unsigned char ch : s) {
    if (ch == '\n') {
      cx = x;
      cy += 9;
      continue;
    }
    if (ch < 32 || ch > 126)
      ch = '?';
    const uint8_t *glyph = kFont8[ch - 32];
    for (int row = 0; row < 8; ++row) {
      const int py = cy + row;
      if (py < 0 || py >= win.fb_h)
        continue;
      uint8_t bits = glyph[row];
      for (int col = 0; col < 8; ++col) {
        if (bits & 0x80) {
          const int px = cx + col;
          if (px >= 0 && px < win.fb_w)
            win.fb[static_cast<size_t>(py) * win.fb_w + px] = p;
        }
        bits = static_cast<uint8_t>(bits << 1);
      }
    }
    cx += 8;
  }
}

void fbText(HostWin &win, int x, int y, const std::string &s, long long color) {
  ensureFb(win);
  if (sysText(win, x, y, s, color))
    return;
  fbTextBitmap(win, x, y, s, color);
}

void nativePresent(HostWin &win);
void nativeWait();

void feedClick(HostWin &win, int x, int y) {
  win.mouse_x = x;
  win.mouse_y = y;
  win.mouse_down = false;
  win.mouse_click = true;
}

#if defined(__unix__) && !defined(__APPLE__)
enum class DispKind { Off, X11, Wayland };
DispKind gDisp = DispKind::Off;
#endif

#ifdef _WIN32
ATOM gAtom = 0;

std::wstring wideOf(const std::string &s) {
  if (s.empty())
    return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                              static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0)
    return L"";
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                      w.data(), n);
  return w;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  const long long id =
      static_cast<long long>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  HostWin *win = nullptr;
  if (id) {
    auto it = gWins.find(id);
    if (it != gWins.end())
      win = &it->second;
  }
  if (msg == WM_MOUSEMOVE && win) {
    win->mouse_x = static_cast<int>(static_cast<short>(LOWORD(lp)));
    win->mouse_y = static_cast<int>(static_cast<short>(HIWORD(lp)));
    return 0;
  }
  if (msg == WM_LBUTTONDOWN && win) {
    SetCapture(hwnd);
    win->mouse_x = static_cast<int>(static_cast<short>(LOWORD(lp)));
    win->mouse_y = static_cast<int>(static_cast<short>(HIWORD(lp)));
    win->mouse_down = true;
    return 0;
  }
  if (msg == WM_LBUTTONUP && win) {
    ReleaseCapture();
    win->mouse_x = static_cast<int>(static_cast<short>(LOWORD(lp)));
    win->mouse_y = static_cast<int>(static_cast<short>(HIWORD(lp)));
    win->mouse_down = false;
    win->mouse_click = true;
    return 0;
  }
  if (msg == WM_ENTERSIZEMOVE) {
    SetTimer(hwnd, 1, 16, nullptr);
    return 0;
  }
  if (msg == WM_EXITSIZEMOVE) {
    KillTimer(hwnd, 1);
    if (win)
      runFrame(id);
    return 0;
  }
  if (msg == WM_TIMER && wp == 1) {
    if (win) {
      RECT rc{};
      GetClientRect(hwnd, &rc);
      applyClientSize(*win, rc.right, rc.bottom);
      runFrame(id);
    }
    return 0;
  }
  if (msg == WM_SIZE && win) {
    if (wp != SIZE_MINIMIZED) {
      applyClientSize(*win, static_cast<int>(LOWORD(lp)),
                      static_cast<int>(HIWORD(lp)));
      runFrame(id);
    }
    return 0;
  }
  if (msg == WM_ERASEBKGND)
    return 1;
  if (msg == WM_PAINT) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    if (win && !win->fb.empty()) {
      RECT rc{};
      GetClientRect(hwnd, &rc);
      const int cw = rc.right > 0 ? rc.right : win->fb_w;
      const int ch = rc.bottom > 0 ? rc.bottom : win->fb_h;
      BITMAPINFO bi{};
      bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      bi.bmiHeader.biWidth = win->fb_w;
      bi.bmiHeader.biHeight = -win->fb_h;
      bi.bmiHeader.biPlanes = 1;
      bi.bmiHeader.biBitCount = 32;
      bi.bmiHeader.biCompression = BI_RGB;
      StretchDIBits(hdc, 0, 0, cw, ch, 0, 0, win->fb_w, win->fb_h,
                    win->fb.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
    }
    EndPaint(hwnd, &ps);
    return 0;
  }
  if (msg == WM_CLOSE) {
    DestroyWindow(hwnd);
    return 0;
  }
  if (msg == WM_DESTROY) {
    KillTimer(hwnd, 1);
    if (win) {
      win->alive = false;
      win->hwnd = nullptr;
    }
    if (!anyAlive())
      PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(hwnd, msg, wp, lp);
}

void ensureClass() {
  if (gAtom)
    return;
  WNDCLASSW wc{};
  wc.lpfnWndProc = wndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = L"RoseGoldC.Window";
  gAtom = RegisterClassW(&wc);
}

bool nativeReady() { return true; }

bool nativeOpen(HostWin &win, const std::string &title, int w, int h,
                bool visible) {
  (void)visible;
  ensureClass();
  if (!gAtom)
    return false;
  const DWORD style = WS_OVERLAPPEDWINDOW;
  DWORD ex = 0;
  if (!visible)
    ex = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
  RECT rc{0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
  AdjustWindowRectEx(&rc, style, FALSE, ex);
  HWND hwnd = CreateWindowExW(
      ex, L"RoseGoldC.Window", wideOf(title).c_str(), style, CW_USEDEFAULT,
      CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr,
      GetModuleHandleW(nullptr), nullptr);
  if (!hwnd)
    return false;
  win.hwnd = hwnd;
  win.mapped = false;
  return true;
}

void nativeBind(HostWin &win, long long id) {
  if (win.hwnd)
    SetWindowLongPtr(win.hwnd, GWLP_USERDATA, static_cast<LONG_PTR>(id));
}

void nativeShow(HostWin &win) {
  if (!win.hwnd)
    return;
  ShowWindow(win.hwnd, SW_SHOW);
  UpdateWindow(win.hwnd);
  win.mapped = true;
}

void nativeHide(HostWin &win) {
  if (!win.hwnd)
    return;
  ShowWindow(win.hwnd, SW_HIDE);
  win.mapped = false;
}

void nativeSetTitle(HostWin &win) {
  if (win.hwnd)
    SetWindowTextW(win.hwnd, wideOf(win.title).c_str());
}

void nativeSetSize(HostWin &win) {
  if (!win.hwnd)
    return;
  RECT rc{0, 0, static_cast<LONG>(win.width), static_cast<LONG>(win.height)};
  DWORD style = static_cast<DWORD>(GetWindowLongPtr(win.hwnd, GWL_STYLE));
  DWORD ex = static_cast<DWORD>(GetWindowLongPtr(win.hwnd, GWL_EXSTYLE));
  AdjustWindowRectEx(&rc, style, FALSE, ex);
  SetWindowPos(win.hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
               SWP_NOMOVE | SWP_NOZORDER);
}

void nativeClose(HostWin &win) {
  if (!win.hwnd)
    return;
  HWND hwnd = win.hwnd;
  win.hwnd = nullptr;
  DestroyWindow(hwnd);
}

void nativePoll() {
  MSG msg;
  while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT)
      continue;
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}

void nativeRun() {
  while (anyAlive()) {
    MSG msg;
    const BOOL r = GetMessage(&msg, nullptr, 0, 0);
    if (r <= 0)
      break;
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}

void nativePresent(HostWin &win) {
  if (!win.hwnd || win.fb.empty())
    return;
  HDC hdc = GetDC(win.hwnd);
  if (!hdc)
    return;
  RECT rc{};
  GetClientRect(win.hwnd, &rc);
  const int cw = rc.right > 0 ? rc.right : win.fb_w;
  const int ch = rc.bottom > 0 ? rc.bottom : win.fb_h;
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = win.fb_w;
  bi.bmiHeader.biHeight = -win.fb_h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  StretchDIBits(hdc, 0, 0, cw, ch, 0, 0, win.fb_w, win.fb_h, win.fb.data(), &bi,
                DIB_RGB_COLORS, SRCCOPY);
  ReleaseDC(win.hwnd, hdc);
}

void nativeWait() {
  if (!anyAlive())
    return;
  MSG msg;
  const BOOL r = GetMessage(&msg, nullptr, 0, 0);
  if (r <= 0)
    return;
  TranslateMessage(&msg);
  DispatchMessage(&msg);
}

#elif defined(__APPLE__)

id gApp = nil;

void cocoaEnsureApp() {
  if (gApp)
    return;
  gApp = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(
      (id)objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
  reinterpret_cast<void (*)(id, SEL, long)>(objc_msgSend)(
      gApp, sel_registerName("setActivationPolicy:"), 0L);
  reinterpret_cast<void (*)(id, SEL)>(objc_msgSend)(
      gApp, sel_registerName("finishLaunching"));
}

bool nativeReady() {
  cocoaEnsureApp();
  return gApp != nil;
}

bool cocoaVisible(void *nswin) {
  if (!nswin)
    return false;
  return reinterpret_cast<BOOL (*)(id, SEL)>(objc_msgSend)(
             (id)nswin, sel_registerName("isVisible")) != 0;
}

void cocoaReap() {
  for (auto &kv : gWins) {
    HostWin &win = kv.second;
    if (!win.alive || !win.nswin)
      continue;
    if (win.mapped && !cocoaVisible(win.nswin)) {
      win.alive = false;
      win.nswin = nullptr;
      win.mapped = false;
      continue;
    }
    id view = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(
        (id)win.nswin, sel_registerName("contentView"));
    if (!view)
      continue;
    CGRect bounds = reinterpret_cast<CGRect (*)(id, SEL)>(objc_msgSend)(
        view, sel_registerName("bounds"));
    const int ow = win.width;
    const int oh = win.height;
    applyClientSize(win, static_cast<int>(bounds.size.width),
                    static_cast<int>(bounds.size.height));
    if (win.width != ow || win.height != oh)
      runFrame(win.id);
  }
}

bool nativeOpen(HostWin &win, const std::string &title, int w, int h,
                bool visible) {
  (void)visible;
  cocoaEnsureApp();
  if (!gApp)
    return false;
  id cls = (id)objc_getClass("NSWindow");
  id window = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(
      cls, sel_registerName("alloc"));
  CGRect frame = CGRectMake(100, 100, w, h);
  const unsigned long style = 1ul | 2ul | 4ul | 8ul;
  window = reinterpret_cast<id (*)(id, SEL, CGRect, unsigned long, unsigned long,
                                   BOOL)>(objc_msgSend)(
      window, sel_registerName("initWithContentRect:styleMask:backing:defer:"),
      frame, style, 2ul, NO);
  if (!window)
    return false;
  id nsTitle = reinterpret_cast<id (*)(id, SEL, const char *)>(objc_msgSend)(
      (id)objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"),
      title.c_str());
  reinterpret_cast<void (*)(id, SEL, id)>(objc_msgSend)(
      window, sel_registerName("setTitle:"), nsTitle);
  reinterpret_cast<void (*)(id, SEL, BOOL)>(objc_msgSend)(
      window, sel_registerName("setReleasedWhenClosed:"), NO);
  win.nswin = window;
  win.mapped = false;
  return true;
}

void nativeBind(HostWin &, long long) {}

void nativeShow(HostWin &win) {
  if (!win.nswin)
    return;
  reinterpret_cast<void (*)(id, SEL, id)>(objc_msgSend)(
      (id)win.nswin, sel_registerName("makeKeyAndOrderFront:"), nil);
  if (gApp)
    reinterpret_cast<void (*)(id, SEL, BOOL)>(objc_msgSend)(
        gApp, sel_registerName("activateIgnoringOtherApps:"), YES);
  win.mapped = true;
}

void nativeHide(HostWin &win) {
  if (!win.nswin)
    return;
  reinterpret_cast<void (*)(id, SEL, id)>(objc_msgSend)(
      (id)win.nswin, sel_registerName("orderOut:"), nil);
  win.mapped = false;
}

void nativeSetTitle(HostWin &win) {
  if (!win.nswin)
    return;
  id nsTitle = reinterpret_cast<id (*)(id, SEL, const char *)>(objc_msgSend)(
      (id)objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"),
      win.title.c_str());
  reinterpret_cast<void (*)(id, SEL, id)>(objc_msgSend)(
      (id)win.nswin, sel_registerName("setTitle:"), nsTitle);
}

void nativeSetSize(HostWin &win) {
  if (!win.nswin)
    return;
  CGSize size = CGSizeMake(win.width, win.height);
  reinterpret_cast<void (*)(id, SEL, CGSize)>(objc_msgSend)(
      (id)win.nswin, sel_registerName("setContentSize:"), size);
}

void nativeClose(HostWin &win) {
  if (!win.nswin)
    return;
  reinterpret_cast<void (*)(id, SEL)>(objc_msgSend)(
      (id)win.nswin, sel_registerName("close"));
  reinterpret_cast<void (*)(id, SEL)>(objc_msgSend)(
      (id)win.nswin, sel_registerName("release"));
  win.nswin = nullptr;
  win.mapped = false;
}

void nativePoll() {
  if (!gApp)
    return;
  CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true);
  cocoaReap();
}

void nativeRun() {
  if (!gApp)
    return;
  while (anyAlive()) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
    cocoaReap();
  }
}

void nativePresent(HostWin &win) {
  (void)win;
}

void nativeWait() {
  if (!gApp)
    return;
  CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
  cocoaReap();
}

#elif defined(__unix__)

Display *gDpy = nullptr;
Atom gWmDelete = None;

#ifdef ROSEGOLD_WAYLAND
struct xdg_wm_base;
struct xdg_surface;
struct xdg_toplevel;
struct xdg_positioner;
struct xdg_popup;

extern const struct wl_interface xdg_wm_base_interface;
extern const struct wl_interface xdg_positioner_interface;
extern const struct wl_interface xdg_surface_interface;
extern const struct wl_interface xdg_toplevel_interface;
extern const struct wl_interface xdg_popup_interface;

static const struct wl_interface *t_xdg_positioner[] = {
    &xdg_positioner_interface};
static const struct wl_interface *t_xdg_surface_wl_surface[] = {
    &xdg_surface_interface, &wl_surface_interface};
static const struct wl_interface *t_xdg_toplevel[] = {&xdg_toplevel_interface};
static const struct wl_interface *t_xdg_popup_parent_pos[] = {
    &xdg_popup_interface, &xdg_surface_interface, &xdg_positioner_interface};
static const struct wl_interface *t_xdg_toplevel_opt[] = {
    &xdg_toplevel_interface};
static const struct wl_interface *t_wl_seat[] = {&wl_seat_interface};
static const struct wl_interface *t_wl_output[] = {&wl_output_interface};

static const struct wl_message xdg_wm_base_requests[] = {
    {"destroy", "", nullptr},
    {"create_positioner", "n", t_xdg_positioner},
    {"get_xdg_surface", "no", t_xdg_surface_wl_surface},
    {"pong", "u", nullptr},
};
static const struct wl_message xdg_wm_base_events[] = {
    {"ping", "u", nullptr},
};
const struct wl_interface xdg_wm_base_interface = {
    "xdg_wm_base", 1, 4, xdg_wm_base_requests, 1, xdg_wm_base_events};

static const struct wl_message xdg_positioner_requests[] = {
    {"destroy", "", nullptr},
    {"set_size", "ii", nullptr},
    {"set_anchor_rect", "iiii", nullptr},
    {"set_anchor", "u", nullptr},
    {"set_gravity", "u", nullptr},
    {"set_constraint_adjustment", "u", nullptr},
    {"set_offset", "ii", nullptr},
};
const struct wl_interface xdg_positioner_interface = {
    "xdg_positioner", 1, 7, xdg_positioner_requests, 0, nullptr};

static const struct wl_message xdg_surface_requests[] = {
    {"destroy", "", nullptr},
    {"get_toplevel", "n", t_xdg_toplevel},
    {"get_popup", "n?oo", t_xdg_popup_parent_pos},
    {"set_window_geometry", "iiii", nullptr},
    {"ack_configure", "u", nullptr},
};
static const struct wl_message xdg_surface_events[] = {
    {"configure", "u", nullptr},
};
const struct wl_interface xdg_surface_interface = {
    "xdg_surface", 1, 5, xdg_surface_requests, 1, xdg_surface_events};

static const struct wl_message xdg_toplevel_requests[] = {
    {"destroy", "", nullptr},
    {"set_parent", "?o", t_xdg_toplevel_opt},
    {"set_title", "s", nullptr},
    {"set_app_id", "s", nullptr},
    {"show_window_menu", "ouii", t_wl_seat},
    {"move", "ou", t_wl_seat},
    {"resize", "ouu", t_wl_seat},
    {"set_max_size", "ii", nullptr},
    {"set_min_size", "ii", nullptr},
    {"set_maximized", "", nullptr},
    {"unset_maximized", "", nullptr},
    {"set_fullscreen", "?o", t_wl_output},
    {"unset_fullscreen", "", nullptr},
    {"set_minimized", "", nullptr},
};
static const struct wl_message xdg_toplevel_events[] = {
    {"configure", "iia", nullptr},
    {"close", "", nullptr},
};
const struct wl_interface xdg_toplevel_interface = {
    "xdg_toplevel", 1, 14, xdg_toplevel_requests, 2, xdg_toplevel_events};

static const struct wl_message xdg_popup_requests[] = {
    {"destroy", "", nullptr},
    {"grab", "ou", t_wl_seat},
};
static const struct wl_message xdg_popup_events[] = {
    {"configure", "iiii", nullptr},
    {"popup_done", "", nullptr},
};
const struct wl_interface xdg_popup_interface = {
    "xdg_popup", 1, 2, xdg_popup_requests, 2, xdg_popup_events};

struct xdg_wm_base_listener {
  void (*ping)(void *data, xdg_wm_base *xdg_wm_base, uint32_t serial);
};
struct xdg_surface_listener {
  void (*configure)(void *data, xdg_surface *xdg_surface, uint32_t serial);
};
struct xdg_toplevel_listener {
  void (*configure)(void *data, xdg_toplevel *xdg_toplevel, int32_t width,
                    int32_t height, struct wl_array *states);
  void (*close)(void *data, xdg_toplevel *xdg_toplevel);
};

xdg_surface *xdgGetSurface(xdg_wm_base *wm, struct wl_surface *surface) {
  return reinterpret_cast<xdg_surface *>(wl_proxy_marshal_constructor(
      reinterpret_cast<struct wl_proxy *>(wm), 2, &xdg_surface_interface,
      nullptr, surface));
}
xdg_toplevel *xdgGetToplevel(xdg_surface *surface) {
  return reinterpret_cast<xdg_toplevel *>(wl_proxy_marshal_constructor(
      reinterpret_cast<struct wl_proxy *>(surface), 1, &xdg_toplevel_interface,
      nullptr));
}
void xdgPong(xdg_wm_base *wm, uint32_t serial) {
  wl_proxy_marshal(reinterpret_cast<struct wl_proxy *>(wm), 3, serial);
}
void xdgAckConfigure(xdg_surface *surface, uint32_t serial) {
  wl_proxy_marshal(reinterpret_cast<struct wl_proxy *>(surface), 4, serial);
}
void xdgSetTitle(xdg_toplevel *top, const char *title) {
  wl_proxy_marshal(reinterpret_cast<struct wl_proxy *>(top), 2, title);
}
void xdgSetAppId(xdg_toplevel *top, const char *id) {
  wl_proxy_marshal(reinterpret_cast<struct wl_proxy *>(top), 3, id);
}
void xdgDestroyProxy(void *obj) {
  if (!obj)
    return;
  wl_proxy_marshal(reinterpret_cast<struct wl_proxy *>(obj), 0);
  wl_proxy_destroy(reinterpret_cast<struct wl_proxy *>(obj));
}

struct wl_display *gWl = nullptr;
struct wl_registry *gReg = nullptr;
struct wl_compositor *gComp = nullptr;
struct wl_shm *gShm = nullptr;
xdg_wm_base *gXdg = nullptr;

void xdgPing(void *, xdg_wm_base *wm, uint32_t serial) { xdgPong(wm, serial); }
const xdg_wm_base_listener gXdgWmListener = {xdgPing};

void wlAttach(HostWin &win);

void xdgSurfConfigure(void *data, xdg_surface *surface, uint32_t serial) {
  xdgAckConfigure(surface, serial);
  auto *win = static_cast<HostWin *>(data);
  if (win && win->mapped && win->wls)
    wlAttach(*win);
}

void xdgTopConfigure(void *data, xdg_toplevel *, int32_t width, int32_t height,
                     struct wl_array *) {
  auto *win = static_cast<HostWin *>(data);
  if (!win)
    return;
  if (width > 0)
    applyClientSize(*win, width, height > 0 ? height : win->height);
  else if (height > 0)
    applyClientSize(*win, win->width, height);
  runFrame(win->id);
}

void wlDestroy(HostWin &win);

void xdgTopClose(void *data, xdg_toplevel *) {
  auto *win = static_cast<HostWin *>(data);
  if (!win)
    return;
  win->alive = false;
}

const xdg_surface_listener gXdgSurfListener = {xdgSurfConfigure};
const xdg_toplevel_listener gXdgTopListener = {xdgTopConfigure, xdgTopClose};

void registryGlobal(void *, struct wl_registry *reg, uint32_t name,
                    const char *iface, uint32_t ver) {
  if (std::strcmp(iface, wl_compositor_interface.name) == 0) {
    const uint32_t v = ver >= 4 ? 4 : ver;
    gComp = static_cast<struct wl_compositor *>(
        wl_registry_bind(reg, name, &wl_compositor_interface, v));
  } else if (std::strcmp(iface, wl_shm_interface.name) == 0) {
    gShm = static_cast<struct wl_shm *>(
        wl_registry_bind(reg, name, &wl_shm_interface, 1));
  } else if (std::strcmp(iface, xdg_wm_base_interface.name) == 0) {
    const uint32_t v = ver >= 1 ? 1 : ver;
    gXdg = static_cast<xdg_wm_base *>(
        wl_registry_bind(reg, name, &xdg_wm_base_interface, v));
  }
}
void registryRemove(void *, struct wl_registry *, uint32_t) {}
const struct wl_registry_listener gRegListener = {registryGlobal,
                                                  registryRemove};

int shmFd(size_t size) {
#ifdef __NR_memfd_create
  int fd = static_cast<int>(syscall(__NR_memfd_create, "rosegold-wl", 1));
  if (fd >= 0) {
    if (ftruncate(fd, static_cast<off_t>(size)) == 0)
      return fd;
    close(fd);
  }
#endif
  char path[] = "/tmp/rosegold-wl-XXXXXX";
  int fd = mkstemp(path);
  if (fd < 0)
    return -1;
  unlink(path);
  if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

void wlDestroy(HostWin &win) {
  if (win.buf) {
    wl_buffer_destroy(static_cast<struct wl_buffer *>(win.buf));
    win.buf = nullptr;
  }
  if (win.pixels && win.buf_size) {
    munmap(win.pixels, win.buf_size);
    win.pixels = nullptr;
    win.buf_size = 0;
  }
  win.buf_w = 0;
  win.buf_h = 0;
  if (win.xdg_t) {
    xdgDestroyProxy(win.xdg_t);
    win.xdg_t = nullptr;
  }
  if (win.xdg_s) {
    xdgDestroyProxy(win.xdg_s);
    win.xdg_s = nullptr;
  }
  if (win.wls) {
    wl_surface_destroy(static_cast<struct wl_surface *>(win.wls));
    win.wls = nullptr;
  }
  win.mapped = false;
  if (gWl)
    wl_display_flush(gWl);
}

bool wlEnsureBuffer(HostWin &win) {
  const int w = win.width > 0 ? win.width : 1;
  const int h = win.height > 0 ? win.height : 1;
  if (win.buf && win.buf_w == w && win.buf_h == h)
    return true;
  if (win.buf) {
    wl_buffer_destroy(static_cast<struct wl_buffer *>(win.buf));
    win.buf = nullptr;
  }
  if (win.pixels && win.buf_size) {
    munmap(win.pixels, win.buf_size);
    win.pixels = nullptr;
    win.buf_size = 0;
  }
  const int stride = w * 4;
  const size_t size = static_cast<size_t>(stride) * static_cast<size_t>(h);
  const int fd = shmFd(size);
  if (fd < 0)
    return false;
  void *pixels =
      mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (pixels == MAP_FAILED) {
    close(fd);
    return false;
  }
  struct wl_shm_pool *pool =
      wl_shm_create_pool(gShm, fd, static_cast<int32_t>(size));
  close(fd);
  if (!pool) {
    munmap(pixels, size);
    return false;
  }
  struct wl_buffer *buf = wl_shm_pool_create_buffer(
      pool, 0, w, h, stride, WL_SHM_FORMAT_XRGB8888);
  wl_shm_pool_destroy(pool);
  if (!buf) {
    munmap(pixels, size);
    return false;
  }
  auto *px = static_cast<uint32_t *>(pixels);
  const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
  for (size_t i = 0; i < n; ++i)
    px[i] = 0x00F2F2F2u;
  win.pixels = pixels;
  win.buf_size = size;
  win.buf = buf;
  win.buf_w = w;
  win.buf_h = h;
  return true;
}

void wlAttach(HostWin &win) {
  if (!win.wls || !gWl)
    return;
  if (!wlEnsureBuffer(win))
    return;
  auto *surf = static_cast<struct wl_surface *>(win.wls);
  wl_surface_attach(surf, static_cast<struct wl_buffer *>(win.buf), 0, 0);
  wl_surface_damage(surf, 0, 0, win.buf_w, win.buf_h);
  wl_surface_commit(surf);
}

void wlUnmap(HostWin &win) {
  if (!win.wls || !gWl)
    return;
  auto *surf = static_cast<struct wl_surface *>(win.wls);
  wl_surface_attach(surf, nullptr, 0, 0);
  wl_surface_commit(surf);
}

void wlShutdown() {
  if (gXdg) {
    xdgDestroyProxy(gXdg);
    gXdg = nullptr;
  }
  if (gShm) {
    wl_shm_destroy(gShm);
    gShm = nullptr;
  }
  if (gComp) {
    wl_compositor_destroy(gComp);
    gComp = nullptr;
  }
  if (gReg) {
    wl_registry_destroy(gReg);
    gReg = nullptr;
  }
  if (gWl) {
    wl_display_disconnect(gWl);
    gWl = nullptr;
  }
}

bool wlInit() {
  if (gWl)
    return true;
  gWl = wl_display_connect(nullptr);
  if (!gWl)
    return false;
  gReg = wl_display_get_registry(gWl);
  wl_registry_add_listener(gReg, &gRegListener, nullptr);
  wl_display_roundtrip(gWl);
  if (!gComp || !gShm || !gXdg) {
    wlShutdown();
    return false;
  }
  wl_proxy_add_listener(reinterpret_cast<struct wl_proxy *>(gXdg),
                        reinterpret_cast<void (**)(void)>(&gXdgWmListener),
                        nullptr);
  return true;
}

bool envSet(const char *name) {
  const char *v = std::getenv(name);
  return v && v[0];
}

bool preferWayland() {
  if (envSet("WAYLAND_DISPLAY"))
    return true;
  const char *session = std::getenv("XDG_SESSION_TYPE");
  if (session && std::strcmp(session, "wayland") == 0)
    return true;
  if (envSet("DISPLAY"))
    return false;
  return true;
}

bool wlOpen(HostWin &win, const std::string &title, int, int, bool) {
  if (!gComp || !gXdg)
    return false;
  win.mapped = false;
  struct wl_surface *surf = wl_compositor_create_surface(gComp);
  if (!surf)
    return false;
  xdg_surface *xs = xdgGetSurface(gXdg, surf);
  if (!xs) {
    wl_surface_destroy(surf);
    return false;
  }
  xdg_toplevel *top = xdgGetToplevel(xs);
  if (!top) {
    xdgDestroyProxy(xs);
    wl_surface_destroy(surf);
    return false;
  }
  win.wls = surf;
  win.xdg_s = xs;
  win.xdg_t = top;
  wl_proxy_add_listener(reinterpret_cast<struct wl_proxy *>(xs),
                        reinterpret_cast<void (**)(void)>(&gXdgSurfListener),
                        &win);
  wl_proxy_add_listener(reinterpret_cast<struct wl_proxy *>(top),
                        reinterpret_cast<void (**)(void)>(&gXdgTopListener),
                        &win);
  xdgSetTitle(top, title.c_str());
  xdgSetAppId(top, "RoseGoldC");
  wl_surface_commit(surf);
  wl_display_roundtrip(gWl);
  win.mapped = false;
  return true;
}

void wlReap() {
  for (auto &kv : gWins) {
    HostWin &win = kv.second;
    if (!win.alive && win.wls)
      wlDestroy(win);
  }
}

void wlPoll() {
  if (!gWl)
    return;
  while (wl_display_prepare_read(gWl) != 0)
    wl_display_dispatch_pending(gWl);
  wl_display_flush(gWl);
  pollfd pfd{};
  pfd.fd = wl_display_get_fd(gWl);
  pfd.events = POLLIN;
  const int r = poll(&pfd, 1, 0);
  if (r > 0)
    wl_display_read_events(gWl);
  else
    wl_display_cancel_read(gWl);
  wl_display_dispatch_pending(gWl);
  wl_display_flush(gWl);
  wlReap();
}

void wlRun() {
  if (!gWl)
    return;
  while (anyAlive()) {
    if (wl_display_dispatch(gWl) == -1)
      break;
    wlReap();
  }
}
#endif

bool xInit() {
  if (gDpy)
    return true;
  gDpy = XOpenDisplay(nullptr);
  if (!gDpy)
    return false;
  gWmDelete = XInternAtom(gDpy, "WM_DELETE_WINDOW", False);
  return true;
}

bool pickDisplay() {
  if (gDisp == DispKind::Wayland)
#ifdef ROSEGOLD_WAYLAND
    return gWl != nullptr;
#else
    return false;
#endif
  if (gDisp == DispKind::X11)
    return gDpy != nullptr;
#ifdef ROSEGOLD_WAYLAND
  const bool wantWl = preferWayland();
  if (wantWl && wlInit()) {
    gDisp = DispKind::Wayland;
    return true;
  }
  if (xInit()) {
    gDisp = DispKind::X11;
    return true;
  }
  if (!wantWl && wlInit()) {
    gDisp = DispKind::Wayland;
    return true;
  }
  return false;
#else
  if (xInit()) {
    gDisp = DispKind::X11;
    return true;
  }
  return false;
#endif
}

HostWin *findByXid(unsigned long xid) {
  for (auto &kv : gWins) {
    if (kv.second.xid == xid)
      return &kv.second;
  }
  return nullptr;
}

void xHandle(const XEvent &e) {
  if (e.type == MotionNotify) {
    HostWin *win = findByXid(e.xmotion.window);
    if (win) {
      win->mouse_x = e.xmotion.x;
      win->mouse_y = e.xmotion.y;
    }
    return;
  }
  if (e.type == ButtonPress) {
    HostWin *win = findByXid(e.xbutton.window);
    if (win && e.xbutton.button == 1) {
      win->mouse_x = e.xbutton.x;
      win->mouse_y = e.xbutton.y;
      win->mouse_down = true;
    }
    return;
  }
  if (e.type == ButtonRelease) {
    HostWin *win = findByXid(e.xbutton.window);
    if (win && e.xbutton.button == 1) {
      win->mouse_x = e.xbutton.x;
      win->mouse_y = e.xbutton.y;
      win->mouse_down = false;
      win->mouse_click = true;
    }
    return;
  }
  if (e.type == Expose) {
    HostWin *win = findByXid(e.xexpose.window);
    if (win && e.xexpose.count == 0)
      nativePresent(*win);
    return;
  }
  if (e.type == ConfigureNotify) {
    HostWin *win = findByXid(e.xconfigure.window);
    if (win)
      applyClientSize(*win, e.xconfigure.width, e.xconfigure.height);
    if (win)
      runFrame(win->id);
    return;
  }
  if (e.type == ClientMessage) {
    if (gWmDelete != None &&
        static_cast<Atom>(e.xclient.data.l[0]) == gWmDelete) {
      HostWin *win = findByXid(e.xclient.window);
      if (win && win->xid) {
        unsigned long xid = win->xid;
        win->xid = 0;
        win->alive = false;
        win->mapped = false;
        XDestroyWindow(gDpy, xid);
      }
    }
    return;
  }
  if (e.type == DestroyNotify) {
    HostWin *win = findByXid(e.xdestroywindow.window);
    if (win) {
      win->alive = false;
      win->xid = 0;
      win->mapped = false;
    }
  }
}

bool xOpen(HostWin &win, const std::string &title, int w, int h) {
  if (!gDpy)
    return false;
  const int screen = DefaultScreen(gDpy);
  unsigned long xid = XCreateSimpleWindow(
      gDpy, RootWindow(gDpy, screen), 64, 64, static_cast<unsigned>(w),
      static_cast<unsigned>(h), 1, BlackPixel(gDpy, screen),
      WhitePixel(gDpy, screen));
  if (!xid)
    return false;
  XStoreName(gDpy, xid, title.c_str());
  XSelectInput(gDpy, xid,
               StructureNotifyMask | ButtonPressMask | ButtonReleaseMask |
                   PointerMotionMask | ExposureMask);
  if (gWmDelete != None)
    XSetWMProtocols(gDpy, xid, &gWmDelete, 1);
  XSizeHints hints{};
  hints.flags = PSize | PMinSize;
  hints.width = w;
  hints.height = h;
  hints.min_width = 1;
  hints.min_height = 1;
  XSetWMNormalHints(gDpy, xid, &hints);
  win.xid = xid;
  win.mapped = false;
  XFlush(gDpy);
  return true;
}

bool nativeReady() { return pickDisplay(); }

bool nativeOpen(HostWin &win, const std::string &title, int w, int h,
                bool visible) {
  (void)visible;
  if (!pickDisplay())
    return false;
#ifdef ROSEGOLD_WAYLAND
  if (gDisp == DispKind::Wayland)
    return wlOpen(win, title, w, h, visible);
#endif
  return xOpen(win, title, w, h);
}

void nativeBind(HostWin &, long long) {}

void nativeShow(HostWin &win) {
#ifdef ROSEGOLD_WAYLAND
  if (gDisp == DispKind::Wayland) {
    win.mapped = true;
    wlAttach(win);
    if (gWl)
      wl_display_flush(gWl);
    return;
  }
#endif
  if (!gDpy || !win.xid)
    return;
  XMapRaised(gDpy, win.xid);
  XFlush(gDpy);
  win.mapped = true;
}

void nativeHide(HostWin &win) {
#ifdef ROSEGOLD_WAYLAND
  if (gDisp == DispKind::Wayland) {
    win.mapped = false;
    wlUnmap(win);
    if (gWl)
      wl_display_flush(gWl);
    return;
  }
#endif
  if (!gDpy || !win.xid)
    return;
  XUnmapWindow(gDpy, win.xid);
  XFlush(gDpy);
  win.mapped = false;
}

void nativeSetTitle(HostWin &win) {
#ifdef ROSEGOLD_WAYLAND
  if (gDisp == DispKind::Wayland) {
    if (win.xdg_t)
      xdgSetTitle(static_cast<xdg_toplevel *>(win.xdg_t), win.title.c_str());
    if (gWl)
      wl_display_flush(gWl);
    return;
  }
#endif
  if (!gDpy || !win.xid)
    return;
  XStoreName(gDpy, win.xid, win.title.c_str());
  XFlush(gDpy);
}

void nativeSetSize(HostWin &win) {
#ifdef ROSEGOLD_WAYLAND
  if (gDisp == DispKind::Wayland) {
    if (win.mapped)
      wlAttach(win);
    if (gWl)
      wl_display_flush(gWl);
    return;
  }
#endif
  if (!gDpy || !win.xid)
    return;
  XResizeWindow(gDpy, win.xid, static_cast<unsigned>(win.width),
                static_cast<unsigned>(win.height));
  XFlush(gDpy);
}

void nativeClose(HostWin &win) {
#ifdef ROSEGOLD_WAYLAND
  if (gDisp == DispKind::Wayland) {
    wlDestroy(win);
    return;
  }
#endif
  if (!gDpy || !win.xid)
    return;
  unsigned long xid = win.xid;
  win.xid = 0;
  win.mapped = false;
  XDestroyWindow(gDpy, xid);
  XFlush(gDpy);
}

void nativePoll() {
#ifdef ROSEGOLD_WAYLAND
  if (gDisp == DispKind::Wayland) {
    wlPoll();
    return;
  }
#endif
  if (!gDpy)
    return;
  while (XPending(gDpy)) {
    XEvent e;
    XNextEvent(gDpy, &e);
    xHandle(e);
  }
}

void nativeRun() {
#ifdef ROSEGOLD_WAYLAND
  if (gDisp == DispKind::Wayland) {
    wlRun();
    return;
  }
#endif
  if (!gDpy)
    return;
  while (anyAlive()) {
    XEvent e;
    XNextEvent(gDpy, &e);
    xHandle(e);
    nativePoll();
  }
}

void nativePresent(HostWin &win) {
#ifdef ROSEGOLD_WAYLAND
  if (gDisp == DispKind::Wayland) {
    if (!win.wls || !gWl)
      return;
    ensureFb(win);
    if (!wlEnsureBuffer(win))
      return;
    if (win.pixels && win.buf_w == win.fb_w && win.buf_h == win.fb_h &&
        win.fb.size() == static_cast<size_t>(win.fb_w) * win.fb_h)
      std::memcpy(win.pixels, win.fb.data(),
                  win.fb.size() * sizeof(uint32_t));
    auto *surf = static_cast<struct wl_surface *>(win.wls);
    wl_surface_attach(surf, static_cast<struct wl_buffer *>(win.buf), 0, 0);
    wl_surface_damage(surf, 0, 0, win.buf_w, win.buf_h);
    wl_surface_commit(surf);
    wl_display_flush(gWl);
    return;
  }
#endif
  if (!gDpy || !win.xid || win.fb.empty())
    return;
  const int screen = DefaultScreen(gDpy);
  XImage *img = XCreateImage(
      gDpy, DefaultVisual(gDpy, screen), DefaultDepth(gDpy, screen), ZPixmap, 0,
      reinterpret_cast<char *>(win.fb.data()), static_cast<unsigned>(win.fb_w),
      static_cast<unsigned>(win.fb_h), 32, win.fb_w * 4);
  if (!img)
    return;
  XPutImage(gDpy, win.xid, DefaultGC(gDpy, screen), img, 0, 0, 0, 0,
            static_cast<unsigned>(win.fb_w), static_cast<unsigned>(win.fb_h));
  img->data = nullptr;
  XDestroyImage(img);
  XFlush(gDpy);
}

void nativeWait() {
#ifdef ROSEGOLD_WAYLAND
  if (gDisp == DispKind::Wayland) {
    if (gWl)
      wl_display_dispatch(gWl);
    wlReap();
    return;
  }
#endif
  if (!gDpy)
    return;
  XEvent e;
  XNextEvent(gDpy, &e);
  xHandle(e);
  nativePoll();
}

#else

bool nativeReady() { return false; }
bool nativeOpen(HostWin &, const std::string &, int, int, bool) { return false; }
void nativeBind(HostWin &, long long) {}
void nativeShow(HostWin &) {}
void nativeHide(HostWin &) {}
void nativeSetTitle(HostWin &) {}
void nativeSetSize(HostWin &) {}
void nativeClose(HostWin &) {}
void nativePoll() {}
void nativeRun() {}
void nativePresent(HostWin &) {}
void nativeWait() {}

#endif

bool hasNative(const HostWin &win) {
#ifdef _WIN32
  return win.hwnd != nullptr;
#elif defined(__APPLE__)
  return win.nswin != nullptr;
#elif defined(__unix__)
  return win.xid != 0 || win.wls != nullptr;
#else
  (void)win;
  return false;
#endif
}

// Write-once contract: widgets paint pixels; each OS only blits.
// Future hosts: android / ios / web for both platform() and backend().
const char *runtimePlatform() {
#if defined(__EMSCRIPTEN__)
  return "web";
#elif defined(__ANDROID__)
  return "android";
#elif defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
  return "ios";
#else
  return "macos";
#endif
#elif defined(__unix__)
  return "linux";
#else
  return "linux";
#endif
}

const char *runtimeBackend() {
#if defined(__EMSCRIPTEN__)
  return "web";
#elif defined(__ANDROID__)
  return "android";
#elif defined(_WIN32)
  return "win32";
#elif defined(__APPLE__)
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
  return "ios";
#else
  return "cocoa";
#endif
#elif defined(__unix__)
  nativeReady();
  if (gDisp == DispKind::Wayland)
    return "wayland";
  if (gDisp == DispKind::X11)
    return "x11";
  return "none";
#else
  return "none";
#endif
}

} // namespace

void uiHostReset() {
  gFrameFns.clear();
  gUiInterp = nullptr;
  for (auto &kv : gWins) {
    if (hasNative(kv.second))
      nativeClose(kv.second);
    kv.second.alive = false;
  }
  gWins.clear();
  nativePoll();
}

Value uiHostCall(Interpreter &I, const std::string &name,
                 const std::vector<Value> &args, int line, int col) {
  gUiInterp = &I;
  auto needInt = [&](size_t i) {
    if (args[i].kind != Value::Kind::Int)
      I.runtime("__ui." + name + " expects Int", line, col);
    return args[i].i;
  };
  auto needStr = [&](size_t i) {
    if (args[i].kind != Value::Kind::String)
      I.runtime("__ui." + name + " expects String", line, col);
    return args[i].s;
  };
  auto needBool = [&](size_t i) {
    if (args[i].kind != Value::Kind::Bool)
      I.runtime("__ui." + name + " expects Bool", line, col);
    return args[i].b;
  };
  auto failOpen = [&](const std::string &msg) {
    throw ThrowEscape{Value::makeString(msg), line, col};
  };

  if (name == "backend") {
    if (!args.empty())
      I.runtime("__ui.backend takes 0 arguments", line, col);
    return Value::makeString(runtimeBackend());
  }
  if (name == "platform") {
    if (!args.empty())
      I.runtime("__ui.platform takes 0 arguments", line, col);
    return Value::makeString(runtimePlatform());
  }
  if (name == "open") {
    if (args.size() != 4)
      I.runtime("__ui.open takes 4 arguments", line, col);
    std::string title = needStr(0);
    const long long w = needInt(1);
    const long long h = needInt(2);
    const bool visible = needBool(3);
    if (w < 1 || h < 1 || w > 16384 || h > 16384)
      failOpen("window size must be between 1 and 16384");
    if (title.empty())
      title = "RoseGold";
    HostWin win;
    win.alive = true;
    win.width = static_cast<int>(w);
    win.height = static_cast<int>(h);
    win.title = title;
    win.mapped = visible;
    const long long id = gNext++;
    win.id = id;
    gWins[id] = win;
    HostWin &slot = gWins[id];
    const bool ok = nativeOpen(slot, title, static_cast<int>(w),
                               static_cast<int>(h), visible);
    if (!ok) {
      if (visible) {
        gWins.erase(id);
        failOpen("cannot create window");
      }
    } else {
      nativeBind(slot, id);
      if (visible)
        nativeShow(slot);
      else
        nativeHide(slot);
    }
    ensureFb(slot);
    nativePoll();
    return Value::makeInt(id);
  }
  if (name == "close") {
    if (args.size() != 1)
      I.runtime("__ui.close takes 1 argument", line, col);
    const long long id = needInt(0);
    HostWin *win = findAlive(id);
    if (win) {
      gFrameFns.erase(id);
      win->alive = false;
      if (hasNative(*win))
        nativeClose(*win);
      nativePoll();
    }
    return Value::makeVoid();
  }
  if (name == "alive") {
    if (args.size() != 1)
      I.runtime("__ui.alive takes 1 argument", line, col);
    return Value::makeBool(findAlive(needInt(0)) != nullptr);
  }
  if (name == "show") {
    if (args.size() != 1)
      I.runtime("__ui.show takes 1 argument", line, col);
    HostWin *win = findAlive(needInt(0));
    if (win) {
      win->mapped = true;
      nativeShow(*win);
    }
    return Value::makeVoid();
  }
  if (name == "hide") {
    if (args.size() != 1)
      I.runtime("__ui.hide takes 1 argument", line, col);
    HostWin *win = findAlive(needInt(0));
    if (win) {
      win->mapped = false;
      nativeHide(*win);
    }
    return Value::makeVoid();
  }
  if (name == "title") {
    if (args.size() != 1)
      I.runtime("__ui.title takes 1 argument", line, col);
    HostWin *win = findAlive(needInt(0));
    return Value::makeString(win ? win->title : "");
  }
  if (name == "set_title") {
    if (args.size() != 2)
      I.runtime("__ui.set_title takes 2 arguments", line, col);
    HostWin *win = findAlive(needInt(0));
    const std::string title = needStr(1);
    if (win) {
      win->title = title.empty() ? "RoseGold" : title;
      nativeSetTitle(*win);
    }
    return Value::makeVoid();
  }
  if (name == "width") {
    if (args.size() != 1)
      I.runtime("__ui.width takes 1 argument", line, col);
    HostWin *win = findAlive(needInt(0));
    return Value::makeInt(win ? win->width : 0);
  }
  if (name == "height") {
    if (args.size() != 1)
      I.runtime("__ui.height takes 1 argument", line, col);
    HostWin *win = findAlive(needInt(0));
    return Value::makeInt(win ? win->height : 0);
  }
  if (name == "set_size") {
    if (args.size() != 3)
      I.runtime("__ui.set_size takes 3 arguments", line, col);
    HostWin *win = findAlive(needInt(0));
    const long long w = needInt(1);
    const long long h = needInt(2);
    if (w < 1 || h < 1 || w > 16384 || h > 16384)
      I.runtime("window size must be between 1 and 16384", line, col);
    if (win) {
      win->width = static_cast<int>(w);
      win->height = static_cast<int>(h);
      nativeSetSize(*win);
      ensureFb(*win);
    }
    return Value::makeVoid();
  }
  if (name == "clear") {
    if (args.size() != 2)
      I.runtime("__ui.clear takes 2 arguments", line, col);
    HostWin *win = findAlive(needInt(0));
    if (win)
      fbClear(*win, needInt(1));
    return Value::makeVoid();
  }
  if (name == "fill") {
    if (args.size() != 6)
      I.runtime("__ui.fill takes 6 arguments", line, col);
    HostWin *win = findAlive(needInt(0));
    if (win)
      fbFill(*win, static_cast<int>(needInt(1)), static_cast<int>(needInt(2)),
             static_cast<int>(needInt(3)), static_cast<int>(needInt(4)),
             needInt(5));
    return Value::makeVoid();
  }
  if (name == "text") {
    if (args.size() != 5)
      I.runtime("__ui.text takes 5 arguments", line, col);
    HostWin *win = findAlive(needInt(0));
    if (win)
      fbText(*win, static_cast<int>(needInt(1)), static_cast<int>(needInt(2)),
             needStr(3), needInt(4));
    return Value::makeVoid();
  }
  if (name == "text_width") {
    if (args.size() != 1)
      I.runtime("__ui.text_width takes 1 argument", line, col);
    return Value::makeInt(sysTextWidth(needStr(0)));
  }
  if (name == "font_height") {
    if (!args.empty())
      I.runtime("__ui.font_height takes 0 arguments", line, col);
    return Value::makeInt(sysFontHeight());
  }
  if (name == "present") {
    if (args.size() != 1)
      I.runtime("__ui.present takes 1 argument", line, col);
    HostWin *win = findAlive(needInt(0));
    if (win)
      nativePresent(*win);
    return Value::makeVoid();
  }
  if (name == "mouse_x") {
    if (args.size() != 1)
      I.runtime("__ui.mouse_x takes 1 argument", line, col);
    HostWin *win = findAlive(needInt(0));
    return Value::makeInt(win ? win->mouse_x : 0);
  }
  if (name == "mouse_y") {
    if (args.size() != 1)
      I.runtime("__ui.mouse_y takes 1 argument", line, col);
    HostWin *win = findAlive(needInt(0));
    return Value::makeInt(win ? win->mouse_y : 0);
  }
  if (name == "mouse_down") {
    if (args.size() != 1)
      I.runtime("__ui.mouse_down takes 1 argument", line, col);
    HostWin *win = findAlive(needInt(0));
    return Value::makeBool(win && win->mouse_down);
  }
  if (name == "take_click") {
    if (args.size() != 1)
      I.runtime("__ui.take_click takes 1 argument", line, col);
    HostWin *win = findAlive(needInt(0));
    if (!win)
      return Value::makeBool(false);
    const bool click = win->mouse_click;
    win->mouse_click = false;
    return Value::makeBool(click);
  }
  if (name == "feed_click") {
    if (args.size() != 3)
      I.runtime("__ui.feed_click takes 3 arguments", line, col);
    HostWin *win = findAlive(needInt(0));
    if (win)
      feedClick(*win, static_cast<int>(needInt(1)),
                static_cast<int>(needInt(2)));
    return Value::makeVoid();
  }
  if (name == "set_frame") {
    if (args.size() != 2)
      I.runtime("__ui.set_frame takes 2 arguments", line, col);
    const long long id = needInt(0);
    if (args[1].kind != Value::Kind::FnRef)
      I.runtime("__ui.set_frame expects a function", line, col);
    if (findAlive(id))
      gFrameFns[id] = args[1];
    return Value::makeVoid();
  }
  if (name == "wait") {
    if (!args.empty())
      I.runtime("__ui.wait takes 0 arguments", line, col);
    if (nativeReady())
      nativeWait();
    return Value::makeVoid();
  }
  if (name == "poll") {
    if (args.size() != 1)
      I.runtime("__ui.poll takes 1 argument", line, col);
    const long long id = needInt(0);
    nativePoll();
    return Value::makeBool(findAlive(id) != nullptr);
  }
  if (name == "run") {
    if (!args.empty())
      I.runtime("__ui.run takes 0 arguments", line, col);
    if (nativeReady())
      nativeRun();
    nativePoll();
    return Value::makeVoid();
  }
  if (name == "count") {
    if (!args.empty())
      I.runtime("__ui.count takes 0 arguments", line, col);
    long long n = 0;
    for (const auto &kv : gWins) {
      if (kv.second.alive)
        ++n;
    }
    return Value::makeInt(n);
  }
  I.runtime("unknown function __ui." + name, line, col);
  return Value::makeVoid();
}
