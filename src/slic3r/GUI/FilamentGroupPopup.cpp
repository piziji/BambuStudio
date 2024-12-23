#include "FilamentGroupPopup.hpp"
#include "FilamentMapDialog.hpp"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"
#include "I18N.hpp"
#include "wx/dcgraph.h"

namespace Slic3r { namespace GUI {

static const wxString AutoForFlushLabel = _L("Filament-Saving Mode");
static const wxString AutoForMatchLabel = _L("Convenient Mode");
static const wxString ManualLabel       = _L("Manual Mode");

static const wxString AutoForFlushDesp = _L("(Arrange after slicing)");
static const wxString AutoForMatchDesp = _L("(Arrange before slicing)");
static const wxString ManualDesp       = "";
static const wxString MachineSyncTip = _L("(Please sync printer)");

static const wxString AutoForFlushDetail = _L("Disregrad the filaments in AMS. Optimize filament usage "
                                              "by calculating the best allocation for the left and right "
                                              "nozzles. Arrange the filaments according on the printer according to "
                                              "the slicing results.");
static const wxString AutoForMatchDetail = _L("Based on the current filaments in the AMS, allocate the "
                                              "filaments to the left and right nozzles.");
static const wxString ManualDetail       = _L("Mannully allocate the filaments for the left and right nozzles.");

static const wxColour LabelEnableColor = wxColour("#262E30");
static const wxColour LabelDisableColor = wxColour("#6B6B6B");
static const wxColour GreyColor = wxColour("#6B6B6B");
static const wxColour GreenColor = wxColour("#00AE42");
static const wxColour BackGroundColor = wxColour("#FFFFFF");


static bool is_multi_extruder()
{
    const auto &preset_bundle    = wxGetApp().preset_bundle;
    const auto &full_config      = preset_bundle->full_config();
    const auto  nozzle_diameters = full_config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
    return nozzle_diameters->size() > 1;
}

FilamentMapMode get_prefered_map_mode()
{
    const static std::map<std::string, int> enum_keys_map = ConfigOptionEnum<FilamentMapMode>::get_enum_values();
    auto                                   &app_config    = wxGetApp().app_config;
    std::string                             mode_str      = app_config->get("prefered_filament_map_mode");
    auto                                    iter          = enum_keys_map.find(mode_str);
    if (iter == enum_keys_map.end()) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format("Could not get prefered_filament_map_mode from app config, use AutoForFlsuh mode");
        return FilamentMapMode::fmmAutoForFlush;
    }
    return FilamentMapMode(iter->second);
}

static void set_prefered_map_mode(FilamentMapMode mode)
{
    const static std::vector<std::string> enum_values = ConfigOptionEnum<FilamentMapMode>::get_enum_names();
    auto                                 &app_config  = wxGetApp().app_config;
    std::string                           mode_str;
    if (mode < enum_values.size()) mode_str = enum_values[mode];

    if (mode_str.empty()) BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format("Set empty prefered_filament_map_mode to app config");
    app_config->set("prefered_filament_map_mode", mode_str);
}

static bool get_pop_up_remind_flag()
{
    auto &app_config = wxGetApp().app_config;
    return app_config->get_bool("pop_up_filament_map_mode");
}

static void set_pop_up_remind_flag(bool remind)
{
    auto &app_config = wxGetApp().app_config;
    app_config->set_bool("pop_up_filament_map_mode", remind);
}

bool is_pop_up_required()
{
    FilamentMapMode mode               = get_prefered_map_mode();
    bool            is_manual_mode_    = FilamentMapMode::fmmManual == mode;
    bool            is_multi_extruder_ = is_multi_extruder();
    return is_multi_extruder_ && is_manual_mode_;
}

FilamentGroupPopup::FilamentGroupPopup(wxWindow *parent) : PopupWindow(parent, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS)
{
    wxBoxSizer *top_sizer         = new wxBoxSizer(wxVERTICAL);
    const int   horizontal_margin = FromDIP(16);
    const int   vertical_margin   = FromDIP(15);
    const int   vertical_padding  = FromDIP(12);
    const int   ratio_spacing     = FromDIP(4);

    SetBackgroundColour(BackGroundColor);

    radio_btns.resize(ButtonType::btCount);
    button_labels.resize(ButtonType::btCount);
    button_desps.resize(ButtonType::btCount);
    detail_infos.resize(ButtonType::btCount);
    std::vector<wxString> btn_texts    = {AutoForFlushLabel, AutoForMatchLabel, ManualLabel};
    std::vector<wxString> btn_desps    = {AutoForFlushDesp, AutoForMatchDesp, ManualDesp};
    std::vector<wxString> mode_details = {AutoForFlushDetail, AutoForMatchDetail, ManualDetail};

    top_sizer->AddSpacer(vertical_margin);

    auto checked_bmp   = create_scaled_bitmap("map_mode_on", nullptr, 16);
    auto unchecked_bmp = create_scaled_bitmap("map_mode_off", nullptr, 16);

    for (size_t idx = 0; idx < ButtonType::btCount; ++idx) {
        wxBoxSizer *button_sizer = new wxBoxSizer(wxHORIZONTAL);
        radio_btns[idx]          = new wxBitmapButton(this, idx, unchecked_bmp, wxDefaultPosition, wxDefaultSize, wxNO_BORDER);
        radio_btns[idx]->SetBackgroundColour(BackGroundColor);

        button_labels[idx] = new Label(this, btn_texts[idx]);
        button_labels[idx]->SetBackgroundColour(BackGroundColor);
        button_labels[idx]->SetForegroundColour(LabelEnableColor);
        button_labels[idx]->SetFont(Label::Body_14);

        button_desps[idx] = new Label(this, btn_desps[idx]);
        button_desps[idx]->SetBackgroundColour(BackGroundColor);
        button_desps[idx]->SetForegroundColour(LabelEnableColor);
        button_desps[idx]->SetFont(Label::Body_14);

        button_sizer->Add(radio_btns[idx], 0, wxALIGN_CENTER_VERTICAL);
        button_sizer->AddSpacer(ratio_spacing);
        button_sizer->Add(button_labels[idx], 0, wxALIGN_CENTER_VERTICAL);
        button_sizer->Add(button_desps[idx], 0, wxALIGN_CENTER_VERTICAL);

        wxBoxSizer *label_sizer = new wxBoxSizer(wxHORIZONTAL);

        detail_infos[idx] = new Label(this, mode_details[idx]);
        detail_infos[idx]->SetBackgroundColour(BackGroundColor);
        detail_infos[idx]->SetForegroundColour(GreyColor);
        detail_infos[idx]->SetFont(Label::Body_12);
        detail_infos[idx]->Wrap(FromDIP(320));

        label_sizer->AddSpacer(radio_btns[idx]->GetRect().width + ratio_spacing);
        label_sizer->Add(detail_infos[idx], 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

        top_sizer->Add(button_sizer, 0, wxLEFT | wxRIGHT, horizontal_margin);
        top_sizer->Add(label_sizer, 0, wxLEFT | wxRIGHT, horizontal_margin);
        top_sizer->AddSpacer(vertical_padding);

        radio_btns[idx]->Bind(wxEVT_LEFT_DOWN, [this, idx](auto &) { OnRadioBtn(idx);});

        radio_btns[idx]->Bind(wxEVT_ENTER_WINDOW, [this, idx](auto &) { UpdateButtonStatus(idx); });
        radio_btns[idx]->Bind(wxEVT_LEAVE_WINDOW, [this](auto &) { UpdateButtonStatus(); });

        button_labels[idx]->Bind(wxEVT_LEFT_DOWN, [this, idx](auto &) { OnRadioBtn(idx);});
        button_labels[idx]->Bind(wxEVT_ENTER_WINDOW, [this, idx](auto &) { UpdateButtonStatus(idx); });
        button_labels[idx]->Bind(wxEVT_LEAVE_WINDOW, [this](auto &) { UpdateButtonStatus(); });
    }

    {
        wxBoxSizer *button_sizer = new wxBoxSizer(wxHORIZONTAL);

        auto* wiki_sizer = new wxBoxSizer(wxHORIZONTAL);
        wiki_link = new wxStaticText(this, wxID_ANY, _L("More info on wiki"));
        wiki_link->SetBackgroundColour(BackGroundColor);
        wiki_link->SetForegroundColour(GreenColor);
        wiki_link->SetFont(Label::Body_12.Underlined());
        wiki_link->SetCursor(wxCursor(wxCURSOR_HAND));
        wiki_link->Bind(wxEVT_LEFT_DOWN, [](wxMouseEvent &) { wxLaunchDefaultBrowser("http//:example.com"); });
        wiki_sizer->Add(wiki_link, 0, wxALIGN_CENTER | wxALL, FromDIP(3));

        auto* checkbox_sizer = new wxBoxSizer(wxHORIZONTAL);
        remind_checkbox = new CheckBox(this);
        remind_checkbox->Bind(wxEVT_TOGGLEBUTTON, &FilamentGroupPopup::OnRemindBtn, this);
        checkbox_sizer->Add(remind_checkbox, 0, wxALIGN_CENTER, 0);

        auto checkbox_title = new Label(this, _L("Don't remind me again"));
        checkbox_title->SetBackgroundColour(BackGroundColor);
        checkbox_title->SetForegroundColour(GreyColor);
        checkbox_title->SetFont(Label::Body_12);
        checkbox_sizer->Add(checkbox_title, 0, wxALIGN_CENTER | wxALL, FromDIP(3));

        button_sizer->Add(wiki_sizer, 0, wxLEFT, horizontal_margin);
        button_sizer->AddStretchSpacer();
        button_sizer->Add(checkbox_sizer, 0, wxRIGHT, horizontal_margin);

        top_sizer->Add(button_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, horizontal_margin);
    }

    top_sizer->AddSpacer(vertical_margin);
    SetSizerAndFit(top_sizer);

    m_mode  = get_prefered_map_mode();
    m_timer = new wxTimer(this);

    GUI::wxGetApp().UpdateDarkUIWin(this);

    Bind(wxEVT_TIMER, &FilamentGroupPopup::OnTimer, this);
    Bind(wxEVT_ENTER_WINDOW, &FilamentGroupPopup::OnEnterWindow, this);
    Bind(wxEVT_LEAVE_WINDOW, &FilamentGroupPopup::OnLeaveWindow, this);
}

void FilamentGroupPopup::DrawRoundedCorner(int radius)
{
#ifdef __WIN32__
    HWND hwnd = GetHWND();
    if (hwnd) {
        HRGN hrgn = CreateRoundRectRgn(0, 0, GetRect().GetWidth(), GetRect().GetHeight(), radius, radius);
        SetWindowRgn(hwnd, hrgn, TRUE);

        SetWindowLong(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
        SetLayeredWindowAttributes(hwnd, 0, 0, LWA_COLORKEY);
    }
#else
    wxClientDC         dc(this);
    wxGraphicsContext *gc = wxGraphicsContext::Create(dc);
    if (gc) {
        gc->SetBrush(*wxWHITE_BRUSH);
        gc->SetPen(*wxTRANSPARENT_PEN);
        wxRect         rect(0, 0, GetSize().GetWidth(), GetSize().GetHeight());
        wxGraphicsPath path = wxGraphicsRenderer::GetDefaultRenderer()->CreatePath();
        path.AddRoundedRectangle(0, 0, rect.width, rect.height, radius);
        gc->DrawPath(path);
        delete gc;
    }
#endif
}

void FilamentGroupPopup::Init()
{
    radio_btns[ButtonType::btForMatch]->Enable(m_connected);
    if (m_connected)
        button_labels[ButtonType::btForMatch]->SetForegroundColour(LabelEnableColor);
    else
        button_labels[ButtonType::btForMatch]->SetForegroundColour(LabelDisableColor);


    if (m_connected) {
        button_desps[ButtonType::btForMatch]->SetLabel(AutoForMatchDesp);
    }
    else {
        button_desps[ButtonType::btForMatch]->SetLabel(MachineSyncTip);
    }

    m_mode = get_prefered_map_mode();
    if (m_mode == fmmAutoForMatch && !m_connected) {
        set_prefered_map_mode(fmmAutoForFlush);
        m_mode = fmmAutoForFlush;
    }

    bool check_state = get_pop_up_remind_flag() ? false : true;
    remind_checkbox->SetValue(check_state);

    UpdateButtonStatus();
}

void FilamentGroupPopup::tryPopup(bool connect_status)
{
    auto canPopup = [this]() {
        bool is_multi_extruder_ = is_multi_extruder();
        bool pop_up_flag        = get_pop_up_remind_flag();
        return is_multi_extruder_ && pop_up_flag;
    };

    if (canPopup()) {
        m_connected = connect_status;
        DrawRoundedCorner(16);
        Init();
        ResetTimer();
        PopupWindow::Popup();
    }
}

void FilamentGroupPopup::tryClose() { StartTimer(); }

void FilamentGroupPopup::StartTimer() { m_timer->StartOnce(300); }

void FilamentGroupPopup::ResetTimer()
{
    if (m_timer->IsRunning()) { m_timer->Stop(); }
}

void FilamentGroupPopup::OnRadioBtn(int idx)
{
    m_mode = mode_list.at(idx);

    set_prefered_map_mode(m_mode);

    UpdateButtonStatus(m_mode);
}

void FilamentGroupPopup::OnRemindBtn(wxCommandEvent &event)
{
    bool is_checked = remind_checkbox->GetValue();
    remind_checkbox->SetValue(is_checked);
    set_pop_up_remind_flag(!is_checked);

    if (is_checked) {
        MessageDialog dialog(nullptr, _L("No further pop up.You can go to \"Preferences\" to reopen the pop up."), _L("Tips"), wxICON_INFORMATION | wxOK);
        dialog.ShowModal();
        Dismiss();
    }
}

void FilamentGroupPopup::OnTimer(wxTimerEvent &event) { Dismiss(); }

void FilamentGroupPopup::OnLeaveWindow(wxMouseEvent &)
{
    wxPoint pos = this->ScreenToClient(wxGetMousePosition());
    if (this->GetClientRect().Contains(pos)) return;
    StartTimer();
}

void FilamentGroupPopup::OnEnterWindow(wxMouseEvent &) { ResetTimer(); }

void FilamentGroupPopup::UpdateButtonStatus(int hover_idx)
{
    auto checked_bmp         = create_scaled_bitmap("map_mode_on", nullptr, 16);
    auto unchecked_bmp       = create_scaled_bitmap("map_mode_off", nullptr, 16);
    auto checked_hover_bmp   = create_scaled_bitmap("map_mode_on_hovered", nullptr);
    auto unchecked_hover_bmp = create_scaled_bitmap("map_mode_off_hovered", nullptr);

    for (int i = 0; i < ButtonType::btCount; ++i) {
        if (ButtonType::btForMatch == i && !m_connected) {
            button_labels[i]->SetFont(Label::Body_14);
            continue;
        }
        // process checked and unchecked status
        if (mode_list.at(i) == m_mode) {
            if (i == hover_idx)
                radio_btns[i]->SetBitmap(checked_hover_bmp);
            else
                radio_btns[i]->SetBitmap(checked_bmp);
            button_labels[i]->SetFont(Label::Head_14);
        } else {
            if (i == hover_idx)
                radio_btns[i]->SetBitmap(unchecked_hover_bmp);
            else
                radio_btns[i]->SetBitmap(unchecked_bmp);
            button_labels[i]->SetFont(Label::Body_14);
        }
    }

    if (m_mode == FilamentMapMode::fmmAutoForMatch)
        remind_checkbox->Enable(false);
    else
        remind_checkbox->Enable(true);
    Fit();
    Layout();
}

}} // namespace Slic3r::GUI