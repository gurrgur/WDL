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

    Stub implementations for SWELL functions not yet implemented in:
      swell-ini.cpp, swell-gdi.cpp, swell-wnd.cpp, swell-backend-headless.cpp
*/

#include "swell-internal.h"
#include <cstdlib>
#include <cstring>

// ============================================================================
// Dialog/Control creation
// ============================================================================

void SWELL_MakeSetCurParms(float xscale, float yscale, float xtrans, float ytrans, HWND parent, bool doauto, bool dosizetofit)
{
}

HWND SWELL_MakeButton(int def, const char *label, int idx, int x, int y, int w, int h, int flags)
{
  return NULL;
}

HWND SWELL_MakeEditField(int idx, int x, int y, int w, int h, int flags)
{
  return NULL;
}

HWND SWELL_MakeLabel(int align, const char *label, int idx, int x, int y, int w, int h, int flags)
{
  return NULL;
}

HWND SWELL_MakeControl(const char *cname, int idx, const char *classname, int style, int x, int y, int w, int h, int exstyle)
{
  return NULL;
}

HWND SWELL_MakeCombo(int idx, int x, int y, int w, int h, int flags)
{
  return NULL;
}

HWND SWELL_MakeGroupBox(const char *name, int idx, int x, int y, int w, int h, int style)
{
  return NULL;
}

HWND SWELL_MakeCheckBox(const char *name, int idx, int x, int y, int w, int h, int flags)
{
  return NULL;
}

HWND SWELL_MakeListBox(int idx, int x, int y, int w, int h, int styles)
{
  return NULL;
}

void SWELL_GenerateDialogFromList(const void *list, int listsz)
{
}

int SWELL_DialogBox(struct SWELL_DialogResourceIndex *reshead, const char *resid, HWND parent, DLGPROC dlgproc, LPARAM param)
{
  return -1;
}

HWND SWELL_CreateDialog(struct SWELL_DialogResourceIndex *reshead, const char *resid, HWND parent, DLGPROC dlgproc, LPARAM param)
{
  return NULL;
}

void EndDialog(HWND hwnd, int result)
{
}

void SWELL_CloseWindow(HWND hwnd)
{
}

void *SWELL_ModalWindowStart(HWND hwnd)
{
  return NULL;
}

bool SWELL_ModalWindowRun(void *ctx, int *ret)
{
  return false;
}

void SWELL_ModalWindowEnd(void *ctx)
{
}

// ============================================================================
// Rect utilities
// ============================================================================

BOOL SWELL_PtInRect(const RECT *r, POINT p)
{
  if (!r) return FALSE;
  return (p.x >= r->left && p.x < r->right && p.y >= r->top && p.y < r->bottom) ? TRUE : FALSE;
}

BOOL WinOffsetRect(LPRECT lprc, int dx, int dy)
{
  if (!lprc) return FALSE;
  lprc->left += dx;
  lprc->top += dy;
  lprc->right += dx;
  lprc->bottom += dy;
  return TRUE;
}

BOOL WinSetRect(LPRECT lprc, int xLeft, int yTop, int xRight, int yBottom)
{
  if (!lprc) return FALSE;
  lprc->left = xLeft;
  lprc->top = yTop;
  lprc->right = xRight;
  lprc->bottom = yBottom;
  return TRUE;
}

void WinUnionRect(RECT *out, const RECT *in1, const RECT *in2)
{
  if (!out || !in1 || !in2) return;
  out->left   = in1->left < in2->left ? in1->left : in2->left;
  out->top    = in1->top < in2->top ? in1->top : in2->top;
  out->right  = in1->right > in2->right ? in1->right : in2->right;
  out->bottom = in1->bottom > in2->bottom ? in1->bottom : in2->bottom;
}

int WinIntersectRect(RECT *out, const RECT *in1, const RECT *in2)
{
  if (!out || !in1 || !in2) return 0;
  if (in1->left >= in2->right || in2->left >= in1->right ||
      in1->top >= in2->bottom || in2->top >= in1->bottom) {
    memset(out, 0, sizeof(RECT));
    return 0;
  }
  out->left   = in1->left > in2->left ? in1->left : in2->left;
  out->top    = in1->top > in2->top ? in1->top : in2->top;
  out->right  = in1->right < in2->right ? in1->right : in2->right;
  out->bottom = in1->bottom < in2->bottom ? in1->bottom : in2->bottom;
  return 1;
}

// ============================================================================
// GUID
// ============================================================================

bool SWELL_GenerateGUID(void *g)
{
  if (g) memset(g, 0, 16);
  return true;
}

// ============================================================================
// Module/Library
// ============================================================================

DWORD GetModuleFileName(HINSTANCE hInst, char *fn, DWORD nSize)
{
  return 0;
}

HINSTANCE LoadLibrary(const char *fileName)
{
  return NULL;
}

HINSTANCE LoadLibraryGlobals(const char *fileName, bool symGlob)
{
  return NULL;
}

void *GetProcAddress(HINSTANCE hInst, const char *procName)
{
  return NULL;
}

BOOL FreeLibrary(HINSTANCE hInst)
{
  return FALSE;
}

void *SWELL_GetBundle(HINSTANCE hInst)
{
  return NULL;
}

// ============================================================================
// Message Box / File dialogs
// ============================================================================

int MessageBox(HWND hwndParent, const char *text, const char *caption, int type)
{
  return IDOK;
}

char *BrowseForFiles(const char *text, const char *initialdir, const char *initialfile, bool allowmul, const char *extlist)
{
  return NULL;
}

bool BrowseForSaveFile(const char *text, const char *initialdir, const char *initialfile, const char *extlist, char *fn, int fnsize)
{
  return false;
}

bool BrowseForDirectory(const char *text, const char *initialdir, char *fn, int fnsize)
{
  return false;
}

void BrowseFile_SetTemplate(const char *dlgid, DLGPROC dlgProc, struct SWELL_DialogResourceIndex *reshead)
{
}

BOOL ShellExecute(HWND hwndDlg, const char *action, const char *content1, const char *content2, const char *content3, int blah)
{
  return FALSE;
}

void GetTempPath(int sz, char *buf)
{
  lstrcpyn(buf, "/tmp", sz);
}

// ============================================================================
// Menu
// ============================================================================

HMENU CreatePopupMenu()
{
  return NULL;
}

HMENU CreatePopupMenuEx(const char *title)
{
  return NULL;
}

void DestroyMenu(HMENU hMenu)
{
}

int AddMenuItem(HMENU hMenu, int pos, const char *name, int tagid)
{
  return -1;
}

HMENU GetSubMenu(HMENU hMenu, int pos)
{
  return NULL;
}

int GetMenuItemCount(HMENU hMenu)
{
  return 0;
}

int GetMenuItemID(HMENU hMenu, int pos)
{
  return -1;
}

bool SetMenuItemModifier(HMENU hMenu, int idx, int flag, int code, unsigned int mask)
{
  return false;
}

bool SetMenuItemText(HMENU hMenu, int idx, int flag, const char *text)
{
  return false;
}

bool EnableMenuItem(HMENU hMenu, int idx, int en)
{
  return false;
}

bool DeleteMenu(HMENU hMenu, int idx, int flag)
{
  return false;
}

bool CheckMenuItem(HMENU hMenu, int idx, int chk)
{
  return false;
}

void InsertMenuItem(HMENU hMenu, int pos, BOOL byPos, MENUITEMINFO *mi)
{
}

void SWELL_InsertMenu(HMENU menu, int pos, unsigned int flag, UINT_PTR idx, const char *str)
{
}

BOOL GetMenuItemInfo(HMENU hMenu, int pos, BOOL byPos, MENUITEMINFO *mi)
{
  return FALSE;
}

BOOL SetMenuItemInfo(HMENU hMenu, int pos, BOOL byPos, MENUITEMINFO *mi)
{
  return FALSE;
}

void DrawMenuBar(HWND hwnd)
{
}

int TrackPopupMenu(HMENU hMenu, int flags, int xpos, int ypos, int resvd, HWND hwnd, const RECT *r)
{
  return 0;
}

HMENU SWELL_LoadMenu(struct SWELL_MenuResourceIndex *head, const char *resid)
{
  return NULL;
}

HMENU SWELL_DuplicateMenu(HMENU menu)
{
  return NULL;
}

BOOL SetMenu(HWND hwnd, HMENU menu)
{
  return FALSE;
}

HMENU GetMenu(HWND hwnd)
{
  return NULL;
}

HMENU SWELL_GetDefaultWindowMenu()
{
  return NULL;
}

void SWELL_SetDefaultWindowMenu(HMENU menu)
{
}

HMENU SWELL_GetDefaultModalWindowMenu()
{
  return NULL;
}

void SWELL_SetDefaultModalWindowMenu(HMENU menu)
{
}

HMENU SWELL_GetCurrentMenu()
{
  return NULL;
}

void SWELL_SetCurrentMenu(HMENU menu)
{
}

void SWELL_Menu_AddMenuItem(HMENU hMenu, const char *name, int idx, unsigned int flags)
{
}

int SWELL_GenerateMenuFromList(HMENU hMenu, const void *list, int listsz)
{
  return 0;
}

// ============================================================================
// Clipboard
// ============================================================================

bool OpenClipboard(HWND hwndDlg)
{
  return false;
}

void CloseClipboard()
{
}

HANDLE GetClipboardData(UINT type)
{
  return NULL;
}

void EmptyClipboard()
{
}

void SetClipboardData(UINT type, HANDLE h)
{
}

UINT RegisterClipboardFormat(const char *desc)
{
  return 0;
}

UINT EnumClipboardFormats(UINT lastfmt)
{
  return 0;
}

HANDLE GlobalAlloc(int flags, int sz)
{
  return (HANDLE)malloc(sz);
}

void *GlobalLock(HANDLE h)
{
  return (void *)h;
}

int GlobalSize(HANDLE h)
{
  return 0;
}

void GlobalUnlock(HANDLE h)
{
}

void GlobalFree(HANDLE h)
{
  free((void *)h);
}

// ============================================================================
// Threading
// ============================================================================

HANDLE CreateThread(void *TA, DWORD stackSize, DWORD (*ThreadProc)(LPVOID), LPVOID parm, DWORD cf, DWORD *tidOut)
{
  return NULL;
}

DWORD GetCurrentThreadId()
{
  return 0;
}

BOOL SetThreadPriority(HANDLE evt, int prio)
{
  return FALSE;
}

BOOL CloseHandle(HANDLE hand)
{
  return FALSE;
}

HANDLE CreateEvent(void *SA, BOOL manualReset, BOOL initialSig, const char *ignored)
{
  return NULL;
}

HANDLE CreateEventAsSocket(void *SA, BOOL manualReset, BOOL initialSig, const char *ignored)
{
  return NULL;
}

BOOL SetEvent(HANDLE evt)
{
  return FALSE;
}

BOOL ResetEvent(HANDLE evt)
{
  return FALSE;
}

DWORD WaitForSingleObject(HANDLE hand, DWORD msTO)
{
  return (DWORD)WAIT_FAILED;
}

DWORD WaitForAnySocketObject(int numObjs, HANDLE *objs, DWORD msTO)
{
  return (DWORD)WAIT_FAILED;
}

// ============================================================================
// Process
// ============================================================================

HANDLE SWELL_CreateProcess(const char *exe, int nparams, const char **params)
{
  return NULL;
}

int SWELL_GetProcessExitCode(HANDLE hand)
{
  return -1;
}

// ============================================================================
// Mouse/Keyboard/Cursor
// ============================================================================

void GetCursorPos(POINT *pt)
{
  if (pt) { pt->x = 0; pt->y = 0; }
}

DWORD GetMessagePos()
{
  return 0;
}

WORD GetAsyncKeyState(int key)
{
  return 0;
}

int SWELL_KeyToASCII(int wParam, int lParam, int *newflags)
{
  return 0;
}

HCURSOR SWELL_LoadCursor(const char *idx)
{
  return NULL;
}

void SWELL_SetCursor(HCURSOR curs)
{
}

HCURSOR SWELL_GetCursor()
{
  return NULL;
}

HCURSOR SWELL_GetLastSetCursor()
{
  return NULL;
}

bool SWELL_IsCursorVisible()
{
  return false;
}

int SWELL_ShowCursor(BOOL bShow)
{
  return 0;
}

BOOL SWELL_SetCursorPos(int X, int Y)
{
  return FALSE;
}

void SWELL_EnableRightClickEmulate(BOOL enable)
{
}

void SWELL_GetViewPort(RECT *r, const RECT *sourcerect, bool wantWork)
{
  if (r) { WinSetRect(r, 0, 0, 1920, 1080); }
}

// ============================================================================
// GDI (missing)
// ============================================================================

void SWELL_FillDialogBackground(HDC hdc, const RECT *r, int level)
{
}

void SetOpaque(HWND h, bool isopaque)
{
}

void SetAllowNoMiddleManRendering(HWND h, bool allow)
{
}

void SWELL_SetViewGL(HWND h, char wantGL)
{
}

bool SWELL_GetViewGL(HWND h)
{
  return false;
}

bool SWELL_SetGLContextToView(HWND h)
{
  return false;
}

// ============================================================================
// ListView helpers
// ============================================================================

void ListView_SetExtendedListViewStyleEx(HWND h, int mask, int style)
{
}

void ListView_InsertColumn(HWND h, int pos, const LVCOLUMN *lvc)
{
}

bool ListView_DeleteColumn(HWND h, int pos)
{
  return false;
}

void ListView_SetColumn(HWND h, int pos, const LVCOLUMN *lvc)
{
}

void ListView_GetColumn(HWND h, int pos, LVCOLUMN *lvc)
{
}

int ListView_GetColumnWidth(HWND h, int pos)
{
  return 0;
}

int ListView_InsertItem(HWND h, const LVITEM *item)
{
  return -1;
}

void ListView_SetItemText(HWND h, int ipos, int cpos, const char *txt)
{
}

bool ListView_SetItem(HWND h, LVITEM *item)
{
  return false;
}

bool ListView_GetItem(HWND h, LVITEM *item)
{
  return false;
}

int ListView_GetNextItem(HWND h, int istart, int flags)
{
  return -1;
}

int ListView_GetItemState(HWND h, int ipos, UINT mask)
{
  return 0;
}

bool ListView_SetItemState(HWND h, int item, UINT state, UINT statemask)
{
  return false;
}

void ListView_DeleteItem(HWND h, int ipos)
{
}

void ListView_DeleteAllItems(HWND h)
{
}

int ListView_GetSelectedCount(HWND h)
{
  return 0;
}

int ListView_GetItemCount(HWND h)
{
  return 0;
}

int ListView_GetSelectionMark(HWND h)
{
  return -1;
}

void ListView_SetColumnWidth(HWND h, int colpos, int wid)
{
}

void ListView_RedrawItems(HWND h, int startitem, int enditem)
{
}

void ListView_SetItemCount(HWND h, int cnt)
{
}

void ListView_EnsureVisible(HWND h, int i, BOOL pok)
{
}

void ListView_SetImageList(HWND h, HIMAGELIST imagelist, int which)
{
}

int ListView_SubItemHitTest(HWND h, LVHITTESTINFO *pinf)
{
  return -1;
}

void ListView_GetItemText(HWND hwnd, int item, int subitem, char *text, int textmax)
{
}

void ListView_SortItems(HWND hwnd, PFNLVCOMPARE compf, LPARAM parm)
{
}

bool ListView_Scroll(HWND h, int xscroll, int yscroll)
{
  return false;
}

int ListView_GetTopIndex(HWND h)
{
  return 0;
}

int ListView_GetCountPerPage(HWND h)
{
  return 0;
}

bool ListView_GetItemRect(HWND h, int item, RECT *r, int code)
{
  return false;
}

bool ListView_GetSubItemRect(HWND h, int item, int subitem, int code, RECT *r)
{
  return false;
}

int ListView_HitTest(HWND h, LVHITTESTINFO *pinf)
{
  return -1;
}

BOOL ListView_SetColumnOrderArray(HWND h, int cnt, int *arr)
{
  return FALSE;
}

BOOL ListView_GetColumnOrderArray(HWND h, int cnt, int *arr)
{
  return FALSE;
}

HWND ListView_GetHeader(HWND h)
{
  return NULL;
}

int Header_GetItemCount(HWND h)
{
  return 0;
}

BOOL Header_GetItem(HWND h, int col, HDITEM *hi)
{
  return FALSE;
}

BOOL Header_SetItem(HWND h, int col, HDITEM *hi)
{
  return FALSE;
}

int SWELL_GetListViewHeaderHeight(HWND h)
{
  return 0;
}

void SWELL_SetListViewFastClickMask(HWND hList, int mask)
{
}

void ListView_SetBkColor(HWND hwnd, int color)
{
}

void ListView_SetTextBkColor(HWND hwnd, int color)
{
}

void ListView_SetTextColor(HWND hwnd, int color)
{
}

void ListView_SetGridColor(HWND hwnd, int color)
{
}

void ListView_SetSelColors(HWND hwnd, int *colors, int ncolors)
{
}

// ============================================================================
// TreeView helpers
// ============================================================================

HTREEITEM TreeView_InsertItem(HWND hwnd, TV_INSERTSTRUCT *ins)
{
  return NULL;
}

BOOL TreeView_Expand(HWND hwnd, HTREEITEM item, UINT flag)
{
  return FALSE;
}

HTREEITEM TreeView_GetSelection(HWND hwnd)
{
  return NULL;
}

void TreeView_DeleteItem(HWND hwnd, HTREEITEM item)
{
}

void TreeView_DeleteAllItems(HWND hwnd)
{
}

void TreeView_SelectItem(HWND hwnd, HTREEITEM item)
{
}

void TreeView_EnsureVisible(HWND hwnd, HTREEITEM item)
{
}

BOOL TreeView_GetItem(HWND hwnd, LPTVITEM pitem)
{
  return FALSE;
}

BOOL TreeView_SetItem(HWND hwnd, LPTVITEM pitem)
{
  return FALSE;
}

HTREEITEM TreeView_HitTest(HWND hwnd, TVHITTESTINFO *hti)
{
  return NULL;
}

BOOL TreeView_SetIndent(HWND hwnd, int indent)
{
  return FALSE;
}

HTREEITEM TreeView_GetParent(HWND hwnd, HTREEITEM item)
{
  return NULL;
}

HTREEITEM TreeView_GetChild(HWND hwnd, HTREEITEM item)
{
  return NULL;
}

HTREEITEM TreeView_GetNextSibling(HWND hwnd, HTREEITEM item)
{
  return NULL;
}

HTREEITEM TreeView_GetRoot(HWND hwnd)
{
  return NULL;
}

void TreeView_SetBkColor(HWND hwnd, int color)
{
}

void TreeView_SetTextColor(HWND hwnd, int color)
{
}

// ============================================================================
// Tab helpers
// ============================================================================

int TabCtrl_GetItemCount(HWND hwnd)
{
  return 0;
}

BOOL TabCtrl_DeleteItem(HWND hwnd, int idx)
{
  return FALSE;
}

int TabCtrl_InsertItem(HWND hwnd, int idx, TCITEM *item)
{
  return -1;
}

int TabCtrl_SetCurSel(HWND hwnd, int idx)
{
  return -1;
}

int TabCtrl_GetCurSel(HWND hwnd)
{
  return -1;
}

BOOL TabCtrl_AdjustRect(HWND hwnd, BOOL fLarger, RECT *r)
{
  return FALSE;
}

// ============================================================================
// ImageList
// ============================================================================

HIMAGELIST ImageList_CreateEx()
{
  return NULL;
}

BOOL ImageList_Remove(HIMAGELIST list, int idx)
{
  return FALSE;
}

int ImageList_ReplaceIcon(HIMAGELIST list, int offset, HICON image)
{
  return -1;
}

int ImageList_Add(HIMAGELIST list, HBITMAP image, HBITMAP mask)
{
  return -1;
}

void ImageList_Destroy(HIMAGELIST list)
{
}

// ============================================================================
// Drag-drop
// ============================================================================

BOOL DragQueryPoint(HDROP hDrop, LPPOINT pt)
{
  return FALSE;
}

void DragFinish(HDROP hDrop)
{
}

UINT DragQueryFile(HDROP hDrop, UINT iFile, char *buf, UINT cb)
{
  return 0;
}

void SWELL_InitiateDragDrop(HWND hwnd, RECT *srcrect, const char *srcfn, void (*callback)(const char *droppath))
{
}

void SWELL_InitiateDragDropOfFileList(HWND hwnd, RECT *srcrect, const char **srclist, int srccount, HICON icon)
{
}

void SWELL_FinishDragDrop()
{
}

// ============================================================================
// Monitors
// ============================================================================

BOOL EnumDisplayMonitors(HDC hdc, const LPRECT r, MONITORENUMPROC proc, LPARAM lp)
{
  return FALSE;
}

BOOL GetMonitorInfo(HMONITOR hMonitor, void *info)
{
  return FALSE;
}

int GetSystemMetrics(int idx)
{
  return 0;
}

// ============================================================================
// Extended API / Misc
// ============================================================================

void *SWELL_ExtendedAPI(const char *key, void *v)
{
  return NULL;
}

unsigned int _controlfp(unsigned int flag, unsigned int mask)
{
  return 0;
}

void SWELL_HideApp()
{
}

BOOL SWELL_GetGestureInfo(LPARAM lParam, GESTUREINFO *gi)
{
  return FALSE;
}

bool SWELL_ChooseColor(HWND hwnd, COLORREF *color, int ncustom, COLORREF *custom)
{
  return false;
}

bool SWELL_ChooseFont(HWND hwnd, LOGFONT *lf)
{
  return false;
}

void SWELL_Register_Cursor_Resource(const char *idx, const char *name, int hotspot_x, int hotspot_y)
{
}

HCURSOR SWELL_LoadCursorFromFile(const char *fn)
{
  return NULL;
}

// ============================================================================
// macOS-only functions
// ============================================================================

#ifdef SWELL_TARGET_OSX

int SWELL_TerminateProcess(HANDLE hand)
{
  return 0;
}

HANDLE SWELL_CreateProcessIO(const char *exe, int nparams, const char **params, bool redirectIO)
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

void SWELL_SetMenuDestination(HMENU menu, HWND hwnd)
{
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

int SWELL_CB_FindString(HWND hwnd, int idx, int startAfter, const char *str, bool exact)
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

// ============================================================================
// Non-OSX only
// ============================================================================

#ifndef SWELL_TARGET_OSX

HANDLE SWELL_CreateProcessFromPID(int pid)
{
  return NULL;
}

#endif // !SWELL_TARGET_OSX
