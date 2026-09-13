#include "interp.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#include "stb_image.h"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
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
#include <CoreGraphics/CoreGraphics.h>
#include <TargetConditionals.h>
#include <objc/message.h>
#include <objc/objc.h>
#include <objc/runtime.h>
#elif defined(__unix__)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <cstdlib>
#include <cstring>
#ifdef ROSEGOLD_WAYLAND
#include <poll.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/input-event-codes.h>
extern "C" {
#include <wayland-client.h>
}
#ifdef ROSEGOLD_XKB
#include <xkbcommon/xkbcommon.h>
#endif
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
  bool mouse_right_click = false;
  int cursor_kind = 0; // 0 arrow, 1 hand, 2 ibeam
  bool key_pending = false;
  int key_code = 0;
  std::string key_text;
  bool scroll_pending = false;
  int scroll_dx = 0;
  int scroll_dy = 0;
  int wheel_acc = 0; // residual WM_MOUSEWHEEL delta for smooth trackpads
  struct ClipRect {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
  };
  std::vector<ClipRect> clip_stack;
#if defined(__APPLE__)
  void *nsview = nullptr;
#endif
};

std::map<long long, HostWin> gWins;
long long gNext = 1;
Interpreter *gUiInterp = nullptr;
std::map<long long, Value> gFrameFns;
bool gInFrame = false;
std::string gClipboard;

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

void fbPlot(HostWin &win, int x, int y, uint32_t p);

void fbPlotCover(HostWin &win, int x, int y, uint32_t src, int cover) {
  if (cover <= 0)
    return;
  if (x < 0 || y < 0 || x >= win.fb_w || y >= win.fb_h)
    return;
  if (!win.clip_stack.empty()) {
    const HostWin::ClipRect &c = win.clip_stack.back();
    if (x < c.x0 || y < c.y0 || x >= c.x1 || y >= c.y1)
      return;
  }
  if (cover >= 256) {
    win.fb[static_cast<size_t>(y) * win.fb_w + x] = src;
    return;
  }
  uint32_t &dst = win.fb[static_cast<size_t>(y) * win.fb_w + x];
  const int inv = 256 - cover;
  const int dr = (dst >> 16) & 255;
  const int dg = (dst >> 8) & 255;
  const int db = dst & 255;
  const int sr = (src >> 16) & 255;
  const int sg = (src >> 8) & 255;
  const int sb = src & 255;
  const int r = (sr * cover + dr * inv) >> 8;
  const int g = (sg * cover + dg * inv) >> 8;
  const int b = (sb * cover + db * inv) >> 8;
  dst = static_cast<uint32_t>(b) | (static_cast<uint32_t>(g) << 8) |
        (static_cast<uint32_t>(r) << 16);
}

float sdRoundBox(float px, float py, float hw, float hh, float radius) {
  float r = radius;
  if (r < 0.f)
    r = 0.f;
  float maxR = hw < hh ? hw : hh;
  if (r > maxR)
    r = maxR;
  const float ax = px < 0.f ? -px : px;
  const float ay = py < 0.f ? -py : py;
  float qx = ax - hw + r;
  float qy = ay - hh + r;
  const float mx = qx > 0.f ? qx : 0.f;
  const float my = qy > 0.f ? qy : 0.f;
  const float outside = sqrtf(mx * mx + my * my);
  float inside = qx > qy ? qx : qy;
  if (inside > 0.f)
    inside = 0.f;
  return outside + inside - r;
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

void clipBounds(const HostWin &win, int &x0, int &y0, int &x1, int &y1) {
  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 > win.fb_w)
    x1 = win.fb_w;
  if (y1 > win.fb_h)
    y1 = win.fb_h;
  if (!win.clip_stack.empty()) {
    const HostWin::ClipRect &c = win.clip_stack.back();
    if (x0 < c.x0)
      x0 = c.x0;
    if (y0 < c.y0)
      y0 = c.y0;
    if (x1 > c.x1)
      x1 = c.x1;
    if (y1 > c.y1)
      y1 = c.y1;
  }
}

void fbFill(HostWin &win, int x, int y, int w, int h, long long color) {
  ensureFb(win);
  if (w < 1 || h < 1)
    return;
  int x0 = x;
  int y0 = y;
  int x1 = x + w;
  int y1 = y + h;
  clipBounds(win, x0, y0, x1, y1);
  if (x0 >= x1 || y0 >= y1)
    return;
  const uint32_t p = packRgb(color);
  for (int row = y0; row < y1; ++row) {
    uint32_t *dst = win.fb.data() + static_cast<size_t>(row) * win.fb_w + x0;
    for (int col = x0; col < x1; ++col)
      *dst++ = p;
  }
}

void fbPlot(HostWin &win, int x, int y, uint32_t p) {
  if (x < 0 || y < 0 || x >= win.fb_w || y >= win.fb_h)
    return;
  if (!win.clip_stack.empty()) {
    const HostWin::ClipRect &c = win.clip_stack.back();
    if (x < c.x0 || y < c.y0 || x >= c.x1 || y >= c.y1)
      return;
  }
  win.fb[static_cast<size_t>(y) * win.fb_w + x] = p;
}

void fbLine(HostWin &win, int x0, int y0, int x1, int y1, long long color) {
  ensureFb(win);
  const uint32_t p = packRgb(color);
  int dx = x1 - x0;
  int dy = y1 - y0;
  const int absDx = dx < 0 ? -dx : dx;
  const int absDy = dy < 0 ? -dy : dy;
  const int steps = absDx > absDy ? absDx : absDy;
  if (steps == 0) {
    fbPlot(win, x0, y0, p);
    return;
  }
  for (int i = 0; i <= steps; ++i) {
    const int x = x0 + dx * i / steps;
    const int y = y0 + dy * i / steps;
    fbPlot(win, x, y, p);
  }
}

void fbStrokeRect(HostWin &win, int x, int y, int w, int h, long long color) {
  if (w < 1 || h < 1)
    return;
  fbFill(win, x, y, w, 1, color);
  fbFill(win, x, y + h - 1, w, 1, color);
  fbFill(win, x, y, 1, h, color);
  fbFill(win, x + w - 1, y, 1, h, color);
}

int clampRoundRadius(int w, int h, int radius) {
  int r = radius;
  if (r < 0)
    r = 0;
  const int lim = (w < h ? w : h) / 2;
  if (r > lim)
    r = lim;
  return r;
}

void fbFillRound(HostWin &win, int x, int y, int w, int h, int radius,
                 long long color) {
  ensureFb(win);
  if (w < 1 || h < 1)
    return;
  const int r = clampRoundRadius(w, h, radius);
  if (r == 0) {
    fbFill(win, x, y, w, h, color);
    return;
  }
  const uint32_t p = packRgb(color);
  const float cx = static_cast<float>(x) + static_cast<float>(w) * 0.5f;
  const float cy = static_cast<float>(y) + static_cast<float>(h) * 0.5f;
  const float hw = static_cast<float>(w) * 0.5f;
  const float hh = static_cast<float>(h) * 0.5f;
  const float rf = static_cast<float>(r);
  int x0 = x;
  int y0 = y;
  int x1 = x + w;
  int y1 = y + h;
  clipBounds(win, x0, y0, x1, y1);
  for (int row = y0; row < y1; ++row) {
    for (int col = x0; col < x1; ++col) {
      const float sd =
          sdRoundBox(static_cast<float>(col) + 0.5f - cx,
                     static_cast<float>(row) + 0.5f - cy, hw, hh, rf);
      float cov = 0.5f - sd;
      if (cov <= 0.f)
        continue;
      if (cov >= 1.f)
        fbPlot(win, col, row, p);
      else
        fbPlotCover(win, col, row, p, static_cast<int>(cov * 256.f));
    }
  }
}

void fbStrokeRound(HostWin &win, int x, int y, int w, int h, int radius,
                   long long color) {
  ensureFb(win);
  if (w < 1 || h < 1)
    return;
  const int r = clampRoundRadius(w, h, radius);
  if (r == 0) {
    fbStrokeRect(win, x, y, w, h, color);
    return;
  }
  const uint32_t p = packRgb(color);
  const float cx = static_cast<float>(x) + static_cast<float>(w) * 0.5f;
  const float cy = static_cast<float>(y) + static_cast<float>(h) * 0.5f;
  // Inset half a pixel so AA stays inside the widget hit box.
  const float hw = static_cast<float>(w) * 0.5f - 0.5f;
  const float hh = static_cast<float>(h) * 0.5f - 0.5f;
  if (hw < 1.f || hh < 1.f) {
    fbStrokeRect(win, x, y, w, h, color);
    return;
  }
  float rf = static_cast<float>(r) - 0.5f;
  if (rf < 0.f)
    rf = 0.f;
  int x0 = x;
  int y0 = y;
  int x1 = x + w;
  int y1 = y + h;
  clipBounds(win, x0, y0, x1, y1);
  for (int row = y0; row < y1; ++row) {
    for (int col = x0; col < x1; ++col) {
      const float sd =
          sdRoundBox(static_cast<float>(col) + 0.5f - cx,
                     static_cast<float>(row) + 0.5f - cy, hw, hh, rf);
      const float d = sd < 0.f ? -sd : sd;
      float cov = 1.f - (d - 0.25f) / 0.75f;
      if (cov <= 0.f)
        continue;
      if (cov >= 1.f)
        fbPlot(win, col, row, p);
      else
        fbPlotCover(win, col, row, p, static_cast<int>(cov * 256.f));
    }
  }
}

struct RgbImage {
  int w = 0;
  int h = 0;
  std::vector<uint32_t> px;
};

std::map<std::string, RgbImage> gImages;

void fbBlitRgb(HostWin &win, int x, int y, int iw, int ih,
               const uint32_t *src) {
  ensureFb(win);
  if (!src || iw < 1 || ih < 1)
    return;
  for (int row = 0; row < ih; ++row) {
    for (int col = 0; col < iw; ++col)
      fbPlot(win, x + col, y + row, src[static_cast<size_t>(row) * iw + col]);
  }
}

bool loadPpmFile(const std::string &path, RgbImage &out) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return false;
  std::string magic;
  in >> magic;
  if (magic != "P6" && magic != "P3")
    return false;
  auto skipComments = [&]() {
    while (in) {
      const int c = in.peek();
      if (c == '#') {
        std::string line;
        std::getline(in, line);
        continue;
      }
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        in.get();
        continue;
      }
      break;
    }
  };
  skipComments();
  int w = 0;
  int h = 0;
  int maxv = 0;
  in >> w >> h;
  skipComments();
  in >> maxv;
  if (!in || w < 1 || h < 1 || maxv < 1 || w > 8192 || h > 8192)
    return false;
  if (magic == "P6") {
    in.get(); // single whitespace after maxval
    std::vector<unsigned char> raw(static_cast<size_t>(w) * h * 3);
    in.read(reinterpret_cast<char *>(raw.data()),
            static_cast<std::streamsize>(raw.size()));
    if (!in)
      return false;
    out.w = w;
    out.h = h;
    out.px.resize(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < out.px.size(); ++i) {
      const unsigned r = raw[i * 3];
      const unsigned g = raw[i * 3 + 1];
      const unsigned b = raw[i * 3 + 2];
      const long long color =
          (static_cast<long long>(r) << 16) | (static_cast<long long>(g) << 8) | b;
      out.px[i] = packRgb(color);
    }
    return true;
  }
  out.w = w;
  out.h = h;
  out.px.resize(static_cast<size_t>(w) * h);
  for (size_t i = 0; i < out.px.size(); ++i) {
    int r = 0;
    int g = 0;
    int b = 0;
    in >> r >> g >> b;
    if (!in)
      return false;
    const long long color = (static_cast<long long>(r & 255) << 16) |
                            (static_cast<long long>(g & 255) << 8) | (b & 255);
    out.px[i] = packRgb(color);
  }
  return true;
}

bool loadStbImageFile(const std::string &path, RgbImage &out) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return false;
  in.seekg(0, std::ios::end);
  const std::streamoff sz = in.tellg();
  if (sz <= 0 || sz > 32 * 1024 * 1024)
    return false;
  in.seekg(0, std::ios::beg);
  std::vector<unsigned char> file(static_cast<size_t>(sz));
  in.read(reinterpret_cast<char *>(file.data()), sz);
  if (!in)
    return false;
  int w = 0;
  int h = 0;
  int comp = 0;
  unsigned char *data =
      stbi_load_from_memory(file.data(), static_cast<int>(file.size()), &w, &h,
                            &comp, 3);
  if (!data || w < 1 || h < 1 || w > 8192 || h > 8192) {
    if (data)
      stbi_image_free(data);
    return false;
  }
  out.w = w;
  out.h = h;
  out.px.resize(static_cast<size_t>(w) * h);
  for (size_t i = 0; i < out.px.size(); ++i) {
    const unsigned r = data[i * 3];
    const unsigned g = data[i * 3 + 1];
    const unsigned b = data[i * 3 + 2];
    const long long color =
        (static_cast<long long>(r) << 16) | (static_cast<long long>(g) << 8) | b;
    out.px[i] = packRgb(color);
  }
  stbi_image_free(data);
  return true;
}

bool loadSvgFile(const std::string &path, RgbImage &out) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return false;
  in.seekg(0, std::ios::end);
  const std::streamoff sz = in.tellg();
  if (sz <= 0 || sz > 32 * 1024 * 1024)
    return false;
  in.seekg(0, std::ios::beg);
  std::vector<char> file(static_cast<size_t>(sz) + 1);
  in.read(file.data(), sz);
  if (!in)
    return false;
  file[static_cast<size_t>(sz)] = '\0';
  NSVGimage *svg = nsvgParse(file.data(), "px", 96.0f);
  if (!svg)
    return false;
  int w = static_cast<int>(svg->width + 0.5f);
  int h = static_cast<int>(svg->height + 0.5f);
  if (w < 1)
    w = 1;
  if (h < 1)
    h = 1;
  if (w > 8192 || h > 8192) {
    nsvgDelete(svg);
    return false;
  }
  NSVGrasterizer *rast = nsvgCreateRasterizer();
  if (!rast) {
    nsvgDelete(svg);
    return false;
  }
  std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
  nsvgRasterize(rast, svg, 0, 0, 1.0f, rgba.data(), w, h, w * 4);
  nsvgDeleteRasterizer(rast);
  nsvgDelete(svg);
  out.w = w;
  out.h = h;
  out.px.resize(static_cast<size_t>(w) * h);
  for (size_t i = 0; i < out.px.size(); ++i) {
    const unsigned char *p = &rgba[i * 4];
    const unsigned a = p[3];
    const unsigned r = (p[0] * a + 255 * (255 - a)) / 255;
    const unsigned g = (p[1] * a + 255 * (255 - a)) / 255;
    const unsigned b = (p[2] * a + 255 * (255 - a)) / 255;
    const long long color =
        (static_cast<long long>(r) << 16) | (static_cast<long long>(g) << 8) | b;
    out.px[i] = packRgb(color);
  }
  return true;
}

const RgbImage *cachedImage(const std::string &path) {
  auto it = gImages.find(path);
  if (it != gImages.end())
    return &it->second;
  RgbImage img;
  const bool ok = loadPpmFile(path, img) || loadStbImageFile(path, img) ||
                  loadSvgFile(path, img);
  if (!ok)
    return nullptr;
  auto &slot = gImages[path];
  slot = std::move(img);
  return &slot;
}

void feedKey(HostWin &win, int code, std::string text) {
  win.key_code = code;
  win.key_text = std::move(text);
  win.key_pending = true;
}

void feedScroll(HostWin &win, int dx, int dy) {
  if (!win.scroll_pending) {
    win.scroll_dx = 0;
    win.scroll_dy = 0;
  }
  win.scroll_dx += dx;
  win.scroll_dy += dy;
  win.scroll_pending = true;
}

// Wheel delta units: 120 per notch (Windows WHEEL_DELTA). Keep a residual so
// precision trackpads that send small deltas still produce smooth pixel steps.
int wheelDeltaToPixels(HostWin &win, int delta) {
  constexpr int kWheelUnit = 120;
  constexpr int kPixelsPerNotch = 48;
  win.wheel_acc += delta;
  const int px = win.wheel_acc * kPixelsPerNotch / kWheelUnit;
  if (px != 0)
    win.wheel_acc -= px * kWheelUnit / kPixelsPerNotch;
  return px;
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
int gFontDpi = 0;
bool gSysFontOk = false;
bool gDpiAwareTried = false;
HCURSOR gArrowCursor = nullptr;
HCURSOR gHandCursor = nullptr;
HCURSOR gIBeamCursor = nullptr;

void ensureCursors() {
  if (!gArrowCursor)
    gArrowCursor = LoadCursor(nullptr, IDC_ARROW);
  if (!gHandCursor)
    gHandCursor = LoadCursor(nullptr, IDC_HAND);
  if (!gIBeamCursor)
    gIBeamCursor = LoadCursor(nullptr, IDC_IBEAM);
}

HCURSOR cursorHandle(int kind) {
  ensureCursors();
  if (kind == 1 && gHandCursor)
    return gHandCursor;
  if (kind == 2 && gIBeamCursor)
    return gIBeamCursor;
  return gArrowCursor;
}

void applyCursor(HostWin &win) {
  HCURSOR cur = cursorHandle(win.cursor_kind);
  if (cur)
    SetCursor(cur);
}

void enableDpiAwareness() {
  if (gDpiAwareTried)
    return;
  gDpiAwareTried = true;
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  if (user32) {
    using SetCtxFn = BOOL(WINAPI *)(void *);
    auto setCtx = reinterpret_cast<SetCtxFn>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setCtx) {
      // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (DPI_AWARENESS_CONTEXT)-4
      if (setCtx(reinterpret_cast<void *>(static_cast<INT_PTR>(-4))))
        return;
    }
    using SetAwareFn = BOOL(WINAPI *)(void);
    auto setAware = reinterpret_cast<SetAwareFn>(
        GetProcAddress(user32, "SetProcessDPIAware"));
    if (setAware)
      setAware();
  }
}

int queryDpi(HWND hwnd) {
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  if (user32 && hwnd) {
    using GetDpiWinFn = UINT(WINAPI *)(HWND);
    auto getWin = reinterpret_cast<GetDpiWinFn>(
        GetProcAddress(user32, "GetDpiForWindow"));
    if (getWin) {
      const UINT d = getWin(hwnd);
      if (d >= 72)
        return static_cast<int>(d);
    }
  }
  if (user32) {
    using GetDpiSysFn = UINT(WINAPI *)(void);
    auto getSys = reinterpret_cast<GetDpiSysFn>(
        GetProcAddress(user32, "GetDpiForSystem"));
    if (getSys) {
      const UINT d = getSys();
      if (d >= 72)
        return static_cast<int>(d);
    }
  }
  HDC screen = GetDC(nullptr);
  if (screen) {
    const int d = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);
    if (d >= 72)
      return d;
  }
  return 96;
}

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

void clearFontDib() {
  if (!gFontBmp)
    return;
  if (gFontDc)
    SelectObject(gFontDc, GetStockObject(SYSTEM_FONT));
  DeleteObject(gFontBmp);
  gFontBmp = nullptr;
  gFontBits = nullptr;
  gFontBw = 0;
  gFontBh = 0;
}

bool ensureSysFont(int dpi) {
  if (dpi < 72)
    dpi = 96;
  if (gSysFontOk && gFont && gFontDpi == dpi)
    return true;

  clearFontDib();
  if (gFont) {
    if (gFontDc)
      SelectObject(gFontDc, GetStockObject(SYSTEM_FONT));
    DeleteObject(gFont);
    gFont = nullptr;
  }
  if (!gFontDc) {
    gFontDc = CreateCompatibleDC(nullptr);
    if (!gFontDc) {
      gSysFontOk = false;
      return false;
    }
  }

  const int px = -MulDiv(13, dpi, 96);
  NONCLIENTMETRICSW ncm{};
  ncm.cbSize = sizeof(ncm);
  if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
    LOGFONTW lf = ncm.lfMessageFont;
    lf.lfHeight = px;
    lf.lfQuality = CLEARTYPE_QUALITY;
    gFont = CreateFontIndirectW(&lf);
  }
  if (!gFont)
    gFont = CreateFontW(px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS,
                        L"Segoe UI");
  if (!gFont) {
    gSysFontOk = false;
    gFontDpi = 0;
    return false;
  }
  SelectObject(gFontDc, gFont);
  TEXTMETRICW tm{};
  GetTextMetricsW(gFontDc, &tm);
  gSysFontH = tm.tmHeight > 0 ? static_cast<int>(tm.tmHeight) : 8;
  gFontDpi = dpi;
  gSysFontOk = true;
  return true;
}

bool ensureSysFont() { return ensureSysFont(queryDpi(nullptr)); }

bool ensureSysFontFor(HostWin &win) {
  return ensureSysFont(queryDpi(win.hwnd));
}

void ensureFontDib(int w, int h) {
  if (w < 1)
    w = 1;
  if (h < 1)
    h = 1;
  if (gFontBmp && gFontBw >= w && gFontBh >= h)
    return;
  clearFontDib();
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
    return static_cast<int>(s.size()) * 16;
  const std::wstring w = utf8ToWide(s);
  SIZE sz{};
  if (!GetTextExtentPoint32W(gFontDc, w.c_str(), static_cast<int>(w.size()),
                             &sz))
    return static_cast<int>(s.size()) * 16;
  return sz.cx;
}

int sysFontHeight() {
  if (!ensureSysFont())
    return 16;
  return gSysFontH;
}

bool sysText(HostWin &win, int x, int y, const std::string &s, long long color) {
  if (!ensureSysFontFor(win))
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
          fbPlot(win, fx, fy, dst[static_cast<size_t>(row) * gFontBw + col]);
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

void blitFbToDc(HDC hdc, HostWin &win, int cw, int ch) {
  if (win.fb.empty() || win.fb_w < 1 || win.fb_h < 1)
    return;
  SetStretchBltMode(hdc, HALFTONE);
  SetBrushOrgEx(hdc, 0, 0, nullptr);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = win.fb_w;
  bi.bmiHeader.biHeight = -win.fb_h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  StretchDIBits(hdc, 0, 0, cw, ch, 0, 0, win.fb_w, win.fb_h, win.fb.data(), &bi,
                DIB_RGB_COLORS, SRCCOPY);
  // HALFTONE StretchDIBits can clear the cursor; restore for client area.
  applyCursor(win);
}
#else
int sysTextWidth(const std::string &s) {
  int n = 0;
  for (unsigned char ch : s) {
    if (ch != '\n')
      ++n;
  }
  return n * 16;
}

int sysFontHeight() { return 16; }

bool sysText(HostWin &, int, int, const std::string &, long long) {
  return false;
}
#endif

void fbTextBitmap(HostWin &win, int x, int y, const std::string &s, long long color) {
  const uint32_t p = packRgb(color);
  const int scale = 2;
  const int cell = 8 * scale;
  int cx = x;
  int cy = y;
  for (unsigned char ch : s) {
    if (ch == '\n') {
      cx = x;
      cy += cell + scale;
      continue;
    }
    if (ch < 32 || ch > 126)
      ch = '?';
    const uint8_t *glyph = kFont8[ch - 32];
    for (int row = 0; row < 8; ++row) {
      uint8_t bits = glyph[row];
      for (int col = 0; col < 8; ++col) {
        if (bits & 0x80) {
          for (int dy = 0; dy < scale; ++dy) {
            for (int dx = 0; dx < scale; ++dx)
              fbPlot(win, cx + col * scale + dx, cy + row * scale + dy, p);
          }
        }
        bits = static_cast<uint8_t>(bits << 1);
      }
    }
    cx += cell;
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

void feedRightClick(HostWin &win, int x, int y) {
  win.mouse_x = x;
  win.mouse_y = y;
  win.mouse_down = false;
  win.mouse_right_click = true;
}

void feedMouse(HostWin &win, int x, int y) {
  win.mouse_x = x;
  win.mouse_y = y;
}

void feedDown(HostWin &win, bool down) {
  win.mouse_down = down;
  if (!down)
    win.mouse_click = false;
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
  if (msg == WM_SETCURSOR) {
    if (LOWORD(lp) == HTCLIENT) {
      if (win)
        applyCursor(*win);
      else {
        ensureCursors();
        if (gArrowCursor)
          SetCursor(gArrowCursor);
      }
      return TRUE;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
  }
  if (msg == WM_MOUSEMOVE && win) {
    win->mouse_x = static_cast<int>(static_cast<short>(LOWORD(lp)));
    win->mouse_y = static_cast<int>(static_cast<short>(HIWORD(lp)));
    runFrame(id);
    applyCursor(*win);
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
  if (msg == WM_RBUTTONUP && win) {
    win->mouse_x = static_cast<int>(static_cast<short>(LOWORD(lp)));
    win->mouse_y = static_cast<int>(static_cast<short>(HIWORD(lp)));
    win->mouse_right_click = true;
    runFrame(id);
    return 0;
  }
  if (msg == WM_MOUSEWHEEL && win) {
    POINT pt{static_cast<LONG>(static_cast<short>(LOWORD(lp))),
             static_cast<LONG>(static_cast<short>(HIWORD(lp)))};
    ScreenToClient(hwnd, &pt);
    win->mouse_x = static_cast<int>(pt.x);
    win->mouse_y = static_cast<int>(pt.y);
    const int delta = GET_WHEEL_DELTA_WPARAM(wp);
    const int px = wheelDeltaToPixels(*win, delta);
    if (px != 0) {
      feedScroll(*win, 0, px);
      runFrame(id);
    }
    return 0;
  }
  if (msg == WM_KEYDOWN && win) {
    const int vk = static_cast<int>(wp);
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    auto modText = [&]() {
      if (shift && ctrl)
        return std::string("ctrl+shift");
      if (shift)
        return std::string("shift");
      if (ctrl)
        return std::string("ctrl");
      return std::string();
    };
    if (vk == VK_TAB) {
      feedKey(*win, vk, shift ? "shift" : "");
      runFrame(id);
      return 0;
    }
    if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_HOME || vk == VK_END ||
        vk == VK_DELETE || vk == VK_UP || vk == VK_DOWN || vk == VK_PRIOR ||
        vk == VK_NEXT || vk == VK_ESCAPE) {
      feedKey(*win, vk, modText());
      runFrame(id);
      return 0;
    }
    if (ctrl && (vk == 'A' || vk == 'a' || vk == 'C' || vk == 'c' ||
                 vk == 'X' || vk == 'x' || vk == 'V' || vk == 'v')) {
      feedKey(*win, vk, "ctrl");
      runFrame(id);
      return 0;
    }
  }
  if (msg == WM_CHAR && win) {
    const unsigned ch = static_cast<unsigned>(wp);
    if (ch == 9)
      return 0;
    if (ch == 8 || ch == 13) {
      feedKey(*win, static_cast<int>(ch), "");
    } else if (ch >= 32 && ch != 127) {
      wchar_t wch = static_cast<wchar_t>(ch);
      char utf8[8]{};
      const int n = WideCharToMultiByte(CP_UTF8, 0, &wch, 1, utf8, 7, nullptr,
                                        nullptr);
      feedKey(*win, static_cast<int>(ch),
              n > 0 ? std::string(utf8, utf8 + n) : std::string());
    }
    runFrame(id);
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
  if (msg == WM_TIMER && wp == 2) {
    if (win)
      runFrame(id);
    return 0;
  }
  if (msg == WM_DPICHANGED && win) {
    const auto *sug = reinterpret_cast<const RECT *>(lp);
    if (sug) {
      SetWindowPos(hwnd, nullptr, sug->left, sug->top, sug->right - sug->left,
                   sug->bottom - sug->top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    ensureSysFont(queryDpi(hwnd));
    RECT rc{};
    GetClientRect(hwnd, &rc);
    applyClientSize(*win, rc.right, rc.bottom);
    runFrame(id);
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
      blitFbToDc(hdc, *win, cw, ch);
    }
    EndPaint(hwnd, &ps);
    if (win)
      applyCursor(*win);
    return 0;
  }
  if (msg == WM_CLOSE) {
    DestroyWindow(hwnd);
    return 0;
  }
  if (msg == WM_DESTROY) {
    KillTimer(hwnd, 1);
    KillTimer(hwnd, 2);
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
  enableDpiAwareness();
  if (gAtom)
    return;
  if (!gArrowCursor)
    ensureCursors();
  WNDCLASSW wc{};
  wc.lpfnWndProc = wndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  // NULL class cursor: we own the cursor via WM_SETCURSOR / applyCursor.
  // A non-NULL class cursor (IDC_ARROW) is restored after GDI blits and
  // fights hover cursors, especially near control edges.
  wc.hCursor = nullptr;
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
  const int sysDpi = queryDpi(nullptr);
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  using AdjDpiFn = BOOL(WINAPI *)(LPRECT, DWORD, BOOL, DWORD, UINT);
  AdjDpiFn adjDpi = nullptr;
  if (user32)
    adjDpi = reinterpret_cast<AdjDpiFn>(
        GetProcAddress(user32, "AdjustWindowRectExForDpi"));
  if (adjDpi)
    adjDpi(&rc, style, FALSE, ex, static_cast<UINT>(sysDpi));
  else
    AdjustWindowRectEx(&rc, style, FALSE, ex);
  HWND hwnd = CreateWindowExW(
      ex, L"RoseGoldC.Window", wideOf(title).c_str(), style, CW_USEDEFAULT,
      CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr,
      GetModuleHandleW(nullptr), nullptr);
  if (!hwnd)
    return false;
  win.hwnd = hwnd;
  win.mapped = false;
  ensureSysFont(queryDpi(hwnd));
  SetTimer(hwnd, 2, 500, nullptr);
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
  blitFbToDc(hdc, win, cw, ch);
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
Class gRgViewClass = nil;

HostWin *hostFromView(id view) {
  if (!view)
    return nullptr;
  const long long idVal = reinterpret_cast<long long (*)(id, SEL)>(objc_msgSend)(
      view, sel_registerName("winId"));
  return findAlive(idVal);
}

void cocoaSetMouse(HostWin *win, id view, id event) {
  if (!win || !view || !event)
    return;
  CGPoint loc = reinterpret_cast<CGPoint (*)(id, SEL)>(objc_msgSend)(
      event, sel_registerName("locationInWindow"));
  CGPoint pt = reinterpret_cast<CGPoint (*)(id, SEL, CGPoint, id)>(objc_msgSend)(
      view, sel_registerName("convertPoint:fromView:"), loc, nil);
  win->mouse_x = static_cast<int>(pt.x);
  win->mouse_y = static_cast<int>(pt.y);
}

BOOL rgIsFlipped(id, SEL) { return YES; }

long long rgWinId(id self, SEL) {
  Ivar iv = class_getInstanceVariable(gRgViewClass, "winId_");
  if (!iv)
    return 0;
  return *reinterpret_cast<long long *>(reinterpret_cast<char *>(self) +
                                        ivar_getOffset(iv));
}

void rgSetWinId(id self, SEL, long long v) {
  Ivar iv = class_getInstanceVariable(gRgViewClass, "winId_");
  if (!iv)
    return;
  *reinterpret_cast<long long *>(reinterpret_cast<char *>(self) +
                                 ivar_getOffset(iv)) = v;
}

void rgDrawRect(id self, SEL, CGRect) {
  HostWin *win = hostFromView(self);
  if (!win || win->fb.empty())
    return;
  CGContextRef ctx = reinterpret_cast<CGContextRef (*)(id, SEL)>(objc_msgSend)(
      reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(
          (id)objc_getClass("NSGraphicsContext"),
          sel_registerName("currentContext")),
      sel_registerName("CGContext"));
  if (!ctx)
    return;
  ensureFb(*win);
  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  CGContextRef bmp = CGBitmapContextCreate(
      win->fb.data(), static_cast<size_t>(win->fb_w),
      static_cast<size_t>(win->fb_h), 8,
      static_cast<size_t>(win->fb_w) * 4, cs,
      kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
  CGColorSpaceRelease(cs);
  if (!bmp)
    return;
  CGImageRef img = CGBitmapContextCreateImage(bmp);
  CGContextRelease(bmp);
  if (!img)
    return;
  CGContextSaveGState(ctx);
  CGContextTranslateCTM(ctx, 0, win->fb_h);
  CGContextScaleCTM(ctx, 1, -1);
  CGContextDrawImage(ctx, CGRectMake(0, 0, win->fb_w, win->fb_h), img);
  CGContextRestoreGState(ctx);
  CGImageRelease(img);
}

void rgMouseDown(id self, SEL, id event) {
  HostWin *win = hostFromView(self);
  cocoaSetMouse(win, self, event);
  if (win)
    win->mouse_down = true;
}

void rgMouseUp(id self, SEL, id event) {
  HostWin *win = hostFromView(self);
  cocoaSetMouse(win, self, event);
  if (win) {
    win->mouse_down = false;
    win->mouse_click = true;
    runFrame(win->id);
  }
}

void rgRightMouseUp(id self, SEL, id event) {
  HostWin *win = hostFromView(self);
  cocoaSetMouse(win, self, event);
  if (win) {
    win->mouse_right_click = true;
    runFrame(win->id);
  }
}

void rgMouseMoved(id self, SEL, id event) {
  HostWin *win = hostFromView(self);
  cocoaSetMouse(win, self, event);
  if (win)
    runFrame(win->id);
}

void rgScrollWheel(id self, SEL, id event) {
  HostWin *win = hostFromView(self);
  cocoaSetMouse(win, self, event);
  if (!win)
    return;
  const double dy = reinterpret_cast<double (*)(id, SEL)>(objc_msgSend)(
      event, sel_registerName("scrollingDeltaY"));
  int py = static_cast<int>(dy);
  if (py == 0 && dy != 0.0)
    py = dy > 0.0 ? 1 : -1;
  if (py != 0) {
    feedScroll(*win, 0, py);
    runFrame(win->id);
  }
}

void rgKeyDown(id self, SEL, id event) {
  HostWin *win = hostFromView(self);
  if (!win || !event)
    return;
  id chars = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(
      event, sel_registerName("characters"));
  const char *utf8 = chars ? reinterpret_cast<const char *(*)(id, SEL)>(
                                 objc_msgSend)(chars, sel_registerName("UTF8String"))
                           : nullptr;
  std::string text = utf8 ? utf8 : "";
  unsigned short keyCode = reinterpret_cast<unsigned short (*)(id, SEL)>(
      objc_msgSend)(event, sel_registerName("keyCode"));
  int code = 0;
  if (keyCode == 51)
    code = 8;
  else if (keyCode == 36)
    code = 13;
  else if (!text.empty())
    code = static_cast<unsigned char>(text[0]);
  if (code == 8 || code == 13)
    text.clear();
  if (code != 0 || !text.empty()) {
    feedKey(*win, code, text);
    runFrame(win->id);
  }
}

BOOL rgAcceptsFirstResponder(id, SEL) { return YES; }

void ensureRgViewClass() {
  if (gRgViewClass)
    return;
  gRgViewClass = objc_allocateClassPair(objc_getClass("NSView"), "RGHostView", 0);
  class_addIvar(gRgViewClass, "winId_", sizeof(long long), 3, "q");
  class_addMethod(gRgViewClass, sel_registerName("isFlipped"), (IMP)rgIsFlipped,
                  "c@:");
  class_addMethod(gRgViewClass, sel_registerName("winId"), (IMP)rgWinId, "q@:");
  class_addMethod(gRgViewClass, sel_registerName("setWinId:"), (IMP)rgSetWinId,
                  "v@:q");
  class_addMethod(gRgViewClass, sel_registerName("drawRect:"), (IMP)rgDrawRect,
                  "v@:{CGRect={CGPoint=dd}{CGSize=dd}}");
  class_addMethod(gRgViewClass, sel_registerName("mouseDown:"), (IMP)rgMouseDown,
                  "v@:@");
  class_addMethod(gRgViewClass, sel_registerName("mouseUp:"), (IMP)rgMouseUp,
                  "v@:@");
  class_addMethod(gRgViewClass, sel_registerName("rightMouseUp:"),
                  (IMP)rgRightMouseUp, "v@:@");
  class_addMethod(gRgViewClass, sel_registerName("mouseDragged:"),
                  (IMP)rgMouseMoved, "v@:@");
  class_addMethod(gRgViewClass, sel_registerName("mouseMoved:"),
                  (IMP)rgMouseMoved, "v@:@");
  class_addMethod(gRgViewClass, sel_registerName("scrollWheel:"),
                  (IMP)rgScrollWheel, "v@:@");
  class_addMethod(gRgViewClass, sel_registerName("keyDown:"), (IMP)rgKeyDown,
                  "v@:@");
  class_addMethod(gRgViewClass, sel_registerName("acceptsFirstResponder"),
                  (IMP)rgAcceptsFirstResponder, "c@:");
  objc_registerClassPair(gRgViewClass);
}

void cocoaEnsureApp() {
  if (gApp)
    return;
  gApp = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(
      (id)objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
  reinterpret_cast<void (*)(id, SEL, long)>(objc_msgSend)(
      gApp, sel_registerName("setActivationPolicy:"), 0L);
  reinterpret_cast<void (*)(id, SEL)>(objc_msgSend)(
      gApp, sel_registerName("finishLaunching"));
  ensureRgViewClass();
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
      win.nsview = nullptr;
      win.mapped = false;
      continue;
    }
    id view = win.nsview ? (id)win.nsview
                         : reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(
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
  id view = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(
      (id)gRgViewClass, sel_registerName("alloc"));
  view = reinterpret_cast<id (*)(id, SEL, CGRect)>(objc_msgSend)(
      view, sel_registerName("initWithFrame:"), CGRectMake(0, 0, w, h));
  reinterpret_cast<void (*)(id, SEL, id)>(objc_msgSend)(
      window, sel_registerName("setContentView:"), view);
  reinterpret_cast<void (*)(id, SEL)>(objc_msgSend)(
      view, sel_registerName("release"));
  win.nswin = window;
  win.nsview = view;
  win.mapped = false;
  return true;
}

void nativeBind(HostWin &win, long long id) {
  if (!win.nsview)
    return;
  reinterpret_cast<void (*)(id, SEL, long long)>(objc_msgSend)(
      (id)win.nsview, sel_registerName("setWinId:"), id);
}

void nativeShow(HostWin &win) {
  if (!win.nswin)
    return;
  reinterpret_cast<void (*)(id, SEL, id)>(objc_msgSend)(
      (id)win.nswin, sel_registerName("makeKeyAndOrderFront:"), nil);
  if (win.nsview)
    reinterpret_cast<BOOL (*)(id, SEL, id)>(objc_msgSend)(
        (id)win.nswin, sel_registerName("makeFirstResponder:"), (id)win.nsview);
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
  win.nsview = nullptr;
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
  if (!win.nsview)
    return;
  reinterpret_cast<void (*)(id, SEL, BOOL)>(objc_msgSend)(
      (id)win.nsview, sel_registerName("setNeedsDisplay:"), YES);
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
struct wl_seat *gSeat = nullptr;
struct wl_pointer *gPointer = nullptr;
struct wl_keyboard *gKeyboard = nullptr;
HostWin *gWlPtrWin = nullptr;
HostWin *gWlKeyWin = nullptr;
double gWlPtrX = 0;
double gWlPtrY = 0;
uint32_t gWlMods = 0; // bit0 shift, bit1 ctrl
#ifdef ROSEGOLD_XKB
struct xkb_context *gXkbCtx = nullptr;
struct xkb_keymap *gXkbMap = nullptr;
struct xkb_state *gXkbState = nullptr;
#endif

HostWin *findByWls(struct wl_surface *surf) {
  if (!surf)
    return nullptr;
  for (auto &kv : gWins) {
    if (kv.second.wls == static_cast<void *>(surf))
      return &kv.second;
  }
  return nullptr;
}

void wlFeedSpecial(HostWin &win, int code) {
  std::string text;
  if ((code == 37 || code == 39 || code == 36 || code == 35 || code == 38 ||
       code == 40 || code == 33 || code == 34) &&
      (gWlMods & 1))
    text = "shift";
  if ((code == 'A' || code == 'a' || code == 'C' || code == 'c' || code == 'X' ||
       code == 'x' || code == 'V' || code == 'v') &&
      (gWlMods & 2))
    text = "ctrl";
  feedKey(win, code, std::move(text));
  runFrame(win.id);
}

int wlMapEvdevKey(uint32_t key) {
  switch (key) {
  case KEY_BACKSPACE:
    return 8;
  case KEY_TAB:
    return 9;
  case KEY_ENTER:
  case KEY_KPENTER:
    return 13;
  case KEY_ESC:
    return 27;
  case KEY_DELETE:
    return 46;
  case KEY_HOME:
    return 36;
  case KEY_END:
    return 35;
  case KEY_LEFT:
    return 37;
  case KEY_UP:
    return 38;
  case KEY_RIGHT:
    return 39;
  case KEY_DOWN:
    return 40;
  case KEY_PAGEUP:
    return 33;
  case KEY_PAGEDOWN:
    return 34;
  case KEY_A:
    return 'A';
  case KEY_C:
    return 'C';
  case KEY_X:
    return 'X';
  case KEY_V:
    return 'V';
  default:
    return 0;
  }
}

void wlPointerEnter(void *, struct wl_pointer *, uint32_t,
                    struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
  gWlPtrWin = findByWls(surface);
  gWlPtrX = wl_fixed_to_double(sx);
  gWlPtrY = wl_fixed_to_double(sy);
  if (gWlPtrWin) {
    gWlPtrWin->mouse_x = static_cast<int>(gWlPtrX);
    gWlPtrWin->mouse_y = static_cast<int>(gWlPtrY);
  }
}

void wlPointerLeave(void *, struct wl_pointer *, uint32_t,
                    struct wl_surface *) {
  if (gWlPtrWin)
    gWlPtrWin->mouse_down = false;
  gWlPtrWin = nullptr;
}

void wlPointerMotion(void *, struct wl_pointer *, uint32_t, wl_fixed_t sx,
                     wl_fixed_t sy) {
  gWlPtrX = wl_fixed_to_double(sx);
  gWlPtrY = wl_fixed_to_double(sy);
  if (!gWlPtrWin)
    return;
  gWlPtrWin->mouse_x = static_cast<int>(gWlPtrX);
  gWlPtrWin->mouse_y = static_cast<int>(gWlPtrY);
  runFrame(gWlPtrWin->id);
}

void wlPointerButton(void *, struct wl_pointer *, uint32_t, uint32_t,
                     uint32_t button, uint32_t state) {
  if (!gWlPtrWin)
    return;
  HostWin &win = *gWlPtrWin;
  win.mouse_x = static_cast<int>(gWlPtrX);
  win.mouse_y = static_cast<int>(gWlPtrY);
  const bool pressed = state == WL_POINTER_BUTTON_STATE_PRESSED;
  if (button == BTN_LEFT) {
    win.mouse_down = pressed;
    if (!pressed)
      win.mouse_click = true;
    runFrame(win.id);
  } else if (button == BTN_RIGHT && !pressed) {
    win.mouse_right_click = true;
    runFrame(win.id);
  }
}

void wlPointerAxis(void *, struct wl_pointer *, uint32_t, uint32_t axis,
                   wl_fixed_t value) {
  if (!gWlPtrWin)
    return;
  const double v = wl_fixed_to_double(value);
  // Wayland axis is typically positive = down/right; match X11 button4 = up.
  int dx = 0;
  int dy = 0;
  if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
    dy = v > 0 ? -48 : (v < 0 ? 48 : 0);
  else if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL)
    dx = v > 0 ? 48 : (v < 0 ? -48 : 0);
  if (dx == 0 && dy == 0)
    return;
  gWlPtrWin->mouse_x = static_cast<int>(gWlPtrX);
  gWlPtrWin->mouse_y = static_cast<int>(gWlPtrY);
  feedScroll(*gWlPtrWin, dx, dy);
  runFrame(gWlPtrWin->id);
}

void wlPointerFrame(void *, struct wl_pointer *) {}
void wlPointerAxisSource(void *, struct wl_pointer *, uint32_t) {}
void wlPointerAxisStop(void *, struct wl_pointer *, uint32_t, uint32_t) {}
void wlPointerAxisDiscrete(void *, struct wl_pointer *, uint32_t, int32_t) {}

const struct wl_pointer_listener gWlPointerListener = {
    wlPointerEnter,       wlPointerLeave,     wlPointerMotion,
    wlPointerButton,      wlPointerAxis,      wlPointerFrame,
    wlPointerAxisSource,  wlPointerAxisStop,  wlPointerAxisDiscrete};

void wlKeyboardKeymap(void *, struct wl_keyboard *, uint32_t format, int fd,
                      uint32_t size) {
#ifdef ROSEGOLD_XKB
  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
    close(fd);
    return;
  }
  char *mapStr = static_cast<char *>(
      mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
  close(fd);
  if (mapStr == MAP_FAILED)
    return;
  if (!gXkbCtx)
    gXkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (gXkbState) {
    xkb_state_unref(gXkbState);
    gXkbState = nullptr;
  }
  if (gXkbMap) {
    xkb_keymap_unref(gXkbMap);
    gXkbMap = nullptr;
  }
  if (gXkbCtx) {
    gXkbMap = xkb_keymap_new_from_string(gXkbCtx, mapStr,
                                         XKB_KEYMAP_FORMAT_TEXT_V1,
                                         XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (gXkbMap)
      gXkbState = xkb_state_new(gXkbMap);
  }
  munmap(mapStr, size);
#else
  (void)format;
  (void)size;
  close(fd);
#endif
}

void wlKeyboardEnter(void *, struct wl_keyboard *, uint32_t,
                     struct wl_surface *surface, struct wl_array *) {
  gWlKeyWin = findByWls(surface);
}

void wlKeyboardLeave(void *, struct wl_keyboard *, uint32_t,
                     struct wl_surface *) {
  gWlKeyWin = nullptr;
}

void wlKeyboardKey(void *, struct wl_keyboard *, uint32_t, uint32_t,
                   uint32_t key, uint32_t state) {
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED || !gWlKeyWin)
    return;
  HostWin &win = *gWlKeyWin;
#ifdef ROSEGOLD_XKB
  if (gXkbState) {
    const xkb_keycode_t code = key + 8;
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(gXkbState, code);
    char buf[64]{};
    const int n = xkb_state_key_get_utf8(gXkbState, code, buf, sizeof(buf));
    int mapped = 0;
    std::string text;
    if (sym == XKB_KEY_BackSpace)
      mapped = 8;
    else if (sym == XKB_KEY_Tab || sym == XKB_KEY_ISO_Left_Tab)
      mapped = 9;
    else if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter)
      mapped = 13;
    else if (sym == XKB_KEY_Escape)
      mapped = 27;
    else if (sym == XKB_KEY_Delete)
      mapped = 46;
    else if (sym == XKB_KEY_Home)
      mapped = 36;
    else if (sym == XKB_KEY_End)
      mapped = 35;
    else if (sym == XKB_KEY_Left)
      mapped = 37;
    else if (sym == XKB_KEY_Up)
      mapped = 38;
    else if (sym == XKB_KEY_Right)
      mapped = 39;
    else if (sym == XKB_KEY_Down)
      mapped = 40;
    else if (sym == XKB_KEY_Page_Up)
      mapped = 33;
    else if (sym == XKB_KEY_Page_Down)
      mapped = 34;
    else if ((gWlMods & 2) != 0 &&
             (sym == XKB_KEY_a || sym == XKB_KEY_A || sym == XKB_KEY_c ||
              sym == XKB_KEY_C || sym == XKB_KEY_x || sym == XKB_KEY_X ||
              sym == XKB_KEY_v || sym == XKB_KEY_V)) {
      if (sym == XKB_KEY_a || sym == XKB_KEY_A)
        mapped = 'A';
      else if (sym == XKB_KEY_c || sym == XKB_KEY_C)
        mapped = 'C';
      else if (sym == XKB_KEY_x || sym == XKB_KEY_X)
        mapped = 'X';
      else
        mapped = 'V';
      text = "ctrl";
    } else if (n > 0) {
      text.assign(buf, buf + n);
      if (!text.empty())
        mapped = static_cast<unsigned char>(text[0]);
    }
    if (mapped == 8 || mapped == 13 || mapped == 9 || mapped == 27 ||
        mapped == 37 || mapped == 39 || mapped == 36 || mapped == 35 ||
        mapped == 38 || mapped == 40 || mapped == 33 || mapped == 34 ||
        mapped == 46) {
      text.clear();
      if ((gWlMods & 1) != 0 &&
          (mapped == 37 || mapped == 39 || mapped == 36 || mapped == 35 ||
           mapped == 38 || mapped == 40 || mapped == 33 || mapped == 34))
        text = "shift";
    }
    if (mapped != 0 || !text.empty()) {
      feedKey(win, mapped, std::move(text));
      runFrame(win.id);
    }
    return;
  }
#endif
  const int mapped = wlMapEvdevKey(key);
  if (mapped == 0)
    return;
  if ((gWlMods & 2) != 0 &&
      (mapped == 'A' || mapped == 'C' || mapped == 'X' || mapped == 'V')) {
    feedKey(win, mapped, "ctrl");
    runFrame(win.id);
    return;
  }
  wlFeedSpecial(win, mapped);
}

void wlKeyboardModifiers(void *, struct wl_keyboard *, uint32_t, uint32_t deps,
                         uint32_t latched, uint32_t locked, uint32_t group) {
  (void)latched;
  (void)locked;
  (void)group;
#ifdef ROSEGOLD_XKB
  if (gXkbState) {
    xkb_state_update_mask(gXkbState, deps, latched, locked, 0, 0, group);
    gWlMods = 0;
    if (xkb_state_mod_name_is_active(gXkbState, XKB_MOD_NAME_SHIFT,
                                     XKB_STATE_MODS_EFFECTIVE) > 0)
      gWlMods |= 1;
    if (xkb_state_mod_name_is_active(gXkbState, XKB_MOD_NAME_CTRL,
                                     XKB_STATE_MODS_EFFECTIVE) > 0)
      gWlMods |= 2;
    return;
  }
#endif
  // Without xkb, approximate from depressed mask bits commonly used by
  // compositors (shift=1, ctrl=4) — best-effort for nav + clipboard chords.
  gWlMods = 0;
  if (deps & 1)
    gWlMods |= 1;
  if (deps & 4)
    gWlMods |= 2;
}

void wlKeyboardRepeatInfo(void *, struct wl_keyboard *, int32_t, int32_t) {}

const struct wl_keyboard_listener gWlKeyboardListener = {
    wlKeyboardKeymap, wlKeyboardEnter, wlKeyboardLeave,
    wlKeyboardKey,    wlKeyboardModifiers, wlKeyboardRepeatInfo};

void wlSeatCapabilities(void *, struct wl_seat *seat, uint32_t caps) {
  if ((caps & WL_SEAT_CAPABILITY_POINTER) && !gPointer) {
    gPointer = wl_seat_get_pointer(seat);
    if (gPointer)
      wl_pointer_add_listener(gPointer, &gWlPointerListener, nullptr);
  } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && gPointer) {
    wl_pointer_destroy(gPointer);
    gPointer = nullptr;
    gWlPtrWin = nullptr;
  }
  if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !gKeyboard) {
    gKeyboard = wl_seat_get_keyboard(seat);
    if (gKeyboard)
      wl_keyboard_add_listener(gKeyboard, &gWlKeyboardListener, nullptr);
  } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && gKeyboard) {
    wl_keyboard_destroy(gKeyboard);
    gKeyboard = nullptr;
    gWlKeyWin = nullptr;
  }
}

void wlSeatName(void *, struct wl_seat *, const char *) {}

const struct wl_seat_listener gWlSeatListener = {wlSeatCapabilities, wlSeatName};

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
  } else if (std::strcmp(iface, wl_seat_interface.name) == 0) {
    if (!gSeat) {
      const uint32_t v = ver >= 5 ? 5 : ver;
      gSeat = static_cast<struct wl_seat *>(
          wl_registry_bind(reg, name, &wl_seat_interface, v));
      if (gSeat)
        wl_seat_add_listener(gSeat, &gWlSeatListener, nullptr);
    }
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
  if (gWlPtrWin == &win)
    gWlPtrWin = nullptr;
  if (gWlKeyWin == &win)
    gWlKeyWin = nullptr;
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
  gWlPtrWin = nullptr;
  gWlKeyWin = nullptr;
  gWlMods = 0;
  if (gPointer) {
    wl_pointer_destroy(gPointer);
    gPointer = nullptr;
  }
  if (gKeyboard) {
    wl_keyboard_destroy(gKeyboard);
    gKeyboard = nullptr;
  }
  if (gSeat) {
    wl_seat_destroy(gSeat);
    gSeat = nullptr;
  }
#ifdef ROSEGOLD_XKB
  if (gXkbState) {
    xkb_state_unref(gXkbState);
    gXkbState = nullptr;
  }
  if (gXkbMap) {
    xkb_keymap_unref(gXkbMap);
    gXkbMap = nullptr;
  }
  if (gXkbCtx) {
    xkb_context_unref(gXkbCtx);
    gXkbCtx = nullptr;
  }
#endif
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
      runFrame(win->id);
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
    if (win && (e.xbutton.button == 4 || e.xbutton.button == 5)) {
      win->mouse_x = e.xbutton.x;
      win->mouse_y = e.xbutton.y;
      feedScroll(*win, 0, e.xbutton.button == 4 ? 48 : -48);
      runFrame(win->id);
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
    if (win && e.xbutton.button == 3) {
      win->mouse_x = e.xbutton.x;
      win->mouse_y = e.xbutton.y;
      win->mouse_right_click = true;
      runFrame(win->id);
    }
    return;
  }
  if (e.type == KeyPress) {
    HostWin *win = findByXid(e.xkey.window);
    if (win) {
      char buf[8]{};
      KeySym sym = 0;
      const int n = XLookupString(const_cast<XKeyEvent *>(&e.xkey), buf,
                                  sizeof(buf) - 1, &sym, nullptr);
      int code = 0;
      std::string text;
      if (sym == XK_BackSpace)
        code = 8;
      else if (sym == XK_Delete)
        code = 46;
      else if (sym == XK_Left)
        code = 37;
      else if (sym == XK_Right)
        code = 39;
      else if (sym == XK_Home)
        code = 36;
      else if (sym == XK_End)
        code = 35;
      else if (sym == XK_Return || sym == XK_KP_Enter)
        code = 13;
      else if (n > 0) {
        text.assign(buf, buf + n);
        code = static_cast<unsigned char>(text[0]);
      }
      if (code == 8 || code == 13 || code == 37 || code == 39 || code == 36 ||
          code == 35 || code == 46) {
        text.clear();
        if ((e.xkey.state & ShiftMask) &&
            (code == 37 || code == 39 || code == 36 || code == 35))
          text = "shift";
      }
      if (code != 0 || !text.empty()) {
        feedKey(*win, code, text);
        runFrame(win->id);
      }
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
                   PointerMotionMask | ExposureMask | KeyPressMask);
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
  gImages.clear();
  gClipboard.clear();
  for (auto &kv : gWins) {
    if (hasNative(kv.second))
      nativeClose(kv.second);
    kv.second.alive = false;
  }
  gWins.clear();
  nativePoll();
}

bool uiPumpFrameJobs(Interpreter &I) {
  if (I.frameJobs.empty())
    return false;
  nativePoll();
  auto jobs = std::move(I.frameJobs);
  I.frameJobs.clear();
  for (auto &job : jobs) {
    const bool alive = findAlive(job.winId) != nullptr;
    I.settleFuture(job.future, Value::makeBool(alive));
  }
  return true;
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
  if (name == "line") {
    if (args.size() != 6)
      I.runtime("__ui.line takes 6 arguments", line, col);
    HostWin *win = findAlive(needInt(0));
    if (win)
      fbLine(*win, static_cast<int>(needInt(1)), static_cast<int>(needInt(2)),
             static_cast<int>(needInt(3)), static_cast<int>(needInt(4)),
             needInt(5));
    return Value::makeVoid();
  }
  if (name == "stroke_rect") {
    if (args.size() != 6)
      I.runtime("__ui.stroke_rect takes 6 arguments", line, col);
    HostWin *win = findAlive(needInt(0));
    if (win)
      fbStrokeRect(*win, static_cast<int>(needInt(1)),
                   static_cast<int>(needInt(2)), static_cast<int>(needInt(3)),
                   static_cast<int>(needInt(4)), needInt(5));
    return Value::makeVoid();
  }
  if (name == "fill_round") {
    if (args.size() != 7)
      I.runtime("__ui.fill_round takes 7 arguments", line, col);
    HostWin *win = findAlive(needInt(0));
    if (win)
      fbFillRound(*win, static_cast<int>(needInt(1)),
                  static_cast<int>(needInt(2)), static_cast<int>(needInt(3)),
                  static_cast<int>(needInt(4)), static_cast<int>(needInt(5)),
                  needInt(6));
    return Value::makeVoid();
  }
  if (name == "stroke_round") {
    if (args.size() != 7)
      I.runtime("__ui.stroke_round takes 7 arguments", line, col);
    HostWin *win = findAlive(needInt(0));
    if (win)
      fbStrokeRound(*win, static_cast<int>(needInt(1)),
                    static_cast<int>(needInt(2)), static_cast<int>(needInt(3)),
                    static_cast<int>(needInt(4)), static_cast<int>(needInt(5)),
                    needInt(6));
    return Value::makeVoid();
  }
  if (name == "image_rgb") {
    if (args.size() != 6)
      I.runtime("__ui.image_rgb takes 6 arguments", line, col);
    HostWin *win = findAlive(needInt(0));
    const int x = static_cast<int>(needInt(1));
    const int y = static_cast<int>(needInt(2));
    const int iw = static_cast<int>(needInt(3));
    const int ih = static_cast<int>(needInt(4));
    if (args[5].kind != Value::Kind::Array || !args[5].items)
      I.runtime("__ui.image_rgb expects Array[Int] pixels", line, col);
    if (iw < 1 || ih < 1 || iw > 8192 || ih > 8192)
      I.runtime("__ui.image_rgb size out of range", line, col);
    const auto &items = *args[5].items;
    const size_t need = static_cast<size_t>(iw) * static_cast<size_t>(ih);
    if (items.size() < need)
      I.runtime("__ui.image_rgb pixel array too short", line, col);
    if (win) {
      std::vector<uint32_t> px(need);
      for (size_t i = 0; i < need; ++i) {
        if (items[i].kind != Value::Kind::Int)
          I.runtime("__ui.image_rgb pixels must be Int", line, col);
        px[i] = packRgb(items[i].i);
      }
      fbBlitRgb(*win, x, y, iw, ih, px.data());
    }
    return Value::makeVoid();
  }
  if (name == "image") {
    if (args.size() != 4)
      I.runtime("__ui.image takes 4 arguments", line, col);
    HostWin *win = findAlive(needInt(0));
    const int x = static_cast<int>(needInt(1));
    const int y = static_cast<int>(needInt(2));
    const std::string path = needStr(3);
    const RgbImage *img = cachedImage(path);
    if (win && img)
      fbBlitRgb(*win, x, y, img->w, img->h, img->px.data());
    return Value::makeVoid();
  }
  if (name == "image_width") {
    if (args.size() != 1)
      I.runtime("__ui.image_width takes 1 argument", line, col);
    const RgbImage *img = cachedImage(needStr(0));
    return Value::makeInt(img ? img->w : 0);
  }
  if (name == "image_height") {
    if (args.size() != 1)
      I.runtime("__ui.image_height takes 1 argument", line, col);
    const RgbImage *img = cachedImage(needStr(0));
    return Value::makeInt(img ? img->h : 0);
  }
  if (name == "clip_push") {
    if (args.size() != 5)
      I.runtime("__ui.clip_push takes 5 arguments", line, col);
    HostWin *win = findAlive(needInt(0));
    if (win) {
      HostWin::ClipRect c;
      c.x0 = static_cast<int>(needInt(1));
      c.y0 = static_cast<int>(needInt(2));
      c.x1 = c.x0 + static_cast<int>(needInt(3));
      c.y1 = c.y0 + static_cast<int>(needInt(4));
      if (!win->clip_stack.empty()) {
        const HostWin::ClipRect &p = win->clip_stack.back();
        if (c.x0 < p.x0)
          c.x0 = p.x0;
        if (c.y0 < p.y0)
          c.y0 = p.y0;
        if (c.x1 > p.x1)
          c.x1 = p.x1;
        if (c.y1 > p.y1)
          c.y1 = p.y1;
      }
      win->clip_stack.push_back(c);
    }
    return Value::makeVoid();
  }
  if (name == "clip_pop") {
    if (args.size() != 1)
      I.runtime("__ui.clip_pop takes 1 argument", line, col);
    HostWin *win = findAlive(needInt(0));
    if (win && !win->clip_stack.empty())
      win->clip_stack.pop_back();
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
  if (name == "cursor") {
    if (args.size() != 2)
      I.runtime("__ui.cursor takes 2 arguments", line, col);
    HostWin *win = findAlive(needInt(0));
    if (win) {
      int kind = static_cast<int>(needInt(1));
      if (kind < 0 || kind > 2)
        kind = 0;
      win->cursor_kind = kind;
#ifdef _WIN32
      applyCursor(*win);
#endif
    }
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
  if (name == "take_right_click") {
    if (args.size() != 1)
      I.runtime("__ui.take_right_click takes 1 argument", line, col);
    HostWin *win = findAlive(needInt(0));
    if (!win)
      return Value::makeBool(false);
    const bool click = win->mouse_right_click;
    win->mouse_right_click = false;
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
  if (name == "feed_right_click") {
    if (args.size() != 3)
      I.runtime("__ui.feed_right_click takes 3 arguments", line, col);
    HostWin *win = findAlive(needInt(0));
    if (win)
      feedRightClick(*win, static_cast<int>(needInt(1)),
                     static_cast<int>(needInt(2)));
    return Value::makeVoid();
  }
  if (name == "feed_mouse") {
    if (args.size() != 3)
      I.runtime("__ui.feed_mouse takes 3 arguments", line, col);
    HostWin *win = findAlive(needInt(0));
    if (win)
      feedMouse(*win, static_cast<int>(needInt(1)),
                static_cast<int>(needInt(2)));
    return Value::makeVoid();
  }
  if (name == "feed_down") {
    if (args.size() != 2)
      I.runtime("__ui.feed_down takes 2 arguments", line, col);
    HostWin *win = findAlive(needInt(0));
    if (win)
      feedDown(*win, needBool(1));
    return Value::makeVoid();
  }
  if (name == "take_key") {
    if (args.size() != 1)
      I.runtime("__ui.take_key takes 1 argument", line, col);
    HostWin *win = findAlive(needInt(0));
    if (!win)
      return Value::makeBool(false);
    const bool pending = win->key_pending;
    win->key_pending = false;
    return Value::makeBool(pending);
  }
  if (name == "key_code") {
    if (args.size() != 1)
      I.runtime("__ui.key_code takes 1 argument", line, col);
    HostWin *win = findAlive(needInt(0));
    return Value::makeInt(win ? win->key_code : 0);
  }
  if (name == "key_text") {
    if (args.size() != 1)
      I.runtime("__ui.key_text takes 1 argument", line, col);
    HostWin *win = findAlive(needInt(0));
    return Value::makeString(win ? win->key_text : "");
  }
  if (name == "feed_key") {
    if (args.size() != 3)
      I.runtime("__ui.feed_key takes 3 arguments", line, col);
    HostWin *win = findAlive(needInt(0));
    if (win)
      feedKey(*win, static_cast<int>(needInt(1)), needStr(2));
    return Value::makeVoid();
  }
  if (name == "take_scroll") {
    if (args.size() != 1)
      I.runtime("__ui.take_scroll takes 1 argument", line, col);
    HostWin *win = findAlive(needInt(0));
    if (!win)
      return Value::makeBool(false);
    const bool pending = win->scroll_pending;
    win->scroll_pending = false;
    return Value::makeBool(pending);
  }
  if (name == "scroll_dx") {
    if (args.size() != 1)
      I.runtime("__ui.scroll_dx takes 1 argument", line, col);
    HostWin *win = findAlive(needInt(0));
    return Value::makeInt(win ? win->scroll_dx : 0);
  }
  if (name == "scroll_dy") {
    if (args.size() != 1)
      I.runtime("__ui.scroll_dy takes 1 argument", line, col);
    HostWin *win = findAlive(needInt(0));
    return Value::makeInt(win ? win->scroll_dy : 0);
  }
  if (name == "feed_scroll") {
    if (args.size() != 3)
      I.runtime("__ui.feed_scroll takes 3 arguments", line, col);
    HostWin *win = findAlive(needInt(0));
    if (win)
      feedScroll(*win, static_cast<int>(needInt(1)),
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
  if (name == "clipboard_get") {
    if (!args.empty())
      I.runtime("__ui.clipboard_get takes 0 arguments", line, col);
#ifdef _WIN32
    if (OpenClipboard(nullptr)) {
      HANDLE h = GetClipboardData(CF_UNICODETEXT);
      if (h) {
        const wchar_t *w = static_cast<const wchar_t *>(GlobalLock(h));
        if (w) {
          const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0,
                                            nullptr, nullptr);
          if (n > 1) {
            std::string utf8(static_cast<size_t>(n - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, w, -1, utf8.data(), n, nullptr,
                                nullptr);
            gClipboard = utf8;
          }
          GlobalUnlock(h);
        }
      }
      CloseClipboard();
    }
#endif
    return Value::makeString(gClipboard);
  }
  if (name == "clipboard_set") {
    if (args.size() != 1)
      I.runtime("__ui.clipboard_set takes 1 argument", line, col);
    gClipboard = needStr(0);
#ifdef _WIN32
    if (OpenClipboard(nullptr)) {
      EmptyClipboard();
      const int n = MultiByteToWideChar(CP_UTF8, 0, gClipboard.c_str(), -1,
                                        nullptr, 0);
      if (n > 0) {
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(n) *
                                                     sizeof(wchar_t));
        if (mem) {
          wchar_t *dst = static_cast<wchar_t *>(GlobalLock(mem));
          if (dst) {
            MultiByteToWideChar(CP_UTF8, 0, gClipboard.c_str(), -1, dst, n);
            GlobalUnlock(mem);
            SetClipboardData(CF_UNICODETEXT, mem);
          } else {
            GlobalFree(mem);
          }
        }
      }
      CloseClipboard();
    }
#endif
    return Value::makeVoid();
  }
  if (name == "poll") {
    if (args.size() != 1)
      I.runtime("__ui.poll takes 1 argument", line, col);
    const long long id = needInt(0);
    nativePoll();
    return Value::makeBool(findAlive(id) != nullptr);
  }
  if (name == "next_frame") {
    if (args.size() != 1)
      I.runtime("__ui.next_frame takes 1 argument", line, col);
    const long long id = needInt(0);
    auto fut = std::make_shared<FutureData>();
    I.frameJobs.push_back(FrameJob{id, fut});
    return Value::makeFuture(std::move(fut));
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
