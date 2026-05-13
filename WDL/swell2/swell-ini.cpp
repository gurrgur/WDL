#ifndef SWELL_PROVIDED_BY_APP

// swell-ini.cpp needs only the public types and function declarations
// (swell.h) plus WDL_Mutex.  The internal structs in swell-internal.h are
// not needed here, and swell-internal.h's Skia includes conflict with the
// min/max macros defined in swell-types.h.
#include "swell.h"
#include "../mutex.h"
#include "../wdlcstring.h"

// swell-types.h defines min/max as Win32-compat macros.  Undefine them
// before including C++ standard library headers which use min()/max() as
// member functions.
#ifdef min
# undef min
#endif
#ifdef max
# undef max
#endif

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

// ---- CRC32 (IEEE 802.3, reflected) ----

static uint32_t s_crc32_table[256];
static bool s_crc32_inited;

static void crc32_init()
{
  if (s_crc32_inited) return;
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t c = i;
    for (int j = 0; j < 8; ++j)
      c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0);
    s_crc32_table[i] = c;
  }
  s_crc32_inited = true;
}

static uint32_t crc32_calc(const void *data, int len)
{
  crc32_init();
  uint32_t crc = 0xFFFFFFFFu;
  const uint8_t *p = (const uint8_t *)data;
  for (int i = 0; i < len; ++i)
    crc = s_crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

// ---- Path resolution ----

static std::string resolve_ini_path(const char *fn)
{
  if (!fn || !fn[0]) {
    const char *home = getenv("HOME");
    if (!home || !home[0])
      home = "/tmp";
    return std::string(home) + "/.libSwell.ini";
  }
  return fn;
}

// ---- Section / key-value storage ----

using KeyMap = std::map<std::string, std::string>;
using SecMap = std::map<std::string, KeyMap>;

// ---- Cached INI file (LRU pool) ----

struct IniCtx {
  std::string  path;
  time_t       mtime;
  int          fsize;
  uint64_t     last_access;
  bool         dirty;
  SecMap       sections;

  IniCtx() : mtime(0), fsize(0), last_access(0), dirty(false) {}
};

enum { MAX_CTX = 32 };

static IniCtx    s_ctxs[MAX_CTX];
static WDL_Mutex s_mutex;
static uint64_t  s_acc_counter;

static time_t file_info(const char *path, int *sz)
{
  struct stat st;
  *sz = 0;
  if (!path || !path[0] || stat(path, &st)) return 0;
  if (S_ISLNK(st.st_mode)) {
    char *rp = realpath(path, nullptr);
    if (rp) {
      bool ok = (stat(rp, &st) == 0);
      free(rp);
      if (!ok) return 0;
    }
  }
  *sz = (int)st.st_size;
  return st.st_mtime;
}

static void trim_crlf(char *p)
{
  char *end = p + strlen(p);
  while (end > p && (end[-1] == '\r' || end[-1] == '\n' ||
                     end[-1] == ' '  || end[-1] == '\t'))
    --end;
  *end = 0;
}

/* Load (or re-load) an .ini file into a cache slot.
   Returns the cache entry; never null when path resolves. */
static IniCtx *ctx_load(const char *fn)
{
  std::string path = resolve_ini_path(fn);
  if (path.empty()) return nullptr;

  int best = 0;
  uint64_t bestcnt = UINT64_MAX;
  for (int i = 0; i < MAX_CTX; ++i) {
    if (!s_ctxs[i].path.empty() && s_ctxs[i].path == path)
      { best = i; break; }
    if (s_ctxs[i].last_access < bestcnt)
      { best = i; bestcnt = s_ctxs[i].last_access; }
  }

  IniCtx *ctx = &s_ctxs[best];
  ctx->last_access = ++s_acc_counter;

  int sz = 0;
  time_t mt = file_info(path.c_str(), &sz);

  if (ctx->path != path || ctx->mtime != mt || ctx->fsize != sz) {
    ctx->path    = path;
    ctx->mtime   = 0;
    ctx->fsize   = 0;
    ctx->dirty   = false;
    ctx->sections.clear();

    FILE *fp = fopen(path.c_str(), "r");
    if (!fp) return ctx;

    flock(fileno(fp), LOCK_SH);

    KeyMap *cursec = nullptr;
    char buf[32768];

    while (fgets(buf, sizeof(buf), fp)) {
      trim_crlf(buf);
      char *p = buf;
      while (*p == ' ' || *p == '\t') ++p;

      if (*p == '[') {
        char *q = strchr(p, ']');
        if (q) {
          *q = 0;
          cursec = p[1] ? &ctx->sections[p + 1] : nullptr;
        }
      } else if (cursec && *p) {
        char *eq = strchr(p, '=');
        if (eq) {
          *eq++ = 0;
          char *ke = p + strlen(p);
          while (ke > p && (ke[-1] == ' ' || ke[-1] == '\t')) --ke;
          *ke = 0;
          while (*eq == ' ' || *eq == '\t') ++eq;
          if (*p)
            (*cursec)[p] = eq;
        }
      }
    }

    ctx->mtime = file_info(path.c_str(), &ctx->fsize);

    flock(fileno(fp), LOCK_UN);
    fclose(fp);
  }
  return ctx;
}

/* Write dirty context back to disk (atomic via rename).
   Must be called with s_mutex held. */
static void ctx_flush(IniCtx *ctx)
{
  if (!ctx || ctx->path.empty() || !ctx->dirty) return;

  char tmppath[1024];
  snprintf(tmppath, sizeof(tmppath), "%s.%d.new",
           ctx->path.c_str(), (int)getpid());

  FILE *fp = fopen(tmppath, "w");
  if (!fp) return;

  flock(fileno(fp), LOCK_EX);

  for (auto &sec : ctx->sections) {
    fprintf(fp, "[%s]\n", sec.first.c_str());
    for (auto &kv : sec.second)
      fprintf(fp, "%s=%s\n", kv.first.c_str(), kv.second.c_str());
    fprintf(fp, "\n");
  }

  fflush(fp);
  flock(fileno(fp), LOCK_UN);
  fclose(fp);

  if (rename(tmppath, ctx->path.c_str()) == 0) {
    ctx->dirty = false;
    ctx->mtime = file_info(ctx->path.c_str(), &ctx->fsize);
  } else {
    unlink(tmppath);
  }
}

// ---- Trim quotes and whitespace (mimics Win32 trimming) ----

static void lstrcpyn_trimmed(char *dest, const char *src, int len)
{
  if (len < 1) return;
  while (*src == ' ' || *src == '\t') ++src;

  const char *end = src;
  if (*end) while (end[1]) ++end;
  while (end >= src && (*end == ' ' || *end == '\t')) --end;

  if      (end > src && *src == '"'  && *end == '"')  { ++src; --end; }
  else if (end > src && *src == '\'' && *end == '\'') { ++src; --end; }

  int nl = (int)(end - src + 2);
  if (nl < 1) nl = 1;
  else if (nl > len) nl = len;

  lstrcpyn_safe(dest, src, nl);
}

// ---- Hex byte decoding (2 chars -> 1 byte) ----

static bool read_hex_byte(const char *src, uint8_t *out)
{
  uint8_t v = 0;
  for (int s = 4; s >= 0; s -= 4) {
    char c = *src++;
    if      (c >= '0' && c <= '9') v += (uint8_t)(c - '0')       << s;
    else if (c >= 'a' && c <= 'f') v += (uint8_t)(c - 'a' + 10) << s;
    else if (c >= 'A' && c <= 'F') v += (uint8_t)(c - 'A' + 10) << s;
    else return false;
  }
  *out = v;
  return true;
}

// ===================================================================
// Public API
// ===================================================================

BOOL WritePrivateProfileString(const char *appname, const char *keyname,
                                const char *val, const char *fn)
{
  if (!appname || (keyname && !keyname[0])) return FALSE;

  WDL_MutexLock lock(&s_mutex);
  IniCtx *ctx = ctx_load(fn);
  if (!ctx) return FALSE;

  if (!keyname) {
    auto it = ctx->sections.find(appname);
    if (it != ctx->sections.end()) {
      ctx->sections.erase(it);
      ctx->dirty = true;
      ctx_flush(ctx);
    }
  } else if (!val) {
    auto it = ctx->sections.find(appname);
    if (it != ctx->sections.end()) {
      auto kit = it->second.find(keyname);
      if (kit != it->second.end()) {
        it->second.erase(kit);
        ctx->dirty = true;
        ctx_flush(ctx);
      }
    }
  } else {
    KeyMap &km = ctx->sections[appname];
    auto it = km.find(keyname);
    if (it == km.end() || it->second != val) {
      km[keyname] = val;
      ctx->dirty = true;
      ctx_flush(ctx);
    }
  }

  return TRUE;
}

DWORD GetPrivateProfileString(const char *appname, const char *keyname,
                               const char *def, char *ret, int retsize,
                               const char *fn)
{
  WDL_MutexLock lock(&s_mutex);
  IniCtx *ctx = ctx_load(fn);

  if (!ctx) {
    lstrcpyn_safe(ret, def ? def : "", retsize);
    return (DWORD)strlen(ret);
  }

  if (!appname) {
    std::string buf;
    for (auto &sec : ctx->sections)
      buf += sec.first + '\0';
    if (buf.empty()) { ret[0] = ret[1] = 0; return 0; }
    int sz = (int)buf.size();
    if (sz > retsize - 2) sz = retsize - 2;
    memcpy(ret, buf.c_str(), sz);
    ret[sz] = ret[sz + 1] = 0;
    return (DWORD)sz;
  }

  if (!keyname) {
    auto it = ctx->sections.find(appname);
    if (it == ctx->sections.end()) { ret[0] = ret[1] = 0; return 0; }
    std::string buf;
    for (auto &kv : it->second)
      buf += kv.first + '\0';
    if (buf.empty()) { ret[0] = ret[1] = 0; return 0; }
    int sz = (int)buf.size();
    if (sz > retsize - 2) sz = retsize - 2;
    memcpy(ret, buf.c_str(), sz);
    ret[sz] = ret[sz + 1] = 0;
    return (DWORD)sz;
  }

  auto sit = ctx->sections.find(appname);
  if (sit != ctx->sections.end()) {
    auto kit = sit->second.find(keyname);
    if (kit != sit->second.end()) {
      lstrcpyn_trimmed(ret, kit->second.c_str(), retsize);
      return (DWORD)strlen(ret);
    }
  }

  lstrcpyn_safe(ret, def ? def : "", retsize);
  return (DWORD)strlen(ret);
}

int GetPrivateProfileInt(const char *appname, const char *keyname,
                          int def, const char *fn)
{
  char buf[512];
  GetPrivateProfileString(appname, keyname, "", buf, sizeof(buf), fn);
  if (buf[0]) {
    int a = atoi(buf);
    if (a || buf[0] == '0') return a;
  }
  return def;
}

BOOL GetPrivateProfileStruct(const char *appname, const char *keyname,
                              void *buf, int bufsz, const char *fn)
{
  if (!appname || !keyname || bufsz < 0) return FALSE;

  int hexlen = bufsz * 2 + 8 + 1;
  char *tmp = (char *)malloc(hexlen);
  if (!tmp) return FALSE;

  BOOL ret = FALSE;
  GetPrivateProfileString(appname, keyname, "", tmp, hexlen, fn);

  if ((int)strlen(tmp) == bufsz * 2 + 8) {
    uint8_t *out = (uint8_t *)buf;
    const char *src = tmp;
    int i;
    for (i = 0; i < bufsz; ++i) {
      if (!read_hex_byte(src, out + i)) break;
      src += 2;
    }
    if (i == bufsz) {
      uint8_t crc_bytes[4];
      if (read_hex_byte(src + 0, crc_bytes + 0) &&
          read_hex_byte(src + 2, crc_bytes + 1) &&
          read_hex_byte(src + 4, crc_bytes + 2) &&
          read_hex_byte(src + 6, crc_bytes + 3)) {
        uint32_t stored_crc =
          ((uint32_t)crc_bytes[0] << 24) |
          ((uint32_t)crc_bytes[1] << 16) |
          ((uint32_t)crc_bytes[2] << 8)  |
          ((uint32_t)crc_bytes[3]);
        if (stored_crc == crc32_calc(buf, bufsz))
          ret = TRUE;
      }
    }
  }

  free(tmp);
  return ret;
}

BOOL WritePrivateProfileStruct(const char *appname, const char *keyname,
                                const void *buf, int bufsz, const char *fn)
{
  if (!keyname)
    return WritePrivateProfileString(appname, keyname, (const char *)buf, fn);

  if (!buf || bufsz <= 0)
    return WritePrivateProfileString(appname, keyname, (const char *)buf, fn);

  int hexlen = bufsz * 2 + 8 + 1;
  char *tmp = (char *)malloc(hexlen);
  if (!tmp) return FALSE;

  const uint8_t *src = (const uint8_t *)buf;
  char *p = tmp;
  for (int i = 0; i < bufsz; ++i) {
    sprintf(p, "%02X", src[i]);
    p += 2;
  }

  uint32_t crc = crc32_calc(buf, bufsz);
  sprintf(p, "%08X", crc);

  BOOL ret = WritePrivateProfileString(appname, keyname, tmp, fn);
  free(tmp);
  return ret;
}

BOOL WritePrivateProfileSection(const char *appname, const char *strings,
                                 const char *fn)
{
  if (!appname) return FALSE;

  WDL_MutexLock lock(&s_mutex);
  IniCtx *ctx = ctx_load(fn);
  if (!ctx) return FALSE;

  KeyMap &km = ctx->sections[appname];
  km.clear();

  if (strings && *strings) {
    for (const char *s = strings; *s; ) {
      std::string item(s);
      s += item.size() + 1;

      size_t eq = item.find('=');
      if (eq != std::string::npos)
        km[item.substr(0, eq)] = item.substr(eq + 1);
      else
        km[item] = "";
    }
  }

  ctx->dirty = true;
  ctx_flush(ctx);
  return TRUE;
}

DWORD GetPrivateProfileSection(const char *appname, char *strout,
                                DWORD strout_len, const char *fn)
{
  WDL_MutexLock lock(&s_mutex);

  if (!strout || strout_len < 2) {
    if (strout && strout_len == 1) *strout = 0;
    return 0;
  }

  IniCtx *ctx = ctx_load(fn);
  if (!ctx) { strout[0] = strout[1] = 0; return 0; }

  auto it = ctx->sections.find(appname ? appname : "");
  if (it == ctx->sections.end()) { strout[0] = strout[1] = 0; return 0; }

  DWORD pos = 0;
  for (auto &kv : it->second) {
    std::string line = kv.first + "=" + kv.second;
    DWORD len = (DWORD)line.size();

    if (pos + len > strout_len - 2)
      len = strout_len - 2 - pos;
    if (len > 0) {
      memcpy(strout + pos, line.c_str(), len);
      pos += len;
    }
    if (pos < strout_len - 1)
      strout[pos++] = 0;
    else {
      strout[strout_len - 1] = 0;
      return strout_len - 2;
    }
  }
  strout[pos] = 0;
  if (pos == 0) strout[1] = 0;
  return pos;
}

#endif // !SWELL_PROVIDED_BY_APP
