// ---------------------------------------------------------------------------
// ScrollBarView.cpp - self-painted scroll bar
//
// Flat light track, plain grey thumb, thin frame - the look the log view had.
// ---------------------------------------------------------------------------
#include "ScrollBarView.h"

#include <windowsx.h>

#include <algorithm>

namespace
{
    const COLORREF kTrack  = RGB(240, 240, 240);
    const COLORREF kThumb  = RGB(160, 160, 160);
    const int      kMinThumb = 18;    // never so small it is impossible to hit
    const int      kFrame    = 1;
}

bool ScrollBarView::Create(HWND owner, int id, HINSTANCE inst, bool vertical)
{
    owner_ = owner;
    vert_  = vertical;

    WNDCLASSEXW wc;
    ::ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProcStatic;
    wc.hInstance     = inst;
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = ClassName();
    ::RegisterClassExW(&wc);

    bar_ = ::CreateWindowExW(0, ClassName(), L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                             0, 0, 10, 10, owner, (HMENU)(INT_PTR)id, inst, this);
    return bar_ != nullptr;
}

void ScrollBarView::SetRange(int minV, int maxV, int page)
{
    min_  = minV;
    max_  = (maxV < minV) ? minV : maxV;
    page_ = (page < 1) ? 1 : page;
    SetValue(value_);
}

void ScrollBarView::SetValue(int v)
{
    const int top  = min_;
    const int last = max_ - page_ + 1;
    value_ = std::min(std::max(v, top), std::max(top, last));
    Refresh();
}

void ScrollBarView::Refresh()
{
    if (bar_) ::InvalidateRect(bar_, nullptr, FALSE);
}

// ---- geometry -------------------------------------------------------------
int ScrollBarView::TrackLength() const
{
    if (!bar_) return 0;
    RECT rc;
    ::GetClientRect(bar_, &rc);
    return (vert_ ? (rc.bottom - rc.top) : (rc.right - rc.left)) - 2 * kFrame;
}

int ScrollBarView::ThumbLength() const
{
    const int track = TrackLength();
    if (track <= 0) return 0;
    const int range = max_ - min_ + 1;
    if (range <= 0 || page_ >= range) return 0;     // nothing to scroll

    int thumb = track * page_ / range;
    thumb = std::max(kMinThumb, thumb);
    return std::min(thumb, track);
}

int ScrollBarView::ThumbOffset() const
{
    const int track = TrackLength();
    const int thumb = ThumbLength();
    if (track <= 0 || thumb <= 0 || thumb >= track) return 0;

    const int scrollable = (max_ - min_ + 1) - page_;
    if (scrollable <= 0) return 0;
    return (track - thumb) * (value_ - min_) / scrollable;
}

int ScrollBarView::ValueAtPixel(int along) const
{
    const int track = TrackLength();
    const int thumb = ThumbLength();
    if (track <= 0 || thumb <= 0 || thumb >= track) return min_;

    const int scrollable = (max_ - min_ + 1) - page_;
    if (scrollable <= 0) return min_;

    int delta = along - ThumbOffset();
    delta = std::min(std::max(delta, 0), track - thumb);
    return min_ + delta * scrollable / (track - thumb);
}

bool ScrollBarView::OnThumb(int along) const
{
    const int thumb = ThumbLength();
    if (thumb <= 0) return false;
    const int off = ThumbOffset();
    return along >= off && along < off + thumb;
}

void ScrollBarView::Notify(int code, int value)
{
    if (!owner_) return;
    ::SendMessageW(owner_, vert_ ? WM_VSCROLL : WM_HSCROLL, MAKEWPARAM(code, value), 0);
}

// ---- input ----------------------------------------------------------------
void ScrollBarView::OnLButtonDown(int x, int y)
{
    if (!HasRange()) return;
    const int along = vert_ ? y : x;

    if (OnThumb(along))
    {
        dragging_ = true;
        grabOff_  = along - ThumbOffset();
        ::SetCapture(bar_);
    }
    else
    {
        // click on the track: page towards the click
        const int before = value_;
        value_ = std::min(std::max(ValueAtPixel(along), min_),
                          std::max(min_, max_ - page_ + 1));
        if (value_ != before) Notify(vert_ ? SB_PAGEUP : SB_PAGELEFT, value_);
    }
    Refresh();
}

void ScrollBarView::OnMouseMove(int x, int y, bool leftDown)
{
    if (!dragging_ || !leftDown) return;
    const int v = ValueAtPixel((vert_ ? y : x) - grabOff_);
    if (v != value_)
    {
        value_ = v;
        Notify(SB_THUMBTRACK, value_);
        Refresh();
    }
}

void ScrollBarView::OnLButtonUp()
{
    if (dragging_)
    {
        dragging_ = false;
        ::ReleaseCapture();
        Notify(SB_THUMBTRACK, value_);
    }
}

void ScrollBarView::OnLButtonDblClk(int x, int y)
{
    if (!HasRange()) return;
    const int along = vert_ ? y : x;
    const int target = (along < ThumbOffset() + ThumbLength() / 2) ? min_ : max_ - page_ + 1;
    if (target == value_) return;
    value_ = target;
    Notify(vert_ ? SB_BOTTOM : SB_RIGHT, value_);
    Refresh();
}

// ---- window proc ----------------------------------------------------------
LRESULT CALLBACK ScrollBarView::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ScrollBarView* self = (ScrollBarView*)::GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        self = (ScrollBarView*)cs->lpCreateParams;
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        if (self) self->bar_ = hwnd;      // WM_SIZE arrives before Create returns
    }
    if (!self) return ::DefWindowProcW(hwnd, msg, wp, lp);
    return self->WndProc(msg, wp, lp);
}

LRESULT ScrollBarView::WndProc(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_PAINT:       OnPaint(); return 0;
    case WM_ERASEBKGND:  return 1;
    case WM_SETCURSOR:
        ::SetCursor(::LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;

    case WM_LBUTTONDOWN:
        ::SetFocus(bar_);
        OnLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_LBUTTONDBLCLK:
        OnLButtonDblClk(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_MOUSEMOVE:
        OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), (wp & MK_LBUTTON) != 0);
        return 0;

    case WM_LBUTTONUP:      OnLButtonUp(); return 0;
    case WM_CAPTURECHANGED: dragging_ = false; return 0;

    case WM_MOUSEWHEEL:
        // The owner owns the wheel logic (the timeline maps it to rows or to
        // time), so hand it straight over rather than duplicating it here.
        if (owner_) ::SendMessageW(owner_, msg, wp, lp);
        return 0;

    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_SYSCHAR:
        // Clicking the bar takes the focus (so the drag reads correctly), but
        // the arrow / Home / End handling lives in the owner - pass keys on so
        // keyboard scrolling keeps working while the bar has the focus.
        if (owner_) return ::SendMessageW(owner_, msg, wp, lp);
        return 0;
    }
    return ::DefWindowProcW(bar_, msg, wp, lp);
}

// ---- painting -------------------------------------------------------------
void ScrollBarView::OnPaint()
{
    PAINTSTRUCT ps;
    HDC dc = ::BeginPaint(bar_, &ps);
    RECT rc;
    ::GetClientRect(bar_, &rc);

    HBRUSH track = ::CreateSolidBrush(kTrack);
    ::FillRect(dc, &rc, track);
    ::DeleteObject(track);

    const int thumb = ThumbLength();
    if (thumb > 0)
    {
        const int off = ThumbOffset();
        RECT t;
        if (vert_) t = { rc.left + 2, rc.top + kFrame + off + 2,
                         rc.right - 2, rc.top + kFrame + off + thumb - 2 };
        else       t = { rc.left + kFrame + off + 2, rc.top + 2,
                         rc.left + kFrame + off + thumb - 2, rc.bottom - 2 };

        HBRUSH hb = ::CreateSolidBrush(kThumb);
        ::FillRect(dc, &t, hb);
        ::DeleteObject(hb);
    }

    ::FrameRect(dc, &rc, (HBRUSH)::GetStockObject(GRAY_BRUSH));
    ::EndPaint(bar_, &ps);
}