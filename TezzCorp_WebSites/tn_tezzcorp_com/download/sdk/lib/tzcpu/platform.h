#pragma once

// ── Tensor Element Accessors & Synthetic Dataset Helpers ──────────────────────

TZ_CPU_EXPORT int tz_cpu_set_f32(int64_t ptr, int64_t idx, float val) {
  float* p = fp(ptr);
  if (p) p[idx] = val;
  return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_get_f32(int64_t ptr, int64_t idx) {
  float* p = fp(ptr);
  if (!p) return 0;
  float val = p[idx];
  uint32_t bits;
  memcpy(&bits, &val, 4);
  return (int64_t)bits;
}

TZ_CPU_EXPORT int tz_cpu_fill_xor_data(int64_t x_ptr, int64_t y_ptr) {
  float* X = fp(x_ptr);
  int* Y = ip(y_ptr);
  if (X) {
    X[0] = 0.0f; X[1] = 0.0f;
    X[2] = 0.0f; X[3] = 1.0f;
    X[4] = 1.0f; X[5] = 0.0f;
    X[6] = 1.0f; X[7] = 1.0f;
  }
  if (Y) {
    Y[0] = 0;
    Y[1] = 1;
    Y[2] = 1;
    Y[3] = 0;
  }
  return 0;
}

TZ_CPU_EXPORT int64_t tz_cpu_stdin(void) {
  return (int64_t)(intptr_t)stdin;
}

TZ_CPU_EXPORT int64_t tz_cpu_stdout(void) {
  return (int64_t)(intptr_t)stdout;
}

TZ_CPU_EXPORT int64_t tz_cpu_stderr(void) {
  return (int64_t)(intptr_t)stderr;
}

typedef struct {
  void* (*fn)(void*);
  void* arg;
  int64_t result;
  int done;
#ifdef _WIN32
  HANDLE thread;
#else
  pthread_t thread;
#endif
} TzAsyncTask;

#ifdef _WIN32
static DWORD WINAPI tz_async_worker(LPVOID param) {
  TzAsyncTask* task = (TzAsyncTask*)param;
  if (task && task->fn) {
    task->result = (int64_t)(intptr_t)task->fn(task->arg);
    task->done = 1;
  }
  return 0;
}
#else
static void* tz_async_worker(void* param) {
  TzAsyncTask* task = (TzAsyncTask*)param;
  if (task && task->fn) {
    task->result = (int64_t)(intptr_t)task->fn(task->arg);
    task->done = 1;
  }
  return NULL;
}
#endif

TZ_CPU_EXPORT int64_t tz_task_spawn(int64_t fn_ptr, int64_t arg) {
  if (!fn_ptr) return 0;
  TzAsyncTask* task = (TzAsyncTask*)calloc(1, sizeof(TzAsyncTask));
  if (!task) return 0;
  task->fn = (void* (*)(void*))(intptr_t)fn_ptr;
  task->arg = (void*)(intptr_t)arg;
#ifdef _WIN32
  task->thread = CreateThread(NULL, 0, tz_async_worker, task, 0, NULL);
#else
  pthread_create(&task->thread, NULL, tz_async_worker, task);
#endif
  return (int64_t)(intptr_t)task;
}

TZ_CPU_EXPORT int64_t tz_task_await(int64_t task_handle) {
  if (!task_handle) return 0;
  TzAsyncTask* task = (TzAsyncTask*)(intptr_t)task_handle;
#ifdef _WIN32
  if (task->thread) {
    WaitForSingleObject(task->thread, INFINITE);
    CloseHandle(task->thread);
    task->thread = NULL;
  }
#else
  if (task->thread) {
    pthread_join(task->thread, NULL);
    task->thread = 0;
  }
#endif
  int64_t res = task->result;
  free(task);
  return res;
}

TZ_CPU_EXPORT int64_t tz_task_yield(void) {
#ifdef _WIN32
  Sleep(0);
#else
  usleep(0);
#endif
  return 0;
}

/* ==========================================================================
 * Native Win32 Double-Buffered Framebuffer & Host Windowing Subsystem
 * ========================================================================== */
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")

static HWND g_fb_hwnd = NULL;
static HDC g_fb_hdc = NULL;
static HDC g_fb_memdc = NULL;
static HBITMAP g_fb_hbmp = NULL;
static uint32_t* g_fb_pixels = NULL;
static int g_fb_w = 0;
static int g_fb_h = 0;
static int g_fb_closed = 0;
static int g_mouse_x = 0;
static int g_mouse_y = 0;
static int g_mouse_prev_x = 0;
static int g_mouse_prev_y = 0;
static int g_mouse_btns = 0;
static int g_last_key = 0;
static int g_key_queue[64];
static int g_key_head = 0;
static int g_key_tail = 0;

static LRESULT CALLBACK TzFbWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_MOUSEMOVE:
      g_mouse_prev_x = g_mouse_x;
      g_mouse_prev_y = g_mouse_y;
      g_mouse_x = (int)(short)LOWORD(lp);
      g_mouse_y = (int)(short)HIWORD(lp);
      return 0;
    case WM_LBUTTONDOWN:
      g_mouse_btns |= 1;
      SetCapture(hwnd);
      return 0;
    case WM_LBUTTONUP:
      g_mouse_btns &= ~1;
      ReleaseCapture();
      return 0;
    case WM_RBUTTONDOWN:
      g_mouse_btns |= 2;
      return 0;
    case WM_RBUTTONUP:
      g_mouse_btns &= ~2;
      return 0;
    case WM_KEYDOWN: {
      g_last_key = (int)wp;
      int next = (g_key_tail + 1) % 64;
      if (next != g_key_head) {
        g_key_queue[g_key_tail] = (int)wp;
        g_key_tail = next;
      }
      return 0;
    }
    case WM_CHAR: {
      int next = (g_key_tail + 1) % 64;
      if (next != g_key_head) {
        g_key_queue[g_key_tail] = (int)wp;
        g_key_tail = next;
      }
      return 0;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      if (g_fb_memdc && g_fb_w > 0 && g_fb_h > 0) {
        BitBlt(hdc, 0, 0, g_fb_w, g_fb_h, g_fb_memdc, 0, 0, SRCCOPY);
      }
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_CLOSE:
      g_fb_closed = 1;
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      g_fb_closed = 1;
      g_fb_hwnd = NULL;
      return 0;
  }
  return DefWindowProcA(hwnd, msg, wp, lp);
}

TZ_CPU_EXPORT int64_t fb_init_ex(const char* title, int64_t W, int64_t H) {
  g_fb_w = (int)W;
  g_fb_h = (int)H;
  g_fb_closed = 0;

  HINSTANCE hi = GetModuleHandle(NULL);
  WNDCLASSEXA wc = {0};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
  wc.lpfnWndProc = TzFbWndProc;
  wc.hInstance = hi;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = NULL;
  wc.lpszClassName = "TezzNative_FB_Host";
  RegisterClassExA(&wc);

  RECT wr = {0, 0, g_fb_w, g_fb_h};
  AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME, FALSE);
  int win_w = wr.right - wr.left;
  int win_h = wr.bottom - wr.top;
  int sx = (GetSystemMetrics(SM_CXSCREEN) - win_w) / 2;
  int sy = (GetSystemMetrics(SM_CYSCREEN) - win_h) / 2;

  g_fb_hwnd = CreateWindowExA(
      0, "TezzNative_FB_Host", (title && *title) ? title : "TezzNative Window",
      WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
      sx, sy, win_w, win_h, NULL, NULL, hi, NULL);
  if (!g_fb_hwnd) return -1;

  g_fb_hdc = GetDC(g_fb_hwnd);
  g_fb_memdc = CreateCompatibleDC(g_fb_hdc);

  BITMAPINFO bmi = {0};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = g_fb_w;
  bmi.bmiHeader.biHeight = -g_fb_h; // top-down DIB
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  g_fb_hbmp = CreateDIBSection(g_fb_hdc, &bmi, DIB_RGB_COLORS, (void**)&g_fb_pixels, NULL, 0);
  if (!g_fb_hbmp || !g_fb_pixels) return -1;

  SelectObject(g_fb_memdc, g_fb_hbmp);
  ShowWindow(g_fb_hwnd, SW_SHOW);
  UpdateWindow(g_fb_hwnd);
  return 0;
}

TZ_CPU_EXPORT int64_t fb_present(void) {
  if (!g_fb_hwnd || g_fb_closed) return -1;
  MSG msg;
  while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) { g_fb_closed = 1; return -1; }
    TranslateMessage(&msg);
    DispatchMessageA(&msg);
  }
  if (g_fb_hdc && g_fb_memdc) {
    BitBlt(g_fb_hdc, 0, 0, g_fb_w, g_fb_h, g_fb_memdc, 0, 0, SRCCOPY);
  }
  return g_fb_closed ? -1 : 0;
}

TZ_CPU_EXPORT char* fb_addr(void) { return (char*)g_fb_pixels; }
TZ_CPU_EXPORT int64_t fb_width(void) { return g_fb_w; }
TZ_CPU_EXPORT int64_t fb_height(void) { return g_fb_h; }
TZ_CPU_EXPORT int64_t fb_pitch(void) { return g_fb_w * 4; }
TZ_CPU_EXPORT int64_t fb_bpp(void) { return 32; }

TZ_CPU_EXPORT int64_t screen_width(void) { return (int64_t)GetSystemMetrics(SM_CXSCREEN); }
TZ_CPU_EXPORT int64_t screen_height(void) { return (int64_t)GetSystemMetrics(SM_CYSCREEN); }

TZ_CPU_EXPORT int64_t fb_fill(int64_t color) {
  if (!g_fb_pixels) return -1;
  uint32_t c = (uint32_t)color;
  int n = g_fb_w * g_fb_h;
  for (int i = 0; i < n; i++) g_fb_pixels[i] = c;
  return 0;
}

TZ_CPU_EXPORT int64_t fb_put_pixel(int64_t x, int64_t y, int64_t color) {
  if (!g_fb_pixels || x < 0 || x >= g_fb_w || y < 0 || y >= g_fb_h) return -1;
  g_fb_pixels[y * g_fb_w + x] = (uint32_t)color;
  return 0;
}

TZ_CPU_EXPORT int64_t fb_get_pixel(int64_t x, int64_t y) {
  if (!g_fb_pixels || x < 0 || x >= g_fb_w || y < 0 || y >= g_fb_h) return 0;
  return (int64_t)g_fb_pixels[y * g_fb_w + x];
}

TZ_CPU_EXPORT int64_t fb_fill_rect(int64_t x, int64_t y, int64_t w, int64_t h, int64_t color) {
  if (!g_fb_pixels) return -1;
  int x0 = (int)x, y0 = (int)y, rw = (int)w, rh = (int)h;
  if (x0 < 0) { rw += x0; x0 = 0; }
  if (y0 < 0) { rh += y0; y0 = 0; }
  if (x0 + rw > g_fb_w) rw = g_fb_w - x0;
  if (y0 + rh > g_fb_h) rh = g_fb_h - y0;
  if (rw <= 0 || rh <= 0) return 0;
  uint32_t c = (uint32_t)color;
  for (int j = 0; j < rh; j++) {
    uint32_t* row = g_fb_pixels + (y0 + j) * g_fb_w + x0;
    for (int i = 0; i < rw; i++) row[i] = c;
  }
  return 0;
}

TZ_CPU_EXPORT int64_t fb_fill_rounded(int64_t x, int64_t y, int64_t w, int64_t h, int64_t color, int64_t radius) {
  if (!g_fb_pixels) return -1;
  int x0 = (int)x, y0 = (int)y, rw = (int)w, rh = (int)h, r = (int)radius;
  if (r <= 0) return fb_fill_rect(x, y, w, h, color);
  if (r > rw / 2) r = rw / 2;
  if (r > rh / 2) r = rh / 2;
  uint32_t c = (uint32_t)color;
  int r2 = r * r;
  for (int j = 0; j < rh; j++) {
    int py = y0 + j;
    if (py < 0 || py >= g_fb_h) continue;
    for (int i = 0; i < rw; i++) {
      int px = x0 + i;
      if (px < 0 || px >= g_fb_w) continue;
      int in_corner = 0;
      int dx = 0, dy = 0;
      if (i < r && j < r) { dx = r - i; dy = r - j; in_corner = 1; }
      else if (i >= rw - r && j < r) { dx = i - (rw - r - 1); dy = r - j; in_corner = 1; }
      else if (i < r && j >= rh - r) { dx = r - i; dy = j - (rh - r - 1); in_corner = 1; }
      else if (i >= rw - r && j >= rh - r) { dx = i - (rw - r - 1); dy = j - (rh - r - 1); in_corner = 1; }
      if (in_corner && (dx * dx + dy * dy > r2)) continue;
      g_fb_pixels[py * g_fb_w + px] = c;
    }
  }
  return 0;
}

TZ_CPU_EXPORT int64_t fb_blit(int64_t sx, int64_t sy, int64_t w, int64_t h, int64_t dx, int64_t dy) {
  if (!g_fb_pixels) return -1;
  return 0;
}

TZ_CPU_EXPORT int64_t fb_text_ex(int64_t x, int64_t y, const char* s, int64_t color, int64_t scale, int64_t bold) {
  if (!g_fb_memdc || !s) return -1;
  int font_h = (int)scale * 14;
  if (font_h < 12) font_h = 14;
  HFONT font = CreateFontA(-font_h, 0, 0, 0, (bold ? FW_BOLD : FW_NORMAL), 0, 0, 0,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, 0, "Segoe UI");
  HFONT old_font = (HFONT)SelectObject(g_fb_memdc, font);
  SetBkMode(g_fb_memdc, TRANSPARENT);
  COLORREF cr = RGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
  SetTextColor(g_fb_memdc, cr);
  TextOutA(g_fb_memdc, (int)x, (int)y, s, (int)strlen(s));
  SelectObject(g_fb_memdc, old_font);
  DeleteObject(font);
  return 0;
}

TZ_CPU_EXPORT int64_t fb_text(int64_t x, int64_t y, const char* s, int64_t color) {
  return fb_text_ex(x, y, s, color, 1, 0);
}

TZ_CPU_EXPORT int64_t fb_soft_cursor(int64_t x, int64_t y, int64_t color) {
  return 0;
}

TZ_CPU_EXPORT int64_t kbd_has_event(void) {
  return (g_key_head != g_key_tail) ? 1 : 0;
}

TZ_CPU_EXPORT int64_t kbd_read_char(void) {
  if (g_key_head == g_key_tail) return 0;
  int k = g_key_queue[g_key_head];
  g_key_head = (g_key_head + 1) % 64;
  return k;
}

TZ_CPU_EXPORT int64_t kbd_read_scancode(void) {
  return (int64_t)g_last_key;
}

TZ_CPU_EXPORT int64_t mouse_has_packet(void) { return 1; }
TZ_CPU_EXPORT int64_t mouse_read_packet(void) { return 0; }
TZ_CPU_EXPORT int64_t mouse_dx(void) { return (int64_t)(g_mouse_x - g_mouse_prev_x); }
TZ_CPU_EXPORT int64_t mouse_dy(void) { return (int64_t)(g_mouse_y - g_mouse_prev_y); }
TZ_CPU_EXPORT int64_t mouse_buttons(void) { return (int64_t)g_mouse_btns; }
TZ_CPU_EXPORT int64_t mouse_pos_x(void) { return (int64_t)g_mouse_x; }
TZ_CPU_EXPORT int64_t mouse_pos_y(void) { return (int64_t)g_mouse_y; }

TZ_CPU_EXPORT int64_t sleep_ms(int64_t ms) {
  Sleep((DWORD)ms);
  return 0;
}

/* Audio Subsystem */
TZ_CPU_EXPORT int64_t tts_play_pcm(int64_t* pcm, int64_t len, int64_t rate) {
  if (!pcm || len <= 0 || rate <= 0) return -1;
  char temp_path[MAX_PATH];
  GetTempPathA(MAX_PATH, temp_path);
  strcat(temp_path, "tezz_tts_temp.wav");

  FILE* f = fopen(temp_path, "wb");
  if (!f) return -1;

  long long data_bytes = len * 2;
  long long total_size = data_bytes + 36;
  unsigned char hdr[44] = {
    'R', 'I', 'F', 'F',
    (unsigned char)(total_size & 0xFF), (unsigned char)((total_size >> 8) & 0xFF),
    (unsigned char)((total_size >> 16) & 0xFF), (unsigned char)((total_size >> 24) & 0xFF),
    'W', 'A', 'V', 'E',
    'f', 'm', 't', ' ',
    16, 0, 0, 0,
    1, 0, 1, 0,
    (unsigned char)(rate & 0xFF), (unsigned char)((rate >> 8) & 0xFF),
    (unsigned char)((rate >> 16) & 0xFF), (unsigned char)((rate >> 24) & 0xFF),
    (unsigned char)((rate * 2) & 0xFF), (unsigned char)(((rate * 2) >> 8) & 0xFF),
    (unsigned char)(((rate * 2) >> 16) & 0xFF), (unsigned char)(((rate * 2) >> 24) & 0xFF),
    2, 0, 16, 0,
    'd', 'a', 't', 'a',
    (unsigned char)(data_bytes & 0xFF), (unsigned char)((data_bytes >> 8) & 0xFF),
    (unsigned char)((data_bytes >> 16) & 0xFF), (unsigned char)((data_bytes >> 24) & 0xFF)
  };

  fwrite(hdr, 1, 44, f);
  for (int64_t i = 0; i < len; i++) {
    int64_t v = pcm[i];
    if (v > 32767) v = 32767;
    if (v < -32767) v = -32767;
    short s = (short)v;
    fwrite(&s, 2, 1, f);
  }
  fclose(f);

  PlaySoundA(temp_path, NULL, SND_FILENAME | SND_SYNC);
  DeleteFileA(temp_path);
  return 0;
}

TZ_CPU_EXPORT int64_t stt_capture_mic(int64_t* pcm, int64_t n_samples, int64_t rate) {
  if (!pcm || n_samples <= 0 || rate <= 0) return -1;
  for (int64_t i = 0; i < n_samples; i++) pcm[i] = 0;
  return 0;
}
#else
TZ_CPU_EXPORT int64_t fb_init_ex(const char* title, int64_t W, int64_t H) { return 0; }
TZ_CPU_EXPORT int64_t fb_present(void) { return 0; }
TZ_CPU_EXPORT char* fb_addr(void) { return NULL; }
TZ_CPU_EXPORT int64_t fb_width(void) { return 0; }
TZ_CPU_EXPORT int64_t fb_height(void) { return 0; }
TZ_CPU_EXPORT int64_t fb_pitch(void) { return 0; }
TZ_CPU_EXPORT int64_t fb_bpp(void) { return 32; }
TZ_CPU_EXPORT int64_t screen_width(void) { return 1920; }
TZ_CPU_EXPORT int64_t screen_height(void) { return 1080; }
TZ_CPU_EXPORT int64_t fb_fill(int64_t color) { return 0; }
TZ_CPU_EXPORT int64_t fb_put_pixel(int64_t x, int64_t y, int64_t color) { return 0; }
TZ_CPU_EXPORT int64_t fb_get_pixel(int64_t x, int64_t y) { return 0; }
TZ_CPU_EXPORT int64_t fb_fill_rect(int64_t x, int64_t y, int64_t w, int64_t h, int64_t color) { return 0; }
TZ_CPU_EXPORT int64_t fb_fill_rounded(int64_t x, int64_t y, int64_t w, int64_t h, int64_t color, int64_t radius) { return 0; }
TZ_CPU_EXPORT int64_t fb_blit(int64_t sx, int64_t sy, int64_t w, int64_t h, int64_t dx, int64_t dy) { return 0; }
TZ_CPU_EXPORT int64_t fb_text(int64_t x, int64_t y, const char* s, int64_t color) { return 0; }
TZ_CPU_EXPORT int64_t fb_text_ex(int64_t x, int64_t y, const char* s, int64_t color, int64_t scale, int64_t bold) { return 0; }
TZ_CPU_EXPORT int64_t fb_soft_cursor(int64_t x, int64_t y, int64_t color) { return 0; }
TZ_CPU_EXPORT int64_t kbd_has_event(void) { return 0; }
TZ_CPU_EXPORT int64_t kbd_read_char(void) { return 0; }
TZ_CPU_EXPORT int64_t kbd_read_scancode(void) { return 0; }
TZ_CPU_EXPORT int64_t mouse_has_packet(void) { return 0; }
TZ_CPU_EXPORT int64_t mouse_read_packet(void) { return 0; }
TZ_CPU_EXPORT int64_t mouse_dx(void) { return 0; }
TZ_CPU_EXPORT int64_t mouse_dy(void) { return 0; }
TZ_CPU_EXPORT int64_t mouse_buttons(void) { return 0; }
TZ_CPU_EXPORT int64_t mouse_pos_x(void) { return 0; }
TZ_CPU_EXPORT int64_t mouse_pos_y(void) { return 0; }
TZ_CPU_EXPORT int64_t sleep_ms(int64_t ms) { usleep(ms * 1000); return 0; }
TZ_CPU_EXPORT int64_t tts_play_pcm(int64_t* pcm, int64_t len, int64_t rate) { return 0; }
TZ_CPU_EXPORT int64_t stt_capture_mic(int64_t* pcm, int64_t n_samples, int64_t rate) { return 0; }
#endif

#ifdef __cplusplus
} // extern "C"
#endif
