// ---------------------------------------------------------------------------
// LogView.cpp - self-painted log pane
//
// Every line is drawn here with TextOutW. Windows is never asked to wrap the
// text or to remember where a line begins, so scrolling cannot make two lines
// land on the same y.
// ---------------------------------------------------------------------------
#include "LogView.h"

#include "Utf.h"

#include <windowsx.h>

#include <algorithm>
#include <cstring>

namespace
{
    const int      kWheelLinesPerNotch = 3;
    const int      kScrollBarW         = 14;
    const int      kMinScrollRange     = 8;
    const COLORREF kSelBg              = RGB(0xCC, 0xE4, 0xF7);
    const COLORREF kText               = RGB(16, 16, 16);
    const COLORREF kSelText            = RGB(0x00, 0x00, 0x00);
}

bool LogView::Create(HWND owner, int id, HINSTANCE inst)
{
    WNDCLASSEXW wc;
    ::ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProcStatic;
    wc.hInstance     = inst;
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;                 // OnPaint covers everything
    wc.lpszClassName = ClassName();
    ::RegisterClassExW(&wc);                    // harmless if already registered

    // WS_CLIPCHILDREN: never paint over our own children.
    // WS_CLIPSIBLINGS: when the timeline or the preview pane overlaps this
    // rect and repaints, it must not smear into here. The main window already
    // carries WS_CLIPCHILDREN, but the pane protects itself too - cheap, and
    // that kind of smear looks exactly like "text piled on text".
    view_ = ::CreateWindowExW(WS_EX_CLIENTEDGE, ClassName(), L"",
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                              WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                              0, 0, 10, 10, owner, (HMENU)(INT_PTR)id, inst, this);
    return view_ != nullptr;
}

void LogView::SetFont(HFONT font)
{
    font_ = font;
    ComputeMetrics();
    if (view_) Rebuild();
}

// Not called SelectFont: windowsx.h defines that as a macro.
HFONT LogView::PickFont(HDC dc)
{
    return font_ ? (HFONT)::SelectObject(dc, font_) : nullptr;
}

void LogView::ComputeMetrics()
{
    if (!view_) return;
    HDC dc = ::GetDC(view_);
    HFONT old = PickFont(dc);
    TEXTMETRICW tm{};
    if (dc && ::GetTextMetricsW(dc, &tm))
        lineH_ = (int)(tm.tmHeight + tm.tmExternalLeading);
    if (lineH_ <= 0) lineH_ = 15;
    if (old) ::SelectObject(dc, old);
    if (dc)  ::ReleaseDC(view_, dc);
}

int LogView::UsableWidth() const
{
    if (!view_) return 0;
    RECT rc;
    ::GetClientRect(view_, &rc);
    // RECT members are LONG, so cast before mixing with int in std::max.
    const int w = (int)(rc.right - rc.left);
    return std::max(8, w - kScrollBarW - 8);
}

int LogView::VisibleLines() const
{
    if (!view_ || lineH_ <= 0) return 0;
    RECT rc;
    ::GetClientRect(view_, &rc);
    return std::max(0, (int)(rc.bottom - rc.top) / lineH_);
}

// Appends `line` to shown_, broken into visual lines that each fit the width.
void LogView::WrapInto(HDC dc, const std::wstring& line)
{
    const std::wstring body = WrapTextToWidth(dc, line, UsableWidth());
    size_t at = 0;
    while (at < body.size())
    {
        const size_t nl = body.find(L"\r\n", at);
        if (nl == std::wstring::npos) { shown_.push_back(body.substr(at)); break; }
        shown_.push_back(body.substr(at, nl - at));
        at = nl + 2;
    }
}

void LogView::AppendLine(const std::wstring& line)
{
    raw_.push_back(line);
    if (raw_.size() > 4000) raw_.erase(raw_.begin(), raw_.begin() + 2000);

    if (!view_) return;

    ComputeMetrics();
    HDC dc = ::GetDC(view_);
    HFONT old = PickFont(dc);
    WrapInto(dc, line);
    if (old) ::SelectObject(dc, old);
    ::ReleaseDC(view_, dc);

    if (stick_) ScrollToBottom();
    ::InvalidateRect(view_, nullptr, FALSE);
}

void LogView::Rebuild()
{
    if (!view_) return;

    const int avail = UsableWidth();
    if (avail == lastWidth_) return;      // WM_SIZE fires per pixel while dragging
    lastWidth_ = avail;

    const bool wasAtBottom = stick_;
    const int  keepFirst   = first_;

    ComputeMetrics();
    HDC dc = ::GetDC(view_);
    HFONT old = PickFont(dc);
    shown_.clear();
    shown_.reserve(raw_.size() * 2);
    for (const std::wstring& l : raw_) WrapInto(dc, l);
    if (old) ::SelectObject(dc, old);
    ::ReleaseDC(view_, dc);

    if (wasAtBottom) ScrollToBottom();
    else             ScrollTo(keepFirst);

    if (selTo_ > shown_.size())   selTo_   = shown_.size();
    if (selFrom_ > shown_.size()) selFrom_ = shown_.size();
    ::InvalidateRect(view_, nullptr, FALSE);
}

void LogView::ScrollTo(int firstLine)
{
    const int maxFirst = std::max(0, (int)shown_.size() - VisibleLines());
    first_ = std::min(std::max(0, firstLine), maxFirst);
    stick_ = (first_ >= maxFirst);
    ::InvalidateRect(view_, nullptr, FALSE);
}

void LogView::ScrollBy(int lines)
{
    ScrollTo(first_ + lines);
}

void LogView::ScrollToBottom()
{
    first_ = std::max(0, (int)shown_.size() - VisibleLines());
    stick_ = true;
    ::InvalidateRect(view_, nullptr, FALSE);
}

int LogView::LineAt(int y) const
{
    if (lineH_ <= 0) return 0;
    return first_ + std::max(0, y) / lineH_;
}

std::wstring LogView::SelectedText() const
{
    if (selFrom_ == selTo_) return std::wstring();
    const size_t a = std::min(selFrom_, selTo_);
    const size_t b = std::max(selFrom_, selTo_);
    std::wstring out;
    for (size_t i = a; i < b && i < shown_.size(); ++i)
    {
        out += shown_[i];
        out += L"\r\n";
    }
    return out;
}

void LogView::CopySelection()
{
    const std::wstring s = SelectedText();
    if (s.empty()) return;
    if (!::OpenClipboard(view_)) return;
    ::EmptyClipboard();
    const size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h)
    {
        void* p = ::GlobalLock(h);
        if (p)
        {
            ::memcpy(p, s.c_str(), bytes);
            ::GlobalUnlock(h);
            ::SetClipboardData(CF_UNICODETEXT, h);
        }
        else
        {
            ::GlobalFree(h);
        }
    }
    ::CloseClipboard();
}

void LogView::SelectAll()
{
    selFrom_ = 0;
    selTo_   = shown_.size();
}

void LogView::OnPaint()
{
    PAINTSTRUCT ps;
    HDC dc = ::BeginPaint(view_, &ps);
    RECT rc;
    ::GetClientRect(view_, &rc);

    // Always repaint the whole client area before drawing any glyph. Every
    // invalidate in this control (append, scroll, re-wrap) covers the full
    // window, but erasing unconditionally here means a stale line can never
    // survive underneath a scrolled-to one - that is what "characters piled on
    // top of each other" looks like.
    ::FillRect(dc, &rc, (HBRUSH)::GetStockObject(WHITE_BRUSH));

    if (lineH_ <= 0) ComputeMetrics();

    HFONT old = PickFont(dc);
    // TRANSPARENT is deliberate and safe *because* of the full-area FillRect
    // above: the glyph backgrounds are already white. SetBkColor is kept in
    // sync with that fill anyway, so switching the mode to OPAQUE later could
    // not introduce a colour mismatch.
    ::SetBkMode(dc, TRANSPARENT);
    ::SetBkColor(dc, RGB(255, 255, 255));

    const size_t a = std::min(selFrom_, selTo_);
    const size_t b = std::max(selFrom_, selTo_);

    HBRUSH selBrush = (selFrom_ != selTo_) ? ::CreateSolidBrush(kSelBg) : nullptr;

    const int lines = VisibleLines();
    for (int i = 0; i < lines; ++i)
    {
        const size_t idx = (size_t)(first_ + i);
        if (idx >= shown_.size()) break;

        const int y = i * lineH_;
        const bool selected = (selBrush && idx >= a && idx < b);
        if (selected)
        {
            RECT band = { 0, y, rc.right - kScrollBarW, y + lineH_ };
            ::FillRect(dc, &band, selBrush);
        }
        ::SetTextColor(dc, selected ? kSelText : kText);
        ::TextOutW(dc, 4, y, shown_[idx].c_str(), (int)shown_[idx].size());
    }

    if (selBrush) ::DeleteObject(selBrush);
    if (old) ::SelectObject(dc, old);

    // ---- scroll bar (drawn by hand; it is not a real control) --------------
    const int maxFirst = std::max(0, (int)shown_.size() - VisibleLines());
    if (maxFirst > kMinScrollRange)
    {
        RECT bar = { rc.right - kScrollBarW, 0, rc.right, rc.bottom };
        HBRUSH tb = ::CreateSolidBrush(RGB(240, 240, 240));
        ::FillRect(dc, &bar, tb);
        ::DeleteObject(tb);

        const int track = bar.bottom - bar.top;
        int thumbH = track * std::max(1, VisibleLines()) / (int)shown_.size();
        thumbH = std::min(track, std::max(24, thumbH));
        const int thumbY = bar.top + (track - thumbH) * first_ / std::max(1, maxFirst);

        RECT thumb = { bar.left + 2, thumbY, bar.right - 2, thumbY + thumbH };
        HBRUSH hb = ::CreateSolidBrush(RGB(160, 160, 160));
        ::FillRect(dc, &thumb, hb);
        ::DeleteObject(hb);
        ::FrameRect(dc, &bar, (HBRUSH)::GetStockObject(GRAY_BRUSH));
    }

    ::EndPaint(view_, &ps);
}

void LogView::OnMouseWheel(int wheelDelta)
{
    int notch = wheelDelta / WHEEL_DELTA;
    if (wheelDelta % WHEEL_DELTA) ++notch;
    if (notch == 0) notch = 1;
    ScrollBy(-notch * kWheelLinesPerNotch);
}

void LogView::OnLButtonDown(int x, int y)
{
    (void)x;
    ::SetFocus(view_);
    const int idx = std::min((int)shown_.size(), std::max(0, LineAt(y)));
    selFrom_ = selTo_ = (size_t)idx;
    dragging_ = true;
    ::SetCapture(view_);
    ::InvalidateRect(view_, nullptr, FALSE);
}

void LogView::OnMouseMove(int x, int y, bool leftDown)
{
    (void)x;
    if (!leftDown || !dragging_) return;
    const int idx = std::min((int)shown_.size(), std::max(0, LineAt(y)));
    if ((size_t)idx != selTo_)
    {
        selTo_ = (size_t)idx;
        ::InvalidateRect(view_, nullptr, FALSE);
    }
}

void LogView::OnLButtonUp()
{
    if (dragging_)
    {
        dragging_ = false;
        ::ReleaseCapture();
    }
}

void LogView::OnKeyDown(WPARAM key)
{
    const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const int  page = std::max(1, VisibleLines() - 1);

    if (ctrl && (key == 'C' || key == 'c')) { CopySelection(); return; }
    if (ctrl && (key == 'A' || key == 'a'))
    {
        SelectAll();
        ::InvalidateRect(view_, nullptr, FALSE);
        return;
    }

    switch (key)
    {
    case VK_UP:     ScrollBy(-1);   return;
    case VK_DOWN:   ScrollBy(1);    return;
    case VK_PRIOR:  ScrollBy(-page); return;
    case VK_NEXT:   ScrollBy(page);  return;
    case VK_HOME:   ScrollTo(0);     return;
    case VK_END:    ScrollToBottom(); return;
    }
}

LRESULT CALLBACK LogView::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    LogView* self = (LogView*)::GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        self = (LogView*)cs->lpCreateParams;
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        // Publish the handle right away: WM_CREATE / WM_SIZE arrive while
        // CreateWindowEx is still running, so Create() has not assigned view_
        // yet, and DefWindowProcW(NULL, ...) would abort the creation.
        if (self) self->view_ = hwnd;
    }
    if (!self) return ::DefWindowProcW(hwnd, msg, wp, lp);
    return self->WndProc(msg, wp, lp);
}

LRESULT LogView::WndProc(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_SIZE:
        ComputeMetrics();
        Rebuild();
        return 0;

    case WM_PAINT:
        OnPaint();
        return 0;

    case WM_MOUSEWHEEL:
        OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wp));
        return 0;

    case WM_LBUTTONDOWN:
        OnLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_MOUSEMOVE:
        OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), (wp & MK_LBUTTON) != 0);
        return 0;

    case WM_LBUTTONUP:   OnLButtonUp(); return 0;
    case WM_CAPTURECHANGED: dragging_ = false; return 0;
    case WM_KEYDOWN:     OnKeyDown(wp); return 0;
    case WM_SETFOCUS:    ::InvalidateRect(view_, nullptr, FALSE); return 0;
    case WM_KILLFOCUS:   ::InvalidateRect(view_, nullptr, FALSE); return 0;
    case WM_ERASEBKGND:  return 1;               // OnPaint fills the whole area
    case WM_GETDLGCODE:  return DLGC_WANTALLKEYS;
    }
    return ::DefWindowProcW(view_, msg, wp, lp);
}