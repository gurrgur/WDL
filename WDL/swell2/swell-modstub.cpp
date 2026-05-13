/*
  SWELL2 modstub — DllMain shim for SWELL_PROVIDED_BY_APP (plugin mode).

  When a plugin is compiled with SWELL_PROVIDED_BY_APP, every
  SWELL_API_DEFINE entry becomes an extern function pointer. This file
  provides DllMain (or equivalent constructor on Linux) that receives
  the SWELLAPI_GetFunc pointer from the host and resolves all function
  pointers declared in swell-functions.h.

  Usage: compile this file into the plugin, NOT into the host. The host
  links against the full swell2 library.
*/

#ifdef SWELL_PROVIDED_BY_APP

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "swell.h"
#include <cstring>

// Under SWELL_PROVIDED_BY_APP, swell-functions.h declares each API function
// as an extern function pointer:
//   extern rettype (*funcname)(parms);
//
// We need to define (not just declare) all those pointers:
#undef SWELL_API_DEFINE
#define SWELL_API_DEFINE(ret, func, parms) ret (*func) parms = nullptr;
#include "swell-functions.h"

// SWELLAPI_GetFunc: host-provided lookup function pointer
static void *(*s_getfunc)(const char *) = nullptr;

// Resolve one function pointer by name
static void resolve_one(const char *name, void **ptr)
{
  if (!s_getfunc || !ptr) return;
  void *fn = s_getfunc(name);
  if (fn) *ptr = fn;
}

// Resolve all SWELL function pointers.
// Reuse the double-include trick to generate name+pointer pairs.
static void resolve_all()
{
#undef SWELL_API_DEFINE
#define SWELL_API_DEFINE(ret, func, parms) \
  resolve_one(#func, (void **)&func);
#include "swell-functions.h"
}

// Called by the host (REAPER) after loading the plugin .so.
// The host calls DllMain(hInst, DLL_PROCESS_ATTACH, getfunc_ptr).
// On Linux there is no DllMain, so the host calls
// SWELL_dllMain(hInst, DLL_PROCESS_ATTACH, getfunc_ptr) directly,
// or the plugin exports it as DllMain via __attribute__((visibility("default"))).

extern "C" __attribute__((visibility("default")))
int SWELL_dllMain(void *hInst, unsigned int reason, void *getfunc)
{
  if (reason == 1 /*DLL_PROCESS_ATTACH*/) {
    s_getfunc = (void *(*)(const char *))getfunc;
    resolve_all();
  }
  return 1;
}

// Alias DllMain to SWELL_dllMain so REAPER can find it by either name.
extern "C" __attribute__((visibility("default"), weak))
int DllMain(void *hInst, unsigned int reason, void *getfunc)
{
  return SWELL_dllMain(hInst, reason, getfunc);
}

#endif // SWELL_PROVIDED_BY_APP
