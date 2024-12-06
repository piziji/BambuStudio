#include "FanControl.hpp"
#include "Label.hpp"
#include "../BitmapCache.hpp"
#include "../I18N.hpp"
#include "../GUI_App.hpp"

#include <wx/simplebook.h>
#include <wx/dcgraph.h>

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_FAN_SWITCH_ON, wxCommandEvent);
wxDEFINE_EVENT(EVT_FAN_SWITCH_OFF, wxCommandEvent);
wxDEFINE_EVENT(EVT_FAN_ADD, wxCommandEvent);
wxDEFINE_EVENT(EVT_FAN_DEC, wxCommandEvent);
wxDEFINE_EVENT(EVT_FAN_CHANGED, wxCommandEvent);

constexpr int time_out = 20;

/*************************************************
Description:Fan
**************************************************/
Fan::Fan(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    create(parent, id, pos, size);
}

void Fan::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    m_current_speeds  = 0;

    wxWindow::Create(parent, id, pos, size, wxBORDER_NONE);
    SetBackgroundColour(*wxWHITE);

    m_rotate_offsets.push_back(RotateOffSet{ 2.5, wxPoint(-FromDIP(16), FromDIP(11)) });
    m_rotate_offsets.push_back(RotateOffSet{ 2.2, wxPoint(-FromDIP(20), FromDIP(11)) });
    m_rotate_offsets.push_back(RotateOffSet{ 1.7, wxPoint(-FromDIP(24), FromDIP(12)) });
    m_rotate_offsets.push_back(RotateOffSet{ 1.2, wxPoint(-FromDIP(22), FromDIP(4)) });
    m_rotate_offsets.push_back(RotateOffSet{ 0.7, wxPoint(-FromDIP(17), -FromDIP(6)) });
    m_rotate_offsets.push_back(RotateOffSet{ 0.3, wxPoint(-FromDIP(8), -FromDIP(11)) });
    m_rotate_offsets.push_back(RotateOffSet{ 6.1, wxPoint(-FromDIP(0), -FromDIP(9)) });
    m_rotate_offsets.push_back(RotateOffSet{ 5.5, wxPoint(-FromDIP(4), -FromDIP(2)) });
    m_rotate_offsets.push_back(RotateOffSet{ 5.1, wxPoint(-FromDIP(3), FromDIP(5)) });
    m_rotate_offsets.push_back(RotateOffSet{ 4.6, wxPoint(-FromDIP(3), FromDIP(14)) });
    m_rotate_offsets.push_back(RotateOffSet{ 4.0, wxPoint(-FromDIP(2), FromDIP(11)) });

    //auto m_bitmap_pointer  = ScalableBitmap(this, "fan_pointer", FromDIP(25));
    //m_img_pointer     = m_bitmap_pointer.bmp().ConvertToImage();

    m_bitmap_bk  = ScalableBitmap(this, "fan_dash_bk", FromDIP(80));

    for (auto i = 0; i <= 10; i++) {
#ifdef __APPLE__
        auto m_bitmap_scale  = ScalableBitmap(this, wxString::Format("fan_scale_%d", i).ToStdString(), FromDIP(60));
        m_bitmap_scales.push_back(m_bitmap_scale);
#else
        auto m_bitmap_scale  = ScalableBitmap(this, wxString::Format("fan_scale_%d", i).ToStdString(), FromDIP(46));
        m_bitmap_scales.push_back(m_bitmap_scale);
#endif
        
    }

//#ifdef __APPLE__
//    SetMinSize(wxSize(FromDIP(100), FromDIP(100) + FromDIP(6)));
//    SetMaxSize(wxSize(FromDIP(100), FromDIP(100) + FromDIP(6)));
//#else
    SetMinSize(wxSize(m_bitmap_bk.GetBmpSize().x, m_bitmap_bk.GetBmpSize().y + FromDIP(6)));
    SetMaxSize(wxSize(m_bitmap_bk.GetBmpSize().x, m_bitmap_bk.GetBmpSize().y + FromDIP(6)));
//#endif // __APPLE__
    
    Bind(wxEVT_PAINT, &Fan::paintEvent, this);
}

void Fan::set_fan_speeds(int g)
{
    m_current_speeds = g;
    Refresh();
}

void Fan::post_event(wxCommandEvent &&event)
{
    /*event.SetString(m_info.can_id);
    event.SetEventObject(m_parent);
    wxPostEvent(m_parent, event);
    event.Skip();*/
}

void Fan::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void Fan::render(wxDC& dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({ 0, 0 }, size, &dc, { 0, 0 });

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

void Fan::doRender(wxDC& dc)
{
    auto rpm = wxT("rpm");

    wxSize size = GetSize();
    dc.DrawBitmap(m_bitmap_bk.bmp(), wxPoint(0,0));

    //fan scale
    /*auto central_point = wxPoint(size.x / 2, size.y / 2 + FromDIP(15));
    dc.DrawBitmap(m_bitmap_scale_0.bmp(), central_point.x - FromDIP(38), central_point.y);
    dc.DrawBitmap(m_bitmap_scale_1.bmp(), central_point.x - FromDIP(40), central_point.y - FromDIP(17));
    dc.DrawBitmap(m_bitmap_scale_2.bmp(), central_point.x - FromDIP(40), central_point.y - FromDIP(36));
    dc.DrawBitmap(m_bitmap_scale_3.bmp(), central_point.x - FromDIP(32), central_point.y - FromDIP(48));
    dc.DrawBitmap(m_bitmap_scale_4.bmp(), central_point.x - FromDIP(18), central_point.y - FromDIP(53));
    dc.DrawBitmap(m_bitmap_scale_5.bmp(), central_point.x - FromDIP(0),  central_point.y  - FromDIP(53));
    dc.DrawBitmap(m_bitmap_scale_6.bmp(), central_point.x + FromDIP(18), central_point.y - FromDIP(48));
    dc.DrawBitmap(m_bitmap_scale_7.bmp(), central_point.x + FromDIP(31), central_point.y - FromDIP(36));
    dc.DrawBitmap(m_bitmap_scale_8.bmp(), central_point.x + FromDIP(36), central_point.y - FromDIP(17));
    dc.DrawBitmap(m_bitmap_scale_9.bmp(), central_point.x + FromDIP(28), central_point.y);*/

    //fan pointer
    //auto pointer_central_point = wxPoint((size.x - m_img_pointer.GetSize().x) / 2, (size.y - m_img_pointer.GetSize().y) / 2);
    //auto bmp = m_img_pointer.Rotate(m_rotate_offsets[m_current_speeds].rotate, wxPoint(size.x / 2,size.y / 2));
    auto central_point = wxPoint((size.x  - m_bitmap_scales[m_current_speeds].GetBmpSize().x) / 2, (size.y  - m_bitmap_scales[m_current_speeds].GetBmpSize().y) / 2 - FromDIP(4));
    dc.DrawBitmap(m_bitmap_scales[m_current_speeds].bmp(), central_point.x, central_point.y);

    //fan val
    dc.SetTextForeground(DRAW_TEXT_COLOUR);
    dc.SetFont(::Label::Head_13);
    auto speeds = wxString::Format("%d%%", m_current_speeds * 10);
    dc.DrawText(speeds, (size.x - dc.GetTextExtent(speeds).x) / 2 + FromDIP(2), size.y - dc.GetTextExtent(speeds).y - FromDIP(5));

    //rpm
    //dc.SetFont(::Label::Body_13);
    //dc.DrawText(rpm, (size.x - dc.GetTextExtent(rpm).x) / 2, size.y - dc.GetTextExtent(rpm).y);
}

void Fan::msw_rescale() {
   m_bitmap_bk.msw_rescale();
}

void Fan::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    wxWindow::DoSetSize(x, y, width, height, sizeFlags);
}


/*************************************************
Description:FanOperate
**************************************************/
FanOperate::FanOperate(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    m_current_speeds = 0;
    m_min_speeds     = 1;
    m_max_speeds     = 10;
    create(parent, id, pos, size);
}

void FanOperate::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    
    wxWindow::Create(parent, id, pos, size, wxBORDER_NONE);
    SetBackgroundColour(*wxWHITE);

    m_bitmap_add        = ScalableBitmap(this, "fan_control_add", FromDIP(24));
    m_bitmap_decrease   = ScalableBitmap(this, "fan_control_decrease", FromDIP(24));

    SetMinSize(wxSize(FromDIP(SIZE_OF_FAN_OPERATE.x), FromDIP(SIZE_OF_FAN_OPERATE.y)));
    Bind(wxEVT_PAINT, &FanOperate::paintEvent, this);
    Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND);});
    Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW);});
    Bind(wxEVT_LEFT_DOWN, &FanOperate::on_left_down, this);
}

void FanOperate::on_left_down(wxMouseEvent& event)
{
     auto mouse_pos = ClientToScreen(event.GetPosition());
     auto win_pos = ClientToScreen(wxPoint(0, 0));

     auto decrease_fir = FromDIP(24);
     auto add_fir = GetSize().x - FromDIP(24);

     if (mouse_pos.x > win_pos.x && mouse_pos.x < (decrease_fir + win_pos.x) && mouse_pos.y > win_pos.y && mouse_pos.y < (win_pos.y + GetSize().y)) {
         decrease_fan_speeds();
         return;
     }

     if (mouse_pos.x > (add_fir + win_pos.x) && mouse_pos.x < (win_pos.x + GetSize().x) && mouse_pos.y > win_pos.y && mouse_pos.y < (win_pos.y + GetSize().y)) {
         add_fan_speeds();
         return;
     }
}

void FanOperate::set_fan_speeds(int g)
{
    m_current_speeds = g;
    Refresh();
}

void FanOperate::add_fan_speeds()
{
    if (m_current_speeds + 1 > m_max_speeds) return;
    set_fan_speeds(++m_current_speeds);
    post_event(wxCommandEvent(EVT_FAN_ADD)); 
    post_event(wxCommandEvent(EVT_FAN_SWITCH_ON)); 
}

void FanOperate::decrease_fan_speeds()
{
    //turn off
    if (m_current_speeds - 1 < m_min_speeds) {
        m_current_speeds = 0;
        set_fan_speeds(m_current_speeds);
        post_event(wxCommandEvent(EVT_FAN_SWITCH_OFF));
    }
    else {
        set_fan_speeds(--m_current_speeds);
    }
     post_event(wxCommandEvent(EVT_FAN_DEC));
    
}

void FanOperate::post_event(wxCommandEvent &&event)
{
    event.SetInt(m_current_speeds);
    event.SetEventObject(this);
    wxPostEvent(this, event);
    event.Skip();
}

void FanOperate::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void FanOperate::render(wxDC& dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({ 0, 0 }, size, &dc, { 0, 0 });

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

void FanOperate::doRender(wxDC& dc)
{
    wxSize size = GetSize();
    dc.SetPen(wxPen(DRAW_OPERATE_LINE_COLOUR));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRoundedRectangle(0,0,size.x,size.y,5);

    //splt
    auto left_fir = size.x / 3;

    dc.DrawBitmap(m_bitmap_decrease.bmp(), 0, (size.y - m_bitmap_decrease.GetBmpHeight()) / 2);
    dc.DrawBitmap(m_bitmap_add.bmp(), size.x - m_bitmap_add.GetBmpWidth(), (size.y - m_bitmap_add.GetBmpSize().y) / 2);

    wxPoint pot(m_bitmap_decrease.GetBmpWidth(), (size.y - m_bitmap_decrease.GetBmpHeight()) / 2);
    dc.DrawLine(pot.x, 0, pot.x, size.y);
    dc.DrawLine(size.x - m_bitmap_add.GetBmpWidth(), 0, size.x - m_bitmap_add.GetBmpWidth(), size.y);

    //txt
    dc.SetFont(::Label::Body_12);
    dc.SetTextForeground(StateColor::darkModeColorFor(wxColour(0x898989)));
    wxString text = wxString::Format("%d%%", m_current_speeds * 10);
    wxSize text_size = dc.GetTextExtent(text);
    auto text_width = size.x - m_bitmap_decrease.GetBmpWidth() * 2;
    dc.DrawText(text, wxPoint(pot.x + (text_width - text_size.x) / 2, (size.y - text_size.y) / 2));
}

void FanOperate::msw_rescale() {
}

static void nop_deleter_fan_control(FanControlNew* ){}
    /*************************************************
Description:FanControlNew
**************************************************/
FanControlNew::FanControlNew(wxWindow *parent, const AirDuctData &fan_data, int mode_id, int part_id, wxWindowID id, const wxPoint &pos, const wxSize &size)
    : wxWindow(parent, id, pos, size)
    , m_fan_data(fan_data)
    , m_mode_id(mode_id)
    , m_part_id(part_id)
{
    SetMaxSize(wxSize(FromDIP(180), FromDIP(80)));
    SetMinSize(wxSize(FromDIP(180), FromDIP(80)));
    auto m_bitmap_fan = new ScalableBitmap(this, "fan_icon", 20);
    m_bitmap_toggle_off = new ScalableBitmap(this, "toggle_off", 16);
    m_bitmap_toggle_on = new ScalableBitmap(this, "toggle_on", 16);

    SetBackgroundColour(wxColour(248, 248, 248));

    wxBoxSizer* m_sizer_main = new wxBoxSizer(wxHORIZONTAL);

    //m_sizer_main->Add(0, 0, 0, wxLEFT, FromDIP(18));

    wxBoxSizer* sizer_control = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* sizer_control_top = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_control_bottom = new wxBoxSizer(wxVERTICAL);

    auto m_static_bitmap_fan = new wxStaticBitmap(this, wxID_ANY, m_bitmap_fan->bmp(), wxDefaultPosition, wxDefaultSize);

    m_static_name = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END | wxALIGN_CENTER_HORIZONTAL);
    m_static_name->SetForegroundColour(wxColour(DRAW_HEAD_TEXT_COLOUR));
    m_static_name->SetBackgroundColour(wxColour(248, 248, 248));
    m_static_name->SetFont(Label::Head_18);
    m_static_name->SetMinSize(wxSize(FromDIP(100), -1));
    m_static_name->SetMaxSize(wxSize(FromDIP(100), -1));

    m_switch_button = new wxStaticBitmap(this, wxID_ANY, m_bitmap_toggle_off->bmp(), wxDefaultPosition, wxDefaultSize, 0);
    m_switch_button->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    m_switch_button->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_RIGHT_ARROW); });
    m_switch_button->Bind(wxEVT_LEFT_DOWN, &FanControlNew::on_swith_fan, this);


    sizer_control_top->Add(m_static_bitmap_fan, 0, wxLEFT | wxTOP, FromDIP(8));
    sizer_control_top->Add(m_static_name, 0, wxLEFT | wxTOP, FromDIP(5));
    sizer_control_top->Add(0, 0, 1, wxEXPAND, 0);
    sizer_control_top->Add(m_switch_button, 0, wxALIGN_RIGHT | wxRIGHT | wxTOP, FromDIP(10));

    sizer_control->Add(sizer_control_top, 0, wxEXPAND, 0);

    m_static_status_name = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END | wxALIGN_CENTER_HORIZONTAL);
    m_static_status_name->SetForegroundColour(wxColour(0x00AE42));
    m_static_status_name->SetBackgroundColour(wxColour(248, 248, 248));
    m_static_status_name->SetFont(Label::Head_18);
    m_static_status_name->SetMinSize(wxSize(FromDIP(100), -1));
    m_static_status_name->SetMaxSize(wxSize(FromDIP(100), -1));
    m_fan_operate = new FanOperate(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);

    m_fan_operate->Bind(EVT_FAN_SWITCH_ON, [this](const wxCommandEvent &e) {
        m_current_speed = e.GetInt();
        m_switch_button->SetBitmap(m_bitmap_toggle_on->bmp());
        m_switch_fan = true;
    });
    m_fan_operate->Bind(EVT_FAN_SWITCH_OFF, [this](const wxCommandEvent &e) {
        m_current_speed = e.GetInt();
        m_switch_button->SetBitmap(m_bitmap_toggle_off->bmp());
        m_switch_fan = false;
    });

    m_fan_operate->Bind(EVT_FAN_ADD, [this](const wxCommandEvent &e) {
        m_current_speed = e.GetInt();
        command_control_fan();
    });

    m_sizer_control_bottom->Add(m_static_status_name, 0, wxALL, FromDIP(10));
    m_sizer_control_bottom->Add(m_fan_operate, 0, wxALL, FromDIP(10));

    sizer_control->Add(m_sizer_control_bottom, 0, wxALL, 0);
    m_sizer_main->Add(sizer_control, 0, wxALIGN_CENTER, 0);
    update_mode();

    this->SetSizer(m_sizer_main);
    this->Layout();
    m_sizer_main->Fit(this);
}

void FanControlNew::on_left_down(wxMouseEvent& evt)
{
    auto mouse_pos = ClientToScreen(evt.GetPosition());
    auto tag_pos = m_fan_operate->ScreenToClient(mouse_pos);
    evt.SetPosition(tag_pos);
    m_fan_operate->on_left_down(evt);
}

void FanControlNew::command_control_fan()
{
    if (m_current_speed < 0 || m_current_speed > 10) { return; }

    BOOST_LOG_TRIVIAL(info) << "Functions Need to be supplemented! :FanControlNew::command_control_fan. the speed may change";
    token.reset(this, nop_deleter_fan_control);
    if (m_obj) {
        if (!m_obj->is_enable_np){
            int speed = floor(m_current_speed * float(25.5));
            m_obj->command_control_fan(m_part_id, speed);
        } else {
            m_obj->command_control_fan_new(m_part_id, m_current_speed, [this, w = std::weak_ptr<FanControlNew>(token)](const json &reply) {
                if (w.expired())
                    return;
                post_event(1);
            });
        }
        post_event(0);
    }
}

void FanControlNew::on_swith_fan(wxMouseEvent& evt)
{
    int speed = 0;
    if (m_switch_fan) {
        m_switch_button->SetBitmap(m_bitmap_toggle_off->bmp());
        m_switch_fan = false;
    }
    else {
        speed = 255;
        m_switch_button->SetBitmap(m_bitmap_toggle_on->bmp());
        m_switch_fan = true;
    }

    set_fan_speed(speed);
    command_control_fan();
}

void FanControlNew::on_swith_fan(bool on)
{
    m_switch_fan = on;
    if (m_switch_fan) {
        m_switch_button->SetBitmap(m_bitmap_toggle_on->bmp());
    }
    else {
        m_switch_button->SetBitmap(m_bitmap_toggle_off->bmp());
    }
}

void FanControlNew::update_mode()
{
    int cur_mode = m_mode_id;
    if (cur_mode < 0) {
        m_static_status_name->Hide();
        m_fan_operate->Show();
    } else {
        auto mode = m_fan_data.modes[cur_mode];
        auto it   = std::find(mode.off.begin(), mode.off.end(), m_part_id);

        if (it != mode.off.end()) {
            m_show_mode = 2;
            m_static_status_name->SetLabelText(_L("Off"));
        } else {
            auto it_ctrl = std::find(mode.ctrl.begin(), mode.ctrl.end(), m_part_id);
            if (it_ctrl == mode.ctrl.end()) {
                m_show_mode = 1;
                m_static_status_name->SetLabelText(_L("Auto"));
            } else {
                m_show_mode = 0;
            }
        }

        if (m_show_mode == 0) {
            m_static_status_name->Hide();
            m_switch_button->Show();
            m_fan_operate->Show();
        } else {
            m_static_status_name->Show();
            m_switch_button->Hide();
            m_fan_operate->Hide();
        }
    }
}

void FanControlNew::set_machine_obj(MachineObject* obj)
{
    m_update_already = true;
    m_obj = obj;
}

void FanControlNew::set_name(wxString name) {
    m_static_name->SetLabelText(name);
}

void FanControlNew::set_fan_speed(int g)
{
    if (g < 0 || g > 255) return;
    int speed = round(float(g) / float(25.5));

    if (m_current_speed != speed) {
        m_current_speed = speed;
        m_fan_operate->set_fan_speeds(m_current_speed);

        if (m_current_speed <= 0) {
            on_swith_fan(false);
        }
        else {
            on_swith_fan(true);
        }
    }
}

void FanControlNew::set_fan_speed_percent(int speed)
{
    if (m_current_speed != speed) {
        m_current_speed = speed;
        m_fan_operate->set_fan_speeds(m_current_speed);

        if (m_current_speed <= 0) {
            on_swith_fan(false);
        } else {
            on_swith_fan(true);
        }
    }
}

void FanControlNew::set_fan_switch(bool s)
{
}

void FanControlNew::post_event(int type)
{
    auto event = wxCommandEvent(EVT_FAN_CHANGED);
    event.SetInt(type);
    event.SetString(wxString::Format("%d", m_current_speed));
    event.SetEventObject(GetParent());
    wxPostEvent(GetParent(), event);
    event.Skip();
}

/*************************************************
Description:FanControlPopupNew
**************************************************/
static void nop_deleter_fan_control_popup(FanControlPopupNew *) {}
FanControlPopupNew::FanControlPopupNew(wxWindow* parent, MachineObject* obj,AirDuctData data)
    :PopupWindow(parent, wxBORDER_NONE)
{
    SetBackgroundColour(*wxWHITE);
    init_names();

    m_data = data;
    m_obj = obj;

    m_sizer_main = new wxBoxSizer(wxVERTICAL);
    m_radio_btn_sizer = new wxGridSizer( 0, 3, 3, 3 );
    m_sizer_fanControl = new wxGridSizer( 0, 3, 3, 10 );

    m_mode_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_button_refresh = new Button(this, wxString(""), "fan_poppingup_refresh", 0, FromDIP(24));
    m_button_refresh->SetBackgroundColor(*wxWHITE);
    m_button_refresh->SetBorderColor(*wxWHITE);
    m_button_refresh->SetMinSize(wxSize(FromDIP(26), FromDIP(26)));
    m_button_refresh->SetMaxSize(wxSize(FromDIP(26), FromDIP(26)));

    m_mode_sizer->Add(m_radio_btn_sizer, 0, wxALIGN_CENTRE_VERTICAL, 0);
    m_mode_sizer->Add(m_button_refresh, 0, wxALIGN_CENTRE_VERTICAL, 0);

    m_cooling_text = new wxStaticText(this, wxID_ANY, wxT(""));
    m_cooling_text->SetBackgroundColour(*wxWHITE);

    //Control the show or hide of controls based on id

    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(23));
    m_sizer_main->Add(m_mode_sizer, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT, FromDIP(30));
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(10));
    m_sizer_main->Add(m_cooling_text, 0, wxLEFT | wxRIGHT, FromDIP(30));
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(10));
    m_sizer_main->Add(m_sizer_fanControl, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(16));

    CreateDuct();

    SetSizer(m_sizer_main);
    Layout();
    Fit();

    this->Centre(wxBOTH);
    Bind(wxEVT_PAINT, &FanControlPopupNew::paintEvent, this);

#if __APPLE__
    Bind(wxEVT_LEFT_DOWN, &FanControlPopupNew::on_left_down, this);
#endif

#ifdef __WXOSX__
    Bind(wxEVT_IDLE, [](wxIdleEvent& evt) {});
#endif
    Bind(wxEVT_SHOW, &FanControlPopupNew::on_show, this);
    Bind(EVT_FAN_CHANGED, &FanControlPopupNew::on_fan_changed, this);

    for (const auto& btn : m_mode_switch_btn_list) {
        btn->Bind(wxEVT_LEFT_DOWN, &FanControlPopupNew::on_mode_changed, this);
    }
}

void FanControlPopupNew::CreateDuct(){

    //tips
    UpdateTips(m_data.curren_mode);

    //fan or door
    UpdateParts(m_data.curren_mode);

    if (m_data.modes.empty()) {
        m_button_refresh->Hide();
        return;
    }
    size_t mode_size = m_data.modes.size();
    for (auto i = 0; i < mode_size; i++) {
        wxString text = wxString::Format("%s", radio_btn_name[AIR_DUCT(m_data.modes[i].id)]);
        SendModeSwitchButton *radio_btn = new SendModeSwitchButton(this, text, m_data.curren_mode == m_data.modes[i].id);
        m_mode_switch_btn_list.emplace_back(radio_btn);
        m_radio_btn_sizer->Add(radio_btn, wxALL, FromDIP(5));
    }
}

void FanControlPopupNew::UpdateParts(int mode_id)
{
    m_sizer_fanControl->Clear(true);
    for (const auto& part : m_data.parts) {
        
        auto part_id = part.id;
        auto part_func = part.func;
        auto part_name = fan_func_name[AIR_FUN(part_id)];
        auto part_state = part.state;

        auto fan_control = new FanControlNew(this, m_data, mode_id, part_id, wxID_ANY, wxDefaultPosition, wxDefaultSize);

        fan_control->set_machine_obj(m_obj);
        fan_control->set_name(part_name);

        m_fan_control_list[part_id] = fan_control;
        m_sizer_fanControl->Add(fan_control, 0, wxALL, FromDIP(5));

        /*if (fan_control.type == AIR_FAN_TYPE)
            m_duct_fans_list[fan.id] = fan_control;
        else if (fan_control.type == AIR_DOOR_TYPE)
            m_duct_doors_list[fan.id] = fan_control;*/
    }

    m_sizer_fanControl->Layout();

    /*update state*/

    //m_sizer_fanControl->Clear();
    //if (m_fan_control_list.find(duct_id) == m_fan_control_list.end()) return;
    //for (auto fan_control_list : m_fan_control_list){
    //    if (fan_control_list.first == duct_id) continue;
    //    for (auto fan_control : fan_control_list.second){
    //        fan_control.second->Hide();
    //    }
    //}
    //auto fan_control_new_list = m_fan_control_list[duct_id];
    //for (auto it : fan_control_new_list){
    //    //m_sizer_fanControl->Add(it.second, 0, wxALL | wxEXPAND, 0);
    //    m_sizer_fanControl->Add(it.second, 0, wxALL, 5);
    //    it.second->Show();
    //}
    //m_sizer_fanControl->Layout();
}

void FanControlPopupNew::UpdateTips(int model)
{
    auto text = label_text[AIR_DUCT(model)];
    m_cooling_text->SetLabelText(text);
    m_cooling_text->Wrap(FromDIP(600));
    Layout();
}

void FanControlPopupNew::update_fan_data(MachineObject *obj)
{
    if (!obj)
        return;

    if (obj->is_enable_np) {
        if (m_air_duct_time_out == 0) {
            m_air_duct_time_out--;
            return;
        }
        update_fan_data(obj->m_air_duct_data);
    } else {
        if (m_fan_set_time_out > 0) {
            m_fan_set_time_out--;
            return;
        }
        int cooling_fan_speed = round(obj->cooling_fan_speed / float(25.5));
        int big_fan1_speed    = round(obj->big_fan1_speed / float(25.5));
        int big_fan2_speed    = round(obj->big_fan2_speed / float(25.5));
        update_fan_data(AIR_FUN::FAN_COOLING_0_AIRDOOR, cooling_fan_speed);
        update_fan_data(AIR_FUN::FAN_REMOTE_COOLING_0_IDX, big_fan1_speed);
        update_fan_data(AIR_FUN::FAN_REMOTE_COOLING_0_IDX, big_fan2_speed);
    }
}

void FanControlPopupNew::update_fan_data(const AirDuctData &data)
{
    m_data = data;
    for (const auto& part : m_data.parts) {
        auto part_id    = part.id;
        auto part_func  = part.func;
        auto part_name  = fan_func_name[AIR_FUN(part_id)];
        auto part_state = part.state;

        auto it = m_fan_control_list.find(part_id);
        if (it != m_fan_control_list.end()) {
            auto fan_control = m_fan_control_list[part_id];
            fan_control->set_fan_speed_percent(part_state);
        }
    }
}

void FanControlPopupNew::update_fan_data(AIR_FUN id, int speed) 
{
    for (auto& part : m_data.parts) {
        auto part_id    = part.id;
        auto part_func  = part.func;
        auto part_name  = fan_func_name[AIR_FUN(part_id)];

        if (id == part_id) {
            part.state = speed;
            auto it = m_fan_control_list.find(part_id);
            if (it != m_fan_control_list.end()) {
                auto fan_control = m_fan_control_list[part_id];
                fan_control->update_mode();
                fan_control->set_fan_speed_percent(speed);
            }
        }
    }
}

// device change
void FanControlPopupNew::update_device(AirDuctData data, MachineObject* obj)
{

    //for (int i = 0; i < data.airducts.size(); i++){
    //    auto duct = data.airducts[i];
    //    if (m_fan_control_list.find(duct.airduct_id) == m_fan_control_list.end())
    //        CreateDuct(duct);
    //    else{
    //        auto fan_list = m_fan_control_list[duct.airduct_id];
    //        for (auto fan : duct.fans_list){
    //            if (fan_list.find(fan.id) == fan_list.end())
    //                CreateFanAndDoor(duct.airduct_id, fan);
    //            else{
    //                auto fan_control = fan_list[fan.id];
    //                fan_control->update_fan_data(fan);
    //            }
    //        }
    //    }
    //    m_duct_ctrl[duct.airduct_id] = duct.fans_ctrl[i];
    //}
    //m_data = data;
    //for (auto fan_list_of_duct : m_fan_control_list){
    //    for (auto fan : fan_list_of_duct.second){
    //        if (fan.second != nullptr)
    //            fan.second->set_machine_obj(obj);
    //    }
    //}
    ////auto text = wxString::Format("%s", radio_btn_name[AIR_DUCT_mode_e(m_data.curren_duct)]);
    //auto text = wxT("The fan controls the temperature during printing to improve print quality.The system automatically adjusts the fan's switch and speed according to dif");
    //m_cooling_text->SetLabelText(text);
    //m_cooling_text->Wrap(FromDIP(360));

    //ChangeCoolingTips(m_data.airducts.size());

    //if (data.airducts.size() <= 1)
    //    m_radio_btn_sizer->Show(false);
    //else
    //    m_radio_btn_sizer->Show(true);

    //this->Layout();

    //Bind(EVT_FAN_CHANGED, [this](wxCommandEvent& e) {
    //    post_event(e.GetInt(), e.GetString());
    //    });
}

void FanControlPopupNew::on_left_down(wxMouseEvent& evt)
{
    auto mouse_pos = ClientToScreen(evt.GetPosition());

    for (auto fan_it : m_fan_control_list){
        auto fan     = fan_it.second;
        auto win_pos = fan->m_switch_button->ClientToScreen(wxPoint(0, 0));
        auto size    = fan->m_switch_button->GetSize();
        if (mouse_pos.x > win_pos.x && mouse_pos.x < (win_pos.x + fan->m_switch_button->GetSize().x) && mouse_pos.y > win_pos.y &&
            mouse_pos.y < (win_pos.y + fan->m_switch_button->GetSize().y)) {
            fan->on_swith_fan(evt);
        }
    }

    evt.Skip();
}

void FanControlPopupNew::OnDismiss()
{
}

void FanControlPopupNew::post_event(int fan_type, wxString speed)
{
    // id, speed
    wxCommandEvent event(EVT_FAN_CHANGED);
    event.SetInt(fan_type);
    event.SetString(speed);
    event.SetEventObject(GetParent());
    wxPostEvent(GetParent(), event);
    event.Skip();
}

bool FanControlPopupNew::ProcessLeftDown(wxMouseEvent& event)
{
    return PopupWindow::ProcessLeftDown(event);
}

void FanControlPopupNew::on_show(wxShowEvent& evt)
{
    wxGetApp().UpdateDarkUIWin(this);
}

void FanControlPopupNew::command_control_air_duct(int mode_id)
{
    m_air_duct_time_out = time_out;
    token.reset(this, nop_deleter_fan_control_popup);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", control air duct, id = " << mode_id;
    if (m_obj) {
        m_obj->command_control_air_duct(mode_id, [this, w = std::weak_ptr<FanControlPopupNew>(token), mode_id](const json& reply) {
            if (w.expired())
                return;
            m_air_duct_time_out = 0;
            if (reply.contains("errno")) {
                int result = reply["errno"].get<int>();
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", control air duct, errno = " << result;
                if (result == 0) {
                    int update_id = mode_id;
                    if (reply.contains("modeId")) {
                        update_id = reply["modeId"].get<int>();
                        this->UpdateParts(update_id);
                        this->UpdateTips(update_id);
                    }
                }
                this->Layout();
                this->Refresh();
            }
         });
    }
}

void FanControlPopupNew::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRoundedRectangle(0, 0, GetSize().x, GetSize().y, 0);
}

void FanControlPopupNew::on_mode_changed(const wxMouseEvent &event)
{
    size_t btn_list_size = m_mode_switch_btn_list.size();
    for (size_t i = 0; i < btn_list_size; ++i) {
        if (m_mode_switch_btn_list[i]->GetId() == event.GetId()) {
            if (m_mode_switch_btn_list[i]->isSelected())
                return;
            m_mode_switch_btn_list[i]->setSelected(true);
            command_control_air_duct(i);
        } else {
            m_mode_switch_btn_list[i]->setSelected(false);
        }
    }
}

void FanControlPopupNew::on_fan_changed(const wxCommandEvent &event)
{
    int type           = event.GetInt();
    if (type == 0)
        m_fan_set_time_out = time_out;
    else
        m_fan_set_time_out = 0;
}

void FanControlPopupNew::init_names() {

    //Iint fan/door/func/duct name lists
    radio_btn_name[AIR_DUCT::AIR_DUCT_COOLING_FILT] = _L("Cooling Filter");
    radio_btn_name[AIR_DUCT::AIR_DUCT_HEATING_INTERNAL_FILT] = L("Internal Filter");
    radio_btn_name[AIR_DUCT::AIR_DUCT_EXHAUST] = L("Exhaust");
    radio_btn_name[AIR_DUCT::AIR_DUCT_FULL_COOLING] = L("Full Cooling");
    radio_btn_name[AIR_DUCT::AIR_DUCT_NUM] = L("Num?");
    radio_btn_name[AIR_DUCT::AIR_DUCT_INIT] = L("Init");
    
    fan_func_name[AIR_FUN::FAN_HEAT_BREAK_0_IDX] = _L("Nozzle0");
    fan_func_name[AIR_FUN::FAN_COOLING_0_AIRDOOR] = _L("Part");
    fan_func_name[AIR_FUN::FAN_REMOTE_COOLING_0_IDX] = _L("Aux");
    fan_func_name[AIR_FUN::FAN_CHAMBER_0_IDX] = _L("Chamber");
    fan_func_name[AIR_FUN::FAN_HEAT_BREAK_1_IDX] = _L("Nozzle1");
    fan_func_name[AIR_FUN::FAN_MC_BOARD_0_IDX] = _L("MC Board");
    fan_func_name[AIR_FUN::FAN_INNNER_LOOP_FAN_0_IDX] = _L("Innerloop");
    
    air_door_func_name[AIR_DOOR::AIR_DOOR_FUNC_CHAMBER] = _L("Chamber");
    air_door_func_name[AIR_DOOR::AIR_DOOR_FUNC_INNERLOOP] = _L("Innerloop");
    air_door_func_name[AIR_DOOR::AIR_DOOR_FUNC_TOP] = _L("Top");

    label_text[AIR_DUCT::AIR_DUCT_NONE] = _L("The fan controls the temperature during printing to improve print quality.The system automatically adjusts the fan's switch and speed according to different printing materials.");
    label_text[AIR_DUCT::AIR_DUCT_COOLING_FILT] = L("Cooling-filtering mode is suitable for printing PLA/PETG/TPU materials. In this mode, the chamber temperature is low, and the air in the chamber can be filtered while cooling the printed part.");
    label_text[AIR_DUCT::AIR_DUCT_HEATING_INTERNAL_FILT] = L("Cooling-filtering mode is suitable for printing PLA/PETG/TPU materials. In this mode, the chamber temperature is low, and the air in the chamber can be filtered while cooling the printed part.");
    label_text[AIR_DUCT::AIR_DUCT_EXHAUST] = L("Exhaust");
    label_text[AIR_DUCT::AIR_DUCT_FULL_COOLING] = L("Strong cooling mode is suitable for printing PLA/TPU materials. In this mode, the printed part will be cooled in all directions.");
    label_text[AIR_DUCT::AIR_DUCT_NUM] = _L("Num");
    label_text[AIR_DUCT::AIR_DUCT_INIT] = _L("Init");

    /*label_text[AIR_DUCT_mode_e::AIR_DUCT_NONE] = "...";
    label_text[AIR_DUCT_mode_e::AIR_DUCT_COOLING_FILT] = "...";
    label_text[AIR_DUCT_mode_e::AIR_DUCT_HEATING_INTERNAL_FILT] = "...";
    label_text[AIR_DUCT_mode_e::AIR_DUCT_EXHAUST] = "Exhaust";
    label_text[AIR_DUCT_mode_e::AIR_DUCT_FULL_COOLING] = "...";
    label_text[AIR_DUCT_mode_e::AIR_DUCT_NUM] = "Num";
    label_text[AIR_DUCT_mode_e::AIR_DUCT_INIT] = "Init";*/
}


}} // namespace Slic3r::GUI
