//////////////////////////////////////////////////////////////////////
// This file was auto-generated by codelite's wxCrafter Plugin
// wxCrafter project file: UI.wxcp
// Do not modify this file by hand!
//////////////////////////////////////////////////////////////////////

#include "UI.h"

// Declare the bitmap loading function
extern void wxCrafternz79PnInitBitmapResources();

static bool bBitmapLoaded = false;

DAPMainViewBase::DAPMainViewBase(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
{
    if(!bBitmapLoaded) {
        // We need to initialise the default bitmap handler
        wxXmlResource::Get()->AddHandler(new wxBitmapXmlHandler);
        wxCrafternz79PnInitBitmapResources();
        bBitmapLoaded = true;
    }

    wxBoxSizer* boxSizer237 = new wxBoxSizer(wxVERTICAL);
    this->SetSizer(boxSizer237);

    m_splitter238 = new clThemedSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDLG_UNIT(this, wxSize(-1, -1)),
                                               wxSP_LIVE_UPDATE | wxSP_3DSASH);
    m_splitter238->SetSashGravity(0.5);
    m_splitter238->SetMinimumPaneSize(10);

    boxSizer237->Add(m_splitter238, 1, wxALL | wxEXPAND, WXC_FROM_DIP(5));

    m_splitterPage240 = new wxPanel(m_splitter238, wxID_ANY, wxDefaultPosition,
                                    wxDLG_UNIT(m_splitter238, wxSize(-1, -1)), wxTAB_TRAVERSAL);

    wxBoxSizer* boxSizer243 = new wxBoxSizer(wxVERTICAL);
    m_splitterPage240->SetSizer(boxSizer243);

    m_threadsTree = new clThemedTreeCtrl(m_splitterPage240, wxID_ANY, wxDefaultPosition,
                                         wxDLG_UNIT(m_splitterPage240, wxSize(-1, -1)), wxDV_ROW_LINES | wxDV_SINGLE);

    boxSizer243->Add(m_threadsTree, 1, wxEXPAND, WXC_FROM_DIP(5));

    m_splitterPage242 = new wxPanel(m_splitter238, wxID_ANY, wxDefaultPosition,
                                    wxDLG_UNIT(m_splitter238, wxSize(-1, -1)), wxTAB_TRAVERSAL);
    m_splitter238->SplitVertically(m_splitterPage240, m_splitterPage242, 0);

    wxBoxSizer* boxSizer244 = new wxBoxSizer(wxVERTICAL);
    m_splitterPage242->SetSizer(boxSizer244);

    m_variablesTree = new clThemedTreeCtrl(m_splitterPage242, wxID_ANY, wxDefaultPosition,
                                           wxDLG_UNIT(m_splitterPage242, wxSize(-1, -1)), wxDV_ROW_LINES | wxDV_SINGLE);

    boxSizer244->Add(m_variablesTree, 1, wxEXPAND, WXC_FROM_DIP(5));

    SetName(wxT("DAPMainViewBase"));
    SetSize(wxDLG_UNIT(this, wxSize(500, 300)));
    if(GetSizer()) {
        GetSizer()->Fit(this);
    }
}

DAPMainViewBase::~DAPMainViewBase() {}

DapDebuggerSettingsDlgBase::DapDebuggerSettingsDlgBase(wxWindow* parent, wxWindowID id, const wxString& title,
                                                       const wxPoint& pos, const wxSize& size, long style)
    : wxDialog(parent, id, title, pos, size, style)
{
    if(!bBitmapLoaded) {
        // We need to initialise the default bitmap handler
        wxXmlResource::Get()->AddHandler(new wxBitmapXmlHandler);
        wxCrafternz79PnInitBitmapResources();
        bBitmapLoaded = true;
    }

    wxBoxSizer* boxSizer249 = new wxBoxSizer(wxVERTICAL);
    this->SetSizer(boxSizer249);

    m_toolbar = new clToolBar(this, wxID_ANY, wxDefaultPosition, wxDLG_UNIT(this, wxSize(-1, -1)),
                              wxTB_HORZ_TEXT | wxTB_NODIVIDER | wxTB_FLAT);
    m_toolbar->SetToolBitmapSize(wxSize(16, 16));

    boxSizer249->Add(m_toolbar, 0, wxEXPAND, WXC_FROM_DIP(5));

    m_panelMain = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDLG_UNIT(this, wxSize(-1, -1)), wxTAB_TRAVERSAL);

    boxSizer249->Add(m_panelMain, 1, wxEXPAND, WXC_FROM_DIP(5));

    wxBoxSizer* boxSizer254 = new wxBoxSizer(wxVERTICAL);
    m_panelMain->SetSizer(boxSizer254);

    m_notebook =
        new wxNotebook(m_panelMain, wxID_ANY, wxDefaultPosition, wxDLG_UNIT(m_panelMain, wxSize(-1, -1)), wxBK_DEFAULT);
    m_notebook->SetName(wxT("m_notebook"));

    boxSizer254->Add(m_notebook, 1, wxALL | wxEXPAND, WXC_FROM_DIP(5));

    m_stdBtnSizer250 = new wxStdDialogButtonSizer();

    boxSizer249->Add(m_stdBtnSizer250, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, WXC_FROM_DIP(5));

    m_button251 = new wxButton(this, wxID_OK, wxT(""), wxDefaultPosition, wxDLG_UNIT(this, wxSize(-1, -1)), 0);
    m_button251->SetDefault();
    m_stdBtnSizer250->AddButton(m_button251);

    m_button252 = new wxButton(this, wxID_CANCEL, wxT(""), wxDefaultPosition, wxDLG_UNIT(this, wxSize(-1, -1)), 0);
    m_stdBtnSizer250->AddButton(m_button252);
    m_stdBtnSizer250->Realize();

#if wxVERSION_NUMBER >= 2900
    if(!wxPersistenceManager::Get().Find(m_notebook)) {
        wxPersistenceManager::Get().RegisterAndRestore(m_notebook);
    } else {
        wxPersistenceManager::Get().Restore(m_notebook);
    }
#endif

    SetName(wxT("DapDebuggerSettingsDlgBase"));
    SetSize(wxDLG_UNIT(this, wxSize(-1, -1)));
    if(GetSizer()) {
        GetSizer()->Fit(this);
    }
    if(GetParent()) {
        CentreOnParent(wxBOTH);
    } else {
        CentreOnScreen(wxBOTH);
    }
    if(!wxPersistenceManager::Get().Find(this)) {
        wxPersistenceManager::Get().RegisterAndRestore(this);
    } else {
        wxPersistenceManager::Get().Restore(this);
    }
}

DapDebuggerSettingsDlgBase::~DapDebuggerSettingsDlgBase() {}

DAPBreakpointsViewBase::DAPBreakpointsViewBase(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size,
                                               long style)
    : wxPanel(parent, id, pos, size, style)
{
    if(!bBitmapLoaded) {
        // We need to initialise the default bitmap handler
        wxXmlResource::Get()->AddHandler(new wxBitmapXmlHandler);
        wxCrafternz79PnInitBitmapResources();
        bBitmapLoaded = true;
    }

    wxBoxSizer* boxSizer264 = new wxBoxSizer(wxVERTICAL);
    this->SetSizer(boxSizer264);

    m_dvListCtrl = new clThemedListCtrl(this, wxID_ANY, wxDefaultPosition, wxDLG_UNIT(this, wxSize(-1, -1)),
                                        wxDV_ROW_LINES | wxDV_SINGLE);

    boxSizer264->Add(m_dvListCtrl, 1, wxALL | wxEXPAND, WXC_FROM_DIP(5));

    m_dvListCtrl->AppendTextColumn(_("ID"), wxDATAVIEW_CELL_INERT, WXC_FROM_DIP(-2), wxALIGN_LEFT, 0);
    m_dvListCtrl->AppendTextColumn(_("File"), wxDATAVIEW_CELL_INERT, WXC_FROM_DIP(-2), wxALIGN_LEFT, 0);
    m_dvListCtrl->AppendTextColumn(_("Line"), wxDATAVIEW_CELL_INERT, WXC_FROM_DIP(-2), wxALIGN_LEFT, 0);
    m_dvListCtrl->AppendTextColumn(_("Verified"), wxDATAVIEW_CELL_INERT, WXC_FROM_DIP(-2), wxALIGN_LEFT, 0);

    SetName(wxT("DAPBreakpointsViewBase"));
    SetSize(wxDLG_UNIT(this, wxSize(500, 300)));
    if(GetSizer()) {
        GetSizer()->Fit(this);
    }
}

DAPBreakpointsViewBase::~DAPBreakpointsViewBase() {}

DAPTextViewBase::DAPTextViewBase(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
{
    if(!bBitmapLoaded) {
        // We need to initialise the default bitmap handler
        wxXmlResource::Get()->AddHandler(new wxBitmapXmlHandler);
        wxCrafternz79PnInitBitmapResources();
        bBitmapLoaded = true;
    }

    wxBoxSizer* boxSizer267 = new wxBoxSizer(wxVERTICAL);
    this->SetSizer(boxSizer267);

    m_stcTextView =
        new wxStyledTextCtrl(this, wxID_ANY, wxDefaultPosition, wxDLG_UNIT(this, wxSize(-1, -1)), wxBORDER_NONE);
    // Configure the fold margin
    m_stcTextView->SetMarginType(4, wxSTC_MARGIN_SYMBOL);
    m_stcTextView->SetMarginMask(4, wxSTC_MASK_FOLDERS);
    m_stcTextView->SetMarginSensitive(4, true);
    m_stcTextView->SetMarginWidth(4, 0);

    // Configure the tracker margin
    m_stcTextView->SetMarginWidth(1, 0);

    // Configure the symbol margin
    m_stcTextView->SetMarginType(2, wxSTC_MARGIN_SYMBOL);
    m_stcTextView->SetMarginMask(2, ~(wxSTC_MASK_FOLDERS));
    m_stcTextView->SetMarginWidth(2, 0);
    m_stcTextView->SetMarginSensitive(2, true);

    // Configure the line numbers margin
    m_stcTextView->SetMarginType(0, wxSTC_MARGIN_NUMBER);
    m_stcTextView->SetMarginWidth(0, 0);

    // Configure the line symbol margin
    m_stcTextView->SetMarginType(3, wxSTC_MARGIN_FORE);
    m_stcTextView->SetMarginMask(3, 0);
    m_stcTextView->SetMarginWidth(3, 0);
    // Select the lexer
    m_stcTextView->SetLexer(wxSTC_LEX_NULL);
    // Set default font / styles
    m_stcTextView->StyleClearAll();
    m_stcTextView->SetWrapMode(0);
    m_stcTextView->SetIndentationGuides(0);
    m_stcTextView->SetKeyWords(0, wxT(""));
    m_stcTextView->SetKeyWords(1, wxT(""));
    m_stcTextView->SetKeyWords(2, wxT(""));
    m_stcTextView->SetKeyWords(3, wxT(""));
    m_stcTextView->SetKeyWords(4, wxT(""));

    boxSizer267->Add(m_stcTextView, 1, wxEXPAND, WXC_FROM_DIP(5));

    SetName(wxT("DAPTextViewBase"));
    SetSize(wxDLG_UNIT(this, wxSize(500, 300)));
    if(GetSizer()) {
        GetSizer()->Fit(this);
    }
}

DAPTextViewBase::~DAPTextViewBase() {}
