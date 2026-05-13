/*
  SWELL2 built-in control WNDPROCs.
  Button, Edit, Static, ListBox/ListView, TreeView, ComboBox,
  TabControl, Trackbar, ProgressBar.
*/

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "swell-internal.h"
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
#define TVM_ENSUREVISIBLE  (TVM_FIRST+20)
#define TVM_SETBKCOLOR     (TVM_FIRST+29)
#define TVM_SETTEXTCOLOR   (TVM_FIRST+30)
// SWELL2-specific tree navigation (no wParam packing needed)
#define TVM_DELETEALLITEMS (TVM_FIRST+60)
#define TVM_GETSELECTION   (TVM_FIRST+61)
#define TVM_GETPARENT      (TVM_FIRST+62)
#define TVM_GETCHILD       (TVM_FIRST+63)
#define TVM_GETNEXTSIBLING (TVM_FIRST+64)
#define TVM_GETROOT        (TVM_FIRST+65)

#ifndef TVC_BYMOUSE
#define TVC_BYMOUSE 0x0002
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
  }
  return NULL;
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

    case BM_SETCHECK:
      if (st) st->state = (int)wParam;
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;

    case BM_GETCHECK:
      return st ? st->state : 0;

    case BM_SETIMAGE:
      if (st) {
        st->bitmap_mode = (int)wParam;
        st->bitmap = (HICON)lParam;
        InvalidateRect(hwnd, NULL, FALSE);
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
          // uncheck sibling radio buttons in same group
          HWND par = GetParent(hwnd);
          if (par) {
            int n = par->m_children.GetSize();
            bool past_group_start = false;
            for (int i = 0; i < n; i++) {
              HWND sib = par->m_children.Get(i);
              if (!sib || sib == hwnd) continue;
              if (sib->m_style & WS_GROUP) past_group_start = true;
              if (past_group_start) break;
              if ((sib->m_style & 0xFF) == BS_AUTORADIOBUTTON) {
                buttonWindowState *ss = (buttonWindowState *)(void *)sib->m_private_data;
                if (ss) { ss->state = BST_UNCHECKED; InvalidateRect(sib, NULL, FALSE); }
              }
            }
          }
        }
        InvalidateRect(hwnd, NULL, FALSE);
        notify_parent(hwnd, BN_CLICKED);
      }
      return 0;
    }

    case WM_KEYDOWN:
      if ((wParam == VK_SPACE || wParam == VK_RETURN) && st) {
        DWORD style = hwnd->m_style & 0xFF;
        if (style == BS_PUSHBUTTON || style == BS_DEFPUSHBUTTON) {
          notify_parent(hwnd, BN_CLICKED);
        }
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

      RECT cr; GetClientRect(hwnd, &cr);
      DWORD style = hwnd->m_style;
      DWORD bstyle = style & 0x0F; // bottom nibble = Win32 button type
      bool enabled = IsWindowEnabled(hwnd);
      bool focused = (GetFocus() == hwnd);
      bool pressed = (GetCapture() == hwnd);

      if (style & BS_GROUPBOX) {
        // Draw a named border
        HBRUSH bg = CreateSolidBrush((COLORREF)g_swell_ctheme._3dface);
        FillRect(hdc, &cr, bg);
        DeleteObject(bg);

        HPEN pen = CreatePen(PS_SOLID, 1, (COLORREF)g_swell_ctheme._3dshadow);
        HGDIOBJ oldpen = SelectObject(hdc, pen);
        // Box: leave gap at top for title text
        HFONT fnt = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
        HGDIOBJ oldfont = SelectObject(hdc, fnt);
        TEXTMETRIC tm; GetTextMetrics(hdc, &tm);
        int titlegap = tm.tmHeight / 2;
        SelectObject(hdc, oldfont);

        // draw border rect skipping top-left where text goes
        MoveToEx(hdc, cr.left + 8, cr.top + titlegap, NULL);
        LineTo(hdc, cr.left, cr.top + titlegap);
        LineTo(hdc, cr.left, cr.bottom - 1);
        LineTo(hdc, cr.right - 1, cr.bottom - 1);
        LineTo(hdc, cr.right - 1, cr.top + titlegap);
        LineTo(hdc, cr.left + 8, cr.top + titlegap);

        SelectObject(hdc, oldpen);
        DeleteObject(pen);

        // draw title
        if (hwnd->m_title.GetLength() > 0) {
          HFONT f = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
          SelectObject(hdc, f);
          SetTextColor(hdc, enabled ? (COLORREF)g_swell_ctheme.group_text
                                    : (COLORREF)g_swell_ctheme.button_text_disabled);
          SetBkMode(hdc, TRANSPARENT);
          RECT tr = { cr.left + 8, 0, cr.right - 8, titlegap * 2 };
          SWELL_DrawText(hdc, hwnd->m_title.Get(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        EndPaint(hwnd, &ps);
        return 0;
      }

      // Push button / check box / radio button
      bool is_check = (bstyle == BS_AUTOCHECKBOX || bstyle == BS_AUTO3STATE ||
                       bstyle == BS_CHECKBOX || bstyle == BS_3STATE);
      bool is_radio = (bstyle == BS_AUTORADIOBUTTON || bstyle == BS_RADIOBUTTON);

      COLORREF bgcol = (COLORREF)g_swell_ctheme.button_bg;
      COLORREF fgcol = enabled ? (COLORREF)g_swell_ctheme.button_text
                                : (COLORREF)g_swell_ctheme.button_text_disabled;

      if (is_check || is_radio) {
        // Background
        HBRUSH bg = CreateSolidBrush((COLORREF)g_swell_ctheme._3dface);
        FillRect(hdc, &cr, bg);
        DeleteObject(bg);

        int box_size = cr.bottom - cr.top - 4;
        if (box_size > 14) box_size = 14;
        int box_x = cr.left + 2;
        int box_y = cr.top + (cr.bottom - cr.top - box_size) / 2;
        RECT box = { box_x, box_y, box_x + box_size, box_y + box_size };

        HPEN pen = CreatePen(PS_SOLID, 1, (COLORREF)g_swell_ctheme._3dshadow);
        HGDIOBJ oldpen = SelectObject(hdc, pen);
        HBRUSH boxbr = CreateSolidBrush((COLORREF)g_swell_ctheme.checkbox_bg);
        HGDIOBJ oldbr = SelectObject(hdc, boxbr);

        if (is_radio) {
          Ellipse(hdc, box.left, box.top, box.right, box.bottom);
        } else {
          Rectangle(hdc, box.left, box.top, box.right, box.bottom);
        }

        SelectObject(hdc, oldpen); DeleteObject(pen);
        SelectObject(hdc, oldbr);  DeleteObject(boxbr);

        // check mark
        int cstate = st ? st->state : 0;
        if (cstate != BST_UNCHECKED) {
          HPEN cpen = CreatePen(PS_SOLID, 2,
              enabled ? (COLORREF)g_swell_ctheme.checkbox_text
                      : (COLORREF)g_swell_ctheme.checkbox_text_disabled);
          HGDIOBJ op = SelectObject(hdc, cpen);

          if (is_radio) {
            // filled circle
            HBRUSH fb = CreateSolidBrush(
                enabled ? (COLORREF)g_swell_ctheme.checkbox_text
                        : (COLORREF)g_swell_ctheme.checkbox_text_disabled);
            HGDIOBJ ob = SelectObject(hdc, fb);
            int m = 3;
            Ellipse(hdc, box.left+m, box.top+m, box.right-m, box.bottom-m);
            SelectObject(hdc, ob); DeleteObject(fb);
          } else if (cstate == BST_INDETERMINATE) {
            // filled square
            RECT ir = { box.left+3, box.top+3, box.right-3, box.bottom-3 };
            HBRUSH fb = CreateSolidBrush(
                enabled ? (COLORREF)g_swell_ctheme.checkbox_text
                        : (COLORREF)g_swell_ctheme.checkbox_text_disabled);
            FillRect(hdc, &ir, fb);
            DeleteObject(fb);
          } else {
            // checkmark: two lines
            MoveToEx(hdc, box.left+2, box.top + box_size/2, NULL);
            LineTo(hdc, box.left + box_size/3, box.bottom - 3);
            LineTo(hdc, box.right - 2, box.top + 2);
          }

          SelectObject(hdc, op); DeleteObject(cpen);
        }

        // label
        HFONT f = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
        SelectObject(hdc, f);
        SetTextColor(hdc, fgcol);
        SetBkMode(hdc, TRANSPARENT);
        RECT tr = { box.right + 4, cr.top, cr.right, cr.bottom };
        SWELL_DrawText(hdc, hwnd->m_title.Get(), -1, &tr,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      } else {
        // Push button
        bool isdef = (style & BS_DEFPUSHBUTTON) != 0;

        HBRUSH bg = CreateSolidBrush(pressed ? (COLORREF)g_swell_ctheme.button_shadow
                                             : bgcol);
        FillRect(hdc, &cr, bg);
        DeleteObject(bg);

        HPEN pen = CreatePen(PS_SOLID, isdef ? 2 : 1,
                             (COLORREF)g_swell_ctheme.button_shadow);
        HGDIOBJ op = SelectObject(hdc, pen);
        HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
        HGDIOBJ ob = SelectObject(hdc, nb);
        Rectangle(hdc, cr.left, cr.top, cr.right, cr.bottom);
        SelectObject(hdc, op); DeleteObject(pen);
        SelectObject(hdc, ob);

        HFONT f = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
        SelectObject(hdc, f);
        SetTextColor(hdc, fgcol);
        SetBkMode(hdc, TRANSPARENT);
        SWELL_DrawText(hdc, hwnd->m_title.Get(), -1, &cr,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        if (focused) {
          RECT fr = { cr.left+2, cr.top+2, cr.right-2, cr.bottom-2 };
          HPEN fp = CreatePen(PS_SOLID, 1, (COLORREF)g_swell_ctheme.focusrect);
          HGDIOBJ ofp = SelectObject(hdc, fp);
          HGDIOBJ ofb = SelectObject(hdc, GetStockObject(NULL_BRUSH));
          Rectangle(hdc, fr.left, fr.top, fr.right, fr.bottom);
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
      hwnd->m_title.Set(s ? s : "");
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
      int s1 = st ? (st->sel1 < 0 ? st->cursor_pos : (st->sel1 < st->sel2 ? st->sel1 : st->sel2)) : 0;
      int s2 = st ? (st->sel1 < 0 ? st->cursor_pos : (st->sel1 < st->sel2 ? st->sel2 : st->sel1)) : 0;
      if (wParam) *(int *)wParam = s1;
      if (lParam) *(int *)lParam = s2;
      return MAKELPARAM(s1, s2);
    }

    case EM_SETSEL:
      if (st) {
        st->sel1 = (int)wParam;
        st->sel2 = (int)lParam;
        if (st->sel2 < 0) st->sel2 = hwnd->m_title.GetLength();
        st->cursor_pos = st->sel2;
        InvalidateRect(hwnd, NULL, FALSE);
      }
      return 0;

    case EM_REPLACESEL: {
      const char *newtext = (const char *)lParam;
      if (!st || !newtext) return 0;
      const char *t = hwnd->m_title.Get();
      int len = (int)strlen(t);
      int s1 = st->sel1 < 0 ? st->cursor_pos : (st->sel1 < st->sel2 ? st->sel1 : st->sel2);
      int s2 = st->sel1 < 0 ? st->cursor_pos : (st->sel1 < st->sel2 ? st->sel2 : st->sel1);
      if (s1 < 0) s1 = 0; if (s1 > len) s1 = len;
      if (s2 < s1) s2 = s1; if (s2 > len) s2 = len;
      WDL_FastString ns;
      ns.Set(t, s1);
      ns.Append(newtext);
      ns.Append(t + s2);
      hwnd->m_title.Set(ns.Get());
      st->cursor_pos = s1 + (int)strlen(newtext);
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

    case WM_KEYDOWN: {
      if (!st) return 0;
      const char *t = hwnd->m_title.Get();
      int len = (int)strlen(t);
      bool shift = (lParam & FSHIFT) != 0;
      bool ctrl  = (lParam & FCONTROL) != 0;

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
        } else if (st->cursor_pos < len) {
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
        st->cursor_pos = len;
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
        if (st->cursor_pos < len) st->cursor_pos++;
        if (!shift) { st->sel1 = st->sel2 = -1; }
        InvalidateRect(hwnd, NULL, FALSE);
        return 1;
      }
      // Ctrl+A: select all
      if (ctrl && (wParam == 'A' || wParam == 'a')) {
        SendMessage(hwnd, EM_SETSEL, 0, -1);
        return 1;
      }
      return 0;
    }

    case WM_LBUTTONDOWN:
      SetFocus(hwnd);
      if (st) { st->sel1 = st->sel2 = -1; }
      return 0;

    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      if (!hdc) return 0;

      RECT cr; GetClientRect(hwnd, &cr);
      bool enabled = IsWindowEnabled(hwnd);
      bool focused = (GetFocus() == hwnd);

      // Background
      COLORREF bgcol = enabled ? (COLORREF)g_swell_ctheme.edit_bg
                                : (COLORREF)g_swell_ctheme._3dface;
      HBRUSH bg = CreateSolidBrush(bgcol);
      FillRect(hdc, &cr, bg);
      DeleteObject(bg);

      // Border
      HPEN pen = CreatePen(PS_SOLID, 1, (COLORREF)g_swell_ctheme._3dshadow);
      HGDIOBJ op = SelectObject(hdc, pen);
      HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
      Rectangle(hdc, cr.left, cr.top, cr.right, cr.bottom);
      SelectObject(hdc, op); DeleteObject(pen);
      SelectObject(hdc, ob);

      HFONT f = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
      SelectObject(hdc, f);

      const char *txt = hwnd->m_title.Get();
      bool is_pass = (hwnd->m_style & ES_PASSWORD) != 0;
      WDL_FastString disp;
      if (is_pass) {
        int n = (int)strlen(txt);
        for (int i = 0; i < n; i++) disp.Append("*", 1);
        txt = disp.Get();
      }

      RECT tr = { cr.left+2, cr.top+1, cr.right-2, cr.bottom-1 };

      // Draw normal text first (TRANSPARENT) — selection overpaints below
      if (!st || st->sel1 < 0 || st->sel1 == st->sel2)
      {
        SetTextColor(hdc, enabled ? (COLORREF)g_swell_ctheme.edit_text
                                  : (COLORREF)g_swell_ctheme.button_text_disabled);
        SetBkMode(hdc, TRANSPARENT);
        SWELL_DrawText(hdc, txt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      }
      else
      {
        int s1 = st->sel1 < st->sel2 ? st->sel1 : st->sel2;
        int s2 = st->sel1 < st->sel2 ? st->sel2 : st->sel1;
        int tlen = (int)strlen(txt);
        if (s1 > tlen) s1 = tlen;
        if (s2 > tlen) s2 = tlen;

        // 1) Draw normal text (TRANSPARENT)
        SetTextColor(hdc, enabled ? (COLORREF)g_swell_ctheme.edit_text
                                  : (COLORREF)g_swell_ctheme.button_text_disabled);
        SetBkMode(hdc, TRANSPARENT);
        SWELL_DrawText(hdc, txt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // 2) Measure pre-selection width, overpaint selection
        if (s2 > s1)
        {
          RECT measR = { 0, 0, 0, 0 };
          SWELL_DrawText(hdc, txt, s1, &measR, DT_CALCRECT | DT_LEFT | DT_SINGLELINE);
          RECT selR = tr;
          selR.left += measR.right;
          SetBkMode(hdc, OPAQUE);
          SetBkColor(hdc, (COLORREF)g_swell_ctheme.edit_bg_sel);
          SetTextColor(hdc, (COLORREF)g_swell_ctheme.edit_text_sel);
          SWELL_DrawText(hdc, txt + s1, s2 - s1, &selR, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
      }

      // cursor
      if (st && st->cursor_state && focused) {
        // approximate cursor x
        TEXTMETRIC tm; GetTextMetrics(hdc, &tm);
        int cpos = st->cursor_pos;
        int tlen = (int)strlen(txt);
        if (cpos > tlen) cpos = tlen;
        // use char width * cursor_pos as rough estimate
        int cx = tr.left + (int)(cpos * (float)(cr.right - cr.left - 4) / (tlen > 0 ? tlen : 1));
        if (cx > cr.right-2) cx = cr.right-2;
        HPEN cp = CreatePen(PS_SOLID, 1, (COLORREF)g_swell_ctheme.edit_cursor);
        HGDIOBJ ocp = SelectObject(hdc, cp);
        MoveToEx(hdc, cx, tr.top, NULL);
        LineTo(hdc, cx, tr.bottom);
        SelectObject(hdc, ocp); DeleteObject(cp);
      }

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
        ? (COLORREF)g_swell_ctheme.label_text
        : (COLORREF)g_swell_ctheme.button_text_disabled;
      SetTextColor(hdc, fgcol);

      HBRUSH br = get_window_brush(hwnd, hdc, WM_CTLCOLORSTATIC);
      if (br && (INT_PTR)br != 1) {
        FillRect(hdc, &cr, br);
      } else if (!br) {
        SWELL_FillDialogBackground(hdc, &cr, 0);
      }
      // else: parent returned 1 (already painted bg), skip

      SetBkMode(hdc, TRANSPARENT);

      DWORD align_flag = DT_VCENTER | DT_SINGLELINE | DT_WORDBREAK;
      DWORD style = hwnd->m_style;
      if (style & SS_RIGHT)       align_flag = DT_RIGHT | DT_VCENTER | DT_SINGLELINE;
      else if (style & SS_CENTER) align_flag = DT_CENTER | DT_VCENTER | DT_SINGLELINE;
      else                        align_flag = DT_LEFT | DT_VCENTER | DT_SINGLELINE;

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
      col.col_index = pos;
      if (lvc->mask & LVCF_TEXT && lvc->pszText)
        col.name = strdup(lvc->pszText);
      if (lvc->mask & LVCF_WIDTH) col.xwid = lvc->cx;
      if (lvc->mask & LVCF_FMT)  col.fmt  = lvc->fmt;
      int n = st->m_cols.GetSize();
      if (pos < 0 || pos > n) pos = n;
      col.col_index = pos;
      // insert at pos
      st->m_cols.Resize(n+1, false);
      for (int i = n; i > pos; i--) st->m_cols.Get()[i] = st->m_cols.Get()[i-1];
      st->m_cols.Get()[pos] = col;
      col.name = NULL; // ownership transferred
      // fix indices
      for (int i = 0; i < st->m_cols.GetSize(); i++) st->m_cols.Get()[i].col_index = i;
      InvalidateRect(hwnd, NULL, FALSE);
      return pos;
    }

    case LVM_DELETECOLUMN: {
      if (!st) return FALSE;
      int pos = (int)wParam;
      if (pos < 0 || pos >= st->m_cols.GetSize()) return FALSE;
      free(st->m_cols.Get()[pos].name);
      int n = st->m_cols.GetSize();
      for (int i = pos; i < n-1; i++) st->m_cols.Get()[i] = st->m_cols.Get()[i+1];
      st->m_cols.Resize(n-1, false);
      for (int i = 0; i < st->m_cols.GetSize(); i++) st->m_cols.Get()[i].col_index = i;
      InvalidateRect(hwnd, NULL, FALSE);
      return TRUE;
    }

    case LVM_SETCOLUMN: {
      if (!st || !lParam) return FALSE;
      int pos = (int)wParam;
      if (pos < 0 || pos >= st->m_cols.GetSize()) return FALSE;
      const LVCOLUMN *lvc = (const LVCOLUMN *)lParam;
      SWELL_ListView_Col &c = st->m_cols.Get()[pos];
      if (lvc->mask & LVCF_TEXT && lvc->pszText) { free(c.name); c.name = strdup(lvc->pszText); }
      if (lvc->mask & LVCF_WIDTH) c.xwid = lvc->cx;
      if (lvc->mask & LVCF_FMT)  c.fmt  = lvc->fmt;
      InvalidateRect(hwnd, NULL, FALSE);
      return TRUE;
    }

    case LVM_GETCOLUMN: {
      if (!st || !lParam) return FALSE;
      int pos = (int)wParam;
      if (pos < 0 || pos >= st->m_cols.GetSize()) return FALSE;
      LVCOLUMN *lvc = (LVCOLUMN *)lParam;
      const SWELL_ListView_Col &c = st->m_cols.Get()[pos];
      if (lvc->mask & LVCF_TEXT && lvc->pszText && lvc->cchTextMax > 0)
        lstrcpyn(lvc->pszText, c.name ? c.name : "", lvc->cchTextMax);
      if (lvc->mask & LVCF_WIDTH) lvc->cx  = c.xwid;
      if (lvc->mask & LVCF_FMT)  lvc->fmt = c.fmt;
      return TRUE;
    }

    case LVM_GETCOLUMNWIDTH:
      if (!st) return 0;
      { int p = (int)wParam; return (p>=0 && p<st->m_cols.GetSize()) ? st->m_cols.Get()[p].xwid : 0; }

    case LVM_SETCOLUMNWIDTH:
      if (st && wParam >= 0 && (int)wParam < st->m_cols.GetSize()) {
        st->m_cols.Get()[(int)wParam].xwid = (int)lParam;
        InvalidateRect(hwnd, NULL, FALSE);
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

    case LVM_SETBKCOLOR:
      if (st) { st->m_color_bg = (int)lParam; InvalidateRect(hwnd, NULL, FALSE); }
      return TRUE;
    case LVM_SETTEXTBKCOLOR:
      return TRUE;
    case LVM_SETTEXTCOLOR:
      if (st) { st->m_color_text = (int)lParam; InvalidateRect(hwnd, NULL, FALSE); }
      return TRUE;

    case LVM_SETCOLUMNORDERARRAY:
    case LVM_GETCOLUMNORDERARRAY:
      return TRUE;

    case LVM_GETHEADER:
      return (LRESULT)hwnd; // return self as header

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
      int row = (my - hdr + st->m_scroll_y) / rh;
      int n = st->m_owner_data_size >= 0 ? st->m_owner_data_size : st->m_data.GetSize();
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

    case WM_MOUSEWHEEL: {
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

      // Background
      HBRUSH bg = CreateSolidBrush((COLORREF)g_swell_ctheme.listview_bg);
      FillRect(hdc, &cr, bg);
      DeleteObject(bg);

      bool focused = (GetFocus() == hwnd);
      int hdr = 0;

      // Draw column headers (ListView only)
      if (!st->m_is_listbox && st->m_cols.GetSize() > 0 &&
          !(hwnd->m_style & LVS_NOCOLUMNHEADER)) {
        hdr = rh + 2;
        RECT hr = { cr.left, cr.top, cr.right, cr.top + hdr };
        HBRUSH hbg = CreateSolidBrush((COLORREF)g_swell_ctheme.listview_header_bg);
        FillRect(hdc, &hr, hbg);
        DeleteObject(hbg);

        SetTextColor(hdc, (COLORREF)g_swell_ctheme.listview_header_text);
        SetBkMode(hdc, TRANSPARENT);
        int cx = cr.left - st->m_scroll_x;
        for (int c = 0; c < st->m_cols.GetSize(); c++) {
          const SWELL_ListView_Col &col = st->m_cols.Get()[c];
          RECT tr = { cx, cr.top, cx + col.xwid, cr.top + hdr };
          if (col.name) SWELL_DrawText(hdc, col.name, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
          // divider
          HPEN dp = CreatePen(PS_SOLID, 1, (COLORREF)g_swell_ctheme._3dshadow);
          HGDIOBJ op = SelectObject(hdc, dp);
          MoveToEx(hdc, cx + col.xwid - 1, cr.top, NULL);
          LineTo(hdc, cx + col.xwid - 1, cr.top + hdr);
          SelectObject(hdc, op); DeleteObject(dp);
          cx += col.xwid;
        }
      }

      // Draw rows
      int n = st->m_owner_data_size >= 0 ? st->m_owner_data_size : st->m_data.GetSize();
      int top_row = st->m_scroll_y / rh;
      RECT client_vis = { cr.left, cr.top + hdr, cr.right, cr.bottom };

      for (int i = top_row; i < n; i++) {
        int ry = cr.top + hdr + i * rh - st->m_scroll_y;
        if (ry >= cr.bottom) break;
        if (ry + rh < cr.top + hdr) continue;

        bool sel = (st->m_selitem == i) ||
                   (i < st->m_data.GetSize() && (st->m_data.Get(i)->m_tmp & 1));

        COLORREF rowbg = sel ? (focused ? (COLORREF)g_swell_ctheme.listview_bg
                                        : (COLORREF)g_swell_ctheme.listview_bg)
                             : (COLORREF)g_swell_ctheme.listview_bg;
        COLORREF rowfg = sel ? (focused ? (COLORREF)g_swell_ctheme.listview_text
                                        : (COLORREF)g_swell_ctheme.listview_text)
                             : (COLORREF)g_swell_ctheme.listview_text;

        if (sel) {
          rowbg = focused ? (COLORREF)g_swell_ctheme.listview_bg
                          : (COLORREF)g_swell_ctheme.listview_bg;
          // use slightly highlighted colors
          rowbg = focused ? 0x0078D7 : 0xCCCCCC;
          rowfg = focused ? 0xFFFFFF : 0x000000;
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
          if (par) SendMessage(par, WM_NOTIFY, hwnd->m_id, (LPARAM)&di);

          int ncols = st->m_cols.GetSize();
          if (ncols == 0) {
            RECT tr = { cr.left+2, ry, cr.right-2, ry+rh };
            SWELL_DrawText(hdc, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
          } else {
            int cx = cr.left - st->m_scroll_x;
            for (int c = 0; c < ncols; c++) {
              if (c > 0) {
                di.item.iSubItem = c;
                di.item.pszText = buf; buf[0] = 0;
                if (par) SendMessage(par, WM_NOTIFY, hwnd->m_id, (LPARAM)&di);
              }
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

static void tv_send_selchange(HWND hwnd, HTREEITEM olditem, HTREEITEM newitem)
{
  HWND par = GetParent(hwnd);
  if (!par) return;
  NMTREEVIEW nm = {};
  nm.hdr.hwndFrom = hwnd;
  nm.hdr.idFrom   = hwnd->m_id;
  nm.hdr.code     = TVN_SELCHANGED;
  nm.action       = TVC_BYMOUSE;
  if (olditem) { nm.itemOld.hItem = olditem; nm.itemOld.mask = TVIF_HANDLE | TVIF_PARAM; nm.itemOld.lParam = olditem->m_param; }
  if (newitem) { nm.itemNew.hItem = newitem; nm.itemNew.mask = TVIF_HANDLE | TVIF_PARAM; nm.itemNew.lParam = newitem->m_param; }
  SendMessage(par, WM_NOTIFY, hwnd->m_id, (LPARAM)&nm);
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
      UINT flag = (UINT)wParam;
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
      HTREEITEM old = st->m_sel;
      st->m_sel = item;
      tv_send_selchange(hwnd, old, item);
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
        if (st->m_sel == item) st->m_sel = NULL;
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
      if (!h) return FALSE;
      if (item->mask & TVIF_TEXT && item->pszText && item->cchTextMax > 0)
        lstrcpyn(item->pszText, h->m_value.Get(), item->cchTextMax);
      if (item->mask & TVIF_PARAM) item->lParam = h->m_param;
      if (item->mask & TVIF_STATE) item->state = h->m_state;
      if (item->mask & TVIF_CHILDREN) item->cChildren = h->m_children.GetSize() > 0 || h->m_haschildren ? 1 : 0;
      return TRUE;
    }

    case TVM_SETITEM: {
      if (!st || !lParam) return FALSE;
      LPTVITEM item = (LPTVITEM)lParam;
      HTREEITEM h = item->hItem;
      if (!h) return FALSE;
      if (item->mask & TVIF_TEXT && item->pszText) h->m_value.Set(item->pszText);
      if (item->mask & TVIF_PARAM) h->m_param = item->lParam;
      if (item->mask & TVIF_STATE) h->m_state = (h->m_state & ~item->stateMask) | (item->state & item->stateMask);
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
        hti->flags = TVHT_ONITEM;
        return (LRESULT)hti->hItem;
      }
      hti->hItem = NULL; hti->flags = TVHT_NOWHERE;
      return 0;
    }

    case TVM_ENSUREVISIBLE:
    case TVM_SETINDENT:
      return TRUE;

    case TVM_GETPARENT: {
      if (!st) return 0;
      HTREEITEM item = (HTREEITEM)lParam;
      int dummy = -1;
      HTREEITEM par = tv_find_parent(st->m_root, item, &dummy);
      return (par == st->m_root) ? 0 : (LRESULT)par;
    }

    case TVM_GETCHILD:
      if (!lParam) return 0;
      { HTREEITEM item = (HTREEITEM)lParam;
        return item->m_children.GetSize() > 0 ? (LRESULT)item->m_children.Get(0) : 0; }

    case TVM_GETNEXTSIBLING: {
      if (!st || !lParam) return 0;
      HTREEITEM item = (HTREEITEM)lParam;
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

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
      if (!st) return 0;
      SetFocus(hwnd);
      int rh = st->m_last_row_height > 0 ? st->m_last_row_height : 16;
      int indent = 16;
      WDL_PtrList<HTREEITEM__> items;
      tv_flatten(st->m_root, items);
      int my = GET_Y_LPARAM(lParam);
      int mx = GET_X_LPARAM(lParam);
      int row = (my + st->m_scroll_y) / rh;
      if (row >= 0 && row < items.GetSize()) {
        HTREEITEM item = items.Get(row);
        // check expand arrow (first indent area)
        // compute depth
        int depth = 0;
        HTREEITEM p = item;
        while ((p = (HTREEITEM)(void *)1)) { // just use mx
          break;
        }
        if (mx < indent && (item->m_children.GetSize() > 0 || item->m_haschildren)) {
          item->m_state ^= TVIS_EXPANDED;
          InvalidateRect(hwnd, NULL, FALSE);
        } else {
          HTREEITEM old = st->m_sel;
          st->m_sel = item;
          tv_send_selchange(hwnd, old, item);
          InvalidateRect(hwnd, NULL, FALSE);
        }
      }
      return 0;
    }

    case WM_MOUSEWHEEL: {
      if (!st) return 0;
      int delta = (short)HIWORD(wParam);
      st->m_scroll_y -= delta / 40 * (st->m_last_row_height > 0 ? st->m_last_row_height : 16);
      if (st->m_scroll_y < 0) st->m_scroll_y = 0;
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

      HBRUSH bg = CreateSolidBrush((COLORREF)g_swell_ctheme.treeview_bg);
      FillRect(hdc, &cr, bg);
      DeleteObject(bg);

      SetBkMode(hdc, TRANSPARENT);
      bool focused = (GetFocus() == hwnd);

      WDL_PtrList<HTREEITEM__> items;
      tv_flatten(st->m_root, items);

      int indent = 16;
      for (int i = 0; i < items.GetSize(); i++) {
        int ry = cr.top + i * rh - st->m_scroll_y;
        if (ry + rh < cr.top) continue;
        if (ry >= cr.bottom) break;
        HTREEITEM item = items.Get(i);

        // compute depth by finding parent chain
        // rough: just indent proportional to scrolled x
        int depth = 0;
        {
          HTREEITEM cur = item;
          while (true) {
            int didx = -1;
            HTREEITEM p = tv_find_parent(st->m_root, cur, &didx);
            if (!p || p == st->m_root) break;
            depth++;
            cur = p;
          }
        }

        int x = cr.left + depth * indent;
        bool sel = (item == st->m_sel);

        if (sel) {
          RECT sr = { cr.left, ry, cr.right, ry + rh };
          HBRUSH sb = CreateSolidBrush(focused ? (COLORREF)0x0078D7 : (COLORREF)0xCCCCCC);
          FillRect(hdc, &sr, sb);
          DeleteObject(sb);
        }

        // expand arrow
        if (item->m_children.GetSize() > 0 || item->m_haschildren) {
          int ax = x - indent/2;
          int ay = ry + rh/2;
          if (item->m_state & TVIS_EXPANDED) {
            // down arrow
            MoveToEx(hdc, ax-4, ay-2, NULL); LineTo(hdc, ax, ay+3); LineTo(hdc, ax+4, ay-2);
          } else {
            // right arrow
            MoveToEx(hdc, ax-2, ay-4, NULL); LineTo(hdc, ax+3, ay); LineTo(hdc, ax-2, ay+4);
          }
        }

        SetTextColor(hdc, sel ? (focused ? (COLORREF)0xFFFFFF : (COLORREF)0x000000)
                               : (COLORREF)g_swell_ctheme.treeview_text);
        RECT tr = { x + 2, ry, cr.right, ry + rh };
        SWELL_DrawText(hdc, item->m_value.Get(), -1, &tr,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      }

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
      SetFocus(hwnd);
      notify_parent(hwnd, CBN_DROPDOWN);
      return 0;

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      if (!hdc) return 0;

      RECT cr; GetClientRect(hwnd, &cr);
      int btnw = 16;

      HBRUSH bg = CreateSolidBrush((COLORREF)g_swell_ctheme.combo_bg);
      FillRect(hdc, &cr, bg);
      DeleteObject(bg);

      HPEN pen = CreatePen(PS_SOLID, 1, (COLORREF)g_swell_ctheme._3dshadow);
      HGDIOBJ op = SelectObject(hdc, pen);
      HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
      Rectangle(hdc, cr.left, cr.top, cr.right, cr.bottom);
      SelectObject(hdc, op); DeleteObject(pen);
      SelectObject(hdc, ob);

      // dropdown button
      RECT btnr = { cr.right - btnw, cr.top, cr.right, cr.bottom };
      HBRUSH btnbg = CreateSolidBrush((COLORREF)g_swell_ctheme.button_bg);
      FillRect(hdc, &btnr, btnbg);
      DeleteObject(btnbg);

      // draw filled arrow
      int ax = btnr.left + btnw/2;
      int ay = (cr.top + cr.bottom)/2;
      {
        POINT arrow_pts[3] = {
          { ax-4, ay-2 }, { ax, ay+2 }, { ax+4, ay-2 }
        };
        HBRUSH abr = CreateSolidBrush((COLORREF)g_swell_ctheme._3dshadow);
        HGDIOBJ obr = SelectObject(hdc, abr);
        HGDIOBJ openo = SelectObject(hdc, GetStockObject(NULL_PEN));
        SWELL_Polygon(hdc, arrow_pts, 3);
        SelectObject(hdc, openo);
        SelectObject(hdc, obr);
        DeleteObject(abr);
      }

      // text
      HFONT f = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
      SelectObject(hdc, f);
      SetTextColor(hdc, (COLORREF)g_swell_ctheme.combo_text);
      SetBkMode(hdc, TRANSPARENT);
      RECT tr = { cr.left+2, cr.top, cr.right - btnw - 2, cr.bottom };
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

LRESULT tabControlWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  tabControlState *st = (tabControlState *)(void *)hwnd->m_private_data;

  switch (msg) {
    case WM_CREATE:
      st = new tabControlState();
      hwnd->m_private_data = (INT_PTR)st;
      hwnd->m_wantfocus = false;
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
      st->m_curtab = (int)wParam;
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
      int th = SWELL_UI_SCALE(20);
      if (wParam) { r->top -= th; } // make larger
      else        { r->top += th; } // subtract tab bar
      return 0;
    }

    case WM_LBUTTONDOWN: {
      if (!st) return 0;
      int mx = GET_X_LPARAM(lParam);
      int th = SWELL_UI_SCALE(20);
      RECT cr; GetClientRect(hwnd, &cr);
      if (GET_Y_LPARAM(lParam) > th) return 0;
      // hit test tabs
      HDC hdc = GetDC(hwnd);
      HFONT f = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
      SelectObject(hdc, f);
      TEXTMETRIC tm2; GetTextMetrics(hdc, &tm2);
      int avgcw = tm2.tmAveCharWidth > 0 ? tm2.tmAveCharWidth : 8;
      int x = cr.left;
      for (int i = 0; i < st->m_tabs.GetSize(); i++) {
        const char *s = st->m_tabs.Get(i);
        int tw = (int)strlen(s) * avgcw + 16;
        if (mx >= x && mx < x + tw) {
          ReleaseDC(hwnd, hdc);
          SendMessage(hwnd, TCM_SETCURSEL, i, 0);
          return 0;
        }
        x += tw + 2;
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

      RECT cr; GetClientRect(hwnd, &cr);
      int th = SWELL_UI_SCALE(20);

      HBRUSH bg = CreateSolidBrush((COLORREF)g_swell_ctheme.tab_bg);
      FillRect(hdc, &cr, bg);
      DeleteObject(bg);

      HFONT f = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
      SelectObject(hdc, f);
      SetBkMode(hdc, TRANSPARENT);

      TEXTMETRIC tmtab; GetTextMetrics(hdc, &tmtab);
      int avgcwp = tmtab.tmAveCharWidth > 0 ? tmtab.tmAveCharWidth : 8;
      int x = cr.left;
      for (int i = 0; i < st->m_tabs.GetSize(); i++) {
        const char *s = st->m_tabs.Get(i);
        int tw = (int)strlen(s) * avgcwp + 16;
        bool sel = (i == st->m_curtab);

        RECT tr = { x, cr.top, x + tw, cr.top + th };
        HBRUSH tbbr = CreateSolidBrush(sel ? (COLORREF)g_swell_ctheme.tab_sel_bg
                                           : (COLORREF)g_swell_ctheme.tab_bg);
        FillRect(hdc, &tr, tbbr);
        DeleteObject(tbbr);

        SetTextColor(hdc, sel ? (COLORREF)g_swell_ctheme.tab_sel_text
                               : (COLORREF)g_swell_ctheme.tab_text);
        SWELL_DrawText(hdc, s, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // border
        HPEN tbp = CreatePen(PS_SOLID, 1, (COLORREF)g_swell_ctheme._3dshadow);
        HGDIOBJ otp = SelectObject(hdc, tbp);
        HGDIOBJ otb = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, tr.left, tr.top, tr.right + 1, tr.bottom + (sel ? 1 : 0));
        SelectObject(hdc, otp); DeleteObject(tbp);
        SelectObject(hdc, otb);

        x += tw + 2;
      }

      // bottom border line
      HPEN bp = CreatePen(PS_SOLID, 1, (COLORREF)g_swell_ctheme._3dshadow);
      HGDIOBJ obp = SelectObject(hdc, bp);
      MoveToEx(hdc, cr.left, cr.top + th, NULL);
      LineTo(hdc, cr.right, cr.top + th);
      SelectObject(hdc, obp); DeleteObject(bp);

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
  int *p = (int *)(void *)hwnd->m_private_data; // {pos, min, max}

  switch (msg) {
    case WM_CREATE:
      p = (int *)calloc(3, sizeof(int));
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
      if (tw > 0 && range > 0)
        p[0] = p[1] + (int)((float)mx / tw * range + 0.5f);
      if (p[0] < p[1]) p[0] = p[1]; if (p[0] > p[2]) p[0] = p[2];
      InvalidateRect(hwnd, NULL, FALSE);
      HWND par = GetParent(hwnd);
      if (par) SendMessage(par, WM_HSCROLL, MAKEWPARAM(SB_THUMBTRACK, (WORD)p[0]), (LPARAM)hwnd);
      return 0;
    }

    case WM_MOUSEMOVE: {
      if (!p || GetCapture() != hwnd) return 0;
      RECT cr; GetClientRect(hwnd, &cr);
      int mx = GET_X_LPARAM(lParam);
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

      RECT cr; GetClientRect(hwnd, &cr);

      HBRUSH bg = CreateSolidBrush((COLORREF)g_swell_ctheme.trackbar_bg);
      FillRect(hdc, &cr, bg);
      DeleteObject(bg);

      int cx = (cr.left + cr.right) / 2;
      int cy = (cr.top + cr.bottom) / 2;

      if (!p || p[2] <= p[1]) { EndPaint(hwnd, &ps); return 0; }

      // Track line
      HPEN tp = CreatePen(PS_SOLID, 2, (COLORREF)g_swell_ctheme.trackbar_fg);
      HGDIOBJ otp = SelectObject(hdc, tp);
      MoveToEx(hdc, cr.left + 4, cy, NULL);
      LineTo(hdc, cr.right - 4, cy);
      SelectObject(hdc, otp); DeleteObject(tp);

      // Thumb
      float frac = (float)(p[0] - p[1]) / (p[2] - p[1]);
      int tx = cr.left + 4 + (int)(frac * (cr.right - cr.left - 8));
      int th = 10, tw2 = 5;
      RECT thumb = { tx - tw2, cy - th/2, tx + tw2, cy + th/2 };
      HBRUSH thb = CreateSolidBrush((COLORREF)g_swell_ctheme.trackbar_thumb);
      FillRect(hdc, &thumb, thb);
      DeleteObject(thb);

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
      p[0] += (int)wParam;
      InvalidateRect(hwnd, NULL, FALSE);
      return p[0];
    }

    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      if (!hdc) return 0;

      RECT cr; GetClientRect(hwnd, &cr);

      HBRUSH bg = CreateSolidBrush((COLORREF)g_swell_ctheme._3dshadow);
      FillRect(hdc, &cr, bg);
      DeleteObject(bg);

      if (p && p[2] > p[1]) {
        float frac = (float)(p[0] - p[1]) / (p[2] - p[1]);
        if (frac < 0) frac = 0; if (frac > 1) frac = 1;
        int fw = (int)(frac * (cr.right - cr.left));
        RECT fr = { cr.left, cr.top, cr.left + fw, cr.bottom };
        HBRUSH fb = CreateSolidBrush((COLORREF)g_swell_ctheme.progress);
        FillRect(hdc, &fr, fb);
        DeleteObject(fb);
      }

      HPEN bp = CreatePen(PS_SOLID, 1, (COLORREF)g_swell_ctheme._3ddkshadow);
      HGDIOBJ obp = SelectObject(hdc, bp);
      HGDIOBJ obb = SelectObject(hdc, GetStockObject(NULL_BRUSH));
      Rectangle(hdc, cr.left, cr.top, cr.right, cr.bottom);
      SelectObject(hdc, obp); DeleteObject(bp);
      SelectObject(hdc, obb);

      EndPaint(hwnd, &ps);
      return 0;
    }

    default:
      return DefWindowProc(hwnd, msg, wParam, lParam);
  }
}
