// ---------------------------------------------------------------------------
// ScrollBarView.cpp - self-painted scroll bar
//
// Flat light track, plain grey thumb, thin frame - the look the log view had.
// No arrow buttons: the thumb darkens on hover and darker still while it is
// being dragged, which is a lighter hint than a button that is not there.
// ---------------------------------------------------------------------------
#include "ScrollBarView.h"

#include <windowsx.h>

#include <algorithm>

namespace
{
    const COLORREF kTrack      = RGB(240, 240, 240);
    const COLORREF kThumb      = RGB(160, 160, 160);
    const COLORREF kThumbHover = RGB(120, 120, 120);
    const COLORREF kThumbDown  = RGB(90, 90, 90);

    const int kMinTrack    = 16;     // below this the bar is not worth showing
    const int kMinThumb    = 18;     // never so small it is impossible to grab
    const int kFrame       = 1;
    const int kThumbInset  = 2;      // gap between the thumb and the frame
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
    value_ = Clamped(v);
    Refresh();
}

void ScrollBarView::Refresh()
{
    if (bar_) ::InvalidateRect(bar_, nullptr, FALSE);
}

// ---- geometry -------------------------------------------------------------
int ScrollBarView::Len() const
{
    if (!bar_) return 0;
    RECT rc;
    ::GetClientRect(bar_, &rc);
    return vert_ ? (rc.bottom - rc.top) : (rc.right - rc.left);
}

int ScrollBarView::TrackStart() const { return kFrame; }

int ScrollBarView::TrackLength() const
{
    const int t = Len() - 2 * TrackStart();
    return t > 0 ? t : 0;
}

int ScrollBarView::ThumbLength() const
{
    const int track = TrackLength();
    if (track <= 0) return 0;

    const int range = max_ - min_ - page_ + 1;
    if (range <= 0) return track;       // nothing to scroll: the thumb fills it

    // Scale the thumb with the ratio, but never below kMinThumb - a stubby
    // thumb the pointer keeps missing is worse than a slightly oversized one.
    return std::min(track, std::max(kMinThumb, (page_ * track) / range));
}

int ScrollBarView::ThumbOffset() const
{
    const int track = TrackLength();
    const int thumb = ThumbLength();
    if (track <= thumb) return 0;

    const int range = max_ - min_ - page_ + 1;
    if (range <= 0) return 0;

    return ((value_ - min_) * (track - thumb)) / range;
}

int ScrollBarView::ValueAtPixel(int along) const
{
    const int track = TrackLength();
    const int thumb = ThumbLength();
    if (track <= thumb) return min_;

    const int range = max_ - min_ - page_ + 1;
    if (range <= 0) return min_;

    int v = min_ + ((along - TrackStart() - thumb / 2) * range) / (track - thumb);
    return Clamped(v);
}

bool ScrollBarView::OnThumb(int along) const
{
    const int off = ThumbOffset();
    const int len = ThumbLength();
    return len > 0 && along >= off + TrackStart() && along < off + len + TrackStart();
}

int ScrollBarView::Clamped(int v) const
{
    if (v < min_) v = min_;
    if (v > max_ - page_ + 1) v = max_ - page_ + 1;
    if (v < 0) v = 0;
    return v;
}

// ---- input ----------------------------------------------------------------
void ScrollBarView::OnLButtonDown(int x, int y)
{
    if (!HasRange()) return;

    const int along = vert_ ? y : x;

    if (OnThumb(along))
    {
        dragging_ = true;
        grabOff_  = along - (ThumbOffset() + TrackStart());
        ::SetCapture(bar_);
        SetHover(true);
    }
    else
    {
        // Page towards the click. Before the thumb means "back", after it means
        // "forward" - the track is not a pair of fixed buttons, so the side
        // that was clicked is the side that decides.
        const bool back = along < ThumbOffset() + TrackStart() + ThumbLength() / 2;
        const int  pos  = Clamped(value_ + (back ? -page_ : page_));
        if (pos == value_) return;

        value_ = pos;
        Notify(vert_ ? (back ? SB_PAGEUP : SB_PAGEDOWN)
                     : (back ? SB_PAGELEFT : SB_PAGERIGHT), value_);
        Refresh();
    }
}

void ScrollBarView::OnMouseMove(int x, int y, bool leftDown)
{
    const int along = vert_ ? y : x;

    if (leftDown && dragging_)
    {
        const int pos = ValueAtPixel(along - grabOff_);
        if (pos != value_)
        {
            value_ = pos;
            Notify(SB_THUMBPOSITION, value_);
            Refresh();
        }
        return;
    }

    // Only track the hover state when nothing is held, otherwise the thumb
    // would flicker between colours as it slides out from under the cursor.
    ArmLeaveNotify();
    SetHover(HasRange() && OnThumb(along));
}

void ScrollBarView::OnLButtonUp()
{
    if (!dragging_) return;
    dragging_ = false;
    if (::GetCapture() == bar_) ::ReleaseCapture();
    Refresh();
}

void ScrollBarView::SetHover(bool on)
{
    if (on && !HasRange()) on = false;   // nothing to grab
    if (hoverThumb_ == on) return;
    hoverThumb_ = on;
    Refresh();
}

void ScrollBarView::ArmLeaveNotify()
{
    if (tracking_) return;
    TRACKMOUSEEVENT tme;
    ::ZeroMemory(&tme, sizeof(tme));
    tme.cbSize      = sizeof(tme);
    tme.dwFlags     = TME_LEAVE;
    tme.hwndTrack   = bar_;
    tme.dwHoverTime = 0;
    ::TrackMouseEvent(&tme);
    tracking_ = true;
}

void ScrollBarView::Notify(int code, int value)
{
    if (owner_)
        ::SendMessageW(owner_, vert_ ? WM_VSCROLL : WM_HSCROLL,
                       MAKEWPARAM(code, (value & 0xFFFF)), 0);
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

    case WM_MOUSEMOVE:
        OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), (wp & MK_LBUTTON) != 0);
        return 0;

    case WM_MOUSELEAVE:
        tracking_ = false;
        if (!dragging_) SetHover(false);
        return 0;

    case WM_LBUTTONDOWN:
        ::SetFocus(bar_);
        ArmLeaveNotify();
        OnLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_LBUTTONUP:
        OnLButtonUp();
        return 0;

    case WM_CAPTURECHANGED:
        // The drag was cut short (a menu, a window switch). Drop the pressed
        // colour, and let the next mouse move decide the hover one.
        dragging_ = false;
        Refresh();
        return 0;

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
        if (vert_) t = { rc.left + kThumbInset, rc.top + kFrame + off + kThumbInset,
                         rc.right - kThumbInset, rc.top + kFrame + off + thumb - kThumbInset };
        else       t = { rc.left + kFrame + off + kThumbInset, rc.top + kThumbInset,
                         rc.left + kFrame + off + thumb - kThumbInset, rc.bottom - kThumbInset };

        const COLORREF c = dragging_  ? kThumbDown
                         : (hoverThumb_ ? kThumbHover : kThumb);
        HBRUSH hb = ::CreateSolidBrush(c);
        ::FillRect(dc, &t, hb);
        ::DeleteObject(hb);
    }

    ::FrameRect(dc, &rc, (HBRUSH)::GetStockObject(GRAY_BRUSH));
    ::EndPaint(bar_, &ps);
}