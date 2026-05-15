/* Cockos SWELL (Simple/Small Win32 Emulation Layer for Linux/OSX)
   Copyright (C) 2006 and later, Cockos, Inc.

    This software is provided 'as-is', without any express or implied
    warranty.  In no event will the authors be held liable for any damages
    arising from the use of this software.

    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
       claim that you wrote the original software. If you use this software
       in a product, an acknowledgment in the product documentation would be
       appreciated but is not required.
    2. Altered source versions must be plainly marked as such, and must not be
       misrepresented as being the original software.
    3. This notice may not be removed or altered from any source distribution.

    Stub and helper implementations that don't belong in a larger module:
    - ListView / TreeView / TabCtrl SendMessage helpers
    - macOS-only stubs (compiled on Linux as no-ops)
*/

#include "swell-internal.h"
#include <cstdlib>
#include <cstring>

// ============================================================================
// ListView / TreeView / TabCtrl message constants
// ============================================================================

#ifndef LVM_FIRST
#define LVM_FIRST 0x1000
#endif
#define SWELL_LVM_SETBKCOLOR             (LVM_FIRST+1)
#define SWELL_LVM_SETIMAGELIST           (LVM_FIRST+3)
#define SWELL_LVM_GETITEMCOUNT           (LVM_FIRST+4)
#define SWELL_LVM_GETITEM                (LVM_FIRST+5)
#define SWELL_LVM_SETITEM                (LVM_FIRST+6)
#define SWELL_LVM_INSERTITEM             (LVM_FIRST+7)
#define SWELL_LVM_DELETEITEM             (LVM_FIRST+8)
#define SWELL_LVM_DELETEALLITEMS         (LVM_FIRST+9)
#define SWELL_LVM_GETNEXTITEM            (LVM_FIRST+12)
#define SWELL_LVM_GETITEMRECT            (LVM_FIRST+14)
#define SWELL_LVM_HITTEST                (LVM_FIRST+18)
#define SWELL_LVM_ENSUREVISIBLE          (LVM_FIRST+19)
#define SWELL_LVM_SCROLL                 (LVM_FIRST+20)
#define SWELL_LVM_REDRAWITEMS            (LVM_FIRST+21)
#define SWELL_LVM_GETCOLUMN              (LVM_FIRST+25)
#define SWELL_LVM_SETCOLUMN              (LVM_FIRST+26)
#define SWELL_LVM_INSERTCOLUMN           (LVM_FIRST+27)
#define SWELL_LVM_DELETECOLUMN           (LVM_FIRST+28)
#define SWELL_LVM_GETCOLUMNWIDTH         (LVM_FIRST+29)
#define SWELL_LVM_SETCOLUMNWIDTH         (LVM_FIRST+30)
#define SWELL_LVM_GETHEADER              (LVM_FIRST+31)
#define SWELL_LVM_GETTOPINDEX            (LVM_FIRST+39)
#define SWELL_LVM_GETCOUNTPERPAGE        (LVM_FIRST+40)
#define SWELL_LVM_SETITEMSTATE           (LVM_FIRST+43)
#define SWELL_LVM_GETITEMSTATE           (LVM_FIRST+44)
#define SWELL_LVM_GETITEMTEXT            (LVM_FIRST+45)
#define SWELL_LVM_SETITEMTEXT            (LVM_FIRST+46)
#define SWELL_LVM_SETITEMCOUNT           (LVM_FIRST+47)
#define SWELL_LVM_SORTITEMS              (LVM_FIRST+48)
#define SWELL_LVM_GETSELECTEDCOUNT       (LVM_FIRST+50)
#define SWELL_LVM_SETEXTENDEDLISTVIEWSTYLE (LVM_FIRST+54)
#define SWELL_LVM_GETSUBITEMRECT         (LVM_FIRST+56)
#define SWELL_LVM_SUBITEMHITTEST         (LVM_FIRST+57)
#define SWELL_LVM_SETCOLUMNORDERARRAY    (LVM_FIRST+58)
#define SWELL_LVM_GETCOLUMNORDERARRAY    (LVM_FIRST+59)
#define SWELL_LVM_GETSELECTIONMARK       (LVM_FIRST+66)
#define SWELL_LVM_SETTEXTCOLOR           (LVM_FIRST+36)
#define SWELL_LVM_SETTEXTBKCOLOR         (LVM_FIRST+38)

#ifndef TVM_FIRST
#define TVM_FIRST 0x1100
#endif
#define SWELL_TVM_INSERTITEM      (TVM_FIRST+0)
#define SWELL_TVM_DELETEITEM      (TVM_FIRST+1)
#define SWELL_TVM_EXPAND          (TVM_FIRST+2)
#define SWELL_TVM_SETINDENT       (TVM_FIRST+7)
#define SWELL_TVM_SELECTITEM      (TVM_FIRST+11)
#define SWELL_TVM_GETITEM         (TVM_FIRST+12)
#define SWELL_TVM_SETITEM         (TVM_FIRST+13)
#define SWELL_TVM_HITTEST         (TVM_FIRST+17)
#define SWELL_TVM_ENSUREVISIBLE   (TVM_FIRST+20)
#define SWELL_TVM_SETBKCOLOR      (TVM_FIRST+29)
#define SWELL_TVM_SETTEXTCOLOR    (TVM_FIRST+30)
#define SWELL_TVM_DELETEALLITEMS  (TVM_FIRST+60)
#define SWELL_TVM_GETSELECTION    (TVM_FIRST+61)
#define SWELL_TVM_GETPARENT       (TVM_FIRST+62)
#define SWELL_TVM_GETCHILD        (TVM_FIRST+63)
#define SWELL_TVM_GETNEXTSIBLING  (TVM_FIRST+64)
#define SWELL_TVM_GETROOT         (TVM_FIRST+65)
#define SWELL_TVM_GETNEXTITEM     (TVM_FIRST+10)
#define SWELL_TVM_GETCOUNT        (TVM_FIRST+5)

#ifndef TCM_FIRST
#define TCM_FIRST 0x1300
#endif
#define SWELL_TCM_GETITEMCOUNT  (TCM_FIRST+4)
#define SWELL_TCM_INSERTITEM    (TCM_FIRST+7)
#define SWELL_TCM_DELETEITEM    (TCM_FIRST+8)
#define SWELL_TCM_GETCURSEL     (TCM_FIRST+11)
#define SWELL_TCM_SETCURSEL     (TCM_FIRST+12)
#define SWELL_TCM_ADJUSTRECT    (TCM_FIRST+40)

// ============================================================================
// ListView helpers
// ============================================================================

void ListView_SetExtendedListViewStyleEx(HWND h, int mask, int style)
{
  SendMessage(h, SWELL_LVM_SETEXTENDEDLISTVIEWSTYLE, (WPARAM)mask, (LPARAM)style);
}

void ListView_InsertColumn(HWND h, int pos, const LVCOLUMN *lvc)
{
  SendMessage(h, SWELL_LVM_INSERTCOLUMN, (WPARAM)pos, (LPARAM)lvc);
}

bool ListView_DeleteColumn(HWND h, int pos)
{
  return SendMessage(h, SWELL_LVM_DELETECOLUMN, (WPARAM)pos, 0) != 0;
}

void ListView_SetColumn(HWND h, int pos, const LVCOLUMN *lvc)
{
  SendMessage(h, SWELL_LVM_SETCOLUMN, (WPARAM)pos, (LPARAM)lvc);
}

void ListView_GetColumn(HWND h, int pos, LVCOLUMN *lvc)
{
  SendMessage(h, SWELL_LVM_GETCOLUMN, (WPARAM)pos, (LPARAM)lvc);
}

int ListView_GetColumnWidth(HWND h, int pos)
{
  return (int)SendMessage(h, SWELL_LVM_GETCOLUMNWIDTH, (WPARAM)pos, 0);
}

int ListView_InsertItem(HWND h, const LVITEM *item)
{
  return (int)SendMessage(h, SWELL_LVM_INSERTITEM, 0, (LPARAM)item);
}

void ListView_SetItemText(HWND h, int ipos, int cpos, const char *txt)
{
  LVITEM lvi = {};
  lvi.iSubItem = cpos;
  lvi.pszText = (char*)txt;
  SendMessage(h, SWELL_LVM_SETITEMTEXT, (WPARAM)ipos, (LPARAM)&lvi);
}

bool ListView_SetItem(HWND h, LVITEM *item)
{
  return SendMessage(h, SWELL_LVM_SETITEM, 0, (LPARAM)item) != 0;
}

bool ListView_GetItem(HWND h, LVITEM *item)
{
  return SendMessage(h, SWELL_LVM_GETITEM, 0, (LPARAM)item) != 0;
}

int ListView_GetNextItem(HWND h, int istart, int flags)
{
  return (int)SendMessage(h, SWELL_LVM_GETNEXTITEM, (WPARAM)istart, (LPARAM)flags);
}

int ListView_GetItemState(HWND h, int ipos, UINT mask)
{
  return (int)SendMessage(h, SWELL_LVM_GETITEMSTATE, (WPARAM)ipos, (LPARAM)mask);
}

bool ListView_SetItemState(HWND h, int item, UINT state, UINT statemask)
{
  LVITEM lvi = {};
  lvi.state = state;
  lvi.stateMask = statemask;
  return SendMessage(h, SWELL_LVM_SETITEMSTATE, (WPARAM)item, (LPARAM)&lvi) != 0;
}

void ListView_DeleteItem(HWND h, int ipos)
{
  SendMessage(h, SWELL_LVM_DELETEITEM, (WPARAM)ipos, 0);
}

void ListView_DeleteAllItems(HWND h)
{
  SendMessage(h, SWELL_LVM_DELETEALLITEMS, 0, 0);
}

int ListView_GetSelectedCount(HWND h)
{
  return (int)SendMessage(h, SWELL_LVM_GETSELECTEDCOUNT, 0, 0);
}

int ListView_GetItemCount(HWND h)
{
  return (int)SendMessage(h, SWELL_LVM_GETITEMCOUNT, 0, 0);
}

int ListView_GetSelectionMark(HWND h)
{
  return (int)SendMessage(h, SWELL_LVM_GETSELECTIONMARK, 0, 0);
}

void ListView_SetColumnWidth(HWND h, int colpos, int wid)
{
  SendMessage(h, SWELL_LVM_SETCOLUMNWIDTH, (WPARAM)colpos, (LPARAM)wid);
}

void ListView_RedrawItems(HWND h, int startitem, int enditem)
{
  SendMessage(h, SWELL_LVM_REDRAWITEMS, (WPARAM)startitem, (LPARAM)enditem);
}

void ListView_SetItemCount(HWND h, int cnt)
{
  SendMessage(h, SWELL_LVM_SETITEMCOUNT, (WPARAM)cnt, 0);
}

void ListView_EnsureVisible(HWND h, int i, BOOL pok)
{
  SendMessage(h, SWELL_LVM_ENSUREVISIBLE, (WPARAM)i, (LPARAM)pok);
}

void ListView_SetImageList(HWND h, HIMAGELIST imagelist, int which)
{
  SendMessage(h, SWELL_LVM_SETIMAGELIST, (WPARAM)which, (LPARAM)imagelist);
}

int ListView_SubItemHitTest(HWND h, LVHITTESTINFO *pinf)
{
  return (int)SendMessage(h, SWELL_LVM_SUBITEMHITTEST, 0, (LPARAM)pinf);
}

void ListView_GetItemText(HWND hwnd, int item, int subitem, char *text, int textmax)
{
  LVITEM lvi = {};
  lvi.iSubItem = subitem;
  lvi.pszText = text;
  lvi.cchTextMax = textmax;
  SendMessage(hwnd, SWELL_LVM_GETITEMTEXT, (WPARAM)item, (LPARAM)&lvi);
}

void ListView_SortItems(HWND hwnd, PFNLVCOMPARE compf, LPARAM parm)
{
  SendMessage(hwnd, SWELL_LVM_SORTITEMS, (WPARAM)parm, (LPARAM)compf);
}

bool ListView_Scroll(HWND h, int xscroll, int yscroll)
{
  return SendMessage(h, SWELL_LVM_SCROLL, (WPARAM)xscroll, (LPARAM)yscroll) != 0;
}

int ListView_GetTopIndex(HWND h)
{
  return (int)SendMessage(h, SWELL_LVM_GETTOPINDEX, 0, 0);
}

int ListView_GetCountPerPage(HWND h)
{
  return (int)SendMessage(h, SWELL_LVM_GETCOUNTPERPAGE, 0, 0);
}

bool ListView_GetItemRect(HWND h, int item, RECT *r, int code)
{
  if (r) r->left = code;
  return SendMessage(h, SWELL_LVM_GETITEMRECT, (WPARAM)item, (LPARAM)r) != 0;
}

bool ListView_GetSubItemRect(HWND h, int item, int subitem, int code, RECT *r)
{
  if (r) { r->left = code; r->top = subitem; }
  return SendMessage(h, SWELL_LVM_GETSUBITEMRECT, (WPARAM)item, (LPARAM)r) != 0;
}

int ListView_HitTest(HWND h, LVHITTESTINFO *pinf)
{
  return (int)SendMessage(h, SWELL_LVM_HITTEST, 0, (LPARAM)pinf);
}

BOOL ListView_SetColumnOrderArray(HWND h, int cnt, int *arr)
{
  return (BOOL)SendMessage(h, SWELL_LVM_SETCOLUMNORDERARRAY, (WPARAM)cnt, (LPARAM)arr);
}

BOOL ListView_GetColumnOrderArray(HWND h, int cnt, int *arr)
{
  return (BOOL)SendMessage(h, SWELL_LVM_GETCOLUMNORDERARRAY, (WPARAM)cnt, (LPARAM)arr);
}

HWND ListView_GetHeader(HWND h)
{
  return (HWND)SendMessage(h, SWELL_LVM_GETHEADER, 0, 0);
}

int Header_GetItemCount(HWND h)
{
  return (int)SendMessage(h, SWELL_LVM_GETITEMCOUNT, 0, 0);
}

BOOL Header_GetItem(HWND h, int col, HDITEM *hi)
{
  if (!hi) return FALSE;
  LVCOLUMN lvc = {};
  lvc.mask = LVCF_TEXT | LVCF_WIDTH;
  lvc.pszText = hi->pszText;
  lvc.cchTextMax = hi->cchTextMax;
  BOOL r = (BOOL)SendMessage(h, SWELL_LVM_GETCOLUMN, (WPARAM)col, (LPARAM)&lvc);
  if (r) hi->cxy = lvc.cx;
  return r;
}

BOOL Header_SetItem(HWND h, int col, HDITEM *hi)
{
  if (!hi) return FALSE;
  LVCOLUMN lvc = {};
  lvc.mask = LVCF_WIDTH;
  lvc.cx = hi->cxy;
  return (BOOL)SendMessage(h, SWELL_LVM_SETCOLUMN, (WPARAM)col, (LPARAM)&lvc);
}

int SWELL_GetListViewHeaderHeight(HWND h)
{
  (void)h; return 20;
}

void SWELL_SetListViewFastClickMask(HWND hList, int mask)
{
  (void)hList; (void)mask;
}

void ListView_SetBkColor(HWND hwnd, int color)
{
  SendMessage(hwnd, SWELL_LVM_SETBKCOLOR, 0, (LPARAM)color);
}

void ListView_SetTextBkColor(HWND hwnd, int color)
{
  SendMessage(hwnd, SWELL_LVM_SETTEXTBKCOLOR, 0, (LPARAM)color);
}

void ListView_SetTextColor(HWND hwnd, int color)
{
  SendMessage(hwnd, SWELL_LVM_SETTEXTCOLOR, 0, (LPARAM)color);
}

void ListView_SetGridColor(HWND hwnd, int color)
{
  (void)hwnd; (void)color;
}

void ListView_SetSelColors(HWND hwnd, int *colors, int ncolors)
{
  (void)hwnd; (void)colors; (void)ncolors;
}

// ============================================================================
// TreeView helpers
// ============================================================================

HTREEITEM TreeView_InsertItem(HWND hwnd, TV_INSERTSTRUCT *ins)
{
  if (!hwnd) return NULL;
  return (HTREEITEM)SendMessage(hwnd, SWELL_TVM_INSERTITEM, 0, (LPARAM)ins);
}

BOOL TreeView_Expand(HWND hwnd, HTREEITEM item, UINT flag)
{
  if (!hwnd) return FALSE;
  return (BOOL)SendMessage(hwnd, SWELL_TVM_EXPAND, (WPARAM)flag, (LPARAM)item);
}

HTREEITEM TreeView_GetSelection(HWND hwnd)
{
  if (!hwnd) return NULL;
  return (HTREEITEM)SendMessage(hwnd, SWELL_TVM_GETSELECTION, 0, 0);
}

void TreeView_DeleteItem(HWND hwnd, HTREEITEM item)
{
  if (!hwnd) return;
  SendMessage(hwnd, SWELL_TVM_DELETEITEM, 0, (LPARAM)item);
}

void TreeView_DeleteAllItems(HWND hwnd)
{
  if (!hwnd) return;
  SendMessage(hwnd, SWELL_TVM_DELETEALLITEMS, 0, 0);
}

void TreeView_SelectItem(HWND hwnd, HTREEITEM item)
{
  if (!hwnd) return;
  SendMessage(hwnd, SWELL_TVM_SELECTITEM, 0, (LPARAM)item);
}

void TreeView_EnsureVisible(HWND hwnd, HTREEITEM item)
{
  if (!hwnd) return;
  SendMessage(hwnd, SWELL_TVM_ENSUREVISIBLE, 0, (LPARAM)item);
}

BOOL TreeView_GetItem(HWND hwnd, LPTVITEM pitem)
{
  if (!hwnd) return FALSE;
  return (BOOL)SendMessage(hwnd, SWELL_TVM_GETITEM, 0, (LPARAM)pitem);
}

BOOL TreeView_SetItem(HWND hwnd, LPTVITEM pitem)
{
  if (!hwnd) return FALSE;
  return (BOOL)SendMessage(hwnd, SWELL_TVM_SETITEM, 0, (LPARAM)pitem);
}

HTREEITEM TreeView_HitTest(HWND hwnd, TVHITTESTINFO *hti)
{
  if (!hwnd) return NULL;
  return (HTREEITEM)SendMessage(hwnd, SWELL_TVM_HITTEST, 0, (LPARAM)hti);
}

BOOL TreeView_SetIndent(HWND hwnd, int indent)
{
  if (!hwnd) return FALSE;
  return (BOOL)SendMessage(hwnd, SWELL_TVM_SETINDENT, (WPARAM)indent, 0);
}

HTREEITEM TreeView_GetParent(HWND hwnd, HTREEITEM item)
{
  if (!hwnd) return NULL;
  return (HTREEITEM)SendMessage(hwnd, SWELL_TVM_GETPARENT, 0, (LPARAM)item);
}

HTREEITEM TreeView_GetChild(HWND hwnd, HTREEITEM item)
{
  if (!hwnd) return NULL;
  return (HTREEITEM)SendMessage(hwnd, SWELL_TVM_GETCHILD, 0, (LPARAM)item);
}

HTREEITEM TreeView_GetNextSibling(HWND hwnd, HTREEITEM item)
{
  if (!hwnd) return NULL;
  return (HTREEITEM)SendMessage(hwnd, SWELL_TVM_GETNEXTSIBLING, 0, (LPARAM)item);
}

HTREEITEM TreeView_GetRoot(HWND hwnd)
{
  if (!hwnd) return NULL;
  return (HTREEITEM)SendMessage(hwnd, SWELL_TVM_GETROOT, 0, 0);
}

void TreeView_SetBkColor(HWND hwnd, int color)
{
  if (!hwnd) return;
  SendMessage(hwnd, SWELL_TVM_SETBKCOLOR, 0, (LPARAM)color);
}

void TreeView_SetTextColor(HWND hwnd, int color)
{
  if (!hwnd) return;
  SendMessage(hwnd, SWELL_TVM_SETTEXTCOLOR, 0, (LPARAM)color);
}

HTREEITEM TreeView_GetNextItem(HWND hwnd, HTREEITEM item, UINT flag)
{
  if (!hwnd) return NULL;
  return (HTREEITEM)SendMessage(hwnd, SWELL_TVM_GETNEXTITEM, (WPARAM)flag, (LPARAM)item);
}

int TreeView_GetCount(HWND hwnd)
{
  if (!hwnd) return 0;
  return (int)SendMessage(hwnd, SWELL_TVM_GETCOUNT, 0, 0);
}

// ============================================================================
// Tab helpers
// ============================================================================

int TabCtrl_GetItemCount(HWND hwnd)
{
  return (int)SendMessage(hwnd, SWELL_TCM_GETITEMCOUNT, 0, 0);
}

BOOL TabCtrl_DeleteItem(HWND hwnd, int idx)
{
  return (BOOL)SendMessage(hwnd, SWELL_TCM_DELETEITEM, (WPARAM)idx, 0);
}

int TabCtrl_InsertItem(HWND hwnd, int idx, TCITEM *item)
{
  return (int)SendMessage(hwnd, SWELL_TCM_INSERTITEM, (WPARAM)idx, (LPARAM)item);
}

int TabCtrl_SetCurSel(HWND hwnd, int idx)
{
  return (int)SendMessage(hwnd, SWELL_TCM_SETCURSEL, (WPARAM)idx, 0);
}

int TabCtrl_GetCurSel(HWND hwnd)
{
  return (int)SendMessage(hwnd, SWELL_TCM_GETCURSEL, 0, 0);
}

BOOL TabCtrl_AdjustRect(HWND hwnd, BOOL fLarger, RECT *r)
{
  return (BOOL)SendMessage(hwnd, SWELL_TCM_ADJUSTRECT, (WPARAM)fLarger, (LPARAM)r);
}

// ============================================================================
// macOS-only function stubs (compiled on Linux as no-ops)
// ============================================================================

#ifdef SWELL_TARGET_OSX

int SWELL_TerminateProcess(HANDLE hand)
{
  return 0;
}

HANDLE SWELL_CreateProcessIO(const char *exe, int nparams,
                              const char **params, bool redirectIO)
{
  return NULL;
}

int SWELL_ReadWriteProcessIO(HANDLE hand, int w, char *buf, int bufsz)
{
  return 0;
}

void SWELL_EnsureMultithreadedCocoa()
{
}

void *SWELL_InitAutoRelease()
{
  return NULL;
}

void SWELL_QuitAutoRelease(void *p)
{
}

void SWELL_PostQuitMessage(void *sender)
{
}

bool SWELL_osx_is_dark_mode(int mode)
{
  return false;
}

void SWELL_SetWindowRepre(HWND hwnd, const char *fn, bool isDirty)
{
}

int SWELL_IsRetinaDC(HDC hdc)
{
  return 0;
}

int SWELL_IsRetinaHWND(HWND h)
{
  return 0;
}

void SWELL_SetNoMultiMonitorAutoSize(HWND h, bool noauto)
{
}

void SWELL_FlushWindow(HWND hwnd)
{
}

void SWELL_DisableAppNap(int disable)
{
}

void SWELL_DisableAppNapEx(int disable, int flag)
{
}

int SWELL_GetOSXVersion()
{
  return 0;
}

int SWELL_EnableMetal(HWND h, int mode)
{
  return 0;
}

int SWELL_MacKeyToWindowsKey(void *nsevent, int *flags)
{
  return 0;
}

int SWELL_MacKeyToWindowsKeyEx(void *nsevent, int *flags, int mode)
{
  return 0;
}

// --- Combobox macOS helpers ---

int SWELL_CB_AddString(HWND hwnd, int idx, const char *str)
{
  return -1;
}

void SWELL_CB_SetCurSel(HWND hwnd, int idx, int sel)
{
}

int SWELL_CB_GetCurSel(HWND hwnd, int idx)
{
  return -1;
}

int SWELL_CB_GetNumItems(HWND hwnd, int idx)
{
  return 0;
}

void SWELL_CB_SetItemData(HWND hwnd, int idx, int item, LONG_PTR data)
{
}

LONG_PTR SWELL_CB_GetItemData(HWND hwnd, int idx, int item)
{
  return 0;
}

void SWELL_CB_Empty(HWND hwnd, int idx)
{
}

int SWELL_CB_InsertString(HWND hwnd, int idx, int pos, const char *str)
{
  return -1;
}

int SWELL_CB_GetItemText(HWND hwnd, int idx, int item, char *buf, int bufsz)
{
  return 0;
}

void SWELL_CB_DeleteString(HWND hwnd, int idx, int wh)
{
}

int SWELL_CB_FindString(HWND hwnd, int idx, int startAfter,
                         const char *str, bool exact)
{
  return -1;
}

// --- Trackbar macOS helpers ---

void SWELL_TB_SetPos(HWND hwnd, int idx, int pos)
{
}

void SWELL_TB_SetRange(HWND hwnd, int idx, int low, int hi)
{
}

int SWELL_TB_GetPos(HWND hwnd, int idx)
{
  return 0;
}

void SWELL_TB_SetTic(HWND hwnd, int idx, int pos)
{
}

// --- CFString/NSString helpers ---

void *SWELL_CStringToCFString(const char *str)
{
  return NULL;
}

void SWELL_CFStringToCString(const void *str, char *buf, int buflen)
{
}

// --- Icon ---

void *GetNSImageFromHICON(HICON icon)
{
  return NULL;
}

#endif // SWELL_TARGET_OSX
