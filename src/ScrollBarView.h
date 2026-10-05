// ---------------------------------------------------------------------------
// ScrollBarView.h - self-painted scroll bar
//
// Replaces the stock SCROLLBAR control (grey chunky Win95 arrows) with the
// slim bar the log view draws: flat light track, plain grey thumb, thin frame.
// Shared by the timeline (both axes) and the log view so they look identical.
//
// It is a real control, not decoration: click or drag the thumb, click the
// track to page, double-click an end to jump. Every user action is reported to
// the owner as WM_HSCROLL / WM_VSCROLL, with an emulated SB_* code in LOWORD
// and the resulting position in HIWORD - the same shape the native control
// uses, so the owner needs no new message plumbing.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>

class ScrollBarView
{
public:
    // `vertical` picks the axis. The owner receives WM_VSCROLL or WM_HSCROLL.
    bool Create(HWND owner, int id, HINSTANCE inst, bool vertical);
    HWND hwnd() const { return bar_; }

    void SetRange(int minV, int maxV, int page);
    void SetValue(int v);               // clamped, no notification sent
    int  Value() const { return value_; }
    int  MinValue()   const { return min_; }
    int  MaxValue()   const { return max_; }
    int  PageSize()   const { return page_; }

    bool HasRange() const { return max_ > min_; }

    void Refresh();                    // repaint

    static const wchar_t* ClassName() { return L"FastVideoCutScrollBar"; }

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);

    void OnPaint();
    void OnLButtonDown(int x, int y);
    void OnMouseMove(int x, int y, bool leftDown);
    void OnLButtonUp();
    void OnLButtonDblClk(int x, int y);

    // ---- geometry ---------------------------------------------------------
    int  TrackLength() const;          // along the scroll axis, inside the frame
    int  ThumbLength() const;          // 0 when everything already fits
    int  ThumbOffset() const;
    int  ValueAtPixel(int along) const;   // inverse of ThumbOffset
    bool OnThumb(int along) const;

    void Notify(int code, int value);

    HWND bar_    = nullptr;
    HWND owner_  = nullptr;
    bool vert_   = false;
    int  min_    = 0;
    int  max_    = 0;
    int  page_   = 1;
    int  value_  = 0;

    bool dragging_ = false;
    int  grabOff_  = 0;                // pixels between cursor and thumb start
};