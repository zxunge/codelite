//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//
// copyright            : (C) 2008 by Eran Ifrah
// file name            : manager.cpp
//
// -------------------------------------------------------------------------
// A
//              _____           _      _     _ _
//             /  __ \         | |    | |   (_) |
//             | /  \/ ___   __| | ___| |    _| |_ ___
//             | |    / _ \ / _  |/ _ \ |   | | __/ _ )
//             | \__/\ (_) | (_| |  __/ |___| | ||  __/
//              \____/\___/ \__,_|\___\_____/_|\__\___|
//
//                                                  F i l e
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
#include "precompiled_header.h"
#include "environmentconfig.h"
#include "evnvarlist.h"
#include "crawler_include.h"
#include "renamefiledlg.h"
#include "clang_code_completion.h"
#include "fileextmanager.h"
#include "localstable.h"
#include "new_quick_watch_dlg.h"
#include "debuggerconfigtool.h"
#include "debuggersettings.h"
#include "dbcontentcacher.h"
#include "debuggerasciiviewer.h"
#include <wx/aui/auibook.h>

#include <vector>
#include <algorithm>
#include <wx/stdpaths.h>
#include <wx/busyinfo.h>
#include <wx/progdlg.h>
#include <wx/file.h>
#include <wx/dir.h>
#include <wx/arrstr.h>
#include <wx/tokenzr.h>
#include <wx/regex.h>

#include "jobqueue.h"
#include "parse_thread.h"
#include "search_thread.h"
#include "pluginmanager.h"
#include "ctags_manager.h"
#include "language.h"
#include "context_manager.h"
#include "buildmanager.h"
#include "build_settings_config.h"
#include "menumanager.h"
#include "editor_config.h"
#include "environmentconfig.h"
#include "frame.h"
#include "sessionmanager.h"
#include "globals.h"
#include "vcimporter.h"
#include "macros.h"
#include "dirsaver.h"
#include "workspace_pane.h"
#include "workspacetab.h"
#include "dockablepanemenumanager.h"
#include "breakpointdlg.h"
#include "exelocator.h"
#include "simpletable.h"
#include "threadlistpanel.h"
#include "memoryview.h"
#include "attachdbgprocdlg.h"
#include "listctrlpanel.h"
#include "cl_editor.h"
#include "custombuildrequest.h"
#include "compile_request.h"
#include "clean_request.h"
#include "buidltab.h"
#include "tabgroupmanager.h"
#include "manager.h"

const wxEventType wxEVT_CMD_RESTART_CODELITE = wxNewEventType();

//---------------------------------------------------------------
// Menu accelerators helper method
//---------------------------------------------------------------

static wxString StripAccel ( const wxString &text )
{
	return text.BeforeFirst ( wxT ( '\t' ) );
}

//---------------------------------------------------------------
// Debugger helper method
//---------------------------------------------------------------
static wxArrayString DoGetTemplateTypes(const wxString& tmplDecl)
{
	wxArrayString types;
	int           depth (0);
	wxString      type;

	wxString tmpstr ( tmplDecl );
	tmpstr.Trim().Trim(false);

	if ( tmpstr.StartsWith(wxT("<")) ) {
		tmpstr.Remove(0, 1);
	}

	if ( tmpstr.EndsWith(wxT(">")) ) {
		tmpstr.RemoveLast();
	}
	tmpstr.Trim().Trim(false);

	for (size_t i=0; i<tmpstr.Length(); i++) {
		wxChar ch = tmpstr.GetChar(i);
		switch (ch) {
		case wxT(','):
			if ( depth > 0 ) {
				type << wxT(",");
			} else {
				type.Trim().Trim(false);
				if ( type.Contains(wxT("std::basic_string<char")) ) {
					type = wxT("string");
				} else if ( type.Contains(wxT("std::basic_string<wchar_t")) ) {
					type = wxT("wstring");
				}
				types.Add( type );
				type.Empty();
			}
			break;
		case wxT('<'):
			depth ++;
			type << wxT("<");
			break;
		case wxT('>'):
			depth--;
			type << wxT(">");
			break;
		default:
			type << tmpstr.GetChar(i);
			break;
		}
	}

	if ( depth == 0 && type.IsEmpty() == false ) {
		type.Trim().Trim(false);
		if ( type.Contains(wxT("std::basic_string<char")) ) {
			type = wxT("string");
		} else if ( type.Contains(wxT("std::basic_string<wchar_t")) ) {
			type = wxT("wstring");
		}
		types.Add( type );
	}

	return types;
}

//---------------------------------------------------------------
//
// The CodeLite manager class
//
//---------------------------------------------------------------

Manager::Manager ( void )
	: m_shellProcess         ( NULL )
	, m_asyncExeCmd          ( NULL )
	, m_breakptsmgr          ( new BreakptMgr )
	, m_isShutdown           ( false )
	, m_workspceClosing      ( false )
	, m_dbgCanInteract       ( false )
	, m_useTipWin            ( false )
	, m_tipWinPos            ( wxNOT_FOUND )
	, m_frameLineno          ( wxNOT_FOUND )
	, m_watchDlg             ( NULL )
	, m_retagInProgress      ( false )
	, m_repositionEditor     ( true )
{
	m_codeliteLauncher = wxFileName(wxT("codelite_launcher"));
	Connect(wxEVT_CMD_RESTART_CODELITE,            wxCommandEventHandler(Manager::OnRestart),              NULL, this);
	Connect(wxEVT_PARSE_THREAD_SCAN_INCLUDES_DONE, wxCommandEventHandler(Manager::OnIncludeFilesScanDone), NULL, this);
	Connect(wxEVT_PARSE_THREAD_INTERESTING_MACROS, wxCommandEventHandler(Manager::OnInterrestingMacrosFound), NULL, this);
	Connect(wxEVT_CMD_DB_CONTENT_CACHE_COMPLETED,  wxCommandEventHandler(Manager::OnDbContentCacherLoaded), NULL, this);

	wxTheApp->Connect(wxEVT_CMD_PROJ_SETTINGS_SAVED,  wxCommandEventHandler(Manager::OnProjectSettingsModified     ),     NULL, this);
}

Manager::~Manager ( void )
{
	wxTheApp->Disconnect(wxEVT_CMD_PROJ_SETTINGS_SAVED,  wxCommandEventHandler(Manager::OnProjectSettingsModified     ),     NULL, this);
	//stop background processes
	IDebugger *debugger = DebuggerMgr::Get().GetActiveDebugger();

	if(debugger && debugger->IsRunning())
		DbgStop();
	{
		//wxLogNull noLog;
		JobQueueSingleton::Instance()->Stop();
		ParseThreadST::Get()->Stop();
		SearchThreadST::Get()->Stop();
	}

	//free all plugins
	PluginManager::Get()->UnLoad();

	// release singleton objects
	DebuggerMgr::Free();
	JobQueueSingleton::Release();
	ParseThreadST::Free();  //since the parser is making use of the TagsManager,
	TagsManagerST::Free();  //it is important to release it *before* the TagsManager
	LanguageST::Free();
	WorkspaceST::Free();
	ContextManager::Free();
	BuildManagerST::Free();
	BuildSettingsConfigST::Free();
	SearchThreadST::Free();
	MenuManager::Free();
	EnvironmentConfig::Release();
	
#if HAS_LIBCLANG
	ClangCodeCompletion::Release();
#endif

	if ( m_shellProcess ) {
		delete m_shellProcess;
		m_shellProcess = NULL;
	}
	delete m_breakptsmgr;

	TabGroupsManager::Free();
}


//--------------------------- Workspace Loading -----------------------------

bool Manager::IsWorkspaceOpen() const
{
	return WorkspaceST::Get()->GetName().IsEmpty() == false;
}

void Manager::CreateWorkspace ( const wxString &name, const wxString &path )
{

	// make sure that the workspace pane is visible
	ShowWorkspacePane (clMainFrame::Get()->GetWorkspaceTab()->GetCaption());

	wxString errMsg;
	bool res = WorkspaceST::Get()->CreateWorkspace ( name, path, errMsg );
	if ( !res ) {
		wxMessageBox(errMsg, _("Error"), wxOK | wxICON_HAND);
		return;
	}

	OpenWorkspace ( path + PATH_SEP + name + wxT ( ".workspace" ) );
}

void Manager::OpenWorkspace ( const wxString &path )
{
	wxLogNull noLog;
	CloseWorkspace();

	wxString errMsg;
	bool res = WorkspaceST::Get()->OpenWorkspace ( path, errMsg );
	if ( !res ) {
		// in case part of the workspace was opened, close the workspace
		CloseWorkspace();
		wxMessageBox( errMsg, _("Error"), wxOK | wxICON_HAND );
		return;
	}

	// OpenWorkspace returned true, but errMsg is not empty
	// this could only mean that we removed a fauly project
	if(errMsg.IsEmpty() == false) {
		clMainFrame::Get()->GetMainBook()->ShowMessage(errMsg, true, PluginManager::Get()->GetStdIcons()->LoadBitmap(wxT("messages/48/error")));
	}

	if (GetActiveProjectName().IsEmpty()) {
		// This might happen if a removed faulty project was active
		wxArrayString list;
		WorkspaceST::Get()->GetProjectList(list);
		if ( !list.IsEmpty() ) {
			WorkspaceST::Get()->SetActiveProject(list.Item(0), true);
		}
	}

	DoSetupWorkspace ( path );
}

void Manager::ReloadWorkspace()
{
	if ( !IsWorkspaceOpen() )
		return;
	DbgStop();
	WorkspaceST::Get()->ReloadWorkspace();
	DoSetupWorkspace ( WorkspaceST::Get()->GetWorkspaceFileName().GetFullPath() );
}

void Manager::DoSetupWorkspace ( const wxString &path )
{
	wxString errMsg;
	wxString dbfile = WorkspaceST::Get()->GetStringProperty ( wxT ( "Database" ), errMsg );
	wxFileName fn ( dbfile );
	wxBusyCursor cursor;
	AddToRecentlyOpenedWorkspaces ( path );
	SendCmdEvent ( wxEVT_WORKSPACE_LOADED );
	if ( clMainFrame::Get()->GetFrameGeneralInfo().GetFlags() & CL_LOAD_LAST_SESSION ) {
		SessionEntry session;
		if ( SessionManager::Get().FindSession ( path, session ) ) {
			SessionManager::Get().SetLastWorkspaceName ( path );
			clMainFrame::Get()->GetWorkspaceTab()->FreezeThaw(true);	// Undo any workspace/editor link while loading
			clMainFrame::Get()->GetMainBook()->RestoreSession(session);
			clMainFrame::Get()->GetWorkspaceTab()->FreezeThaw(false);
			GetBreakpointsMgr()->LoadSession(session);
			clMainFrame::Get()->GetDebuggerPane()->GetBreakpointView()->Initialize();
		}
	}

	// Update the parser search paths
	UpdateParserPaths();
	clMainFrame::Get()->SelectBestEnvSet();

	// send an event to the main frame indicating that a re-tag is required
	// we do this only if the "smart retagging" is on
	TagsOptionsData tagsopt = TagsManagerST::Get()->GetCtagsOptions();
	if ( tagsopt.GetFlags() & CC_RETAG_WORKSPACE_ON_STARTUP ) {
		wxCommandEvent e(wxEVT_COMMAND_MENU_SELECTED, XRCID("retag_workspace"));
		clMainFrame::Get()->GetEventHandler()->AddPendingEvent(e);
	}

	// Set the encoding for the tags manager
	TagsManagerST::Get()->SetEncoding( EditorConfigST::Get()->GetOptions()->GetFileFontEncoding() );

	// Load the tags file content (we load the file and then destroy it) this way the file is forced into
	// the file system cache and will prevent hangs when first using the tagging system
	if(TagsManagerST::Get()->GetDatabase()) {
		wxFileName dbfn = TagsManagerST::Get()->GetDatabase()->GetDatabaseFileName();
		JobQueueSingleton::Instance()->PushJob( new DbContentCacher(this, dbfn.GetFullPath().c_str()) );
	}
}

void Manager::CloseWorkspace()
{
	m_workspceClosing = true;

	DbgClearWatches();

	// If we got a running debugging session - terminate it
	IDebugger *debugger = DebuggerMgr::Get().GetActiveDebugger();
	if(debugger && debugger->IsRunning())
		DbgStop();

	//save the current session before closing
	SessionEntry session;
	session.SetWorkspaceName ( WorkspaceST::Get()->GetWorkspaceFileName().GetFullPath() );
	wxArrayInt unused;
	clMainFrame::Get()->GetMainBook()->SaveSession(session, unused);
	GetBreakpointsMgr()->SaveSession(session);
	SessionManager::Get().Save ( WorkspaceST::Get()->GetWorkspaceFileName().GetFullPath(), session );

	// Delete any breakpoints belong to the current workspace
	GetBreakpointsMgr()->DelAllBreakpoints();

	// since we closed the workspace, we also need to set the 'LastActiveWorkspaceName' to be
	// default
	SessionManager::Get().SetLastWorkspaceName ( wxT ( "Default" ) );

	WorkspaceST::Get()->CloseWorkspace();
	if ( !IsShutdownInProgress() ) {
		SendCmdEvent ( wxEVT_WORKSPACE_CLOSED );
	}

#ifdef __WXMSW__
	// Under Windows, and in order to avoid locking the directory set the working directory back to the start up directory
	wxSetWorkingDirectory ( GetStarupDirectory() );
#endif

	/////////////////////////////////////////////////////////////////
	// set back the "Default" environment variable as the active set
	/////////////////////////////////////////////////////////////////

	EvnVarList vars = EnvironmentConfig::Instance()->GetSettings();
	if(vars.IsSetExist(wxT("Default"))) {
		vars.SetActiveSet(wxT("Default"));
	}
	EnvironmentConfig::Instance()->SetSettings(vars);
	clMainFrame::Get()->SelectBestEnvSet();

	UpdateParserPaths();
	m_workspceClosing = false;
}

void Manager::AddToRecentlyOpenedWorkspaces ( const wxString &fileName )
{
	// Add this workspace to the history. Don't check for uniqueness:
	// if it's already on the list, wxFileHistory will move it to the top
	m_recentWorkspaces.AddFileToHistory ( fileName );

	//sync between the history object and the configuration file
	wxArrayString files;
	m_recentWorkspaces.GetFiles ( files );
	EditorConfigST::Get()->SetRecentItems( files, wxT("RecentWorkspaces") );

	// The above call to AddFileToHistory() rewrote the Recent Workspaces menu
	// Unfortunately it rewrote it with path/to/foo.workspace, and we'd prefer
	// not to display the extension. So reload it again the way we like it :)
	clMainFrame::Get()->CreateRecentlyOpenedWorkspacesMenu();
}

void Manager::ClearWorkspaceHistory()
{
	size_t count = m_recentWorkspaces.GetCount();
	for ( size_t i=0; i<count; i++ ) {
		m_recentWorkspaces.RemoveFileFromHistory ( 0 );
	}
	wxArrayString files;
	EditorConfigST::Get()->SetRecentItems( files, wxT("RecentWorkspaces") );
}

void Manager::GetRecentlyOpenedWorkspaces ( wxArrayString &files )
{
	EditorConfigST::Get()->GetRecentItems( files, wxT("RecentWorkspaces") );
}


//--------------------------- Workspace Projects Mgmt -----------------------------

void Manager::CreateProject ( ProjectData &data )
{
	if ( IsWorkspaceOpen() == false ) {
		//create a workspace before creating a project
		CreateWorkspace ( data.m_name, data.m_path );
	}

	wxString errMsg;
	bool res = WorkspaceST::Get()->CreateProject ( data.m_name,
	           data.m_path,
	           data.m_srcProject->GetSettings()->GetProjectType ( wxEmptyString ),
	           false,
	           errMsg );
	if ( !res ) {
		wxMessageBox(errMsg, _("Error"), wxOK | wxICON_HAND);
		return;
	}
	ProjectPtr proj = WorkspaceST::Get()->FindProjectByName ( data.m_name, errMsg );

	//copy the project settings to the new one
	proj->SetSettings ( data.m_srcProject->GetSettings() );

	proj->SetProjectInternalType(data.m_srcProject->GetProjectInternalType());

	// now add the new project to the build matrix
	WorkspaceST::Get()->AddProjectToBuildMatrix ( proj );
	ProjectSettingsPtr settings = proj->GetSettings();

	// set the compiler type
	ProjectSettingsCookie cookie;
	BuildConfigPtr bldConf = settings->GetFirstBuildConfiguration ( cookie );
	while ( bldConf ) {
		bldConf->SetCompilerType ( data.m_cmpType );
		bldConf = settings->GetNextBuildConfiguration ( cookie );
	}
	proj->SetSettings ( settings );

	// copy the files as they appear in the source project
	proj->SetFiles ( data.m_srcProject );

	// copy plugins data
	std::map<wxString, wxString> pluginsData;
	data.m_srcProject->GetAllPluginsData( pluginsData );
	proj->SetAllPluginsData( pluginsData );

	{
		// copy the actual files from the template directory to the new project path
		DirSaver ds;
		wxSetWorkingDirectory ( proj->GetFileName().GetPath() );

		// get list of files
		std::vector<wxFileName> files;
		data.m_srcProject->GetFiles ( files, true );
		for ( size_t i=0; i<files.size(); i++ ) {
			wxFileName f ( files.at ( i ) );
			wxCopyFile ( f.GetFullPath(), f.GetFullName() );
		}
	}

	wxString projectName = proj->GetName();
	RetagProject ( projectName, true );
	SendCmdEvent ( wxEVT_PROJ_ADDED, ( void* ) &projectName );
}

void Manager::AddProject ( const wxString & path )
{
	// create a workspace if there is non
	if ( IsWorkspaceOpen() == false ) {

		wxFileName fn(path);

		//create a workspace before creating a project
		CreateWorkspace ( fn.GetName(), fn.GetPath() );
	}

	wxString errMsg;
	bool res = WorkspaceST::Get()->AddProject ( path, errMsg );
	if ( !res ) {
		wxMessageBox(errMsg, _("Error"), wxOK | wxICON_HAND);
		return;
	}

	wxFileName fn ( path );
	wxString projectName ( fn.GetName() );
	RetagProject ( projectName, true );
	SendCmdEvent ( wxEVT_PROJ_ADDED, ( void* ) &projectName );
}

void Manager::ImportMSVSSolution ( const wxString &path, const wxString &defaultCompiler )
{
	wxFileName fn ( path );
	if ( fn.FileExists() == false ) {
		return;
	}

	// Show some messages to the user
	wxBusyCursor busyCursor;
	wxBusyInfo info(_("Importing MS solution..."), clMainFrame::Get());

	wxString errMsg;
	VcImporter importer ( path, defaultCompiler );
	if ( importer.Import ( errMsg ) ) {
		wxString wspfile;
		wspfile << fn.GetPath() << wxT ( "/" ) << fn.GetName() << wxT ( ".workspace" );
		OpenWorkspace ( wspfile );

		// Retag workspace
		wxCommandEvent event( wxEVT_COMMAND_MENU_SELECTED, XRCID("retag_workspace") );
		clMainFrame::Get()->GetEventHandler()->AddPendingEvent( event );
	}
}

bool Manager::RemoveProject ( const wxString &name )
{
	if ( name.IsEmpty() ) {
		return false;
	}

	ProjectPtr proj = GetProject ( name );

	wxString errMsg;
	bool res = WorkspaceST::Get()->RemoveProject ( name, errMsg );
	if ( !res ) {
		wxMessageBox(errMsg, _("Error"), wxOK | wxICON_HAND);
		return false;
	}

	if ( proj ) {
		//remove symbols from the database
		std::vector<wxFileName> projectFiles;
		proj->GetFiles ( projectFiles, true );
		TagsManagerST::Get()->DeleteFilesTags ( projectFiles );
		wxArrayString prjfls;
		for ( size_t i = 0; i < projectFiles.size(); i++ ) {
			prjfls.Add ( projectFiles[i].GetFullPath() );
		}
		SendCmdEvent ( wxEVT_PROJ_FILE_REMOVED, ( void* ) &prjfls );
	} // if(proj)

	SendCmdEvent ( wxEVT_PROJ_REMOVED, ( void* ) &name );

	return true;
}

void Manager::GetProjectList ( wxArrayString &list )
{
	WorkspaceST::Get()->GetProjectList ( list );
}

ProjectPtr Manager::GetProject ( const wxString &name ) const
{
	wxString projectName ( name );
	projectName.Trim().Trim(false);

	if(projectName.IsEmpty())
		return NULL;

	wxString errMsg;
	ProjectPtr proj = WorkspaceST::Get()->FindProjectByName ( name, errMsg );
	if ( !proj ) {
		wxLogMessage ( errMsg );
		return NULL;
	}
	return proj;
}

wxString Manager::GetActiveProjectName()
{

	return WorkspaceST::Get()->GetActiveProjectName();
}

void Manager::SetActiveProject ( const wxString &name )
{
	WorkspaceST::Get()->SetActiveProject ( WorkspaceST::Get()->GetActiveProjectName(), false );
	WorkspaceST::Get()->SetActiveProject ( name, true );
	clMainFrame::Get()->SelectBestEnvSet();
}

BuildMatrixPtr Manager::GetWorkspaceBuildMatrix() const
{
	return WorkspaceST::Get()->GetBuildMatrix();
}

void Manager::SetWorkspaceBuildMatrix ( BuildMatrixPtr matrix )
{
	WorkspaceST::Get()->SetBuildMatrix ( matrix );
	SendCmdEvent(wxEVT_WORKSPACE_CONFIG_CHANGED);
}


//--------------------------- Workspace Files Mgmt -----------------------------

void Manager::GetWorkspaceFiles ( wxArrayString &files )
{
	// Send an event to the plugins to get a list of the workspace files.
	// If no plugin has replied, use the default GetWorkspaceFiles method
	wxCommandEvent getFilesEevet(wxEVT_CMD_GET_WORKSPACE_FILES);
	getFilesEevet.SetEventObject(this);
	getFilesEevet.SetClientData(&files);
	if(wxTheApp->ProcessEvent(getFilesEevet)) {
		return;
	}

	if ( !IsWorkspaceOpen() ) {
		return;
	}

	wxArrayString projects;
	GetProjectList ( projects );

	for ( size_t i=0; i<projects.GetCount(); i++ ) {
		GetProjectFiles ( projects.Item ( i ), files );
	}
}

void Manager::GetWorkspaceFiles ( std::vector<wxFileName> &files, bool absPath )
{
	wxArrayString projects;
	GetProjectList ( projects );
	for ( size_t i=0; i<projects.GetCount(); i++ ) {
		ProjectPtr p = GetProject ( projects.Item ( i ) );
		p->GetFiles ( files, absPath );
	}
}

bool Manager::IsFileInWorkspace ( const wxString &fileName )
{
	std::set<wxString> files;
	GetWorkspaceFiles ( files );
	return files.find(fileName) != files.end();
}

void Manager::GetWorkspaceFiles(std::set<wxString>& files)
{
	wxArrayString filesArr;
	wxArrayString projects;
	GetProjectList ( projects );

	for ( size_t i=0; i<projects.GetCount(); i++ ) {
		GetProjectFiles ( projects.Item ( i ), filesArr );
	}

	for(size_t i=0; i<filesArr.GetCount(); i++) {
		files.insert(filesArr.Item(i));
	}
}

wxFileName Manager::FindFile ( const wxString &filename, const wxString &project )
{
	wxString tmpfile(filename);
	tmpfile.Trim().Trim(false);

	if(tmpfile.IsEmpty()) {
		return wxFileName();
	}

	wxFileName fn ( filename );
	if ( !fn.FileExists() ) {
		// try to open the file as is
		fn.Clear();
	}
	if ( !fn.IsOk() && !project.IsEmpty() ) {
		// try to open the file in context of its project
		wxArrayString project_files;
		GetProjectFiles ( project, project_files );
		fn = FindFile ( project_files, filename );
	}
	if ( !fn.IsOk() ) {
		// no luck there.  try the whole workspace
		wxArrayString workspace_files;
		GetWorkspaceFiles ( workspace_files );
		fn = FindFile ( workspace_files, filename );
	}
	if (!fn.IsAbsolute()) {
		fn.MakeAbsolute();
	}
	return fn;
}

// ATTN: Please do not change this code!
wxFileName Manager::FindFile ( const wxArrayString& files, const wxFileName &fn )
{
	// Iterate over the files twice:
	// first, try to full path
	// if the first iteration failes, iterate the files again
	// and compare full name only
	if ( fn.IsAbsolute() && !fn.GetFullPath().Contains ( wxT ( ".." ) ) ) {
		return fn;
	}

	std::vector<wxFileName> matches;
	// Try to find a match in the workspace (by comparing full paths)
	for ( size_t i=0; i< files.GetCount(); i++ ) {
		wxFileName tmpFileName ( files.Item ( i ) );
		if ( tmpFileName.GetFullPath().CmpNoCase(fn.GetFullPath()) == 0 ) {
			wxFileName tt ( tmpFileName );
			if ( tt.MakeAbsolute() ) {
				return tt;
			} else {
				return tmpFileName;
			}
		}
		if ( tmpFileName.GetFullName() == fn.GetFullName() ) {
			matches.push_back ( tmpFileName );
		}
	}

	wxString lastDir;
	wxArrayString dirs = fn.GetDirs();
	if ( dirs.GetCount() > 0 ) {
		lastDir = dirs.Last();
	}

	if ( matches.size() == 1 ) {
		wxFileName tt ( matches.at ( 0 ) );
		if ( tt.MakeAbsolute() ) {
			return tt;
		} else {
			return matches.at ( 0 );
		}

	} else if ( matches.size() > 1 ) {
		// take the best match
		std::vector<wxFileName> betterMatches;
		for ( size_t i=0; i<matches.size(); i++ ) {

			wxFileName filename ( matches.at ( i ) );
			wxArrayString tmpdirs = filename.GetDirs();
			if ( tmpdirs.GetCount() > 0 ) {
				if ( tmpdirs.Last() == lastDir ) {
					betterMatches.push_back ( filename );
				}
			}
		}

		if ( betterMatches.size() == 1 ) {
			wxFileName tt ( betterMatches.at ( 0 ) );
			if ( tt.MakeAbsolute() ) {
				return tt;
			} else {
				return betterMatches.at ( 0 );
			}
		} else {
			// open the first match
			wxFileName tt ( matches.at ( 0 ) );
			if ( tt.MakeAbsolute() ) {
				return tt;
			} else {
				return matches.at ( 0 );
			}
		}
	} else {
		// try to convert it to absolute path
		wxFileName f1 ( fn );
		if ( f1.MakeAbsolute() /*&& f1.FileExists()*/ && !f1.GetFullPath().Contains ( wxT ( ".." ) ) ) {
			return f1;
		}
	}
	return wxFileName();
}

void Manager::RetagWorkspace(bool quickRetag)
{
	SetRetagInProgress(true);

	// in the case of re-tagging the entire workspace and full re-tagging is enabled
	// it is faster to drop the tables instead of deleting
	if ( !quickRetag )
		TagsManagerST::Get()->GetDatabase()->RecreateDatabase();

	wxArrayString projects;
	GetProjectList ( projects );

	std::vector<wxFileName> projectFiles;
	for ( size_t i=0; i<projects.GetCount(); i++ ) {
		ProjectPtr proj = GetProject ( projects.Item ( i ) );
		if ( proj ) {
			proj->GetFiles( projectFiles, true );
		}
	}

	// Create a parsing request
	ParseRequest *parsingRequest = new ParseRequest();
	for (size_t i=0; i<projectFiles.size(); i++) {
		// filter any non valid coding file
		if(!TagsManagerST::Get()->IsValidCtagsFile(projectFiles.at(i)))
			continue;
		parsingRequest->_workspaceFiles.push_back( projectFiles.at(i).GetFullPath().mb_str(wxConvUTF8).data() );
	}

	parsingRequest->setType(ParseRequest::PR_PARSEINCLUDES);
	parsingRequest->_evtHandler = this;
	parsingRequest->_quickRetag = quickRetag;
	ParseThreadST::Get()->Add ( parsingRequest );

	clMainFrame::Get()->SetStatusMessage(_("Scanning for include files to parse..."), 0);
}

void Manager::RetagFile ( const wxString& filename )
{
	if ( IsWorkspaceClosing() ) {
		wxLogMessage ( wxString::Format ( wxT ( "Workspace in being closed, skipping re-tag for file %s" ), filename.c_str() ) );
		return;
	}
	if ( !TagsManagerST::Get()->IsValidCtagsFile ( wxFileName ( filename ) ) ) {
		wxLogMessage ( wxT ( "Not a valid C tags file type: %s. Skipping." ), filename.c_str() );
		return;
	}

	wxFileName absFile ( filename );
	absFile.MakeAbsolute();

	// Put a request to the parsing thread
	ParseRequest *req = new ParseRequest();
	req->setDbFile   ( TagsManagerST::Get()->GetDatabase()->GetDatabaseFileName().GetFullPath().c_str() );
	req->setFile     ( absFile.GetFullPath().c_str() );
	req->setType     ( ParseRequest::PR_FILESAVED );
	ParseThreadST::Get()->Add ( req );

	wxString msg = wxString::Format(wxT( "Re-tagging file %s..." ), absFile.GetFullName().c_str());
	clMainFrame::Get()->SetStatusMessage(msg, 0);
}

//--------------------------- Project Files Mgmt -----------------------------

void Manager::AddVirtualDirectory ( const wxString &virtualDirFullPath )
{
	wxString errMsg;
	bool res = WorkspaceST::Get()->CreateVirtualDirectory ( virtualDirFullPath, errMsg );
	if ( !res ) {
		wxMessageBox(errMsg, _("Error"), wxOK | wxICON_HAND);
		return;
	}
}

void Manager::RemoveVirtualDirectory ( const wxString &virtualDirFullPath )
{
	wxString errMsg;
	wxString project = virtualDirFullPath.BeforeFirst ( wxT ( ':' ) );
	ProjectPtr p = WorkspaceST::Get()->FindProjectByName ( project, errMsg );
	if ( !p ) {
		return;
	}

	// Update symbol tree and database
	wxString vdPath = virtualDirFullPath.AfterFirst ( wxT ( ':' ) );
	wxArrayString files;
	p->GetFilesByVirtualDir ( vdPath, files );
	wxFileName tagsDb = TagsManagerST::Get()->GetDatabase()->GetDatabaseFileName();
	for ( size_t i=0; i<files.Count(); i++ ) {
		TagsManagerST::Get()->Delete ( tagsDb, files.Item ( i ) );
	}

	//and finally, remove the virtual dir from the workspace
	bool res = WorkspaceST::Get()->RemoveVirtualDirectory ( virtualDirFullPath, errMsg );
	if ( !res ) {
		wxMessageBox(errMsg, _("Error"), wxOK | wxICON_HAND);
		return;
	}

	SendCmdEvent ( wxEVT_PROJ_FILE_REMOVED, ( void* ) &files );
}

bool Manager::AddNewFileToProject ( const wxString &fileName, const wxString &vdFullPath, bool openIt )
{
	wxFile file;
	if ( !file.Create ( fileName.GetData(), true ) )
		return false;

	if ( file.IsOpened() ) {
		file.Close();
	}

	return AddFileToProject ( fileName, vdFullPath, openIt );
}

bool Manager::AddFileToProject ( const wxString &fileName, const wxString &vdFullPath, bool openIt )
{
	wxString project;
	project = vdFullPath.BeforeFirst ( wxT ( ':' ) );

	// Add the file to the project
	wxString errMsg;
	bool res = WorkspaceST::Get()->AddNewFile ( vdFullPath, fileName, errMsg );
	if ( !res ) {
		//file or virtual dir does not exist
		return false;
	}

	if ( openIt ) {
		clMainFrame::Get()->GetMainBook()->OpenFile ( fileName, project );
	}

	TagTreePtr ttp;
	if ( project.IsEmpty() == false ) {
		std::vector<CommentPtr> comments;
		if ( TagsManagerST::Get()->GetParseComments() ) {
			ttp = TagsManagerST::Get()->ParseSourceFile ( fileName, &comments );
		} else {
			ttp = TagsManagerST::Get()->ParseSourceFile ( fileName );
		}
		TagsManagerST::Get()->Store ( ttp );
	}

	//send notification command event that files was added to
	//project
	wxArrayString files;
	files.Add ( fileName );
	SendCmdEvent ( wxEVT_PROJ_FILE_ADDED, ( void* ) &files );
	return true;
}

void Manager::AddFilesToProject ( const wxArrayString &files, const wxString &vdFullPath, wxArrayString &actualAdded )
{
	wxString project;
	project = vdFullPath.BeforeFirst ( wxT ( ':' ) );

	// Add the file to the project
	wxString errMsg;
	//bool res = true;
	size_t i=0;

	//try to find this file in the workspace
	for ( i=0; i<files.GetCount(); i++ ) {
		wxString projName = this->GetProjectNameByFile ( files.Item ( i ) );
		//allow adding the file, only if it does not already exist under the current project
		//(it can be already exist under the different project)
		if ( projName.IsEmpty() || projName != project ) {
			actualAdded.Add ( files.Item ( i ) );
		}
	}

	for ( i=0; i<actualAdded.GetCount(); i++ ) {
		Workspace *wsp = WorkspaceST::Get();
		wsp->AddNewFile ( vdFullPath, actualAdded.Item ( i ), errMsg );
	}

	//convert wxArrayString to vector for the ctags api
	std::vector<wxFileName> vFiles;
	for ( size_t i=0; i<actualAdded.GetCount(); i++ ) {
		vFiles.push_back ( actualAdded.Item ( i ) );
	}

	//re-tag the added files
	if ( vFiles.empty() == false ) {
		TagsManagerST::Get()->RetagFiles ( vFiles, true );
	}

	if ( !actualAdded.IsEmpty() ) {
		SendCmdEvent ( wxEVT_PROJ_FILE_ADDED, ( void* ) &actualAdded );
	}
}

bool Manager::RemoveFile ( const wxString &fileName, const wxString &vdFullPath )
{
	wxString project = vdFullPath.BeforeFirst ( wxT ( ':' ) );
	wxFileName absPath ( fileName );
	absPath.MakeAbsolute ( GetProjectCwd ( project ) );

	clMainFrame::Get()->GetMainBook()->ClosePage(absPath.GetFullPath());

	wxString errMsg;
	bool res = WorkspaceST::Get()->RemoveFile ( vdFullPath, fileName, errMsg );
	if ( !res ) {
		wxMessageBox(errMsg, _("Error"), wxOK | wxICON_HAND, clMainFrame::Get());
		return false;
	}

	TagsManagerST::Get()->Delete ( TagsManagerST::Get()->GetDatabase()->GetDatabaseFileName(), absPath.GetFullPath() );
	wxArrayString files(1, &fileName);
	SendCmdEvent(wxEVT_PROJ_FILE_REMOVED, (void*)&files);

	return true;
}

bool Manager::RenameFile(const wxString &origName, const wxString &newName, const wxString &vdFullPath)
{
	// Step: 1
	// remove the file from the workspace (this will erase it from the symbol database and will
	// also close the editor that it is currently opened in (if any)
	if (!RemoveFile(origName, vdFullPath))
		return false;

	// Step: 2
	// Notify the plugins, maybe they want to override the
	// default behavior (e.g. Subversion plugin)
	wxArrayString f;
	f.Add(origName);
	f.Add(newName);

	if(!SendCmdEvent(wxEVT_FILE_RENAMED, (void*)&f)) {
		// rename the file on filesystem
		wxLogNull noLog;
		wxRenameFile(origName, newName);
	}

	// read file to project with the new name
	wxString projName = vdFullPath.BeforeFirst(wxT(':'));
	ProjectPtr proj = GetProject(projName);
	proj->FastAddFile(newName, vdFullPath.AfterFirst(wxT(':')));

	// Step 3: retag the new file
	RetagFile(newName);

	// Step 4: send an event about new file was added
	// to the workspace
	wxArrayString files;
	files.Add(newName);
	SendCmdEvent(wxEVT_PROJ_FILE_ADDED, (void*)&files);

	// Step 5: Change all include files refering to the old
	// file
	if( !IsWorkspaceOpen() ) {
		// if there is no workspace opened, we are done
		return true;
	}

	wxArrayString     workspaceFiles;
	GetWorkspaceFiles(workspaceFiles);
	std::vector<IncludeStatement> includes, matches;

	for(size_t i=0; i<workspaceFiles.GetCount(); i++) {

		// Dont attempt to scan binary files
		// Skip binary files
		if(TagsManagerST::Get()->IsBinaryFile(workspaceFiles.Item(i))) {
			continue;
		}

		// Scan only C/C++/h files
		switch(FileExtManager::GetType(workspaceFiles.Item(i))) {
		case FileExtManager::TypeHeader:
		case FileExtManager::TypeSourceC:
		case FileExtManager::TypeSourceCpp:
			IncludeFinder(workspaceFiles.Item(i).mb_str(wxConvUTF8).data(), includes);
			break;
		default:
			// ignore any other type
			break;
		}
	}

	// Filter non-relevant matches
	wxFileName oldName (origName);
	for(size_t i=0; i<includes.size(); i++) {

		wxString   inclName (includes.at(i).file.c_str(), wxConvUTF8);
		wxFileName inclFn   ( inclName );

		if(oldName.GetFullName() == inclFn.GetFullName()) {
			matches.push_back(includes.at(i));
		}
	}

	// Prompt the user with the list of files which are about to be modified
	wxFileName newFile(newName);
	if(matches.empty() == false) {
		RenameFileDlg dlg(clMainFrame::Get(), newFile.GetFullName(), matches);
		if(dlg.ShowModal() == wxID_OK) {
			matches.clear();
			matches     = dlg.GetMatches();

			for(size_t i=0; i<matches.size(); i++) {
				IncludeStatement includeStatement = matches.at(i);
				wxString editorFileName (includeStatement.includedFrom.c_str(), wxConvUTF8);
				wxString findWhat       (includeStatement.pattern.c_str(),      wxConvUTF8);
				wxString oldIncl        (includeStatement.file.c_str(),         wxConvUTF8);

				// We want to keep the original open/close braces
				// "" or <>
				wxFileName strippedOldInc(oldIncl);
				wxString   replaceWith   (findWhat);

				replaceWith.Replace(strippedOldInc.GetFullName(), newFile.GetFullName());

				LEditor *editor = clMainFrame::Get()->GetMainBook()->OpenFile(editorFileName, wxEmptyString, 0);
				if (editor && (editor->GetFileName().GetFullPath().CmpNoCase(editorFileName) == 0) ) {
					editor->ReplaceAllExactMatch(findWhat, replaceWith);
				}
			}
		}
	}
	return true;
}

bool Manager::MoveFileToVD ( const wxString &fileName, const wxString &srcVD, const wxString &targetVD )
{
	// to move the file between targets, we need to change the file path, we do this
	// by changing the file to be in absolute path related to the src's project
	// and then making it relative to the target's project
	wxString srcProject, targetProject;
	srcProject = srcVD.BeforeFirst ( wxT ( ':' ) );
	targetProject = targetVD.BeforeFirst ( wxT ( ':' ) );
	wxFileName srcProjWd ( GetProjectCwd ( srcProject ), wxEmptyString );

	//set a dir saver point
	wxFileName fn ( fileName );
	wxArrayString files;
	files.Add(fn.GetFullPath());

	//remove the file from the source project
	wxString errMsg;
	bool res = WorkspaceST::Get()->RemoveFile ( srcVD, fileName, errMsg );
	if ( !res ) {
		wxMessageBox(errMsg, _("Error"), wxOK | wxICON_HAND);
		return false;
	}

	// Add the file to the project
	res = WorkspaceST::Get()->AddNewFile ( targetVD, fn.GetFullPath(), errMsg );
	if ( !res ) {
		//file or virtual dir does not exist
		return false;
	}
	return true;
}

void Manager::RetagProject ( const wxString &projectName, bool quickRetag )
{
	ProjectPtr proj = GetProject ( projectName );
	if ( !proj )
		return;

	SetRetagInProgress(true);

	std::vector<wxFileName> projectFiles;
	proj->GetFiles ( projectFiles, true );
	TagsManagerST::Get()->RetagFiles ( projectFiles, quickRetag );
	SendCmdEvent ( wxEVT_FILE_RETAGGED, ( void* ) &projectFiles );
}

void Manager::GetProjectFiles ( const wxString &project, wxArrayString &files )
{
	std::vector<wxFileName> fileNames;
	ProjectPtr p = GetProject ( project );

	if ( p ) {
		p->GetFiles ( fileNames, true );

		//convert std::vector to wxArrayString
		for ( std::vector<wxFileName>::iterator it = fileNames.begin(); it != fileNames.end(); it ++ ) {
			files.Add ( ( *it ).GetFullPath() );
		}
	}
}

wxString Manager::GetProjectNameByFile ( const wxString &fullPathFileName )
{
	wxArrayString projects;
	GetProjectList ( projects );

	std::vector<wxFileName> files;
	for ( size_t i=0; i<projects.GetCount(); i++ ) {
		files.clear();
		ProjectPtr proj = GetProject ( projects.Item ( i ) );
		proj->GetFiles ( files, true );

		for ( size_t xx=0; xx<files.size(); xx++ ) {
			wxString f (files.at ( xx ).GetFullPath());
			if ( f.CmpNoCase(fullPathFileName) == 0 ) {
				return proj->GetName();
			}
		}
	}

	return wxEmptyString;
}


//--------------------------- Project Settings Mgmt -----------------------------

wxString Manager::GetProjectCwd ( const wxString &project ) const
{
	wxString errMsg;
	ProjectPtr p = WorkspaceST::Get()->FindProjectByName ( project, errMsg );
	if ( !p ) {
		return wxGetCwd();
	}

	wxFileName projectFileName ( p->GetFileName() );
	projectFileName.MakeAbsolute();
	return projectFileName.GetPath();
}

ProjectSettingsPtr Manager::GetProjectSettings ( const wxString &projectName ) const
{
	wxString errMsg;
	ProjectPtr proj = WorkspaceST::Get()->FindProjectByName ( projectName, errMsg );
	if ( !proj ) {
		wxLogMessage ( errMsg );
		return NULL;
	}

	return proj->GetSettings();
}

void Manager::SetProjectSettings ( const wxString &projectName, ProjectSettingsPtr settings )
{
	wxString errMsg;
	ProjectPtr proj = WorkspaceST::Get()->FindProjectByName ( projectName, errMsg );
	if ( !proj ) {
		wxLogMessage ( errMsg );
		return;
	}

	proj->SetSettings ( settings );
}

void Manager::SetProjectGlobalSettings ( const wxString &projectName, BuildConfigCommonPtr settings )
{
	wxString errMsg;
	ProjectPtr proj = WorkspaceST::Get()->FindProjectByName ( projectName, errMsg );
	if ( !proj ) {
		wxLogMessage ( errMsg );
		return;
	}

	proj->SetGlobalSettings ( settings );
}

wxString Manager::GetProjectExecutionCommand ( const wxString& projectName, wxString &wd, bool considerPauseWhenExecuting )
{
	BuildConfigPtr bldConf = WorkspaceST::Get()->GetProjBuildConf ( projectName, wxEmptyString );
	if ( !bldConf ) {
		wxLogMessage ( wxT ( "failed to find project configuration for project '" ) + projectName + wxT ( "'" ) );
		return wxEmptyString;
	}

	//expand variables
	wxString cmd = bldConf->GetCommand();
	cmd = ExpandVariables ( cmd, GetProject ( projectName ), clMainFrame::Get()->GetMainBook()->GetActiveEditor() );

	wxString cmdArgs = bldConf->GetCommandArguments();
	cmdArgs = ExpandVariables ( cmdArgs, GetProject ( projectName ), clMainFrame::Get()->GetMainBook()->GetActiveEditor() );

	//execute command & cmdArgs
	wxString execLine ( cmd + wxT ( " " ) + cmdArgs );
	wd = bldConf->GetWorkingDirectory();
	wd = ExpandVariables ( wd, GetProject ( projectName ), clMainFrame::Get()->GetMainBook()->GetActiveEditor() );

	//change directory to the working directory
	if ( considerPauseWhenExecuting ) {
		ProjectPtr proj = GetProject ( projectName );
		OptionsConfigPtr opts = EditorConfigST::Get()->GetOptions();

#if defined(__WXMAC__)

		execLine = opts->GetProgramConsoleCommand();

		wxString tmp_cmd;
		tmp_cmd = wxT("cd \"") + proj->GetFileName().GetPath() + wxT ( "\" && cd \"" ) + wd + wxT ( "\" && " ) + cmd + wxT ( " " ) + cmdArgs;

		execLine.Replace(wxT("$(CMD)"), tmp_cmd);
		execLine.Replace(wxT("$(TITLE)"), cmd + wxT ( " " ) + cmdArgs);

#elif defined(__WXGTK__)

		//set a console to the execute target
		wxString term;
		term = opts->GetProgramConsoleCommand();
		term.Replace(wxT("$(TITLE)"), cmd + wxT ( " " ) + cmdArgs);

		// build the command
		wxString command;
		if ( bldConf->GetPauseWhenExecEnds() ) {
			wxString ld_lib_path;
			wxFileName exePath ( wxStandardPaths::Get().GetExecutablePath() );
			wxFileName exeWrapper ( exePath.GetPath(), wxT ( "codelite_exec" ) );

			if ( wxGetEnv ( wxT ( "LD_LIBRARY_PATH" ), &ld_lib_path ) && ld_lib_path.IsEmpty() == false ) {
				command << wxT ( "/bin/sh -f " ) << exeWrapper.GetFullPath() << wxT ( " LD_LIBRARY_PATH=" ) << ld_lib_path << wxT ( " " );
			} else {
				command << wxT ( "/bin/sh -f " ) << exeWrapper.GetFullPath() << wxT ( " " );
			}
		}

		command << execLine;
		term.Replace(wxT("$(CMD)"), command);
		execLine = term;

#elif defined (__WXMSW__)

		if ( bldConf->GetPauseWhenExecEnds() ) {
			execLine.Prepend ( wxT ( "le_exec.exe " ) );
		}
#endif
	}
	return execLine;
}


//--------------------------- Top Level Pane Management -----------------------------

bool Manager::IsPaneVisible ( const wxString& pane_name )
{
	wxAuiPaneInfo &info = clMainFrame::Get()->GetDockingManager().GetPane ( pane_name );
	if ( info.IsOk() && info.IsShown() ) {
		return true;
	}
	return false;
}

bool Manager::DoFindDockInfo(const wxString &saved_perspective, const wxString &dock_name, wxString &dock_info)
{
	// search for the 'Output View' perspective
	wxArrayString panes = wxStringTokenize(saved_perspective, wxT("|"), wxTOKEN_STRTOK);
	for (size_t i=0; i<panes.GetCount(); i++) {
		if ( panes.Item(i).StartsWith(dock_name) ) {
			dock_info = panes.Item(i);
			return true;
		}
	}
	return false;
}

void Manager::ToggleOutputPane(bool hide)
{
	static wxString saved_dock_info;
	wxAuiManager *aui = &clMainFrame::Get()->GetDockingManager();

	if ( aui ) {
		wxAuiPaneInfo &pane_info = aui->GetPane(wxT("Output View"));
		wxString dock_info ( wxString::Format(wxT("dock_size(%d,%d,%d)"), pane_info.dock_direction, pane_info.dock_layer, pane_info.dock_row) );
		if ( hide ) {
			if ( pane_info.IsShown() ) {
				clMainFrame::Get()->Freeze();

				DoFindDockInfo(aui->SavePerspective(), dock_info, saved_dock_info);
				DockablePaneMenuManager::HackHidePane(true,pane_info,aui);

				clMainFrame::Get()->Thaw();
			}


		} else {
			// Show it
			if ( pane_info.IsShown() == false ) {
				clMainFrame::Get()->Freeze();

				if ( saved_dock_info.IsEmpty() ) {

					DockablePaneMenuManager::HackShowPane(pane_info,aui);

				} else {
					wxString auiPerspective = aui->SavePerspective();
					if ( auiPerspective.Find(dock_info) == wxNOT_FOUND ) {
						// the dock_info does not exist
						auiPerspective << saved_dock_info << wxT("|");
						aui->LoadPerspective(auiPerspective, false);
						DockablePaneMenuManager::HackShowPane(pane_info,aui);
					} else {
						DockablePaneMenuManager::HackShowPane(pane_info,aui);
					}
				}
				clMainFrame::Get()->Thaw();
			}
		}
	}
}



bool Manager::ShowOutputPane ( wxString focusWin, bool commit )
{
	clMainFrame::Get()->ViewPane(wxT("Output View"), true);
	
	// make the output pane visible
	ToggleOutputPane( false );

	// set the selection to focus win
	OutputPane *pane = clMainFrame::Get()->GetOutputPane();
	size_t index(Notebook::npos);
	for(size_t i=0; i<pane->GetNotebook()->GetPageCount(); i++) {
		if(pane->GetNotebook()->GetPageText(i) == focusWin) {
			index = i;
			break;
		}
	}

	if ( index != Notebook::npos && index != pane->GetNotebook()->GetSelection() ) {
		wxWindow *focus = wxWindow::FindFocus();
		LEditor *editor = dynamic_cast<LEditor*>( focus );
		pane->GetNotebook()->SetSelection ( ( size_t ) index );
		if (editor) {
			editor->SetFocus();
		}
	}

#if wxVERSION_NUMBER >= 2900
	// This is needed in >=wxGTK-2.9, otherwise the current editor sometimes doesn't notice that the output pane has appeared
	// resulting in an area at the bottom that can't be scrolled to
	clMainFrame::Get()->SendSizeEvent(wxSEND_EVENT_POST);
#endif

	return true;
}

void Manager::ShowDebuggerPane ( bool show )
{
	// make the output pane visible
	wxArrayString dbgPanes;
	dbgPanes.Add ( wxT("Debugger") );
	dbgPanes.Add ( wxGetTranslation(DebuggerPane::LOCALS) );
	dbgPanes.Add ( wxGetTranslation(DebuggerPane::FRAMES) );
	dbgPanes.Add ( wxGetTranslation(DebuggerPane::WATCHES) );
	dbgPanes.Add ( wxGetTranslation(DebuggerPane::BREAKPOINTS) );
	dbgPanes.Add ( wxGetTranslation(DebuggerPane::THREADS) );
	dbgPanes.Add ( wxGetTranslation(DebuggerPane::MEMORY) );
	dbgPanes.Add ( wxGetTranslation(DebuggerPane::ASCII_VIEWER) );

	wxAuiManager *aui = &clMainFrame::Get()->GetDockingManager();
	if ( show ) {

		for ( size_t i=0; i<dbgPanes.GetCount(); i++ ) {
			wxAuiPaneInfo &info = clMainFrame::Get()->GetDockingManager().GetPane ( dbgPanes.Item ( i ) );
			// show all debugger related panes
			if ( info.IsOk() && !info.IsShown() ) {
				DockablePaneMenuManager::DockablePaneMenuManager::HackShowPane(info,aui);
			}
		}

	} else {

		// hide all debugger related panes
		for ( size_t i=0; i<dbgPanes.GetCount(); i++ ) {
			wxAuiPaneInfo &info = clMainFrame::Get()->GetDockingManager().GetPane ( dbgPanes.Item ( i ) );
			// show all debugger related panes
			if ( info.IsOk() && info.IsShown() ) {
				DockablePaneMenuManager::HackHidePane(true,info,aui);
			}
		}
	}

}

void Manager::ShowWorkspacePane ( wxString focusWin, bool commit )
{
	// make the output pane visible
	wxAuiPaneInfo &info = clMainFrame::Get()->GetDockingManager().GetPane(wxT("Workspace View"));
	if ( info.IsOk() && !info.IsShown() ) {
		info.Show();
		if ( commit ) {
			clMainFrame::Get()->GetDockingManager().Update();
		}
	}

	// set the selection to focus win
	Notebook *book = clMainFrame::Get()->GetWorkspacePane()->GetNotebook();
	int index = book->GetPageIndex ( focusWin );
	if ( index != wxNOT_FOUND && ( size_t ) index != book->GetSelection() ) {
		book->SetSelection ( ( size_t ) index );
	}
}

void Manager::HidePane ( const wxString &paneName, bool commit )
{
	wxAuiPaneInfo &info = clMainFrame::Get()->GetDockingManager().GetPane ( paneName );
	if ( info.IsOk() && info.IsShown() ) {
		wxAuiManager *aui = &clMainFrame::Get()->GetDockingManager();
		DockablePaneMenuManager::HackHidePane(commit,info,aui);
	}
}

void Manager::TogglePanes()
{
	static bool toggled = false;
	static wxString savedLayout;

	// list of panes to be toggled on and off
	static wxArrayString panes;

	if ( !toggled ) {
		savedLayout = clMainFrame::Get()->GetDockingManager().SavePerspective();
		panes.Clear();
		// create the list of panes to be tested
		wxArrayString candidates;
		candidates.Add ( wxT("Output View") );
		candidates.Add ( wxT("Workspace View") );
		candidates.Add ( wxT("Debugger") );

		// add the detached tabs list
		wxArrayString dynamicPanes = clMainFrame::Get()->GetDockablePaneMenuManager()->GetDeatchedPanesList();
		for ( size_t i=0; i<dynamicPanes.GetCount(); i++ ) {
			candidates.Add ( dynamicPanes.Item ( i ) );
		}

		for ( size_t i=0; i<candidates.GetCount(); i++ ) {
			wxAuiPaneInfo info;
			info = clMainFrame::Get()->GetDockingManager().GetPane ( candidates.Item ( i ) );
			if ( info.IsOk() && info.IsShown() ) {
				panes.Add ( candidates.Item ( i ) );
			}
		}


		// hide the matched panes
		for ( size_t i=0; i<panes.GetCount(); i++ ) {
			HidePane ( panes.Item ( i ), false );
		}

		//update changes
		clMainFrame::Get()->GetDockingManager().Update();
		toggled = true;

	} else {
		clMainFrame::Get()->GetDockingManager().LoadPerspective(savedLayout);
		toggled = false;
		savedLayout.Clear();
	}
}


//--------------------------- Menu and Accelerator Mmgt -----------------------------

void Manager::UpdateMenuAccelerators()
{
	MenuItemDataMap menuMap, defAccelMap;

	wxArrayString files;
	DoGetAccelFiles ( files );

	// load user accelerators map
	LoadAcceleratorTable ( files, menuMap );

	// load the default accelerator map
	GetDefaultAcceleratorMap ( defAccelMap );

	// loop over default accelerators map, and search for items that does not exist in the user's list
	std::map< int, MenuItemData >::iterator it = defAccelMap.begin();
	for ( ; it != defAccelMap.end(); it++ ) {
		if ( menuMap.find ( it->first ) == menuMap.end() ) {
			// this item does not exist in the users accelerators
			// probably a new accelerator that was added to the default
			// files directly via update/manually modified it
			menuMap[it->first] = it->second;
		}
	}

	wxMenuBar *bar = clMainFrame::Get()->GetMenuBar();

	std::vector< wxAcceleratorEntry > accelVec;
	size_t count = bar->GetMenuCount();
	for ( size_t i=0; i< count; i++ ) {
		wxMenu * menu = bar->GetMenu ( i );
		UpdateMenu ( menu, menuMap, accelVec );
	}

	//In case we still have items to map, map them. this can happen for items
	//which exist in the list but does not have any menu associated to them in the menu bar (e.g. C++ menu)
	if ( menuMap.empty() == false ) {
//		wxString msg;
//		msg << wxT("Info: UpdateMenuAccelerators: There are still ") << menuMap.size() << wxT(" un-mapped item(s)");
//		wxLogMessage(msg);

		MenuItemDataMap::iterator iter = menuMap.begin();
		for ( ; iter != menuMap.end(); iter++ ) {
			MenuItemData itemData = iter->second;

			wxString txt;
			txt << itemData.action;
			if ( itemData.accel.IsEmpty() == false ) {
				txt << wxT ( "\t" ) << itemData.accel;
			}

			wxAcceleratorEntry* a = wxAcceleratorEntry::Create ( txt );
			if ( a ) {
				long commandId ( 0 );
				itemData.id.ToLong ( &commandId );

				//use the resource ID
				if ( commandId == 0 ) {
					commandId = wxXmlResource::GetXRCID ( itemData.id );
				}

				a->Set ( a->GetFlags(), a->GetKeyCode(), commandId );
				accelVec.push_back ( *a );
				delete a;
			}
		}
	}

	//update the accelerator table of the main frame
	wxAcceleratorEntry *entries = new wxAcceleratorEntry[accelVec.size() ];
	for ( size_t i=0; i < accelVec.size(); i++ ) {
		entries[i] = accelVec[i];
	}

	wxAcceleratorTable table ( accelVec.size(), entries );

	//update the accelerator table
	clMainFrame::Get()->SetAcceleratorTable ( table );
	delete [] entries;
}

void Manager::LoadAcceleratorTable ( const wxArrayString &files, MenuItemDataMap &accelMap )
{
	wxString content;
	for ( size_t i=0; i<files.GetCount(); i++ ) {
		wxString tmpContent;
		if ( ReadFileWithConversion ( files.Item ( i ), tmpContent ) ) {
			wxLogMessage ( wxString::Format ( wxT ( "Loading accelerators from '%s'" ), files.Item ( i ).c_str() ) );
			content << wxT ( "\n" ) << tmpContent;
		}
	}

	wxArrayString lines = wxStringTokenize ( content, wxT ( "\r\n" ), wxTOKEN_STRTOK );
	for ( size_t i = 0; i < lines.GetCount(); i ++ ) {
		wxString line = lines.Item ( i );

		line.Trim(false).Trim();
		if ( line.IsEmpty() ) {
			continue;
		}

		MenuItemData item;

		item.id = line.BeforeFirst ( wxT ( '|' ) );
		line = line.AfterFirst ( wxT ( '|' ) );

		item.parent = line.BeforeFirst ( wxT ( '|' ) );
		line = line.AfterFirst ( wxT ( '|' ) );

		item.action = line.BeforeFirst ( wxT ( '|' ) );
		line = line.AfterFirst ( wxT ( '|' ) );

		item.accel = line.BeforeFirst ( wxT ( '|' ) );
		line = line.AfterFirst ( wxT ( '|' ) );

		accelMap[wxXmlResource::GetXRCID(item.id)] = item;
	}
}

void Manager::UpdateMenu ( wxMenu *menu, MenuItemDataMap &accelMap, std::vector< wxAcceleratorEntry > &accelVec )
{
	wxMenuItemList items = menu->GetMenuItems();
	wxMenuItemList::iterator iter = items.begin();
	for ( ; iter != items.end(); iter++ ) {
		wxMenuItem *item =  *iter;
		if ( item->GetSubMenu() ) {
			UpdateMenu ( item->GetSubMenu(), accelMap, accelVec );
		} else {

			if ( item->GetId() == wxID_SEPARATOR ) {
				continue;
			}

			//search for this item in the accelMap
			int id = item->GetId();
			if ( (id != wxID_ANY) && accelMap.find(id) != accelMap.end() ) {
				MenuItemData item_data = accelMap.find(id)->second;

				wxString txt;
				txt << StripAccel ( item->GetItemLabel() );

				//set the new accelerator
				if ( item_data.accel.IsEmpty() == false ) {
					txt << wxT ( "\t" ) << item_data.accel;
				}

				txt.Replace ( wxT ( "_" ), wxT ( "&" ) );
				item->SetItemLabel ( txt );

				wxAcceleratorEntry* a = wxAcceleratorEntry::Create ( txt );
				if ( a ) {
					a->Set ( a->GetFlags(), a->GetKeyCode(), item->GetId() );
					accelVec.push_back ( *a );
					delete a;
				}

				//remove this entry from the map
				accelMap.erase(id);
			}
		}
	}
}


void Manager::GetDefaultAcceleratorMap ( MenuItemDataMap& accelMap )
{
	//use the default settings
	wxString fileName = GetInstallDir() + wxT ( "/config/accelerators.conf.default" );
	wxArrayString files;
	files.Add ( fileName );

	// append the content of all '*.accelerators' from the plugins
	// resources table
#ifdef __WXGTK__
	wxString pluginsDir(PLUGINS_DIR, wxConvUTF8);
#else
	wxString pluginsDir(GetInstallDir() + wxT( "/plugins" ));
#endif

	wxDir::GetAllFiles ( pluginsDir + wxT ( "/resources/" ), &files, wxT ( "*.accelerators" ), wxDIR_FILES );
	LoadAcceleratorTable ( files, accelMap );
}

void Manager::GetAcceleratorMap ( MenuItemDataMap& accelMap )
{
	wxArrayString files;
	DoGetAccelFiles ( files );
	LoadAcceleratorTable ( files, accelMap );
}

void Manager::DoGetAccelFiles ( wxArrayString& files )
{
	//try to locate the user's settings
	wxFileName fileName ( wxStandardPaths::Get().GetUserDataDir() + wxFileName::GetPathSeparator() + wxT("config/accelerators.conf") );
	if ( !fileName.FileExists() ) {

		//use the default settings
		fileName = wxFileName(GetInstallDir() + wxT("/config/accelerators.conf.default"));
		files.Add ( fileName.GetFullPath() );

#ifdef __WXGTK__
		wxString pluginsDir = wxString::From8BitData(PLUGINS_DIR);
#else
		wxString pluginsDir(GetInstallDir() + wxT( "/plugins" ));
#endif
		// append the content of all '*.accelerators' from the plugins
		// resources table
		wxDir::GetAllFiles (pluginsDir + wxT ( "/resources/" ), &files, wxT ( "*.accelerators" ), wxDIR_FILES );
	} else {
		files.Add ( fileName.GetFullPath() );
	}
}

void Manager::DumpMenu ( wxMenu *menu, const wxString &label, wxString &content )
{
#if 0
	wxMenuItemList items = menu->GetMenuItems();
	wxMenuItemList::iterator iter = items.begin();
	for ( ; iter != items.end(); iter++ ) {
		wxMenuItem *item = *iter;
		if ( item->GetSubMenu() ) {
			DumpMenu ( item->GetSubMenu(), label + wxT ( "::" ) + item->GetLabel(), content );
			continue;
		}
		if ( item->GetId() == wxID_SEPARATOR ) {
			continue;
		} else if ( item->GetId() >= RecentFilesSubMenuID && item->GetId() <= RecentFilesSubMenuID + 10 ) {
			continue;
		} else if ( item->GetId() >= RecentWorkspaceSubMenuID && item->GetId() <= RecentWorkspaceSubMenuID + 10 ) {
			continue;
		}
		//dump the content of this menu item
		content << item->GetId() << wxT ( "|" )
		        << label << wxT ( "|" )
		        << wxMenuItem::GetLabelText(item->GetItemLabel()) << wxT ( "|" );
		if ( item->GetAccel() ) {
			content << item->GetAccel()->ToString();
		}
		content << wxT ( "\n" );
	}
#endif
}


//--------------------------- Run Program (No Debug) -----------------------------

bool Manager::IsProgramRunning() const
{
	return ( m_asyncExeCmd && m_asyncExeCmd->IsBusy() );
}

void Manager::ExecuteNoDebug ( const wxString &projectName )
{
	//an instance is already running
	if ( m_asyncExeCmd && m_asyncExeCmd->IsBusy() ) {
		return;
	}
	wxString wd;

	// we call it here once for the 'wd'
	EnvSetter env1(NULL, NULL, projectName);
	wxString execLine = GetProjectExecutionCommand ( projectName, wd, true );
	ProjectPtr proj = GetProject ( projectName );

	DirSaver ds;

	//print the current directory
	::wxSetWorkingDirectory ( proj->GetFileName().GetPath() );

	//now set the working directory according to working directory field from the
	//project settings
	::wxSetWorkingDirectory ( wd );

	//execute the command line
	//the a sync command is a one time executable object,
	m_asyncExeCmd = new AsyncExeCmd (clMainFrame::Get()->GetOutputPane()->GetOutputWindow());

	//execute the program:
	//- no hiding the console
	//- no redirection of the stdin/out
	EnvSetter env(NULL, NULL, projectName);

	// call it again here to get the actual exection line - we do it here since
	// the environment has been applied
	execLine = GetProjectExecutionCommand ( projectName, wd, true );
	m_asyncExeCmd->Execute ( execLine, false, false );

	if ( m_asyncExeCmd->GetProcess() ) {
		m_asyncExeCmd->GetProcess()->Connect ( wxEVT_END_PROCESS, wxProcessEventHandler ( Manager::OnProcessEnd ), NULL, this );
	}
}

void Manager::KillProgram()
{
	if ( !IsProgramRunning() )
		return;

	m_asyncExeCmd->Terminate();
}

void Manager::OnProcessEnd ( wxProcessEvent &event )
{
	m_asyncExeCmd->ProcessEnd ( event );
	m_asyncExeCmd->GetProcess()->Disconnect ( wxEVT_END_PROCESS, wxProcessEventHandler ( Manager::OnProcessEnd ), NULL, this );

#ifdef __WXMSW__
	// On Windows, sometimes killing the DOS Windows, does not kill the children
	m_asyncExeCmd->Terminate();
#endif

	delete m_asyncExeCmd;
	m_asyncExeCmd = NULL;

	//return the focus back to the editor
	if ( clMainFrame::Get()->GetMainBook()->GetActiveEditor() ) {
		clMainFrame::Get()->GetMainBook()->GetActiveEditor()->SetActive();
	}
}


//--------------------------- Debugger Support -----------------------------

static void DebugMessage ( wxString msg )
{
	clMainFrame::Get()->GetDebuggerPane()->GetDebugWindow()->AppendLine(msg);
}

void Manager::UpdateDebuggerPane()
{
	DebuggerPane *pane = clMainFrame::Get()->GetDebuggerPane();

#if CL_USE_NATIVEBOOK
	DoUpdateDebuggerTabControl(pane->GetNotebook()->GetCurrentPage());
#else

	std::set<wxAuiTabCtrl*> tabControls = pane->GetNotebook()->GetAllTabControls();
	std::set<wxAuiTabCtrl*>::iterator iter = tabControls.begin();

	for(; iter != tabControls.end(); iter++) {
		int activePageId = (*iter)->GetActivePage();
		if(activePageId != wxNOT_FOUND) {
			DoUpdateDebuggerTabControl((*iter)->GetPage((size_t)activePageId).window);
		}
	}
#endif
}

void Manager::DoUpdateDebuggerTabControl(wxWindow* curpage)
{
	//Update the debugger pane
	DebuggerPane *pane = clMainFrame::Get()->GetDebuggerPane();

	// make sure that the debugger pane is visible
	if(!IsPaneVisible ( wxT("Debugger") ))
		return;

	if ( curpage == ( wxWindow* ) pane->GetBreakpointView() || IsPaneVisible ( DebuggerPane::BREAKPOINTS) ) {
		pane->GetBreakpointView()->Initialize();
	}

	IDebugger *dbgr = DebuggerMgr::Get().GetActiveDebugger();
	if ( dbgr && dbgr->IsRunning() && DbgCanInteract() ) {

		//--------------------------------------------------------------------
		// Lookup the selected tab in the debugger notebook and update it
		// once this is done, we need to go through the list of detached panes
		// and scan for another debugger tabs which are visible and need to be
		// updated
		//--------------------------------------------------------------------

		if ( curpage == pane->GetLocalsTable() || IsPaneVisible (wxGetTranslation( DebuggerPane::LOCALS) ) ) {

			//update the locals tree
			pane->GetLocalsTable()->UpdateVariableObjects();
			dbgr->QueryLocals();
		}

		if ( curpage == pane->GetWatchesTable() || IsPaneVisible ( wxGetTranslation(DebuggerPane::WATCHES) ) ) {
			pane->GetWatchesTable()->RefreshValues();

		}
		if ( curpage == ( wxWindow* ) pane->GetFrameListView() || IsPaneVisible ( wxGetTranslation(DebuggerPane::FRAMES) ) ) {

			//update the stack call
			dbgr->ListFrames();

		}
		if ( curpage == ( wxWindow* ) pane->GetBreakpointView() || IsPaneVisible ( wxGetTranslation(DebuggerPane::BREAKPOINTS) ) ) {

			// update the breakpoint view
			pane->GetBreakpointView()->Initialize();

		}
		if ( curpage == ( wxWindow* ) pane->GetThreadsView() || IsPaneVisible ( wxGetTranslation(DebuggerPane::THREADS) ) ) {

			// update the thread list
			dbgr->ListThreads();
		}

		if ( curpage == ( wxWindow* ) pane->GetMemoryView() || IsPaneVisible ( wxGetTranslation(DebuggerPane::MEMORY) ) ) {

			// Update the memory view tab
			MemoryView *memView = pane->GetMemoryView();
			if ( memView->GetExpression().IsEmpty() == false ) {

				wxString output;
				dbgr->WatchMemory ( memView->GetExpression(), memView->GetSize() );
			}
		}
	}
}

void Manager::SetMemory ( const wxString& address, size_t count, const wxString &hex_value )
{
	IDebugger *dbgr = DebuggerMgr::Get().GetActiveDebugger();
	if ( dbgr && dbgr->IsRunning() && DbgCanInteract() ) {
		dbgr->SetMemory ( address, count, hex_value );
	}
}


// Debugger API

void Manager::DbgStart ( long attachPid )
{
	//set the working directory to the project directory
	DirSaver            ds;
	wxString            errMsg;
	wxString            output;
	wxString            debuggerName;
	wxString            exepath;
	wxString            wd;
	wxString            args;
	BuildConfigPtr      bldConf;
	ProjectPtr          proj;
	DebuggerStartupInfo startup_info;
	long                PID ( -1 );

#if defined(__WXGTK__)
	wxString where;
	if ( !ExeLocator::Locate ( wxT ( "xterm" ), where ) ) {
		wxMessageBox ( _( "Failed to locate 'xterm' application required by CodeLite, please install it and try again!" ), _("CodeLite" ), wxOK|wxCENTER|wxICON_WARNING, clMainFrame::Get() );
		return;
	}
#endif

	if ( attachPid == 1 ) { //attach to process
		AttachDbgProcDlg *dlg = new AttachDbgProcDlg ( NULL );
		if ( dlg->ShowModal() == wxID_OK ) {
			wxString processId = dlg->GetProcessId();
			exepath   = dlg->GetExeName();
			debuggerName = dlg->GetDebugger();
			DebuggerMgr::Get().SetActiveDebugger ( debuggerName );

			processId.ToLong ( &PID );
			if ( exepath.IsEmpty() == false ) {
				wxFileName fn ( exepath );
				wxSetWorkingDirectory ( fn.GetPath() );
				exepath = fn.GetFullName();

			}
			dlg->Destroy();

			startup_info.pid = PID;
		} else {
			dlg->Destroy();
			return;
		}
	}

	if ( attachPid == wxNOT_FOUND ) {
		//need to debug the current project
		proj = WorkspaceST::Get()->FindProjectByName ( GetActiveProjectName(), errMsg );
		if ( proj ) {
			wxSetWorkingDirectory ( proj->GetFileName().GetPath() );
			bldConf = WorkspaceST::Get()->GetProjBuildConf ( proj->GetName(), wxEmptyString );
			if ( bldConf ) {
				debuggerName = bldConf->GetDebuggerType();
				DebuggerMgr::Get().SetActiveDebugger ( debuggerName );
			}
		}

		startup_info.project = GetActiveProjectName();
	}

	//make sure we have an active debugger
	IDebugger *dbgr = DebuggerMgr::Get().GetActiveDebugger();
	if ( !dbgr ) {
		//No debugger available,
		wxString message;
		message << _( "Failed to launch debugger '" ) << debuggerName << _( "': debugger not loaded\n" );
		message << _( "Make sure that you have an open workspace and that the active project is of type 'Executable'" );
		wxMessageBox ( message,_("CodeLite"), wxOK|wxICON_WARNING );
		return;
	}
	startup_info.debugger = dbgr;

	if ( dbgr->IsRunning() ) {

		//debugger is already running, so issue a 'cont' command
		clMainFrame::Get()->GetDebuggerPane()->GetLocalsTable()->ResetTableColors();
		clMainFrame::Get()->GetDebuggerPane()->GetWatchesTable()->ResetTableColors();

		dbgr->Continue();
		return;
	}

	// Save the current layout
	GetPerspectiveManager().SavePerspective(NORMAL_LAYOUT);

	//set the debugger information
	DebuggerInformation dinfo;
	DebuggerMgr::Get().GetDebuggerInformation ( debuggerName, dinfo );

	// if user override the debugger path, apply it
	if ( bldConf ) {
		wxString userDebuggr = bldConf->GetDebuggerPath();
		userDebuggr.Trim().Trim ( false );
		if ( userDebuggr.IsEmpty() == false ) {
			dinfo.path = userDebuggr;
		}
	}
	// read the console command
	dinfo.consoleCommand = EditorConfigST::Get()->GetOptions()->GetProgramConsoleCommand();
	dbgr->SetDebuggerInformation ( dinfo );

	if ( attachPid == wxNOT_FOUND ) {
		exepath = bldConf->GetCommand();

		// Get the debugging arguments.
		if(bldConf->GetUseSeparateDebugArgs()) {
			args = bldConf->GetDebugArgs();
		} else {
			args = bldConf->GetCommandArguments();
		}

		exepath.Prepend ( wxT ( "\"" ) );
		exepath.Append ( wxT ( "\"" ) );

		// Apply environment variables
		EnvSetter env(NULL, NULL, proj ? proj->GetName() : wxT(""));
		wd = bldConf->GetWorkingDirectory();

		// Expand variables before passing them to the debugger
		wd = ExpandVariables ( wd, proj, clMainFrame::Get()->GetMainBook()->GetActiveEditor() );
		exepath = ExpandVariables ( exepath, proj, clMainFrame::Get()->GetMainBook()->GetActiveEditor() );
	}

	//get the debugger path to execute
	DebuggerInformation dbginfo;
	DebuggerMgr::Get().GetDebuggerInformation ( debuggerName, dbginfo );

	// if user override the debugger path, apply it
	if ( bldConf ) {
		wxString userDebuggr = bldConf->GetDebuggerPath();
		userDebuggr.Trim().Trim ( false );
		if ( userDebuggr.IsEmpty() == false ) {
			dbginfo.path = userDebuggr;
		}
	}

	wxString dbgname = dbginfo.path;
	dbgname = EnvironmentConfig::Instance()->ExpandVariables ( dbgname, true );

	//set ourselves as the observer for the debugger class
	dbgr->SetObserver ( this );

	// Set the 'Is remote debugging' flag'
	dbgr->SetIsRemoteDebugging(bldConf && bldConf->GetIsDbgRemoteTarget() && PID == wxNOT_FOUND);

	if(proj) {
		dbgr->SetProjectName(proj->GetName());
	} else {
		dbgr->SetProjectName(wxT(""));
	}

	// Loop through the open editors and let each editor
	// a chance to update the debugger manager with any line
	// changes (i.e. file was edited and breakpoints were moved)
	// this loop must take place before the debugger start up
	// or else call to UpdateBreakpoint() will yield an attempt to
	// actually add the breakpoint before Run() is called - this can
	// be a problem when adding breakpoint to dll files.
	if ( wxNOT_FOUND == attachPid ) {
		clMainFrame::Get()->GetMainBook()->UpdateBreakpoints();
	}

	//We can now get all the gathered breakpoints from the manager
	std::vector<BreakpointInfo> bps;

	// since files may have been updated and the breakpoints may have been moved,
	// delete all the information
	GetBreakpointsMgr()->GetBreakpoints ( bps );
	// Take the opportunity to store them in the pending array too
	GetBreakpointsMgr()->SetPendingBreakpoints( bps );

	// notify plugins that we're about to start debugging
	if (SendCmdEvent(wxEVT_DEBUG_STARTING, &startup_info))
		// plugin stopped debugging
		return;

	wxString title;
	title << _("Debugging: ") << exepath << wxT(" ") << args;

	// read
	wxArrayString dbg_cmds;
	if ( attachPid == wxNOT_FOUND ) {
		//it is now OK to start the debugger...
		dbg_cmds = wxStringTokenize ( bldConf->GetDebuggerStartupCmds(), wxT ( "\n" ), wxTOKEN_STRTOK );
		if ( !dbgr->Start ( dbgname, exepath, wd, bps, dbg_cmds, clMainFrame::Get()->StartTTY(title) ) ) {
			wxString errMsg;
			errMsg << _("Failed to initialize debugger: ") << dbgname << wxT("\n");
			DebugMessage ( errMsg );
			return;
		}
	} else {
		//Attach to process...
		if ( !dbgr->Start ( dbgname, exepath, PID, wxT(""), bps, dbg_cmds, clMainFrame::Get()->StartTTY(title) ) ) {
			wxString errMsg;
			errMsg << _( "Failed to initialize debugger: " ) << dbgname << wxT( "\n" );
			DebugMessage ( errMsg );
			return;
		}
	}

	// notify plugins that the debugger just started
	SendCmdEvent(wxEVT_DEBUG_STARTED, &startup_info);

	// Now the debugger has been fed the breakpoints, re-Initialise the breakpt view,
	// so that it uses debugger_ids instead of internal_ids
	// Hmm. The above comment is probably no longer true; but it'll do no harm to Initialise() anyway
	clMainFrame::Get()->GetDebuggerPane()->GetBreakpointView()->Initialize();

	// Initialize the 'Locals' table. We do this for performane reason so we
	// wont need to read from the XML each time perform 'next' step
	clMainFrame::Get()->GetDebuggerPane()->GetLocalsTable()->Initialize();

	// let the active editor get the focus
	LEditor *editor = clMainFrame::Get()->GetMainBook()->GetActiveEditor();
	if ( editor ) {
		editor->SetActive();
	}

	// mark that we are waiting for the first GotControl()
	DebugMessage ( output );
	DebugMessage ( _( "Debug session started successfully!\n" ) );

	if ( dbgr->GetIsRemoteDebugging() ) {

		// debugging remote target
		wxString comm;
		wxString port = bldConf->GetDbgHostPort();
		wxString host = bldConf->GetDbgHostName();

		comm << host;

		host = host.Trim().Trim ( false );
		port = port.Trim().Trim ( false );

		if ( port.IsEmpty() == false ) {
			comm << wxT ( ":" ) << port;
		}

		dbgr->Run ( args, comm );

	} else if ( attachPid == wxNOT_FOUND ) {

		// debugging local target
		dbgr->Run ( args, wxEmptyString );
	}

	GetPerspectiveManager().LoadPerspective(DEBUG_LAYOUT);
}

void Manager::DbgStop()
{
	GetPerspectiveManager().SavePerspective(DEBUG_LAYOUT);
	GetPerspectiveManager().LoadPerspective(NORMAL_LAYOUT);

	if ( m_watchDlg ) {
		m_watchDlg->Destroy();
		m_watchDlg = NULL;
	}

	// remove all debugger markers
	DbgUnMarkDebuggerLine();

	// Mark the debugger as non interactive
	m_dbgCanInteract = false;

	// Keep the current watches for the next debug session
	m_dbgWatchExpressions = clMainFrame::Get()->GetDebuggerPane()->GetWatchesTable()->GetExpressions();

	//clear the debugger pane
	clMainFrame::Get()->GetDebuggerPane()->Clear();

	// Notify the breakpoint manager that the debugger has stopped
	GetBreakpointsMgr()->DebuggerStopped();

	clMainFrame::Get()->GetDebuggerPane()->GetBreakpointView()->Initialize();

	// Clear the ascii viewer
	clMainFrame::Get()->GetDebuggerPane()->GetAsciiViewer()->UpdateView(wxT(""), wxT(""));
	// update toolbar state
	UpdateStopped();

	IDebugger *dbgr =  DebuggerMgr::Get().GetActiveDebugger();
	if ( !dbgr ) {
		return;
	}

	m_dbgCurrentFrameInfo.Clear();
	if ( !dbgr->IsRunning() ) {
		// notify plugins that the debugger stopped
		SendCmdEvent(wxEVT_DEBUG_ENDED);
		return;
	}

	// notify plugins that the debugger is about to be stopped
	SendCmdEvent(wxEVT_DEBUG_ENDING);

	if(dbgr->IsRunning())
		dbgr->Stop();

	DebuggerMgr::Get().SetActiveDebugger ( wxEmptyString );
	DebugMessage ( _( "Debug session ended\n" ) );

	// notify plugins that the debugger stopped
	SendCmdEvent(wxEVT_DEBUG_ENDED);
}

void Manager::DbgMarkDebuggerLine ( const wxString &fileName, int lineno )
{
	DbgUnMarkDebuggerLine();
	if ( lineno < 0 ) {
		return;
	}

	//try to open the file
	wxFileName fn ( fileName );
	LEditor *editor = clMainFrame::Get()->GetMainBook()->GetActiveEditor();
	if ( editor && editor->GetFileName().GetFullPath().CmpNoCase(fn.GetFullPath()) == 0 && lineno > 0) {
		editor->HighlightLine ( lineno   );
		editor->GotoLine      ( lineno-1 );
		editor->EnsureVisible ( lineno-1 );
	} else if (clMainFrame::Get()->GetMainBook()->OpenFile ( fn.GetFullPath(), wxEmptyString, lineno-1, wxNOT_FOUND) && lineno > 0) {
		editor = clMainFrame::Get()->GetMainBook()->GetActiveEditor();
		if ( editor ) {
			editor->HighlightLine ( lineno );
		}
	}
}

void Manager::DbgUnMarkDebuggerLine()
{
	//remove all debugger markers from all editors
	clMainFrame::Get()->GetMainBook()->UnHighlightAll();
}

void Manager::DbgDoSimpleCommand ( int cmd )
{
	// Hide tooltip dialog if its ON
	if(GetDebuggerTip() && GetDebuggerTip()->IsShown()) {
		GetDebuggerTip()->HideDialog();
	}

	IDebugger *dbgr = DebuggerMgr::Get().GetActiveDebugger();
	if ( dbgr && dbgr->IsRunning() ) {
		switch ( cmd ) {
		case DBG_PAUSE:
			GetBreakpointsMgr()->SetExpectingControl(true);
			dbgr->Interrupt();
			dbgr->ListFrames();
			break;
		case DBG_NEXT:
			clMainFrame::Get()->GetDebuggerPane()->GetLocalsTable()->ResetTableColors();
			clMainFrame::Get()->GetDebuggerPane()->GetWatchesTable()->ResetTableColors();
			dbgr->Next();
			break;
		case DBG_STEPIN:
			clMainFrame::Get()->GetDebuggerPane()->GetLocalsTable()->ResetTableColors();
			clMainFrame::Get()->GetDebuggerPane()->GetWatchesTable()->ResetTableColors();
			dbgr->StepIn();
			break;
		case DBG_STEPOUT:
			clMainFrame::Get()->GetDebuggerPane()->GetLocalsTable()->ResetTableColors();
			clMainFrame::Get()->GetDebuggerPane()->GetWatchesTable()->ResetTableColors();
			dbgr->StepOut();
			break;
		case DBG_SHOW_CURSOR:
			dbgr->QueryFileLine();
			break;
		default:
			break;
		}
	}
}

void Manager::DbgSetFrame ( int frame, int lineno )
{
	wxAuiPaneInfo &info = clMainFrame::Get()->GetDockingManager().GetPane (wxT("Debugger"));
	if ( info.IsShown() ) {
		IDebugger *dbgr = DebuggerMgr::Get().GetActiveDebugger();
		if ( dbgr && dbgr->IsRunning() && DbgCanInteract() ) {
			//set the frame
			dbgr->SetFrame ( frame );
			m_frameLineno = lineno;
		}
	}
}

void Manager::DbgSetThread ( long threadId )
{
	IDebugger *dbgr = DebuggerMgr::Get().GetActiveDebugger();
	if ( dbgr && dbgr->IsRunning() && DbgCanInteract() ) {
		//set the frame
		dbgr->SelectThread ( threadId );
		dbgr->QueryFileLine();
	}
}


// Event handlers from the debugger

void Manager::UpdateAddLine ( const wxString &line, const bool OnlyIfLoggingOn /*=false*/ )
{
	// There are a few messages that are only worth displaying if full logging is enabled
	if (OnlyIfLoggingOn) {
		IDebugger *dbgr = DebuggerMgr::Get().GetActiveDebugger();
		if ( dbgr && (dbgr->IsLoggingEnabled()==false) ) {
			return;
		}
	}

	DebugMessage ( line + wxT ( "\n" ) );
}

void Manager::UpdateFileLine ( const wxString &filename, int lineno, bool repositionEditor )
{
	wxString fileName = filename;
	long lineNumber = lineno;
	if ( m_frameLineno != wxNOT_FOUND ) {
		lineNumber = m_frameLineno;
		m_frameLineno = wxNOT_FOUND;
	}

	if (repositionEditor)
		DbgMarkDebuggerLine ( fileName, lineNumber );

	UpdateDebuggerPane();
}

void Manager::UpdateGotControl ( const DebuggerEvent &e )
{
	int reason = e.m_controlReason;
	bool userTriggered = GetBreakpointsMgr()->GetExpectingControl();
	GetBreakpointsMgr()->SetExpectingControl(false);

	IDebugger *dbgr = DebuggerMgr::Get().GetActiveDebugger();
	if (dbgr == NULL) {
		wxLogDebug(wxT("Active debugger not found :/"));
		return;
	}

	DebuggerInformation dinfo;
	DebuggerMgr::Get().GetDebuggerInformation(dbgr->GetName(), dinfo);
	// Raise CodeLite (unless the cause is a hit bp, and we're configured not to
	//   e.g. because there's a command-list breakpoint ending in 'continue')
	if ( (reason != DBG_BP_HIT) || dinfo.whenBreakpointHitRaiseCodelite ) {
		//put us on top of the z-order window
		long curFlags = clMainFrame::Get()->GetWindowStyleFlag();
		clMainFrame::Get()->SetWindowStyleFlag(curFlags | wxSTAY_ON_TOP);
		clMainFrame::Get()->Raise();
		clMainFrame::Get()->SetWindowStyleFlag(curFlags);
		m_dbgCanInteract = true;
	}

	SendCmdEvent(wxEVT_DEBUG_EDITOR_GOT_CONTROL);

	if(dbgr && dbgr->IsRunning()) {
		// in case we got an error, try and clear the Locals view
		if(reason == DBG_CMD_ERROR) {
			clMainFrame::Get()->GetDebuggerPane()->GetLocalsTable()->Clear();
		}
	}

	switch ( reason ) {
	case DBG_RECV_SIGNAL_SIGTRAP:         // DebugBreak()
	case DBG_RECV_SIGNAL_EXC_BAD_ACCESS:  // SIGSEGV on Mac
	case DBG_RECV_SIGNAL_SIGABRT:         // assert() ?
	case DBG_RECV_SIGNAL_SIGSEGV: {       // program received signal sigsegv

		// Clear the 'Locals' view
		clMainFrame::Get()->GetDebuggerPane()->GetLocalsTable()->Clear();

		wxString signame = wxT ( "SIGSEGV" );

		// show the dialog only if the signal is not sigtrap
		// since sigtap might be triggered by user inserting a breakpoint
		// into an already running debug session
		bool     showDialog(true);
		if ( reason == DBG_RECV_SIGNAL_EXC_BAD_ACCESS ) {
			signame = wxT ( "EXC_BAD_ACCESS" );
			showDialog = true;

		} else if ( reason == DBG_RECV_SIGNAL_SIGABRT ) {
			signame = wxT ( "SIGABRT" );
			showDialog = true;

		} else if ( reason == DBG_RECV_SIGNAL_SIGTRAP ) {
			signame = wxT ( "SIGTRAP" );
			showDialog = !userTriggered;
		}

		DebugMessage ( _("Program Received signal ") + signame + wxT("\n") );
		if(showDialog) {
			wxMessageDialog dlg( clMainFrame::Get(), _("Program Received signal ") + signame + wxT("\n") +
			                     _("Stack trace is available in the 'Call Stack' tab\n"),
			                     _("CodeLite"), wxICON_ERROR|wxOK );
			dlg.ShowModal();
		}

		//Print the stack trace
		wxAuiPaneInfo &info = clMainFrame::Get()->GetDockingManager().GetPane(wxT("Debugger"));
		if ( showDialog ) {
			// select the "Call Stack" tab
			clMainFrame::Get()->GetDebuggerPane()->SelectTab ( DebuggerPane::FRAMES );
		}

		if(info.IsShown()) {
			// Refresh the view
			UpdateDebuggerPane();
		}

		if(!userTriggered) {
			if ( dbgr && dbgr->IsRunning() ) {
				dbgr->QueryFileLine();
				dbgr->BreakList();
				// Apply all previous watches
				DbgRestoreWatches();
			}
		}
	}
	break;
	case DBG_BP_ASSERTION_HIT: {
		// Clear the 'Locals' view
		clMainFrame::Get()->GetDebuggerPane()->GetLocalsTable()->Clear();

		wxMessageDialog dlg( clMainFrame::Get(), _("Assertion failed!\nStack trace is available in the 'Call Stack' tab\n"),
		                     _("CodeLite"), wxICON_ERROR|wxOK );
		dlg.ShowModal();

		//Print the stack trace
		wxAuiPaneInfo &info = clMainFrame::Get()->GetDockingManager().GetPane(wxT("Debugger"));
		if ( info.IsShown() ) {
			clMainFrame::Get()->GetDebuggerPane()->SelectTab ( DebuggerPane::FRAMES );
			UpdateDebuggerPane();
		}
	}
	break;
	case DBG_BP_HIT:        // breakpoint reached
		// Clear the 'Locals' view
		clMainFrame::Get()->GetDebuggerPane()->GetLocalsTable()->Clear();
		// fall through...
	case DBG_END_STEPPING:  // finished one of the following: next/step/nexti/stepi
	case DBG_FUNC_FINISHED:
	case DBG_UNKNOWN:       // the most common reason: temporary breakpoint
		if ( dbgr && dbgr->IsRunning() ) {
			dbgr->QueryFileLine();
			dbgr->BreakList();
			// Apply all previous watches
			DbgRestoreWatches();
		}
		break;

	case DBG_DBGR_KILLED:
		m_dbgCanInteract = false;
		break;

	case DBG_EXIT_WITH_ERROR: {
		wxMessageBox(wxString::Format(_("Debugger exited with the following error string:\n%s"),
		                              e.m_text.c_str()),
		             _("CodeLite"),
		             wxOK|wxICON_ERROR);
		// fall through
	}

	case DBG_EXITED_NORMALLY:
		//debugging finished, stop the debugger process
		DbgStop();
		break;
	default:
		break;
	}
}

void Manager::UpdateLostControl()
{
	//debugger lost control
	//hide the marker
	DbgUnMarkDebuggerLine();
	m_dbgCanInteract = false;
	DebugMessage ( _( "Continuing...\n" ) );

	// Reset the debugger call-stack pane
	clMainFrame::Get()->GetDebuggerPane()->GetFrameListView()->Clear();
	clMainFrame::Get()->GetDebuggerPane()->GetFrameListView()->SetCurrentLevel(0);

	SendCmdEvent(wxEVT_DEBUG_EDITOR_LOST_CONTROL);
}

void Manager::UpdateTypeReolsved(const wxString& expr, const wxString& type_name)
{
	IDebugger *dbgr = DebuggerMgr::Get().GetActiveDebugger();
	// Sanity
	if ( dbgr == NULL               ) {
		return;
	}
	if ( dbgr->IsRunning() == false ) {
		return;
	}
	if ( DbgCanInteract() == false  ) {
		return;
	}

	// gdb returns usually expression like:
	// const string &, so in order to get the actual type
	// we construct a valid expression by appending a valid identifier followed by a semi colon.
	wxString expression;
	wxString command(expr);
	wxString dbg_command(wxT("print"));
	wxString expression_type;

	DebuggerSettingsPreDefMap data;
	DebuggerConfigTool::Get()->ReadObject(wxT("DebuggerCommands"), &data);
	DebuggerPreDefinedTypes preDefTypes = data.GetActiveSet();
	DebuggerCmdDataVec      cmds        = preDefTypes.GetCmds();

	expression << wxT("/^");
	expression << type_name;
	expression << wxT(" someValidName;");
	expression << wxT("$/");

	Variable variable;
	if (LanguageST::Get()->VariableFromPattern(expression, wxT("someValidName"), variable)) {
		expression_type = _U(variable.m_type.c_str());
		for (size_t i=0; i<cmds.size(); i++) {
			DebuggerCmdData cmd = cmds.at(i);
			if (cmd.GetName() == expression_type) {
				// prepare the string to be evaluated
				command = cmd.GetCommand();
				command.Replace(wxT("$(Variable)"), expr);

				dbg_command = cmd.GetDbgCommand();

				//---------------------------------------------------
				// Special handling for the templates
				//---------------------------------------------------

				wxArrayString types = DoGetTemplateTypes(_U(variable.m_templateDecl.c_str()));
				// Case 1: list
				// The user defined scripts requires that we pass info like this:
				// plist <list name> <T>
				if ( expression_type == wxT("list") && types.GetCount() > 0 ) {
					command << wxT(" ") << types.Item(0);
				}
				// Case 2: map & multimap
				// The user defined script requires that we pass the TLeft & TRight
				// pmap <list name> TLeft TRight
				if ( (expression_type == wxT("map") || expression_type == wxT("multimap")) && types.GetCount() > 1 ) {
					command << wxT(" ") << types.Item(0) << wxT(" ") << types.Item(1);
				}

				break;
			}
		}
	}


	wxString output;
	bool     get_tip (false);

	Notebook * book = clMainFrame::Get()->GetDebuggerPane()->GetNotebook();
	if ( book->GetPageText(book->GetSelection()) == DebuggerPane::ASCII_VIEWER || IsPaneVisible(DebuggerPane::ASCII_VIEWER) ) {
		get_tip = true;
	}

	if ( get_tip ) {
		dbgr->GetAsciiViewerContent(dbg_command, command); // Will trigger a call to UpdateTip()
	}
}

void Manager::UpdateAsciiViewer(const wxString& expression, const wxString& tip)
{
	clMainFrame::Get()->GetDebuggerPane()->GetAsciiViewer()->UpdateView( expression, tip );
}

void Manager::UpdateRemoteTargetConnected(const wxString& line)
{
	IDebugger *dbgr = DebuggerMgr::Get().GetActiveDebugger();
	if (dbgr && dbgr->IsRunning() && IsWorkspaceOpen()) {
		// we currently do not support this feature when debugging using 'Quick debug'
		wxString errMsg;
		ProjectPtr proj = WorkspaceST::Get()->FindProjectByName ( GetActiveProjectName(), errMsg );
		BuildConfigPtr bldConf = WorkspaceST::Get()->GetProjBuildConf ( proj->GetName(), wxEmptyString );
		if ( bldConf ) {
			wxArrayString dbg_cmds = wxStringTokenize ( bldConf->GetDebuggerPostRemoteConnectCmds(), wxT ( "\n" ), wxTOKEN_STRTOK );
			for (size_t i=0; i<dbg_cmds.GetCount(); i++) {
				dbgr->ExecuteCmd(dbg_cmds.Item(i));
			}
		}
	}
	// log the line
	UpdateAddLine(line);
}


//--------------------------- Build Management -----------------------------

bool Manager::IsBuildInProgress() const
{
	return m_shellProcess && m_shellProcess->IsBusy();
}

void Manager::StopBuild()
{
	// Mark this build as 'interrupted'
	clMainFrame::Get()->GetOutputPane()->GetBuildTab()->SetBuildInterrupted(true);

	if ( m_shellProcess && m_shellProcess->IsBusy() ) {
		m_shellProcess->Stop();
	}
	m_buildQueue.clear();
}

void Manager::PushQueueCommand ( const QueueCommand& buildInfo )
{
	m_buildQueue.push_back ( buildInfo );
}

void Manager::ProcessCommandQueue()
{
	if ( m_buildQueue.empty() ) {
		return;
	}

	// pop the next build build and process it
	QueueCommand qcmd = m_buildQueue.front();
	m_buildQueue.pop_front();

	if ( qcmd.GetCheckBuildSuccess() && !IsBuildEndedSuccessfully() ) {
		// build failed, remove command from the queue
		return;
	}

	switch ( qcmd.GetKind() ) {
	case QueueCommand::CustomBuild:
		DoCustomBuild ( qcmd );
		break;
	case QueueCommand::Clean:
		DoCleanProject ( qcmd );
		break;
	case QueueCommand::ReBuild:
	case QueueCommand::Build:
		DoBuildProject ( qcmd );
		break;
	case QueueCommand::Debug:
		DbgStart ( wxNOT_FOUND );
		break;
	}
}

void Manager::BuildWorkspace()
{
	DoCmdWorkspace ( QueueCommand::Build );
	ProcessCommandQueue();
}

void Manager::CleanWorkspace()
{
	DoCmdWorkspace ( QueueCommand::Clean );
	ProcessCommandQueue();
}

void Manager::RebuildWorkspace()
{
	DoCmdWorkspace ( QueueCommand::Clean );
	DoCmdWorkspace ( QueueCommand::Build );
	ProcessCommandQueue();
}

void Manager::RunCustomPreMakeCommand ( const wxString &project )
{
	if ( m_shellProcess && m_shellProcess->IsBusy() ) {
		return;
	}

	wxString conf;
	// get the selected configuration to be built
	BuildConfigPtr bldConf = WorkspaceST::Get()->GetProjBuildConf ( project, wxEmptyString );
	if ( bldConf ) {
		conf = bldConf->GetName();
	}
	QueueCommand info ( project, conf, false, QueueCommand::Build );

	if ( m_shellProcess ) {
		delete m_shellProcess;
	}
	m_shellProcess = new CompileRequest ( clMainFrame::Get(),  //owner window
	                                      info,
	                                      wxEmptyString, //no file name (valid only for build file only)
	                                      true );        //run premake step only
	m_shellProcess->Process(PluginManager::Get());
}

void Manager::CompileFile ( const wxString &projectName, const wxString &fileName, bool preprocessOnly )
{
	if ( m_shellProcess && m_shellProcess->IsBusy() ) {
		return;
	}

	DoSaveAllFilesBeforeBuild();

	//If a debug session is running, stop it.
	IDebugger *dbgr = DebuggerMgr::Get().GetActiveDebugger();
	if ( dbgr && dbgr->IsRunning() ) {
		if ( wxMessageBox ( _( "This would terminate the current debug session, continue?" ),
		                    _("Confirm" ), wxICON_QUESTION|wxYES_NO|wxCANCEL ) != wxYES )
			return;
		DbgStop();
	}

	wxString conf;
	BuildConfigPtr bldConf = WorkspaceST::Get()->GetProjBuildConf ( projectName, wxEmptyString );
	if ( bldConf ) {
		conf = bldConf->GetName();
	}

	QueueCommand info ( projectName, conf, false, QueueCommand::Build );
	if ( bldConf && bldConf->IsCustomBuild() ) {
		info.SetCustomBuildTarget ( preprocessOnly ? _("Preprocess File") : _("Compile Single File" ) );
		info.SetKind ( QueueCommand::CustomBuild );
	}

	if ( m_shellProcess ) {
		delete m_shellProcess;
	}
	switch ( info.GetKind() ) {
	case QueueCommand::Build:
		m_shellProcess = new CompileRequest ( clMainFrame::Get(), info, fileName, false, preprocessOnly );
		break;
	case  QueueCommand::CustomBuild:
		m_shellProcess = new CustomBuildRequest ( clMainFrame::Get(), info, fileName );
		break;
	default:
		m_shellProcess = NULL;
		break;
	}
	m_shellProcess->Process(PluginManager::Get());
}

bool Manager::IsBuildEndedSuccessfully() const
{
	// return the result of the last build
	return clMainFrame::Get()->GetOutputPane()->GetBuildTab()->GetBuildEndedSuccessfully();
}

void Manager::DoBuildProject ( const QueueCommand& buildInfo )
{
	if ( m_shellProcess && m_shellProcess->IsBusy() )
		return;

	DoSaveAllFilesBeforeBuild();

	//If a debug session is running, stop it.
	IDebugger *dbgr = DebuggerMgr::Get().GetActiveDebugger();
	if ( dbgr && dbgr->IsRunning() ) {
		if ( wxMessageBox ( _( "This would terminate the current debug session, continue?" ),
		                    _("Confirm" ), wxICON_QUESTION|wxYES_NO|wxCANCEL ) != wxYES )
			return;
		DbgStop();
	}

	if ( m_shellProcess ) {
		delete m_shellProcess;
	}

	m_shellProcess = new CompileRequest ( clMainFrame::Get(), buildInfo );
	m_shellProcess->Process(PluginManager::Get());
}

void Manager::DoCleanProject ( const QueueCommand& buildInfo )
{
	if ( m_shellProcess && m_shellProcess->IsBusy() ) {
		return;
	}

	if ( m_shellProcess ) {
		delete m_shellProcess;
	}
	m_shellProcess = new CleanRequest ( clMainFrame::Get(), buildInfo );
	m_shellProcess->Process(PluginManager::Get());
}

void Manager::DoCustomBuild ( const QueueCommand& buildInfo )
{
	if ( m_shellProcess && m_shellProcess->IsBusy() ) {
		return;
	}

	DoSaveAllFilesBeforeBuild();


	//If a debug session is running, stop it.
	IDebugger *dbgr = DebuggerMgr::Get().GetActiveDebugger();
	if ( dbgr && dbgr->IsRunning() ) {
		if ( wxMessageBox ( _( "This would terminate the current debug session, continue?" ),
		                    _("Confirm"), wxICON_QUESTION|wxYES_NO|wxCANCEL ) != wxYES )
			return;
		DbgStop();
	}

	if ( m_shellProcess ) {
		delete m_shellProcess;
	}
	m_shellProcess = new CustomBuildRequest ( clMainFrame::Get(), buildInfo, wxEmptyString );
	m_shellProcess->Process( PluginManager::Get() );
}

void Manager::DoCmdWorkspace ( int cmd )
{
	// get list of projects
	wxArrayString projects;
	wxArrayString optimizedList;

	ManagerST::Get()->GetProjectList ( projects );

	for ( size_t i=0; i<projects.GetCount(); i++ ) {
		ProjectPtr p = GetProject ( projects.Item ( i ) );
		BuildConfigPtr buildConf = WorkspaceST::Get()->GetProjBuildConf ( projects.Item ( i ), wxEmptyString );
		if ( p && buildConf ) {
			wxArrayString deps = p->GetDependencies ( buildConf->GetName() );
			for ( size_t j=0; j<deps.GetCount(); j++ ) {
				// add a project only if it does not exist yet
				if ( optimizedList.Index ( deps.Item ( j ) ) == wxNOT_FOUND ) {
					optimizedList.Add ( deps.Item ( j ) );
				}
			}
			// add the project itself now, again only if it is not included yet
			if ( optimizedList.Index ( projects.Item ( i ) ) == wxNOT_FOUND ) {
				optimizedList.Add ( projects.Item ( i ) );
			}
		}
	}

	// add a build/clean project only command for every project in the optimized list
	for ( size_t i=0; i<optimizedList.GetCount(); i++ ) {
		BuildConfigPtr buildConf = WorkspaceST::Get()->GetProjBuildConf ( optimizedList.Item ( i ), wxEmptyString );
		if ( buildConf ) {
			QueueCommand bi ( optimizedList.Item ( i ), buildConf->GetName(), true, cmd );
			if ( buildConf->IsCustomBuild() ) {
				bi.SetKind ( QueueCommand::CustomBuild );
				switch ( cmd ) {
				case QueueCommand::Build:
					bi.SetCustomBuildTarget ( wxT ( "Build" ) );
					break;
				case QueueCommand::Clean:
					bi.SetCustomBuildTarget ( wxT ( "Clean" ) );
					break;
				}
			}
			bi.SetCleanLog ( i == 0 );
			PushQueueCommand ( bi );
		}
	}
}

void Manager::DbgClearWatches()
{
	m_dbgWatchExpressions.Clear();
}

void Manager::DebuggerUpdate(const DebuggerEvent& event)
{
	IDebugger *dbgr = DebuggerMgr::Get().GetActiveDebugger();
	DebuggerInformation dbgInfo = dbgr ? dbgr->GetDebuggerInformation() : DebuggerInformation();
	switch ( event.m_updateReason ) {

	case DBG_UR_GOT_CONTROL:
		// keep the current functin name
		m_dbgCurrentFrameInfo.func = event.m_frameInfo.function;
		UpdateGotControl(event);
		break;

	case DBG_UR_LOST_CONTROL:
		UpdateLostControl();
		break;

	case DBG_UR_FILE_LINE:
		//in some cases we don't physically reposition the file+line position, such as during updates made by user actions (like add watch)
		//but since this app uses a debugger refresh to update newly added watch values, it automatically repositions the editor always.
		//this isn't always desirable behavior, so we pass a parameter indicating for certain operations if an override was used
		UpdateFileLine(event.m_file, event.m_line, /*ManagerST::Get()->GetRepositionEditor()*/true);
		//raise the flag for the next call, as this "override" is only used once per consumption
		ManagerST::Get()->SetRepositionEditor(true);
		break;

	case DBG_UR_FRAMEDEPTH: {
		long frameDepth(0);
		event.m_frameInfo.level.ToLong(&frameDepth);
		m_dbgCurrentFrameInfo.depth = frameDepth;
		//clMainFrame::Get()->GetDebuggerPane()->GetLocalsTable()->UpdateFrameInfo();
		break;
	}

	case DBG_UR_VAROBJUPDATE:
		// notify the 'Locals' view to remove all
		// out-of-scope variable objects
		clMainFrame::Get()->GetDebuggerPane()->GetLocalsTable()->OnVariableObjUpdate(event);
		clMainFrame::Get()->GetDebuggerPane()->GetWatchesTable()->OnUpdateVariableObject(event);
		break;

	case DBG_UR_ADD_LINE:
		UpdateAddLine(event.m_text, event.m_onlyIfLogging);
		break;

	case DBG_UR_BP_ADDED:
		GetBreakpointsMgr()->SetBreakpointDebuggerID(event.m_bpInternalId, event.m_bpDebuggerId);
		break;

	case DBG_UR_STOPPED:
		// Nothing to do here
		break;

	case DBG_UR_LOCALS:
		clMainFrame::Get()->GetDebuggerPane()->GetLocalsTable()->UpdateLocals( event.m_locals );
#ifdef __WXMAC__
		{
			for(size_t i=0; i<event.m_locals.size(); i++) {
				LocalVariable v = event.m_locals.at(i);
				if(v.gdbId.IsEmpty() == false) {
					dbgr->DeleteVariableObject(v.gdbId);
				}
			}
		}
#endif
		break;

	case DBG_UR_FUNC_ARGS:
		clMainFrame::Get()->GetDebuggerPane()->GetLocalsTable()->UpdateFuncArgs( event.m_locals );
#ifdef __WXMAC__
		{
			for(size_t i=0; i<event.m_locals.size(); i++) {
				LocalVariable v = event.m_locals.at(i);
				if(v.gdbId.IsEmpty() == false) {
					dbgr->DeleteVariableObject(v.gdbId);
				}
			}
		}
#endif
		break;

	case DBG_UR_EXPRESSION:
		//clMainFrame::Get()->GetDebuggerPane()->GetWatchesTable()->UpdateExpression ( event.m_expression, event.m_evaluated );
		break;

	case DBG_UR_UPDATE_STACK_LIST:
		clMainFrame::Get()->GetDebuggerPane()->GetFrameListView()->Update ( event.m_stack );
		break;

	case DBG_UR_FUNCTIONFINISHED:
		clMainFrame::Get()->GetDebuggerPane()->GetLocalsTable()->UpdateFuncReturnValue( event.m_expression );
		break;

	case DBG_UR_REMOTE_TARGET_CONNECTED:
		UpdateRemoteTargetConnected( event.m_text );
		break;

	case DBG_UR_RECONCILE_BPTS:
		GetBreakpointsMgr()->ReconcileBreakpoints( event.m_bpInfoList );
		break;

	case DBG_UR_BP_HIT:
		GetBreakpointsMgr()->BreakpointHit( event.m_bpDebuggerId );
		break;

	case DBG_UR_TYPE_RESOLVED:
		UpdateTypeReolsved( event.m_expression, event.m_evaluated );
		break;

	case DBG_UR_ASCII_VIEWER:
		UpdateAsciiViewer( event.m_expression, event.m_text );
		break;

	case DBG_UR_LISTTHRAEDS:
		clMainFrame::Get()->GetDebuggerPane()->GetThreadsView()->PopulateList( event.m_threads );
		break;

	case DBG_UR_WATCHMEMORY:
		clMainFrame::Get()->GetDebuggerPane()->GetMemoryView()->SetViewString( event.m_evaluated );
		break;

	case DBG_UR_VARIABLEOBJUPDATEERR:
		// Variable object update fail!
		break;

	case DBG_UR_VARIABLEOBJCREATEERR:
		// Variable creation error, remove it from the relevant table
		if(/*event.m_userReason == DBG_USERR_LOCALS || */event.m_userReason == DBG_USERR_WATCHTABLE) {
			clMainFrame::Get()->GetDebuggerPane()->GetWatchesTable()->OnCreateVariableObjError( event );
			//clMainFrame::Get()->GetDebuggerPane()->GetLocalsTable()->OnCreateVariableObjError( event );
		}
		break;

	case DBG_UR_VARIABLEOBJ: {
		// CreateVariableObject callback
		if ( event.m_userReason == DBG_USERR_QUICKWACTH ) {

			/////////////////////////////////////////////
			// Handle Tooltips
			/////////////////////////////////////////////
			DoShowQuickWatchDialog( event );

			// Handle ASCII Viewer
			wxString expression ( event.m_expression );
			if ( event.m_variableObject.isPtr && !event.m_expression.StartsWith(wxT("*")) ) {
				if ( event.m_variableObject.typeName.Contains(wxT("char *"))    ||
				     event.m_variableObject.typeName.Contains(wxT("wchar_t *")) ||
				     event.m_variableObject.typeName.Contains(wxT("QChar *"))   ||
				     event.m_variableObject.typeName.Contains(wxT("wxChar *"))) {
					// dont de-reference
				} else {
					expression.Prepend(wxT("(*"));
					expression.Append(wxT(")"));

				}
			}
			UpdateTypeReolsved( expression, event.m_variableObject.typeName );
		} else if ( event.m_userReason == DBG_USERR_WATCHTABLE ) {
			// Double clicked on the 'Watches' table
			clMainFrame::Get()->GetDebuggerPane()->GetWatchesTable()->OnCreateVariableObject(event);

		} else if ( event.m_userReason == DBG_USERR_LOCALS ) {
			clMainFrame::Get()->GetDebuggerPane()->GetLocalsTable()->OnCreateVariableObj(event);

		}
	}
	break;
	case DBG_UR_LISTCHILDREN: {

		if(event.m_userReason == QUERY_NUM_CHILDS || event.m_userReason == LIST_WATCH_CHILDS) {
			// Watch table
			clMainFrame::Get()->GetDebuggerPane()->GetWatchesTable()->OnListChildren( event );

		} else if(event.m_userReason == QUERY_LOCALS_CHILDS || event.m_userReason == LIST_LOCALS_CHILDS) {
			// Locals table
			clMainFrame::Get()->GetDebuggerPane()->GetLocalsTable()->OnListChildren( event );

		} else {
			// Tooltip
			IDebugger *dbgr = DebuggerMgr::Get().GetActiveDebugger();
			if ( dbgr && dbgr->IsRunning() && DbgCanInteract() ) {
				if ( GetDebuggerTip() && !GetDebuggerTip()->IsShown() ) {
					GetDebuggerTip()->BuildTree( event.m_varObjChildren, dbgr );
					GetDebuggerTip()->m_mainVariableObject = event.m_expression;
					GetDebuggerTip()->ShowDialog( (event.m_userReason == DBG_USERR_WATCHTABLE || event.m_userReason == DBG_USERR_LOCALS) );

				} else if(GetDebuggerTip()) {
					// The dialog is shown
					GetDebuggerTip()->AddItems(event.m_expression, event.m_varObjChildren);
				}
			}

		}
	}
	break;
	case DBG_UR_EVALVARIABLEOBJ:

		// EvaluateVariableObject callback
		if(event.m_userReason == DBG_USERR_WATCHTABLE) {
			clMainFrame::Get()->GetDebuggerPane()->GetWatchesTable()->OnEvaluateVariableObj(event );

		} else if(event.m_userReason == DBG_USERR_LOCALS) {
			clMainFrame::Get()->GetDebuggerPane()->GetLocalsTable()->OnEvaluateVariableObj( event );

		} else if (GetDebuggerTip() && GetDebuggerTip()->IsShown()) {
			GetDebuggerTip()->UpdateValue(event.m_expression, event.m_evaluated);

		}
		break;
	case DBG_UR_INVALID:
	default:
		break;
	}
}

void Manager::DbgRestoreWatches()
{
	// restore any saved watch expressions from previous debug sessions
	if ( m_dbgWatchExpressions.empty() == false ) {
		for (size_t i=0; i<m_dbgWatchExpressions.GetCount(); i++) {
			DebugMessage(wxT("Restoring watch: ") + m_dbgWatchExpressions.Item(i) + wxT("\n"));
			wxCommandEvent e(wxEVT_COMMAND_MENU_SELECTED, XRCID("add_watch"));
			e.SetString(m_dbgWatchExpressions.Item(i));
			clMainFrame::Get()->GetDebuggerPane()->GetWatchesTable()->GetEventHandler()->ProcessEvent( e );
		}
		m_dbgWatchExpressions.Clear();
	}
}

void Manager::DoRestartCodeLite()
{
	wxString restartCodeLiteCommand;
#ifdef __WXMSW__
	// the codelite_launcher application is located where the codelite executable is
	// to properly shoutdown codelite. We first need to close the codelite_indexer process
	restartCodeLiteCommand << wxT("\"") << m_codeliteLauncher.GetFullPath() << wxT("\" ")
	                       << wxT(" --name=\"")
	                       << wxStandardPaths::Get().GetExecutablePath()
	                       << wxT("\"");

	wxCommandEvent event(wxEVT_COMMAND_MENU_SELECTED, XRCID("exit_app"));
	clMainFrame::Get()->GetEventHandler()->ProcessEvent(event);

	wxExecute(restartCodeLiteCommand, wxEXEC_ASYNC|wxEXEC_NOHIDE);

#elif defined (__WXGTK__)
	// The Shell is our friend
	restartCodeLiteCommand << wxStandardPaths::Get().GetExecutablePath();

	// Restore the original working dir and any paramters
	for (int i = 1; i < wxTheApp->argc; i++) {
		restartCodeLiteCommand << wxT(" ") << wxTheApp->argv[i];
	}
	wxSetWorkingDirectory(GetOriginalCwd());

	wxCommandEvent event(wxEVT_COMMAND_MENU_SELECTED, XRCID("exit_app"));
	clMainFrame::Get()->GetEventHandler()->AddPendingEvent(event);

	wxExecute(restartCodeLiteCommand, wxEXEC_ASYNC|wxEXEC_NOHIDE);
#endif
}

void Manager::SetCodeLiteLauncherPath(const wxString& path)
{
	m_codeliteLauncher = wxFileName(path, wxT("codelite_launcher"));
}

void Manager::OnRestart(wxCommandEvent& event)
{
	wxUnusedVar(event);
	DoRestartCodeLite();
}

void Manager::DoShowQuickWatchDialog( const DebuggerEvent &event )
{
	/////////////////////////////////////////////
	// Handle Tooltips
	/////////////////////////////////////////////

	bool                useDialog   = (event.m_userReason == DBG_USERR_WATCHTABLE || event.m_userReason == DBG_USERR_LOCALS);
	IDebugger *         dbgr        = DebuggerMgr::Get().GetActiveDebugger();
	bool                canInteract = (dbgr && dbgr->IsRunning() && DbgCanInteract());
	DisplayVariableDlg* view        = NULL;

	if(canInteract) {
		// First see if this type has a user-defined alternative
		// If so, don't do anything here, just create a new  for the u-d type
		// That'll bring us back here, but with the correct data
		DebuggerSettingsPreDefMap data;
		DebuggerConfigTool::Get()->ReadObject(wxT("DebuggerCommands"), &data);
		DebuggerPreDefinedTypes preDefTypes = data.GetActiveSet();

		wxString preDefinedType =
		    preDefTypes.GetPreDefinedTypeForTypename(event.m_variableObject.typeName, event.m_expression);
		if (!preDefinedType.IsEmpty()) {
			dbgr->CreateVariableObject( preDefinedType, false, DBG_USERR_QUICKWACTH );
#ifdef __WXMAC__
			if(!event.m_variableObject.gdbId.IsEmpty()) {
				dbgr->DeleteVariableObject(event.m_variableObject.gdbId);
			}
#endif
			return;
		}

		// Editor Tooltip
		view = GetDebuggerTip();
		view->m_mainVariableObject = event.m_variableObject.gdbId;
		view->m_variableName       = event.m_expression;
		view->m_expression         = event.m_expression;

		if ( event.m_evaluated.IsEmpty() == false ) {
			view->m_variableName << wxT(" = ") << event.m_evaluated;
		}

		if ( event.m_variableObject.typeName.IsEmpty() == false ) {
			view->m_variableName << wxT(" [") << event.m_variableObject.typeName << wxT("] ");
		}

		if ( event.m_variableObject.numChilds > 0 ) {
			// Complex type
			dbgr->ListChildren(event.m_variableObject.gdbId, event.m_userReason);

		} else  {
			// Simple type, no need for further calls, show the dialog
			if ( !view->IsShown() ) {
				view->BuildTree( event.m_varObjChildren, dbgr );
				// If the reason for showing the dialog was the 'Watches' table being d-clicked,
				// center the dialog
				view->ShowDialog( useDialog );
			}
		}
	}
}

bool Manager::UpdateParserPaths(bool notify)
{
	wxArrayString localIncludePaths;
	wxArrayString localExcludePaths;
	wxArrayString projectIncludePaths;

	// If we have an opened workspace, get its search paths
	if(IsWorkspaceOpen()) {
		LocalWorkspaceST::Get()->GetParserPaths(localIncludePaths, localExcludePaths);

		BuildConfigPtr buildConf = GetCurrentBuildConf();
		if(buildConf) {
			wxString projSearchPaths = buildConf->GetCcSearchPaths();
			projectIncludePaths = wxStringTokenize(projSearchPaths, wxT("\r\n"), wxTOKEN_STRTOK);
		}
	}

	// Update the parser thread with the new paths
	wxArrayString globalIncludePath, uniExcludePath;
	TagsOptionsData tod = clMainFrame::Get()->GetTagsOptions();
	globalIncludePath   = tod.GetParserSearchPaths();
	uniExcludePath      = tod.GetParserExcludePaths();

	// Add the global search paths to the local workspace
	// include paths (the order does matter)
	for(size_t i=0; i<globalIncludePath.GetCount(); i++) {
		if(localIncludePaths.Index(globalIncludePath.Item(i)) == wxNOT_FOUND) {
			localIncludePaths.Add( globalIncludePath.Item(i) );
		}
	}

	// Add the project paths as well
	for(size_t i=0; i<projectIncludePaths.GetCount(); i++) {
		if(localIncludePaths.Index(projectIncludePaths.Item(i)) == wxNOT_FOUND) {
			localIncludePaths.Add( projectIncludePaths.Item(i) );
		}
	}

	for(size_t i=0; i<localExcludePaths.GetCount(); i++) {
		if(uniExcludePath.Index(localExcludePaths.Item(i)) == wxNOT_FOUND) {
			uniExcludePath.Add( localExcludePaths.Item(i) );
		}
	}

	ParseThreadST::Get()->SetSearchPaths( localIncludePaths, uniExcludePath );

	if(notify) {
		wxCommandEvent event( wxEVT_COMMAND_MENU_SELECTED, XRCID("retag_workspace") );
		clMainFrame::Get()->GetEventHandler()->AddPendingEvent( event );
	}
	return true;
}

void Manager::OnIncludeFilesScanDone(wxCommandEvent& event)
{
	clMainFrame::Get()->SetStatusMessage(_("Retagging..."), 0);

	wxBusyCursor busyCursor;
	std::set<std::string> *fileSet = (std::set<std::string>*)event.GetClientData();
//	fprintf(stderr, "fileSet size=%d\n", fileSet->size());

	wxArrayString projects;
	GetProjectList ( projects );

	std::vector<wxFileName> projectFiles;
	for ( size_t i=0; i<projects.GetCount(); i++ ) {
		ProjectPtr proj = GetProject ( projects.Item ( i ) );
		if ( proj ) {
			proj->GetFiles( projectFiles, true );
		}
	}

	// add to this set the workspace files to create a unique list of
	// files
	for (size_t i=0; i<projectFiles.size(); i++) {
		wxString fn( projectFiles.at(i).GetFullPath() );
		fileSet->insert( fn.mb_str(wxConvUTF8).data() );
	}

//	fprintf(stderr, "Parsing the following files\n");
	// recreate the list in the form of vector (the API requirs vector)
	projectFiles.clear();
	std::set<std::string>::iterator iter = fileSet->begin();
	for (; iter != fileSet->end(); iter++ ) {
		wxFileName fn(wxString((*iter).c_str(), wxConvUTF8));
		fn.MakeAbsolute();

//		const wxCharBuffer cfile = _C(fn.GetFullPath());
//		fprintf(stderr, "%s\n", cfile.data());

		projectFiles.push_back( fn );
	}

	wxStopWatch sw;
	sw.Start();

	// -----------------------------------------------
	// tag them
	// -----------------------------------------------

	TagsManagerST::Get()->RetagFiles ( projectFiles, event.GetInt() );

#if !USE_PARSER_TREAD_FOR_RETAGGING_WORKSPACE
	long end   = sw.Time();
	clMainFrame::Get()->SetStatusMessage(_("Done"), 0);
	wxLogMessage(wxT("INFO: Retag workspace completed in %d seconds (%d files were scanned)"), (end)/1000, projectFiles.size());
	SendCmdEvent ( wxEVT_FILE_RETAGGED, ( void* ) &projectFiles );
#endif

	delete fileSet;
}

void Manager::DoSaveAllFilesBeforeBuild()
{
	// Save all files before compiling, but dont saved new documents
	SendCmdEvent(wxEVT_FILE_SAVE_BY_BUILD_START);
	if (!clMainFrame::Get()->GetMainBook()->SaveAll(false, false)) {
		SendCmdEvent(wxEVT_FILE_SAVE_BY_BUILD_END);
		return;
	}
	SendCmdEvent(wxEVT_FILE_SAVE_BY_BUILD_END);
}

DisplayVariableDlg* Manager::GetDebuggerTip()
{
	if(!m_watchDlg) {
		m_watchDlg = new DisplayVariableDlg(clMainFrame::Get());
	}
	return m_watchDlg;
}

void Manager::OnProjectSettingsModified(wxCommandEvent& event)
{
	// Get the project settings
	clMainFrame::Get()->SelectBestEnvSet();
}

void Manager::OnDbContentCacherLoaded(wxCommandEvent& event)
{
	wxLogMessage(event.GetString());
}

void Manager::GetActiveProjectAndConf(wxString& project, wxString& conf)
{
	if(!IsWorkspaceOpen()) {
		project.Clear();
		conf.Clear();
		return;
	}

	project = GetActiveProjectName();
	BuildMatrixPtr matrix = WorkspaceST::Get()->GetBuildMatrix();
	if(!matrix) {
		return;
	}

	wxString workspaceConf = matrix->GetSelectedConfigurationName();
	matrix->GetProjectSelectedConf(workspaceConf, project);
}

void Manager::UpdatePreprocessorFile(const wxFileName& filename)
{
	if(!TagsManagerST::Get()->IsValidCtagsFile(filename.GetFullPath())) {
		return;
	}

	if((TagsManagerST::Get()->GetCtagsOptions().GetCcColourFlags() & CC_COLOUR_MACRO_BLOCKS) == 0)
		return;

	const wxString sFilename = filename.GetFullPath();

	wxArrayString projects;
	GetProjectList( projects );
	std::vector<wxFileName> projectFiles;
	for (size_t i = 0; i < projects.GetCount(); i++) {
		ProjectPtr proj = GetProject( projects.Item(i) );
		if ( proj ) {
			proj->GetFiles(projectFiles, true);
		}
	}

	// Create a parsing request
	ParseRequest* parsingRequest = new ParseRequest();
	for (size_t i = 0; i < projectFiles.size(); i++) {
		const wxFileName& projFileName = projectFiles.at(i);
		// filter any non valid coding file
		if (!TagsManagerST::Get()->IsValidCtagsFile(projFileName)) {
			continue;
		}
		// TODO : Only keep header files
		parsingRequest->_workspaceFiles.push_back(projFileName.GetFullPath().mb_str(wxConvUTF8).data());
	}

	parsingRequest->setFile(sFilename);
	parsingRequest->setType(ParseRequest::PR_GET_INTERRESTING_MACROS);
	parsingRequest->setDbFile( TagsManagerST::Get()->GetDatabase()->GetDatabaseFileName().GetFullPath().c_str() );
	parsingRequest->_evtHandler = this;
	ParseThreadST::Get()->Add( parsingRequest );
}

void Manager::OnInterrestingMacrosFound(wxCommandEvent& e)
{
	InterrestingMacrosEventData* pMacros = (InterrestingMacrosEventData*) e.GetClientData();
	if (!pMacros) {
		return;
	}
	wxString filename = pMacros->GetFileName();
	wxString macros(pMacros->GetMacros());
	delete pMacros;

	// Check that the editor is still opened
	MainBook* book = clMainFrame::Get()->GetMainBook();
	LEditor* editor = book->FindEditor(filename);
	if (!editor) {
		return;
	}

	if (macros.empty()) {
		return;
	}

	macros.Replace(wxT("\n"), wxT(" "));
	macros.Replace(wxT("\r"), wxT(" "));

	// Scintilla preprocessor management
	editor->SetProperty(wxT("lexer.cpp.track.preprocessor"), wxT("1"));
	editor->SetProperty(wxT("lexer.cpp.update.preprocessor"), wxT("1"));
	editor->SetKeyWords(4, macros);
}

BuildConfigPtr Manager::GetCurrentBuildConf()
{
	wxString project, conf;
	GetActiveProjectAndConf(project, conf);
	if(project.IsEmpty())
		return NULL;

	return WorkspaceST::Get()->GetProjBuildConf(project, conf);
}

void Manager::GetActiveFileProjectFiles(wxArrayString& files)
{
	// Send an event to the plugins to get a list of the current file's project files
	// If no plugin has replied, use the default GetProjectFiles method
	wxCommandEvent getFilesEevet(wxEVT_CMD_GET_CURRENT_FILE_PROJECT_FILES);
	getFilesEevet.SetEventObject(this);
	getFilesEevet.SetClientData(&files);
	if(!wxTheApp->ProcessEvent(getFilesEevet)) {
		// Set default project name
		wxString project = GetActiveProjectName();
		if (clMainFrame::Get()->GetMainBook()->GetActiveEditor()) {
			// use the active file's project
			wxFileName activeFile = clMainFrame::Get()->GetMainBook()->GetActiveEditor()->GetFileName();
			project = GetProjectNameByFile(activeFile.GetFullPath());
		}
		GetProjectFiles(project, files);
	}
}

void Manager::GetActiveProjectFiles(wxArrayString& files)
{
	wxCommandEvent getFilesEevet(wxEVT_CMD_GET_ACTIVE_PROJECT_FILES);
	getFilesEevet.SetEventObject(this);
	getFilesEevet.SetClientData(&files);
	if(!wxTheApp->ProcessEvent(getFilesEevet)) {
		GetProjectFiles(GetActiveProjectName(), files);
	}
}

bool Manager::DbgCanInteract()
{
	// Allow the plugins to override the default built-in behavior
	wxCommandEvent evtDbgCanInteract(wxEVT_CMD_DEBUGGER_CAN_INTERACT);
	evtDbgCanInteract.SetEventObject(this);
	evtDbgCanInteract.SetInt(0);
	if(wxTheApp->ProcessEvent(evtDbgCanInteract)) {
		return evtDbgCanInteract.GetInt() == 1;
	}

	return m_dbgCanInteract;
}

