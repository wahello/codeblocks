/*
 * This file is part of the Code::Blocks IDE and licensed under the GNU General Public License, version 3
 * http://www.gnu.org/licenses/gpl-3.0.html
 *
 * $Revision$
 * $Id$
 * $HeadURL$
 */

#include "sdk.h"
#ifndef CB_PRECOMP
  #include <wx/filename.h>
  #include <wx/fs_zip.h>
  #include <wx/intl.h>
  #include <wx/menu.h>
  #include <wx/process.h>
  #include <wx/xrc/xmlres.h>
  #include "cbeditor.h"
  #include "cbproject.h"
  #include "configmanager.h"
  #include "editormanager.h"
  #include "globals.h"
  #include "logmanager.h"
  #include "macrosmanager.h"
  #include "manager.h"
  #include "projectmanager.h"
  #include "scriptingmanager.h"
#endif

#include <vector>

#include <wx/mimetype.h>
#include <wx/help.h> //(wxWindows chooses the appropriate help controller class)
#include <wx/helpbase.h> //(wxHelpControllerBase class)
#include <wx/helpwin.h> //(Windows Help controller)
#include <wx/generic/helpext.h> //(external HTML browser controller)
#include <wx/html/helpctrl.h> //(wxHTML based help controller: wxHtmlHelpController)

#ifdef __WXMSW__
#include <wx/msw/helpchm.h> // used in case we fail to load the OCX module (it could fail too)
#include <wx/thread.h>
#endif

#include "cbstyledtextctrl.h"
#include "scripting/bindings/sc_utils.h"
#include "scripting/bindings/sc_typeinfo_all.h"

#include "help_plugin.h"
#include "MANFrame.h"

// 20 wasn't enough
#define MAX_HELP_ITEMS 32

int idHelpMenus[MAX_HELP_ITEMS];

// Register the plugin
namespace
{
    PluginRegistrant<HelpPlugin> reg("HelpPlugin");
    int idViewMANViewer = wxNewId();
};

BEGIN_EVENT_TABLE(HelpPlugin, cbPlugin)
    EVT_MENU(idViewMANViewer, HelpPlugin::OnViewMANViewer)
    EVT_UPDATE_UI(idViewMANViewer, HelpPlugin::OnUpdateUI)
END_EVENT_TABLE()

#ifdef __WXMSW__
namespace
{
  // This little class helps to fix a problem when the help file is CHM and the
  // keyword throws many results
  class LaunchCHMThread : public wxThread
  {
    private:
      wxCHMHelpController m_helpctl;
      wxString m_filename;
      wxString m_keyword;

    public:
      LaunchCHMThread(const wxString &file, const wxString &keyword);
      ExitCode Entry();
  };

  LaunchCHMThread::LaunchCHMThread(const wxString &file, const wxString &keyword)
  : m_filename(file), m_keyword(keyword)
  {
    m_helpctl.Initialize(file);
  }

  wxThread::ExitCode LaunchCHMThread::Entry()
  {
    m_helpctl.KeywordSearch(m_keyword);
    return 0;
  }
}
#endif

HelpPlugin::HelpPlugin()
: m_pMenuBar(0), m_LastId(0), m_manFrame(0)
{
  //ctor
    if (!Manager::LoadResource("help_plugin.zip"))
        NotifyMissingFile("help_plugin.zip");

    // initialize IDs for Help and popup menu
    for (int i = 0; i < MAX_HELP_ITEMS; ++i)
    {
        idHelpMenus[i] = wxNewId();

        // dynamically connect the events
        Connect(idHelpMenus[i], -1, wxEVT_COMMAND_MENU_SELECTED,
                (wxObjectEventFunction) (wxEventFunction) (wxCommandEventFunction)
                &HelpPlugin::OnFindItem);
    }

    m_LastId = idHelpMenus[0];
}

HelpPlugin::~HelpPlugin()
{
}

void HelpPlugin::SetManPageDirs(MANFrame *manFrame)
{
    const wxString man_prefix = "man:";
    wxString all_man_dirs(man_prefix);

    for (HelpCommon::HelpFilesVector::const_iterator i = m_Vector.begin(); i != m_Vector.end(); ++i)
    {
        if (i->second.name.Mid(0, man_prefix.size()).CmpNoCase(man_prefix) == 0)
        {
            // only add ; if a dir is already set
            if (all_man_dirs.Length() > man_prefix.Length())
                all_man_dirs += ";";

            all_man_dirs += i->second.name.Mid(man_prefix.Length());
        }
    }
    manFrame->SetDirs(all_man_dirs);
}

void HelpPlugin::OnAttach()
{
    // load configuration (only saved in our config dialog)
    HelpCommon::LoadHelpFilesVector(m_Vector);

    wxString prefix(ConfigManager::GetDataFolder() + "/help_plugin.zip#zip:/images/");
#if wxCHECK_VERSION(3, 1, 6)
    prefix << "svg/";
    wxBitmapBundle zoominbmp = cbLoadBitmapBundleFromSVG(prefix + "zoomin.svg", wxSize(16, 16));
    wxBitmapBundle zoomoutbmp = cbLoadBitmapBundleFromSVG(prefix + "zoomout.svg", wxSize(16, 16));
#else
    const int imageSize = Manager::Get()->GetImageSize(Manager::UIComponent::Main);
    prefix << wxString::Format("%dx%d/", imageSize, imageSize);
    wxBitmap zoominbmp = cbLoadBitmap(prefix + "zoomin.png");
    wxBitmap zoomoutbmp = cbLoadBitmap(prefix + "zoomout.png");
#endif

    m_manFrame = new MANFrame(Manager::Get()->GetAppWindow(), wxID_ANY, zoominbmp, zoomoutbmp);
    SetManPageDirs(m_manFrame);
    CodeBlocksDockEvent evt(cbEVT_ADD_DOCK_WINDOW);
    evt.name = "MANViewer";
    evt.title = _("Man/Html pages viewer");
    evt.pWindow = m_manFrame;
    evt.dockSide = CodeBlocksDockEvent::dsRight;
    evt.desiredSize.Set(320, 240);
    evt.floatingSize.Set(320, 240);
    evt.minimumSize.Set(240, 160);
    Manager::Get()->ProcessEvent(evt);

    int baseFont = Manager::Get()->GetConfigManager("help_plugin")->ReadInt("/base_font_size", 0);

    if (baseFont > 0)
        m_manFrame->SetBaseFontSize(baseFont);

    if (Manager::Get()->GetConfigManager("help_plugin")->ReadBool("/show_man_viewer", false))
        ShowMANViewer();
}

cbConfigurationPanel* HelpPlugin::GetConfigurationPanel(wxWindow* parent)
{
  return new HelpConfigDialog(parent, this);
}

void HelpPlugin::Reload()
{
    // remove entries from help menu
    int counter = m_LastId - idHelpMenus[0];
    HelpCommon::HelpFilesVector::iterator it;

    for (it = m_Vector.begin(); it != m_Vector.end(); ++it)
      RemoveFromHelpMenu(idHelpMenus[--counter], it->first);

    // reload configuration (saved in the config dialog)
    HelpCommon::LoadHelpFilesVector(m_Vector);
    BuildHelpMenu();
    if (m_manFrame)
        SetManPageDirs(m_manFrame);
}

void HelpPlugin::OnRelease(bool /*appShutDown*/)
{
    Manager::Get()->GetConfigManager("help_plugin")->Write("/base_font_size", m_manFrame->GetBaseFontSize());
    CodeBlocksDockEvent evt(cbEVT_REMOVE_DOCK_WINDOW);
    evt.pWindow = m_manFrame;
    Manager::Get()->ProcessEvent(evt);
    m_manFrame->Destroy();
    m_manFrame = 0;
}

void HelpPlugin::BuildHelpMenu()
{
    int counter = 0;
    HelpCommon::HelpFilesVector::iterator it;

    for (it = m_Vector.begin(); it != m_Vector.end(); ++it, ++counter)
    {
        if (counter == HelpCommon::getDefaultHelpIndex())
            AddToHelpMenu(idHelpMenus[counter], it->first + "\tF1", it->second.readFromIni);
        else
            AddToHelpMenu(idHelpMenus[counter], it->first, it->second.readFromIni);
    }

    m_LastId = idHelpMenus[0] + counter;
}

void HelpPlugin::BuildMenu(wxMenuBar *menuBar)
{
    if (!IsAttached())
        return;

    m_pMenuBar = menuBar;

    BuildHelpMenu();

    int idx = menuBar->FindMenu(_("&View"));
    if (idx != wxNOT_FOUND)
    {
        wxMenu* view = menuBar->GetMenu(idx);
        wxMenuItemList& items = view->GetMenuItems();
        // find the first separator and insert before it
        for (size_t i = 0; i < items.GetCount(); ++i)
        {
            if (items[i]->IsSeparator())
            {
                view->InsertCheckItem(i, idViewMANViewer, _("Man pages viewer"), _("Toggle displaying the man pages viewer"));
                return;
            }
        }
        // not found, just append
        view->AppendCheckItem(idViewMANViewer, _("Man pages viewer"), _("Toggle displaying the man pages viewer"));
    }
}

void HelpPlugin::BuildModuleMenu(const ModuleType type, wxMenu *menu, const FileTreeData* /*data*/)
{
  if (!menu || !IsAttached() || !m_Vector.size())
    return;

  if (type == mtEditorManager)
  {
    // add entries in popup menu
    int counter = 0;
    HelpCommon::HelpFilesVector::iterator it;
    wxMenu *sub_menu = new wxMenu;

    for (it = m_Vector.begin(); it != m_Vector.end(); ++it)
      AddToPopupMenu(sub_menu, idHelpMenus[counter++], it->first, it->second.readFromIni);

    const wxString label = _("&Locate in");
    wxMenuItem *locate_in_menu = new wxMenuItem(0, wxID_ANY, label, "", wxITEM_NORMAL);
    locate_in_menu->SetSubMenu(sub_menu);

    const int position = Manager::Get()->GetPluginManager()->FindSortedMenuItemPosition(*menu, label);
    menu->Insert(position, locate_in_menu);
  }
}

bool HelpPlugin::BuildToolBar(wxToolBar * /*toolBar*/)
{
    return false;
}

void HelpPlugin::AddToHelpMenu(int id, const wxString &help, cb_unused bool fromIni)
{
    if (!m_pMenuBar)
        return;

    const int pos = m_pMenuBar->FindMenu(_("&Help"));
    if (pos != wxNOT_FOUND)
    {
        wxMenu *helpMenu = m_pMenuBar->GetMenu(pos);
        if (id == idHelpMenus[0])
            helpMenu->AppendSeparator();

        helpMenu->Append(id, help);
    }
}

void HelpPlugin::RemoveFromHelpMenu(int id, const wxString & /*help*/)
{
  if (!m_pMenuBar)
    return;

  int pos = m_pMenuBar->FindMenu(_("&Help"));

  if (pos != wxNOT_FOUND)
  {
    wxMenu *helpMenu = m_pMenuBar->GetMenu(pos);
    wxMenuItem *mi = helpMenu->Remove(id);

    if (id)
      delete mi;

    // remove separator too (if it's the last thing left)
    mi = helpMenu->FindItemByPosition(helpMenu->GetMenuItemCount() - 1);

    if (mi && (mi->GetKind() == wxITEM_SEPARATOR || mi->GetItemLabelText().IsEmpty()))
    {
      helpMenu->Remove(mi);
      delete mi;
    }
  }
}

void HelpPlugin::AddToPopupMenu(wxMenu *menu, int id, const wxString &help, bool
#ifdef __WXMSW__
    fromIni
#endif
)
{
  if (!help.IsEmpty())
  {
#ifdef __WXMSW__
    if (fromIni)
    {
      wxMenuItem *mitem = new wxMenuItem(0, id, help);
      wxFont &font = mitem->GetFont();
      font.SetWeight(wxFONTWEIGHT_BOLD);
      mitem->SetFont(font);
      menu->Append(mitem);
    }
    else
#endif
      menu->Append(id, help);
  }
}

HelpCommon::HelpFileAttrib HelpPlugin::HelpFileFromId(int id)
{
  int counter = 0;
  HelpCommon::HelpFilesVector::iterator it;

  for (it = m_Vector.begin(); it != m_Vector.end(); ++it, ++counter)
  {
    if (idHelpMenus[counter] == id)
      return it->second;
  }

  return HelpCommon::HelpFileAttrib();
}

void HelpPlugin::OnViewMANViewer(wxCommandEvent &event)
{
    CodeBlocksDockEvent evt(event.IsChecked() ? cbEVT_SHOW_DOCK_WINDOW : cbEVT_HIDE_DOCK_WINDOW);
    evt.pWindow = m_manFrame;
    Manager::Get()->ProcessEvent(evt);
}

void HelpPlugin::ShowMANViewer(bool show)
{
    CodeBlocksDockEvent evt(show ? cbEVT_SHOW_DOCK_WINDOW : cbEVT_HIDE_DOCK_WINDOW);
    evt.pWindow = m_manFrame;
    Manager::Get()->ProcessEvent(evt);

    // update user prefs
    Manager::Get()->GetConfigManager("help_plugin")->Write("/show_man_viewer", show);
}

void HelpPlugin::OnUpdateUI(wxUpdateUIEvent& /*event*/)
{
    wxMenuBar* pbar = Manager::Get()->GetAppFrame()->GetMenuBar();

    // uncheck checkbox if window was closed
    if (m_manFrame && !IsWindowReallyShown(m_manFrame))
        pbar->Check(idViewMANViewer, false);
}

void HelpPlugin::LaunchHelp(const wxString &c_helpfile, bool isExecutable, bool openEmbeddedViewer, HelpCommon::StringCase keyCase, const wxString &defkeyword, const wxString &c_keyword)
{
  const static wxString http_prefix("http://");
  const static wxString man_prefix("man:");
  wxString helpfile(c_helpfile);

  // Patch by Yorgos Pagles: Use the new attributes to calculate the keyword
  wxString keyword = c_keyword.IsEmpty() ? defkeyword : c_keyword;

  if (keyCase == HelpCommon::UpperCase)
    keyword.MakeUpper();
  else if (keyCase == HelpCommon::LowerCase)
    keyword.MakeLower();

  helpfile.Replace("$(keyword)", keyword);
  Manager::Get()->GetMacrosManager()->ReplaceMacros(helpfile);

  if (isExecutable)
  {
    Manager::Get()->GetLogManager()->DebugLog("Executing " + helpfile);
    wxExecute(helpfile);
    return;
  }

  // Support C::B scripts
  if (wxFileName(helpfile).GetExt() == "script")
  {
    ScriptingManager *scriptMgr = Manager::Get()->GetScriptingManager();
    if (scriptMgr->LoadScript(helpfile))
    {
      // help scripts must contain a function with the following signature:
      // function SearchHelp(keyword)
      ScriptBindings::Caller caller(scriptMgr->GetVM());
      if (!caller.CallByName1(_SC("SearchHelp"), &keyword))
        scriptMgr->DisplayErrors(true);
    }
    else
      Manager::Get()->GetLogManager()->DebugLog("Couldn't run script");

    return;
  }

  // Operate on help html file links inside embedded viewer
  if (openEmbeddedViewer && wxFileName(helpfile).GetExt().Mid(0, 3).CmpNoCase("htm") == 0)
  {
    Manager::Get()->GetLogManager()->DebugLog("Launching " + helpfile);
    cbMimePlugin* p = Manager::Get()->GetPluginManager()->GetMIMEHandlerForFile(helpfile);
    if (p)
      p->OpenFile(helpfile);
    else
    {
      reinterpret_cast<MANFrame *>(m_manFrame)->LoadPage(helpfile);
      ShowMANViewer();
    }
    return;
  }

  // Operate on help http (web) links
  if (helpfile.Mid(0, http_prefix.size()).CmpNoCase(http_prefix) == 0)
  {
    Manager::Get()->GetLogManager()->DebugLog("Launching " + helpfile);
    wxLaunchDefaultBrowser(helpfile);
    return;
  }

  // Operate on man pages
  if (helpfile.Mid(0, man_prefix.size()).CmpNoCase(man_prefix) == 0)
  {
    if (reinterpret_cast<MANFrame *>(m_manFrame)->SearchManPage(keyword))
      Manager::Get()->GetLogManager()->DebugLog("Couldn't find man page");
    else
      Manager::Get()->GetLogManager()->DebugLog("Launching man page");

    ShowMANViewer();
    return;
  }

#ifdef __WXMSW__
  if (helpfile.StartsWith("ms-xhelp:///"))
  {
    Manager::Get()->GetLogManager()->DebugLog("Launching " + helpfile);
    ShellExecute(0, 0, helpfile.c_str(), 0, 0, SW_SHOWDEFAULT);
    return;
  }
#endif // __WXMSW__

  wxFileName the_helpfile = wxFileName(helpfile);
  Manager::Get()->GetLogManager()->DebugLog("Help File is " + helpfile);

  if (!(the_helpfile.FileExists()))
  {
    wxString msg;
    msg << _("Couldn't find the help file:\n")
        << the_helpfile.GetFullPath() << _("\n")
        << _("Do you want to run the associated program anyway?");
    if (!(cbMessageBox(msg, _("Warning"), wxICON_WARNING | wxYES_NO | wxNO_DEFAULT) == wxID_YES))
        return;
  }

  wxString ext = the_helpfile.GetExt();

#ifdef __WXMSW__
  // Operate on help files with keyword search (windows only)
  if (!keyword.IsEmpty())
  {
    if (ext.CmpNoCase("hlp") == 0)
    {
      wxWinHelpController HelpCtl;
      HelpCtl.Initialize(helpfile);
      HelpCtl.KeywordSearch(keyword);
      return;
    }

    if ((ext.CmpNoCase("chm") == 0) || (ext.CmpNoCase("col") == 0))
    {
      LaunchCHMThread *p_thread = new LaunchCHMThread(helpfile, keyword);
      p_thread->Create();
      p_thread->Run();
      return;
    }
  }
#endif

  // Just call it with the associated program
  wxFileType *filetype = wxTheMimeTypesManager->GetFileTypeFromExtension(ext);

  if (!filetype)
  {
    cbMessageBox(_("Couldn't find an associated program to open:\n") +
      the_helpfile.GetFullPath(), _("Warning"), wxOK | wxICON_EXCLAMATION);
    return;
  }

  wxExecute(filetype->GetOpenCommand(helpfile));
  delete filetype;
}

void HelpPlugin::OnFindItem(wxCommandEvent &event)
{
  wxString text; // save here the word to lookup... if any
  cbEditor *ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();

  if (ed)
  {
    cbStyledTextCtrl *control = ed->GetControl();
    text = control->GetSelectedText();

    if (text.IsEmpty())
    {
      int origPos = control->GetCurrentPos();
      int start = control->WordStartPosition(origPos, true);
      int end = control->WordEndPosition(origPos, true);
      text = control->GetTextRange(start, end);
    }
  }

  int id = event.GetId();
  HelpCommon::HelpFileAttrib hfa = HelpFileFromId(id);
  // Patch by Yorgos Pagles: Use the new keyword calculation
  LaunchHelp(hfa.name, hfa.isExecutable, hfa.openEmbeddedViewer, hfa.keywordCase, hfa.defaultKeyword, text);
}
