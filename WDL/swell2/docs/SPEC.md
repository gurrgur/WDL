# SWELL API Specification

**SWELL** — Simple/Small Win32 Emulation Layer  
Version: derived from Cockos swell (2006–2026)  
Target: Linux/macOS, generic (headless) and SDL3 backends

This document is a complete, normative specification of the SWELL API surface,
intended as the sole reference for a clean reimplementation. Everything the
original implementation does is described here; divergences from Win32 are
explicitly noted.

---

## 1. Build Configuration

| Macro | Meaning |
|---|---|
| `SWELL_TARGET_OSX` | macOS (Cocoa) backend |
| `SWELL_TARGET_OSX_COCOA` | Cocoa (set together with above) |
| `SWELL_TARGET_SDL3` | SDL3 backend |
| `SWELL_LICE_GDI` | Use LICE for GDI rendering (generic) |
| `SWELL_FORCE_GENERIC` | Force generic backend even on macOS |
| `SWELL_PROVIDED_BY_APP` | API is function-pointer table, loaded at runtime |
| `SWELL_LOAD_SWELL_DYLIB` | Synonym for above |
| `SWELL_USE_WIN32_RGB` | RGB byte order matches Win32 (B,G,R). Default is SWELL order (R,G,B) |

When `SWELL_PROVIDED_BY_APP` or `SWELL_LOAD_SWELL_DYLIB` is defined, every
`SWELL_API_DEFINE(ret, func, parms)` entry becomes an `extern` function pointer
instead of a direct function declaration.

---

## 2. Core Types (`swell-types.h`)

### 2.1 Integer types

```c
typedef intptr_t  INT_PTR, LONG_PTR;
typedef uintptr_t UINT_PTR, ULONG_PTR, DWORD_PTR;

typedef signed char   BOOL;   // NOT objc BOOL
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned int  DWORD;
typedef unsigned int  UINT;
typedef signed int    INT, LONG, HRESULT;
typedef unsigned int  ULONG;
typedef short         SHORT;
typedef DWORD         COLORREF;
typedef ULONG_PTR     WPARAM;
typedef LONG_PTR      LPARAM, LRESULT;
typedef unsigned long long ULONGLONG;
```

`BOOL` is `signed char` (not `int`). TRUE=1, FALSE=0.  
`LONG` is 32-bit even on 64-bit platforms.

### 2.2 Handle types

```c
typedef struct HWND__   *HWND;
typedef struct HMENU__  *HMENU;
typedef struct HDC__    *HDC;
typedef struct HCURSOR__*HCURSOR;
typedef struct HRGN__   *HRGN;
typedef struct HGDIOBJ__*HGDIOBJ, *HBITMAP, *HICON, *HBRUSH, *HPEN, *HFONT;
typedef struct HIMAGELIST__ *HIMAGELIST;
typedef struct HTREEITEM__  *HTREEITEM;
typedef void *HANDLE, *HINSTANCE, *HDROP, *HGLOBAL, *HMONITOR;
```

All GDI object types (`HBITMAP`, `HICON`, `HBRUSH`, `HPEN`, `HFONT`) are the
same underlying pointer type `HGDIOBJ__*`. They may be freely cast to `HGDIOBJ`
and passed to `DeleteObject()`.

### 2.3 Callback types

```c
typedef INT_PTR (*DLGPROC)(HWND, UINT, WPARAM, LPARAM);
typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef void    (*TIMERPROC)(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
typedef BOOL    (*PROPENUMPROCEX)(HWND hwnd, const char *lpszString, HANDLE hData, LPARAM lParam);
typedef HWND    (*SWELL_ControlCreatorProc)(HWND parent, const char *cname, int idx,
                    const char *classname, int style, int x, int y, int w, int h);
typedef int     (*PFNLVCOMPARE)(LPARAM, LPARAM, LPARAM);
typedef BOOL    (*MONITORENUMPROC)(HMONITOR, HDC, LPRECT, LPARAM);
```

### 2.4 Structs

```c
typedef struct { LONG x, y; } POINT, *LPPOINT;
typedef struct { SHORT x, y; } POINTS;
typedef struct { LONG left, top, right, bottom; } RECT, *LPRECT;

typedef struct {
  HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt;
} MSG, *LPMSG;

typedef struct {
  HWND hwndFrom; UINT_PTR idFrom; UINT code;
} NMHDR, *LPNMHDR;

typedef struct {
  NMHDR hdr; DWORD_PTR dwItemSpec; DWORD_PTR dwItemData; POINT pt; DWORD dwHitInfo;
} NMMOUSE, NMCLICK, *LPNMMOUSE, *LPNMCLICK;

typedef struct { HDC hdc; BOOL fErase; RECT rcPaint; } PAINTSTRUCT;

typedef struct {
  int lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
  char lfItalic, lfUnderline, lfStrikeOut, lfCharSet,
       lfOutPrecision, lfClipPrecision, lfQuality, lfPitchAndFamily;
  char lfFaceName[32];
} LOGFONT;

typedef struct {
  LONG tmHeight, tmAscent, tmDescent, tmInternalLeading, tmAveCharWidth;
} TEXTMETRIC;

typedef struct {
  UINT cbSize, fMask;
  int nMin, nMax;
  UINT nPage;
  int nPos, nTrackPos;
} SCROLLINFO, *LPSCROLLINFO;

typedef struct {
  HWND hwnd; HWND hwndInsertAfter; int x, y, cx, cy; UINT flags;
} WINDOWPOS, *LPWINDOWPOS;

typedef struct {
  RECT rgrc[3]; PWINDOWPOS lppos;
} NCCALCSIZE_PARAMS, *LPNCCALCSIZE_PARAMS;

typedef struct {
  POINT ptReserved, ptMaxSize, ptMaxPosition, ptMinTrackSize, ptMaxTrackSize;
} MINMAXINFO, *LPMINMAXINFO;

typedef struct {
  unsigned char fVirt; unsigned short key, cmd;
} ACCEL, *LPACCEL;

typedef struct { DWORD dwLowDateTime, dwHighDateTime; } FILETIME;

typedef struct _GUID {
  unsigned int Data1; unsigned short Data2, Data3; unsigned char Data4[8];
} GUID;

typedef struct {
  DWORD cbSize; HWND hWnd; UINT uID, uFlags, uCallbackMessage;
  HICON hIcon; CHAR szTip[64];
} NOTIFYICONDATA, *PNOTIFYICONDATA;

typedef struct _ICONINFO {
  BOOL fIcon; DWORD xHotspot, yHotspot; HBITMAP hbmMask, hbmColor;
} ICONINFO, *PICONINFO;

typedef struct _COPYDATASTRUCT {
  ULONG_PTR dwData; DWORD cbData; PVOID lpData;
} COPYDATASTRUCT, *PCOPYDATASTRUCT;

typedef struct _DROPFILES {
  DWORD pFiles; POINT pt; BOOL fNC, fWide;
} DROPFILES, *LPDROPFILES;

typedef struct _MONITORINFO {
  DWORD cbSize; RECT rcMonitor, rcWork; DWORD dwFlags;
} MONITORINFO, *LPMONITORINFO;

typedef struct _MONITORINFOEX {
  DWORD cbSize; RECT rcMonitor, rcWork; DWORD dwFlags; char szDevice[256];
} MONITORINFOEX, *LPMONITORINFOEX;

typedef struct {
  UINT CtlType, CtlID, itemID, itemAction, itemState;
  HWND hwndItem; HDC hDC; RECT rcItem; DWORD_PTR itemData;
} DRAWITEMSTRUCT, *LPDRAWITEMSTRUCT;

typedef struct tagBITMAP {
  LONG bmWidth, bmHeight, bmWidthBytes; WORD bmPlanes, bmBitsPixel; LPVOID bmBits;
} BITMAP, *LPBITMAP;

typedef struct {
  DWORD styleOld, styleNew;
} STYLESTRUCT, *LPSTYLESTRUCT;
```

#### Gesture types

```c
typedef struct tagGESTUREINFO {
  UINT cbSize; DWORD dwFlags, dwID; HWND hwndTarget; POINTS ptsLocation;
  DWORD dwInstanceID, dwSequenceID; ULONGLONG ullArguments; UINT cbExtraArgs;
} GESTUREINFO;

typedef struct tagGESTURECONFIG {
  DWORD dwID, dwWant, dwBlock;
} GESTURECONFIG;
```

#### ListView types

```c
typedef struct {
  int mask, fmt, cx; char *pszText; int cchTextMax, iSubItem;
} LVCOLUMN;

typedef struct {
  int mask, iItem, iSubItem, state, stateMask;
  char *pszText; int cchTextMax, iImage; LPARAM lParam;
} LVITEM;

typedef struct {
  POINT pt; UINT flags; int iItem, iSubItem;
} LVHITTESTINFO, *LPLVHITTESTINFO;

typedef struct {
  NMHDR hdr; int iItem, iSubItem; UINT uNewState, uOldState, uChanged;
  POINT ptAction; LPARAM lParam;
} NMLISTVIEW, *LPNMLISTVIEW;

typedef struct {
  NMHDR hdr; LVITEM item;
} NMLVDISPINFO, *LPNMLVDISPINFO;

typedef struct {
  struct { NMHDR hdr; DWORD dwDrawStage; HDC hdc; RECT rc;
           DWORD dwItemSpec; UINT uItemState; LPARAM lItemlParam; } nmcd;
  COLORREF clrText, clrTextBk; int iSubItem;
} NMLVCUSTOMDRAW, *LPNMLVCUSTOMDRAW;

typedef struct {
  UINT mask; int cxy; char *pszText; HBITMAP hbm;
  int cchTextMax, fmt; LPARAM lParam; int iImage, iOrder;
  UINT type; void *pvFilter; UINT state;
} HDITEM, *LPHDITEM;
```

#### TreeView types

```c
typedef struct {
  UINT mask; HTREEITEM hItem; UINT state, stateMask;
  char *pszText; int cchTextMax, iImage, iSelectedImage, cChildren; LPARAM lParam;
} TVITEM, TV_ITEM, *LPTVITEM;

typedef struct {
  HTREEITEM hParent, hInsertAfter; TVITEM item;
} TVINSERTSTRUCT, TV_INSERTSTRUCT, *LPTVINSERTSTRUCT;

typedef struct {
  POINT pt; UINT flags; HTREEITEM hItem;
} TVHITTESTINFO, *LPTVHITTESTINFO;

typedef struct {
  NMHDR hdr; UINT action; TVITEM itemOld, itemNew; POINT ptDrag;
} NMTREEVIEW, *LPNMTREEVIEW;
```

#### Tab control

```c
typedef struct {
  UINT mask; DWORD dwState, dwStateMask;
  char *pszText; int cchTextMax, iImage; LPARAM lParam;
} TCITEM, *LPTCITEM;
```

#### Menu item

```c
typedef struct {
  unsigned int cbSize, fMask, fType, fState, wID;
  HMENU hSubMenu; HICON hbmpChecked, hbmpUnchecked;
  DWORD_PTR dwItemData; char *dwTypeData; int cch; HBITMAP hbmpItem;
} MENUITEMINFO;
```

---

## 3. Constants

### 3.1 Window styles

```c
WS_CHILD        = 0x40000000
WS_CHILDWINDOW  = WS_CHILD
WS_DISABLED     = 0x08000000
WS_CLIPSIBLINGS = 0x04000000
WS_VISIBLE      = 0x02000000   // read-only from GetWindowLong
WS_CAPTION      = 0x00C00000
WS_VSCROLL      = 0x00200000
WS_HSCROLL      = 0x00100000
WS_SYSMENU      = 0x00080000
WS_THICKFRAME   = 0x00040000
WS_GROUP        = 0x00020000
WS_TABSTOP      = 0x00010000
WS_BORDER       = 0              // ignored
WS_EX_LEFTSCROLLBAR = 0x00004000
WS_EX_ACCEPTFILES   = 0x00000010
```

### 3.2 GetWindowLong indices

```c
GWL_HWNDPARENT = -25
GWL_USERDATA   = -21
GWL_ID         = -12
GWL_STYLE      = -16   // only BS_ flags supported
GWL_EXSTYLE    = -20
GWL_WNDPROC    = -4
DWL_DLGPROC    = -8
```

Non-negative indices 0..63 (generic) or 0..31 (macOS) are extra per-window data slots.

### 3.3 ShowWindow commands

**Note:** These values differ from Win32.

```c
SW_HIDE         = 0
SW_SHOWNA       = 1    // Win32: 8
SW_SHOW         = 2    // Win32: 1
SW_SHOWMINIMIZED= 3    // Win32: 2
SW_SHOWMAXIMIZED= 4
SW_RESTORE      = 5
SW_SHOWNOACTIVATE = SW_SHOWNA
SW_NORMAL = SW_SHOWNORMAL = SW_SHOWDEFAULT = SW_SHOW
```

### 3.4 SetWindowPos flags

```c
SWP_NOMOVE      = 1
SWP_NOSIZE      = 2
SWP_NOZORDER    = 4
SWP_NOACTIVATE  = 8
SWP_SHOWWINDOW  = 16
SWP_FRAMECHANGED= 32
SWP_NOCOPYBITS  = 0    // no-op

HWND_TOP     = (HWND)0
HWND_BOTTOM  = (HWND)1
HWND_TOPMOST = (HWND)-1
HWND_NOTOPMOST=(HWND)-2
```

### 3.5 Window messages

```c
WM_CREATE        = 0x0001
WM_DESTROY       = 0x0002
WM_MOVE          = 0x0003
WM_SIZE          = 0x0005
WM_ACTIVATE      = 0x0006
WM_SETFOCUS      = 0x0007
WM_KILLFOCUS     = 0x0008
WM_SETREDRAW     = 0x000B
WM_SETTEXT       = 0x000C
WM_PAINT         = 0x000F
WM_CLOSE         = 0x0010
WM_ERASEBKGND    = 0x0014
WM_SHOWWINDOW    = 0x0018
WM_ACTIVATEAPP   = 0x001C
WM_SETCURSOR     = 0x0020
WM_MOUSEACTIVATE = 0x0021
WM_GETMINMAXINFO = 0x0024
WM_DRAWITEM      = 0x002B
WM_SETFONT       = 0x0030
WM_GETFONT       = 0x0031
WM_GETOBJECT     = 0x003D
WM_COPYDATA      = 0x004A
WM_NOTIFY        = 0x004E
WM_CONTEXTMENU   = 0x007B
WM_STYLECHANGED  = 0x007D
WM_DISPLAYCHANGE = 0x007E
WM_NCDESTROY     = 0x0082
WM_NCCALCSIZE    = 0x0083
WM_NCHITTEST     = 0x0084
WM_NCPAINT       = 0x0085
WM_NCMOUSEMOVE   = 0x00A0
WM_NCLBUTTONDOWN = 0x00A1
WM_NCLBUTTONUP   = 0x00A2
WM_NCLBUTTONDBLCLK=0x00A3
WM_NCRBUTTONDOWN = 0x00A4
WM_NCRBUTTONUP   = 0x00A5
WM_NCRBUTTONDBLCLK=0x00A6
WM_NCMBUTTONDOWN = 0x00A7
WM_NCMBUTTONUP   = 0x00A8
WM_NCMBUTTONDBLCLK=0x00A9
WM_KEYDOWN       = 0x0100
WM_KEYUP         = 0x0101
WM_CHAR          = 0x0102
WM_DEADCHAR      = 0x0103
WM_SYSKEYDOWN    = 0x0104
WM_SYSKEYUP      = 0x0105
WM_SYSCHAR       = 0x0106
WM_SYSDEADCHAR   = 0x0107
WM_INITDIALOG    = 0x0110
WM_COMMAND       = 0x0111
WM_SYSCOMMAND    = 0x0112
WM_TIMER         = 0x0113
WM_HSCROLL       = 0x0114
WM_VSCROLL       = 0x0115
WM_INITMENUPOPUP = 0x0117
WM_GESTURE       = 0x0119
WM_MOUSEMOVE     = 0x0200
WM_LBUTTONDOWN   = 0x0201
WM_LBUTTONUP     = 0x0202
WM_LBUTTONDBLCLK = 0x0203
WM_RBUTTONDOWN   = 0x0204
WM_RBUTTONUP     = 0x0205
WM_RBUTTONDBLCLK = 0x0206
WM_MBUTTONDOWN   = 0x0207
WM_MBUTTONUP     = 0x0208
WM_MBUTTONDBLCLK = 0x0209
WM_MOUSEWHEEL    = 0x020A
WM_MOUSEHWHEEL   = 0x020E
WM_CAPTURECHANGED= 0x0215
WM_DROPFILES     = 0x0233
WM_SWELL_EXTENDED= 0x0399   // SWELL extension; wParam=subtype
WM_USER          = 0x0400
```

WM_CTLCOLOR* messages (for coloring controls):
```c
WM_CTLCOLORMSGBOX  = 0x0132
WM_CTLCOLOREDIT    = 0x0133
WM_CTLCOLORLISTBOX = 0x0134
WM_CTLCOLORBTN     = 0x0135
WM_CTLCOLORDLG     = 0x0136
WM_CTLCOLORSCROLLBAR=0x0137
WM_CTLCOLORSTATIC  = 0x0138
```

### 3.6 Notification codes

```c
NM_CLICK    = (NM_FIRST-2)    // uses NMCLICK
NM_DBLCLK   = (NM_FIRST-3)
NM_RCLICK   = (NM_FIRST-5)    // uses NMCLICK
NM_CUSTOMDRAW=(NM_FIRST-12)

LVN_ITEMCHANGED  = (LVN_FIRST-1)
LVN_COLUMNCLICK  = (LVN_FIRST-8)
LVN_BEGINDRAG    = (LVN_FIRST-9)
LVN_GETDISPINFO  = (LVN_FIRST-50)
LVN_ODFINDITEM   = (LVN_FIRST-52)
LVN_GETDISPINFOW = (LVN_FIRST-77)

TVN_SELCHANGED   = (TVN_FIRST-2)
TVN_ITEMEXPANDING= (TVN_FIRST-5)
TVN_BEGINDRAG    = (TVN_FIRST-7)

TCN_SELCHANGE    = (TCN_FIRST-1)
```

### 3.7 Control notification codes (WM_COMMAND HIWORD)

```c
BN_CLICKED   = 0
EN_SETFOCUS  = 0x0100
EN_KILLFOCUS = 0x0200
EN_CHANGE    = 0x0300
STN_CLICKED  = 0
STN_DBLCLK   = 1
LBN_SELCHANGE= 1
LBN_DBLCLK   = 2
CBN_SELCHANGE= 1
CBN_EDITCHANGE=5
CBN_DROPDOWN = 7
CBN_CLOSEUP  = 8
```

### 3.8 Virtual key codes

Standard Win32 VK_ codes are defined (VK_BACK, VK_TAB, VK_RETURN, VK_SHIFT,
VK_CONTROL, VK_MENU, VK_ESCAPE, VK_SPACE, VK_PRIOR–VK_DOWN, VK_INSERT,
VK_DELETE, VK_LWIN, VK_RWIN, VK_NUMPAD0–9, VK_MULTIPLY, VK_ADD, VK_SUBTRACT,
VK_DECIMAL, VK_DIVIDE, VK_F1–VK_F24, VK_NUMLOCK, VK_SCROLL).

Mouse button VKs:
```c
VK_LBUTTON = 0x01
VK_RBUTTON = 0x02
VK_MBUTTON = 0x04
```

Accelerator modifier flags:
```c
FVIRTKEY = 1
FSHIFT   = 0x04
FCONTROL = 0x08
FALT     = 0x10
FLWIN    = 0x20
```

### 3.9 Button styles

```c
BS_AUTOCHECKBOX    = 0x00000003
BS_AUTO3STATE      = 0x00000006
BS_AUTORADIOBUTTON = 0x00000009
BS_OWNERDRAW       = 0x0000000B
BS_BITMAP          = 0x00000080
BS_LEFTTEXT        = 0x0020
BS_LEFT            = 0x100
BS_CENTER          = 0x300
BS_GROUPBOX        = 0x20000000
BS_DEFPUSHBUTTON   = 0x10000000
BS_PUSHBUTTON      = 0x8000000

BST_UNCHECKED     = 0
BST_CHECKED       = 1
BST_INDETERMINATE = 2
```

### 3.10 Edit styles

```c
ES_LEFT       = 0
ES_CENTER     = 1
ES_RIGHT      = 2
ES_MULTILINE  = 4
ES_PASSWORD   = 0x0020
ES_AUTOHSCROLL= 0x0080
ES_NOHIDESEL  = 0x0100
ES_READONLY   = 0x0800
ES_WANTRETURN = 0x1000
ES_NUMBER     = 0x2000
```

### 3.11 Static styles

```c
SS_LEFT          = 0
SS_CENTER        = 0x1
SS_RIGHT         = 0x2
SS_BLACKRECT     = 0x4
SS_BLACKFRAME    = SS_BLACKRECT
SS_LEFTNOWORDWRAP= 0xC
SS_ETCHEDHORZ    = 0x10
SS_ETCHEDVERT    = 0x11
SS_ETCHEDFRAME   = 0x12
SS_TYPEMASK      = 0x1F
SS_NOPREFIX      = 0x80
SS_NOTIFY        = 0x0100
```

### 3.12 Combobox styles

```c
CBS_DROPDOWNLIST = 0x0003
CBS_DROPDOWN     = 0x0002
CBS_SORT         = 0x0100
```

### 3.13 Listbox styles

```c
LBS_SORT           = 0x0002
LBS_OWNERDRAWFIXED = 0x0010
LBS_EXTENDEDSEL    = 0x0800
```

### 3.14 ListView styles and extended styles

```c
LVS_LIST          = 0
LVS_REPORT        = 0x0001
LVS_TYPEMASK      = 0x0003
LVS_SINGLESEL     = 0x0004
LVS_SORTASCENDING = 0x0010
LVS_SORTDESCENDING= 0x0020
LVS_OWNERDATA     = 0x1000
LVS_NOCOLUMNHEADER= 0x4000
LVS_NOSORTHEADER  = 0x8000

LVS_EX_GRIDLINES     = 0x01
LVS_EX_SUBITEMIMAGES = 0x02
LVS_EX_HEADERDRAGDROP= 0x10
LVS_EX_FULLROWSELECT = 0x20

LVCF_FMT   = 1
LVCF_WIDTH = 2
LVCF_TEXT  = 4
LVCFMT_LEFT   = 0
LVCFMT_RIGHT  = 1
LVCFMT_CENTER = 2

LVIF_TEXT  = 1
LVIF_IMAGE = 2
LVIF_PARAM = 4
LVIF_STATE = 8

LVIS_SELECTED = 1
LVIS_FOCUSED  = 2
LVNI_SELECTED = 1
LVNI_FOCUSED  = 2

LVIR_BOUNDS      = 0
LVIR_ICON        = 1
LVIR_LABEL       = 2
LVIR_SELECTBOUNDS= 3

LVHT_ONITEMICON      = 0x0002
LVHT_ONITEMLABEL     = 0x0004
LVHT_ONITEMSTATEICON = 0x0008
LVHT_ONITEM = (LVHT_ONITEMICON|LVHT_ONITEMLABEL|LVHT_ONITEMSTATEICON)

CDDS_PREPAINT    = 0x00001
CDDS_ITEM        = 0x10000
CDDS_ITEMPREPAINT= (CDDS_ITEM|CDDS_PREPAINT)

LVSIL_STATE = 1
LVSIL_SMALL = 2

HDI_FORMAT  = 0x4
HDF_SORTUP  = 0x0400
HDF_SORTDOWN= 0x0200
```

### 3.15 TreeView styles/states

```c
TVS_DISABLEDRAGDROP = 0x10

TVIF_TEXT          = 0x0001
TVIF_IMAGE         = 0x0002
TVIF_PARAM         = 0x0004
TVIF_STATE         = 0x0008
TVIF_HANDLE        = 0x0010
TVIF_SELECTEDIMAGE = 0x0020
TVIF_CHILDREN      = 0x0040

TVIS_SELECTED    = 0x0002
TVIS_DROPHILITED = 0x0008
TVIS_BOLD        = 0x0010
TVIS_EXPANDED    = 0x0020

TVE_COLLAPSE = 0x0001
TVE_EXPAND   = 0x0002
TVE_TOGGLE   = 0x0003

TVI_ROOT  = (HTREEITEM)0xFFFF0000
TVI_FIRST = (HTREEITEM)0xFFFF0001
TVI_LAST  = (HTREEITEM)0xFFFF0002
TVI_SORT  = (HTREEITEM)0xFFFF0003

TVHT_ONITEMICON      = 0x0002
TVHT_ONITEMLABEL     = 0x0004
TVHT_ONITEM = (TVHT_ONITEMICON|TVHT_ONITEMLABEL|TVHT_ONITEMSTATEICON)
TVHT_ONITEMSTATEICON = 0x0040
```

### 3.16 Tab control flags

```c
TCIF_TEXT  = 0x0001
TCIF_IMAGE = 0x0002
TCIF_PARAM = 0x0008
```

### 3.17 Trackbar messages

```c
TBM_GETPOS  = WM_USER
TBM_SETTIC  = WM_USER+4
TBM_SETPOS  = WM_USER+5
TBM_SETRANGE= WM_USER+6
TBM_SETSEL  = WM_USER+10
```

### 3.18 Progress bar messages

```c
PBM_SETRANGE = WM_USER+1
PBM_SETPOS   = WM_USER+2
PBM_DELTAPOS = WM_USER+3
```

### 3.19 Combobox messages

```c
CB_ADDSTRING       = 0x0143
CB_DELETESTRING    = 0x0144
CB_GETCOUNT        = 0x0146
CB_GETCURSEL       = 0x0147
CB_GETLBTEXT       = 0x0148
CB_GETLBTEXTLEN    = 0x0149
CB_INSERTSTRING    = 0x014A
CB_RESETCONTENT    = 0x014B
CB_FINDSTRING      = 0x014C
CB_SETCURSEL       = 0x014E
CB_GETITEMDATA     = 0x0150
CB_SETITEMDATA     = 0x0151
CB_FINDSTRINGEXACT = 0x0158
CB_INITSTORAGE     = 0x0161
CB_ERR = -1
```

### 3.20 Listbox messages

**Note:** Some values differ from Win32.

```c
LB_ADDSTRING       = 0x0180
LB_INSERTSTRING    = 0x0181
LB_DELETESTRING    = 0x0182
LB_GETTEXT         = 0x0183
LB_RESETCONTENT    = 0x0184
LB_SETSEL          = 0x0185
LB_SETCURSEL       = 0x0186
LB_GETSEL          = 0x0187
LB_GETCURSEL       = 0x0188
LB_GETTEXTLEN      = 0x018A
LB_GETCOUNT        = 0x018B
LB_GETSELCOUNT     = 0x0190
LB_GETITEMDATA     = 0x0199
LB_SETITEMDATA     = 0x019A
LB_FINDSTRINGEXACT = 0x01A2
LB_ERR = -1
```

### 3.21 Button messages

```c
BM_GETCHECK = 0x00F0
BM_SETCHECK = 0x00F1
BM_GETIMAGE = 0x00F6
BM_SETIMAGE = 0x00F7
IMAGE_BITMAP = 0
IMAGE_ICON   = 1
```

### 3.22 Edit messages

```c
EM_GETSEL         = 0xF0B0
EM_SETSEL         = 0xF0B1
EM_SCROLL         = 0xF0B5
EM_REPLACESEL     = 0xF0C2
EM_SETPASSWORDCHAR= 0xF0CC
```

### 3.23 Scroll bar constants

```c
SB_HORZ = 0, SB_VERT = 1, SB_CTL = 2, SB_BOTH = 3

SB_LINEUP=SB_LINELEFT = 0
SB_LINEDOWN=SB_LINERIGHT = 1
SB_PAGEUP=SB_PAGELEFT = 2
SB_PAGEDOWN=SB_PAGERIGHT = 3
SB_THUMBPOSITION = 4
SB_THUMBTRACK    = 5
SB_TOP=SB_LEFT   = 6
SB_BOTTOM=SB_RIGHT=7
SB_ENDSCROLL     = 8

SIF_RANGE          = 0x0001
SIF_PAGE           = 0x0002
SIF_POS            = 0x0004
SIF_DISABLENOSCROLL= 0x0008
SIF_TRACKPOS       = 0x0010
SIF_ALL = SIF_RANGE|SIF_PAGE|SIF_POS|SIF_TRACKPOS
```

### 3.24 Menu flags

```c
MF_ENABLED   = 0
MF_GRAYED    = 1
MF_DISABLED  = 2
MF_STRING    = 0
MF_BITMAP    = 4
MF_UNCHECKED = 0
MF_CHECKED   = 8
MF_POPUP     = 0x10
MF_BYCOMMAND = 0
MF_BYPOSITION= 0x400
MF_SEPARATOR = 0x800

MFT_STRING    = MF_STRING
MFT_BITMAP    = MF_BITMAP
MFT_SEPARATOR = MF_SEPARATOR
MFT_RADIOCHECK= 0x200

MFS_GRAYED   = MF_GRAYED|MF_DISABLED
MFS_DISABLED = MFS_GRAYED
MFS_CHECKED  = MF_CHECKED
MFS_ENABLED  = MF_ENABLED
MFS_UNCHECKED= MF_UNCHECKED

MIIM_ID      = 1
MIIM_STATE   = 2
MIIM_TYPE    = 4
MIIM_SUBMENU = 8
MIIM_DATA    = 16
MIIM_BITMAP  = 0x80
```

### 3.25 TrackPopupMenu flags

```c
TPM_LEFTBUTTON  = 0x0000
TPM_RIGHTBUTTON = 0x0002
TPM_LEFTALIGN   = 0x0000
TPM_CENTERALIGN = 0x0004
TPM_RIGHTALIGN  = 0x0008
TPM_TOPALIGN    = 0x0000
TPM_VCENTERALIGN= 0x0010
TPM_BOTTOMALIGN = 0x0020
TPM_HORIZONTAL  = 0x0000
TPM_VERTICAL    = 0x0040
TPM_NONOTIFY    = 0x0080   // implemented
TPM_RETURNCMD   = 0x0100   // implemented
```

Most alignment flags are ignored. `TPM_NONOTIFY` suppresses WM_COMMAND.
`TPM_RETURNCMD` returns the selected item ID from `TrackPopupMenu` instead
of posting WM_COMMAND.

### 3.26 DrawText flags

```c
DT_TOP       = 0
DT_LEFT      = 0
DT_CENTER    = 1
DT_RIGHT     = 2
DT_VCENTER   = 4
DT_BOTTOM    = 8
DT_WORDBREAK = 0x10
DT_SINGLELINE= 0x20
DT_NOCLIP    = 0x100
DT_CALCRECT  = 0x400
DT_NOPREFIX  = 0x800
DT_END_ELLIPSIS=0x8000
```

### 3.27 GDI / colors

```c
SRCCOPY            = 0
SRCCOPY_USEALPHACHAN=0xdeadbeef  // SWELL extension: use alpha channel
PS_SOLID = 0
NULL_PEN   = 1
NULL_BRUSH = 2
TRANSPARENT = 0
OPAQUE      = 1
PATCOPY     = (DWORD)0x00F00021

COLOR_3DSHADOW  = 0
COLOR_3DHILIGHT = 1
COLOR_3DFACE    = 2
COLOR_BTNTEXT   = 3
COLOR_WINDOW    = 4
COLOR_SCROLLBAR = 5
COLOR_3DDKSHADOW= 6
COLOR_BTNFACE   = 7
COLOR_INFOBK    = 8
COLOR_INFOTEXT  = 9
```

### 3.28 Font constants

```c
FW_DONTCARE=0, FW_THIN=100, FW_EXTRALIGHT=200, FW_LIGHT=300,
FW_NORMAL=400, FW_MEDIUM=500, FW_SEMIBOLD=600, FW_BOLD=700,
FW_EXTRABOLD=800, FW_HEAVY=900

OUT_DEFAULT_PRECIS=0, CLIP_DEFAULT_PRECIS=0
DEFAULT_QUALITY=0, DRAFT_QUALITY=1, PROOF_QUALITY=2
NONANTIALIASED_QUALITY=3, ANTIALIASED_QUALITY=4
DEFAULT_PITCH=0, DEFAULT_CHARSET=0, ANSI_CHARSET=0
```

### 3.29 System metrics

```c
SM_CXSCREEN  = 0
SM_CYSCREEN  = 1
SM_CXVSCROLL = 2
SM_CYHSCROLL = 3
SM_CYMENU    = 15
SM_CYVSCROLL = 20
SM_CXHSCROLL = 21
```

### 3.30 Size message constants

```c
SIZE_RESTORED  = 0
SIZE_MINIMIZED = 1
SIZE_MAXIMIZED = 2
SIZE_MAXSHOW   = 3
SIZE_MAXHIDE   = 4
```

### 3.31 Hit-test constants

```c
HTTRANSPARENT = -1
HTNOWHERE     = 0
HTCLIENT      = 1
HTCAPTION     = 2
HTMENU        = 5
HTHSCROLL     = 6
HTVSCROLL     = 7
HTBOTTOMRIGHT = 17
```

### 3.32 Window relationship constants

```c
GW_HWNDFIRST = 0
GW_HWNDLAST  = 1
GW_HWNDNEXT  = 2
GW_HWNDPREV  = 3
GW_OWNER     = 4
GW_CHILD     = 5
```

### 3.33 Global memory

```c
GMEM_ZEROINIT = 1
GMEM_FIXED = GMEM_MOVEABLE = GMEM_DDESHARE = 0   // all no-ops except ZEROINIT
```

### 3.34 Clipboard formats

```c
CF_TEXT  = 1
CF_HDROP = 2
```

### 3.35 Thread priority

```c
THREAD_PRIORITY_LOWEST        = -2
THREAD_PRIORITY_BELOW_NORMAL  = -1
THREAD_PRIORITY_NORMAL        = 0
THREAD_PRIORITY_ABOVE_NORMAL  = 1
THREAD_PRIORITY_HIGHEST       = 2
THREAD_PRIORITY_TIME_CRITICAL = 15
THREAD_PRIORITY_IDLE          = -15

WAIT_OBJECT_0 = 0
WAIT_TIMEOUT  = 0x00000102
WAIT_FAILED   = 0xFFFFFFFF
INFINITE      = 0xFFFFFFFF
```

### 3.36 SWELLAppMain message codes

```c
SWELLAPP_ONLOAD          = 0x0001
SWELLAPP_LOADED          = 0x0002
SWELLAPP_DESTROY         = 0x0003
SWELLAPP_SHOULDDESTROY   = 0x0004
SWELLAPP_OPENFILE        = 0x0050
SWELLAPP_NEWFILE         = 0x0051
SWELLAPP_SHOULDOPENNEWFILE=0x0052
SWELLAPP_ONCOMMAND       = 0x0099
SWELLAPP_PROCESSMESSAGE  = 0x0100
SWELLAPP_ACTIVATE        = 0x1000
```

### 3.37 DLL lifecycle

```c
DLL_PROCESS_DETACH = 0
DLL_PROCESS_ATTACH = 1
```

---

## 4. Macros and Inline Utilities

### 4.1 Packing/unpacking

```c
MAKEWORD(a,b)   -> (unsigned short)
MAKELONG(a,b)   -> (int)
MAKEWPARAM(l,h), MAKELPARAM(l,h), MAKELRESULT(l,h)
LOWORD(l), HIWORD(l), LOBYTE(w), HIBYTE(w)
GET_X_LPARAM(lp) -> (int)(short)LOWORD(lp)
GET_Y_LPARAM(lp) -> (int)(short)HIWORD(lp)
MAKEINTRESOURCE(x) -> (const char *)(UINT_PTR)(x)
INDEXTOSTATEIMAGEMASK(x) -> (x)<<16
LVIS_STATEIMAGEMASK -> (255<<16)
```

### 4.2 Color packing

Default (non-Win32) byte order: `RGB(r,g,b)` = `(r<<16)|(g<<8)|b`  
When `SWELL_USE_WIN32_RGB`: `RGB(r,g,b)` = `(b<<16)|(g<<8)|r`

```c
RGB(r,g,b), GetRValue(x), GetGValue(x), GetBValue(x)
```

### 4.3 Rect helpers (implemented as functions, aliased)

```c
#define OffsetRect    WinOffsetRect
#define SetRect       WinSetRect
#define UnionRect     WinUnionRect
#define IntersectRect WinIntersectRect
#define PtInRect(r,p) SWELL_PtInRect(r,p)
```

### 4.4 File system shims

```c
#define DeleteFile(x)            (!unlink(x))
#define MoveFile(x,y)            (!rename(x,y))
#define GetCurrentDirectory(sz,buf) (!getcwd(buf,sz))
#define SetCurrentDirectory(buf) (!chdir(buf))
#define CreateDirectory(x,y)     (!mkdir((x),0755))
#define MAX_PATH 1024
```

### 4.5 Window/dialog shorthand

```c
#define GetWindowText(hwnd,text,textlen)  GetDlgItemText(hwnd,0,text,textlen)
#define SetWindowText(hwnd,text)          SetDlgItemText(hwnd,0,text)
#define GetActiveWindow()                 GetForegroundWindow()
#define SetActiveWindow(x)                SetForegroundWindow(x)
#define SendDlgItemMessage(hwnd,idx,msg,wp,lp) SendMessage(GetDlgItem(hwnd,idx),msg,wp,lp)
#define DialogBox(hi,resid,par,dp)        SWELL_DialogBox(SWELL_curmodule_dialogresource_head,(resid),par,dp,0)
#define DialogBoxParam(hi,resid,par,dp,p) SWELL_DialogBox(SWELL_curmodule_dialogresource_head,(resid),par,dp,p)
#define CreateDialog(hi,resid,par,dp)     SWELL_CreateDialog(SWELL_curmodule_dialogresource_head,(resid),par,dp,0)
#define CreateDialogParam(hi,resid,par,dp,p) SWELL_CreateDialog(SWELL_curmodule_dialogresource_head,(resid),par,dp,p)
#define LoadMenu(hinst,resid)             SWELL_LoadMenu(SWELL_curmodule_menuresource_head,(resid))
#define InsertMenu                        SWELL_InsertMenu
#define SetCursor(x)                      SWELL_SetCursor(x)
#define GetCursor                         SWELL_GetCursor
#define ShowCursor                        SWELL_ShowCursor
#define SetCursorPos                      SWELL_SetCursorPos
#define LoadCursor(a,x)                   SWELL_LoadCursor(x)
#define DrawText                          SWELL_DrawText
#define FillRect                          SWELL_FillRect
#define LineTo                            SWELL_LineTo
#define SetPixel                          SWELL_SetPixel
#define Polygon(a,b,c)                    SWELL_Polygon(a,b,c)
#define DestroyIcon(x)                    DeleteObject(x)
#define timeGetTime()                     GetTickCount()
#define CallWindowProc(A,B,C,D,E)         ((WNDPROC)A)(B,C,D,E)
#define ScrollWindowEx(a,b,c,d,e,f,g,h)  ScrollWindow(a,b,c,d,e)
#define ListView_SetItemCountEx(list,cnt,flags) ListView_SetItemCount(list,cnt)
```

### 4.6 Combobox convenience macros

When not on macOS, `SWELL_CB_*` collapse to `SendDlgItemMessage` with `CB_*`:

```c
SWELL_CB_InsertString(hwnd, idx, pos, str)
SWELL_CB_AddString(hwnd, idx, str)
SWELL_CB_SetCurSel(hwnd, idx, val)
SWELL_CB_GetNumItems(hwnd, idx)
SWELL_CB_GetCurSel(hwnd, idx)
SWELL_CB_SetItemData(hwnd, idx, item, val)
SWELL_CB_GetItemData(hwnd, idx, item)
SWELL_CB_GetItemText(hwnd, idx, item, buf, bufsz)
SWELL_CB_Empty(hwnd, idx)
SWELL_CB_DeleteString(hwnd, idx, str)
```

### 4.7 Trackbar convenience macros

```c
SWELL_TB_SetPos(hwnd, idx, pos)
SWELL_TB_SetRange(hwnd, idx, low, hi)
SWELL_TB_GetPos(hwnd, idx)
SWELL_TB_SetTic(hwnd, idx, pos)
```

---

## 5. API Functions

All functions declared via `SWELL_API_DEFINE(ret, name, parms)`.

### 5.1 String / math

```c
char *lstrcpyn(char *dest, const char *src, int l)
```
Always null-terminates. Does not zero-fill. Preferred over `strncpy`.

```c
int MulDiv(int a, int b, int c)
```
Returns `(a*b)/c` using 64-bit arithmetic.

### 5.2 Time

```c
void  Sleep(int ms)
```
Maps to `usleep`. ms=0 sleeps 100µs.

```c
DWORD GetTickCount()
```
Millisecond timer via `gettimeofday`/`mach_getabsolutetime`/`clock_gettime`.
May wrap sooner than Win32 because epoch is not system boot.

```c
BOOL GetFileTime(int filedes, FILETIME *lpCreationTime,
                 FILETIME *lpLastAccessTime, FILETIME *lpLastWriteTime)
```
Takes a file descriptor (not HANDLE). Accuracy is 1 second.

### 5.3 INI file

```c
BOOL  WritePrivateProfileString(const char *appname, const char *keyname,
                                const char *val, const char *fn)
DWORD GetPrivateProfileString(const char *appname, const char *keyname,
                              const char *def, char *ret, int retsize, const char *fn)
int   GetPrivateProfileInt(const char *appname, const char *keyname,
                           int def, const char *fn)
BOOL  GetPrivateProfileStruct(const char *appname, const char *keyname,
                              void *buf, int bufsz, const char *fn)
BOOL  WritePrivateProfileStruct(const char *appname, const char *keyname,
                                const void *buf, int bufsz, const char *fn)
BOOL  WritePrivateProfileSection(const char *appname, const char *strings, const char *fn)
DWORD GetPrivateProfileSection(const char *appname, char *strout,
                               DWORD strout_len, const char *fn)
```

- `fn` must be an absolute path. Empty string `""` uses `~/.libSwell.ini`
  (overridable by app).
- Thread-safe, inter-process-safe via file locking.
- `GetPrivateProfileStruct` stores binary as hex + checksum; the checksum is
  asserted on read.

### 5.4 Module / library

```c
DWORD     GetModuleFileName(HINSTANCE hInst, char *fn, DWORD nSize)
HINSTANCE LoadLibrary(const char *fileName)
HINSTANCE LoadLibraryGlobals(const char *fileName, bool symbolsAsGlobals)
void     *GetProcAddress(HINSTANCE hInst, const char *procName)
BOOL      FreeLibrary(HINSTANCE hInst)
void     *SWELL_GetBundle(HINSTANCE hInst)
```

`LoadLibraryGlobals` with `symbolsAsGlobals=true` loads with `RTLD_GLOBAL`.

### 5.5 Process

```c
HANDLE SWELL_CreateProcess(const char *exe, int nparams, const char **params)
int    SWELL_GetProcessExitCode(HANDLE hand)
```

macOS only:
```c
int    SWELL_TerminateProcess(HANDLE hand)
HANDLE SWELL_CreateProcessIO(const char *exe, int nparams,
                             const char **params, bool redirectIO)
int    SWELL_ReadWriteProcessIO(HANDLE, int w/*0=stdin,1=stdout,2=stderr*/,
                               char *buf, int bufsz)
```

Generic only:
```c
HANDLE SWELL_CreateProcessFromPID(int pid)
```

### 5.6 Threading and synchronization

```c
HANDLE CreateThread(void *TA, DWORD stackSize,
                    DWORD (*ThreadProc)(LPVOID), LPVOID parm, DWORD cf, DWORD *tidOut)
DWORD  GetCurrentThreadId()
BOOL   SetThreadPriority(HANDLE evt, int prio)
BOOL   CloseHandle(HANDLE hand)

HANDLE CreateEvent(void *SA, BOOL manualReset, BOOL initialSig, const char *ignored)
HANDLE CreateEventAsSocket(void *SA, BOOL manualReset, BOOL initialSig, const char *ignored)
BOOL   SetEvent(HANDLE evt)
BOOL   ResetEvent(HANDLE evt)
DWORD  WaitForSingleObject(HANDLE hand, DWORD msTO)
DWORD  WaitForAnySocketObject(int numObjs, HANDLE *objs, DWORD msTO)
```

`CreateEventAsSocket` creates an event backed by a socket pair (for use in
select()-based event loops on generic/SDL3).

`_beginthreadex` is defined as `(UINT_PTR)CreateThread(...)`.

### 5.7 GUID

```c
bool SWELL_GenerateGUID(void *g)
```

Fills a `GUID`-sized struct with a random GUID.

### 5.8 Rect utilities

```c
BOOL SWELL_PtInRect(const RECT *r, POINT p)
BOOL WinOffsetRect(LPRECT lprc, int dx, int dy)
BOOL WinSetRect(LPRECT lprc, int xLeft, int yTop, int xRight, int yBottom)
void WinUnionRect(RECT *out, const RECT *in1, const RECT *in2)
int  WinIntersectRect(RECT *out, const RECT *in1, const RECT *in2)
```

---

## 6. Window Management

### 6.1 Creation and destruction

```c
// Create modal dialog (blocks until EndDialog)
int  SWELL_DialogBox(struct SWELL_DialogResourceIndex *reshead,
                     const char *resid, HWND parent,
                     DLGPROC dlgproc, LPARAM param)

// Create modeless dialog or child window
HWND SWELL_CreateDialog(struct SWELL_DialogResourceIndex *reshead,
                        const char *resid, HWND parent,
                        DLGPROC dlgproc, LPARAM param)
```

`resid=0` with `CreateDialog` creates an opaque child window. The `dlgproc`
parameter in that case should be a `WNDPROC` cast to `DLGPROC`.

`hInstance` parameters in the `DialogBox`/`CreateDialog` macros are ignored.
To load from another module, call `SWELL_DialogBox`/`SWELL_CreateDialog`
directly with that module's `SWELL_curmodule_dialogresource_head`.

```c
void DestroyWindow(HWND hwnd)
void EndDialog(HWND, int)
```

`EndDialog` sets a return value and closes a modal dialog.

### 6.2 Visibility, focus, state

```c
void ShowWindow(HWND, int)       // SW_* constants
void EnableWindow(HWND hwnd, int enable)
bool IsWindowEnabled(HWND)
bool IsWindowVisible(HWND hwnd)
bool IsWindow(HWND hwnd)         // expensive on macOS; avoid
void SetFocus(HWND hwnd)
HWND GetFocus()
void SetForegroundWindow(HWND hwnd)
HWND GetForegroundWindow()
```

`IsWindowVisible` on macOS: NSView → `!isHiddenOrHasHiddenAncestor`;
NSWindow → `isVisible`; other non-null → TRUE.

### 6.3 Position and size

```c
void GetClientRect(HWND hwnd, RECT *r)
bool GetWindowRect(HWND hwnd, RECT *r)
void GetWindowContentViewRect(HWND hwnd, RECT *r)  // pre-NCCALCSIZE rect
void SetWindowPos(HWND hwnd, HWND unused, int x, int y, int cx, int cy, int flags)
int  SWELL_SetWindowLevel(HWND hwnd, int newlevel)
void ClientToScreen(HWND hwnd, POINT *p)
void ScreenToClient(HWND hwnd, POINT *p)
HWND WindowFromPoint(POINT p)
```

On macOS coordinates may be flipped (bottom < top). `SetWindowPos` and related
functions handle negative heights gracefully; so should all callers.

`GetWindowContentViewRect` returns the content-view rect before `WM_NCCALCSIZE`
adjustment.

### 6.4 Window properties

```c
LONG_PTR GetWindowLong(HWND hwnd, int idx)
LONG_PTR SetWindowLong(HWND hwnd, int idx, LONG_PTR val)
BOOL     ScrollWindow(HWND hwnd, int xamt, int yamt,
                      const RECT *lpRect, const RECT *lpClipRect)
BOOL     InvalidateRect(HWND hwnd, const RECT *r, int eraseBk)
void     UpdateWindow(HWND hwnd)
```

Supported `GWL_*` indices: `GWL_ID`, `GWL_USERDATA`, `GWL_WNDPROC`,
`DWL_DLGPROC`, `GWL_STYLE` (NSButton only, BS_ flags), extra slots ≥0.

### 6.5 Window hierarchy

```c
HWND GetParent(HWND hwnd)
HWND SetParent(HWND hwnd, HWND newPar)
HWND GetWindow(HWND hwnd, int what)   // GW_* constants
int  IsChild(HWND hwndParent, HWND hwndChild)
BOOL EnumWindows(BOOL (*proc)(HWND, LPARAM), LPARAM lp)
BOOL EnumChildWindows(HWND hwnd, BOOL (*cwEnumFunc)(HWND,LPARAM), LPARAM lParam)
HWND FindWindowEx(HWND par, HWND lastw, const char *classname, const char *title)
HWND GetDlgItem(HWND, int)
```

`GetDlgItem(hwnd, 0)` on macOS: returns `hwnd` if NSView, else content view if
NSWindow. On macOS this searches the entire view hierarchy; on generic/-win32
only immediate children.

### 6.6 Control text and value

```c
BOOL SetDlgItemText(HWND, int idx, const char *text)
BOOL SetDlgItemInt(HWND, int idx, int val, int issigned)
int  GetDlgItemInt(HWND, int idx, BOOL *translated, int issigned)
BOOL GetDlgItemText(HWND, int idx, char *text, int textlen)
int  GetWindowTextLength(HWND)
BOOL CheckDlgButton(HWND hwnd, int idx, int check)
int  IsDlgButtonChecked(HWND hwnd, int idx)
```

macOS: `SetDlgItemText` on an edit control does NOT send `WM_COMMAND`;
generic and win32 do.

### 6.7 Window properties (prop list)

```c
int    EnumPropsEx(HWND, PROPENUMPROCEX, LPARAM)
HANDLE GetProp(HWND, const char *)
BOOL   SetProp(HWND, const char *, HANDLE)
HANDLE RemoveProp(HWND, const char *)
```

If the prop name pointer is `< (void*)65536`, it is treated as a short integer
identifier. Props leak if not freed. Within `PROPENUMPROCEX` only the currently
enumerated prop may be removed; no new props may be added.

### 6.8 Class name

```c
int  GetClassName(HWND, char *, int)
void SWELL_SetClassName(HWND, const char *)   // must pass a static string
```

Partially implemented. Custom control creators should call `SWELL_SetClassName`
to register a class name.

### 6.9 Window style / visibility helpers

```c
BOOL SWELL_IsGroupBox(HWND)
BOOL SWELL_IsButton(HWND)
BOOL SWELL_IsStaticText(HWND)
void SWELL_GetDesiredControlSize(HWND hwnd, RECT *r)
void SWELL_DisableContextMenu(HWND, bool)
void SWELL_HideApp()
```

### 6.10 Mouse capture

```c
HWND SetCapture(HWND hwnd)
HWND GetCapture()
void ReleaseCapture()
```

macOS: any view that returns YES to `swellCapChangeNotify` must call
`ReleaseCapture()` if it holds the capture before dealloc.

### 6.11 Default button

```c
int SWELL_GetDefaultButtonID(HWND hwndDlg, bool onlyIfEnabled)
```

Returns the control ID of the default push button, or 0.

### 6.12 Window message functions

```c
LRESULT SendMessage(HWND, UINT, WPARAM, LPARAM)
void    SWELL_BroadcastMessage(UINT, WPARAM, LPARAM)
BOOL    PostMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
void    SWELL_MessageQueue_Flush()
void    SWELL_MessageQueue_Clear(HWND h)
LRESULT DefWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
```

`SendMessage` is NOT thread-safe. Must only be called from the window's creation
thread.

`PostMessage` queues a message processed later from the main thread via a timer.
Outstanding messages are discarded when the destination window is destroyed.

`SWELL_MessageQueue_Flush` must only be called from the main thread.

`SWELL_MessageQueue_Clear(NULL)` discards all queued messages.

`SWELL_BroadcastMessage` sends to all top-level windows.

### 6.13 Timers

```c
UINT_PTR SetTimer(HWND hwnd, UINT_PTR timerid, UINT rate, TIMERPROC tProc)
BOOL     KillTimer(HWND hwnd, UINT_PTR timerid)
```

`KillTimer(hwnd, -1)` kills all timers for `hwnd`.

Must kill all timers before destroying a window (SWELL-created windows do this
automatically). Timers are safest from the main thread only.

### 6.14 Scrollbars

```c
// No standalone SetScrollInfo/GetScrollInfo API; use WM_HSCROLL/WM_VSCROLL + SCROLLINFO
// via custom control or per-control messaging
```

SWELL does not expose standalone `SetScrollInfo`/`GetScrollInfo` functions.
Scroll state is communicated via `WM_HSCROLL`/`WM_VSCROLL` messages with
`SB_*` codes.

### 6.15 Modal window helpers (macOS, deprecated)

```c
void *SWELL_ModalWindowStart(HWND hwnd)
bool  SWELL_ModalWindowRun(void *ctx, int *ret)
void  SWELL_ModalWindowEnd(void *ctx)
void  SWELL_CloseWindow(HWND hwnd)
```

Prefer `DialogBox` with a timer instead.

---

## 7. Custom Controls

```c
void SWELL_RegisterCustomControlCreator(SWELL_ControlCreatorProc proc)
void SWELL_UnregisterCustomControlCreator(SWELL_ControlCreatorProc proc)
```

The `SWELL_ControlCreatorProc` callback receives a classname and should return
an HWND if it handles the class, or NULL to defer to the next registered creator.

---

## 8. GDI Drawing

### 8.1 Device context lifetime

```c
HDC  SWELL_CreateMemContext(HDC hdc, int w, int h)  // hdc param ignored
void SWELL_DeleteGfxContext(HDC)
HDC  BeginPaint(HWND, PAINTSTRUCT *)
BOOL EndPaint(HWND, PAINTSTRUCT *)
HDC  GetDC(HWND)
HDC  GetWindowDC(HWND)
void ReleaseDC(HWND, HDC)
```

`GetDC`/`GetWindowDC`/`ReleaseDC` work but should be used sparingly.

### 8.2 Context info

```c
void *SWELL_GetCtxGC(HDC ctx)          // macOS: CGContextRef; SDL3: NULL
void *SWELL_GetCtxFrameBuffer(HDC ctx) // raw pixel buffer or NULL
```

### 8.3 Clip region

```c
void SWELL_PushClipRegion(HDC ctx)
void SWELL_SetClipRegion(HDC ctx, const RECT *r)
void SWELL_PopClipRegion(HDC ctx)
```

On LICE backend only one item on the stack.

### 8.4 GDI objects

```c
HFONT   CreateFontIndirect(const LOGFONT *)
HFONT   CreateFont(int lfHeight, int lfWidth, int lfEscapement, int lfOrientation,
                   int lfWeight, char lfItalic, char lfUnderline, char lfStrikeOut,
                   char lfCharSet, char lfOutPrecision, char lfClipPrecision,
                   char lfQuality, char lfPitchAndFamily, const char *lfFaceName)
HPEN    CreatePen(int attr, int wid, int col)
HPEN    CreatePenAlpha(int attr, int wid, int col, float alpha)
HBRUSH  CreateSolidBrush(int col)
HBRUSH  CreateSolidBrushAlpha(int col, float alpha)
HGDIOBJ SelectObject(HDC ctx, HGDIOBJ pen)
HGDIOBJ GetStockObject(int wh)    // NULL_PEN, NULL_BRUSH
void    DeleteObject(HGDIOBJ)
HGDIOBJ SWELL_CloneGDIObject(HGDIOBJ a)
HBITMAP CreateBitmap(int width, int height, int numplanes, int bitsperpixel,
                     unsigned char *bits)
BOOL    GetObject(HICON icon, int bmsz, void *_bm)  // fills BITMAP struct
HICON   CreateIconIndirect(const ICONINFO *iconinfo)
HICON   LoadNamedImage(const char *name, bool alphaFromMask)
```

### 8.5 Drawing operations

```c
void SWELL_FillRect(HDC ctx, const RECT *r, HBRUSH br)
void Rectangle(HDC ctx, int l, int t, int r, int b)
void Ellipse(HDC ctx, int l, int t, int r, int b)
void SWELL_Polygon(HDC ctx, POINT *pts, int npts)
void RoundRect(HDC ctx, int x, int y, int x2, int y2, int xrnd, int yrnd)
void PolyPolyline(HDC ctx, const POINT *pts, const DWORD *cnts, int nseg)
void MoveToEx(HDC ctx, int x, int y, POINT *op)
void LineTo(HDC ctx, int x, int y)      // aliased to SWELL_LineTo
void SetPixel(HDC ctx, int x, int y, int c)  // aliased
void PolyBezierTo(HDC ctx, POINT *pts, int np)
```

`PolyPolyline` is not implemented on Linux.

### 8.6 Blit operations

```c
void BitBlt(HDC hdcOut, int x, int y, int w, int h,
            HDC hdcIn, int xin, int yin, int mode)
void StretchBlt(HDC hdcOut, int x, int y, int w, int h,
                HDC hdcIn, int xin, int yin, int srcw, int srch, int mode)
void DrawImageInRect(HDC ctx, HICON img, const RECT *r)
```

Generic/non-macOS only:
```c
void StretchBltFromMem(HDC hdcOut, int x, int y, int w, int h,
                       const void *bits, int srcw, int srch, int srcspan)
```

`mode=SRCCOPY` or `mode=SRCCOPY_USEALPHACHAN` (SWELL extension for alpha).

### 8.7 Text

```c
int  SWELL_DrawText(HDC ctx, const char *buf, int len, RECT *r, int align)
void SetTextColor(HDC ctx, int col)
int  GetTextColor(HDC ctx)
void SetBkColor(HDC ctx, int col)
void SetBkMode(HDC ctx, int col)      // TRANSPARENT or OPAQUE
BOOL GetTextMetrics(HDC ctx, TEXTMETRIC *tm)
int  GetTextFace(HDC ctx, int nCount, LPTSTR lpFaceName)
int  GetGlyphIndicesW(HDC ctx, wchar_t *buf, int len,
                      unsigned short *indices, int flags)
```

`DrawText` is aliased to `SWELL_DrawText`.

### 8.8 System colors

```c
int GetSysColor(int idx)    // COLOR_* constants
```

### 8.9 Rendering state / GL

```c
void SWELL_FillDialogBackground(HDC hdc, const RECT *r, int level)
void SetOpaque(HWND h, bool isopaque)
void SetAllowNoMiddleManRendering(HWND h, bool allow)
void SWELL_SetViewGL(HWND h, char wantGL)  // wantGL=2 enables best-resolution
bool SWELL_GetViewGL(HWND h)
bool SWELL_SetGLContextToView(HWND h)      // NULL to clear GL context
```

macOS only:
```c
int  SWELL_EnableMetal(HWND h, int mode)
int  SWELL_IsRetinaDC(HDC hdc)
int  SWELL_IsRetinaHWND(HWND h)
void SWELL_SetNoMultiMonitorAutoSize(HWND h, bool noauto)
void SWELL_FlushWindow(HWND)
```

Generic only:
```c
int  SWELL_GetScaling256()   // returns DPI scaling factor * 256
```

---

## 9. Fonts

```c
int  AddFontResourceEx(LPCTSTR str, DWORD fl, void *pdv)
```

`fl=FR_PRIVATE` loads font privately.

---

## 10. Menus

### 10.1 Lifecycle

```c
HMENU CreatePopupMenu()
HMENU CreatePopupMenuEx(const char *title)
void  DestroyMenu(HMENU hMenu)
HMENU SWELL_DuplicateMenu(HMENU menu)
```

### 10.2 Item manipulation

```c
int  AddMenuItem(HMENU hMenu, int pos, const char *name, int tagid)
void SWELL_InsertMenu(HMENU menu, int pos, unsigned int flag, UINT_PTR idx, const char *str)
void InsertMenuItem(HMENU hMenu, int pos, BOOL byPos, MENUITEMINFO *mi)
BOOL GetMenuItemInfo(HMENU hMenu, int pos, BOOL byPos, MENUITEMINFO *mi)
BOOL SetMenuItemInfo(HMENU hMenu, int pos, BOOL byPos, MENUITEMINFO *mi)
bool SetMenuItemModifier(HMENU hMenu, int idx, int flag, int code, unsigned int mask)
bool SetMenuItemText(HMENU hMenu, int idx, int flag, const char *text)
bool EnableMenuItem(HMENU hMenu, int idx, int en)
bool DeleteMenu(HMENU hMenu, int idx, int flag)
bool CheckMenuItem(HMENU hMenu, int idx, int chk)
```

### 10.3 Query

```c
HMENU GetSubMenu(HMENU hMenu, int pos)
int   GetMenuItemCount(HMENU hMenu)
int   GetMenuItemID(HMENU hMenu, int pos)
```

### 10.4 Window menu bar

```c
BOOL  SetMenu(HWND hwnd, HMENU menu)
HMENU GetMenu(HWND hwnd)
void  DrawMenuBar(HWND)
```

macOS: `SetMenu` sets `NSApp setMainMenu:` when the window is activated.

### 10.5 Default and modal menus

```c
HMENU SWELL_GetDefaultWindowMenu()
void  SWELL_SetDefaultWindowMenu(HMENU)
HMENU SWELL_GetDefaultModalWindowMenu()
void  SWELL_SetDefaultModalWindowMenu(HMENU)
HMENU SWELL_GetCurrentMenu()
void  SWELL_SetCurrentMenu(HMENU)
```

### 10.6 Popup

```c
int TrackPopupMenu(HMENU hMenu, int flags, int xpos, int ypos,
                   int resvd, HWND hwnd, const RECT *r)
```

`resvd` must be 0. The rect parameter is ignored.

With `TPM_RETURNCMD`: returns selected item ID, no WM_COMMAND posted.
Without `TPM_RETURNCMD` and without `TPM_NONOTIFY`: sends `WM_COMMAND` to hwnd.

### 10.7 Resource loading

```c
HMENU SWELL_LoadMenu(struct SWELL_MenuResourceIndex *head, const char *resid)
void  SWELL_SetMenuDestination(HMENU menu, HWND hwnd)  // macOS only
```

### 10.8 Internal menu generation

```c
void SWELL_Menu_AddMenuItem(HMENU hMenu, const char *name, int idx, unsigned int flags)
int  SWELL_GenerateMenuFromList(HMENU hMenu, const void *list, int listsz)
```

---

## 11. Input

### 11.1 Keyboard

```c
int  SWELL_KeyToASCII(int wParam, int lParam, int *newflags)
WORD GetAsyncKeyState(int key)
```

macOS only:
```c
int SWELL_MacKeyToWindowsKey(void *nsevent, int *flags)
int SWELL_MacKeyToWindowsKeyEx(void *nsevent, int *flags, int mode)
```

`GetAsyncKeyState` on macOS: supports VK_LBUTTON, VK_RBUTTON, VK_MBUTTON,
VK_SHIFT, VK_MENU, VK_CONTROL (apple/command), VK_LWIN (control key).

### 11.2 Mouse

```c
void GetCursorPos(POINT *pt)
DWORD GetMessagePos()           // same coords as GetCursorPos currently
BOOL  SWELL_GetGestureInfo(LPARAM lParam, GESTUREINFO *gi)
```

### 11.3 Cursor

```c
HCURSOR SWELL_LoadCursor(const char *idx)
HCURSOR SWELL_LoadCursorFromFile(const char *fn)
void    SWELL_SetCursor(HCURSOR curs)
HCURSOR SWELL_GetCursor()
HCURSOR SWELL_GetLastSetCursor()
bool    SWELL_IsCursorVisible()
int     SWELL_ShowCursor(BOOL bShow)
BOOL    SWELL_SetCursorPos(int X, int Y)
void    SWELL_EnableRightClickEmulate(BOOL enable)  // macOS: ctrl+left → right
void    SWELL_Register_Cursor_Resource(const char *idx, const char *name,
                                       int hotspot_x, int hotspot_y)
```

---

## 12. Clipboard

```c
bool    OpenClipboard(HWND hwndDlg)
void    CloseClipboard()
void    EmptyClipboard()
HANDLE  GetClipboardData(UINT type)
void    SetClipboardData(UINT type, HANDLE h)
UINT    RegisterClipboardFormat(const char *desc)
UINT    EnumClipboardFormats(UINT lastfmt)   // start with 0
HANDLE  GlobalAlloc(int flags, int sz)
void   *GlobalLock(HANDLE h)
int     GlobalSize(HANDLE h)
void    GlobalUnlock(HANDLE h)
void    GlobalFree(HANDLE h)
```

macOS: setting multiple types may not be supported.
SDL3: only `CF_TEXT` is shared with the system clipboard; other types are
stored internally.

`GlobalAlloc` with `GMEM_ZEROINIT` zeroes the block. `GMEM_FIXED`,
`GMEM_MOVEABLE` etc. are no-ops.

---

## 13. Drag and Drop

### 13.1 Drop target (receiving)

```c
BOOL DragQueryPoint(HDROP, LPPOINT)
void DragFinish(HDROP)
UINT DragQueryFile(HDROP, UINT, char *, UINT)
```

### 13.2 Drag source (initiating)

```c
void SWELL_InitiateDragDrop(HWND, RECT *srcrect, const char *srcfn,
                            void (*callback)(const char *droppath))
void SWELL_InitiateDragDropOfFileList(HWND, RECT *srcrect,
                                      const char **srclist, int srccount, HICON icon)
void SWELL_FinishDragDrop()
```

`SWELL_FinishDragDrop` cancels any outstanding `InitiateDragDrop`.

### 13.3 Drag/drop callbacks (global)

```c
extern void (*SWELL_DDrop_onDragLeave)();
extern void (*SWELL_DDrop_onDragOver)(POINT pt);
extern void (*SWELL_DDrop_onDragEnter)(void *hGlobal, POINT pt);
extern const char *(*SWELL_DDrop_getDroppedFileTargetPath)(const char *extension);
```

---

## 14. ListView

```c
void ListView_SetExtendedListViewStyleEx(HWND h, int mask, int style)
void ListView_InsertColumn(HWND h, int pos, const LVCOLUMN *lvc)
bool ListView_DeleteColumn(HWND h, int pos)
void ListView_SetColumn(HWND h, int pos, const LVCOLUMN *lvc)
void ListView_GetColumn(HWND h, int pos, LVCOLUMN *lvc)
int  ListView_GetColumnWidth(HWND h, int pos)
int  ListView_InsertItem(HWND h, const LVITEM *item)
void ListView_SetItemText(HWND h, int ipos, int cpos, const char *txt)
bool ListView_SetItem(HWND h, LVITEM *item)
bool ListView_GetItem(HWND h, LVITEM *item)
int  ListView_GetItemState(HWND h, int ipos, UINT mask)
bool ListView_SetItemState(HWND h, int item, UINT state, UINT statemask)
void ListView_GetItemText(HWND hwnd, int item, int subitem, char *text, int textmax)
void ListView_DeleteItem(HWND h, int ipos)
void ListView_DeleteAllItems(HWND h)
int  ListView_GetNextItem(HWND h, int istart, int flags)
int  ListView_GetSelectedCount(HWND h)
int  ListView_GetItemCount(HWND h)
int  ListView_GetSelectionMark(HWND h)
void ListView_SetColumnWidth(HWND h, int colpos, int wid)
void ListView_RedrawItems(HWND h, int startitem, int enditem)
void ListView_SetItemCount(HWND h, int cnt)    // owner-data mode
void ListView_EnsureVisible(HWND h, int i, BOOL pok)
void ListView_SortItems(HWND hwnd, PFNLVCOMPARE compf, LPARAM parm)
bool ListView_Scroll(HWND h, int xscroll, int yscroll)
int  ListView_GetTopIndex(HWND h)
int  ListView_GetCountPerPage(HWND h)
int  ListView_HitTest(HWND h, LVHITTESTINFO *pinf)
int  ListView_SubItemHitTest(HWND h, LVHITTESTINFO *pinf)
bool ListView_GetItemRect(HWND h, int item, RECT *r, int code)
bool ListView_GetSubItemRect(HWND h, int item, int subitem, int code, RECT *r)
void ListView_SetImageList(HWND h, HIMAGELIST imagelist, int which)
BOOL ListView_SetColumnOrderArray(HWND h, int cnt, int *arr)
BOOL ListView_GetColumnOrderArray(HWND h, int cnt, int *arr)
HWND ListView_GetHeader(HWND h)
int  Header_GetItemCount(HWND h)
BOOL Header_GetItem(HWND h, int col, HDITEM *hi)
BOOL Header_SetItem(HWND h, int col, HDITEM *hi)
int  SWELL_GetListViewHeaderHeight(HWND h)
void SWELL_SetListViewFastClickMask(HWND hList, int mask)

void ListView_SetBkColor(HWND hwnd, int color)
void ListView_SetTextBkColor(HWND hwnd, int color)
void ListView_SetTextColor(HWND hwnd, int color)
void ListView_SetGridColor(HWND hwnd, int color)
void ListView_SetSelColors(HWND hwnd, int *colors, int ncolors)
```

In owner-data mode (`LVS_OWNERDATA`): `LVN_GETDISPINFO` is sent to populate
items. `LVN_ODFINDITEM` is never sent.

macOS `ListView_GetItemRect`/`ListView_GetSubItemRect`: return absolute
(unscrolled) coordinates. Use `ScreenToClient` after `ClientToScreen` to
convert to relative.

---

## 15. TreeView

```c
HTREEITEM TreeView_InsertItem(HWND hwnd, TV_INSERTSTRUCT *ins)
BOOL      TreeView_Expand(HWND hwnd, HTREEITEM item, UINT flag)
HTREEITEM TreeView_GetSelection(HWND hwnd)
void      TreeView_DeleteItem(HWND hwnd, HTREEITEM item)
void      TreeView_DeleteAllItems(HWND hwnd)
void      TreeView_SelectItem(HWND hwnd, HTREEITEM item)
void      TreeView_EnsureVisible(HWND hwnd, HTREEITEM item)
BOOL      TreeView_GetItem(HWND hwnd, LPTVITEM pitem)
BOOL      TreeView_SetItem(HWND hwnd, LPTVITEM pitem)
HTREEITEM TreeView_HitTest(HWND hwnd, TVHITTESTINFO *hti)
BOOL      TreeView_SetIndent(HWND hwnd, int indent)
HTREEITEM TreeView_GetParent(HWND hwnd, HTREEITEM item)
HTREEITEM TreeView_GetChild(HWND hwnd, HTREEITEM item)
HTREEITEM TreeView_GetNextSibling(HWND hwnd, HTREEITEM item)
HTREEITEM TreeView_GetRoot(HWND hwnd)
void      TreeView_SetBkColor(HWND hwnd, int color)
void      TreeView_SetTextColor(HWND hwnd, int color)
```

SWELL extension: in `TVN_BEGINDRAG`, if drag is set via `WM_MOUSEMOVE` capture,
the return value encodes destination:
- `-1` = drag not possible
- `-2` = destination at end of list
- `(HTREEITEM)` = will insert before that item

---

## 16. Tab Control

```c
int  TabCtrl_GetItemCount(HWND hwnd)
BOOL TabCtrl_DeleteItem(HWND hwnd, int idx)
int  TabCtrl_InsertItem(HWND hwnd, int idx, TCITEM *item)
int  TabCtrl_SetCurSel(HWND hwnd, int idx)
int  TabCtrl_GetCurSel(HWND hwnd)
BOOL TabCtrl_AdjustRect(HWND hwnd, BOOL fLarger, RECT *r)
```

---

## 17. Image Lists

```c
HIMAGELIST ImageList_CreateEx()
BOOL       ImageList_Remove(HIMAGELIST list, int idx)
int        ImageList_ReplaceIcon(HIMAGELIST list, int offset, HICON image)
int        ImageList_Add(HIMAGELIST list, HBITMAP image, HBITMAP mask)
void       ImageList_Destroy(HIMAGELIST)
```

`ImageList_Create(x,y,a,b,c)` is a macro that calls `ImageList_CreateEx()`.

---

## 18. File / Shell

```c
BOOL ShellExecute(HWND hwndDlg, const char *action, const char *content1,
                  const char *content2, const char *content3, int blah)
int  MessageBox(HWND hwndParent, const char *text, const char *caption, int type)
void GetTempPath(int sz, char *buf)
```

`ShellExecute`:
- `action` ignored
- `content1` as `http://` or `https://` URL → opens in browser
- `content1` as `notepad`/`notepad.exe` → `xdg-open`/`TextEdit.app`; `content2`=document
- `content1` as `explorer.exe` (optionally `+/select`) → `open`/`xdg-open`
- Otherwise `content1` as app, `content2` as parameters

`MessageBox` types: `MB_OK`, `MB_OKCANCEL`, `MB_YESNO`, `MB_YESNOCANCEL`,
`MB_ABORTRETRYIGNORE`, `MB_RETRYCANCEL`.

Returns: `IDOK=1`, `IDCANCEL=2`, `IDABORT=3`, `IDRETRY=4`, `IDIGNORE=5`,
`IDYES=6`, `IDNO=7`.

### 18.1 File dialogs

```c
char *BrowseForFiles(const char *text, const char *initialdir,
                     const char *initialfile, bool allowmul, const char *extlist)
bool  BrowseForSaveFile(const char *text, const char *initialdir,
                        const char *initialfile, const char *extlist,
                        char *fn, int fnsize)
bool  BrowseForDirectory(const char *text, const char *initialdir,
                         char *fn, int fnsize)
void  BrowseFile_SetTemplate(const char *dlgid, DLGPROC dlgProc,
                             struct SWELL_DialogResourceIndex *reshead)
```

`BrowseForFiles` returns a `malloc`'d string; caller must `free()`.
Multiple-file result format matches Win32 `GetOpenFileName` output (null-separated, double-null terminated; first entry is directory, remaining are filenames).

`extlist` format: alternating null-terminated description and filter strings (e.g. `"JPEG Files\0*.jpg\0All Files\0*.*\0\0"`).

---

## 19. Monitor / Display

```c
void SWELL_GetViewPort(RECT *r, const RECT *sourcerect, bool wantWork)
BOOL EnumDisplayMonitors(HDC, const LPRECT, MONITORENUMPROC, LPARAM)
BOOL GetMonitorInfo(HMONITOR, void *)  // fills MONITORINFO or MONITORINFOEX
int  GetSystemMetrics(int)
```

`SWELL_GetViewPort`: fills `r` with the screen rectangle that contains `sourcerect`. If `wantWork=true`, excludes menu bar and dock.

---

## 20. Focus rect

```c
void SWELL_DrawFocusRect(HWND hwndPar, RECT *rct, void **handle)
```

`rct=NULL` frees the handle. `rct` coordinates are in `hwndPar`-local space.
Implementation may use XOR, a separate bitmap overlay, or a separate window.

---

## 21. Window raise amount

```c
void SWELL_SetWindowWantRaiseAmt(HWND h, int amt)
int  SWELL_GetWindowWantRaiseAmt(HWND)
```

Hint to the window manager about how much to raise a window when clicked.

---

## 22. App lifecycle (non-macOS)

```c
void SWELL_initargs(int *argc, char ***argv)
void SWELL_RunMessageLoop()
HWND SWELL_CreateXBridgeWindow(HWND viewpar, void **wref, const RECT *)
void *SWELL_GetOSWindow(HWND hwnd, const char *type)   // type="SDL_Window"
void *SWELL_GetOSEvent(const char *type)               // type="SDL_Event"
```

---

## 23. App lifecycle (macOS)

```c
void SWELL_PostQuitMessage(void *sender)
void SWELL_EnsureMultithreadedCocoa()
void *SWELL_InitAutoRelease()
void  SWELL_QuitAutoRelease(void *p)
bool  SWELL_osx_is_dark_mode(int mode)   // 0=dark enabled, 1=dark allowed
int   SWELL_GetOSXVersion()
void  SWELL_DisableAppNap(int disable)
void  SWELL_DisableAppNapEx(int disable, int flag)
void  SWELL_SetWindowRepre(HWND hwnd, const char *fn, bool isDirty)
```

`SWELL_AutoReleaseHelper` is a C++ RAII class that calls `SWELL_InitAutoRelease`
/ `SWELL_QuitAutoRelease` on macOS; no-op elsewhere.

---

## 24. SWELLAppMain (application entry point)

Apps using `swellappmain.mm`/`swellappmain.h` implement:

```c
INT_PTR SWELLAppMain(int msg, INT_PTR parm1, INT_PTR parm2);
```

| msg | Meaning | parms |
|---|---|---|
| `SWELLAPP_ONLOAD` | Initialize app variables | — |
| `SWELLAPP_LOADED` | Create dialogs etc | — |
| `SWELLAPP_DESTROY` | Cleanup before quit | — |
| `SWELLAPP_SHOULDDESTROY` | Return 0 to allow quit, >0 to prevent | — |
| `SWELLAPP_OPENFILE` | File drag/open | parm1=`(const char *)path` |
| `SWELLAPP_NEWFILE` | New file request | return >0 if allowed |
| `SWELLAPP_SHOULDOPENNEWFILE` | Query open new file | return >0 if allowed |
| `SWELLAPP_ONCOMMAND` | Menu/command | parm1=`(int)`cmdID, parm2=`(id)`sender |
| `SWELLAPP_PROCESSMESSAGE` | Event preprocessing | parm1=`(MSG*)`, parm2=native event; return >0 to eat |
| `SWELLAPP_ACTIVATE` | App activate | parm1=`(bool)`isActive; return nonzero to suppress WM_ACTIVATEAPP |

---

## 25. DLL / Module entry

```c
BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID lpvReserved);
```

Implemented by app/plugin, called for `DLL_PROCESS_ATTACH`/`DLL_PROCESS_DETACH`
when linked with `swell-modstub-generic.cpp`.

---

## 26. PostMessage infrastructure (internal)

```c
BOOL SWELL_Internal_PostMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
void SWELL_Internal_PMQ_ClearAllMessages(HWND hwnd)
void SWELL_Internal_PostMessage_Init()
```

Apps using macOS must include these in the NSApp delegate via
`SWELL_POSTMESSAGE_DELEGATE_IMPL` and call `SWELL_POSTMESSAGE_INIT` from the
delegate.

---

## 27. Extended API

```c
void *SWELL_ExtendedAPI(const char *key, void *v)
unsigned int _controlfp(unsigned int flag, unsigned int mask)
```

`SWELL_ExtendedAPI` is a key-value extensibility escape hatch.
`_controlfp` is an FPU control word shim.

---

## 28. Dialog / Control Resource System

### 28.1 Dialog resource index

Dialogs are registered at static-init time via `SWELL_DEFINE_DIALOG_RESOURCE_BEGIN`
/ `SWELL_DEFINE_DIALOG_RESOURCE_END` macros (from `swell-dlggen.h`).

A `SWELL_DialogResourceIndex` linked list is accessible as
`SWELL_curmodule_dialogresource_head`.

Fields:
```c
const char *resid;          // MAKEINTRESOURCE(id)
const char *title;
int windowTypeFlags;        // SWELL_DLG_WS_* flags
void (*createFunc)(HWND, int);
int width, height;
```

Window type flags:
```c
SWELL_DLG_WS_CHILD     = 1
SWELL_DLG_WS_RESIZABLE = 2
SWELL_DLG_WS_FLIPPED   = 4
SWELL_DLG_WS_NOAUTOSIZE= 8
SWELL_DLG_WS_OPAQUE    = 16
SWELL_DLG_WS_DROPTARGET= 32
```

### 28.2 Control generation

```c
void SWELL_MakeSetCurParms(float xscale, float yscale, float xtrans, float ytrans,
                           HWND parent, bool doauto, bool dosizetofit)
HWND SWELL_MakeButton(int def, const char *label, int idx, int x, int y, int w, int h, int flags)
HWND SWELL_MakeEditField(int idx, int x, int y, int w, int h, int flags)
HWND SWELL_MakeLabel(int align, const char *label, int idx, int x, int y, int w, int h, int flags)
HWND SWELL_MakeControl(const char *cname, int idx, const char *classname,
                       int style, int x, int y, int w, int h, int exstyle)
HWND SWELL_MakeCombo(int idx, int x, int y, int w, int h, int flags)
HWND SWELL_MakeGroupBox(const char *name, int idx, int x, int y, int w, int h, int style)
HWND SWELL_MakeCheckBox(const char *name, int idx, int x, int y, int w, int h, int flags)
HWND SWELL_MakeListBox(int idx, int x, int y, int w, int h, int styles)
void SWELL_GenerateDialogFromList(const void *list, int listsz)
```

### 28.3 Built-in control class names (internal, used in resource entries)

```
__SWELL_BUTTON   → push button / checkbox / radio / groupbox
__SWELL_EDIT     → edit field
__SWELL_LABEL    → static text
__SWELL_COMBO    → combobox
__SWELL_GROUP    → group box
__SWELL_CHECKBOX → checkbox
__SWELL_LISTBOX  → listbox
__SWELL_ICON     → icon
```

External classnames used via `CONTROL`:
- `SysListView32` → ListView
- `SysTreeView32` → TreeView
- `SysTabControl32` → TabControl
- `msctls_trackbar32` → TrackBar
- `msctls_progress32` → ProgressBar
- `msctls_updown32` → UpDown (not fully implemented)
- Others → passed to `SWELL_RegisterCustomControlCreator` chain

### 28.4 Menu resource index

```c
typedef struct SWELL_MenuResourceIndex {
  const char *resid;
  void (*createFunc)(HMENU hMenu);
  struct SWELL_MenuResourceIndex *_next;
} SWELL_MenuResourceIndex;
extern SWELL_MenuResourceIndex *SWELL_curmodule_menuresource_head;
```

Menus defined via `SWELL_DEFINE_MENU_RESOURCE_BEGIN`/`SWELL_DEFINE_MENU_RESOURCE_END`.

---

## 29. Theme System (generic backend)

The generic backend exposes a `swell_colortheme` struct at `g_swell_ctheme` with
named integer color fields for every widget element. Names follow the pattern:

```
_3dface, _3dshadow, _3dhilight, _3ddkshadow
button_bg, button_text, button_text_disabled, button_shadow, button_hilight
checkbox_*, scrollbar, scrollbar_fg, scrollbar_bg
edit_*, info_*, menu_*, menubar_*
trackbar_*, progress
label_*, combo_*, listview_*, treeview_*, tab_*
focusrect, group_*, focus_hilight
```

DPI scaling:
```c
extern int g_swell_ui_scale;   // 256 = 1.0x
#define SWELL_UI_SCALE(x) (((x)*g_swell_ui_scale)/256)
```

---

## 30. Notify icon

```c
// NIM_ADD=0, NIM_MODIFY=1, NIM_DELETE=2
// NIF_MESSAGE=1, NIF_ICON=2, NIF_TIP=4
// Shell_NotifyIcon is not part of the SWELL API surface.
// NOTIFYICONDATA struct is defined for compat; actual notify icon
// functionality must be platform-specific.
```

---

## 31. Behavioral Notes and Win32 Deviations

1. **Thread safety**: Window functions are NOT thread-safe. Create/use windows from main thread only. Use `PostMessage` for cross-thread notifications.

2. **BOOL**: `signed char` not `int`. Never compare with `== TRUE`; check with `!= FALSE` or `if (val)`.

3. **RGB byte order**: Reversed from Win32 by default. Define `SWELL_USE_WIN32_RGB` for Win32 order.

4. **SW_* constants**: Values differ from Win32. Code comparing SW_* values numerically will break.

5. **LB_* message values**: Differ from Win32 (historical mistake). Do not hardcode.

6. **Coordinate system (macOS)**: y-axis may be flipped. `r.bottom < r.top` is possible from `GetWindowRect`. Functions handle negative height gracefully.

7. **GetDlgItem(hwnd, 0)**: Returns the window itself (or content view). On macOS, searches full view hierarchy; on generic, only immediate children.

8. **SetDlgItemText on edit (macOS)**: Does NOT send `WM_COMMAND/EN_CHANGE`. Generic and Win32 do.

9. **HGDIOBJ subtypes**: `HBITMAP`, `HICON`, `HBRUSH`, `HPEN`, `HFONT` are all `HGDIOBJ__*`. Cast freely; destroy all with `DeleteObject`.

10. **GWL_STYLE**: Only `BS_*` flags readable/writable for `NSButton` on macOS.

11. **KillTimer(hwnd, -1)**: Kills all timers for that window. Not standard Win32.

12. **PostMessage ordering**: Processed via NSTimer on macOS. Destroyed-window messages are discarded.

13. **IsWindow**: Expensive on macOS (enumerates all windows). Avoid.

14. **TrackPopupMenu rect**: Ignored.

15. **MAX_PATH**: 1024 (not 260).

16. **SWELL_BROKEN_RGB_ORDER**: Defined when default RGB order is used (non-Win32). Code dependent on exact byte layout must check this.

17. **Dialog scaling**: macOS uses dynamic DPI scaling. Generic uses 1.9x fixed by default. Override with `SWELL_DEFINE_DIALOG_RESOURCE_BEGIN` (with explicit scale) or `_BEGIN2` (using global default).

18. **GDI pooling**: HDC and HGDIOBJ structs are pooled (up to 100/200 objects) to reduce heap pressure.

19. **StretchBltFromMem** and **SWELL_GetScaling256**: Generic/non-macOS only.

20. **SWELL_EnableMetal**: macOS only. Call at most once per window.

---

## 32. Cursor Resources

```c
typedef struct SWELL_CursorResourceIndex {
  const char *resid;
  const char *resname;
  POINT hotspot;
  HCURSOR cachedCursor;
  struct SWELL_CursorResourceIndex *_next;
} SWELL_CursorResourceIndex;
```

Register cursor resources via `SWELL_Register_Cursor_Resource`. Load with
`SWELL_LoadCursor(resid)` or `LoadCursor(NULL, resid)`.

Predefined cursor IDs (via `MAKEINTRESOURCE`):
```c
IDC_ARROW     = MAKEINTRESOURCE(32512)
IDC_IBEAM     = MAKEINTRESOURCE(32513)
IDC_UPARROW   = MAKEINTRESOURCE(32516)
IDC_SIZENESW  = MAKEINTRESOURCE(32643)
IDC_SIZENWSE  = MAKEINTRESOURCE(32642)
IDC_SIZENS    = MAKEINTRESOURCE(32645)
IDC_SIZEWE    = MAKEINTRESOURCE(32644)
IDC_SIZEALL   = MAKEINTRESOURCE(32646)
IDC_NO        = MAKEINTRESOURCE(32648)
IDC_HAND      = MAKEINTRESOURCE(32649)
```

---

## 33. Font resources

```c
int AddFontResourceEx(LPCTSTR str, DWORD fl, void *pdv)
```

`str` = font file path. `fl=FR_PRIVATE` for private font (not system-wide).
`pdv` = reserved, pass NULL.

---

## 34. Color / Font dialogs

```c
bool SWELL_ChooseColor(HWND, COLORREF *, int ncustom, COLORREF *custom)
bool SWELL_ChooseFont(HWND, LOGFONT *)
```

---

## 35. SDL3-specific (Linux SDL3 backend)

```c
void *SWELL_GetOSWindow(HWND hwnd, const char *type)  // type="SDL_Window"
void *SWELL_GetOSEvent(const char *type)              // type="SDL_Event"
HWND  SWELL_CreateXBridgeWindow(HWND viewpar, void **wref, const RECT *)
```

---

## 36. macOS-specific (NSString bridge)

```c
void *SWELL_CStringToCFString(const char *str)    // returns NSString *
void  SWELL_CFStringToCString(const void *str, char *buf, int buflen)
void *GetNSImageFromHICON(HICON)                  // returns NSImage *
```

---

*End of SWELL API Specification*
