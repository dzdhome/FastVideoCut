// ---------------------------------------------------------------------------
// ScrollBarView.h - self-painted scroll bar
//
// Replaces the stock SCROLLBAR control (grey chunky Win95 arrows) with the
// slim bar the log view draws: flat light track, plain grey thumb, thin frame.
// Shared by the timeline (both axes) and the log view so they look identical.
//
// Deliberately just a track and a thumb - no arrow buttons. The panes that use
// it are only a few hundred pixels across, and the arrows ate width the
// content could have used. The thumb darkens under the mouse and darker again
// while it is being dragged, so it is obvious that it can be grabbed. Drag
// the thumb, or click the track on the side you want to move towards.
//
// It is a real control, not decoration: every user action is reported to the
// owner as WM_HSCROLL / WM_VSCROLL, carrying an SB_* code in LOWORD and the
// resulting absolute position in HIWORD - the owner never has to compute the
// position itself, so a click and a drag end up in the same place.
//
// The wheel and the keyboard are *not* handled here: they are forwarded to the
// owner, which already maps them onto whatever the pane means by them (rows
// vs. time for the timeline, lines for the log).
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>

class ScrollBarView
{
public:
    // `vertical` picks the axis. The owner receives WM_VSCROLL or WM_HSCROLL.
    bool Create(HWND owner, int id, HINSTANCE inst, bool vertical);
    HWND hwnd() const { return bar_; }

    // `page` is the viewport size; the bar has no range to show when it fits.
    void SetRange(int minV, int maxV, int page);
    void SetValue(int v);               // clamped, no notification sent
    int  Value() const { return value_; }

    bool HasRange() const { return max_ - page_ + 1 > min_; }

    void Refresh();                    // repaint

    static const wchar_t* ClassName() { return L"FastVideoCutScrollBar"; }

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

    void OnPaint();
    void OnLButtonDown(int x, int y);
    void OnMouseMove(int x, int y, bool leftDown);
    void OnLButtonUp();
    void SetHover(bool on);             // repaints only when the state flips
    void ArmLeaveNotify();              // ask for WM_MOUSELEAVE once per visit

    // ---- geometry, measured along the scroll axis from the client origin ---
    int  Len() const;                  // full client length along the axis
    int  TrackStart() const;           // the frame is the only inset
    int  TrackLength() const;
    int  ThumbLength() const;
    int  ThumbOffset() const;
    int  ValueAtPixel(int along) const;
    bool OnThumb(int along) const;
    int  Clamped(int v) const;

    void Notify(int code, int value);

    HWND bar_   = nullptr;
    HWND owner_ = nullptr;
    bool vert_  = false;
    int  min_   = 0;
    int  max_   = 0;
    int  page_  = 1;
    int  value_ = 0;

    bool dragging_   = false;
    int  grabOff_    = 0;              // pixels between cursor and thumb start
    bool hoverThumb_ = false;          // cursor sits on a grabbable thumb
    bool tracking_   = false;          // WM_MOUSELEAVE is armed
};