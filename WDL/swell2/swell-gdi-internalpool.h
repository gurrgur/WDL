#ifndef _SWELL_GDI_INTERNALPOOL_H_
#define _SWELL_GDI_INTERNALPOOL_H_

#include "swell-internal.h"

// GDI object pool: up to 100 HDCs, 200 HGDIOBJs
// Mutex-protected free lists.

#define SWELL_MAX_HDC_POOL    100
#define SWELL_MAX_HGDIOBJ_POOL 200

HDC__ *SWELL_GDP_CTX_NEW();
void   SWELL_GDP_CTX_DELETE(HDC__ *hdc);

HGDIOBJ__ *GDP_OBJECT_NEW();
void       GDP_OBJECT_DELETE(HGDIOBJ__ *obj);

bool HGDIOBJ_VALID(HGDIOBJ__ *p, int reqType = 0);
bool HDC_VALID(HDC__ *ct);

#endif // _SWELL_GDI_INTERNALPOOL_H_
