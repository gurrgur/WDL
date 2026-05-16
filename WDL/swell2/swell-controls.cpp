/*
  SWELL2 built-in control WNDPROCs.
  Button, Edit, Static, ListBox/ListView, TreeView, ComboBox,
  TabControl, Trackbar, ProgressBar.
*/

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "swell-internal.h"
#include "../wdlutf8.h"
#include <core/SkPath.h>
#include <cstring>
#include <cstdlib>
#include <cctype>

// ---------------------------------------------------------------------------
// Missing Win32 message/style constants not in swell-types.h
// ---------------------------------------------------------------------------
#ifndef WM_GETTEXT
#define WM_GETTEXT       0x000D
#define WM_GETTEXTLENGTH 0x000E
#endif

// BS styles: SWELL only defines AUTO variants; alias non-auto ones
#ifndef BS_CHECKBOX
#define BS_CHECKBOX    BS_AUTOCHECKBOX
#define BS_3STATE      BS_AUTO3STATE
#define BS_RADIOBUTTON BS_AUTORADIOBUTTON
#endif

// LVM messages
#ifndef LVM_FIRST
#define LVM_FIRST 0x1000
#endif
#define LVM_SETBKCOLOR                  (LVM_FIRST+1)
#define LVM_SETIMAGELIST                (LVM_FIRST+3)
#define LVM_GETITEMCOUNT                (LVM_FIRST+4)
#define LVM_GETITEM                     (LVM_FIRST+5)
#define LVM_SETITEM                     (LVM_FIRST+6)
#define LVM_INSERTITEM                  (LVM_FIRST+7)
#define LVM_DELETEITEM                  (LVM_FIRST+8)
#define LVM_DELETEALLITEMS              (LVM_FIRST+9)
#define LVM_GETNEXTITEM                 (LVM_FIRST+12)
#define LVM_GETITEMRECT                 (LVM_FIRST+14)
#define LVM_HITTEST                     (LVM_FIRST+18)
#define LVM_ENSUREVISIBLE               (LVM_FIRST+19)
#define LVM_SCROLL                      (LVM_FIRST+20)
#define LVM_REDRAWITEMS                 (LVM_FIRST+21)
#define LVM_GETCOLUMN                   (LVM_FIRST+25)
#define LVM_SETCOLUMN                   (LVM_FIRST+26)
#define LVM_INSERTCOLUMN                (LVM_FIRST+27)
#define LVM_DELETECOLUMN                (LVM_FIRST+28)
#define LVM_GETCOLUMNWIDTH              (LVM_FIRST+29)
#define LVM_SETCOLUMNWIDTH              (LVM_FIRST+30)
#define LVM_GETHEADER                   (LVM_FIRST+31)
#define LVM_GETCOLUMNCOUNT              (LVM_FIRST+101)
#define LVM_GETTOPINDEX                 (LVM_FIRST+39)
#define LVM_GETCOUNTPERPAGE             (LVM_FIRST+40)
#define LVM_GETSELECTEDCOUNT            (LVM_FIRST+50)
#define LVM_SETEXTENDEDLISTVIEWSTYLE    (LVM_FIRST+54)
#define LVM_GETEXTENDEDLISTVIEWSTYLE    (LVM_FIRST+55)
#define LVM_GETSUBITEMRECT              (LVM_FIRST+56)
#define LVM_SUBITEMHITTEST              (LVM_FIRST+57)
#define LVM_SETCOLUMNORDERARRAY         (LVM_FIRST+58)
#define LVM_GETCOLUMNORDERARRAY         (LVM_FIRST+59)
#define LVM_GETSELECTIONMARK            (LVM_FIRST+66)
#define LVM_GETITEMSTATE                (LVM_FIRST+44)
#define LVM_SETITEMSTATE                (LVM_FIRST+43)
#define LVM_GETITEMTEXT                 (LVM_FIRST+45)
#define LVM_SETITEMTEXT                 (LVM_FIRST+46)
#define LVM_SETITEMCOUNT                (LVM_FIRST+47)
#define LVM_SORTITEMS                   (LVM_FIRST+48)
#define LVM_SETTEXTCOLOR                (LVM_FIRST+36)
#define LVM_SETTEXTBKCOLOR              (LVM_FIRST+38)

// TCM messages
#ifndef TCM_FIRST
#define TCM_FIRST 0x1300
#endif
#define TCM_GETITEMCOUNT  (TCM_FIRST+4)
#define TCM_INSERTITEM    (TCM_FIRST+7)
#define TCM_DELETEITEM    (TCM_FIRST+8)
#define TCM_GETCURSEL     (TCM_FIRST+11)
#define TCM_SETCURSEL     (TCM_FIRST+12)
#define TCM_ADJUSTRECT    (TCM_FIRST+40)

#ifndef TCIF_TEXT
#define TCIF_TEXT 0x0001
#endif

// TVM messages
#ifndef TVM_FIRST
#define TVM_FIRST 0x1100
#endif
#define TVM_INSERTITEM     (TVM_FIRST+0)
#define TVM_DELETEITEM     (TVM_FIRST+1)
#define TVM_EXPAND         (TVM_FIRST+2)
#define TVM_SETINDENT      (TVM_FIRST+7)
#define TVM_SELECTITEM     (TVM_FIRST+11)
#define TVM_GETITEM        (TVM_FIRST+12)
#define TVM_SETITEM        (TVM_FIRST+13)
#define TVM_HITTEST        (TVM_FIRST+17)
#define TVM_GETNEXTITEM    (TVM_FIRST+10)
#define TVM_ENSUREVISIBLE  (TVM_FIRST+20)
#define TVM_SETBKCOLOR     (TVM_FIRST+29)
#define TVM_SETTEXTCOLOR   (TVM_FIRST+30)
#define TVM_GETCOUNT       (TVM_FIRST+5)
// SWELL2-specific tree navigation (no wParam packing needed)
#define TVM_DELETEALLITEMS (TVM_FIRST+60)
#define TVM_GETSELECTION   (TVM_FIRST+61)
#define TVM_GETPARENT      (TVM_FIRST+62)
#define TVM_GETCHILD       (TVM_FIRST+63)
#define TVM_GETNEXTSIBLING (TVM_FIRST+64)
#define TVM_GETROOT        (TVM_FIRST+65)

#ifndef TVC_UNKNOWN
#define TVC_UNKNOWN 0x0000
#endif
#ifndef TVC_BYMOUSE
#define TVC_BYMOUSE 0x0002
#endif

// TVGN constants for TreeView_GetNextItem / TVM_GETNEXTITEM
#ifndef TVGN_ROOT
#define TVGN_ROOT         0x0000
#define TVGN_NEXT         0x0001
#define TVGN_PREVIOUS     0x0002
#define TVGN_PARENT       0x0003
#define TVGN_CHILD        0x0004
#define TVGN_FIRSTVISIBLE 0x0005
#define TVGN_NEXTVISIBLE  0x0006
#define TVGN_PREVVISIBLE  0x0007
#define TVGN_DROPHILITE   0x0008
#define TVGN_CARET        0x0009
#endif

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static inline void notify_parent(HWND hwnd, int code)
{
  HWND par = GetParent(hwnd);
  if (par)
    SendMessage(par, WM_COMMAND, MAKEWPARAM(hwnd->m_id, code), (LPARAM)hwnd);
}

static inline HBRUSH get_window_brush(HWND hwnd, HDC hdc, UINT ctlmsg)
{
  HWND par = GetParent(hwnd);
  if (par) {
    LRESULT r = SendMessage(par, ctlmsg, (WPARAM)hdc, (LPARAM)hwnd);
    if (r && r != 1) return (HBRUSH)r;
    if (ctlmsg != WM_CTLCOLORDLG) {
      r = SendMessage(par, WM_CTLCOLORDLG, (WPARAM)hdc, (LPARAM)par);
      if (r && r != 1) return (HBRUSH)r;
    }
  }
  return NULL;
}

static inline int scaled_px(int px)
{
  int v = SWELL_UI_SCALE(px);
  return v > 0 ? v : 1;
}

static inline SkColor controls_to_sk(COLORREF c, uint8_t a = 255)
{
  return SkColorSetARGB(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

static void draw_check_mark(HDC hdc, const RECT &box, COLORREF color)
{
  if (!hdc || !hdc->canvas) return;
  const float w = (float)(box.right - box.left);
  const float h = (float)(box.bottom - box.top);
  if (w <= 0.0f || h <= 0.0f) return;

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(controls_to_sk(color));
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeCap(SkPaint::kRound_Cap);
  paint.setStrokeJoin(SkPaint::kRound_Join);
  paint.setStrokeWidth((float)scaled_px(2));

  const float l = (float)box.left;
  const float t = (float)box.top;
  SkPath ck;
  ck.moveTo(l + w * 0.25f, t + h * 0.53f);
  ck.lineTo(l + w * 0.43f, t + h * 0.69f);
  ck.lineTo(l + w * 0.76f, t + h * 0.32f);
  hdc->canvas->drawPath(ck, paint);
}

static void draw_mixed_mark(HDC hdc, const RECT &box, COLORREF color)
{
  if (!hdc || !hdc->canvas) return;
  const float w = (float)(box.right - box.left);
  const float h = (float)(box.bottom - box.top);
  if (w <= 0.0f || h <= 0.0f) return;

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(controls_to_sk(color));
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeCap(SkPaint::kRound_Cap);
  paint.setStrokeWidth((float)scaled_px(2));

  const float y = ((float)box.top + (float)box.bottom) * 0.5f;
  hdc->canvas->drawLine((float)box.left + w * 0.28f, y,
                        (float)box.right - w * 0.28f, y, paint);
}

// fill rect with background color from parent WM_CTLCOLOR* or default
static void fill_bg(HWND hwnd, HDC hdc, UINT ctlmsg, int defcol)
{
  HBRUSH br = get_window_brush(hwnd, hdc, ctlmsg);
  RECT cr; GetClientRect(hwnd, &cr);
  if (br) {
    FillRect(hdc, &cr, br);
  } else {
    COLORREF col = (COLORREF)defcol;
    HBRUSH nb = CreateSolidBrush(col);
    FillRect(hdc, &cr, nb);
    DeleteObject(nb);
  }
}

// ===========================================================================
// 1. buttonWindowProc
// ===========================================================================

static void uncheck_radio_group(HWND hwnd)
{
  HWND par = GetParent(hwnd);
  if (!par) return;
  // Walk parent children (sibling order matches tab/Z-order for radio groups)
  int n = par->m_children.GetSize();
  int myIdx = -1;
  for (int i = 0; i < n; i++) {
    if (par->m_children.Get(i) == hwnd) { myIdx = i; break; }
  }
  if (myIdx < 0) return;
  int grpStart = myIdx;
  for (int i = myIdx - 1; i >= 0; i--) {
    if (par->m_children.Get(i)->m_style & WS_GROUP) break;
    grpStart = i;
  }
  int grpEnd = myIdx + 1;
  for (int i = myIdx + 1; i < n; i++) {
    if (par->m_children.Get(i)->m_style & WS_GROUP) break;
    grpEnd = i + 1;
  }
  for (int i = grpStart; i < grpEnd; i++) {
    HWND sib = par->m_children.Get(i);
    if (!sib || sib == hwnd) continue;
    if ((sib->m_style & 0xFF) == BS_AUTORADIOBUTTON) {
      buttonWindowState *ss = (buttonWindowState *)(void *)sib->m_private_data;
      if (ss) { ss->state = BST_UNCHECKED; InvalidateRect(sib, NULL, FALSE); }
    }
  }
}

LRESULT buttonWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  buttonWindowState *st = (buttonWindowState *)(void *)hwnd->m_private_data;

  switch (msg) {
    case WM_CREATE: {
      st = new buttonWindowState();
      hwnd->m_private_data = (INT_PTR)st;
      DWORD style = hwnd->m_style;
      // GroupBox and static-like buttons don't want focus
      if ((style & BS_GROUPBOX) || (style & BS_OWNERDRAW))
        hwnd->m_wantfocus = false;
      else
        hwnd->m_wantfocus = true;
      return 0;
    }

    case WM_NCDESTROY:
      delete st;
      hwnd->m_private_data = 0;
      return 0;

    case BM_SETCHECK: {
      if (st) {
        int chk = ((int)wParam) & 3;
        st->state = (chk > 2 || chk < 0) ? BST_UNCHECKED : chk;
      }
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }

    case BM_GETCHECK:
      return st ? st->state : 0;

    case BM_SETIMAGE:
      if (st) {
        HANDLE prev = st->bitmap;
        st->bitmap_mode = (int)wParam;
        st->bitmap = (HICON)lParam;
        InvalidateRect(hwnd, NULL, FALSE);
        return (LRESULT)prev;
      }
      return 0;

    case BM_GETIMAGE:
      return st ? (LRESULT)st->bitmap : 0;

    case WM_SETTEXT:
      hwnd->m_title.Set((const char *)lParam);
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;

    case WM_GETTEXT:
      if (lParam && wParam > 0) {
        lstrcpyn((char *)lParam, hwnd->m_title.Get(), (int)wParam);
        return (LRESULT)strlen((char *)lParam);
      }
      return 0;

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;

    case WM_LBUTTONDOWN:
      SetFocus(hwnd);
      SetCapture(hwnd);
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;

    case WM_LBUTTONUP: {
      if (GetCapture() != hwnd) return 0;
      ReleaseCapture();
      RECT cr; GetClientRect(hwnd, &cr);
      int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
      bool inbounds = mx >= 0 && my >= 0 && mx < cr.right && my < cr.bottom;
      if (inbounds && st) {
        DWORD style = hwnd->m_style & 0xFF;
        if (style == BS_AUTOCHECKBOX)
          st->state = (st->state == BST_CHECKED) ? BST_UNCHECKED : BST_CHECKED;
        else if (style == BS_AUTO3STATE)
          st->state = (st->state + 1) % 3;
        else if (style == BS_AUTORADIOBUTTON) {
          st->state = BST_CHECKED;
          uncheck_radio_group(hwnd);
        }
        InvalidateRect(hwnd, NULL, FALSE);
        notify_parent(hwnd, BN_CLICKED);
      }
      return 0;
    }

    case WM_KEYDOWN:
      if (wParam == VK_SPACE && st) {
        DWORD style = hwnd->m_style & 0xFF;
        if (style == BS_AUTOCHECKBOX) {
          st->state = (st->state == BST_CHECKED) ? BST_UNCHECKED : BST_CHECKED;
          InvalidateRect(hwnd, NULL, FALSE);
        } else if (style == BS_AUTO3STATE) {
          st->state = (st->state + 1) % 3;
          InvalidateRect(hwnd, NULL, FALSE);
        } else if (style == BS_AUTORADIOBUTTON) {
          st->state = BST_CHECKED;
          uncheck_radio_group(hwnd);
          InvalidateRect(hwnd, NULL, FALSE);
        } else {
          return 0; // pushbutton / other — no key-toggle
        }
        notify_parent(hwnd, BN_CLICKED);
      } else if (wParam == VK_RETURN && (hwnd->m_style & 0xF) == 0 && st) {
        // Enter on push buttons only (style & 0xF == 0 means pushbutton)
        notify_parent(hwnd, BN_CLICKED);
      }
      return 0;

    case WM_CAPTURECHANGED:
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;

    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      if (!hdc) return 0;

      const swell_theme &th = g_swell_theme;
      RECT cr; GetClientRect(hwnd, &cr);
      DWORD style = hwnd->m_style;
      DWORD bstyle = style & 0x0F; // bottom nibble = Win32 button type
      bool enabled = IsWindowEnabled(hwnd);
      bool focused = (GetFocus() == hwnd);
      bool pressed = (GetCapture() == hwnd);

      if (style & BS_GROUPBOX) {
        // Modern group box: subtle filled card with rounded corners and a
        // floating title that overlaps the top border.
        fill_bg(hwnd, hdc, WM_CTLCOLORBTN, th.bg_window);

        HFONT fnt = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
        HGDIOBJ oldfont = SelectObject(hdc, fnt);
        TEXTMETRIC tm; GetTextMetrics(hdc, &tm);
        int titlegap = tm.tmHeight / 2;

        HPEN pen = CreatePen(PS_SOLID, th.border_width, (COLORREF)th.border);
        HBRUSH frameBr = CreateSolidBrush((COLORREF)th.bg_surface);
        HGDIOBJ oldpen = SelectObject(hdc, pen);
        HGDIOBJ oldbr  = SelectObject(hdc, frameBr);
        const int r = th.corner_radius;
        RoundRect(hdc, cr.left, cr.top + titlegap,
                  cr.right, cr.bottom, r*2, r*2);
        SelectObject(hdc, oldpen); DeleteObject(pen);
        SelectObject(hdc, oldbr);  DeleteObject(frameBr);

        if (hwnd->m_title.GetLength() > 0) {
          SetTextColor(hdc, enabled ? (COLORREF)th.fg_text
                                    : (COLORREF)th.fg_text_disabled);
          RECT tr = { cr.left + r + 4, cr.top,
                      cr.right - r - 4, cr.top + titlegap * 2 };
          HBRUSH titleBg = get_window_brush(hwnd, hdc, WM_CTLCOLORBTN);
          if (titleBg) {
            FillRect(hdc, &tr, titleBg);
          } else {
            HBRUSH tb = CreateSolidBrush((COLORREF)th.bg_window);
            FillRect(hdc, &tr, tb);
            DeleteObject(tb);
          }
          SetBkMode(hdc, TRANSPARENT);
          SWELL_DrawText(hdc, hwnd->m_title.Get(), -1, &tr,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(hdc, oldfont);
        EndPaint(hwnd, &ps);
        return 0;
      }

      // Push button / check box / radio button
      bool is_check = (bstyle == BS_AUTOCHECKBOX || bstyle == BS_AUTO3STATE ||
                       bstyle == BS_CHECKBOX || bstyle == BS_3STATE);
      bool is_radio = (bstyle == BS_AUTORADIOBUTTON || bstyle == BS_RADIOBUTTON);

      COLORREF fgcol = enabled ? (COLORREF)th.fg_text
                               : (COLORREF)th.fg_text_disabled;

      if (is_check || is_radio) {
        int box_size = th.checkbox_size;
        if (box_size > cr.bottom - cr.top - 2) box_size = cr.bottom - cr.top - 2;
        int box_x = cr.left + 2;
        int box_y = cr.top + (cr.bottom - cr.top - box_size) / 2;
        RECT box = { box_x, box_y, box_x + box_size, box_y + box_size };

        int cstate = st ? st->state : 0;
        bool checked = (cstate != BST_UNCHECKED);

        // Indicator fill: accent when checked, input bg otherwise.
        COLORREF ind_bg = checked && enabled
            ? (COLORREF)th.accent
            : (COLORREF)th.bg_input;
        COLORREF ind_border = checked && enabled
            ? (COLORREF)th.accent
            : (COLORREF)th.border_strong;
        if (!enabled) {
          ind_bg = (COLORREF)th.bg_input_alt;
          ind_border = (COLORREF)th.border;
        }

        HPEN pen   = CreatePen(PS_SOLID, th.border_width, ind_border);
        HBRUSH ibr = CreateSolidBrush(ind_bg);
        HGDIOBJ oldpen = SelectObject(hdc, pen);
        HGDIOBJ oldbr  = SelectObject(hdc, ibr);
        if (is_radio) {
          Ellipse(hdc, box.left, box.top, box.right, box.bottom);
        } else {
          int rr = th.corner_radius / 2; if (rr < 2) rr = 2;
          RoundRect(hdc, box.left, box.top, box.right, box.bottom, rr*2, rr*2);
        }
        SelectObject(hdc, oldpen); DeleteObject(pen);
        SelectObject(hdc, oldbr);  DeleteObject(ibr);

        if (checked) {
          COLORREF mark = enabled ? (COLORREF)th.fg_on_accent
                                  : (COLORREF)th.fg_text_disabled;
          if (is_radio) {
            HBRUSH fb = CreateSolidBrush(mark);
            HGDIOBJ ob = SelectObject(hdc, fb);
            HPEN np = (HPEN)GetStockObject(NULL_PEN);
            HGDIOBJ op = SelectObject(hdc, np);
            int m = box_size / 4;
            Ellipse(hdc, box.left+m, box.top+m, box.right-m, box.bottom-m);
            SelectObject(hdc, ob); DeleteObject(fb);
            SelectObject(hdc, op);
          } else if (cstate == BST_INDETERMINATE) {
            draw_mixed_mark(hdc, box, mark);
          } else {
            draw_check_mark(hdc, box, mark);
          }
        }

        // label
        HFONT f = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
        SelectObject(hdc, f);
        SetTextColor(hdc, fgcol);
        SetBkMode(hdc, TRANSPARENT);
        RECT tr = { box.right + 6, cr.top, cr.right, cr.bottom };
        SWELL_DrawText(hdc, hwnd->m_title.Get(), -1, &tr,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        if (focused) {
          // Focus ring around the indicator
          HPEN fp = CreatePen(PS_SOLID, th.focus_ring_width,
                              (COLORREF)th.focus_ring);
          HGDIOBJ op = SelectObject(hdc, fp);
          HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
          HGDIOBJ ob = SelectObject(hdc, nb);
          int o = th.focus_ring_offset;
          if (is_radio)
            Ellipse(hdc, box.left-o, box.top-o, box.right+o, box.bottom+o);
          else {
            int rr = th.corner_radius / 2; if (rr < 2) rr = 2;
            RoundRect(hdc, box.left-o, box.top-o, box.right+o, box.bottom+o,
                      (rr+o)*2, (rr+o)*2);
          }
          SelectObject(hdc, op); DeleteObject(fp);
          SelectObject(hdc, ob);
        }
      } else {
        // Push button
        bool isdef = (style & BS_DEFPUSHBUTTON) != 0;

        COLORREF fillcol;
        COLORREF bordercol;
        COLORREF textcol;
        if (!enabled) {
          fillcol   = (COLORREF)th.bg_input_alt;
          bordercol = (COLORREF)th.border;
          textcol   = (COLORREF)th.fg_text_disabled;
        } else if (isdef) {
          // Accent-colored default button
          fillcol   = pressed ? (COLORREF)th.accent_pressed
                              : (COLORREF)th.accent;
          bordercol = pressed ? (COLORREF)th.accent_pressed
                              : (COLORREF)th.accent_hover;
          textcol   = (COLORREF)th.fg_on_accent;
        } else {
          fillcol   = pressed ? (COLORREF)th.bg_button_pressed
                              : (COLORREF)th.bg_button;
          bordercol = (COLORREF)th.border_strong;
          textcol   = (COLORREF)th.fg_text;
        }

        HPEN pen   = CreatePen(PS_SOLID, th.border_width, bordercol);
        HBRUSH br  = CreateSolidBrush(fillcol);
        HGDIOBJ op = SelectObject(hdc, pen);
        HGDIOBJ ob = SelectObject(hdc, br);
        const int r = th.corner_radius;
        RoundRect(hdc, cr.left, cr.top, cr.right, cr.bottom,
                  r * 2, r * 2);
        SelectObject(hdc, op); DeleteObject(pen);
        SelectObject(hdc, ob); DeleteObject(br);

        HFONT f = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
        SelectObject(hdc, f);
        SetTextColor(hdc, textcol);
        SetBkMode(hdc, TRANSPARENT);
        SWELL_DrawText(hdc, hwnd->m_title.Get(), -1, &cr,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        if (focused) {
          HPEN fp = CreatePen(PS_SOLID, th.focus_ring_width,
                              (COLORREF)th.focus_ring);
          HGDIOBJ ofp = SelectObject(hdc, fp);
          HGDIOBJ ofb = SelectObject(hdc, GetStockObject(NULL_BRUSH));
          RoundRect(hdc, cr.left, cr.top, cr.right, cr.bottom, r*2, r*2);
          SelectObject(hdc, ofp); DeleteObject(fp);
          SelectObject(hdc, ofb);
        }
      }

      EndPaint(hwnd, &ps);
      return 0;
    }

    default:
      return DefWindowProc(hwnd, msg, wParam, lParam);
  }
}

// ===========================================================================
// 2. editWindowProc
// ===========================================================================

static int edit_pos_from_xy(HWND hwnd, int mx, int my, __SWELL_editControlState *st)
{
  const char *txt_orig = hwnd->m_title.Get();
  int tlen_orig = (int)strlen(txt_orig);
  bool multiline = (hwnd->m_style & ES_MULTILINE) != 0;

  RECT cr; GetClientRect(hwnd, &cr);
  const swell_theme &th = g_swell_theme;
  RECT tr = { cr.left + th.padding_edit_h, cr.top + th.padding_edit_v,
              cr.right - th.padding_edit_h, cr.bottom - th.padding_edit_v };

  HDC hdc = GetDC(hwnd);
  HFONT f = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
  if (f) SelectObject(hdc, f);
  SkFont skfont = swell_make_skfont_from_hdc(hdc);

  int result = 0;

  if (!multiline) {
    if (mx > tr.left && tlen_orig > 0) {
      int lo = 0, hi = tlen_orig;
      while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        SkRect bounds;
        skfont.measureText(txt_orig, mid, SkTextEncoding::kUTF8, &bounds);
        int textW = (int)(bounds.width() + 0.5f);
        if (tr.left + textW <= mx)
          lo = mid;
        else
          hi = mid - 1;
      }
      result = lo;
    }
  } else {
    WDL_FastString stripped;
    for (int i = 0; i < tlen_orig; i++) {
      if (txt_orig[i] == '\r') {
        if (i+1 < tlen_orig && txt_orig[i+1] == '\n') { stripped.Append("\n"); i++; }
        else stripped.Append("\n");
      } else {
        char cbuf[2] = { txt_orig[i], 0 };
        stripped.Append(cbuf);
      }
    }
    const char *txt = stripped.Get();
    int tlen = (int)stripped.GetLength();

    TEXTMETRIC tm; GetTextMetrics(hdc, &tm);
    int rowH = tm.tmHeight + 2;
    int scrollY = st->scroll_y;

    if (st->ml_dline_starts.GetSize() > 0 && tlen > 0) {
      int ndlines = st->ml_dline_starts.GetSize();
      int di = (my + scrollY - tr.top) / rowH;
      if (di < 0) di = 0;
      if (di >= ndlines) di = ndlines - 1;

      int d0 = st->ml_dline_starts.Get()[di];
      int d1 = st->ml_dline_ends.Get()[di];
      int seglen = d1 - d0;

      if (mx <= tr.left) {
        result = d0;
      } else if (seglen > 0) {
        int lo = 0, hi = seglen;
        while (lo < hi) {
          int mid = (lo + hi + 1) / 2;
          SkRect bounds;
          skfont.measureText(txt + d0, mid, SkTextEncoding::kUTF8, &bounds);
          int textW = (int)(bounds.width() + 0.5f);
          if (tr.left + textW <= mx)
            lo = mid;
          else
            hi = mid - 1;
        }
        result = d0 + lo;
      } else {
        result = d0;
      }
    }

    // convert stripped index to original text index
    int oi = 0, si = 0;
    while (oi < tlen_orig && si < result) {
      if (txt_orig[oi] == '\r') { oi++; }
      else { oi++; si++; }
    }
    result = oi;
  }

  ReleaseDC(hwnd, hdc);
  // result is a byte offset into the UTF-8 string; convert to character position
  result = WDL_utf8_bytepos_to_charpos(txt_orig, result);
  return result;
}

static void calcScroll(int wh, int totalw, int scroll_x, int *thumbsz, int *thumbpos)
{
  if (wh <= 0 || totalw <= 0) { *thumbsz = wh > 0 ? wh : 0; *thumbpos = 0; return; }
  const double isz = wh / (double) totalw;
  int sz = (int)(wh * isz + 0.5);
  if (sz < g_swell_theme.scrollbar_min_thumb_height)
    sz = g_swell_theme.scrollbar_min_thumb_height;

  *thumbpos = (int)(scroll_x * isz + 0.5);
  if (*thumbpos >= wh - sz) *thumbpos = wh - sz;

  *thumbsz = sz;
}

static void drawHorizontalScrollbar(HDC hdc, RECT cr, int vieww, int totalw, int scroll_x, bool hover)
{
  if (totalw <= vieww) return;
  const swell_theme &th = g_swell_theme;

  RECT tr = { cr.left, cr.bottom - th.scrollbar_width, cr.right, cr.bottom };
  HBRUSH tb = CreateSolidBrush((COLORREF)th.bg_scrollbar);
  FillRect(hdc, &tr, tb);
  DeleteObject(tb);

  int thumbcol = hover ? th.scrollbar_thumb_hover : th.scrollbar_thumb;

  int thumbsz, thumbpos;
  calcScroll(vieww, totalw, scroll_x, &thumbsz, &thumbpos);

  HBRUSH br  = CreateSolidBrush((COLORREF)thumbcol);
  HPEN np = (HPEN)GetStockObject(NULL_PEN);
  const int min_thumb = scaled_px(2);
  int margin = scaled_px(3);
  if (thumbsz < margin * 2 + min_thumb)
    margin = thumbsz > min_thumb ? (thumbsz - min_thumb) / 2 : 0;
  if (th.scrollbar_width < margin * 2 + min_thumb)
    margin = th.scrollbar_width > min_thumb ? (th.scrollbar_width - min_thumb) / 2 : 0;
  RECT fr = { cr.left + thumbpos + margin,
              cr.bottom - th.scrollbar_width + margin,
              cr.left + thumbpos + thumbsz - margin,
              cr.bottom - margin };
  if (fr.right > fr.left && fr.bottom > fr.top) {
    HGDIOBJ op = SelectObject(hdc, np);
    HGDIOBJ ob = SelectObject(hdc, br);
    int rr = (fr.bottom - fr.top) / 2;
    const int maxr = scaled_px(4);
    const int minr = scaled_px(2);
    if (rr > maxr) rr = maxr;
    if (rr < minr) rr = minr;
    RoundRect(hdc, fr.left, fr.top, fr.right, fr.bottom, rr*2, rr*2);
    SelectObject(hdc, op);
    SelectObject(hdc, ob);
  }

  DeleteObject(br);
}

static void drawVerticalScrollbar(HDC hdc, RECT cr, int viewh, int totalh, int scroll_y, bool hover)
{
  if (totalh <= viewh) return;
  const swell_theme &th = g_swell_theme;

  RECT tr = { cr.right - th.scrollbar_width, cr.top, cr.right, cr.bottom };
  HBRUSH tb = CreateSolidBrush((COLORREF)th.bg_scrollbar);
  FillRect(hdc, &tr, tb);
  DeleteObject(tb);

  int thumbcol = hover ? th.scrollbar_thumb_hover : th.scrollbar_thumb;

  int thumbsz, thumbpos;
  calcScroll(viewh, totalh, scroll_y, &thumbsz, &thumbpos);

  HBRUSH br  = CreateSolidBrush((COLORREF)thumbcol);
  HPEN np = (HPEN)GetStockObject(NULL_PEN);
  const int min_thumb = scaled_px(2);
  int margin = scaled_px(3);
  if (thumbsz < margin * 2 + min_thumb)
    margin = thumbsz > min_thumb ? (thumbsz - min_thumb) / 2 : 0;
  if (th.scrollbar_width < margin * 2 + min_thumb)
    margin = th.scrollbar_width > min_thumb ? (th.scrollbar_width - min_thumb) / 2 : 0;
  RECT fr = { cr.right - th.scrollbar_width + margin,
              cr.top + thumbpos + margin,
              cr.right - margin,
              cr.top + thumbpos + thumbsz - margin };
  if (fr.right > fr.left && fr.bottom > fr.top) {
    HGDIOBJ op = SelectObject(hdc, np);
    HGDIOBJ ob = SelectObject(hdc, br);
    int rr = (fr.right - fr.left) / 2;
    const int maxr = scaled_px(4);
    const int minr = scaled_px(2);
    if (rr > maxr) rr = maxr;
    if (rr < minr) rr = minr;
    RoundRect(hdc, fr.left, fr.top, fr.right, fr.bottom, rr*2, rr*2);
    SelectObject(hdc, op);
    SelectObject(hdc, ob);
  }

  DeleteObject(br);
}

static int scrollFromThumbPos(int thumb_pos, int view, int total)
{
  if (view <= 0 || total <= 0) return 0;
  return (int)(thumb_pos * (double)total / view + 0.5);
}

// Returns thumb bounds in scrollbar-local coordinates (0..view)
static void getVThumb(int viewh, int totalh, int scroll_y, int *top, int *bottom)
{
  if (totalh <= viewh) { *top = 0; *bottom = viewh; return; }
  int sz, pos;
  calcScroll(viewh, totalh, scroll_y, &sz, &pos);
  *top = pos;
  *bottom = pos + sz;
}

static void getHThumb(int vieww, int totalw, int scroll_x, int *left, int *right)
{
  if (totalw <= vieww) { *left = 0; *right = vieww; return; }
  int sz, pos;
  calcScroll(vieww, totalw, scroll_x, &sz, &pos);
  *left = pos;
  *right = pos + sz;
}

// Hit test vertical scrollbar: 0=none, 1=above thumb, 2=on thumb, 3=below thumb
static int hitVScrollbar(RECT cr, int viewh, int totalh, int scroll_y, int my)
{
  if (totalh <= viewh) return 0;
  const swell_theme &th = g_swell_theme;
  int sb_left = cr.right - th.scrollbar_width;
  if (my < cr.top || my >= cr.bottom) return 0;
  int ttop, tbot;
  getVThumb(viewh, totalh, scroll_y, &ttop, &tbot);
  int ry = my - cr.top;
  if (ry < ttop) return 1;
  if (ry < tbot) return 2;
  return 3;
}

// Hit test horizontal scrollbar
static int hitHScrollbar(RECT cr, int vieww, int totalw, int scroll_x, int mx)
{
  if (totalw <= vieww) return 0;
  const swell_theme &th = g_swell_theme;
  int sb_top = cr.bottom - th.scrollbar_width;
  if (mx < cr.left || mx >= cr.right) return 0;
  int tleft, tright;
  getHThumb(vieww, totalw, scroll_x, &tleft, &tright);
  int rx = mx - cr.left;
  if (rx < tleft) return 1;
  if (rx < tright) return 2;
  return 3;
}

LRESULT editWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  __SWELL_editControlState *st =
      (__SWELL_editControlState *)(void *)hwnd->m_private_data;

  switch (msg) {
    case WM_CREATE:
      st = new __SWELL_editControlState();
      hwnd->m_private_data = (INT_PTR)st;
      hwnd->m_wantfocus = true;
      return 0;

    case WM_NCDESTROY:
      delete st;
      hwnd->m_private_data = 0;
      return 0;

    case WM_SETTEXT: {
      const char *s = (const char *)lParam;
      if (s && (hwnd->m_style & ES_MULTILINE)) {
        // strip \r, normalize \r\n to \n
        WDL_FastString clean;
        for (const char *p = s; *p; p++) {
          if (*p == '\r') {
            if (*(p+1) == '\n') continue; // skip \r before \n
            clean.Append("\n", 1);
          } else {
            char cb[2] = { *p, 0 };
            clean.Append(cb);
          }
        }
        hwnd->m_title.Set(clean.Get());
      } else {
        hwnd->m_title.Set(s ? s : "");
      }
      if (st) { st->cursor_pos = 0; st->sel1 = -1; st->sel2 = -1; }
      InvalidateRect(hwnd, NULL, FALSE);
      notify_parent(hwnd, EN_CHANGE);
      return 0;
    }

    case WM_GETTEXT:
      if (lParam && wParam > 0) {
        lstrcpyn((char *)lParam, hwnd->m_title.Get(), (int)wParam);
        return (LRESULT)strlen((char *)lParam);
      }
      return 0;

    case WM_GETTEXTLENGTH:
      return hwnd->m_title.GetLength();

    case EM_GETSEL: {
      if (!st) { if (wParam) *(int *)wParam = 0; if (lParam) *(int *)lParam = 0; return 0; }
      if (st->sel1 < 0) {
        if (wParam) *(int *)wParam = -1;
        if (lParam) *(int *)lParam = -1;
        return (LRESULT)-1;
      }
      int s1 = st->sel1 < st->sel2 ? st->sel1 : st->sel2;
      int s2 = st->sel1 < st->sel2 ? st->sel2 : st->sel1;
      if (wParam) *(int *)wParam = s1;
      if (lParam) *(int *)lParam = s2;
      return MAKELPARAM(s1, s2);
    }

    case EM_SETSEL:
      if (st) {
        st->sel1 = (int)wParam;
        st->sel2 = (int)lParam;
        if (st->sel1 == 0 && st->sel2 < 0) st->sel2 = WDL_utf8_get_charlen(hwnd->m_title.Get());
        st->cursor_pos = st->sel2 < 0 ? st->sel1 : st->sel2;
        InvalidateRect(hwnd, NULL, FALSE);
      }
      return 0;

    case EM_REPLACESEL: {
      const char *newtext = (const char *)lParam;
      if (!st || !newtext) return 0;
      const char *t = hwnd->m_title.Get();
      int tlen_chars = WDL_utf8_get_charlen(t);
      int s1 = st->sel1 < 0 ? st->cursor_pos : (st->sel1 < st->sel2 ? st->sel1 : st->sel2);
      int s2 = st->sel1 < 0 ? st->cursor_pos : (st->sel1 < st->sel2 ? st->sel2 : st->sel1);
      if (s1 < 0) s1 = 0; if (s1 > tlen_chars) s1 = tlen_chars;
      if (s2 < s1) s2 = s1; if (s2 > tlen_chars) s2 = tlen_chars;
      int bs1 = WDL_utf8_charpos_to_bytepos(t, s1);
      int bs2 = WDL_utf8_charpos_to_bytepos(t, s2);
      WDL_FastString ns;
      ns.Set(t, bs1);
      ns.Append(newtext);
      ns.Append(t + bs2);
      hwnd->m_title.Set(ns.Get());
      int newtext_chars = WDL_utf8_get_charlen(newtext);
      st->cursor_pos = s1 + newtext_chars;
      st->sel1 = st->sel2 = -1;
      InvalidateRect(hwnd, NULL, FALSE);
      notify_parent(hwnd, EN_CHANGE);
      return 0;
    }

    case WM_SETFOCUS:
      if (st) {
        st->cursor_state = 1;
        st->cursor_timer = SetTimer(hwnd, 100, 500, NULL);
      }
      InvalidateRect(hwnd, NULL, FALSE);
      notify_parent(hwnd, EN_SETFOCUS);
      return 0;

    case WM_KILLFOCUS:
      if (st) {
        KillTimer(hwnd, st->cursor_timer);
        st->cursor_state = 0;
        st->cursor_timer = 0;
      }
      InvalidateRect(hwnd, NULL, FALSE);
      notify_parent(hwnd, EN_KILLFOCUS);
      return 0;

    case WM_TIMER:
      if (st && (UINT_PTR)wParam == (UINT_PTR)st->cursor_timer) {
        st->cursor_state ^= 1;
        InvalidateRect(hwnd, NULL, FALSE);
      }
      return 0;

    case WM_CHAR: {
      if (!st || (hwnd->m_style & ES_READONLY)) return 0;
      unsigned int ch = (unsigned int)wParam;
      if (ch < 32 && ch != '\r' && ch != '\n') return 0;
      if ((hwnd->m_style & ES_NUMBER) && !isdigit(ch)) return 0;
      // multiline: allow newline only if ES_MULTILINE
      if (ch == '\r' || ch == '\n') {
        if (!(hwnd->m_style & ES_MULTILINE)) return 0;
        ch = '\n';
      }
      // encode as UTF-8 (handles non-ASCII from SDL_TEXTINPUT)
      char ins[8] = { 0 };
      if (ch < 0x80) {
        ins[0] = (char)ch;
      } else if (ch < 0x800) {
        ins[0] = (char)(0xC0 | (ch >> 6));
        ins[1] = (char)(0x80 | (ch & 0x3F));
      } else if (ch < 0x10000) {
        ins[0] = (char)(0xE0 | (ch >> 12));
        ins[1] = (char)(0x80 | ((ch >> 6) & 0x3F));
        ins[2] = (char)(0x80 | (ch & 0x3F));
      } else {
        ins[0] = (char)(0xF0 | (ch >> 18));
        ins[1] = (char)(0x80 | ((ch >> 12) & 0x3F));
        ins[2] = (char)(0x80 | ((ch >> 6) & 0x3F));
        ins[3] = (char)(0x80 | (ch & 0x3F));
      }
      SendMessage(hwnd, EM_REPLACESEL, 0, (LPARAM)ins);
      return 0;
    }

    case WM_MOUSEWHEEL:
      if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState(VK_MENU) & 0x8000))
        return DefWindowProc(hwnd, msg, wParam, lParam);
      if (st && (hwnd->m_style & ES_MULTILINE)) {
        int delta = (short)HIWORD(wParam);
        st->scroll_y -= delta / 40 * (st->max_height > 0 ? st->max_height : 16);
        if (st->scroll_y < 0) st->scroll_y = 0;
        InvalidateRect(hwnd, NULL, FALSE);
      }
      return 0;

    case WM_KEYDOWN: {
      if (!st) return 0;
      const char *t = hwnd->m_title.Get();
      int len = (int)strlen(t);
      int nch = WDL_utf8_get_charlen(t);
      bool shift = (lParam & FSHIFT) != 0;
      bool ctrl  = (lParam & FCONTROL) != 0;

      // --- Clipboard shortcuts (Ctrl+C/X/V) ---
      if (ctrl && (wParam == 'C' || wParam == 'c' ||
                   wParam == 'X' || wParam == 'x' ||
                   wParam == 'V' || wParam == 'v')) {
        if (wParam == 'C' || wParam == 'c' || wParam == 'X' || wParam == 'x') {
          if (st->sel1 >= 0 && st->sel1 != st->sel2) {
            int cs1 = st->sel1 < st->sel2 ? st->sel1 : st->sel2;
            int cs2 = st->sel1 < st->sel2 ? st->sel2 : st->sel1;
            int bs1 = WDL_utf8_charpos_to_bytepos(t, cs1);
            int bs2 = WDL_utf8_charpos_to_bytepos(t, cs2);
            if (bs2 <= len) {
              int blen = bs2 - bs1;
              HANDLE h = GlobalAlloc(GMEM_MOVEABLE, blen + 1);
              if (h) {
                char *dst = (char *)GlobalLock(h);
                memcpy(dst, t + bs1, blen);
                dst[blen] = 0;
                GlobalUnlock(h);
                if (OpenClipboard(hwnd)) {
                  EmptyClipboard();
                  SetClipboardData(CF_TEXT, h);
                  CloseClipboard();
                } else {
                  GlobalFree(h);
                }
              }
            }
          }
          if ((wParam == 'X' || wParam == 'x') &&
              !(hwnd->m_style & ES_READONLY))
            SendMessage(hwnd, EM_REPLACESEL, 0, (LPARAM)"");
        } else {
          if (!(hwnd->m_style & ES_READONLY) && OpenClipboard(hwnd)) {
            HANDLE h = GetClipboardData(CF_TEXT);
            if (h) {
              const char *src = (const char *)GlobalLock(h);
              if (src) {
                SendMessage(hwnd, EM_REPLACESEL, 0, (LPARAM)src);
                GlobalUnlock(h);
              }
            }
            CloseClipboard();
          }
        }
        return 1;
      }

      if (wParam == VK_BACK) {
        if (hwnd->m_style & ES_READONLY) return 1;
        if (st->sel1 >= 0 && st->sel1 != st->sel2) {
          SendMessage(hwnd, EM_REPLACESEL, 0, (LPARAM)"");
        } else if (st->cursor_pos > 0) {
          st->cursor_pos--;
          st->sel1 = st->cursor_pos;
          st->sel2 = st->cursor_pos + 1;
          SendMessage(hwnd, EM_REPLACESEL, 0, (LPARAM)"");
        }
        return 1;
      }
      if (wParam == VK_DELETE) {
        if (hwnd->m_style & ES_READONLY) return 1;
        if (st->sel1 >= 0 && st->sel1 != st->sel2) {
          SendMessage(hwnd, EM_REPLACESEL, 0, (LPARAM)"");
        } else if (st->cursor_pos < nch) {
          st->sel1 = st->cursor_pos;
          st->sel2 = st->cursor_pos + 1;
          SendMessage(hwnd, EM_REPLACESEL, 0, (LPARAM)"");
        }
        return 1;
      }
      if (wParam == VK_HOME) {
        st->cursor_pos = 0;
        if (!shift) { st->sel1 = st->sel2 = -1; }
        InvalidateRect(hwnd, NULL, FALSE);
        return 1;
      }
      if (wParam == VK_END) {
        st->cursor_pos = nch;
        if (!shift) { st->sel1 = st->sel2 = -1; }
        InvalidateRect(hwnd, NULL, FALSE);
        return 1;
      }
      if (wParam == VK_LEFT) {
        if (st->cursor_pos > 0) st->cursor_pos--;
        if (!shift) { st->sel1 = st->sel2 = -1; }
        InvalidateRect(hwnd, NULL, FALSE);
        return 1;
      }
      if (wParam == VK_RIGHT) {
        if (st->cursor_pos < nch) st->cursor_pos++;
        if (!shift) { st->sel1 = st->sel2 = -1; }
        InvalidateRect(hwnd, NULL, FALSE);
        return 1;
      }
      if (hwnd->m_style & ES_MULTILINE) {
        // build line-offset arrays (byte positions)
        WDL_TypedBuf<int> lineStarts, lineEnds;
        lineStarts.Add(0);
        for (int i = 0; i < len; i++) { if (t[i] == '\n') { lineEnds.Add(i); lineStarts.Add(i+1); } }
        lineEnds.Add(len);
        // convert cursor position to byte offset for line-finding
        int cursBpos = WDL_utf8_charpos_to_bytepos(t, st->cursor_pos);
        // find current logical line
        int curLine = 0;
        for (int li = 0; li < lineStarts.GetSize(); li++) {
          if (cursBpos >= lineStarts.Get()[li] && cursBpos <= lineEnds.Get()[li]) { curLine = li; break; }
        }
        if (wParam == VK_UP) {
          if (curLine > 0) {
            int colChars = WDL_utf8_bytepos_to_charpos(
                t + lineStarts.Get()[curLine], cursBpos - lineStarts.Get()[curLine]);
            int prevByteLen = lineEnds.Get()[curLine-1] - lineStarts.Get()[curLine-1];
            int prevChars = WDL_utf8_bytepos_to_charpos(
                t + lineStarts.Get()[curLine-1], prevByteLen);
            if (colChars > prevChars) colChars = prevChars;
            int newBpos = lineStarts.Get()[curLine-1] +
                          WDL_utf8_charpos_to_bytepos(t + lineStarts.Get()[curLine-1], colChars);
            st->cursor_pos = WDL_utf8_bytepos_to_charpos(t, newBpos);
            if (!shift) { st->sel1 = st->sel2 = -1; }
            InvalidateRect(hwnd, NULL, FALSE);
          }
          return 1;
        }
        if (wParam == VK_DOWN) {
          if (curLine + 1 < lineStarts.GetSize()) {
            int colChars = WDL_utf8_bytepos_to_charpos(
                t + lineStarts.Get()[curLine], cursBpos - lineStarts.Get()[curLine]);
            int nextByteLen = lineEnds.Get()[curLine+1] - lineStarts.Get()[curLine+1];
            int nextChars = WDL_utf8_bytepos_to_charpos(
                t + lineStarts.Get()[curLine+1], nextByteLen);
            if (colChars > nextChars) colChars = nextChars;
            int newBpos = lineStarts.Get()[curLine+1] +
                          WDL_utf8_charpos_to_bytepos(t + lineStarts.Get()[curLine+1], colChars);
            st->cursor_pos = WDL_utf8_bytepos_to_charpos(t, newBpos);
            if (!shift) { st->sel1 = st->sel2 = -1; }
            InvalidateRect(hwnd, NULL, FALSE);
          }
          return 1;
        }
        if (wParam == VK_PRIOR || wParam == VK_NEXT) {
          RECT cr; GetClientRect(hwnd, &cr);
          int viewH = (cr.bottom - cr.top) - g_swell_theme.padding_edit_v * 2;
          int rh = st->max_height > 0 ? st->max_height : 16;
          int pagesz = viewH / rh;
          if (pagesz < 1) pagesz = 1;
          int targetLine = curLine + (wParam == VK_PRIOR ? -pagesz : pagesz);
          if (targetLine < 0) targetLine = 0;
          if (targetLine >= lineStarts.GetSize()) targetLine = lineStarts.GetSize() - 1;
          int col = st->cursor_pos - lineStarts.Get()[curLine];
          int targLen = lineEnds.Get()[targetLine] - lineStarts.Get()[targetLine];
          if (col > targLen) col = targLen;
          st->cursor_pos = lineStarts.Get()[targetLine] + col;
          if (!shift) { st->sel1 = st->sel2 = -1; }
          InvalidateRect(hwnd, NULL, FALSE);
          return 1;
        }
      }
      // Ctrl+A: select all
      if (ctrl && (wParam == 'A' || wParam == 'a')) {
        SendMessage(hwnd, EM_SETSEL, 0, -1);
        return 1;
      }
      return 0;
    }

    case WM_LBUTTONDOWN: {
      SetFocus(hwnd);
      if (st) {
        int mx = (short)LOWORD(lParam);
        int my = (short)HIWORD(lParam);

        // Check vertical scrollbar first
        RECT cr; GetClientRect(hwnd, &cr);
        bool multiline = (hwnd->m_style & ES_MULTILINE) != 0;
        if (multiline && st->ml_dline_starts.GetSize() > 0) {
          int rowH = st->max_height > 0 ? st->max_height : 16;
          int totalH = st->ml_dline_starts.GetSize() * rowH;
          int viewH = cr.bottom - cr.top;
          if (totalH > viewH) {
            const swell_theme &th = g_swell_theme;
            int hit = hitVScrollbar(cr, viewH, totalH, st->scroll_y, my);
            if (hit && mx >= cr.right - th.scrollbar_width) {
              if (hit == 2) {
                st->m_sb_dragging = 1;
                st->m_sb_drag_mouse = my;
                st->m_sb_drag_scroll = st->scroll_y;
                SetCapture(hwnd);
              } else if (hit == 1) {
                st->scroll_y -= viewH;
                if (st->scroll_y < 0) st->scroll_y = 0;
                InvalidateRect(hwnd, NULL, FALSE);
              } else {
                st->scroll_y += viewH;
                int vmax = totalH - viewH;
                if (st->scroll_y > vmax) st->scroll_y = vmax;
                InvalidateRect(hwnd, NULL, FALSE);
      }
      if (st && !st->m_sb_dragging && !st->m_mouse_sel_active) {
        int mx = (short)LOWORD(lParam);
        int my = (short)HIWORD(lParam);
        bool multiline = (hwnd->m_style & ES_MULTILINE) != 0;
        int new_hover = 0;
        if (multiline && st->ml_dline_starts.GetSize() > 0) {
          int rowH = st->max_height > 0 ? st->max_height : 16;
          int totalH = st->ml_dline_starts.GetSize() * rowH;
          RECT cr; GetClientRect(hwnd, &cr);
          int viewH = cr.bottom - cr.top;
          if (totalH > viewH) {
            const swell_theme &th = g_swell_theme;
            if (mx >= cr.right - th.scrollbar_width) {
              int hit = hitVScrollbar(cr, viewH, totalH, st->scroll_y, my);
              if (hit == 2) new_hover = 1;
            }
          }
        }
        if (new_hover != st->m_sb_hover) {
          st->m_sb_hover = new_hover;
          InvalidateRect(hwnd, NULL, FALSE);
        }
      }
      return 0;
            }
          }
        }

        int pos = edit_pos_from_xy(hwnd, mx, my, st);
        st->cursor_pos = pos;
        st->m_mouse_sel_active = true;
        st->m_mouse_sel_anchor = pos;
        st->sel1 = st->sel2 = -1;
        InvalidateRect(hwnd, NULL, FALSE);
      }
      SetCapture(hwnd);
      return 0;
    }

    case WM_MOUSEMOVE:
      if (st && st->m_sb_dragging) {
        int my = (short)HIWORD(lParam);
        int dy = my - st->m_sb_drag_mouse;
        if (dy != 0) {
          int rowH = st->max_height > 0 ? st->max_height : 16;
          int totalH = st->ml_dline_starts.GetSize() * rowH;
          RECT cr; GetClientRect(hwnd, &cr);
          int viewH = cr.bottom - cr.top;
          if (totalH > viewH) {
            int ttop, tbot;
            getVThumb(viewH, totalH, st->m_sb_drag_scroll, &ttop, &tbot);
            int new_thumb = ttop + dy;
            st->scroll_y = scrollFromThumbPos(new_thumb, viewH, totalH);
            int vmax = totalH - viewH;
            if (st->scroll_y > vmax) st->scroll_y = vmax;
            if (st->scroll_y < 0) st->scroll_y = 0;
            InvalidateRect(hwnd, NULL, FALSE);
          }
        }
        return 0;
      }
      if (st && st->m_mouse_sel_active) {
        int mx = (short)LOWORD(lParam);
        int my = (short)HIWORD(lParam);
        int pos = edit_pos_from_xy(hwnd, mx, my, st);
        st->cursor_pos = pos;
        st->sel1 = st->m_mouse_sel_anchor;
        st->sel2 = pos;
        InvalidateRect(hwnd, NULL, FALSE);
      }
      return 0;

    case WM_LBUTTONUP:
      if (st) {
        if (st->m_sb_dragging) {
          st->m_sb_dragging = 0;
          ReleaseCapture();
          return 0;
        }
        st->m_mouse_sel_active = false;
        ReleaseCapture();
      }
      return 0;

    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      if (!hdc) return 0;

      const swell_theme &th = g_swell_theme;
      RECT cr; GetClientRect(hwnd, &cr);
      bool enabled = IsWindowEnabled(hwnd);
      bool focused = (GetFocus() == hwnd);
      bool multiline = (hwnd->m_style & ES_MULTILINE) != 0;
      bool is_pass = (hwnd->m_style & ES_PASSWORD) != 0;

      // Rounded input field
      COLORREF fillcol = enabled ? (COLORREF)th.bg_input
                                 : (COLORREF)th.bg_input_alt;
      COLORREF bordercol = focused ? (COLORREF)th.accent
                                   : (COLORREF)th.border_strong;
      int bw = focused ? (th.focus_ring_width > 0 ? th.focus_ring_width
                                                  : th.border_width)
                       : th.border_width;
      HPEN pen  = CreatePen(PS_SOLID, bw, bordercol);
      HBRUSH br = CreateSolidBrush(fillcol);
      HGDIOBJ op = SelectObject(hdc, pen);
      HGDIOBJ ob = SelectObject(hdc, br);
      const int r = th.corner_radius;
      RoundRect(hdc, cr.left, cr.top, cr.right, cr.bottom, r*2, r*2);
      SelectObject(hdc, op); DeleteObject(pen);
      SelectObject(hdc, ob); DeleteObject(br);

      SWELL_PushClipRegion(hdc);
      SWELL_SetClipRoundRect(hdc, cr.left, cr.top, cr.right, cr.bottom, r);

      HFONT f = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
      SelectObject(hdc, f);

      TEXTMETRIC tm; GetTextMetrics(hdc, &tm);
      int rowH = tm.tmHeight + 2;
      if (st) st->max_height = rowH;
      SkFont skfont = swell_make_skfont_from_hdc(hdc);

      const char *txt = hwnd->m_title.Get();
      int tlen = (int)strlen(txt);
      WDL_FastString disp;
      if (is_pass) {
        for (int i = 0; i < tlen; i++) disp.Append("*", 1);
        txt = disp.Get();
        tlen = (int)strlen(txt);
      }

      RECT tr = { cr.left + th.padding_edit_h, cr.top + th.padding_edit_v,
                  cr.right - th.padding_edit_h, cr.bottom - th.padding_edit_v };

      if (multiline)
      {
        int scrWidth = tr.right - tr.left;
        if (scrWidth < 1) scrWidth = 1;

        // Recompute cached display lines if text or width changed
        if (!st || st->ml_cached_text.GetLength() != tlen ||
            strcmp(st->ml_cached_text.Get(), txt) != 0 ||
            st->ml_cached_w != scrWidth)
        {
          st->ml_cached_text.Set(txt);
          st->ml_cached_w = scrWidth;
          st->ml_dline_starts.Resize(0, false);
          st->ml_dline_ends.Resize(0, false);

          // Build line-offset arrays from newlines
          WDL_TypedBuf<int> lineStarts, lineEnds;
          lineStarts.Add(0);
          for (int i = 0; i < tlen; i++) {
            if (txt[i] == '\n') { lineEnds.Add(i); lineStarts.Add(i+1); }
          }
          lineEnds.Add(tlen);

           // Word-wrap: split logical lines into display lines if too wide
           for (int li = 0; li < lineStarts.GetSize(); li++) {
             int start_off = lineStarts.Get()[li];
             int end_off = lineEnds.Get()[li];
             // Empty logical line (blank line from consecutive \n)
             if (start_off == end_off) {
               st->ml_dline_starts.Add(start_off);
               st->ml_dline_ends.Add(start_off);
               continue;
             }
             int pos = start_off;
            while (pos < end_off) {
              int remain = end_off - pos;
              SkRect bounds;
              skfont.measureText(txt + pos, remain, SkTextEncoding::kUTF8, &bounds);
              int textW = (int)(bounds.width() + 0.5f);
              if (textW <= scrWidth) {
               st->ml_dline_starts.Add(pos);
               st->ml_dline_ends.Add(end_off);
               break;
              }
              // binary search for wrap point
              int lo = pos + 1, hi = end_off;
              while (lo < hi) {
                int mid = (lo + hi + 1) / 2;
                skfont.measureText(txt + pos, mid - pos, SkTextEncoding::kUTF8, &bounds);
                textW = (int)(bounds.width() + 0.5f);
                if (textW <= scrWidth) lo = mid;
                else hi = mid - 1;
              }
              int wrap = lo;
              if (wrap <= pos) wrap = pos + 1;
              st->ml_dline_starts.Add(pos);
              st->ml_dline_ends.Add(wrap);
              pos = wrap;
              if (pos < end_off && txt[pos] == ' ') pos++;
            }
          }

          // Rebuild char-to-display-line map.
          // Spaces at wrap points get skipped by pos++, creating gaps
          // where ml_dline_ends[di] < ml_dline_starts[di+1].
          // Map gap chars to the display line that ends before them.
          int nd = st->ml_dline_starts.GetSize();
          st->ml_char2dline.Resize(tlen + 1, false);
          if (nd > 0) {
            int di = 0;
            for (int ci = 0; ci <= tlen; ci++) {
              while (di + 1 < nd && ci >= st->ml_dline_starts.Get()[di + 1]) di++;
              st->ml_char2dline.Get()[ci] = di;
            }
          }
        }

        int ndlines = st ? st->ml_dline_starts.GetSize() : 0;
        if (ndlines == 0) ndlines = 1; // at least one empty line

        // Scroll
        int totalH = ndlines * rowH;
        int viewH = tr.bottom - tr.top;
        if (totalH > viewH)
          tr.right -= th.scrollbar_width;
        if (st) {
          if (st->scroll_y > totalH - viewH) st->scroll_y = totalH - viewH;
          if (st->scroll_y < 0) st->scroll_y = 0;
        }
        int scrollY = st ? st->scroll_y : 0;

        // Use cached char-to-display-line map
        const int *char2dline = (st && st->ml_char2dline.GetSize() > 0)
          ? st->ml_char2dline.Get() : nullptr;

        int sel1 = -1, sel2 = -1;
        if (st && st->sel1 >= 0 && st->sel1 != st->sel2) {
          sel1 = st->sel1 < st->sel2 ? st->sel1 : st->sel2;
          sel2 = st->sel1 < st->sel2 ? st->sel2 : st->sel1;
          if (sel1 > tlen) sel1 = tlen;
          if (sel2 > tlen) sel2 = tlen;
        }

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, enabled ? (COLORREF)th.fg_text : (COLORREF)th.fg_text_disabled);
        for (int di = 0; di < ndlines; di++) {
          int d0 = st->ml_dline_starts.Get()[di];
          int d1 = st->ml_dline_ends.Get()[di];
          int ry = tr.top + di * rowH - scrollY;
          if (ry + rowH < tr.top) continue;
          if (ry >= tr.bottom) break;
          if (d0 < d1) {
            RECT lr = { tr.left, ry, tr.right, ry + rowH };
            SWELL_DrawText(hdc, txt + d0, d1 - d0, &lr, DT_LEFT | DT_TOP | DT_NOPREFIX);
          }
        }

        // Selection highlight
        if (char2dline && sel2 > sel1 && sel1 >= 0 && ndlines > 0) {
          int bs1 = WDL_utf8_charpos_to_bytepos(txt, sel1);
          int bs2 = WDL_utf8_charpos_to_bytepos(txt, sel2);
          int dl0 = char2dline[bs1];
          int dl1 = char2dline[bs2];
          for (int di = dl0; di <= dl1 && di < ndlines; di++) {
            int d0 = st->ml_dline_starts.Get()[di];
            int d1 = st->ml_dline_ends.Get()[di];
            int selLineStart = (di == dl0) ? bs1 : d0;
            int selLineEnd = (di == dl1) ? bs2 : d1;
            if (selLineStart >= selLineEnd) continue;
            if (selLineStart < d0) selLineStart = d0;
            int ry = tr.top + di * rowH - scrollY;

            SkRect r1;
            skfont.measureText(txt + d0, selLineStart - d0, SkTextEncoding::kUTF8, &r1);
            int preW = (int)(r1.width() + 0.5f);

            SkRect r2;
            skfont.measureText(txt + selLineStart, selLineEnd - selLineStart, SkTextEncoding::kUTF8, &r2);
            int selW = (int)(r2.width() + 0.5f);

            RECT selR = { tr.left + preW, ry, tr.left + preW + selW, ry + rowH };
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, (COLORREF)th.accent);
            SetTextColor(hdc, (COLORREF)th.fg_on_accent);
            SWELL_DrawText(hdc, txt + selLineStart, selLineEnd - selLineStart, &selR, DT_LEFT | DT_TOP | DT_NOPREFIX);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, enabled ? (COLORREF)th.fg_text : (COLORREF)th.fg_text_disabled);
          }
        }

        // Caret
        if (st && st->cursor_state && focused && ndlines > 0) {
          int cpos = st->cursor_pos;
          if (cpos > tlen) cpos = tlen;
          int bpos = WDL_utf8_charpos_to_bytepos(txt, cpos);
          int cdline = char2dline ? char2dline[bpos] : 0;
          if (cdline >= 0 && cdline < ndlines) {
            int d0 = st->ml_dline_starts.Get()[cdline];
            int clen = bpos - d0;
            if (clen < 0) clen = 0;
            SkRect cbr;
            skfont.measureText(txt + d0, clen, SkTextEncoding::kUTF8, &cbr);
            int cx = tr.left + (int)(cbr.width() + 0.5f);
            int cy = tr.top + cdline * rowH - scrollY;
            if (cy >= tr.top && cy + rowH <= tr.bottom) {
              HPEN cp = CreatePen(PS_SOLID, th.border_width, (COLORREF)th.caret);
              HGDIOBJ ocp = SelectObject(hdc, cp);
              MoveToEx(hdc, cx, cy, NULL);
              LineTo(hdc, cx, cy + rowH);
            SelectObject(hdc, ocp); DeleteObject(cp);
              }
            }
          }

        drawVerticalScrollbar(hdc, cr, cr.bottom - cr.top, totalH, scrollY, st && st->m_sb_hover);
        }
      else
      {
        // Single-line path (unchanged logic)
        if (!st || st->sel1 < 0 || st->sel1 == st->sel2)
        {
          SetTextColor(hdc, enabled ? (COLORREF)th.fg_text
                                    : (COLORREF)th.fg_text_disabled);
          SetBkMode(hdc, TRANSPARENT);
          SWELL_DrawText(hdc, txt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        else
        {
          int s1 = st->sel1 < st->sel2 ? st->sel1 : st->sel2;
          int s2 = st->sel1 < st->sel2 ? st->sel2 : st->sel1;
          if (s1 > tlen) s1 = tlen;
          if (s2 > tlen) s2 = tlen;
          int bs1 = WDL_utf8_charpos_to_bytepos(txt, s1);
          int bs2 = WDL_utf8_charpos_to_bytepos(txt, s2);

          SetTextColor(hdc, enabled ? (COLORREF)th.fg_text
                                    : (COLORREF)th.fg_text_disabled);
          SetBkMode(hdc, TRANSPARENT);
          SWELL_DrawText(hdc, txt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

          if (bs2 > bs1)
          {
            SkRect mr;
            skfont.measureText(txt, bs1, SkTextEncoding::kUTF8, &mr);
            RECT selR = tr;
            selR.left += (int)(mr.width() + 0.5f);
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, (COLORREF)th.accent);
            SetTextColor(hdc, (COLORREF)th.fg_on_accent);
            SWELL_DrawText(hdc, txt + bs1, bs2 - bs1, &selR, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
          }
        }

        // Caret
        if (st && st->cursor_state && focused) {
          int cpos = st->cursor_pos;
          if (cpos > tlen) cpos = tlen;
          int bpos = WDL_utf8_charpos_to_bytepos(txt, cpos);
          SkRect cbounds;
          skfont.measureText(txt, bpos, SkTextEncoding::kUTF8, &cbounds);
          int cx = tr.left + (int)(cbounds.width() + 0.5f);
          if (cx > tr.right - 1) cx = tr.right - 1;
          HPEN cp = CreatePen(PS_SOLID, th.border_width, (COLORREF)th.caret);
          HGDIOBJ ocp = SelectObject(hdc, cp);
          MoveToEx(hdc, cx, tr.top, NULL);
          LineTo(hdc, cx, tr.bottom);
          SelectObject(hdc, ocp); DeleteObject(cp);
        }
      }

      SWELL_PopClipRegion(hdc);
      EndPaint(hwnd, &ps);
      return 0;
    }

    default:
      return DefWindowProc(hwnd, msg, wParam, lParam);
  }
}

// ===========================================================================
// 3. labelWindowProc  (Static)
// ===========================================================================

LRESULT labelWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg) {
    case WM_CREATE:
      hwnd->m_wantfocus = false;
      return 0;

    case WM_SETTEXT:
      hwnd->m_title.Set((const char *)lParam);
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;

    case WM_GETTEXT:
      if (lParam && wParam > 0) {
        lstrcpyn((char *)lParam, hwnd->m_title.Get(), (int)wParam);
        return (LRESULT)strlen((char *)lParam);
      }
      return 0;

    case WM_LBUTTONDOWN:
      if (hwnd->m_style & SS_NOTIFY)
        notify_parent(hwnd, STN_CLICKED);
      return 0;

    case WM_LBUTTONDBLCLK:
      if (hwnd->m_style & SS_NOTIFY)
        notify_parent(hwnd, STN_DBLCLK);
      return 0;

    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      if (!hdc) return 0;

      RECT cr; GetClientRect(hwnd, &cr);

      COLORREF fgcol = hwnd->m_enabled
        ? (COLORREF)g_swell_theme.fg_text
        : (COLORREF)g_swell_theme.fg_text_disabled;
      SetTextColor(hdc, fgcol);

      HBRUSH br = get_window_brush(hwnd, hdc, WM_CTLCOLORSTATIC);
      if (br && (INT_PTR)br != 1) {
        FillRect(hdc, &cr, br);
      } else if (!br) {
        SWELL_FillDialogBackground(hdc, &cr, 0);
      }
      // else: parent returned 1 (already painted bg), skip

      SetBkMode(hdc, TRANSPARENT);

      DWORD style = hwnd->m_style;
      bool nowrap = (style & SS_LEFTNOWORDWRAP) != 0;

      DWORD align_flag;
      if (nowrap) {
        // Single-line: current behaviour — vcenter, no word-break
        if (style & SS_RIGHT)
          align_flag = DT_RIGHT | DT_VCENTER | DT_SINGLELINE;
        else if (style & SS_CENTER)
          align_flag = DT_CENTER | DT_VCENTER | DT_SINGLELINE;
        else
          align_flag = DT_LEFT | DT_VCENTER | DT_SINGLELINE;
      } else {
        // Multiline: word-break, top-aligned text block
        if (style & SS_RIGHT)
          align_flag = DT_RIGHT | DT_TOP | DT_WORDBREAK;
        else if (style & SS_CENTER)
          align_flag = DT_CENTER | DT_TOP | DT_WORDBREAK;
        else
          align_flag = DT_LEFT | DT_TOP | DT_WORDBREAK;
      }

      HFONT f = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
      SelectObject(hdc, f);

      const char *txt = hwnd->m_title.Get();
      if (txt && txt[0]) {
        SWELL_DrawText(hdc, txt, -1, &cr, align_flag);
      }

      EndPaint(hwnd, &ps);
      return 0;
    }

    default:
      return DefWindowProc(hwnd, msg, wParam, lParam);
  }
}

// ===========================================================================
// 4. listViewWindowProc  (ListView and ListBox)
// ===========================================================================

LRESULT listViewWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  listViewState *st = (listViewState *)(void *)hwnd->m_private_data;

  switch (msg) {
    case WM_CREATE: {
      st = new listViewState();
      hwnd->m_private_data = (INT_PTR)st;
      st->m_is_listbox = (lParam != 0);
      if (hwnd->m_style & LVS_OWNERDATA) st->m_owner_data_size = 0;
      hwnd->m_wantfocus = true;
      return 0;
    }

    case WM_NCDESTROY:
      delete st;
      hwnd->m_private_data = 0;
      return 0;

    case WM_ERASEBKGND:
      return 1;

    // ---- ListView messages ----

    case LVM_SETEXTENDEDLISTVIEWSTYLE:
      if (st) {
        int mask = (int)wParam;
        if (!mask) mask = 0xFFFFFF;
        st->m_extended_style = (st->m_extended_style & ~mask) | ((int)lParam & mask);
      }
      return 0;

    case LVM_GETEXTENDEDLISTVIEWSTYLE:
      return st ? st->m_extended_style : 0;

    case LVM_INSERTCOLUMN: {
      if (!st || !lParam) return -1;
      int pos = (int)wParam;
      const LVCOLUMN *lvc = (const LVCOLUMN *)lParam;
      SWELL_ListView_Col col;
      if (lvc->mask & LVCF_TEXT && lvc->pszText)
        col.name = strdup(lvc->pszText);
      if (lvc->mask & LVCF_WIDTH) col.xwid = lvc->cx;
      if (lvc->mask & LVCF_FMT)  col.fmt  = lvc->fmt;
      int n = st->m_cols.GetSize();
      if (pos < 0 || pos > n) pos = n;
      // shift existing col_index values >= pos
      for (int x = 0; x < n; x++)
        if (st->m_cols.Get()[x].col_index >= pos)
          st->m_cols.Get()[x].col_index++;
      col.col_index = pos;
      st->m_cols.Resize(n+1, false);
      for (int i = n; i > pos; i--) st->m_cols.Get()[i] = st->m_cols.Get()[i-1];
      st->m_cols.Get()[pos] = col;
      col.name = NULL; // ownership transferred
      InvalidateRect(hwnd, NULL, FALSE);
      return pos;
    }

    case LVM_DELETECOLUMN: {
      if (!st) return FALSE;
      int pos = (int)wParam;
      if (pos < 0 || pos >= st->m_cols.GetSize()) return FALSE;
      int oldidx = st->m_cols.Get()[pos].col_index;
      free(st->m_cols.Get()[pos].name);
      int n = st->m_cols.GetSize();
      for (int i = pos; i < n-1; i++) st->m_cols.Get()[i] = st->m_cols.Get()[i+1];
      st->m_cols.Resize(n-1, false);
      // decrement col_index for entries > deleted logical index
      for (int i = 0; i < st->m_cols.GetSize(); i++)
        if (st->m_cols.Get()[i].col_index > oldidx)
          st->m_cols.Get()[i].col_index--;
      InvalidateRect(hwnd, NULL, FALSE);
      return TRUE;
    }

    case LVM_SETCOLUMN: {
      if (!st || !lParam) return FALSE;
      int idx = (int)wParam; // logical column index
      const LVCOLUMN *lvc = (const LVCOLUMN *)lParam;
      for (int i = 0; i < st->m_cols.GetSize(); i++) {
        if (st->m_cols.Get()[i].col_index == idx) {
          SWELL_ListView_Col &c = st->m_cols.Get()[i];
          if (lvc->mask & LVCF_TEXT && lvc->pszText) { free(c.name); c.name = strdup(lvc->pszText); }
          if (lvc->mask & LVCF_WIDTH) c.xwid = lvc->cx;
          if (lvc->mask & LVCF_FMT)  c.fmt  = lvc->fmt;
          InvalidateRect(hwnd, NULL, FALSE);
          return TRUE;
        }
      }
      return FALSE;
    }

    case LVM_GETCOLUMN: {
      if (!st || !lParam) return FALSE;
      int idx = (int)wParam; // logical column index
      LVCOLUMN *lvc = (LVCOLUMN *)lParam;
      // find column by logical col_index
      const SWELL_ListView_Col *c = NULL;
      for (int i = 0; i < st->m_cols.GetSize(); i++) {
        if (st->m_cols.Get()[i].col_index == idx) { c = st->m_cols.Get() + i; break; }
      }
      if (!c) return FALSE;
      if (lvc->mask & LVCF_TEXT && lvc->pszText && lvc->cchTextMax > 0)
        lstrcpyn(lvc->pszText, c->name ? c->name : "", lvc->cchTextMax);
      if (lvc->mask & LVCF_WIDTH) lvc->cx  = c->xwid;
      if (lvc->mask & LVCF_FMT)  lvc->fmt = c->fmt;
      return TRUE;
    }

    case LVM_GETCOLUMNWIDTH:
      if (!st) return 0;
      { int idx = (int)wParam; // logical column index
        for (int i = 0; i < st->m_cols.GetSize(); i++)
          if (st->m_cols.Get()[i].col_index == idx)
            return st->m_cols.Get()[i].xwid;
        return 0;
      }

    case LVM_SETCOLUMNWIDTH:
      if (st) {
        int idx = (int)wParam; // logical column index
        for (int i = 0; i < st->m_cols.GetSize(); i++) {
          if (st->m_cols.Get()[i].col_index == idx) {
            st->m_cols.Get()[i].xwid = (int)lParam;
            InvalidateRect(hwnd, NULL, FALSE);
            break;
          }
        }
      }
      return TRUE;

    case LVM_INSERTITEM: {
      if (!st || !lParam) return -1;
      const LVITEM *item = (const LVITEM *)lParam;
      int pos = item->iItem;
      int n = st->m_data.GetSize();
      if (pos < 0 || pos > n) pos = n;
      SWELL_ListView_Row *row = new SWELL_ListView_Row();
      row->m_param = (item->mask & LVIF_PARAM) ? item->lParam : 0;
      if (item->mask & LVIF_TEXT && item->pszText) {
        row->m_cols.Resize(1, false);
        row->m_cols.Get()[0].txt = strdup(item->pszText);
      }
      st->m_data.Insert(pos, row);
      InvalidateRect(hwnd, NULL, FALSE);
      return pos;
    }

    case LVM_SETITEM: {
      if (!st || !lParam) return FALSE;
      const LVITEM *item = (const LVITEM *)lParam;
      int row = item->iItem;
      if (row < 0 || row >= st->m_data.GetSize()) return FALSE;
      SWELL_ListView_Row *r = st->m_data.Get(row);
      if (item->mask & LVIF_PARAM) r->m_param = item->lParam;
      if (item->mask & LVIF_STATE) {
        if (item->stateMask & LVIS_SELECTED) {
          if (item->state & LVIS_SELECTED) r->m_tmp |= 1;
          else r->m_tmp &= ~1;
        }
      }
      if (item->mask & LVIF_TEXT && item->pszText) {
        int col = item->iSubItem;
        if (col >= r->m_cols.GetSize()) {
          int old = r->m_cols.GetSize();
          r->m_cols.Resize(col+1, false);
          for (int i = old; i <= col; i++) r->m_cols.Get()[i].txt = NULL;
        }
        free(r->m_cols.Get()[col].txt);
        r->m_cols.Get()[col].txt = strdup(item->pszText);
      }
      InvalidateRect(hwnd, NULL, FALSE);
      return TRUE;
    }

    case LVM_GETITEM: {
      if (!st || !lParam) return FALSE;
      LVITEM *item = (LVITEM *)lParam;
      int row = item->iItem;
      if (row < 0 || row >= st->m_data.GetSize()) return FALSE;
      SWELL_ListView_Row *r = st->m_data.Get(row);
      if (item->mask & LVIF_PARAM) item->lParam = r->m_param;
      if (item->mask & LVIF_STATE) {
        item->state = 0;
        if (r->m_tmp & 1) item->state |= LVIS_SELECTED;
        if (st->m_selitem == row) item->state |= LVIS_FOCUSED;
      }
      if (item->mask & LVIF_TEXT && item->pszText && item->cchTextMax > 0) {
        int col = item->iSubItem;
        const char *txt = "";
        if (col < r->m_cols.GetSize() && r->m_cols.Get()[col].txt)
          txt = r->m_cols.Get()[col].txt;
        lstrcpyn(item->pszText, txt, item->cchTextMax);
      }
      return TRUE;
    }

    case LVM_DELETEITEM: {
      if (!st) return FALSE;
      int pos = (int)wParam;
      if (pos < 0 || pos >= st->m_data.GetSize()) return FALSE;
      st->m_data.Delete(pos, true);
      if (st->m_selitem >= st->m_data.GetSize()) st->m_selitem = st->m_data.GetSize()-1;
      InvalidateRect(hwnd, NULL, FALSE);
      return TRUE;
    }

    case LVM_DELETEALLITEMS:
      if (st) { st->m_data.Empty(true); st->m_selitem = -1; InvalidateRect(hwnd, NULL, FALSE); }
      return TRUE;

    case LVM_GETITEMCOUNT:
      return st ? (st->m_owner_data_size >= 0 ? st->m_owner_data_size : st->m_data.GetSize()) : 0;

    case LVM_SETITEMCOUNT:
      if (st) {
        st->m_owner_data_size = (int)wParam;
        InvalidateRect(hwnd, NULL, FALSE);
      }
      return 0;

    case LVM_GETSELECTIONMARK:
      return st ? st->m_selitem : -1;

    case LVM_GETSELECTEDCOUNT: {
      if (!st) return 0;
      if (!st->m_is_multisel) return (st->m_selitem >= 0) ? 1 : 0;
      int cnt = 0;
      for (int i = 0; i < st->m_data.GetSize(); i++)
        if (st->m_data.Get(i)->m_tmp & 1) cnt++;
      return cnt;
    }

    case LVM_GETNEXTITEM: {
      if (!st) return -1;
      int start = (int)wParam;
      UINT flags = (UINT)lParam;
      int n = st->m_data.GetSize();
      if (flags & LVNI_SELECTED) {
        for (int i = start+1; i < n; i++) {
          if (st->m_selitem == i || (st->m_is_multisel && (st->m_data.Get(i)->m_tmp & 1)))
            return i;
        }
        return -1;
      }
      return (start+1 < n) ? start+1 : -1;
    }

    case LVM_GETITEMSTATE: {
      if (!st) return 0;
      int row = (int)wParam;
      UINT mask = (UINT)lParam;
      if (row < 0 || row >= st->m_data.GetSize()) return 0;
      int state = 0;
      if (mask & LVIS_SELECTED) {
        if (st->m_selitem == row || (st->m_is_multisel && (st->m_data.Get(row)->m_tmp & 1)))
          state |= LVIS_SELECTED;
      }
      if (mask & LVIS_FOCUSED && st->m_selitem == row) state |= LVIS_FOCUSED;
      return state;
    }

    case LVM_SETITEMSTATE: {
      if (!st || !lParam) return FALSE;
      const LVITEM *item = (const LVITEM *)lParam;
      int row = (int)wParam;
      if (row == -1) {
        // set all
        for (int i = 0; i < st->m_data.GetSize(); i++) {
          if (item->stateMask & LVIS_SELECTED) {
            if (item->state & LVIS_SELECTED) st->m_data.Get(i)->m_tmp |= 1;
            else st->m_data.Get(i)->m_tmp &= ~1;
          }
        }
        if (!(item->state & LVIS_SELECTED) && (item->stateMask & LVIS_SELECTED))
          st->m_selitem = -1;
      } else {
        if (row < 0 || row >= st->m_data.GetSize()) return FALSE;
        if (item->stateMask & LVIS_SELECTED) {
          if (item->state & LVIS_SELECTED) {
            st->m_data.Get(row)->m_tmp |= 1;
            st->m_selitem = row;
          } else {
            st->m_data.Get(row)->m_tmp &= ~1;
          }
        }
        if (item->stateMask & LVIS_FOCUSED && item->state & LVIS_FOCUSED)
          st->m_selitem = row;
      }
      InvalidateRect(hwnd, NULL, FALSE);
      return TRUE;
    }

    case LVM_SETITEMTEXT: {
      if (!st) return FALSE;
      int row = (int)wParam;
      if (row < 0 || row >= st->m_data.GetSize()) return FALSE;
      LVITEM *item = (LVITEM *)lParam;
      if (!item) return FALSE;
      SWELL_ListView_Row *r = st->m_data.Get(row);
      int col = item->iSubItem;
      if (col >= r->m_cols.GetSize()) {
        int old = r->m_cols.GetSize();
        r->m_cols.Resize(col+1, false);
        for (int i = old; i <= col; i++) r->m_cols.Get()[i].txt = NULL;
      }
      free(r->m_cols.Get()[col].txt);
      r->m_cols.Get()[col].txt = item->pszText ? strdup(item->pszText) : NULL;
      InvalidateRect(hwnd, NULL, FALSE);
      return TRUE;
    }

    case LVM_GETITEMTEXT: {
      if (!st || !lParam) return 0;
      int row = (int)wParam;
      LVITEM *item = (LVITEM *)lParam;
      if (row < 0 || row >= st->m_data.GetSize()) return 0;
      SWELL_ListView_Row *r = st->m_data.Get(row);
      int col = item->iSubItem;
      const char *txt = "";
      if (col < r->m_cols.GetSize() && r->m_cols.Get()[col].txt)
        txt = r->m_cols.Get()[col].txt;
      lstrcpyn(item->pszText, txt, item->cchTextMax);
      return (LRESULT)strlen(item->pszText);
    }

    case LVM_SORTITEMS: {
      if (!st) return FALSE;
      PFNLVCOMPARE fn = (PFNLVCOMPARE)lParam;
      if (!fn) return FALSE;
      // simple insertion sort
      int n = st->m_data.GetSize();
      for (int i = 1; i < n; i++) {
        for (int j = i; j > 0; j--) {
          int r = fn(st->m_data.Get(j-1)->m_param,
                     st->m_data.Get(j)->m_param,
                     (LPARAM)wParam);
          if (r <= 0) break;
          SWELL_ListView_Row *tmp = st->m_data.Get(j-1);
          st->m_data.Set(j-1, st->m_data.Get(j));
          st->m_data.Set(j, tmp);
        }
      }
      InvalidateRect(hwnd, NULL, FALSE);
      return TRUE;
    }

    case LVM_REDRAWITEMS:
      InvalidateRect(hwnd, NULL, FALSE);
      return TRUE;

    case LVM_ENSUREVISIBLE:
      // TODO: scroll to make item visible
      return TRUE;

    case LVM_SETIMAGELIST:
      if (st) {
        st->m_status_imagelist = (HIMAGELIST)lParam;
        st->m_status_imagelist_type = (int)wParam;
      }
      return 0;

    case LVM_SCROLL:
      if (st) {
        st->m_scroll_x += (int)wParam;
        st->m_scroll_y += (int)lParam;
        if (st->m_scroll_x < 0) st->m_scroll_x = 0;
        if (st->m_scroll_y < 0) st->m_scroll_y = 0;
        InvalidateRect(hwnd, NULL, FALSE);
      }
      return TRUE;

    case LVM_GETTOPINDEX:
      if (!st) return 0;
      return st->m_scroll_y / (st->m_last_row_height > 0 ? st->m_last_row_height : 16);

    case LVM_GETCOUNTPERPAGE: {
      if (!st) return 0;
      RECT cr; GetClientRect(hwnd, &cr);
      int rh = st->m_last_row_height > 0 ? st->m_last_row_height : 16;
      return (cr.bottom - cr.top) / rh;
    }

    case LVM_GETITEMRECT: {
      if (!st || !lParam) return FALSE;
      RECT *r = (RECT *)lParam;
      int row = (int)wParam;
      if (row < 0) return FALSE;
      RECT cr; GetClientRect(hwnd, &cr);
      int rh = st->m_last_row_height > 0 ? st->m_last_row_height : 16;
      int hdr = (st->m_cols.GetSize() > 0 && !(hwnd->m_style & LVS_NOCOLUMNHEADER)) ? (rh + 2) : 0;
      r->left   = cr.left;
      r->right  = cr.right;
      r->top    = hdr + row * rh - st->m_scroll_y;
      r->bottom = r->top + rh;
      return TRUE;
    }

    case LVM_GETSUBITEMRECT: {
      if (!st || !lParam) return FALSE;
      RECT *r = (RECT *)lParam;
      int row = (int)wParam;
      int code = r->left;    // pre-stored by ListView_GetSubItemRect stub
      int subitem = r->top;  // pre-stored by stub
      if (row < 0) return FALSE;
      int rh = st->m_last_row_height > 0 ? st->m_last_row_height : 16;
      int hdr = (st->m_cols.GetSize() > 0 && !(hwnd->m_style & LVS_NOCOLUMNHEADER)) ? (rh + 2) : 0;
      r->top = hdr + row * rh - st->m_scroll_y;
      r->bottom = r->top + rh;
      r->left = 0;
      int xpos = -st->m_scroll_x;
      if (subitem >= 0) {
        for (int x = 0; x < st->m_cols.GetSize(); x++) {
          int xwid = st->m_cols.Get()[x].xwid;
          if (st->m_cols.Get()[x].col_index == subitem) {
            r->left = xpos;
            r->right = xpos + xwid;
            break;
          }
          xpos += xwid;
        }
      }
      return TRUE;
    }

    case LVM_HITTEST: {
      if (!st || !lParam) return -1;
      LVHITTESTINFO *hti = (LVHITTESTINFO *)lParam;
      RECT cr; GetClientRect(hwnd, &cr);
      int rh = st->m_last_row_height > 0 ? st->m_last_row_height : 16;
      int hdr = (st->m_cols.GetSize() > 0 && !(hwnd->m_style & LVS_NOCOLUMNHEADER)) ? (rh + 2) : 0;
      int n = st->m_owner_data_size >= 0 ? st->m_owner_data_size : st->m_data.GetSize();
      int y = hti->pt.y - hdr + st->m_scroll_y;
      int row = y / rh;
      if (row >= 0 && row < n) {
        hti->iItem = row; hti->flags = LVHT_ONITEM;
        return row;
      }
      hti->iItem = -1; hti->flags = LVHT_NOWHERE;
      return -1;
    }

    case LVM_SUBITEMHITTEST: {
      int row = (int)SendMessage(hwnd, LVM_HITTEST, 0, lParam);
      if (row < 0 || !st || !lParam) return -1;
      LVHITTESTINFO *hti = (LVHITTESTINFO *)lParam;
      int xpos = -st->m_scroll_x;
      int idx = 0;
      for (int x = 0; x < st->m_cols.GetSize(); x++) {
        int xwid = st->m_cols.Get()[x].xwid;
        if (hti->pt.x >= xpos && hti->pt.x < xpos + xwid) {
          idx = st->m_cols.Get()[x].col_index;
          break;
        }
        xpos += xwid;
      }
      hti->iSubItem = idx;
      return row;
    }

    case LVM_SETBKCOLOR:
      if (st) { st->m_color_bg = (int)lParam; InvalidateRect(hwnd, NULL, FALSE); }
      return TRUE;
    case LVM_SETTEXTBKCOLOR:
      return TRUE;
    case LVM_SETTEXTCOLOR:
      if (st) { st->m_color_text = (int)lParam; InvalidateRect(hwnd, NULL, FALSE); }
      return TRUE;

    case LVM_SETCOLUMNORDERARRAY: {
      if (!st || !lParam) return FALSE;
      int cnt = (int)wParam;
      int *arr = (int *)lParam;
      if (!arr || cnt < 0) return FALSE;
      int ncols = st->m_cols.GetSize();

      // Build new physical order from arr (logical col_index values).
      // Use an index array to reorder in-place via swap to avoid
      // use-after-free (SWELL_ListView_Col owns 'name' raw ptr freed in dtor).
      WDL_TypedBuf<int> newIdx;
      for (int x = 0; x < cnt; x++) {
        for (int i = 0; i < ncols; i++) {
          if (st->m_cols.Get()[i].col_index == arr[x]) {
            newIdx.Add(i);
            break;
          }
        }
      }
      for (int i = 0; i < ncols; i++) {
        bool used = false;
        for (int x = 0; x < newIdx.GetSize(); x++) {
          if (newIdx.Get()[x] == i) { used = true; break; }
        }
        if (!used) newIdx.Add(i);
      }

      // Swap elements into place (in-place reorder).
      // For each target position, swap the desired element into it.
      for (int pos = 0; pos < ncols; pos++) {
        int src = newIdx.Get()[pos];
        if (src != pos) {
          SWELL_ListView_Col tmp = st->m_cols.Get()[pos];
          st->m_cols.Get()[pos] = st->m_cols.Get()[src];
          st->m_cols.Get()[src] = tmp;
          tmp.name = NULL;  // ownership transferred, prevent stack dtor double-free
          // Update newIdx so we can find the element we just displaced
          for (int k = pos + 1; k < ncols; k++) {
            if (newIdx.Get()[k] == pos) { newIdx.Get()[k] = src; break; }
          }
        }
      }
      InvalidateRect(hwnd, NULL, FALSE);
      return TRUE;
    }

    case LVM_GETCOLUMNORDERARRAY: {
      if (!st || !lParam) return FALSE;
      int cnt = (int)wParam;
      int *arr = (int *)lParam;
      for (int x = 0; x < cnt; x++)
        arr[x] = x < st->m_cols.GetSize() ? st->m_cols.Get()[x].col_index : x;
      return TRUE;
    }

    case LVM_GETHEADER:
      return (LRESULT)hwnd; // return self as header

    case LVM_GETCOLUMNCOUNT:
      if (!st) return 0;
      return (LRESULT)st->m_cols.GetSize();

    // ---- ListBox messages ----

    case LB_ADDSTRING:
    case LB_INSERTSTRING: {
      if (!st) return LB_ERR;
      const char *s = (const char *)lParam;
      int pos = (msg == LB_INSERTSTRING) ? (int)wParam : st->m_data.GetSize();
      if (pos < 0 || pos > st->m_data.GetSize()) pos = st->m_data.GetSize();
      SWELL_ListView_Row *row = new SWELL_ListView_Row();
      row->m_cols.Resize(1, false);
      row->m_cols.Get()[0].txt = strdup(s ? s : "");
      st->m_data.Insert(pos, row);
      InvalidateRect(hwnd, NULL, FALSE);
      return pos;
    }

    case LB_DELETESTRING: {
      if (!st) return LB_ERR;
      int pos = (int)wParam;
      if (pos < 0 || pos >= st->m_data.GetSize()) return LB_ERR;
      st->m_data.Delete(pos, true);
      if (st->m_selitem >= st->m_data.GetSize()) st->m_selitem = st->m_data.GetSize()-1;
      InvalidateRect(hwnd, NULL, FALSE);
      return st->m_data.GetSize();
    }

    case LB_RESETCONTENT:
      if (st) { st->m_data.Empty(true); st->m_selitem = -1; InvalidateRect(hwnd, NULL, FALSE); }
      return 0;

    case LB_GETCOUNT:
      return st ? st->m_data.GetSize() : 0;

    case LB_GETCURSEL:
      return st ? st->m_selitem : LB_ERR;

    case LB_SETCURSEL: {
      if (!st) return LB_ERR;
      int idx = (int)wParam;
      if (idx >= st->m_data.GetSize()) return LB_ERR;
      st->m_selitem = idx;
      InvalidateRect(hwnd, NULL, FALSE);
      return idx;
    }

    case LB_GETSEL:
      if (!st) return LB_ERR;
      { int i = (int)wParam; if (i<0||i>=st->m_data.GetSize()) return LB_ERR;
        return (st->m_selitem == i || (st->m_data.Get(i)->m_tmp & 1)) ? 1 : 0; }

    case LB_SETSEL:
      if (st && (int)lParam >= 0 && (int)lParam < st->m_data.GetSize()) {
        if (wParam) { st->m_data.Get((int)lParam)->m_tmp |= 1; st->m_selitem = (int)lParam; }
        else st->m_data.Get((int)lParam)->m_tmp &= ~1;
        InvalidateRect(hwnd, NULL, FALSE);
      }
      return 0;

    case LB_GETTEXT: {
      if (!st || !lParam) return LB_ERR;
      int idx = (int)wParam;
      if (idx < 0 || idx >= st->m_data.GetSize()) return LB_ERR;
      SWELL_ListView_Row *r = st->m_data.Get(idx);
      const char *t = r->m_cols.GetSize() > 0 && r->m_cols.Get()[0].txt
                    ? r->m_cols.Get()[0].txt : "";
      strcpy((char *)lParam, t);
      return (LRESULT)strlen(t);
    }

    case LB_GETTEXTLEN: {
      if (!st) return LB_ERR;
      int idx = (int)wParam;
      if (idx < 0 || idx >= st->m_data.GetSize()) return LB_ERR;
      SWELL_ListView_Row *r = st->m_data.Get(idx);
      return r->m_cols.GetSize() > 0 && r->m_cols.Get()[0].txt
           ? (int)strlen(r->m_cols.Get()[0].txt) : 0;
    }

    case LB_GETITEMDATA:
      if (!st) return LB_ERR;
      { int i = (int)wParam; if (i<0||i>=st->m_data.GetSize()) return LB_ERR;
        return (LRESULT)st->m_data.Get(i)->m_param; }

    case LB_SETITEMDATA:
      if (!st) return LB_ERR;
      { int i = (int)wParam; if (i<0||i>=st->m_data.GetSize()) return LB_ERR;
        st->m_data.Get(i)->m_param = (LPARAM)lParam; return 0; }

    case LB_FINDSTRINGEXACT: {
      if (!st) return LB_ERR;
      int start = (int)wParam;
      const char *s = (const char *)lParam;
      if (!s) return LB_ERR;
      int n = st->m_data.GetSize();
      for (int i = 0; i < n; i++) {
        int idx = (start + 1 + i) % n;
        SWELL_ListView_Row *r = st->m_data.Get(idx);
        const char *t = r->m_cols.GetSize() > 0 ? r->m_cols.Get()[0].txt : NULL;
        if (t && !strcasecmp(t, s)) return idx;
      }
      return LB_ERR;
    }

    case LB_GETSELCOUNT: {
      if (!st) return 0;
      int cnt = 0;
      for (int i = 0; i < st->m_data.GetSize(); i++)
        if (st->m_data.Get(i)->m_tmp & 1) cnt++;
      return cnt;
    }

    // ---- Mouse ----

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
      if (!st) return 0;
      SetFocus(hwnd);
      RECT cr; GetClientRect(hwnd, &cr);
      int rh = st->m_last_row_height > 0 ? st->m_last_row_height : 16;
      int hdr = (!st->m_is_listbox && st->m_cols.GetSize() > 0 &&
                  !(hwnd->m_style & LVS_NOCOLUMNHEADER)) ? (rh + 2) : 0;
      int my = GET_Y_LPARAM(lParam);
      int mx = GET_X_LPARAM(lParam);
      int n = st->m_owner_data_size >= 0 ? st->m_owner_data_size : st->m_data.GetSize();

      // Check vertical scrollbar
      int totalH = n * rh;
      int viewH = cr.bottom - cr.top;
      if (totalH > viewH) {
        const swell_theme &th = g_swell_theme;
        if (mx >= cr.right - th.scrollbar_width) {
          int hit = hitVScrollbar(cr, viewH, totalH, st->m_scroll_y, my);
          if (hit == 2) {
            st->m_sb_dragging = 1;
            st->m_sb_drag_mouse_y = my;
            st->m_sb_drag_scroll_y = st->m_scroll_y;
            SetCapture(hwnd);
            return 0;
          } else if (hit == 1) {
            st->m_scroll_y -= viewH;
            if (st->m_scroll_y < 0) st->m_scroll_y = 0;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
          } else if (hit == 3) {
            st->m_scroll_y += viewH;
            int vmax = totalH - viewH;
            if (st->m_scroll_y > vmax) st->m_scroll_y = vmax;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
          }
        }
      }

      // Check horizontal scrollbar
      int totalW = 0;
      for (int c = 0; c < st->m_cols.GetSize(); c++)
        totalW += st->m_cols.Get()[c].xwid;
      int viewW = cr.right - cr.left;
      if (totalW > viewW) {
        const swell_theme &th = g_swell_theme;
        if (my >= cr.bottom - th.scrollbar_width) {
          int hit = hitHScrollbar(cr, viewW, totalW, st->m_scroll_x, mx);
          if (hit == 2) {
            st->m_sb_dragging = 2;
            st->m_sb_drag_mouse_x = mx;
            st->m_sb_drag_scroll_x = st->m_scroll_x;
            SetCapture(hwnd);
            return 0;
          } else if (hit == 1) {
            st->m_scroll_x -= viewW;
            if (st->m_scroll_x < 0) st->m_scroll_x = 0;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
          } else if (hit == 3) {
            st->m_scroll_x += viewW;
            int hmax = totalW - viewW;
            if (st->m_scroll_x > hmax) st->m_scroll_x = hmax;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
          }
        }
      }

      int row = (my - hdr + st->m_scroll_y) / rh;
      if (row >= 0 && row < n) {
        int oldsel = st->m_selitem;
        st->m_selitem = row;
        if (!st->m_is_listbox && st->m_is_multisel && row < st->m_data.GetSize())
          st->m_data.Get(row)->m_tmp |= 1;
        InvalidateRect(hwnd, NULL, FALSE);
        if (st->m_is_listbox) {
          notify_parent(hwnd, msg == WM_LBUTTONDBLCLK ? LBN_DBLCLK : LBN_SELCHANGE);
        } else {
          // send NM_CLICK or NM_DBLCLK
          NMLISTVIEW nm = {};
          nm.hdr.hwndFrom = hwnd;
          nm.hdr.idFrom = hwnd->m_id;
          nm.hdr.code = (msg == WM_LBUTTONDBLCLK) ? NM_DBLCLK : NM_CLICK;
          nm.iItem = row;
          nm.iSubItem = 0;
          nm.ptAction.x = GET_X_LPARAM(lParam);
          nm.ptAction.y = my;
          HWND par = GetParent(hwnd);
          if (par) SendMessage(par, WM_NOTIFY, hwnd->m_id, (LPARAM)&nm);

          // LVN_ITEMCHANGED
          if (oldsel != row) {
            nm.hdr.code = LVN_ITEMCHANGED;
            nm.uChanged = LVIF_STATE;
            nm.uNewState = LVIS_SELECTED | LVIS_FOCUSED;
            nm.uOldState = 0;
            if (par) SendMessage(par, WM_NOTIFY, hwnd->m_id, (LPARAM)&nm);
          }
        }
      }
      return 0;
    }

    case WM_MOUSEMOVE:
      if (st && st->m_sb_dragging) {
        if (st->m_sb_dragging == 1) {
          int my = GET_Y_LPARAM(lParam);
          int dy = my - st->m_sb_drag_mouse_y;
          if (dy != 0) {
            int rh = st->m_last_row_height > 0 ? st->m_last_row_height : 16;
            int n = st->m_owner_data_size >= 0 ? st->m_owner_data_size : st->m_data.GetSize();
            int totalH = n * rh;
            RECT cr; GetClientRect(hwnd, &cr);
            int viewH = cr.bottom - cr.top;
            if (totalH > viewH) {
              int ttop, tbot;
              getVThumb(viewH, totalH, st->m_sb_drag_scroll_y, &ttop, &tbot);
              int new_thumb = ttop + dy;
              st->m_scroll_y = scrollFromThumbPos(new_thumb, viewH, totalH);
              int vmax = totalH - viewH;
              if (st->m_scroll_y > vmax) st->m_scroll_y = vmax;
              if (st->m_scroll_y < 0) st->m_scroll_y = 0;
              InvalidateRect(hwnd, NULL, FALSE);
            }
          }
        } else {
          int mx = GET_X_LPARAM(lParam);
          int dx = mx - st->m_sb_drag_mouse_x;
          if (dx != 0) {
            int totalW = 0;
            for (int c = 0; c < st->m_cols.GetSize(); c++)
              totalW += st->m_cols.Get()[c].xwid;
            RECT cr; GetClientRect(hwnd, &cr);
            int viewW = cr.right - cr.left;
            if (totalW > viewW) {
              int tleft, tright;
              getHThumb(viewW, totalW, st->m_sb_drag_scroll_x, &tleft, &tright);
              int new_thumb = tleft + dx;
              st->m_scroll_x = scrollFromThumbPos(new_thumb, viewW, totalW);
              int hmax = totalW - viewW;
              if (st->m_scroll_x > hmax) st->m_scroll_x = hmax;
              if (st->m_scroll_x < 0) st->m_scroll_x = 0;
              InvalidateRect(hwnd, NULL, FALSE);
            }
          }
        }
        return 0;
      }
      if (st && !st->m_sb_dragging) {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        int rh = st->m_last_row_height > 0 ? st->m_last_row_height : 16;
        RECT cr; GetClientRect(hwnd, &cr);
        const swell_theme &th = g_swell_theme;
        int new_hover = 0;

        // Vertical scrollbar
        int n = st->m_owner_data_size >= 0 ? st->m_owner_data_size : st->m_data.GetSize();
        int totalH = n * rh;
        int viewH = cr.bottom - cr.top;
        if (totalH > viewH && mx >= cr.right - th.scrollbar_width) {
          int hit = hitVScrollbar(cr, viewH, totalH, st->m_scroll_y, my);
          if (hit == 2) new_hover |= 1;
        }

        // Horizontal scrollbar
        int totalW = 0;
        for (int c = 0; c < st->m_cols.GetSize(); c++)
          totalW += st->m_cols.Get()[c].xwid;
        int viewW = cr.right - cr.left;
        if (totalW > viewW && my >= cr.bottom - th.scrollbar_width) {
          int hit = hitHScrollbar(cr, viewW, totalW, st->m_scroll_x, mx);
          if (hit == 2) new_hover |= 2;
        }

        if (new_hover != st->m_sb_hover) {
          st->m_sb_hover = new_hover;
          InvalidateRect(hwnd, NULL, FALSE);
        }
      }
      return 0;

    case WM_LBUTTONUP:
      if (st && st->m_sb_dragging) {
        st->m_sb_dragging = 0;
        ReleaseCapture();
        return 0;
      }
      return 0;

    case WM_MOUSEWHEEL: {
      if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState(VK_MENU) & 0x8000))
        return DefWindowProc(hwnd, msg, wParam, lParam);
      if (!st) return 0;
      int delta = (short)HIWORD(wParam);
      st->m_scroll_y -= delta / 40 * (st->m_last_row_height > 0 ? st->m_last_row_height : 16);
      if (st->m_scroll_y < 0) st->m_scroll_y = 0;
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      if (!hdc) return 0;
      if (!st) { EndPaint(hwnd, &ps); return 0; }

      RECT cr; GetClientRect(hwnd, &cr);
      int rh = 0;
      {
        HFONT f = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
        SelectObject(hdc, f);
        TEXTMETRIC tm; GetTextMetrics(hdc, &tm);
        rh = tm.tmHeight + 2;
        if (rh < 14) rh = 14;
        st->m_last_row_height = rh;
      }

      const swell_theme &th = g_swell_theme;

      RECT outer = cr;
      const int frame_r = th.corner_radius;
      fill_bg(hwnd, hdc, WM_CTLCOLORLISTBOX, th.bg_window);
      SWELL_PushClipRegion(hdc);
      SWELL_SetClipRoundRect(hdc, outer.left, outer.top, outer.right, outer.bottom, frame_r);

      HBRUSH bg = CreateSolidBrush((COLORREF)th.bg_input);
      FillRect(hdc, &outer, bg);
      DeleteObject(bg);

      bool focused = (GetFocus() == hwnd);
      int hdr = 0;

      // Draw column headers (ListView only)
      if (!st->m_is_listbox && st->m_cols.GetSize() > 0 &&
          !(hwnd->m_style & LVS_NOCOLUMNHEADER)) {
        hdr = rh + th.padding_listheader_v * 2;
        RECT hr = { cr.left, cr.top, cr.right, cr.top + hdr };
        HBRUSH hbg = CreateSolidBrush((COLORREF)th.bg_header);
        FillRect(hdc, &hr, hbg);
        DeleteObject(hbg);

        // Separator line under header
        HPEN sp = CreatePen(PS_SOLID, th.border_width, (COLORREF)th.border);
        HGDIOBJ osp = SelectObject(hdc, sp);
        MoveToEx(hdc, cr.left, cr.top + hdr - 1, NULL);
        LineTo(hdc, cr.right, cr.top + hdr - 1);
        SelectObject(hdc, osp); DeleteObject(sp);

        SetTextColor(hdc, (COLORREF)th.fg_text_dim);
        SetBkMode(hdc, TRANSPARENT);
        int cx = cr.left - st->m_scroll_x;
        for (int c = 0; c < st->m_cols.GetSize(); c++) {
          const SWELL_ListView_Col &col = st->m_cols.Get()[c];
          RECT tr = { cx + th.padding_listheader_h, cr.top,
                      cx + col.xwid - th.padding_listheader_h, cr.top + hdr };
          if (col.name) SWELL_DrawText(hdc, col.name, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
          // Column divider (subtle)
          HPEN dp = CreatePen(PS_SOLID, th.border_width, (COLORREF)th.border);
          HGDIOBJ op = SelectObject(hdc, dp);
          MoveToEx(hdc, cx + col.xwid - 1, cr.top + 2, NULL);
          LineTo(hdc, cx + col.xwid - 1, cr.top + hdr - 2);
          SelectObject(hdc, op); DeleteObject(dp);
          cx += col.xwid;
        }
      }

      // Draw rows
      int n = st->m_owner_data_size >= 0 ? st->m_owner_data_size : st->m_data.GetSize();

      // Clamp scroll to valid range
      int vmax = n * rh - (cr.bottom - cr.top);
      if (vmax < 0) vmax = 0;
      if (st->m_scroll_y > vmax) st->m_scroll_y = vmax;
      if (st->m_scroll_y < 0) st->m_scroll_y = 0;

      int totalW = 0;
      for (int c = 0; c < st->m_cols.GetSize(); c++)
        totalW += st->m_cols.Get()[c].xwid;
      int hmax = totalW - (cr.right - cr.left);
      if (hmax < 0) hmax = 0;
      if (st->m_scroll_x > hmax) st->m_scroll_x = hmax;
      if (st->m_scroll_x < 0) st->m_scroll_x = 0;

      // Reserve space for visible scrollbars
      int cr_right_orig = cr.right;
      int cr_bottom_orig = cr.bottom;
      int totalH = n * rh;
      int totalW_calc = totalW;
      int viewH = cr.bottom - cr.top;
      int viewW = cr.right - cr.left;
      if (totalH > viewH) cr.right -= th.scrollbar_width;
      if (totalW_calc > viewW) cr.bottom -= th.scrollbar_width;
      if (cr.right < cr.left + 1) cr.right = cr.left + 1;
      if (cr.bottom < cr.top + hdr + 1) cr.bottom = cr.top + hdr + 1;

      int top_row = st->m_scroll_y / rh;

      for (int i = top_row; i < n; i++) {
        int ry = cr.top + hdr + i * rh - st->m_scroll_y;
        if (ry >= cr.bottom) break;
        if (ry + rh < cr.top + hdr) continue;

        bool sel = (st->m_selitem == i) ||
                   (i < st->m_data.GetSize() && (st->m_data.Get(i)->m_tmp & 1));

        COLORREF rowbg, rowfg;
        if (sel) {
          rowbg = focused ? (COLORREF)th.accent
                          : (COLORREF)th.bg_input_alt;
          rowfg = focused ? (COLORREF)th.fg_on_accent
                          : (COLORREF)th.fg_text;
        } else {
          rowbg = (i & 1) ? (COLORREF)th.bg_input_alt
                          : (COLORREF)th.bg_input;
          rowfg = (COLORREF)th.fg_text;
        }

        RECT rr = { cr.left, ry, cr.right, ry + rh };
        HBRUSH rb = CreateSolidBrush(rowbg);
        FillRect(hdc, &rr, rb);
        DeleteObject(rb);

        SetTextColor(hdc, rowfg);
        SetBkMode(hdc, TRANSPARENT);

        // For owner-data, get text via LVN_GETDISPINFO
        if (st->m_owner_data_size >= 0) {
          NMLVDISPINFO di = {};
          di.hdr.hwndFrom = hwnd;
          di.hdr.idFrom = hwnd->m_id;
          di.hdr.code = LVN_GETDISPINFO;
          char buf[512]; buf[0] = 0;
          di.item.mask = LVIF_TEXT;
          di.item.iItem = i;
          di.item.iSubItem = 0;
          di.item.pszText = buf;
          di.item.cchTextMax = sizeof(buf);
          HWND par = GetParent(hwnd);

          int ncols = st->m_cols.GetSize();
          if (ncols == 0) {
            if (par) SendMessage(par, WM_NOTIFY, hwnd->m_id, (LPARAM)&di);
            RECT tr = { cr.left+2, ry, cr.right-2, ry+rh };
            SWELL_DrawText(hdc, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
          } else {
            int cx = cr.left - st->m_scroll_x;
            for (int c = 0; c < ncols; c++) {
              di.item.iSubItem = st->m_cols.Get()[c].col_index;
              di.item.pszText = buf; buf[0] = 0;
              if (par) SendMessage(par, WM_NOTIFY, hwnd->m_id, (LPARAM)&di);
              RECT tr = { cx+2, ry, cx + st->m_cols.Get()[c].xwid - 2, ry+rh };
              SWELL_DrawText(hdc, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
              cx += st->m_cols.Get()[c].xwid;
            }
          }
        } else if (i < st->m_data.GetSize()) {
          SWELL_ListView_Row *row = st->m_data.Get(i);
          int ncols = st->m_cols.GetSize();
          if (ncols == 0 || st->m_is_listbox) {
            const char *t = row->m_cols.GetSize() > 0 && row->m_cols.Get()[0].txt
                          ? row->m_cols.Get()[0].txt : "";
            RECT tr = { cr.left+2, ry, cr.right-2, ry+rh };
            SWELL_DrawText(hdc, t, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
          } else {
            int cx = cr.left - st->m_scroll_x;
            for (int c = 0; c < ncols; c++) {
              const char *t = "";
              int logcol = st->m_cols.Get()[c].col_index;
              if (logcol < row->m_cols.GetSize() && row->m_cols.Get()[logcol].txt)
                t = row->m_cols.Get()[logcol].txt;
              RECT tr = { cx+2, ry, cx + st->m_cols.Get()[c].xwid - 2, ry+rh };
              SWELL_DrawText(hdc, t, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
              cx += st->m_cols.Get()[c].xwid;
            }
          }
        }
      }

      // Vertical scrollbar
      {
        RECT sb_cr = cr; sb_cr.right = cr_right_orig; sb_cr.bottom = cr_bottom_orig;
        drawVerticalScrollbar(hdc, sb_cr, sb_cr.bottom - sb_cr.top, n * rh, st->m_scroll_y, st->m_sb_hover & 1);
      }

      // Horizontal scrollbar
      if (totalW > 0) {
        RECT sb_cr = cr; sb_cr.right = cr_right_orig; sb_cr.bottom = cr_bottom_orig;
        drawHorizontalScrollbar(hdc, sb_cr, sb_cr.right - sb_cr.left, totalW, st->m_scroll_x, st->m_sb_hover & 2);
      }

      SWELL_PopClipRegion(hdc);

      HPEN fp = CreatePen(PS_SOLID, th.border_width, (COLORREF)th.border_strong);
      HGDIOBJ ofp = SelectObject(hdc, fp);
      HGDIOBJ ofb = SelectObject(hdc, GetStockObject(NULL_BRUSH));
      RoundRect(hdc, outer.left, outer.top, outer.right, outer.bottom,
                frame_r * 2, frame_r * 2);
      SelectObject(hdc, ofp); DeleteObject(fp);
      SelectObject(hdc, ofb);

      EndPaint(hwnd, &ps);
      return 0;
    }

    case WM_SIZE:
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;

    default:
      return DefWindowProc(hwnd, msg, wParam, lParam);
  }
}

// ===========================================================================
// 5. treeViewWindowProc
// ===========================================================================

static bool tv_contains_item(HTREEITEM root, HTREEITEM target)
{
  if (!root || !target) return false;
  if (root == target) return true;
  for (int i = 0; i < root->m_children.GetSize(); i++) {
    if (tv_contains_item(root->m_children.Get(i), target)) return true;
  }
  return false;
}

static void tv_send_selchange(HWND hwnd, HTREEITEM newitem)
{
  HWND par = GetParent(hwnd);
  if (!par) return;
  static int __rent;
  if (!__rent) {
    __rent++;
    NMTREEVIEW nm = {};
    nm.hdr.hwndFrom = hwnd;
    nm.hdr.idFrom   = hwnd->m_id;
    nm.hdr.code     = TVN_SELCHANGED;
    nm.itemNew.hItem = newitem;
    nm.itemNew.lParam = newitem ? newitem->m_param : 0;
    SendMessage(par, WM_NOTIFY, hwnd->m_id, (LPARAM)&nm);
    __rent--;
  }
}

static void tv_send_mouse_notify(HWND hwnd, UINT code, HTREEITEM item, int x, int y)
{
  HWND par = GetParent(hwnd);
  if (!par) return;
  NMMOUSE nm = {};
  nm.hdr.hwndFrom = hwnd;
  nm.hdr.idFrom = hwnd->m_id;
  nm.hdr.code = code;
  nm.dwItemSpec = (DWORD_PTR)item;
  nm.dwItemData = item ? (DWORD_PTR)item->m_param : 0;
  nm.pt.x = x;
  nm.pt.y = y;
  nm.dwHitInfo = item ? TVHT_ONITEM : TVHT_NOWHERE;
  SendMessage(par, WM_NOTIFY, hwnd->m_id, (LPARAM)&nm);
}

static inline int tv_content_pad()
{
  return scaled_px(6);
}

static inline int tv_text_gap()
{
  return scaled_px(6);
}

static inline int tv_indent_width(int row_h)
{
  int min_indent = scaled_px(18);
  return row_h > min_indent ? row_h : min_indent;
}

static inline int tv_expander_width(int row_h)
{
  int w = scaled_px(14);
  if (w > row_h) w = row_h;
  return w > 1 ? w : 1;
}

static void tv_draw_expander(HDC hdc, int cx, int cy, bool expanded, COLORREF color)
{
  if (!hdc || !hdc->canvas) return;

  SkPaint arrow;
  arrow.setAntiAlias(true);
  arrow.setColor(controls_to_sk(color));
  arrow.setStyle(SkPaint::kStroke_Style);
  arrow.setStrokeCap(SkPaint::kRound_Cap);
  arrow.setStrokeJoin(SkPaint::kRound_Join);
  arrow.setStrokeWidth((float)scaled_px(1));

  const float x = (float)cx;
  const float y = (float)cy + 0.5f;
  SkPath p;
  if (expanded) {
    const float hw = (float)scaled_px(3);
    const float hh = (float)scaled_px(2);
    p.moveTo(x - hw, y - hh * 0.5f);
    p.lineTo(x,      y + hh);
    p.lineTo(x + hw, y - hh * 0.5f);
  } else {
    const float hw = (float)scaled_px(2);
    const float hh = (float)scaled_px(3);
    p.moveTo(x - hw * 0.5f, y - hh);
    p.lineTo(x + hw,        y);
    p.lineTo(x - hw * 0.5f, y + hh);
  }
  hdc->canvas->drawPath(p, arrow);
}

// find parent of item (returns NULL if root-level); also returns index
static HTREEITEM tv_find_parent(HTREEITEM root, HTREEITEM target, int *idx_out)
{
  for (int i = 0; i < root->m_children.GetSize(); i++) {
    HTREEITEM ch = root->m_children.Get(i);
    if (ch == target) { if (idx_out) *idx_out = i; return root; }
    HTREEITEM found = tv_find_parent(ch, target, idx_out);
    if (found) return found;
  }
  return NULL;
}

static int tv_visible_count(HTREEITEM node)
{
  int count = 0;
  for (int i = 0; i < node->m_children.GetSize(); i++) {
    HTREEITEM ch = node->m_children.Get(i);
    count++;
    if (ch->m_state & TVIS_EXPANDED)
      count += tv_visible_count(ch);
  }
  return count;
}

static int tv_get_depth(HTREEITEM root, HTREEITEM item)
{
  int depth = 0;
  HTREEITEM cur = item;
  while (cur) {
    int idx = -1;
    HTREEITEM par = tv_find_parent(root, cur, &idx);
    if (!par || par == root) break;
    depth++;
    cur = par;
  }
  return depth;
}

// enumerate visible items into a flat list
static void tv_flatten(HTREEITEM node, WDL_PtrList<HTREEITEM__> &out)
{
  for (int i = 0; i < node->m_children.GetSize(); i++) {
    HTREEITEM ch = node->m_children.Get(i);
    out.Add(ch);
    if (ch->m_state & TVIS_EXPANDED)
      tv_flatten(ch, out);
  }
}

LRESULT treeViewWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  treeViewState *st = (treeViewState *)(void *)hwnd->m_private_data;

  switch (msg) {
    case WM_CREATE:
      st = new treeViewState();
      hwnd->m_private_data = (INT_PTR)st;
      hwnd->m_wantfocus = true;
      return 0;

    case WM_NCDESTROY:
      delete st;
      hwnd->m_private_data = 0;
      return 0;

    case TVM_INSERTITEM: {
      if (!st || !lParam) return 0;
      TV_INSERTSTRUCT *ins = (TV_INSERTSTRUCT *)lParam;
      HTREEITEM__ *newitem = new HTREEITEM__();
      if (ins->item.mask & TVIF_TEXT && ins->item.pszText)
        newitem->m_value.Set(ins->item.pszText);
      if (ins->item.mask & TVIF_PARAM) newitem->m_param = ins->item.lParam;
      if (ins->item.mask & TVIF_STATE) newitem->m_state = ins->item.state;
      if (ins->item.mask & TVIF_CHILDREN) newitem->m_haschildren = ins->item.cChildren > 0;
      if (ins->item.mask & TVIF_IMAGE) newitem->m_image = ins->item.iImage;
      if (ins->item.mask & TVIF_SELECTEDIMAGE) newitem->m_selimage = ins->item.iSelectedImage;

      HTREEITEM par = ins->hParent;
      if (!par || par == TVI_ROOT) par = st->m_root;

      HTREEITEM after = ins->hInsertAfter;
      if (after == TVI_LAST || after == TVI_SORT) {
        par->m_children.Add(newitem);
      } else if (after == TVI_FIRST) {
        par->m_children.Insert(0, newitem);
      } else {
        // insert after specific item
        int idx = -1;
        for (int i = 0; i < par->m_children.GetSize(); i++) {
          if (par->m_children.Get(i) == after) { idx = i; break; }
        }
        if (idx >= 0) par->m_children.Insert(idx+1, newitem);
        else par->m_children.Add(newitem);
      }
      InvalidateRect(hwnd, NULL, FALSE);
      return (LRESULT)newitem;
    }

    case TVM_EXPAND: {
      if (!st || !lParam) return FALSE;
      HTREEITEM item = (HTREEITEM)lParam;
      if (!tv_contains_item(st->m_root, item)) return FALSE;
      UINT flag = (UINT)wParam;
      if (flag == TVE_EXPAND && (item->m_state & TVIS_EXPANDED)) return TRUE;
      if (flag == TVE_COLLAPSE && !(item->m_state & TVIS_EXPANDED)) return TRUE;
      HWND par = GetParent(hwnd);
      if (par) {
        NMTREEVIEW nm = {};
        nm.hdr.hwndFrom = hwnd;
        nm.hdr.idFrom = hwnd->m_id;
        nm.hdr.code = TVN_ITEMEXPANDING;
        nm.action = flag;
        nm.itemNew.hItem = item;
        nm.itemNew.mask = TVIF_HANDLE | TVIF_PARAM;
        nm.itemNew.lParam = item->m_param;
        if (SendMessage(par, WM_NOTIFY, hwnd->m_id, (LPARAM)&nm)) return TRUE;
      }
      if (flag == TVE_EXPAND)        item->m_state |=  TVIS_EXPANDED;
      else if (flag == TVE_COLLAPSE) item->m_state &= ~TVIS_EXPANDED;
      else if (flag == TVE_TOGGLE)
        item->m_state ^= TVIS_EXPANDED;
      InvalidateRect(hwnd, NULL, FALSE);
      return TRUE;
    }

    case TVM_GETSELECTION:
      return st ? (LRESULT)st->m_sel : 0;

    case TVM_SELECTITEM: {
      if (!st) return FALSE;
      HTREEITEM item = (HTREEITEM)lParam;
      if (item && !tv_contains_item(st->m_root, item)) return FALSE;
      if (st->m_sel == item) return TRUE;
      st->m_sel = item;
      tv_send_selchange(hwnd, item);
      if (item) SendMessage(hwnd, TVM_ENSUREVISIBLE, 0, (LPARAM)item);
      InvalidateRect(hwnd, NULL, FALSE);
      return TRUE;
    }

    case TVM_DELETEITEM: {
      if (!st) return FALSE;
      HTREEITEM item = (HTREEITEM)lParam;
      if (!item || item == TVI_ROOT) {
        // delete all
        st->m_root->m_children.Empty(true);
        st->m_sel = NULL;
        InvalidateRect(hwnd, NULL, FALSE);
        return TRUE;
      }
      int idx = -1;
      HTREEITEM par = tv_find_parent(st->m_root, item, &idx);
      if (par && idx >= 0) {
        if (st->m_sel && tv_contains_item(item, st->m_sel)) st->m_sel = par == st->m_root ? NULL : par;
        par->m_children.Delete(idx, true);
        InvalidateRect(hwnd, NULL, FALSE);
        return TRUE;
      }
      return FALSE;
    }

    case TVM_DELETEALLITEMS:
      if (st) {
        st->m_root->m_children.Empty(true);
        st->m_sel = NULL;
        InvalidateRect(hwnd, NULL, FALSE);
      }
      return TRUE;

    case TVM_GETITEM: {
      if (!st || !lParam) return FALSE;
      LPTVITEM item = (LPTVITEM)lParam;
      HTREEITEM h = item->hItem;
      if (!h || !tv_contains_item(st->m_root, h)) return FALSE;
      if (item->mask & TVIF_TEXT && item->pszText && item->cchTextMax > 0)
        lstrcpyn(item->pszText, h->m_value.Get(), item->cchTextMax);
      if (item->mask & TVIF_PARAM) item->lParam = h->m_param;
      if (item->mask & TVIF_STATE) item->state = (h == st->m_sel ? TVIS_SELECTED : 0) | (h->m_state & TVIS_EXPANDED);
      if (item->mask & TVIF_CHILDREN) item->cChildren = h->m_children.GetSize() > 0 || h->m_haschildren ? 1 : 0;
      return TRUE;
    }

    case TVM_SETITEM: {
      if (!st || !lParam) return FALSE;
      LPTVITEM item = (LPTVITEM)lParam;
      HTREEITEM h = item->hItem;
      if (!h || !tv_contains_item(st->m_root, h)) return FALSE;
      if (item->mask & TVIF_TEXT && item->pszText) h->m_value.Set(item->pszText);
      if (item->mask & TVIF_PARAM) h->m_param = item->lParam;
      if (item->mask & TVIF_STATE) {
        h->m_state = (h->m_state & ~item->stateMask) | (item->state & item->stateMask & ~TVIS_SELECTED);
        if ((item->stateMask & item->state & TVIS_SELECTED) && st->m_sel != h) {
          st->m_sel = h;
          tv_send_selchange(hwnd, h);
        }
      }
      if (item->mask & TVIF_CHILDREN) h->m_haschildren = item->cChildren > 0;
      InvalidateRect(hwnd, NULL, FALSE);
      return TRUE;
    }

    case TVM_HITTEST: {
      if (!st || !lParam) return 0;
      TVHITTESTINFO *hti = (TVHITTESTINFO *)lParam;
      int rh = st->m_last_row_height > 0 ? st->m_last_row_height : 16;
      WDL_PtrList<HTREEITEM__> items;
      tv_flatten(st->m_root, items);
      int y = hti->pt.y + st->m_scroll_y;
      int row = y / rh;
      if (row >= 0 && row < items.GetSize()) {
        hti->hItem = items.Get(row);
        const int indent = tv_indent_width(rh);
        const int expw = tv_expander_width(rh);
        int base = tv_content_pad() + tv_get_depth(st->m_root, hti->hItem) * indent;
        if (hti->pt.x >= base && hti->pt.x < base + expw)
          hti->flags = TVHT_ONITEMBUTTON;
        else if (hti->pt.x >= base + expw + tv_text_gap())
          hti->flags = TVHT_ONITEMLABEL;
        else
          hti->flags = TVHT_ONITEMINDENT;
        return (LRESULT)hti->hItem;
      }
      hti->hItem = NULL; hti->flags = TVHT_NOWHERE;
      return 0;
    }

    case TVM_GETNEXTITEM: {
      if (!st || !lParam) return 0;
      UINT flag = (UINT)wParam;
      HTREEITEM item = (HTREEITEM)lParam;
      switch (flag) {
        case TVGN_ROOT:
          return st->m_root->m_children.GetSize() > 0 ? (LRESULT)st->m_root->m_children.Get(0) : 0;
        case TVGN_NEXT: {
          if (!item) return st->m_root->m_children.GetSize() > 0 ? (LRESULT)st->m_root->m_children.Get(0) : 0;
          int idx = -1;
          HTREEITEM par = tv_find_parent(st->m_root, item, &idx);
          if (par && idx >= 0 && idx + 1 < par->m_children.GetSize())
            return (LRESULT)par->m_children.Get(idx+1);
          return 0;
        }
        case TVGN_PREVIOUS: {
          if (!item) return 0;
          int idx = -1;
          HTREEITEM par = tv_find_parent(st->m_root, item, &idx);
          if (par && idx > 0)
            return (LRESULT)par->m_children.Get(idx-1);
          return 0;
        }
        case TVGN_PARENT: {
          if (!item) return st->m_root->m_children.GetSize() > 0 ? (LRESULT)st->m_root->m_children.Get(0) : 0;
          int dummy = -1;
          HTREEITEM par = tv_find_parent(st->m_root, item, &dummy);
          return (par == st->m_root) ? 0 : (LRESULT)par;
        }
        case TVGN_CHILD:
          if (!item || item == TVI_ROOT)
            return st->m_root->m_children.GetSize() > 0 ? (LRESULT)st->m_root->m_children.Get(0) : 0;
          return item->m_children.GetSize() > 0 ? (LRESULT)item->m_children.Get(0) : 0;
        case TVGN_CARET:
          return st ? (LRESULT)st->m_sel : 0;
        case TVGN_NEXTVISIBLE: {
          if (!item) return 0;
          WDL_PtrList<HTREEITEM__> items;
          tv_flatten(st->m_root, items);
          for (int i = 0; i < items.GetSize(); i++) {
            if (items.Get(i) == item && i + 1 < items.GetSize())
              return (LRESULT)items.Get(i+1);
          }
          return 0;
        }
        case TVGN_PREVVISIBLE: {
          if (!item) return 0;
          WDL_PtrList<HTREEITEM__> items;
          tv_flatten(st->m_root, items);
          for (int i = 1; i < items.GetSize(); i++) {
            if (items.Get(i) == item)
              return (LRESULT)items.Get(i-1);
          }
          return 0;
        }
        case TVGN_FIRSTVISIBLE: {
          WDL_PtrList<HTREEITEM__> items;
          tv_flatten(st->m_root, items);
          int rh = st->m_last_row_height > 0 ? st->m_last_row_height : 16;
          int topIdx = st->m_scroll_y / rh;
          if (topIdx >= 0 && topIdx < items.GetSize())
            return (LRESULT)items.Get(topIdx);
          return 0;
        }
        default:
          return 0;
      }
    }

    case TVM_GETCOUNT: {
      if (!st) return 0;
      auto countAll = [](auto&& self, HTREEITEM n) -> int {
        int c = n->m_children.GetSize();
        for (int i = 0; i < n->m_children.GetSize(); i++)
          c += self(self, n->m_children.Get(i));
        return c;
      };
      return (LRESULT)countAll(countAll, st->m_root);
    }

    case TVM_ENSUREVISIBLE: {
      if (!st || !lParam) return FALSE;
      HTREEITEM item = (HTREEITEM)lParam;
      WDL_PtrList<HTREEITEM__> items;
      tv_flatten(st->m_root, items);
      int rh = st->m_last_row_height > 0 ? st->m_last_row_height : 16;
      for (int i = 0; i < items.GetSize(); i++) {
        if (items.Get(i) == item) {
          int y = i * rh;
          RECT cr; GetClientRect(hwnd, &cr);
          int viewH = cr.bottom - cr.top;
          if (y < st->m_scroll_y)
            st->m_scroll_y = y;
          else if (y + rh > st->m_scroll_y + viewH)
            st->m_scroll_y = y + rh - viewH;
          if (st->m_scroll_y < 0) st->m_scroll_y = 0;
          InvalidateRect(hwnd, NULL, FALSE);
          return TRUE;
        }
      }
      return FALSE;
    }

    case TVM_SETINDENT:
      return TRUE;

    case TVM_GETPARENT: {
      if (!st) return 0;
      HTREEITEM item = (HTREEITEM)lParam;
      if (!item) return st->m_root->m_children.GetSize() > 0 ? (LRESULT)st->m_root->m_children.Get(0) : 0;
      int dummy = -1;
      HTREEITEM par = tv_find_parent(st->m_root, item, &dummy);
      return (par == st->m_root) ? 0 : (LRESULT)par;
    }

    case TVM_GETCHILD:
      { HTREEITEM item = (HTREEITEM)lParam;
        if (!st) return 0;
        if (!item || item == TVI_ROOT)
          return st->m_root->m_children.GetSize() > 0 ? (LRESULT)st->m_root->m_children.Get(0) : 0;
        return item->m_children.GetSize() > 0 ? (LRESULT)item->m_children.Get(0) : 0; }

    case TVM_GETNEXTSIBLING: {
      if (!st) return 0;
      HTREEITEM item = (HTREEITEM)lParam;
      if (!item) return st->m_root->m_children.GetSize() > 0 ? (LRESULT)st->m_root->m_children.Get(0) : 0;
      int idx = -1;
      HTREEITEM par = tv_find_parent(st->m_root, item, &idx);
      if (!par || idx < 0) return 0;
      return (idx + 1 < par->m_children.GetSize()) ? (LRESULT)par->m_children.Get(idx+1) : 0;
    }

    case TVM_GETROOT:
      return st ? (LRESULT)(st->m_root->m_children.GetSize() > 0 ? st->m_root->m_children.Get(0) : NULL) : 0;

    case TVM_SETBKCOLOR:
      if (st) { st->m_color_bg = (int)lParam; InvalidateRect(hwnd, NULL, FALSE); }
      return -1;
    case TVM_SETTEXTCOLOR:
      if (st) { st->m_color_text = (int)lParam; InvalidateRect(hwnd, NULL, FALSE); }
      return -1;

    case WM_KEYDOWN: {
      if (!st) return 0;
      int rh = st->m_last_row_height > 0 ? st->m_last_row_height : 16;
      WDL_PtrList<HTREEITEM__> items;
      tv_flatten(st->m_root, items);
      int curIdx = -1;
      for (int i = 0; i < items.GetSize(); i++)
        if (items.Get(i) == st->m_sel) { curIdx = i; break; }

      if (wParam == VK_UP && curIdx > 0) {
        SendMessage(hwnd, TVM_SELECTITEM, 0, (LPARAM)items.Get(curIdx-1));
        return 0;
      }
      if (wParam == VK_DOWN && curIdx + 1 < items.GetSize()) {
        SendMessage(hwnd, TVM_SELECTITEM, 0, (LPARAM)items.Get(curIdx+1));
        return 0;
      }
      if (wParam == VK_LEFT) {
        HTREEITEM sel = st->m_sel;
        if (sel) {
          if (sel->m_state & TVIS_EXPANDED) {
            sel->m_state &= ~TVIS_EXPANDED;
            InvalidateRect(hwnd, NULL, FALSE);
          } else {
            int dummy = -1;
            HTREEITEM par = tv_find_parent(st->m_root, sel, &dummy);
            if (par && par != st->m_root)
              SendMessage(hwnd, TVM_SELECTITEM, 0, (LPARAM)par);
          }
        }
        return 0;
      }
      if (wParam == VK_RIGHT) {
        HTREEITEM sel = st->m_sel;
        if (sel) {
          if (!(sel->m_state & TVIS_EXPANDED) && (sel->m_children.GetSize() > 0 || sel->m_haschildren)) {
            sel->m_state |= TVIS_EXPANDED;
            InvalidateRect(hwnd, NULL, FALSE);
          } else if (sel->m_children.GetSize() > 0) {
            SendMessage(hwnd, TVM_SELECTITEM, 0, (LPARAM)sel->m_children.Get(0));
          }
        }
        return 0;
      }
      return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
      if (!st) return 0;
      SetFocus(hwnd);
      int rh = st->m_last_row_height > 0 ? st->m_last_row_height : 16;
      int my = GET_Y_LPARAM(lParam);
      int mx = GET_X_LPARAM(lParam);

      // Check vertical scrollbar
      WDL_PtrList<HTREEITEM__> items;
      tv_flatten(st->m_root, items);
      int totalH = items.GetSize() * rh;
      RECT cr; GetClientRect(hwnd, &cr);
      int viewH = cr.bottom - cr.top;
      if (totalH > viewH) {
        const swell_theme &th = g_swell_theme;
        if (mx >= cr.right - th.scrollbar_width) {
          int hit = hitVScrollbar(cr, viewH, totalH, st->m_scroll_y, my);
          if (hit == 2) {
            st->m_sb_dragging = 1;
            st->m_sb_drag_mouse = my;
            st->m_sb_drag_scroll = st->m_scroll_y;
            SetCapture(hwnd);
            return 0;
          } else if (hit == 1) {
            st->m_scroll_y -= viewH;
            if (st->m_scroll_y < 0) st->m_scroll_y = 0;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
          } else if (hit == 3) {
            st->m_scroll_y += viewH;
            int vmax = totalH - viewH;
            if (st->m_scroll_y > vmax) st->m_scroll_y = vmax;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
          }
        }
      }

      int indent = tv_indent_width(rh);
      int expw = tv_expander_width(rh);
      int row = (my + st->m_scroll_y) / rh;
      if (row >= 0 && row < items.GetSize()) {
        HTREEITEM item = items.Get(row);
        int base = tv_content_pad() + tv_get_depth(st->m_root, item) * indent;
        if (mx >= base && mx < base + expw &&
            (item->m_children.GetSize() > 0 || item->m_haschildren)) {
          SendMessage(hwnd, TVM_EXPAND, TVE_TOGGLE, (LPARAM)item);
        } else {
          HTREEITEM old = st->m_sel;
          if (old != item) {
            st->m_sel = item;
            tv_send_selchange(hwnd, item);
            InvalidateRect(hwnd, NULL, FALSE);
          }
        }
        if (msg == WM_LBUTTONDBLCLK)
          tv_send_mouse_notify(hwnd, NM_DBLCLK, item, mx, my);
      }
      return 0;
    }

    case WM_MOUSEMOVE:
      if (st && st->m_sb_dragging) {
        int my = GET_Y_LPARAM(lParam);
        int dy = my - st->m_sb_drag_mouse;
        if (dy != 0) {
          int rh = st->m_last_row_height > 0 ? st->m_last_row_height : 16;
          WDL_PtrList<HTREEITEM__> items;
          tv_flatten(st->m_root, items);
          int totalH = items.GetSize() * rh;
          RECT cr; GetClientRect(hwnd, &cr);
          int viewH = cr.bottom - cr.top;
          if (totalH > viewH) {
            int ttop, tbot;
            getVThumb(viewH, totalH, st->m_sb_drag_scroll, &ttop, &tbot);
            int new_thumb = ttop + dy;
            st->m_scroll_y = scrollFromThumbPos(new_thumb, viewH, totalH);
            int vmax = totalH - viewH;
            if (st->m_scroll_y > vmax) st->m_scroll_y = vmax;
            if (st->m_scroll_y < 0) st->m_scroll_y = 0;
            InvalidateRect(hwnd, NULL, FALSE);
          }
        }
        return 0;
      }
      if (st && !st->m_sb_dragging) {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        RECT cr; GetClientRect(hwnd, &cr);
        const swell_theme &th = g_swell_theme;
        int new_hover = 0;
        if (mx >= cr.right - th.scrollbar_width) {
          WDL_PtrList<HTREEITEM__> items;
          tv_flatten(st->m_root, items);
          int rh = st->m_last_row_height > 0 ? st->m_last_row_height : 16;
          int totalH = items.GetSize() * rh;
          int viewH = cr.bottom - cr.top;
          if (totalH > viewH) {
            int hit = hitVScrollbar(cr, viewH, totalH, st->m_scroll_y, my);
            if (hit == 2) new_hover = 1;
          }
        }
        if (new_hover != st->m_sb_hover) {
          st->m_sb_hover = new_hover;
          InvalidateRect(hwnd, NULL, FALSE);
        }
      }
      return 0;

    case WM_LBUTTONUP: {
      if (st && st->m_sb_dragging) {
        st->m_sb_dragging = 0;
        ReleaseCapture();
        return 0;
      }
      if (!st) return 0;
      int rh = st->m_last_row_height > 0 ? st->m_last_row_height : 16;
      WDL_PtrList<HTREEITEM__> items;
      tv_flatten(st->m_root, items);
      int my = GET_Y_LPARAM(lParam);
      int mx = GET_X_LPARAM(lParam);
      int row = (my + st->m_scroll_y) / rh;
      HTREEITEM item = row >= 0 && row < items.GetSize() ? items.Get(row) : NULL;
      tv_send_mouse_notify(hwnd, NM_CLICK, item, mx, my);
      return 0;
    }

    case WM_MOUSEWHEEL: {
      if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState(VK_MENU) & 0x8000))
        return DefWindowProc(hwnd, msg, wParam, lParam);
      if (!st) return 0;
      int delta = (short)HIWORD(wParam);
      int rh = st->m_last_row_height > 0 ? st->m_last_row_height : 16;

      // Count visible items for bottom clamp
      WDL_PtrList<HTREEITEM__> items;
      tv_flatten(st->m_root, items);
      int totalH = items.GetSize() * rh;
      RECT cr; GetClientRect(hwnd, &cr);
      int viewH = cr.bottom - cr.top;
      int vmax = totalH - viewH;
      if (vmax < 0) vmax = 0;

      st->m_scroll_y -= delta / 40 * rh;
      if (st->m_scroll_y < 0) st->m_scroll_y = 0;
      if (st->m_scroll_y > vmax) st->m_scroll_y = vmax;

      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }

    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      if (!hdc || !st) { if (hdc) EndPaint(hwnd, &ps); return 0; }

      RECT cr; GetClientRect(hwnd, &cr);
      int rh = 0;
      {
        HFONT f = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
        SelectObject(hdc, f);
        TEXTMETRIC tm; GetTextMetrics(hdc, &tm);
        rh = tm.tmHeight + 2;
        if (rh < 14) rh = 14;
        st->m_last_row_height = rh;
      }

      const swell_theme &th = g_swell_theme;
      RECT outer = cr;
      const int frame_r = th.corner_radius;
      fill_bg(hwnd, hdc, WM_CTLCOLORLISTBOX, th.bg_window);
      SWELL_PushClipRegion(hdc);
      SWELL_SetClipRoundRect(hdc, outer.left, outer.top, outer.right, outer.bottom, frame_r);
      HBRUSH bg = CreateSolidBrush((COLORREF)th.bg_input);
      FillRect(hdc, &outer, bg);
      DeleteObject(bg);

      SetBkMode(hdc, TRANSPARENT);
      bool focused = (GetFocus() == hwnd);

      WDL_PtrList<HTREEITEM__> items;
      tv_flatten(st->m_root, items);

      // Clamp scroll to valid range
      int vmax = items.GetSize() * rh - (cr.bottom - cr.top);
      if (vmax < 0) vmax = 0;
      if (st->m_scroll_y > vmax) st->m_scroll_y = vmax;
      if (st->m_scroll_y < 0) st->m_scroll_y = 0;

      // Reserve space for vertical scrollbar
      int cr_right_orig = cr.right;
      int totalH = items.GetSize() * rh;
      if (totalH > cr.bottom - cr.top)
        cr.right -= th.scrollbar_width;
      if (cr.right < cr.left + 1) cr.right = cr.left + 1;

      int indent = tv_indent_width(rh);
      int expw = tv_expander_width(rh);
      for (int i = 0; i < items.GetSize(); i++) {
        int ry = cr.top + i * rh - st->m_scroll_y;
        if (ry + rh < cr.top) continue;
        if (ry >= cr.bottom) break;
        HTREEITEM item = items.Get(i);

        // compute depth by finding parent chain
        // rough: just indent proportional to scrolled x
        int depth = tv_get_depth(st->m_root, item);

        int base = cr.left + tv_content_pad() + depth * indent;
        int x = base + expw + tv_text_gap();
        bool sel = (item == st->m_sel);

        if (sel) {
          RECT sr = { cr.left, ry, cr.right, ry + rh };
          HBRUSH sb = CreateSolidBrush(focused ? (COLORREF)th.accent
                                                : (COLORREF)th.bg_input_alt);
          FillRect(hdc, &sr, sb);
          DeleteObject(sb);
        }

        // Expand chevron
        if (item->m_children.GetSize() > 0 || item->m_haschildren) {
          int ax = base + expw / 2;
          int ay = ry + rh/2;
          COLORREF arrowc = sel && focused ? (COLORREF)th.fg_on_accent
                                            : (COLORREF)th.fg_text_dim;
          tv_draw_expander(hdc, ax, ay, (item->m_state & TVIS_EXPANDED) != 0, arrowc);
        }

        SetTextColor(hdc, sel ? (focused ? (COLORREF)th.fg_on_accent
                                          : (COLORREF)th.fg_text)
                               : (COLORREF)th.fg_text);
        RECT tr = { x + 2, ry, cr.right, ry + rh };
        SWELL_DrawText(hdc, item->m_value.Get(), -1, &tr,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      }

      {
        RECT sb_cr = cr; sb_cr.right = cr_right_orig;
        drawVerticalScrollbar(hdc, sb_cr, sb_cr.bottom - sb_cr.top, items.GetSize() * rh, st->m_scroll_y, st->m_sb_hover);
      }

      SWELL_PopClipRegion(hdc);

      HPEN fp = CreatePen(PS_SOLID, th.border_width, (COLORREF)th.border_strong);
      HGDIOBJ ofp = SelectObject(hdc, fp);
      HGDIOBJ ofb = SelectObject(hdc, GetStockObject(NULL_BRUSH));
      RoundRect(hdc, outer.left, outer.top, outer.right, outer.bottom,
                frame_r * 2, frame_r * 2);
      SelectObject(hdc, ofp); DeleteObject(fp);
      SelectObject(hdc, ofb);

      EndPaint(hwnd, &ps);
      return 0;
    }

    default:
      return DefWindowProc(hwnd, msg, wParam, lParam);
  }
}

// ===========================================================================
// 6. comboWindowProc
// ===========================================================================

static bool combo_show_dropdown(HWND hwnd, __SWELL_ComboBoxInternalState *st)
{
  if (!hwnd || !st || st->items.GetSize() <= 0) return false;

  hwnd->Retain();
  notify_parent(hwnd, CBN_DROPDOWN);
  if (!hwnd->m_private_data) {
    hwnd->Release();
    return false;
  }

  HMENU menu = CreatePopupMenu();
  if (!menu) {
    notify_parent(hwnd, CBN_CLOSEUP);
    hwnd->Release();
    return false;
  }

  for (int i = 0; i < st->items.GetSize(); i++) {
    __SWELL_ComboBoxInternalState_rec *rec = st->items.Get(i);
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_STATE | MIIM_TYPE;
    mi.fType = MFT_STRING;
    mi.fState = (i == st->selidx) ? MFS_CHECKED : MFS_UNCHECKED;
    mi.wID = (UINT)(100 + i);
    mi.dwTypeData = rec ? rec->desc : (char *)"";
    InsertMenuItem(menu, i, TRUE, &mi);
  }

  RECT wr;
  GetWindowRect(hwnd, &wr);
  int cmd = TrackPopupMenu(menu,
                           TPM_NONOTIFY | TPM_RETURNCMD | TPM_LEFTALIGN |
                               TPM_TOPALIGN,
                           wr.left, wr.bottom, 0, hwnd, NULL);
  DestroyMenu(menu);

  const bool hwnd_alive = hwnd->m_private_data != 0;
  if (hwnd_alive && cmd >= 100 && cmd < 100 + st->items.GetSize()) {
    const int sel = cmd - 100;
    st->selidx = sel;
    __SWELL_ComboBoxInternalState_rec *rec = st->items.Get(sel);
    hwnd->m_title.Set(rec && rec->desc ? rec->desc : "");
    InvalidateRect(hwnd, NULL, FALSE);
    notify_parent(hwnd, CBN_SELCHANGE);
  }

  if (hwnd->m_private_data) notify_parent(hwnd, CBN_CLOSEUP);
  hwnd->Release();
  return true;
}

LRESULT comboWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  __SWELL_ComboBoxInternalState *st =
      (__SWELL_ComboBoxInternalState *)(void *)hwnd->m_private_data;

  switch (msg) {
    case WM_CREATE:
      st = new __SWELL_ComboBoxInternalState();
      hwnd->m_private_data = (INT_PTR)st;
      hwnd->m_wantfocus = true;
      return 0;

    case WM_NCDESTROY:
      delete st;
      hwnd->m_private_data = 0;
      return 0;

    case CB_ADDSTRING: {
      if (!st) return CB_ERR;
      const char *s = (const char *)lParam;
      __SWELL_ComboBoxInternalState_rec *rec = new __SWELL_ComboBoxInternalState_rec();
      rec->desc = strdup(s ? s : "");
      if (hwnd->m_style & CBS_SORT) {
        // binary insert sorted
        int n = st->items.GetSize();
        int lo = 0, hi = n;
        while (lo < hi) {
          int mid = (lo+hi)/2;
          if (strcmp(st->items.Get(mid)->desc, rec->desc) <= 0) lo = mid+1;
          else hi = mid;
        }
        if (st->selidx >= lo) st->selidx++;
        st->items.Insert(lo, rec);
        InvalidateRect(hwnd, NULL, FALSE);
        return lo;
      }
      st->items.Add(rec);
      InvalidateRect(hwnd, NULL, FALSE);
      return st->items.GetSize() - 1;
    }

    case CB_INSERTSTRING: {
      if (!st) return CB_ERR;
      const char *s = (const char *)lParam;
      int pos = (int)wParam;
      __SWELL_ComboBoxInternalState_rec *rec = new __SWELL_ComboBoxInternalState_rec();
      rec->desc = strdup(s ? s : "");
      if (pos < 0 || pos > st->items.GetSize()) pos = st->items.GetSize();
      if (st->selidx >= pos) st->selidx++;
      st->items.Insert(pos, rec);
      InvalidateRect(hwnd, NULL, FALSE);
      return pos;
    }

    case CB_DELETESTRING: {
      if (!st) return CB_ERR;
      int pos = (int)wParam;
      if (pos < 0 || pos >= st->items.GetSize()) return CB_ERR;
      st->items.Delete(pos, true);
      if (pos == st->selidx || st->selidx >= st->items.GetSize())
      {
        st->selidx = -1;
        hwnd->m_title.Set("");
      }
      else if (pos < st->selidx) st->selidx--;
      InvalidateRect(hwnd, NULL, FALSE);
      return st->items.GetSize();
    }

    case CB_RESETCONTENT:
      if (st) { st->items.Empty(true); st->selidx = -1; InvalidateRect(hwnd, NULL, FALSE); }
      return 0;

    case CB_GETCOUNT:
      return st ? st->items.GetSize() : 0;

    case CB_GETCURSEL:
      return st ? st->selidx : CB_ERR;

    case CB_SETCURSEL: {
      if (!st) return CB_ERR;
      int idx = (int)wParam;
      if (idx >= st->items.GetSize()) return CB_ERR;
      st->selidx = idx;
      if (idx >= 0 && idx < st->items.GetSize())
        hwnd->m_title.Set(st->items.Get(idx)->desc);
      else
        hwnd->m_title.Set("");
      InvalidateRect(hwnd, NULL, FALSE);
      return idx;
    }

    case CB_GETLBTEXT: {
      if (!st || !lParam) return CB_ERR;
      int idx = (int)wParam;
      if (idx < 0 || idx >= st->items.GetSize()) return CB_ERR;
      const char *s = st->items.Get(idx)->desc;
      strcpy((char *)lParam, s ? s : "");
      return (LRESULT)strlen((char *)lParam);
    }

    case CB_GETLBTEXTLEN: {
      if (!st) return CB_ERR;
      int idx = (int)wParam;
      if (idx < 0 || idx >= st->items.GetSize()) return CB_ERR;
      return st->items.Get(idx)->desc ? (int)strlen(st->items.Get(idx)->desc) : 0;
    }

    case CB_FINDSTRING:
    case CB_FINDSTRINGEXACT: {
      if (!st) return CB_ERR;
      int start = (int)wParam;
      const char *s = (const char *)lParam;
      if (!s) return CB_ERR;
      int n = st->items.GetSize();
      for (int i = 0; i < n; i++) {
        int idx = (start + 1 + i) % n;
        const char *d = st->items.Get(idx)->desc;
        if (!d) continue;
        bool match = (msg == CB_FINDSTRINGEXACT) ? !strcasecmp(d, s)
                                                  : !strncasecmp(d, s, strlen(s));
        if (match) return idx;
      }
      return CB_ERR;
    }

    case CB_GETITEMDATA: {
      if (!st) return CB_ERR;
      int idx = (int)wParam;
      if (idx < 0 || idx >= st->items.GetSize()) return CB_ERR;
      return (LRESULT)st->items.Get(idx)->parm;
    }

    case CB_SETITEMDATA: {
      if (!st) return CB_ERR;
      int idx = (int)wParam;
      if (idx < 0 || idx >= st->items.GetSize()) return CB_ERR;
      st->items.Get(idx)->parm = (LPARAM)lParam;
      return (LRESULT)lParam;
    }

    case CB_INITSTORAGE:
      return 0;

    case WM_GETTEXT:
      if (lParam && wParam > 0) {
        lstrcpyn((char *)lParam, hwnd->m_title.Get(), (int)wParam);
        return (LRESULT)strlen((char *)lParam);
      }
      return 0;

    case WM_SETTEXT: {
      hwnd->m_title.Set((const char *)lParam);
      // try to sync selidx
      if (st) {
        const char *s = hwnd->m_title.Get();
        st->selidx = -1;
        for (int i = 0; i < st->items.GetSize(); i++) {
          if (st->items.Get(i)->desc && !strcmp(st->items.Get(i)->desc, s)) {
            st->selidx = i; break;
          }
        }
      }
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }

    case WM_ERASEBKGND:
      return 1;

    case WM_LBUTTONDOWN:
      if (!st) return 0;
      SetFocus(hwnd);
      {
        RECT cr; GetClientRect(hwnd, &cr);
        int mx = GET_X_LPARAM(lParam);
        int btnW = SWELL_UI_SCALE(16);
        bool inButton = (mx >= cr.right - btnW);
        // CBS_DROPDOWNLIST: whole control is clickable. Editable: only button area opens dropdown.
        if ((hwnd->m_style & CBS_DROPDOWNLIST) || inButton) {
          st->dropdown_armed = true;
          SetCapture(hwnd);
          InvalidateRect(hwnd, NULL, FALSE);
        }
      }
      return 0;

    case WM_LBUTTONUP:
      if (st && st->dropdown_armed) {
        st->dropdown_armed = false;
        if (GetCapture() == hwnd) ReleaseCapture();
        InvalidateRect(hwnd, NULL, FALSE);
        combo_show_dropdown(hwnd, st);
      }
      return 0;

    case WM_MOUSEWHEEL:
      if (st) {
        int delta = (short)HIWORD(wParam);
        int n = st->items.GetSize();
        if (n > 0) {
          if (delta > 0 && st->selidx > 0) st->selidx--;
          else if (delta < 0 && st->selidx + 1 < n) st->selidx++;
          __SWELL_ComboBoxInternalState_rec *rec = st->items.Get(st->selidx);
          hwnd->m_title.Set(rec && rec->desc ? rec->desc : "");
          InvalidateRect(hwnd, NULL, FALSE);
          notify_parent(hwnd, CBN_SELCHANGE);
        }
      }
      return 0;

    case WM_CHAR:
      // Only editable combos (not CBS_DROPDOWNLIST) accept typed text
      if (st && (hwnd->m_style & 0x0F) != CBS_DROPDOWNLIST) {
        // Editable combo: set title to typed character
        char s[2] = { (char)wParam, 0 };
        hwnd->m_title.Set(s);
        st->selidx = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        notify_parent(hwnd, CBN_EDITCHANGE);
      }
      return 0;

    case WM_CAPTURECHANGED:
      if (st) st->dropdown_armed = false;
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;

    case WM_KEYDOWN:
      if (wParam == VK_DOWN || wParam == VK_SPACE) {
        combo_show_dropdown(hwnd, st);
        return 0;
      }
      return 0;

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      if (!hdc) return 0;

      const swell_theme &th = g_swell_theme;
      RECT cr; GetClientRect(hwnd, &cr);
      bool focused = (GetFocus() == hwnd);
      int btnw = th.button_min_h;  // square dropdown affordance
      int cw = cr.right - cr.left;
      if (btnw > cw / 2) btnw = cw / 2;

      // Rounded card body
      COLORREF bordercol = focused ? (COLORREF)th.accent
                                   : (COLORREF)th.border_strong;
      int bw = focused ? th.focus_ring_width : th.border_width;
      HPEN pen   = CreatePen(PS_SOLID, bw, bordercol);
      HBRUSH br  = CreateSolidBrush((COLORREF)th.bg_input);
      HGDIOBJ op = SelectObject(hdc, pen);
      HGDIOBJ ob = SelectObject(hdc, br);
      const int r = th.corner_radius;
      RoundRect(hdc, cr.left, cr.top, cr.right, cr.bottom, r*2, r*2);
      SelectObject(hdc, op); DeleteObject(pen);
      SelectObject(hdc, ob); DeleteObject(br);

      const int sep_x = cr.right - btnw;
      if (hdc->canvas) {
        SkPaint arrow;
        arrow.setAntiAlias(true);
        arrow.setColor(controls_to_sk((COLORREF)th.fg_text_dim));
        arrow.setStyle(SkPaint::kStroke_Style);
        arrow.setStrokeCap(SkPaint::kRound_Cap);
        arrow.setStrokeJoin(SkPaint::kRound_Join);
        arrow.setStrokeWidth((float)scaled_px(1));

        const float ax = (float)(sep_x + btnw / 2);
        const float ay = ((float)cr.top + (float)cr.bottom) * 0.5f + 0.5f;
        const float hw = (float)scaled_px(3);
        const float dy = (float)scaled_px(2);
        SkPath chevron;
        chevron.moveTo(ax - hw, ay - dy * 0.5f);
        chevron.lineTo(ax,      ay + dy);
        chevron.lineTo(ax + hw, ay - dy * 0.5f);
        hdc->canvas->drawPath(chevron, arrow);
      }

      // Text
      HFONT f = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
      SelectObject(hdc, f);
      SetTextColor(hdc, (COLORREF)th.fg_text);
      SetBkMode(hdc, TRANSPARENT);
      RECT tr = { cr.left + th.padding_edit_h, cr.top,
                  sep_x - scaled_px(4), cr.bottom };
      SWELL_DrawText(hdc, hwnd->m_title.Get(), -1, &tr,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

      EndPaint(hwnd, &ps);
      return 0;
    }

    default:
      return DefWindowProc(hwnd, msg, wParam, lParam);
  }
}

// ===========================================================================
// 7. tabControlWindowProc
// ===========================================================================

static void draw_active_tab_outline(HDC hdc, const RECT &cr,
                                    const RECT &selR, int by,
                                    int radius, int inner_radius,
                                    int stroke_width, COLORREF color)
{
  if (!hdc || !hdc->canvas || selR.left >= selR.right) return;

  const float sw = (float)(stroke_width > 0 ? stroke_width : 1);
  const float hs = sw * 0.5f;
  const float l = (float)cr.left + hs;
  const float rr = (float)cr.right - 1.0f - hs;
  const float b = (float)cr.bottom - 1.0f - hs;
  const float y = (float)by + hs;
  const float tl = (float)selR.left + hs;
  const float tr = (float)selR.right - hs;
  const float tt = (float)selR.top + hs;
  float r = (float)radius;
  float ir = (float)inner_radius;

  if (r < 0.0f) r = 0.0f;
  if (ir < 0.0f) ir = 0.0f;
  const float tabw = tr - tl;
  const float tabh = y - tt;
  const float pagew = rr - l;
  const float pageh = b - y;
  if (tabw <= 0.0f || pagew <= 0.0f) return;
  if (r > tabw * 0.5f) r = tabw * 0.5f;
  if (r > tabh) r = tabh;
  if (pageh <= 0.0f) r = 0.0f;
  else if (r > pageh) r = pageh;
  if (ir > tabh) ir = tabh;
  if (ir > tabw * 0.25f) ir = tabw * 0.25f;

  SkPath p;
  p.moveTo(l, y);
  p.lineTo(tl - ir, y);
  if (ir > 0.0f) p.quadTo(tl, y, tl, y - ir);
  p.lineTo(tl, tt + r);
  if (r > 0.0f) p.quadTo(tl, tt, tl + r, tt);
  p.lineTo(tr - r, tt);
  if (r > 0.0f) p.quadTo(tr, tt, tr, tt + r);
  p.lineTo(tr, y - ir);
  if (ir > 0.0f) p.quadTo(tr, y, tr + ir, y);
  p.lineTo(rr, y);

  if (pageh > 0.0f) {
    p.lineTo(rr, b - r);
    if (r > 0.0f) p.quadTo(rr, b, rr - r, b);
    p.lineTo(l + r, b);
    if (r > 0.0f) p.quadTo(l, b, l, b - r);
    p.lineTo(l, y);
  }

  SkPaint paint;
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(sw);
  paint.setStrokeJoin(SkPaint::kRound_Join);
  paint.setStrokeCap(SkPaint::kButt_Cap);
  paint.setAntiAlias(true);
  paint.setColor(SWELL_TO_SKCOLOR(color, 255));
  hdc->canvas->drawPath(p, paint);
  swell_DirtyContext(hdc, cr.left, cr.top, cr.right, cr.bottom);
}

static void draw_inactive_tab_outline(HDC hdc, const RECT &tr,
                                      int by, int radius,
                                      int stroke_width, COLORREF color)
{
  if (!hdc || !hdc->canvas || tr.left >= tr.right) return;

  const float sw = (float)(stroke_width > 0 ? stroke_width : 1);
  const float hs = sw * 0.5f;
  const float l = (float)tr.left + hs;
  const float rr = (float)tr.right - hs;
  const float t = (float)tr.top + hs;
  const float b = (float)by + hs;
  float r = (float)radius;
  if (r < 0.0f) r = 0.0f;
  const float w = rr - l;
  const float h = b - t;
  if (w <= 0.0f || h <= 0.0f) return;
  if (r > w * 0.5f) r = w * 0.5f;
  if (r > h) r = h;

  SkPath p;
  p.moveTo(l, b);
  p.lineTo(l, t + r);
  if (r > 0.0f) p.quadTo(l, t, l + r, t);
  p.lineTo(rr - r, t);
  if (r > 0.0f) p.quadTo(rr, t, rr, t + r);
  p.lineTo(rr, b);

  SkPaint paint;
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(sw);
  paint.setStrokeJoin(SkPaint::kRound_Join);
  paint.setStrokeCap(SkPaint::kButt_Cap);
  paint.setAntiAlias(true);
  paint.setColor(SWELL_TO_SKCOLOR(color, 255));
  hdc->canvas->drawPath(p, paint);
  swell_DirtyContext(hdc, tr.left, tr.top, tr.right, by);
}

LRESULT tabControlWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  tabControlState *st = (tabControlState *)(void *)hwnd->m_private_data;

  switch (msg) {
    case WM_CREATE:
      st = new tabControlState();
      hwnd->m_private_data = (INT_PTR)st;
      hwnd->m_wantfocus = true;
      return 0;

    case WM_NCDESTROY:
      if (st) {
        for (int i = 0; i < st->m_tabs.GetSize(); i++) free(st->m_tabs.Get(i));
        delete st;
        hwnd->m_private_data = 0;
      }
      return 0;

    case TCM_INSERTITEM: {
      if (!st || !lParam) return -1;
      int pos = (int)wParam;
      const TCITEM *item = (const TCITEM *)lParam;
      const char *text = (item->mask & TCIF_TEXT && item->pszText) ? item->pszText : "";
      int n = st->m_tabs.GetSize();
      if (pos < 0 || pos > n) pos = n;
      st->m_tabs.Insert(pos, strdup(text));
      if (st->m_curtab < 0) st->m_curtab = 0;
      InvalidateRect(hwnd, NULL, FALSE);
      return pos;
    }

    case TCM_DELETEITEM: {
      if (!st) return FALSE;
      int idx = (int)wParam;
      if (idx < 0 || idx >= st->m_tabs.GetSize()) return FALSE;
      free(st->m_tabs.Get(idx));
      st->m_tabs.Delete(idx);
      if (st->m_curtab >= st->m_tabs.GetSize()) st->m_curtab = st->m_tabs.GetSize() - 1;
      InvalidateRect(hwnd, NULL, FALSE);
      return TRUE;
    }

    case TCM_GETCURSEL:
      return st ? st->m_curtab : -1;

    case TCM_SETCURSEL: {
      if (!st) return -1;
      int old = st->m_curtab;
      int nt = (int)wParam;
      int n = st->m_tabs.GetSize();
      if (nt < 0) nt = 0;
      if (nt >= n) nt = n - 1;
      st->m_curtab = nt;
      InvalidateRect(hwnd, NULL, FALSE);
      // notify parent
      {
        NMHDR nm = { hwnd, (UINT_PTR)hwnd->m_id, TCN_SELCHANGE };
        HWND par = GetParent(hwnd);
        if (par) SendMessage(par, WM_NOTIFY, hwnd->m_id, (LPARAM)&nm);
      }
      return old;
    }

    case TCM_GETITEMCOUNT:
      return st ? st->m_tabs.GetSize() : 0;

    case TCM_ADJUSTRECT: {
      if (!lParam) return 0;
      RECT *r = (RECT *)lParam;
      int th = g_swell_theme.tab_height;
      if (wParam) { r->top -= th; } // make larger
      else        { r->top += th; } // subtract tab bar
      return 0;
    }

    case WM_KEYDOWN: {
      if (!st) return 0;
      if (st->m_tabs.GetSize() <= 1) return 0;
      if (wParam == VK_LEFT) {
        int sel = st->m_curtab - 1;
        if (sel < 0) sel = st->m_tabs.GetSize() - 1;
        SendMessage(hwnd, TCM_SETCURSEL, sel, 0);
        return 0;
      }
      if (wParam == VK_RIGHT) {
        int sel = st->m_curtab + 1;
        if (sel >= st->m_tabs.GetSize()) sel = 0;
        SendMessage(hwnd, TCM_SETCURSEL, sel, 0);
        return 0;
      }
      return 0;
    }

    case WM_LBUTTONDOWN: {
      if (!st) return 0;
      const swell_theme &thm = g_swell_theme;
      int mx = GET_X_LPARAM(lParam);
      int tabH = thm.tab_height;
      RECT cr; GetClientRect(hwnd, &cr);
      if (GET_Y_LPARAM(lParam) > tabH) return 0;
      // hit test tabs: must match paint geometry exactly
      HDC hdc = GetDC(hwnd);
      HFONT f = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
      SelectObject(hdc, f);
      TEXTMETRIC tm2; GetTextMetrics(hdc, &tm2);
      int avgcw = tm2.tmAveCharWidth > 0 ? tm2.tmAveCharWidth : 8;
      const int tab_gap = scaled_px(2);
      int x = cr.left + thm.padding_button_h;
      for (int i = 0; i < st->m_tabs.GetSize(); i++) {
        const char *s = st->m_tabs.Get(i);
        int tw = (int)strlen(s) * avgcw + thm.padding_button_h * 2;
        if (mx >= x && mx < x + tw) {
          ReleaseDC(hwnd, hdc);
          SendMessage(hwnd, TCM_SETCURSEL, i, 0);
          return 0;
        }
        x += tw + tab_gap;
      }
      ReleaseDC(hwnd, hdc);
      return 0;
    }

    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      if (!hdc || !st) { if (hdc) EndPaint(hwnd, &ps); return 0; }

      const swell_theme &thm = g_swell_theme;
      RECT cr; GetClientRect(hwnd, &cr);
      int tabH = thm.tab_height;

      // Bar background
      fill_bg(hwnd, hdc, WM_CTLCOLORSTATIC, thm.bg_window);

      HFONT f = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
      SelectObject(hdc, f);
      SetBkMode(hdc, TRANSPARENT);

      TEXTMETRIC tmtab; GetTextMetrics(hdc, &tmtab);
      int avgcwp = tmtab.tmAveCharWidth > 0 ? tmtab.tmAveCharWidth : 8;
      const int r = thm.corner_radius;
      const int bw = thm.border_width > 0 ? thm.border_width : 1;
      // Baseline where tab strip meets content area.
      const int by = cr.top + tabH;
      if (cr.bottom > by + 1) {
        HBRUSH pageBr = CreateSolidBrush((COLORREF)thm.bg_tab_active);
        HPEN np = (HPEN)GetStockObject(NULL_PEN);
        HGDIOBJ op = SelectObject(hdc, np);
        HGDIOBJ ob = SelectObject(hdc, pageBr);
        SWELL_DrawRoundRectEx(hdc, cr.left, by, cr.right, cr.bottom,
                              0, 0, r, r);
        SelectObject(hdc, op);
        SelectObject(hdc, ob); DeleteObject(pageBr);
      }

      int x = cr.left + thm.padding_button_h;
      const int tab_gap = scaled_px(2);
      const int tab_top_inset = scaled_px(5);
      RECT selR = { 0, 0, 0, 0 };
      for (int i = 0; i < st->m_tabs.GetSize(); i++) {
        const char *s = st->m_tabs.Get(i);
        int tw = (int)strlen(s) * avgcwp + thm.padding_button_h * 2;
        bool sel = (i == st->m_curtab);

        // All tabs same size; selected tab has no bottom border.
        RECT tr = { x, cr.top + tab_top_inset, x + tw, by };

        HBRUSH fillBr = CreateSolidBrush(sel ? (COLORREF)thm.bg_tab_active
                                              : (COLORREF)thm.bg_tab);
        HPEN borderPen = (HPEN)GetStockObject(NULL_PEN);
        HGDIOBJ ob = SelectObject(hdc, fillBr);
        HGDIOBJ op = SelectObject(hdc, borderPen);
        SWELL_DrawRoundRectEx(hdc, tr.left, tr.top, tr.right, by,
                              r, r, 0, 0);
        SelectObject(hdc, op);
        SelectObject(hdc, ob); DeleteObject(fillBr);

        if (!sel)
          draw_inactive_tab_outline(hdc, tr, by, r, bw,
                                    (COLORREF)thm.border);

        SetTextColor(hdc, sel ? (COLORREF)thm.fg_text
                               : (COLORREF)thm.fg_text_dim);
        SWELL_DrawText(hdc, s, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        if (sel) selR = tr;
        x += tw + tab_gap;
      }

      if (selR.left < selR.right) {
        const int min_ir = scaled_px(2);
        const int ir = r > min_ir ? r / 2 : min_ir;
        draw_active_tab_outline(hdc, cr, selR, by, r, ir, bw,
                                (COLORREF)thm.border_strong);
      } else {
        HPEN bp = CreatePen(PS_SOLID, bw, (COLORREF)thm.border);
        HGDIOBJ obp = SelectObject(hdc, bp);
        MoveToEx(hdc, cr.left, by, NULL);
        LineTo(hdc, cr.right, by);
        SelectObject(hdc, obp); DeleteObject(bp);
      }

      EndPaint(hwnd, &ps);
      return 0;
    }

    case WM_SIZE:
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;

    default:
      return DefWindowProc(hwnd, msg, wParam, lParam);
  }
}

// ===========================================================================
// 8. trackbarWindowProc
// ===========================================================================

LRESULT trackbarWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  // private data: {pos, min, max, drag_offs}
  int *p = (int *)(void *)hwnd->m_private_data;

  switch (msg) {
    case WM_CREATE:
      p = (int *)calloc(4, sizeof(int));
      p[0] = 0; p[1] = 0; p[2] = 10;
      hwnd->m_private_data = (INT_PTR)p;
      hwnd->m_wantfocus = true;
      return 0;

    case WM_NCDESTROY:
      free(p);
      hwnd->m_private_data = 0;
      return 0;

    case TBM_GETPOS:  return p ? p[0] : 0;
    case TBM_SETPOS:
      if (p) { p[0] = (int)lParam; if (p[0]<p[1]) p[0]=p[1]; if (p[0]>p[2]) p[0]=p[2]; InvalidateRect(hwnd,NULL,FALSE); }
      return 0;
    case TBM_SETRANGE:
      if (p) { p[1] = LOWORD(lParam); p[2] = HIWORD(lParam); if (p[0]<p[1]) p[0]=p[1]; if (p[0]>p[2]) p[0]=p[2]; InvalidateRect(hwnd,NULL,FALSE); }
      return 0;
    case TBM_SETTIC:
    case TBM_SETSEL:
      return 0;

    case WM_LBUTTONDOWN: {
      if (!p) return 0;
      SetCapture(hwnd);
      RECT cr; GetClientRect(hwnd, &cr);
      int mx = GET_X_LPARAM(lParam);
      int range = p[2] - p[1];
      int tw = cr.right - cr.left;
      // Snapshot thumb position for drag offset
      if (tw > 0 && range > 0) {
        int oldp = p[0];
        p[0] = p[1] + (int)((float)mx / tw * range + 0.5f);
        if (p[0] < p[1]) p[0] = p[1]; if (p[0] > p[2]) p[0] = p[2];
        // Drag offset: thumb pixel position from click pixel position
        float frac = (float)(p[0] - p[1]) / range;
        int thumb_x = cr.left + (int)(frac * tw);
        p[3] = mx - thumb_x;
      }
      InvalidateRect(hwnd, NULL, FALSE);
      HWND par = GetParent(hwnd);
      if (par) SendMessage(par, WM_HSCROLL, MAKEWPARAM(SB_THUMBTRACK, (WORD)p[0]), (LPARAM)hwnd);
      return 0;
    }

    case WM_MOUSEMOVE: {
      if (!p || GetCapture() != hwnd) return 0;
      RECT cr; GetClientRect(hwnd, &cr);
      int mx = GET_X_LPARAM(lParam) - p[3]; // apply drag offset
      int range = p[2] - p[1];
      int tw = cr.right - cr.left;
      if (tw > 0 && range > 0)
        p[0] = p[1] + (int)((float)mx / tw * range + 0.5f);
      if (p[0] < p[1]) p[0] = p[1]; if (p[0] > p[2]) p[0] = p[2];
      InvalidateRect(hwnd, NULL, FALSE);
      HWND par = GetParent(hwnd);
      if (par) SendMessage(par, WM_HSCROLL, MAKEWPARAM(SB_THUMBTRACK, (WORD)p[0]), (LPARAM)hwnd);
      return 0;
    }

    case WM_LBUTTONDBLCLK:
      if (p) {
        // Snap to nearest tick position (use range as coarse snap)
        p[3] = 0; // reset any pending drag offset
        InvalidateRect(hwnd, NULL, FALSE);
      }
      return 0;

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;

    case WM_LBUTTONUP:
      if (GetCapture() == hwnd) {
        ReleaseCapture();
        HWND par = GetParent(hwnd);
        if (par && p) SendMessage(par, WM_HSCROLL, MAKEWPARAM(SB_ENDSCROLL, (WORD)p[0]), (LPARAM)hwnd);
      }
      return 0;

    case WM_KEYDOWN: {
      if (!p) return 0;
      int newpos = p[0];
      if (wParam == VK_LEFT || wParam == VK_DOWN) newpos--;
      else if (wParam == VK_RIGHT || wParam == VK_UP) newpos++;
      else if (wParam == VK_PRIOR) newpos -= 10;
      else if (wParam == VK_NEXT) newpos += 10;
      else return 0;
      if (newpos < p[1]) newpos = p[1]; if (newpos > p[2]) newpos = p[2];
      if (newpos != p[0]) {
        p[0] = newpos;
        InvalidateRect(hwnd, NULL, FALSE);
        HWND par = GetParent(hwnd);
        if (par) SendMessage(par, WM_HSCROLL, MAKEWPARAM(SB_THUMBTRACK, (WORD)p[0]), (LPARAM)hwnd);
      }
      return 1;
    }

    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      if (!hdc) return 0;

      const swell_theme &th = g_swell_theme;
      RECT cr; GetClientRect(hwnd, &cr);

      int cy = (cr.top + cr.bottom) / 2;

      if (!p || p[2] <= p[1]) { EndPaint(hwnd, &ps); return 0; }

      const int track_h = 4;
      const int thumb_r = 7;
      const int track_left = cr.left + thumb_r;
      const int track_right = cr.right - thumb_r;

      float frac = (float)(p[0] - p[1]) / (p[2] - p[1]);
      if (frac < 0) frac = 0; if (frac > 1) frac = 1;
      int tx = track_left + (int)(frac * (track_right - track_left));

      // Track (rounded) — unfilled portion
      {
        HPEN np = (HPEN)GetStockObject(NULL_PEN);
        HBRUSH trbr = CreateSolidBrush((COLORREF)th.trackbar_track);
        HGDIOBJ op = SelectObject(hdc, np);
        HGDIOBJ ob = SelectObject(hdc, trbr);
        RoundRect(hdc, track_left, cy - track_h/2,
                  track_right, cy + track_h/2,
                  track_h, track_h);
        SelectObject(hdc, op);
        SelectObject(hdc, ob); DeleteObject(trbr);
      }

      // Filled portion (accent)
      if (tx > track_left) {
        HPEN np = (HPEN)GetStockObject(NULL_PEN);
        HBRUSH fbr = CreateSolidBrush((COLORREF)th.trackbar_fill);
        HGDIOBJ op = SelectObject(hdc, np);
        HGDIOBJ ob = SelectObject(hdc, fbr);
        RoundRect(hdc, track_left, cy - track_h/2,
                  tx, cy + track_h/2,
                  track_h, track_h);
        SelectObject(hdc, op);
        SelectObject(hdc, ob); DeleteObject(fbr);
      }

      // Thumb: solid circle with border
      {
        HPEN pen = CreatePen(PS_SOLID, th.border_width,
                             (COLORREF)th.border_strong);
        HBRUSH br = CreateSolidBrush((COLORREF)th.trackbar_thumb);
        HGDIOBJ op = SelectObject(hdc, pen);
        HGDIOBJ ob = SelectObject(hdc, br);
        Ellipse(hdc, tx - thumb_r, cy - thumb_r,
                     tx + thumb_r, cy + thumb_r);
        SelectObject(hdc, op); DeleteObject(pen);
        SelectObject(hdc, ob); DeleteObject(br);
      }

      EndPaint(hwnd, &ps);
      return 0;
    }

    default:
      return DefWindowProc(hwnd, msg, wParam, lParam);
  }
}

// ===========================================================================
// 9. progressWindowProc
// ===========================================================================

LRESULT progressWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  int *p = (int *)(void *)hwnd->m_private_data; // {pos, min, max}

  switch (msg) {
    case WM_CREATE:
      p = (int *)calloc(3, sizeof(int));
      p[0] = 0; p[1] = 0; p[2] = 100;
      hwnd->m_private_data = (INT_PTR)p;
      hwnd->m_wantfocus = false;
      return 0;

    case WM_NCDESTROY:
      free(p);
      hwnd->m_private_data = 0;
      return 0;

    case PBM_SETRANGE:
      if (p) { p[1] = LOWORD(lParam); p[2] = HIWORD(lParam); InvalidateRect(hwnd,NULL,FALSE); }
      return 0;

    case PBM_SETPOS: {
      if (!p) return 0;
      int old = p[0];
      p[0] = (int)wParam;
      InvalidateRect(hwnd, NULL, FALSE);
      return old;
    }

    case PBM_DELTAPOS: {
      if (!p) return 0;
      int prev = p[0];
      p[0] += (int)wParam;
      InvalidateRect(hwnd, NULL, FALSE);
      return prev;
    }

    case WM_ERASEBKGND:
      return 1;

    case WM_SIZE:
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      if (!hdc) return 0;

      const swell_theme &th = g_swell_theme;
      RECT cr; GetClientRect(hwnd, &cr);

      const int r = (cr.bottom - cr.top) / 2;

      // Track (rounded pill)
      {
        HPEN np = (HPEN)GetStockObject(NULL_PEN);
        HBRUSH trbr = CreateSolidBrush((COLORREF)th.progress_track);
        HGDIOBJ op = SelectObject(hdc, np);
        HGDIOBJ ob = SelectObject(hdc, trbr);
        RoundRect(hdc, cr.left, cr.top, cr.right, cr.bottom, r*2, r*2);
        SelectObject(hdc, op);
        SelectObject(hdc, ob); DeleteObject(trbr);
      }

      if (p && p[2] > p[1]) {
        float frac = (float)(p[0] - p[1]) / (p[2] - p[1]);
        if (frac < 0) frac = 0; if (frac > 1) frac = 1;
        int fw = (int)(frac * (cr.right - cr.left));
        if (fw > 0) {
          HPEN np = (HPEN)GetStockObject(NULL_PEN);
          HBRUSH fb = CreateSolidBrush((COLORREF)th.progress_fill);
          HGDIOBJ op = SelectObject(hdc, np);
          HGDIOBJ ob = SelectObject(hdc, fb);
          RoundRect(hdc, cr.left, cr.top, cr.left + fw, cr.bottom, r*2, r*2);
          SelectObject(hdc, op);
          SelectObject(hdc, ob); DeleteObject(fb);
        }
      }

      EndPaint(hwnd, &ps);
      return 0;
    }

    default:
      return DefWindowProc(hwnd, msg, wParam, lParam);
  }
}
