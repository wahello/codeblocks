#include "wxsLedNumber.h"

#include "wx/gizmos/ledctrl.h"

namespace
{

    #include "images/LedNumber16.xpm"
    #include "images/LedNumber32.xpm"

    wxsRegisterItem<wxsLedNumber> Reg(
        _T("wxLEDNumberCtrl"),
        wxsTWidget,
        _T("wxWindows"),
        _T("Matt Kimball"),
        _T(""),
        _T("http://wxcode.sourceforge.net/complist.php"),
        _T("Led"),
        80,
        _T("LedNumber"),
        wxsCPP,
        1, 0,
        wxBitmap(LedNumber32_xpm),
        wxBitmap(LedNumber16_xpm),
        false);

        static const long    Values[] = { wxLED_ALIGN_LEFT, wxLED_ALIGN_CENTER, wxLED_ALIGN_RIGHT};

#pragma push_macro("_")
#undef _
#define _(x)   L##x
        static const wxChar* Names[]  = { _("Left"), _("Center"), _("Right"), nullptr }; // Must end with nullptr entry
#pragma pop_macro("_")
}

wxsLedNumber::wxsLedNumber(wxsItemResData* Data) : wxsWidget( Data, &Reg.Info, nullptr, nullptr, flVariable | flId | flPosition | flSize | flColours | flMinMaxSize | flExtraCode)
{
    //ctor
    Content      = wxString();
    Align        = wxLED_ALIGN_LEFT;
    Faded        = true;
    GetBaseProps()->m_Fg = wxColour( 0, 255, 0);
    GetBaseProps()->m_Bg = wxColour( 0,   0, 0);
}

wxsLedNumber::~wxsLedNumber()
{
    //dtor
}

void wxsLedNumber::OnBuildCreatingCode()
{

    wxString FGCol = GetBaseProps()->m_Fg.BuildCode(GetCoderContext());
    wxString BGCol = GetBaseProps()->m_Bg.BuildCode(GetCoderContext());

    switch ( GetLanguage() )
    {
        case wxsCPP:
        {
            AddHeader(_T("<wx/gizmos/ledctrl.h>"),GetInfo().ClassName);
            Codef( _T("%C(%W,%I,%P,%S,%d|wxFULL_REPAINT_ON_RESIZE %s);\n"), Align, (Faded ? "| wxLED_DRAW_FADED" : ""));
            Codef( _T( "%ASetMinSize( %S);\n"));
            if ( !FGCol.empty() )
                Codef( _T("%ASetForegroundColour(%s);\n"),FGCol.wx_str());
            if ( !BGCol.empty() )
                Codef( _T("%ASetBackgroundColour(%s);\n"),BGCol.wx_str());
            if( Content.Len() > 0)
                Codef( _T( "%ASetValue( _T(\"%s\"));\n"), Content.wx_str());
            break;
        }

        case wxsUnknownLanguage: // fall-through
        default:
            wxsCodeMarks::Unknown(_T("wxsLedNumber::OnBuildCreatingCode"),GetLanguage());
    }
}

wxObject* wxsLedNumber::OnBuildPreview(wxWindow* Parent,cb_unused long Flags)
{
    wxLEDNumberCtrl* test = new wxLEDNumberCtrl(Parent,GetId(),Pos(Parent),Size(Parent), Align|wxFULL_REPAINT_ON_RESIZE);
    test->SetMinSize( Size( Parent));

    test->SetForegroundColour(GetBaseProps()->m_Fg.GetColour());
    test->SetBackgroundColour(GetBaseProps()->m_Bg.GetColour());

    if( Content.Len() > 0)
        test->SetValue( Content);

    test->SetDrawFaded( Faded);

    return test;
}

void wxsLedNumber::OnEnumWidgetProperties(cb_unused long Flags)
{

    WXS_SHORT_STRING(
                wxsLedNumber,
                Content,
                _("Content"),
                _T("Content"),
                _T(""),
                false);


    WXS_ENUM(
                wxsLedNumber,
                Align,
                _("Align"),
                _T("Align"),
                Values,
                Names,
                wxLED_ALIGN_LEFT);

    WXS_BOOL(
             wxsLedNumber,
             Faded,
             _("Faded"),
             _T("Faded"),
             true);
}

