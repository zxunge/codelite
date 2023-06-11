#include "TextView.h"

#include "FontUtils.hpp"

#include <wx/sizer.h>
#include <wx/wupdlock.h>

TextView::TextView(wxWindow* parent, wxWindowID winid, const wxFont& font, const wxColour& bg_colour,
                   const wxColour& text_colour)
    : wxWindow(parent, winid)
{
    m_textFont = font.IsOk() ? font : FontUtils::GetDefaultMonospacedFont();
    m_textColour = text_colour;
    m_bgColour = bg_colour;

    SetSizer(new wxBoxSizer(wxVERTICAL));
    m_ctrl = new wxStyledTextCtrl(this, wxID_ANY);
    m_ctrl->SetCaretStyle(wxSTC_CARETSTYLE_BLOCK);

    int caretSlop = 1;
    int caretZone = 20;
    int caretStrict = 0;
    int caretEven = 0;
    int caretJumps = 0;

    m_ctrl->SetXCaretPolicy(caretStrict | caretSlop | caretEven | caretJumps, caretZone);

    caretSlop = 1;
    caretZone = 1;
    caretStrict = 4;
    caretEven = 8;
    caretJumps = 0;
    m_ctrl->SetYCaretPolicy(caretStrict | caretSlop | caretEven | caretJumps, caretZone);

    m_ctrl->SetLexer(wxSTC_LEX_CONTAINER);
    m_ctrl->StartStyling(0);

    GetSizer()->Add(m_ctrl, 1, wxEXPAND);
    GetSizer()->Fit(this);
    m_colourHandler.SetCtrl(this);
    CallAfter(&TextView::ReloadSettings);
}

TextView::~TextView() {}

void TextView::AppendText(const std::string& buffer)
{
    m_ctrl->AppendText(buffer);
    RequestScrollToEnd();
}

long TextView::GetLastPosition() const { return m_ctrl->GetLastPosition(); }

wxString TextView::GetRange(int from, int to) const { return m_ctrl->GetRange(from, to); }

bool TextView::PositionToXY(long pos, long* x, long* y) const { return m_ctrl->PositionToXY(pos, x, y); }

long TextView::XYToPosition(long x, long y) const { return m_ctrl->XYToPosition(x, y); }

void TextView::Remove(long from, long to) { return m_ctrl->Remove(from, to); }

void TextView::SetInsertionPoint(long pos) { m_ctrl->SetInsertionPoint(pos); }

void TextView::SelectNone() { m_ctrl->SelectNone(); }

void TextView::SetInsertionPointEnd() { m_ctrl->SetInsertionPointEnd(); }

int TextView::GetNumberOfLines() const { return m_ctrl->GetNumberOfLines(); }

void TextView::SetDefaultStyle(const wxTextAttr& attr)
{
    m_defaultAttr = attr;
    for(int i = 0; i < wxSTC_STYLE_MAX; ++i) {
        m_ctrl->StyleSetBackground(i, attr.GetBackgroundColour());
        m_ctrl->StyleSetForeground(i, attr.GetTextColour());
        m_ctrl->StyleSetFont(i, attr.GetFont());
    }
}

wxTextAttr TextView::GetDefaultStyle() const { return m_defaultAttr; }

bool TextView::IsEditable() const { return m_ctrl->IsEditable(); }

long TextView::GetInsertionPoint() const { return m_ctrl->GetInsertionPoint(); }

void TextView::Replace(long from, long to, const wxString& replaceWith) { m_ctrl->Replace(from, to, replaceWith); }

wxString TextView::GetLineText(int lineNumber) const { return m_ctrl->GetLineText(lineNumber); }

void TextView::SetEditable(bool b) { m_ctrl->SetEditable(b); }

void TextView::ReloadSettings()
{
    SetBackgroundColour(m_bgColour);
    SetForegroundColour(m_textColour);
    for(int i = 0; i < wxSTC_STYLE_MAX; ++i) {
        m_ctrl->StyleSetBackground(i, m_bgColour);
        m_ctrl->StyleSetForeground(i, m_textColour);
        m_ctrl->StyleSetFont(i, m_textFont);
    }
    m_ctrl->SetCaretForeground(m_textColour);
    m_ctrl->SetBackgroundColour(m_bgColour);
    m_ctrl->SetForegroundColour(m_textColour);
    wxTextAttr defaultAttr = wxTextAttr(m_textColour, m_bgColour, m_textFont);
    SetDefaultStyle(defaultAttr);
    m_colourHandler.SetDefaultStyle(defaultAttr);
    m_ctrl->Refresh();
}

void TextView::StyleAndAppend(const std::string& buffer) { m_colourHandler << buffer; }

void TextView::Focus() { m_ctrl->SetFocus(); }

void TextView::ShowCommandLine()
{
    m_ctrl->SetSelection(m_ctrl->GetLastPosition(), m_ctrl->GetLastPosition());
    m_ctrl->EnsureCaretVisible();
    RequestScrollToEnd();
}

void TextView::SetCommand(long from, const wxString& command)
{
    m_ctrl->SetSelection(from, GetLastPosition());
    m_ctrl->ReplaceSelection(command);
}

void TextView::SetCaretEnd()
{
    m_ctrl->SetSelection(GetLastPosition(), GetLastPosition());
    m_ctrl->SetCurrentPos(GetLastPosition());
    m_ctrl->SetFocus();
}

int TextView::Truncate()
{
    if(GetNumberOfLines() > 1000) {
        // Start removing lines from the top
        long linesToRemove = (GetNumberOfLines() - 1000);
        long startPos = 0;
        long endPos = XYToPosition(0, linesToRemove);
        this->Remove(startPos, endPos);
        return endPos - startPos;
    }
    return 0;
}

wxChar TextView::GetLastChar() const { return m_ctrl->GetCharAt(m_ctrl->GetLastPosition() - 1); }

int TextView::GetCurrentStyle() { return 0; }

void TextView::Clear()
{
    m_ctrl->ClearAll();
    m_colourHandler.Clear();
}

void TextView::DoScrollToEnd()
{
    m_scrollToEndQueued = false;
    m_ctrl->ScrollToEnd();
}

void TextView::RequestScrollToEnd()
{
    if(m_scrollToEndQueued) {
        return;
    }
    m_scrollToEndQueued = true;
    CallAfter(&TextView::DoScrollToEnd);
}