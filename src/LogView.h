// ---------------------------------------------------------------------------
// LogView.h - self-painted log pane
//
// This used to be a read-only multiline EDIT control. Two attempts to tame it
// (ES_AUTOVSCROLL, then ES_AUTOHSCROLL + wrapping by hand) still left lines
// painted on top of each other once the view was scrolled, so the EDIT is gone
// entirely: we own the window and draw every line ourselves with TextOutW.
// Nothing here asks Windows to wrap or to remember where a line starts, which
// is the only way to be sure scrolling cannot scramble the layout.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "ScrollBarView.h"

class LogView
{
public:
    bool Create(HWND owner, int id, HINSTANCE inst);
    HWND hwnd() const { return view_; }

    void SetFont(HFONT font);

    // Adds one logical line. It is wrapped here, at the current width, and
    // appended to the view. Scrolls to the new line unless the user has
    // scrolled up to read something.
    void AppendLine(const std::wstring& line);

    // Re-wraps every line for the current width. Called when the pane changes
    // size. Does nothing when the usable width has not changed, because a
    // drag-resize fires WM_SIZE per pixel.
    void Rebuild();

    // Keeps the scroll bar in step with shown_ / first_. Called after anything
    // that changes either. The bar is a real ScrollBarView, shared with the
    // timeline, so it can be dragged instead of only looking draggable.
    void UpdateBar();

    static const wchar_t* ClassName() { return L"FastVideoCutLogView"; }

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);

    void OnPaint();
    void OnMouseWheel(int wheelDelta);
    void OnLButtonDown(int x, int y);
    void OnMouseMove(int x, int y, bool leftDown);
    void OnLButtonUp();
    void OnKeyDown(WPARAM key);
    void CopySelection();
    void SelectAll();

    // ---- helpers ----------------------------------------------------------
    HFONT PickFont(HDC dc);                // picks font_, returns the previous one
    void  ComputeMetrics();                // lineH_ from the current font
    int   UsableWidth() const;
    int   VisibleLines() const;
    void  WrapInto(HDC dc, const std::wstring& line);   // append wrapped pieces
    void  ScrollTo(int firstLine);
    void  ScrollBy(int lines);
    void  ScrollToBottom();
    int   LineAt(int y) const;             // visual line index under a y
    std::wstring SelectedText() const;

    HWND                      view_    = nullptr;
    HFONT                     font_    = nullptr;
    ScrollBarView             bar_;

    std::vector<std::wstring> raw_;      // lines as logged, still unwrapped
    std::vector<std::wstring> shown_;   // visual lines, already wrapped

    int  first_    = 0;       // first visible visual line
    int  lineH_    = 0;
    int  lastWidth_ = -1;
    bool stick_    = true;    // follow the tail until the user scrolls away

    // selection, in visual line indices
    bool  dragging_  = false;
    size_t selFrom_  = 0;
    size_t selTo_    = 0;
};