/*
  SWELL2 appstub — provides SWELLAPI_GetFunc for apps (like REAPER)
  that resolve SWELL functions via a function-pointer table.
*/

#include "swell.h"

#ifndef SWELL_PROVIDED_BY_APP

#include <cstdlib>
#include <cstring>

#undef _WDL_SWELL_H_API_DEFINED_
#undef SWELL_API_DEFINE
#define SWELL_API_DEFINE(ret, func, parms) {#func, (void *)func},

static struct api_ent {
  const char *name;
  void *func;
} api_table[] = {
  #include "swell-functions.h"
};

static int compfunc(const void *a, const void *b)
{
  return strcmp(((const api_ent *)a)->name, ((const api_ent *)b)->name);
}

extern "C" {

__attribute__((visibility("default"))) void *SWELLAPI_GetFunc(const char *name)
{
  if (!name) return (void *)0x100; // version sentinel
  static int sorted;
  if (!sorted) {
    sorted = 1;
    qsort(api_table, sizeof(api_table) / sizeof(api_table[0]),
          sizeof(api_table[0]), compfunc);
  }
  api_ent find = {name, NULL};
  api_ent *res = (api_ent *)bsearch(&find, api_table,
      sizeof(api_table) / sizeof(api_table[0]),
      sizeof(api_table[0]), compfunc);
  return res ? res->func : NULL;
}

} // extern "C"

#endif // !SWELL_PROVIDED_BY_APP
