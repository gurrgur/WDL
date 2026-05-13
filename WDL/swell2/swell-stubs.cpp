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
  fprintf(stderr, "SWELL_CALL: SWELL_MakeSetCurParms\n");
}

HWND SWELL_MakeButton(int def, const char *label, int idx, int x, int y, int w, int h, int flags)
{
  fprintf(stderr, "SWELL_CALL: SWELL_MakeButton\n");
  return NULL;
}

HWND SWELL_MakeEditField(int idx, int x, int y, int w, int h, int flags)
{
  fprintf(stderr, "SWELL_CALL: SWELL_MakeEditField\n");
  return NULL;
}

HWND SWELL_MakeLabel(int align, const char *label, int idx, int x, int y, int w, int h, int flags)
{
  fprintf(stderr, "SWELL_CALL: SWELL_MakeLabel\n");
  return NULL;
}

HWND SWELL_MakeControl(const char *cname, int idx, const char *classname, int style, int x, int y, int w, int h, int exstyle)
{
  fprintf(stderr, "SWELL_CALL: SWELL_MakeControl\n");
  return NULL;
}

HWND SWELL_MakeCombo(int idx, int x, int y, int w, int h, int flags)
{
  fprintf(stderr, "SWELL_CALL: SWELL_MakeCombo\n");
  return NULL;
}

HWND SWELL_MakeGroupBox(const char *name, int idx, int x, int y, int w, int h, int style)
{
  fprintf(stderr, "SWELL_CALL: SWELL_MakeGroupBox\n");
  return NULL;
}

HWND SWELL_MakeCheckBox(const char *name, int idx, int x, int y, int w, int h, int flags)
{
  fprintf(stderr, "SWELL_CALL: SWELL_MakeCheckBox\n");
  return NULL;
}

HWND SWELL_MakeListBox(int idx, int x, int y, int w, int h, int styles)
{
  fprintf(stderr, "SWELL_CALL: SWELL_MakeListBox\n");
  return NULL;
}

void SWELL_GenerateDialogFromList(const void *list, int listsz)
{
  fprintf(stderr, "SWELL_CALL: SWELL_GenerateDialogFromList\n");
}

int SWELL_DialogBox(struct SWELL_DialogResourceIndex *reshead, const char *resid, HWND parent, DLGPROC dlgproc, LPARAM param)
{
  fprintf(stderr, "SWELL_CALL: SWELL_DialogBox\n");
  return -1;
}

HWND SWELL_CreateDialog(struct SWELL_DialogResourceIndex *reshead, const char *resid, HWND parent, DLGPROC dlgproc, LPARAM param)
{
  fprintf(stderr, "SWELL_CALL: SWELL_CreateDialog\n");
  return NULL;
}

void EndDialog(HWND hwnd, int result)
{
  fprintf(stderr, "SWELL_CALL: EndDialog\n");
}

void SWELL_CloseWindow(HWND hwnd)
{
  fprintf(stderr, "SWELL_CALL: SWELL_CloseWindow\n");
}

void *SWELL_ModalWindowStart(HWND hwnd)
{
  fprintf(stderr, "SWELL_CALL: SWELL_ModalWindowStart\n");
  return NULL;
}

bool SWELL_ModalWindowRun(void *ctx, int *ret)
{
  fprintf(stderr, "SWELL_CALL: SWELL_ModalWindowRun\n");
  return false;
}

void SWELL_ModalWindowEnd(void *ctx)
{
  fprintf(stderr, "SWELL_CALL: SWELL_ModalWindowEnd\n");
}

// ============================================================================
// Rect utilities
// ============================================================================

BOOL SWELL_PtInRect(const RECT *r, POINT p)
{
  fprintf(stderr, "SWELL_CALL: SWELL_PtInRect\n");
  if (!r) return FALSE;
  return (p.x >= r->left && p.x < r->right && p.y >= r->top && p.y < r->bottom) ? TRUE : FALSE;
}

BOOL WinOffsetRect(LPRECT lprc, int dx, int dy)
{
  fprintf(stderr, "SWELL_CALL: WinOffsetRect\n");
  if (!lprc) return FALSE;
  lprc->left += dx;
  lprc->top += dy;
  lprc->right += dx;
  lprc->bottom += dy;
  return TRUE;
}

BOOL WinSetRect(LPRECT lprc, int xLeft, int yTop, int xRight, int yBottom)
{
  fprintf(stderr, "SWELL_CALL: WinSetRect\n");
  if (!lprc) return FALSE;
  lprc->left = xLeft;
  lprc->top = yTop;
  lprc->right = xRight;
  lprc->bottom = yBottom;
  return TRUE;
}

void WinUnionRect(RECT *out, const RECT *in1, const RECT *in2)
{
  fprintf(stderr, "SWELL_CALL: WinUnionRect\n");
  if (!out || !in1 || !in2) return;
  out->left   = in1->left < in2->left ? in1->left : in2->left;
  out->top    = in1->top < in2->top ? in1->top : in2->top;
  out->right  = in1->right > in2->right ? in1->right : in2->right;
  out->bottom = in1->bottom > in2->bottom ? in1->bottom : in2->bottom;
}

int WinIntersectRect(RECT *out, const RECT *in1, const RECT *in2)
{
  fprintf(stderr, "SWELL_CALL: WinIntersectRect\n");
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
  fprintf(stderr, "SWELL_CALL: SWELL_GenerateGUID\n");
  if (g) memset(g, 0, 16);
  return true;
}

// ============================================================================
// Module/Library
// ============================================================================

DWORD GetModuleFileName(HINSTANCE hInst, char *fn, DWORD nSize)
{
  fprintf(stderr, "SWELL_CALL: GetModuleFileName\n");
  return 0;
}

HINSTANCE LoadLibrary(const char *fileName)
{
  fprintf(stderr, "SWELL_CALL: LoadLibrary\n");
  return NULL;
}

HINSTANCE LoadLibraryGlobals(const char *fileName, bool symGlob)
{
  fprintf(stderr, "SWELL_CALL: LoadLibraryGlobals\n");
  return NULL;
}

void *GetProcAddress(HINSTANCE hInst, const char *procName)
{
  fprintf(stderr, "SWELL_CALL: GetProcAddress\n");
  return NULL;
}

BOOL FreeLibrary(HINSTANCE hInst)
{
  fprintf(stderr, "SWELL_CALL: FreeLibrary\n");
  return FALSE;
}

void *SWELL_GetBundle(HINSTANCE hInst)
{
  fprintf(stderr, "SWELL_CALL: SWELL_GetBundle\n");
  return NULL;
}

// ============================================================================
// Message Box / File dialogs
// ============================================================================

int MessageBox(HWND hwndParent, const char *text, const char *caption, int type)
{
  fprintf(stderr, "SWELL_CALL: MessageBox\n");
  return IDOK;
}

char *BrowseForFiles(const char *text, const char *initialdir, const char *initialfile, bool allowmul, const char *extlist)
{
  fprintf(stderr, "SWELL_CALL: BrowseForFiles\n");
  return NULL;
}

bool BrowseForSaveFile(const char *text, const char *initialdir, const char *initialfile, const char *extlist, char *fn, int fnsize)
{
  fprintf(stderr, "SWELL_CALL: BrowseForSaveFile\n");
  return false;
}

bool BrowseForDirectory(const char *text, const char *initialdir, char *fn, int fnsize)
{
  fprintf(stderr, "SWELL_CALL: BrowseForDirectory\n");
  return false;
}

void BrowseFile_SetTemplate(const char *dlgid, DLGPROC dlgProc, struct SWELL_DialogResourceIndex *reshead)
{
  fprintf(stderr, "SWELL_CALL: BrowseFile_SetTemplate\n");
}

BOOL ShellExecute(HWND hwndDlg, const char *action, const char *content1, const char *content2, const char *content3, int blah)
{
  fprintf(stderr, "SWELL_CALL: ShellExecute\n");
  return FALSE;
}

void GetTempPath(int sz, char *buf)
{
  fprintf(stderr, "SWELL_CALL: GetTempPath\n");
  lstrcpyn(buf, "/tmp", sz);
}

// ============================================================================
// Menu
// ============================================================================

HMENU CreatePopupMenu()
{
  fprintf(stderr, "SWELL_CALL: CreatePopupMenu\n");
  return NULL;
}

HMENU CreatePopupMenuEx(const char *title)
{
  fprintf(stderr, "SWELL_CALL: CreatePopupMenuEx\n");
  return NULL;
}

void DestroyMenu(HMENU hMenu)
{
  fprintf(stderr, "SWELL_CALL: DestroyMenu\n");
}

int AddMenuItem(HMENU hMenu, int pos, const char *name, int tagid)
{
  fprintf(stderr, "SWELL_CALL: AddMenuItem\n");
  return -1;
}

HMENU GetSubMenu(HMENU hMenu, int pos)
{
  fprintf(stderr, "SWELL_CALL: GetSubMenu\n");
  return NULL;
}

int GetMenuItemCount(HMENU hMenu)
{
  fprintf(stderr, "SWELL_CALL: GetMenuItemCount\n");
  return 0;
}

int GetMenuItemID(HMENU hMenu, int pos)
{
  fprintf(stderr, "SWELL_CALL: GetMenuItemID\n");
  return -1;
}

bool SetMenuItemModifier(HMENU hMenu, int idx, int flag, int code, unsigned int mask)
{
  fprintf(stderr, "SWELL_CALL: SetMenuItemModifier\n");
  return false;
}

bool SetMenuItemText(HMENU hMenu, int idx, int flag, const char *text)
{
  fprintf(stderr, "SWELL_CALL: SetMenuItemText\n");
  return false;
}

bool EnableMenuItem(HMENU hMenu, int idx, int en)
{
  fprintf(stderr, "SWELL_CALL: EnableMenuItem\n");
  return false;
}

bool DeleteMenu(HMENU hMenu, int idx, int flag)
{
  fprintf(stderr, "SWELL_CALL: DeleteMenu\n");
  return false;
}

bool CheckMenuItem(HMENU hMenu, int idx, int chk)
{
  fprintf(stderr, "SWELL_CALL: CheckMenuItem\n");
  return false;
}

void InsertMenuItem(HMENU hMenu, int pos, BOOL byPos, MENUITEMINFO *mi)
{
  fprintf(stderr, "SWELL_CALL: InsertMenuItem\n");
}

void SWELL_InsertMenu(HMENU menu, int pos, unsigned int flag, UINT_PTR idx, const char *str)
{
  fprintf(stderr, "SWELL_CALL: SWELL_InsertMenu\n");
}

BOOL GetMenuItemInfo(HMENU hMenu, int pos, BOOL byPos, MENUITEMINFO *mi)
{
  fprintf(stderr, "SWELL_CALL: GetMenuItemInfo\n");
  return FALSE;
}

BOOL SetMenuItemInfo(HMENU hMenu, int pos, BOOL byPos, MENUITEMINFO *mi)
{
  fprintf(stderr, "SWELL_CALL: SetMenuItemInfo\n");
  return FALSE;
}

void DrawMenuBar(HWND hwnd)
{
  fprintf(stderr, "SWELL_CALL: DrawMenuBar\n");
}

int TrackPopupMenu(HMENU hMenu, int flags, int xpos, int ypos, int resvd, HWND hwnd, const RECT *r)
{
  fprintf(stderr, "SWELL_CALL: TrackPopupMenu\n");
  return 0;
}

HMENU SWELL_LoadMenu(struct SWELL_MenuResourceIndex *head, const char *resid)
{
  fprintf(stderr, "SWELL_CALL: SWELL_LoadMenu\n");
  return NULL;
}

HMENU SWELL_DuplicateMenu(HMENU menu)
{
  fprintf(stderr, "SWELL_CALL: SWELL_DuplicateMenu\n");
  return NULL;
}

BOOL SetMenu(HWND hwnd, HMENU menu)
{
  fprintf(stderr, "SWELL_CALL: SetMenu\n");
  return FALSE;
}

HMENU GetMenu(HWND hwnd)
{
  fprintf(stderr, "SWELL_CALL: GetMenu\n");
  return NULL;
}

HMENU SWELL_GetDefaultWindowMenu()
{
  fprintf(stderr, "SWELL_CALL: SWELL_GetDefaultWindowMenu\n");
  return NULL;
}

void SWELL_SetDefaultWindowMenu(HMENU menu)
{
  fprintf(stderr, "SWELL_CALL: SWELL_SetDefaultWindowMenu\n");
}

HMENU SWELL_GetDefaultModalWindowMenu()
{
  fprintf(stderr, "SWELL_CALL: SWELL_GetDefaultModalWindowMenu\n");
  return NULL;
}

void SWELL_SetDefaultModalWindowMenu(HMENU menu)
{
  fprintf(stderr, "SWELL_CALL: SWELL_SetDefaultModalWindowMenu\n");
}

HMENU SWELL_GetCurrentMenu()
{
  fprintf(stderr, "SWELL_CALL: SWELL_GetCurrentMenu\n");
  return NULL;
}

void SWELL_SetCurrentMenu(HMENU menu)
{
  fprintf(stderr, "SWELL_CALL: SWELL_SetCurrentMenu\n");
}

void SWELL_Menu_AddMenuItem(HMENU hMenu, const char *name, int idx, unsigned int flags)
{
  fprintf(stderr, "SWELL_CALL: SWELL_Menu_AddMenuItem\n");
}

int SWELL_GenerateMenuFromList(HMENU hMenu, const void *list, int listsz)
{
  fprintf(stderr, "SWELL_CALL: SWELL_GenerateMenuFromList\n");
  return 0;
}

// ============================================================================
// Clipboard
// ============================================================================

bool OpenClipboard(HWND hwndDlg)
{
  fprintf(stderr, "SWELL_CALL: OpenClipboard\n");
  return false;
}

void CloseClipboard()
{
  fprintf(stderr, "SWELL_CALL: CloseClipboard\n");
}

HANDLE GetClipboardData(UINT type)
{
  fprintf(stderr, "SWELL_CALL: GetClipboardData\n");
  return NULL;
}

void EmptyClipboard()
{
  fprintf(stderr, "SWELL_CALL: EmptyClipboard\n");
}

void SetClipboardData(UINT type, HANDLE h)
{
  fprintf(stderr, "SWELL_CALL: SetClipboardData\n");
}

UINT RegisterClipboardFormat(const char *desc)
{
  fprintf(stderr, "SWELL_CALL: RegisterClipboardFormat\n");
  return 0;
}

UINT EnumClipboardFormats(UINT lastfmt)
{
  fprintf(stderr, "SWELL_CALL: EnumClipboardFormats\n");
  return 0;
}

HANDLE GlobalAlloc(int flags, int sz)
{
  fprintf(stderr, "SWELL_CALL: GlobalAlloc\n");
  return (HANDLE)malloc(sz);
}

void *GlobalLock(HANDLE h)
{
  fprintf(stderr, "SWELL_CALL: GlobalLock\n");
  return (void *)h;
}

int GlobalSize(HANDLE h)
{
  fprintf(stderr, "SWELL_CALL: GlobalSize\n");
  return 0;
}

void GlobalUnlock(HANDLE h)
{
  fprintf(stderr, "SWELL_CALL: GlobalUnlock\n");
}

void GlobalFree(HANDLE h)
{
  fprintf(stderr, "SWELL_CALL: GlobalFree\n");
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
  fprintf(stderr, "SWELL_CALL: GetCurrentThreadId\n");
  return 0;
}

BOOL SetThreadPriority(HANDLE evt, int prio)
{
  fprintf(stderr, "SWELL_CALL: SetThreadPriority\n");
  return FALSE;
}

BOOL CloseHandle(HANDLE hand)
{
  fprintf(stderr, "SWELL_CALL: CloseHandle\n");
  return FALSE;
}

HANDLE CreateEvent(void *SA, BOOL manualReset, BOOL initialSig, const char *ignored)
{
  fprintf(stderr, "SWELL_CALL: CreateEvent\n");
  return NULL;
}

HANDLE CreateEventAsSocket(void *SA, BOOL manualReset, BOOL initialSig, const char *ignored)
{
  fprintf(stderr, "SWELL_CALL: CreateEventAsSocket\n");
  return NULL;
}

BOOL SetEvent(HANDLE evt)
{
  fprintf(stderr, "SWELL_CALL: SetEvent\n");
  return FALSE;
}

BOOL ResetEvent(HANDLE evt)
{
  fprintf(stderr, "SWELL_CALL: ResetEvent\n");
  return FALSE;
}

DWORD WaitForSingleObject(HANDLE hand, DWORD msTO)
{
  fprintf(stderr, "SWELL_CALL: WaitForSingleObject\n");
  return (DWORD)WAIT_FAILED;
}

DWORD WaitForAnySocketObject(int numObjs, HANDLE *objs, DWORD msTO)
{
  fprintf(stderr, "SWELL_CALL: WaitForAnySocketObject\n");
  return (DWORD)WAIT_FAILED;
}

// ============================================================================
// Process
// ============================================================================

HANDLE SWELL_CreateProcess(const char *exe, int nparams, const char **params)
{
  fprintf(stderr, "SWELL_CALL: SWELL_CreateProcess\n");
  return NULL;
}

int SWELL_GetProcessExitCode(HANDLE hand)
{
  fprintf(stderr, "SWELL_CALL: SWELL_GetProcessExitCode\n");
  return -1;
}

// ============================================================================
// Mouse/Keyboard/Cursor
// ============================================================================

void GetCursorPos(POINT *pt)
{
  fprintf(stderr, "SWELL_CALL: GetCursorPos\n");
  if (pt) { pt->x = 0; pt->y = 0; }
}

DWORD GetMessagePos()
{
  fprintf(stderr, "SWELL_CALL: GetMessagePos\n");
  return 0;
}

WORD GetAsyncKeyState(int key)
{
  fprintf(stderr, "SWELL_CALL: GetAsyncKeyState\n");
  return 0;
}

int SWELL_KeyToASCII(int wParam, int lParam, int *newflags)
{
  fprintf(stderr, "SWELL_CALL: SWELL_KeyToASCII\n");
  return 0;
}

HCURSOR SWELL_LoadCursor(const char *idx)
{
  fprintf(stderr, "SWELL_CALL: SWELL_LoadCursor\n");
  return NULL;
}

void SWELL_SetCursor(HCURSOR curs)
{
  fprintf(stderr, "SWELL_CALL: SWELL_SetCursor\n");
}

HCURSOR SWELL_GetCursor()
{
  fprintf(stderr, "SWELL_CALL: SWELL_GetCursor\n");
  return NULL;
}

HCURSOR SWELL_GetLastSetCursor()
{
  fprintf(stderr, "SWELL_CALL: SWELL_GetLastSetCursor\n");
  return NULL;
}

bool SWELL_IsCursorVisible()
{
  fprintf(stderr, "SWELL_CALL: SWELL_IsCursorVisible\n");
  return false;
}

int SWELL_ShowCursor(BOOL bShow)
{
  fprintf(stderr, "SWELL_CALL: SWELL_ShowCursor\n");
  return 0;
}

BOOL SWELL_SetCursorPos(int X, int Y)
{
  fprintf(stderr, "SWELL_CALL: SWELL_SetCursorPos\n");
  return FALSE;
}

void SWELL_EnableRightClickEmulate(BOOL enable)
{
  fprintf(stderr, "SWELL_CALL: SWELL_EnableRightClickEmulate\n");
}

void SWELL_GetViewPort(RECT *r, const RECT *sourcerect, bool wantWork)
{
  fprintf(stderr, "SWELL_CALL: SWELL_GetViewPort\n");
  if (r) { WinSetRect(r, 0, 0, 1920, 1080); }
}

// ============================================================================
// GDI (missing)
// ============================================================================



void SetOpaque(HWND h, bool isopaque)
{
  fprintf(stderr, "SWELL_CALL: SetOpaque\n");
}

void SetAllowNoMiddleManRendering(HWND h, bool allow)
{
  fprintf(stderr, "SWELL_CALL: SetAllowNoMiddleManRendering\n");
}

void SWELL_SetViewGL(HWND h, char wantGL)
{
  fprintf(stderr, "SWELL_CALL: SWELL_SetViewGL\n");
}

bool SWELL_GetViewGL(HWND h)
{
  fprintf(stderr, "SWELL_CALL: SWELL_GetViewGL\n");
  return false;
}

bool SWELL_SetGLContextToView(HWND h)
{
  fprintf(stderr, "SWELL_CALL: SWELL_SetGLContextToView\n");
  return false;
}

// ============================================================================
// ListView helpers
// ============================================================================

void ListView_SetExtendedListViewStyleEx(HWND h, int mask, int style)
{
  fprintf(stderr, "SWELL_CALL: ListView_SetExtendedListViewStyleEx\n");
}

void ListView_InsertColumn(HWND h, int pos, const LVCOLUMN *lvc)
{
  fprintf(stderr, "SWELL_CALL: ListView_InsertColumn\n");
}

bool ListView_DeleteColumn(HWND h, int pos)
{
  fprintf(stderr, "SWELL_CALL: ListView_DeleteColumn\n");
  return false;
}

void ListView_SetColumn(HWND h, int pos, const LVCOLUMN *lvc)
{
  fprintf(stderr, "SWELL_CALL: ListView_SetColumn\n");
}

void ListView_GetColumn(HWND h, int pos, LVCOLUMN *lvc)
{
  fprintf(stderr, "SWELL_CALL: ListView_GetColumn\n");
}

int ListView_GetColumnWidth(HWND h, int pos)
{
  fprintf(stderr, "SWELL_CALL: ListView_GetColumnWidth\n");
  return 0;
}

int ListView_InsertItem(HWND h, const LVITEM *item)
{
  fprintf(stderr, "SWELL_CALL: ListView_InsertItem\n");
  return -1;
}

void ListView_SetItemText(HWND h, int ipos, int cpos, const char *txt)
{
  fprintf(stderr, "SWELL_CALL: ListView_SetItemText\n");
}

bool ListView_SetItem(HWND h, LVITEM *item)
{
  fprintf(stderr, "SWELL_CALL: ListView_SetItem\n");
  return false;
}

bool ListView_GetItem(HWND h, LVITEM *item)
{
  fprintf(stderr, "SWELL_CALL: ListView_GetItem\n");
  return false;
}

int ListView_GetNextItem(HWND h, int istart, int flags)
{
  fprintf(stderr, "SWELL_CALL: ListView_GetNextItem\n");
  return -1;
}

int ListView_GetItemState(HWND h, int ipos, UINT mask)
{
  fprintf(stderr, "SWELL_CALL: ListView_GetItemState\n");
  return 0;
}

bool ListView_SetItemState(HWND h, int item, UINT state, UINT statemask)
{
  fprintf(stderr, "SWELL_CALL: ListView_SetItemState\n");
  return false;
}

void ListView_DeleteItem(HWND h, int ipos)
{
  fprintf(stderr, "SWELL_CALL: ListView_DeleteItem\n");
}

void ListView_DeleteAllItems(HWND h)
{
  fprintf(stderr, "SWELL_CALL: ListView_DeleteAllItems\n");
}

int ListView_GetSelectedCount(HWND h)
{
  fprintf(stderr, "SWELL_CALL: ListView_GetSelectedCount\n");
  return 0;
}

int ListView_GetItemCount(HWND h)
{
  fprintf(stderr, "SWELL_CALL: ListView_GetItemCount\n");
  return 0;
}

int ListView_GetSelectionMark(HWND h)
{
  fprintf(stderr, "SWELL_CALL: ListView_GetSelectionMark\n");
  return -1;
}

void ListView_SetColumnWidth(HWND h, int colpos, int wid)
{
  fprintf(stderr, "SWELL_CALL: ListView_SetColumnWidth\n");
}

void ListView_RedrawItems(HWND h, int startitem, int enditem)
{
  fprintf(stderr, "SWELL_CALL: ListView_RedrawItems\n");
}

void ListView_SetItemCount(HWND h, int cnt)
{
  fprintf(stderr, "SWELL_CALL: ListView_SetItemCount\n");
}

void ListView_EnsureVisible(HWND h, int i, BOOL pok)
{
  fprintf(stderr, "SWELL_CALL: ListView_EnsureVisible\n");
}

void ListView_SetImageList(HWND h, HIMAGELIST imagelist, int which)
{
  fprintf(stderr, "SWELL_CALL: ListView_SetImageList\n");
}

int ListView_SubItemHitTest(HWND h, LVHITTESTINFO *pinf)
{
  fprintf(stderr, "SWELL_CALL: ListView_SubItemHitTest\n");
  return -1;
}

void ListView_GetItemText(HWND hwnd, int item, int subitem, char *text, int textmax)
{
  fprintf(stderr, "SWELL_CALL: ListView_GetItemText\n");
}

void ListView_SortItems(HWND hwnd, PFNLVCOMPARE compf, LPARAM parm)
{
  fprintf(stderr, "SWELL_CALL: ListView_SortItems\n");
}

bool ListView_Scroll(HWND h, int xscroll, int yscroll)
{
  fprintf(stderr, "SWELL_CALL: ListView_Scroll\n");
  return false;
}

int ListView_GetTopIndex(HWND h)
{
  fprintf(stderr, "SWELL_CALL: ListView_GetTopIndex\n");
  return 0;
}

int ListView_GetCountPerPage(HWND h)
{
  fprintf(stderr, "SWELL_CALL: ListView_GetCountPerPage\n");
  return 0;
}

bool ListView_GetItemRect(HWND h, int item, RECT *r, int code)
{
  fprintf(stderr, "SWELL_CALL: ListView_GetItemRect\n");
  return false;
}

bool ListView_GetSubItemRect(HWND h, int item, int subitem, int code, RECT *r)
{
  fprintf(stderr, "SWELL_CALL: ListView_GetSubItemRect\n");
  return false;
}

int ListView_HitTest(HWND h, LVHITTESTINFO *pinf)
{
  fprintf(stderr, "SWELL_CALL: ListView_HitTest\n");
  return -1;
}

BOOL ListView_SetColumnOrderArray(HWND h, int cnt, int *arr)
{
  fprintf(stderr, "SWELL_CALL: ListView_SetColumnOrderArray\n");
  return FALSE;
}

BOOL ListView_GetColumnOrderArray(HWND h, int cnt, int *arr)
{
  fprintf(stderr, "SWELL_CALL: ListView_GetColumnOrderArray\n");
  return FALSE;
}

HWND ListView_GetHeader(HWND h)
{
  fprintf(stderr, "SWELL_CALL: ListView_GetHeader\n");
  return NULL;
}

int Header_GetItemCount(HWND h)
{
  fprintf(stderr, "SWELL_CALL: Header_GetItemCount\n");
  return 0;
}

BOOL Header_GetItem(HWND h, int col, HDITEM *hi)
{
  fprintf(stderr, "SWELL_CALL: Header_GetItem\n");
  return FALSE;
}

BOOL Header_SetItem(HWND h, int col, HDITEM *hi)
{
  fprintf(stderr, "SWELL_CALL: Header_SetItem\n");
  return FALSE;
}

int SWELL_GetListViewHeaderHeight(HWND h)
{
  fprintf(stderr, "SWELL_CALL: SWELL_GetListViewHeaderHeight\n");
  return 0;
}

void SWELL_SetListViewFastClickMask(HWND hList, int mask)
{
  fprintf(stderr, "SWELL_CALL: SWELL_SetListViewFastClickMask\n");
}

void ListView_SetBkColor(HWND hwnd, int color)
{
  fprintf(stderr, "SWELL_CALL: ListView_SetBkColor\n");
}

void ListView_SetTextBkColor(HWND hwnd, int color)
{
  fprintf(stderr, "SWELL_CALL: ListView_SetTextBkColor\n");
}

void ListView_SetTextColor(HWND hwnd, int color)
{
  fprintf(stderr, "SWELL_CALL: ListView_SetTextColor\n");
}

void ListView_SetGridColor(HWND hwnd, int color)
{
  fprintf(stderr, "SWELL_CALL: ListView_SetGridColor\n");
}

void ListView_SetSelColors(HWND hwnd, int *colors, int ncolors)
{
  fprintf(stderr, "SWELL_CALL: ListView_SetSelColors\n");
}

// ============================================================================
// TreeView helpers
// ============================================================================

HTREEITEM TreeView_InsertItem(HWND hwnd, TV_INSERTSTRUCT *ins)
{
  fprintf(stderr, "SWELL_CALL: TreeView_InsertItem\n");
  return NULL;
}

BOOL TreeView_Expand(HWND hwnd, HTREEITEM item, UINT flag)
{
  fprintf(stderr, "SWELL_CALL: TreeView_Expand\n");
  return FALSE;
}

HTREEITEM TreeView_GetSelection(HWND hwnd)
{
  fprintf(stderr, "SWELL_CALL: TreeView_GetSelection\n");
  return NULL;
}

void TreeView_DeleteItem(HWND hwnd, HTREEITEM item)
{
  fprintf(stderr, "SWELL_CALL: TreeView_DeleteItem\n");
}

void TreeView_DeleteAllItems(HWND hwnd)
{
  fprintf(stderr, "SWELL_CALL: TreeView_DeleteAllItems\n");
}

void TreeView_SelectItem(HWND hwnd, HTREEITEM item)
{
  fprintf(stderr, "SWELL_CALL: TreeView_SelectItem\n");
}

void TreeView_EnsureVisible(HWND hwnd, HTREEITEM item)
{
  fprintf(stderr, "SWELL_CALL: TreeView_EnsureVisible\n");
}

BOOL TreeView_GetItem(HWND hwnd, LPTVITEM pitem)
{
  fprintf(stderr, "SWELL_CALL: TreeView_GetItem\n");
  return FALSE;
}

BOOL TreeView_SetItem(HWND hwnd, LPTVITEM pitem)
{
  fprintf(stderr, "SWELL_CALL: TreeView_SetItem\n");
  return FALSE;
}

HTREEITEM TreeView_HitTest(HWND hwnd, TVHITTESTINFO *hti)
{
  fprintf(stderr, "SWELL_CALL: TreeView_HitTest\n");
  return NULL;
}

BOOL TreeView_SetIndent(HWND hwnd, int indent)
{
  fprintf(stderr, "SWELL_CALL: TreeView_SetIndent\n");
  return FALSE;
}

HTREEITEM TreeView_GetParent(HWND hwnd, HTREEITEM item)
{
  fprintf(stderr, "SWELL_CALL: TreeView_GetParent\n");
  return NULL;
}

HTREEITEM TreeView_GetChild(HWND hwnd, HTREEITEM item)
{
  fprintf(stderr, "SWELL_CALL: TreeView_GetChild\n");
  return NULL;
}

HTREEITEM TreeView_GetNextSibling(HWND hwnd, HTREEITEM item)
{
  fprintf(stderr, "SWELL_CALL: TreeView_GetNextSibling\n");
  return NULL;
}

HTREEITEM TreeView_GetRoot(HWND hwnd)
{
  fprintf(stderr, "SWELL_CALL: TreeView_GetRoot\n");
  return NULL;
}

void TreeView_SetBkColor(HWND hwnd, int color)
{
  fprintf(stderr, "SWELL_CALL: TreeView_SetBkColor\n");
}

void TreeView_SetTextColor(HWND hwnd, int color)
{
  fprintf(stderr, "SWELL_CALL: TreeView_SetTextColor\n");
}

// ============================================================================
// Tab helpers
// ============================================================================

int TabCtrl_GetItemCount(HWND hwnd)
{
  fprintf(stderr, "SWELL_CALL: TabCtrl_GetItemCount\n");
  return 0;
}

BOOL TabCtrl_DeleteItem(HWND hwnd, int idx)
{
  fprintf(stderr, "SWELL_CALL: TabCtrl_DeleteItem\n");
  return FALSE;
}

int TabCtrl_InsertItem(HWND hwnd, int idx, TCITEM *item)
{
  fprintf(stderr, "SWELL_CALL: TabCtrl_InsertItem\n");
  return -1;
}

int TabCtrl_SetCurSel(HWND hwnd, int idx)
{
  fprintf(stderr, "SWELL_CALL: TabCtrl_SetCurSel\n");
  return -1;
}

int TabCtrl_GetCurSel(HWND hwnd)
{
  fprintf(stderr, "SWELL_CALL: TabCtrl_GetCurSel\n");
  return -1;
}

BOOL TabCtrl_AdjustRect(HWND hwnd, BOOL fLarger, RECT *r)
{
  fprintf(stderr, "SWELL_CALL: TabCtrl_AdjustRect\n");
  return FALSE;
}

// ============================================================================
// ImageList
// ============================================================================

HIMAGELIST ImageList_CreateEx()
{
  fprintf(stderr, "SWELL_CALL: ImageList_CreateEx\n");
  return NULL;
}

BOOL ImageList_Remove(HIMAGELIST list, int idx)
{
  fprintf(stderr, "SWELL_CALL: ImageList_Remove\n");
  return FALSE;
}

int ImageList_ReplaceIcon(HIMAGELIST list, int offset, HICON image)
{
  fprintf(stderr, "SWELL_CALL: ImageList_ReplaceIcon\n");
  return -1;
}

int ImageList_Add(HIMAGELIST list, HBITMAP image, HBITMAP mask)
{
  fprintf(stderr, "SWELL_CALL: ImageList_Add\n");
  return -1;
}

void ImageList_Destroy(HIMAGELIST list)
{
  fprintf(stderr, "SWELL_CALL: ImageList_Destroy\n");
}

// ============================================================================
// Drag-drop
// ============================================================================

BOOL DragQueryPoint(HDROP hDrop, LPPOINT pt)
{
  fprintf(stderr, "SWELL_CALL: DragQueryPoint\n");
  return FALSE;
}

void DragFinish(HDROP hDrop)
{
  fprintf(stderr, "SWELL_CALL: DragFinish\n");
}

UINT DragQueryFile(HDROP hDrop, UINT iFile, char *buf, UINT cb)
{
  fprintf(stderr, "SWELL_CALL: DragQueryFile\n");
  return 0;
}

void SWELL_InitiateDragDrop(HWND hwnd, RECT *srcrect, const char *srcfn, void (*callback)(const char *droppath))
{
}

void SWELL_InitiateDragDropOfFileList(HWND hwnd, RECT *srcrect, const char **srclist, int srccount, HICON icon)
{
  fprintf(stderr, "SWELL_CALL: SWELL_InitiateDragDropOfFileList\n");
}

void SWELL_FinishDragDrop()
{
  fprintf(stderr, "SWELL_CALL: SWELL_FinishDragDrop\n");
}

// ============================================================================
// Monitors
// ============================================================================

BOOL EnumDisplayMonitors(HDC hdc, const LPRECT r, MONITORENUMPROC proc, LPARAM lp)
{
  fprintf(stderr, "SWELL_CALL: EnumDisplayMonitors\n");
  (void)hdc; (void)r;
  if (!proc) return FALSE;
  RECT rc = {0, 0, 1920, 1080};
  HMONITOR hmon = (HMONITOR)1;
  proc(hmon, (HDC)NULL, &rc, lp);
  return TRUE;
}

BOOL GetMonitorInfo(HMONITOR hMonitor, void *info)
{
  fprintf(stderr, "SWELL_CALL: GetMonitorInfo\n");
  return FALSE;
}

int GetSystemMetrics(int idx)
{
  fprintf(stderr, "SWELL_CALL: GetSystemMetrics\n");
  switch (idx) {
    case SM_CXSCREEN: return 1920;
    case SM_CYSCREEN: return 1080;
    case SM_CXVSCROLL: return 16;
    case SM_CYHSCROLL: return 16;
    case SM_CYCAPTION: return 23;
    case SM_CXBORDER: return 1;
    case SM_CYBORDER: return 1;
    case SM_CXDLGFRAME: return 3;
    case SM_CYDLGFRAME: return 3;
    case SM_CXICON: return 32;
    case SM_CYICON: return 32;
    case SM_CXCURSOR: return 32;
    case SM_CYCURSOR: return 32;
    case SM_CYMENU: return 20;
    case SM_CXFULLSCREEN: return 1920;
    case SM_CYFULLSCREEN: return 1080;
    case SM_CXMIN: return 120;
    case SM_CYMIN: return 40;
    case SM_CXSIZE: return 120;
    case SM_CYSIZE: return 40;
    case SM_CXFRAME: return 4;
    case SM_CYFRAME: return 4;
    default: return 0;
  }
}

// ============================================================================
// Extended API / Misc
// ============================================================================

void *SWELL_ExtendedAPI(const char *key, void *v)
{
  fprintf(stderr, "SWELL_CALL: SWELL_ExtendedAPI\n");
  return NULL;
}

unsigned int _controlfp(unsigned int flag, unsigned int mask)
{
  fprintf(stderr, "SWELL_CALL: _controlfp\n");
  return 0;
}

void SWELL_HideApp()
{
  fprintf(stderr, "SWELL_CALL: SWELL_HideApp\n");
}

BOOL SWELL_GetGestureInfo(LPARAM lParam, GESTUREINFO *gi)
{
  fprintf(stderr, "SWELL_CALL: SWELL_GetGestureInfo\n");
  return FALSE;
}

bool SWELL_ChooseColor(HWND hwnd, COLORREF *color, int ncustom, COLORREF *custom)
{
  fprintf(stderr, "SWELL_CALL: SWELL_ChooseColor\n");
  return false;
}

bool SWELL_ChooseFont(HWND hwnd, LOGFONT *lf)
{
  fprintf(stderr, "SWELL_CALL: SWELL_ChooseFont\n");
  return false;
}

void SWELL_Register_Cursor_Resource(const char *idx, const char *name, int hotspot_x, int hotspot_y)
{
  fprintf(stderr, "SWELL_CALL: SWELL_Register_Cursor_Resource\n");
}

HCURSOR SWELL_LoadCursorFromFile(const char *fn)
{
  fprintf(stderr, "SWELL_CALL: SWELL_LoadCursorFromFile\n");
  return NULL;
}

// ============================================================================
// macOS-only functions
// ============================================================================

#ifdef SWELL_TARGET_OSX

int SWELL_TerminateProcess(HANDLE hand)
{
  fprintf(stderr, "SWELL_CALL: SWELL_TerminateProcess\n");
  return 0;
}

HANDLE SWELL_CreateProcessIO(const char *exe, int nparams, const char **params, bool redirectIO)
{
  fprintf(stderr, "SWELL_CALL: SWELL_CreateProcessIO\n");
  return NULL;
}

int SWELL_ReadWriteProcessIO(HANDLE hand, int w, char *buf, int bufsz)
{
  fprintf(stderr, "SWELL_CALL: SWELL_ReadWriteProcessIO\n");
  return 0;
}

void SWELL_EnsureMultithreadedCocoa()
{
  fprintf(stderr, "SWELL_CALL: SWELL_EnsureMultithreadedCocoa\n");
}

void *SWELL_InitAutoRelease()
{
  fprintf(stderr, "SWELL_CALL: SWELL_InitAutoRelease\n");
  return NULL;
}

void SWELL_QuitAutoRelease(void *p)
{
  fprintf(stderr, "SWELL_CALL: SWELL_QuitAutoRelease\n");
}

void SWELL_PostQuitMessage(void *sender)
{
  fprintf(stderr, "SWELL_CALL: SWELL_PostQuitMessage\n");
}

bool SWELL_osx_is_dark_mode(int mode)
{
  fprintf(stderr, "SWELL_CALL: SWELL_osx_is_dark_mode\n");
  return false;
}

void SWELL_SetWindowRepre(HWND hwnd, const char *fn, bool isDirty)
{
  fprintf(stderr, "SWELL_CALL: SWELL_SetWindowRepre\n");
}

int SWELL_IsRetinaDC(HDC hdc)
{
  fprintf(stderr, "SWELL_CALL: SWELL_IsRetinaDC\n");
  return 0;
}

int SWELL_IsRetinaHWND(HWND h)
{
  fprintf(stderr, "SWELL_CALL: SWELL_IsRetinaHWND\n");
  return 0;
}

void SWELL_SetNoMultiMonitorAutoSize(HWND h, bool noauto)
{
  fprintf(stderr, "SWELL_CALL: SWELL_SetNoMultiMonitorAutoSize\n");
}

void SWELL_FlushWindow(HWND hwnd)
{
  fprintf(stderr, "SWELL_CALL: SWELL_FlushWindow\n");
}

void SWELL_DisableAppNap(int disable)
{
  fprintf(stderr, "SWELL_CALL: SWELL_DisableAppNap\n");
}

void SWELL_DisableAppNapEx(int disable, int flag)
{
  fprintf(stderr, "SWELL_CALL: SWELL_DisableAppNapEx\n");
}

int SWELL_GetOSXVersion()
{
  fprintf(stderr, "SWELL_CALL: SWELL_GetOSXVersion\n");
  return 0;
}

int SWELL_EnableMetal(HWND h, int mode)
{
  fprintf(stderr, "SWELL_CALL: SWELL_EnableMetal\n");
  return 0;
}

int SWELL_MacKeyToWindowsKey(void *nsevent, int *flags)
{
  fprintf(stderr, "SWELL_CALL: SWELL_MacKeyToWindowsKey\n");
  return 0;
}

int SWELL_MacKeyToWindowsKeyEx(void *nsevent, int *flags, int mode)
{
  fprintf(stderr, "SWELL_CALL: SWELL_MacKeyToWindowsKeyEx\n");
  return 0;
}

// --- Combobox macOS helpers ---

int SWELL_CB_AddString(HWND hwnd, int idx, const char *str)
{
  fprintf(stderr, "SWELL_CALL: SWELL_CB_AddString\n");
  return -1;
}

void SWELL_CB_SetCurSel(HWND hwnd, int idx, int sel)
{
  fprintf(stderr, "SWELL_CALL: SWELL_CB_SetCurSel\n");
}

int SWELL_CB_GetCurSel(HWND hwnd, int idx)
{
  fprintf(stderr, "SWELL_CALL: SWELL_CB_GetCurSel\n");
  return -1;
}

int SWELL_CB_GetNumItems(HWND hwnd, int idx)
{
  fprintf(stderr, "SWELL_CALL: SWELL_CB_GetNumItems\n");
  return 0;
}

void SWELL_CB_SetItemData(HWND hwnd, int idx, int item, LONG_PTR data)
{
  fprintf(stderr, "SWELL_CALL: SWELL_CB_SetItemData\n");
}

LONG_PTR SWELL_CB_GetItemData(HWND hwnd, int idx, int item)
{
  fprintf(stderr, "SWELL_CALL: SWELL_CB_GetItemData\n");
  return 0;
}

void SWELL_CB_Empty(HWND hwnd, int idx)
{
  fprintf(stderr, "SWELL_CALL: SWELL_CB_Empty\n");
}

int SWELL_CB_InsertString(HWND hwnd, int idx, int pos, const char *str)
{
  fprintf(stderr, "SWELL_CALL: SWELL_CB_InsertString\n");
  return -1;
}

int SWELL_CB_GetItemText(HWND hwnd, int idx, int item, char *buf, int bufsz)
{
  fprintf(stderr, "SWELL_CALL: SWELL_CB_GetItemText\n");
  return 0;
}

void SWELL_CB_DeleteString(HWND hwnd, int idx, int wh)
{
  fprintf(stderr, "SWELL_CALL: SWELL_CB_DeleteString\n");
}

int SWELL_CB_FindString(HWND hwnd, int idx, int startAfter, const char *str, bool exact)
{
  fprintf(stderr, "SWELL_CALL: SWELL_CB_FindString\n");
  return -1;
}

// --- Trackbar macOS helpers ---

void SWELL_TB_SetPos(HWND hwnd, int idx, int pos)
{
  fprintf(stderr, "SWELL_CALL: SWELL_TB_SetPos\n");
}

void SWELL_TB_SetRange(HWND hwnd, int idx, int low, int hi)
{
  fprintf(stderr, "SWELL_CALL: SWELL_TB_SetRange\n");
}

int SWELL_TB_GetPos(HWND hwnd, int idx)
{
  fprintf(stderr, "SWELL_CALL: SWELL_TB_GetPos\n");
  return 0;
}

void SWELL_TB_SetTic(HWND hwnd, int idx, int pos)
{
  fprintf(stderr, "SWELL_CALL: SWELL_TB_SetTic\n");
}

// --- CFString/NSString helpers ---

void *SWELL_CStringToCFString(const char *str)
{
  fprintf(stderr, "SWELL_CALL: SWELL_CStringToCFString\n");
  return NULL;
}

void SWELL_CFStringToCString(const void *str, char *buf, int buflen)
{
  fprintf(stderr, "SWELL_CALL: SWELL_CFStringToCString\n");
}

// --- Icon ---

void *GetNSImageFromHICON(HICON icon)
{
  fprintf(stderr, "SWELL_CALL: GetNSImageFromHICON\n");
  return NULL;
}

#endif // SWELL_TARGET_OSX

// SWELL_SetMenuDestination is declared unconditionally but marked macOS-only in docs
void SWELL_SetMenuDestination(HMENU menu, HWND hwnd)
{
  fprintf(stderr, "SWELL_CALL: SWELL_SetMenuDestination\n");
}

// ============================================================================
// Non-OSX only
// ============================================================================

#ifndef SWELL_TARGET_OSX

HANDLE SWELL_CreateProcessFromPID(int pid)
{
  fprintf(stderr, "SWELL_CALL: SWELL_CreateProcessFromPID\n");
  return NULL;
}

#endif // !SWELL_TARGET_OSX
