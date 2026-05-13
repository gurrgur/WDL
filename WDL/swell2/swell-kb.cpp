/*
  SWELL2 keyboard module — SWELL_KeyToASCII, accelerator processing,
  right-click emulation stub.
*/

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "swell-internal.h"
#include <cstring>
#include <cctype>

// OEM key VK codes (US layout) not in swell-types.h
#ifndef VK_OEM_PLUS
#define VK_OEM_PLUS   0xBB
#define VK_OEM_COMMA  0xBC
#define VK_OEM_MINUS  0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_1      0xBA
#define VK_OEM_2      0xBF
#define VK_OEM_3      0xC0
#define VK_OEM_4      0xDB
#define VK_OEM_5      0xDC
#define VK_OEM_6      0xDD
#define VK_OEM_7      0xDE
#endif

// ============================================================================
// SWELL_KeyToASCII
//
// Translates (wParam=VK_code, lParam=modifier_flags) to an ASCII character.
// lParam uses SWELL modifier encoding: FVIRTKEY|FSHIFT|FCONTROL|FALT|FLWIN
// Returns the ASCII char code, or 0 if no ASCII translation.
// newflags receives the modifier flags stripped of shift (since shift is
// consumed by the translation).
// ============================================================================

int SWELL_KeyToASCII(int wParam, int lParam, int *newflags)
{
  if (newflags) *newflags = lParam;

  // Only translate FVIRTKEY keys (VK_ codes), not raw characters
  if (!(lParam & FVIRTKEY)) {
    // Already an ASCII char in wParam
    return wParam;
  }

  bool shift   = (lParam & FSHIFT)   != 0;
  bool ctrl    = (lParam & FCONTROL) != 0;
  bool alt     = (lParam & FALT)     != 0;

  // Ctrl and Alt combos don't produce printable ASCII (they are accelerators)
  if (ctrl || alt) return 0;

  int ch = 0;

  // Letters A-Z
  if (wParam >= 'A' && wParam <= 'Z') {
    ch = shift ? wParam : (wParam + ('a' - 'A'));
    if (newflags) *newflags = lParam & ~FSHIFT;
    return ch;
  }

  // Digits 0-9
  if (wParam >= '0' && wParam <= '9') {
    if (!shift) {
      ch = wParam;
    } else {
      // US keyboard shift+digit
      static const char shift_digits[] = ")!@#$%^&*(";
      ch = shift_digits[wParam - '0'];
    }
    if (newflags) *newflags = lParam & ~FSHIFT;
    return ch;
  }

  // Numpad digits
  if (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9) {
    ch = '0' + (wParam - VK_NUMPAD0);
    if (newflags) *newflags = lParam & ~FSHIFT;
    return ch;
  }

  // Numpad operators
  switch (wParam) {
    case VK_MULTIPLY:  return '*';
    case VK_ADD:       return '+';
    case VK_SUBTRACT:  return '-';
    case VK_DECIMAL:   return '.';
    case VK_DIVIDE:    return '/';
    case VK_SPACE:     return ' ';
    case VK_RETURN:    return '\r';
    case VK_TAB:       return '\t';
    case VK_BACK:      return '\b';
  }

  // US keyboard punctuation / symbols
  // VK codes for OEM keys (US layout)
  switch (wParam) {
    case VK_OEM_PLUS:   ch = shift ? '+' : '='; break;
    case VK_OEM_COMMA:  ch = shift ? '<' : ','; break;
    case VK_OEM_MINUS:  ch = shift ? '_' : '-'; break;
    case VK_OEM_PERIOD: ch = shift ? '>' : '.'; break;
    case VK_OEM_1:      ch = shift ? ':' : ';'; break;  // ; /
    case VK_OEM_2:      ch = shift ? '?' : '/'; break;
    case VK_OEM_3:      ch = shift ? '~' : '`'; break;
    case VK_OEM_4:      ch = shift ? '{' : '['; break;
    case VK_OEM_5:      ch = shift ? '|' : '\\'; break;
    case VK_OEM_6:      ch = shift ? '}' : ']'; break;
    case VK_OEM_7:      ch = shift ? '"' : '\''; break;
    default: return 0;
  }

  if (newflags) *newflags = lParam & ~FSHIFT;
  return ch;
}

// ============================================================================
// SWELL_EnableRightClickEmulate (macOS Ctrl+click → right-click)
// No-op on Linux.
// ============================================================================

void SWELL_EnableRightClickEmulate(BOOL enable)
{
  (void)enable;
}
