#include "CalibUtils.hpp"
#include "../GUI/I18N.hpp"
#include "../GUI/GUI_App.hpp"
#include "../GUI/DeviceManager.hpp"
#include "../GUI/Jobs/ProgressIndicator.hpp"
#include "../GUI/PartPlate.hpp"
#include "../GUI/OpenGLManager.hpp"

#include "libslic3r/Model.hpp"
#include "../GUI/MsgDialog.hpp"


namespace Slic3r {
namespace GUI {
const float MIN_PA_K_VALUE = 0.0;
const float MAX_PA_K_VALUE = 2.0;

std::shared_ptr<PrintJob> CalibUtils::print_job;
wxString wxstr_temp_dir = fs::path(fs::temp_directory_path() / "calib").wstring();
static const std::string temp_dir = wxstr_temp_dir.utf8_string();
static const std::string temp_gcode_path = temp_dir + "/temp.gcode";
static const std::string path            = temp_dir + "/test.3mf";
static const std::string config_3mf_path = temp_dir + "/test_config.3mf";

static std::string MachineBedTypeString[6] = {
    "auto",
    "pc",
    "ep",
    "pei",
    "pte",
    "suprtack"
};

void get_default_k_n_value(const std::string &filament_id, float &k, float &n)
{
    if (filament_id.compare("GFG00") == 0) {
        // PETG
        k = 0.04;
        n = 1.0;
    } else if (filament_id.compare("GFB00") == 0 || filament_id.compare("GFB50") == 0) {
        // ABS
        k = 0.04;
        n = 1.0;
    } else if (filament_id.compare("GFU01") == 0) {
        // TPU
        k = 0.2;
        n = 1.0;
    } else if (filament_id.compare("GFB01") == 0) {
        // ASA
        k = 0.04;
        n = 1.0;
    } else {
        // PLA , other
        k = 0.02;
        n = 1.0;
    }
}

wxString get_nozzle_volume_type_name(NozzleVolumeType type)
{
    if (NozzleVolumeType::nvtStandard == type) {
        return _L("Standard");
    } else if (NozzleVolumeType::nvtHighFlow == type) {
        return _L("High Flow");
    }
    return wxString();
}

void get_tray_ams_and_slot_id(MachineObject* obj, int in_tray_id, int &ams_id, int &slot_id, int &tray_id)
{
    assert(obj);
    if (!obj)
        return;

    if (in_tray_id == VIRTUAL_TRAY_MAIN_ID || in_tray_id == VIRTUAL_TRAY_DEPUTY_ID) {
        ams_id  = in_tray_id;
        slot_id = 0;
        tray_id = ams_id;
        if (!obj->is_enable_np)
            tray_id = VIRTUAL_TRAY_DEPUTY_ID;
    } else {
        ams_id  = in_tray_id / 4;
        slot_id = in_tray_id % 4;
        tray_id = in_tray_id;
    }
}

std::string get_calib_mode_name(CalibMode cali_mode, int stage)
{
    switch(cali_mode) {
    case CalibMode::Calib_PA_Line:
        return "pa_line_calib_mode";
    case CalibMode::Calib_PA_Pattern:
        return "pa_pattern_calib_mode";
    case CalibMode::Calib_Flow_Rate:
        if (stage == 1)
            return "flow_rate_coarse_calib_mode";
        else if (stage == 2)
            return "flow_rate_fine_calib_mode";
        else
            return "flow_rate_coarse_calib_mode";
    case CalibMode::Calib_Temp_Tower:
        return "temp_tower_calib_mode";
    case CalibMode::Calib_Vol_speed_Tower:
        return "vol_speed_tower_calib_mode";
    case CalibMode::Calib_VFA_Tower:
        return "vfa_tower_calib_mode";
    case CalibMode::Calib_Retraction_tower:
        return "retration_tower_calib_mode";
    default:
        assert(false);
        return "";
    }
}

static wxString to_wstring_name(std::string name)
{
    if (name == "hardened_steel") {
        return _L("Hardened Steel");
    } else if (name == "stainless_steel") {
        return _L("Stainless Steel");
    }

    return wxEmptyString;
}

static bool is_same_nozzle_diameters(const DynamicPrintConfig &full_config, const MachineObject *obj, wxString& error_msg)
{
    if (obj == nullptr)
        return true;

    try {
        std::string nozzle_type;

        const ConfigOptionEnumsGenericNullable * config_nozzle_type = full_config.option<ConfigOptionEnumsGenericNullable>("nozzle_type");
        std::vector<std::string> config_nozzle_types_str(config_nozzle_type->size());
        for (size_t idx = 0; idx < config_nozzle_type->size(); ++idx)
            config_nozzle_types_str[idx] = NozzleTypeEumnToStr[NozzleType(config_nozzle_type->values[idx])];

        auto opt_nozzle_diameters = full_config.option<ConfigOptionFloatsNullable>("nozzle_diameter");

        std::vector<float> config_nozzle_diameters(opt_nozzle_diameters->size());
        for (size_t idx = 0; idx < opt_nozzle_diameters->size(); ++idx)
            config_nozzle_diameters[idx] = opt_nozzle_diameters->values[idx];

        std::vector<float> machine_nozzle_diameters(obj->m_extder_data.extders.size());
        for (size_t idx = 0; idx < obj->m_extder_data.extders.size(); ++idx)
            machine_nozzle_diameters[idx] = obj->m_extder_data.extders[idx].current_nozzle_diameter;

        if (config_nozzle_diameters.size() != machine_nozzle_diameters.size()) {
            wxString nozzle_in_preset  = wxString::Format(_L("nozzle size in preset: %d"), config_nozzle_diameters.size());
            wxString nozzle_in_printer = wxString::Format(_L("nozzle size memorized: %d"), machine_nozzle_diameters.size());
            error_msg = _L("The size of nozzle type in preset is not consistent with memorized nozzle.Did you change your nozzle lately ? ") + "\n    " + nozzle_in_preset +
                "\n    " + nozzle_in_printer + "\n";
            return false;
        }

        for (size_t idx = 0; idx < config_nozzle_diameters.size(); ++idx) {
            if (config_nozzle_diameters[idx] != machine_nozzle_diameters[idx]) {
                wxString nozzle_in_preset = wxString::Format(_L("nozzle[%d] in preset: %.1f"), idx, config_nozzle_diameters[idx]);
                wxString nozzle_in_printer = wxString::Format(_L("nozzle[%d] memorized: %.1f"), idx, machine_nozzle_diameters[idx]);
                error_msg = _L("Your nozzle type in preset is not consistent with memorized nozzle.Did you change your nozzle lately ? ") + "\n    " + nozzle_in_preset +
                    "\n    " + nozzle_in_printer + "\n";
                return false;
            }
        }

    } catch (...) {}

    return true;
}

static bool is_same_nozzle_type(const DynamicPrintConfig &full_config, const MachineObject *obj, wxString& error_msg)
{
    if (obj == nullptr)
        return true;

    NozzleType nozzle_type = obj->m_extder_data.extders[0].current_nozzle_type;
    int printer_nozzle_hrc = Print::get_hrc_by_nozzle_type(nozzle_type);

    if (full_config.has("required_nozzle_HRC")) {
        int filament_nozzle_hrc = full_config.opt_int("required_nozzle_HRC", 0);
        if (abs(filament_nozzle_hrc) > abs(printer_nozzle_hrc)) {
            BOOST_LOG_TRIVIAL(info) << "filaments hardness mismatch:  printer_nozzle_hrc = " << printer_nozzle_hrc << ", filament_nozzle_hrc = " << filament_nozzle_hrc;
            std::string filament_type = full_config.opt_string("filament_type", 0);
            error_msg = wxString::Format(_L("Printing %1s material with %2s nozzle may cause nozzle damage."), filament_type, to_wstring_name(NozzleTypeEumnToStr[obj->m_extder_data.extders[0].current_nozzle_type]));
            error_msg += "\n";

            MessageDialog msg_dlg(nullptr, error_msg, wxEmptyString, wxICON_WARNING | wxOK | wxCANCEL);
            auto          result = msg_dlg.ShowModal();
            if (result == wxID_OK) {
                error_msg.clear();
                return true;
            } else {
                error_msg.clear();
                return false;
            }
        }
    }

    return true;
}

static bool check_nozzle_diameter_and_type(const DynamicPrintConfig &full_config, wxString& error_msg)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) {
        error_msg = _L("Need select printer");
        return false;
    }

    MachineObject *obj = dev->get_selected_machine();
    if (obj == nullptr) {
        error_msg = _L("Need select printer");
        return false;
    }

    if (!Slic3r::GUI::wxGetApp().plater()->check_printer_initialized(obj))
        return false;

    // P1P/S
    if (obj->m_extder_data.extders[0].current_nozzle_type == NozzleType::ntUndefine)
        return true;

    if (!is_same_nozzle_diameters(full_config, obj, error_msg))
        return false;

    if (!is_same_nozzle_type(full_config, obj, error_msg))
        return false;

    return true;
}

static void init_multi_extruder_params_for_cali(DynamicPrintConfig& config, const CalibInfo& calib_info)
{
    int extruder_count = 1;
    auto nozzle_diameters_opt = dynamic_cast<const ConfigOptionFloatsNullable*>(config.option("nozzle_diameter"));
    if (nozzle_diameters_opt != nullptr) {
        extruder_count = (int)(nozzle_diameters_opt->size());
    }
    std::vector<int>& nozzle_volume_types =  dynamic_cast<ConfigOptionEnumsGeneric*>(config.option("nozzle_volume_type", true))->values;
    nozzle_volume_types.clear();
    nozzle_volume_types.resize(extruder_count, (int)calib_info.nozzle_volume_type);

    std::vector<int> physical_extruder_maps = dynamic_cast<ConfigOptionInts*>(config.option("physical_extruder_map", true))->values;
    int extruder_id = calib_info.extruder_id;
    for (size_t index = 0; index < extruder_count; ++index) {
        if (physical_extruder_maps[index] == extruder_id)
        {
            extruder_id = index + 1;
        }
    }

    int num_filaments = 1;
    auto filament_colour_opt = dynamic_cast<const ConfigOptionStrings*>(config.option("filament_colour"));
    if (filament_colour_opt != nullptr) {
        num_filaments = (int)(filament_colour_opt->size());
    }
    std::vector<int>& filament_maps = config.option<ConfigOptionInts>("filament_map", true)->values;
    filament_maps.clear();
    filament_maps.resize(num_filaments, extruder_id);

    config.option<ConfigOptionEnum<FilamentMapMode>>("filament_map_mode", true)->value = FilamentMapMode::fmmManual;

}


CalibMode CalibUtils::get_calib_mode_by_name(const std::string name, int& cali_stage)
{
    if (name == "pa_line_calib_mode") {
        cali_stage = 0;
        return CalibMode::Calib_PA_Line;
    }
    else if (name == "pa_pattern_calib_mode") {
        cali_stage = 1;
        return CalibMode::Calib_PA_Line;
    }
    else if (name == "flow_rate_coarse_calib_mode") {
        cali_stage = 1;
        return CalibMode::Calib_Flow_Rate;
    }
    else if (name == "flow_rate_fine_calib_mode") {
        cali_stage = 2;
        return CalibMode::Calib_Flow_Rate;
    }
    else if (name == "temp_tower_calib_mode")
        return CalibMode::Calib_Temp_Tower;
    else if (name == "vol_speed_tower_calib_mode")
        return CalibMode::Calib_Vol_speed_Tower;
    else if (name == "vfa_tower_calib_mode")
        return CalibMode::Calib_VFA_Tower;
    else if (name == "retration_tower_calib_mode")
        return CalibMode::Calib_Retraction_tower;
    return CalibMode::Calib_None;
}

bool CalibUtils::validate_input_name(wxString name)
{
    if (name.Length() > 40) {
        MessageDialog msg_dlg(nullptr, _L("The name cannot exceed 40 characters."), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }

    name.erase(std::remove(name.begin(), name.end(), L' '), name.end());
    if (name.IsEmpty()) {
        MessageDialog msg_dlg(nullptr, _L("The name cannot be empty."), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }

    return true;
}

bool CalibUtils::validate_input_k_value(wxString k_text, float* output_value)
{
    float default_k = 0.0f;
    if (k_text.IsEmpty()) {
        *output_value = default_k;
        return false;
    }

    double k_value = 0.0;
    try {
        if(!k_text.ToDouble(&k_value))
            return false;
    }
    catch (...) {
        ;
    }

    if (k_value <= MIN_PA_K_VALUE || k_value >= MAX_PA_K_VALUE) {
        *output_value = default_k;
        return false;
    }

    *output_value = k_value;
    return true;
};

bool CalibUtils::validate_input_flow_ratio(wxString flow_ratio, float* output_value) {
    float default_flow_ratio = 1.0f;

    if (flow_ratio.IsEmpty()) {
        *output_value = default_flow_ratio;
        return false;
    }

    double flow_ratio_value = 0.0;
    try {
        flow_ratio.ToDouble(&flow_ratio_value);
    }
    catch (...) {
        ;
    }

    if (flow_ratio_value <= 0.0 || flow_ratio_value >= 2.0) {
        *output_value = default_flow_ratio;
        return false;
    }

    *output_value = flow_ratio_value;
    return true;
}

static void cut_model(Model &model, std::array<Vec3d, 4> plane_points, ModelObjectCutAttributes attributes)
{
    size_t obj_idx = 0;
    size_t instance_idx = 0;
    if (!attributes.has(ModelObjectCutAttribute::KeepUpper) && !attributes.has(ModelObjectCutAttribute::KeepLower))
        return;

    auto* object = model.objects[0];

    const auto new_objects = object->cut(instance_idx, plane_points, attributes);
    model.delete_object(obj_idx);

    for (ModelObject *model_object : new_objects) {
        auto *object = model.add_object(*model_object);
        object->sort_volumes(true);
        std::string object_name = object->name.empty() ? fs::path(object->input_file).filename().string() : object->name;
        object->ensure_on_bed();
    }
}

static void read_model_from_file(const std::string& input_file, Model& model)
{
    LoadStrategy              strategy = LoadStrategy::LoadModel;
    ConfigSubstitutionContext config_substitutions{ForwardCompatibilitySubstitutionRule::Enable};
    int                       plate_to_slice = 0;

    bool                  is_bbl_3mf;
    Semver                file_version;
    DynamicPrintConfig    config;
    PlateDataPtrs         plate_data_src;
    std::vector<Preset *> project_presets;

    model = Model::read_from_file(input_file, &config, &config_substitutions, strategy, &plate_data_src, &project_presets,
        &is_bbl_3mf, &file_version, nullptr, nullptr, nullptr, plate_to_slice);

    model.add_default_instances();
    for (auto object : model.objects)
        object->ensure_on_bed();
}

std::array<Vec3d, 4> get_cut_plane_points(const BoundingBoxf3 &bbox, const double &cut_height)
{
    std::array<Vec3d, 4> plane_pts;
    plane_pts[0] = Vec3d(bbox.min(0), bbox.min(1), cut_height);
    plane_pts[1] = Vec3d(bbox.max(0), bbox.min(1), cut_height);
    plane_pts[2] = Vec3d(bbox.max(0), bbox.max(1), cut_height);
    plane_pts[3] = Vec3d(bbox.min(0), bbox.max(1), cut_height);
    return plane_pts;
}

void CalibUtils::calib_PA(const X1CCalibInfos& calib_infos, int mode, wxString& error_message)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    if (calib_infos.calib_datas.size() > 0) {
        if (!check_printable_status_before_cali(obj_, calib_infos, error_message))
            return;
        obj_->command_start_pa_calibration(calib_infos, mode);
    }
}

void CalibUtils::emit_get_PA_calib_results(float nozzle_diameter)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    obj_->command_get_pa_calibration_result(nozzle_diameter);
}

bool CalibUtils::get_PA_calib_results(std::vector<PACalibResult>& pa_calib_results)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return false;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return false;

    pa_calib_results = obj_->pa_calib_results;
    return pa_calib_results.size() > 0;
}

void CalibUtils::emit_get_PA_calib_infos(const PACalibExtruderInfo &cali_info)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    obj_->command_get_pa_calibration_tab(cali_info);
}

bool CalibUtils::get_PA_calib_tab(std::vector<PACalibResult> &pa_calib_infos)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return false;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return false;

    if (obj_->has_get_pa_calib_tab) {
        pa_calib_infos.assign(obj_->pa_calib_tab.begin(), obj_->pa_calib_tab.end());
    }
    return obj_->has_get_pa_calib_tab;
}

void CalibUtils::set_PA_calib_result(const std::vector<PACalibResult> &pa_calib_values, bool is_auto_cali)
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject* obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    obj_->command_set_pa_calibration(pa_calib_values, is_auto_cali);
}

void CalibUtils::select_PA_calib_result(const PACalibIndexInfo& pa_calib_info)
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject* obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    obj_->commnad_select_pa_calibration(pa_calib_info);
}

void CalibUtils::delete_PA_calib_result(const PACalibIndexInfo& pa_calib_info)
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject* obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    obj_->command_delete_pa_calibration(pa_calib_info);
}

void CalibUtils::calib_flowrate_X1C(const X1CCalibInfos& calib_infos, wxString& error_message)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    if (calib_infos.calib_datas.size() > 0) {
         if (!check_printable_status_before_cali(obj_, calib_infos, error_message))
            return;
        obj_->command_start_flow_ratio_calibration(calib_infos);
    }
    else {
        BOOST_LOG_TRIVIAL(info) << "flow_rate_cali: auto | send info | cali_datas is empty.";
    }
}

void CalibUtils::emit_get_flow_ratio_calib_results(float nozzle_diameter)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    obj_->command_get_flow_ratio_calibration_result(nozzle_diameter);
}

bool CalibUtils::get_flow_ratio_calib_results(std::vector<FlowRatioCalibResult>& flow_ratio_calib_results)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return false;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return false;

    flow_ratio_calib_results = obj_->flow_ratio_results;
    return flow_ratio_calib_results.size() > 0;
}

bool CalibUtils::calib_flowrate(int pass, const CalibInfo &calib_info, wxString &error_message)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) {
        error_message = _L("Need select printer");
        return false;
    }

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr) {
        error_message = _L("Need select printer");
        return false;
    }

    if (!check_printable_status_before_cali(obj_, calib_info, error_message))
        return false;

    if (pass != 1 && pass != 2)
        return false;

    Model       model;
    std::string input_file;
    if (pass == 1)
        input_file = Slic3r::resources_dir() + "/calib/filament_flow/flowrate-test-pass1.3mf";
    else
        input_file = Slic3r::resources_dir() + "/calib/filament_flow/flowrate-test-pass2.3mf";

    read_model_from_file(input_file, model);

    DynamicPrintConfig print_config    = calib_info.print_prest->config;
    DynamicPrintConfig filament_config = calib_info.filament_prest->config;
    DynamicPrintConfig printer_config  = calib_info.printer_prest->config;

    /// --- scale ---
    // model is created for a 0.4 nozzle, scale z with nozzle size.
    const ConfigOptionFloatsNullable *nozzle_diameter_config = printer_config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
    assert(nozzle_diameter_config->values.size() > 0);
    float nozzle_diameter = nozzle_diameter_config->values[0];
    float xyScale         = nozzle_diameter / 0.6;
    // scale z to have 7 layers
    double first_layer_height = print_config.option<ConfigOptionFloat>("initial_layer_print_height")->value;
    double layer_height       = nozzle_diameter / 2.0; // prefer 0.2 layer height for 0.4 nozzle
    first_layer_height        = std::max(first_layer_height, layer_height);

    float zscale = (first_layer_height + 6 * layer_height) / 1.4;
    for (auto _obj : model.objects) _obj->scale(1, 1, zscale);
    // only enlarge
    //if (xyScale > 1.2) {
    //    for (auto _obj : model.objects) _obj->scale(xyScale, xyScale, zscale);
    //} else {
    //    for (auto _obj : model.objects) _obj->scale(1, 1, zscale);
    //}

    Flow   infill_flow                   = Flow(nozzle_diameter * 1.2f, layer_height, nozzle_diameter);

    int index = get_index_for_extruder_parameter(filament_config, "filament_max_volumetric_speed", calib_info.extruder_id, calib_info.extruder_type, calib_info.nozzle_volume_type);
    double filament_max_volumetric_speed = filament_config.option<ConfigOptionFloatsNullable>("filament_max_volumetric_speed")->get_at(index);
    double max_infill_speed              = filament_max_volumetric_speed / (infill_flow.mm3_per_mm() * (pass == 1 ? 1.2 : 1));

    index = get_index_for_extruder_parameter(print_config, "internal_solid_infill_speed", calib_info.extruder_id, calib_info.extruder_type, calib_info.nozzle_volume_type);
    double internal_solid_speed          = std::floor(std::min(print_config.opt_float_nullable("internal_solid_infill_speed", index), max_infill_speed));
    ConfigOptionFloatsNullable* internal_solid_speed_opt = print_config.option<ConfigOptionFloatsNullable>("internal_solid_infill_speed");
    internal_solid_speed_opt->values[index] = internal_solid_speed;

    index = get_index_for_extruder_parameter(print_config, "top_surface_speed", calib_info.extruder_id, calib_info.extruder_type, calib_info.nozzle_volume_type);
    double top_surface_speed        = std::floor(std::min(print_config.opt_float_nullable("top_surface_speed", index), max_infill_speed));
    ConfigOptionFloatsNullable *top_surface_speed_opt = print_config.option<ConfigOptionFloatsNullable>("top_surface_speed");
    top_surface_speed_opt->values[index] = top_surface_speed;

    // adjust parameters
    filament_config.set_key_value("curr_bed_type", new ConfigOptionEnum<BedType>(calib_info.bed_type));

    for (auto _obj : model.objects) {
        _obj->ensure_on_bed();
        _obj->config.set_key_value("wall_loops", new ConfigOptionInt(3));
        _obj->config.set_key_value("top_one_wall_type", new ConfigOptionEnum<TopOneWallType>(TopOneWallType::Topmost));
        _obj->config.set_key_value("top_area_threshold", new ConfigOptionPercent(100));
        _obj->config.set_key_value("sparse_infill_density", new ConfigOptionPercent(35));
        _obj->config.set_key_value("bottom_shell_layers", new ConfigOptionInt(1));
        _obj->config.set_key_value("top_shell_layers", new ConfigOptionInt(5));
        _obj->config.set_key_value("detect_thin_wall", new ConfigOptionBool(true));
        _obj->config.set_key_value("filter_out_gap_fill", new ConfigOptionFloat(0));  // OrcaSlicer parameter
        _obj->config.set_key_value("sparse_infill_pattern", new ConfigOptionEnum<InfillPattern>(ipRectilinear));
        _obj->config.set_key_value("top_surface_line_width", new ConfigOptionFloat(nozzle_diameter * 1.2f));
        _obj->config.set_key_value("internal_solid_infill_line_width", new ConfigOptionFloat(nozzle_diameter * 1.2f));
        _obj->config.set_key_value("top_surface_pattern", new ConfigOptionEnum<InfillPattern>(ipMonotonic));
        _obj->config.set_key_value("top_solid_infill_flow_ratio", new ConfigOptionFloat(1.0f));
        _obj->config.set_key_value("infill_direction", new ConfigOptionFloat(45));
        _obj->config.set_key_value("ironing_type", new ConfigOptionEnum<IroningType>(IroningType::NoIroning));
        _obj->config.set_key_value("internal_solid_infill_speed", new ConfigOptionFloatsNullable(internal_solid_speed_opt->values));
        _obj->config.set_key_value("top_surface_speed", new ConfigOptionFloatsNullable(top_surface_speed_opt->values));

        // extract flowrate from name, filename format: flowrate_xxx
        std::string obj_name = _obj->name;
        assert(obj_name.length() > 9);
        obj_name = obj_name.substr(9);
        if (obj_name[0] == 'm') obj_name[0] = '-';
        auto modifier = stof(obj_name);
        _obj->config.set_key_value("print_flow_ratio", new ConfigOptionFloat(1.0f + modifier / 100.f));
    }

    print_config.set_key_value("layer_height", new ConfigOptionFloat(layer_height));
    print_config.set_key_value("initial_layer_print_height", new ConfigOptionFloat(first_layer_height));
    print_config.set_key_value("reduce_crossing_wall", new ConfigOptionBool(true));

    // apply preset
    DynamicPrintConfig full_config;
    full_config.apply(FullPrintConfig::defaults());
    full_config.apply(print_config);
    full_config.apply(filament_config);
    full_config.apply(printer_config);

    init_multi_extruder_params_for_cali(full_config, calib_info);

    Calib_Params params;
    params.mode = CalibMode::Calib_Flow_Rate;
    if (!process_and_store_3mf(&model, full_config, params, error_message))
        return false;

    try {
        json js;
        if (pass == 1)
            js["cali_type"]   = "cali_flow_rate_1";
        else if (pass == 2)
            js["cali_type"]   = "cali_flow_rate_2";
        js["nozzle_diameter"] = nozzle_diameter;
        js["filament_id"]     = calib_info.filament_prest->filament_id;
        js["printer_type"]    = obj_->printer_type;
        NetworkAgent *agent   = GUI::wxGetApp().getAgent();
        if (agent)
            agent->track_event("cali", js.dump());
    } catch (...) {}

    send_to_print(calib_info, error_message, pass);
    return true;
}

void CalibUtils::calib_pa_pattern(const CalibInfo &calib_info, Model& model)
{
    DynamicPrintConfig& print_config    = calib_info.print_prest->config;
    DynamicPrintConfig& filament_config = calib_info.filament_prest->config;
    DynamicPrintConfig& printer_config  = calib_info.printer_prest->config;

    DynamicPrintConfig full_config;
    full_config.apply(FullPrintConfig::defaults());
    full_config.apply(print_config);
    full_config.apply(filament_config);
    full_config.apply(printer_config);

    float nozzle_diameter = printer_config.option<ConfigOptionFloatsNullable>("nozzle_diameter")->get_at(0);

    for (const auto opt : SuggestedConfigCalibPAPattern().float_pairs) {
        print_config.set_key_value(opt.first, new ConfigOptionFloat(opt.second));
    }
    for (const auto opt : SuggestedConfigCalibPAPattern().floats_pairs) {
        print_config.set_key_value(opt.first, new ConfigOptionFloatsNullable(opt.second));
    }

    int index = get_index_for_extruder_parameter(print_config, "outer_wall_speed", calib_info.extruder_id, calib_info.extruder_type, calib_info.nozzle_volume_type);
    float wall_speed = CalibPressureAdvance::find_optimal_PA_speed(full_config, print_config.get_abs_value("line_width"), print_config.get_abs_value("layer_height"), calib_info.extruder_id, 0);
    ConfigOptionFloatsNullable *wall_speed_speed_opt = print_config.option<ConfigOptionFloatsNullable>("outer_wall_speed");
    wall_speed_speed_opt->values[index]              = wall_speed;

    for (const auto opt : SuggestedConfigCalibPAPattern().nozzle_ratio_pairs) {
        print_config.set_key_value(opt.first, new ConfigOptionFloat(nozzle_diameter * opt.second / 100));
    }

    for (const auto opt : SuggestedConfigCalibPAPattern().int_pairs) {
        print_config.set_key_value(opt.first, new ConfigOptionInt(opt.second));
    }

    print_config.set_key_value(SuggestedConfigCalibPAPattern().brim_pair.first,
        new ConfigOptionEnum<BrimType>(SuggestedConfigCalibPAPattern().brim_pair.second));

    //DynamicPrintConfig full_config;
    full_config.apply(FullPrintConfig::defaults());
    full_config.apply(print_config);
    full_config.apply(filament_config);
    full_config.apply(printer_config);

    Vec3d plate_origin(0, 0, 0);
    CalibPressureAdvancePattern pa_pattern(calib_info.params, full_config, true, model, plate_origin);

    Pointfs bedfs         = full_config.opt<ConfigOptionPoints>("printable_area")->values;
    double  current_width = bedfs[2].x() - bedfs[0].x();
    double  current_depth = bedfs[2].y() - bedfs[0].y();
    Vec3d   half_pattern_size = Vec3d(pa_pattern.print_size_x() / 2, pa_pattern.print_size_y() / 2, 0);
    Vec3d   offset            = Vec3d(current_width / 2, current_depth / 2, 0) - half_pattern_size;
    pa_pattern.set_start_offset(offset);

    pa_pattern.generate_custom_gcodes(full_config, true, model, plate_origin);
    model.calib_pa_pattern = std::make_unique<CalibPressureAdvancePattern>(pa_pattern);
}

bool CalibUtils::calib_generic_PA(const CalibInfo &calib_info, wxString &error_message)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) {
        error_message = _L("Need select printer");
        return false;
    }

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr) {
        error_message = _L("Need select printer");
        return false;
    }

    if (!check_printable_status_before_cali(obj_, calib_info, error_message))
        return false;

    const Calib_Params &params = calib_info.params;
    if (params.mode != CalibMode::Calib_PA_Line && params.mode != CalibMode::Calib_PA_Pattern)
        return false;

    Model model;
    std::string input_file;
    if (params.mode == CalibMode::Calib_PA_Line)
        input_file = Slic3r::resources_dir() + "/calib/pressure_advance/pressure_advance_test.stl";
    else if (params.mode == CalibMode::Calib_PA_Pattern)
        input_file = Slic3r::resources_dir() + "/calib/pressure_advance/pa_pattern.3mf";

    read_model_from_file(input_file, model);

    if (params.mode == CalibMode::Calib_PA_Pattern)
        calib_pa_pattern(calib_info, model);

    DynamicPrintConfig print_config    = calib_info.print_prest->config;
    DynamicPrintConfig filament_config = calib_info.filament_prest->config;
    DynamicPrintConfig printer_config  = calib_info.printer_prest->config;

    filament_config.set_key_value("curr_bed_type", new ConfigOptionEnum<BedType>(calib_info.bed_type));

    DynamicPrintConfig full_config;
    full_config.apply(FullPrintConfig::defaults());
    full_config.apply(print_config);
    full_config.apply(filament_config);
    full_config.apply(printer_config);

    init_multi_extruder_params_for_cali(full_config, calib_info);

    if (!process_and_store_3mf(&model, full_config, params, error_message))
        return false;

    try {
        json js;
        if (params.mode == CalibMode::Calib_PA_Line)
            js["cali_type"] = "cali_pa_line";
        else if (params.mode == CalibMode::Calib_PA_Pattern)
            js["cali_type"] = "cali_pa_pattern";

        const ConfigOptionFloatsNullable *nozzle_diameter_config = printer_config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
        assert(nozzle_diameter_config->values.size() > 0);
        float nozzle_diameter = nozzle_diameter_config->values[0];

        js["nozzle_diameter"] = nozzle_diameter;
        js["filament_id"]     = calib_info.filament_prest->filament_id;
        js["printer_type"]    = obj_->printer_type;
        NetworkAgent *agent   = GUI::wxGetApp().getAgent();
        if (agent)
            agent->track_event("cali", js.dump());
    } catch (...) {}

    send_to_print(calib_info, error_message);
    return true;
}

void CalibUtils::calib_temptue(const CalibInfo &calib_info, wxString &error_message)
{
    const Calib_Params &params = calib_info.params;
    if (params.mode != CalibMode::Calib_Temp_Tower)
        return;

    Model                     model;
    std::string               input_file = Slic3r::resources_dir() + "/calib/temperature_tower/temperature_tower.stl";
    read_model_from_file(input_file, model);

    // cut upper
    auto obj_bb      = model.objects[0]->bounding_box();
    auto block_count = lround((350 - params.start) / 5 + 1);
    if (block_count > 0) {
        // add EPSILON offset to avoid cutting at the exact location where the flat surface is
        auto new_height = block_count * 10.0 + EPSILON;
        if (new_height < obj_bb.size().z()) {
            std::array<Vec3d, 4> plane_pts;
            plane_pts[0] = Vec3d(obj_bb.min(0), obj_bb.min(1), new_height);
            plane_pts[1] = Vec3d(obj_bb.max(0), obj_bb.min(1), new_height);
            plane_pts[2] = Vec3d(obj_bb.max(0), obj_bb.max(1), new_height);
            plane_pts[3] = Vec3d(obj_bb.min(0), obj_bb.max(1), new_height);
            cut_model(model, plane_pts, ModelObjectCutAttribute::KeepLower);
        }
    }

    // cut bottom
    obj_bb      = model.objects[0]->bounding_box();
    block_count = lround((350 - params.end) / 5);
    if (block_count > 0) {
        auto new_height = block_count * 10.0 + EPSILON;
        if (new_height < obj_bb.size().z()) {
            std::array<Vec3d, 4> plane_pts;
            plane_pts[0] = Vec3d(obj_bb.min(0), obj_bb.min(1), new_height);
            plane_pts[1] = Vec3d(obj_bb.max(0), obj_bb.min(1), new_height);
            plane_pts[2] = Vec3d(obj_bb.max(0), obj_bb.max(1), new_height);
            plane_pts[3] = Vec3d(obj_bb.min(0), obj_bb.max(1), new_height);
            cut_model(model, plane_pts, ModelObjectCutAttribute::KeepUpper);
        }
    }

    // edit preset
    DynamicPrintConfig print_config    = calib_info.print_prest->config;
    DynamicPrintConfig filament_config = calib_info.filament_prest->config;
    DynamicPrintConfig printer_config  = calib_info.printer_prest->config;

    auto start_temp      = lround(params.start);
    filament_config.set_key_value("nozzle_temperature_initial_layer", new ConfigOptionIntsNullable(1, (int) start_temp));
    filament_config.set_key_value("nozzle_temperature", new ConfigOptionIntsNullable(1, (int) start_temp));
    filament_config.set_key_value("curr_bed_type", new ConfigOptionEnum<BedType>(calib_info.bed_type));

    model.objects[0]->config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btOuterOnly));
    model.objects[0]->config.set_key_value("brim_width", new ConfigOptionFloat(5.0));
    model.objects[0]->config.set_key_value("brim_object_gap", new ConfigOptionFloat(0.0));
    model.objects[0]->config.set_key_value("enable_support", new ConfigOptionBool(false));

    // apply preset
    DynamicPrintConfig full_config;
    full_config.apply(FullPrintConfig::defaults());
    full_config.apply(print_config);
    full_config.apply(filament_config);
    full_config.apply(printer_config);

    init_multi_extruder_params_for_cali(full_config, calib_info);

    process_and_store_3mf(&model, full_config, params, error_message);
    if (!error_message.empty())
        return;

    send_to_print(calib_info, error_message);
}

void CalibUtils::calib_max_vol_speed(const CalibInfo &calib_info, wxString &error_message)
{
    const Calib_Params &params = calib_info.params;
    if (params.mode != CalibMode::Calib_Vol_speed_Tower)
        return;

    Model       model;
    std::string input_file = Slic3r::resources_dir() + "/calib/volumetric_speed/SpeedTestStructure.step";
    read_model_from_file(input_file, model);

    DynamicPrintConfig print_config    = calib_info.print_prest->config;
    DynamicPrintConfig filament_config = calib_info.filament_prest->config;
    DynamicPrintConfig printer_config  = calib_info.printer_prest->config;

    auto obj             = model.objects[0];
    auto         bed_shape = printer_config.option<ConfigOptionPoints>("printable_area")->values;
    BoundingBoxf bed_ext   = get_extents(bed_shape);
    auto         scale_obj = (bed_ext.size().x() - 10) / obj->bounding_box().size().x();
    if (scale_obj < 1.0)
        obj->scale(scale_obj, 1, 1);

    const ConfigOptionFloatsNullable *nozzle_diameter_config = printer_config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
    assert(nozzle_diameter_config->values.size() > 0);
    double nozzle_diameter = nozzle_diameter_config->values[0];
    double line_width      = nozzle_diameter * 1.75;
    double layer_height    = nozzle_diameter * 0.8;

    auto max_lh = printer_config.option<ConfigOptionFloatsNullable>("max_layer_height");
    if (max_lh->values[0] < layer_height) max_lh->values[0] = {layer_height};

    filament_config.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloatsNullable{50});
    filament_config.set_key_value("slow_down_layer_time", new ConfigOptionInts{0});
    filament_config.set_key_value("curr_bed_type", new ConfigOptionEnum<BedType>(calib_info.bed_type));

    print_config.set_key_value("enable_overhang_speed", new ConfigOptionBoolsNullable{false});
    print_config.set_key_value("timelapse_type", new ConfigOptionEnum<TimelapseType>(tlTraditional));
    print_config.set_key_value("wall_loops", new ConfigOptionInt(1));
    print_config.set_key_value("top_shell_layers", new ConfigOptionInt(0));
    print_config.set_key_value("bottom_shell_layers", new ConfigOptionInt(1));
    print_config.set_key_value("sparse_infill_density", new ConfigOptionPercent(0));
    print_config.set_key_value("spiral_mode", new ConfigOptionBool(true));
    print_config.set_key_value("outer_wall_line_width", new ConfigOptionFloat(line_width));
    print_config.set_key_value("initial_layer_print_height", new ConfigOptionFloat(layer_height));
    print_config.set_key_value("layer_height", new ConfigOptionFloat(layer_height));
    obj->config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btOuterAndInner));
    obj->config.set_key_value("brim_width", new ConfigOptionFloat(3.0));
    obj->config.set_key_value("brim_object_gap", new ConfigOptionFloat(0.0));

    //  cut upper
    auto obj_bb = obj->bounding_box();
    double height = (params.end - params.start + 1) / params.step;
    if (height < obj_bb.size().z()) {
        std::array<Vec3d, 4> plane_pts;
        plane_pts[0] = Vec3d(obj_bb.min(0), obj_bb.min(1), height);
        plane_pts[1] = Vec3d(obj_bb.max(0), obj_bb.min(1), height);
        plane_pts[2] = Vec3d(obj_bb.max(0), obj_bb.max(1), height);
        plane_pts[3] = Vec3d(obj_bb.min(0), obj_bb.max(1), height);
        cut_model(model, plane_pts, ModelObjectCutAttribute::KeepLower);
    }

    auto new_params  = params;
    auto mm3_per_mm  = Flow(line_width, layer_height, nozzle_diameter).mm3_per_mm() * filament_config.option<ConfigOptionFloatsNullable>("filament_flow_ratio")->get_at(0);
    new_params.end   = params.end / mm3_per_mm;
    new_params.start = params.start / mm3_per_mm;
    new_params.step  = params.step / mm3_per_mm;

    DynamicPrintConfig full_config;
    full_config.apply(FullPrintConfig::defaults());
    full_config.apply(print_config);
    full_config.apply(filament_config);
    full_config.apply(printer_config);

    init_multi_extruder_params_for_cali(full_config, calib_info);

    process_and_store_3mf(&model, full_config, new_params, error_message);
    if (!error_message.empty())
        return;

    send_to_print(calib_info, error_message);
}

void CalibUtils::calib_VFA(const CalibInfo &calib_info, wxString &error_message)
{
    const Calib_Params &params = calib_info.params;
    if (params.mode != CalibMode::Calib_VFA_Tower)
        return;

    Model model;
    std::string input_file = Slic3r::resources_dir() + "/calib/vfa/VFA.stl";
    read_model_from_file(input_file, model);

    DynamicPrintConfig print_config    = calib_info.print_prest->config;
    DynamicPrintConfig filament_config = calib_info.filament_prest->config;
    DynamicPrintConfig printer_config  = calib_info.printer_prest->config;

    filament_config.set_key_value("slow_down_layer_time", new ConfigOptionInts{0});
    filament_config.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloatsNullable{200});
    filament_config.set_key_value("curr_bed_type", new ConfigOptionEnum<BedType>(calib_info.bed_type));

    print_config.set_key_value("enable_overhang_speed", new ConfigOptionBoolsNullable{false});
    print_config.set_key_value("timelapse_type", new ConfigOptionEnum<TimelapseType>(tlTraditional));
    print_config.set_key_value("wall_loops", new ConfigOptionInt(1));
    print_config.set_key_value("top_shell_layers", new ConfigOptionInt(0));
    print_config.set_key_value("bottom_shell_layers", new ConfigOptionInt(1));
    print_config.set_key_value("sparse_infill_density", new ConfigOptionPercent(0));
    print_config.set_key_value("spiral_mode", new ConfigOptionBool(true));
    model.objects[0]->config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btOuterOnly));
    model.objects[0]->config.set_key_value("brim_width", new ConfigOptionFloat(3.0));
    model.objects[0]->config.set_key_value("brim_object_gap", new ConfigOptionFloat(0.0));

    // cut upper
    auto obj_bb = model.objects[0]->bounding_box();
    auto height = 5 * ((params.end - params.start) / params.step + 1);
    if (height < obj_bb.size().z()) {
        std::array<Vec3d, 4> plane_pts;
        plane_pts[0] = Vec3d(obj_bb.min(0), obj_bb.min(1), height);
        plane_pts[1] = Vec3d(obj_bb.max(0), obj_bb.min(1), height);
        plane_pts[2] = Vec3d(obj_bb.max(0), obj_bb.max(1), height);
        plane_pts[3] = Vec3d(obj_bb.min(0), obj_bb.max(1), height);
        cut_model(model, plane_pts, ModelObjectCutAttribute::KeepLower);
    }
    else {
        error_message = _L("The start, end or step is not valid value.");
        return;
    }

    DynamicPrintConfig full_config;
    full_config.apply(FullPrintConfig::defaults());
    full_config.apply(print_config);
    full_config.apply(filament_config);
    full_config.apply(printer_config);

    init_multi_extruder_params_for_cali(full_config, calib_info);

    process_and_store_3mf(&model, full_config, params, error_message);
    if (!error_message.empty())
        return;

    send_to_print(calib_info, error_message);
}

void CalibUtils::calib_retraction(const CalibInfo &calib_info, wxString &error_message)
{
    const Calib_Params &params = calib_info.params;
    if (params.mode != CalibMode::Calib_Retraction_tower)
        return;

    Model model;
    std::string input_file = Slic3r::resources_dir() + "/calib/retraction/retraction_tower.stl";
    read_model_from_file(input_file, model);

    DynamicPrintConfig print_config    = calib_info.print_prest->config;
    DynamicPrintConfig filament_config = calib_info.filament_prest->config;
    DynamicPrintConfig printer_config  = calib_info.printer_prest->config;

    auto obj = model.objects[0];

    double layer_height = 0.2;

    auto max_lh = printer_config.option<ConfigOptionFloatsNullable>("max_layer_height");
    if (max_lh->values[0] < layer_height) max_lh->values[0] = {layer_height};

    filament_config.set_key_value("curr_bed_type", new ConfigOptionEnum<BedType>(calib_info.bed_type));

    obj->config.set_key_value("wall_loops", new ConfigOptionInt(2));
    obj->config.set_key_value("top_shell_layers", new ConfigOptionInt(0));
    obj->config.set_key_value("bottom_shell_layers", new ConfigOptionInt(3));
    obj->config.set_key_value("sparse_infill_density", new ConfigOptionPercent(0));
    obj->config.set_key_value("initial_layer_print_height", new ConfigOptionFloat(layer_height));
    obj->config.set_key_value("layer_height", new ConfigOptionFloat(layer_height));

    //  cut upper
    auto obj_bb = obj->bounding_box();
    auto height = 1.0 + 0.4 + ((params.end - params.start)) / params.step;
    if (height < obj_bb.size().z()) {
        std::array<Vec3d, 4> plane_pts = get_cut_plane_points(obj_bb, height);
        cut_model(model, plane_pts, ModelObjectCutAttribute::KeepLower);
    }

    DynamicPrintConfig full_config;
    full_config.apply(FullPrintConfig::defaults());
    full_config.apply(print_config);
    full_config.apply(filament_config);
    full_config.apply(printer_config);

    init_multi_extruder_params_for_cali(full_config, calib_info);

    process_and_store_3mf(&model, full_config, params, error_message);
    if (!error_message.empty())
        return;

    send_to_print(calib_info, error_message);
}

int CalibUtils::get_selected_calib_idx(const std::vector<PACalibResult> &pa_calib_values, int cali_idx) {
    for (int i = 0; i < pa_calib_values.size(); ++i) {
        if(pa_calib_values[i].cali_idx == cali_idx)
            return i;
    }
    return -1;
}

bool CalibUtils::get_pa_k_n_value_by_cali_idx(const MachineObject *obj, int cali_idx, float &out_k, float &out_n) {
    if (!obj)
        return false;

    for (auto pa_calib_info : obj->pa_calib_tab) {
        if (pa_calib_info.cali_idx == cali_idx) {
            out_k = pa_calib_info.k_value;
            out_n = pa_calib_info.n_coef;
            return true;
        }
    }
    return false;
}

bool CalibUtils::check_printable_status_before_cali(const MachineObject *obj, const X1CCalibInfos &cali_infos, wxString &error_message)
{
    if (!obj) {
        error_message = _L("Need select printer");
        return false;
    }

    float cali_diameter = cali_infos.calib_datas[0].nozzle_diameter;
    int   extruder_id   = cali_infos.calib_datas[0].extruder_id;
    for (const auto& cali_info : cali_infos.calib_datas) {
        if (!is_approx(cali_diameter, cali_info.nozzle_diameter)) {
            error_message = _L("Automatic calibration only supports cases where the left and right nozzle diameters are identical.");
            return false;
        }
    }

    if (extruder_id >= obj->m_extder_data.extders.size()) {
        error_message = _L("The number of printer extruders and the printer selected for calibration does not match.");
        return false;
    }

    float diameter = obj->m_extder_data.extders[extruder_id].current_nozzle_diameter;
    bool  is_multi_extruder = obj->is_multi_extruders();
    std::vector<NozzleFlowType> nozzle_volume_types;
    if (is_multi_extruder) {
        for (const Extder& extruder : obj->m_extder_data.extders) {
            nozzle_volume_types.emplace_back(extruder.current_nozzle_flow_type);
        }
    }

    for (const auto &cali_info : cali_infos.calib_datas) {
        wxString name = _L("left");
        if (cali_info.extruder_id == 0) {
            name = _L("right");
        }

        if (!is_approx(cali_info.nozzle_diameter, diameter)) {
            if (is_multi_extruder)
                error_message = wxString::Format(_L("The currently selected nozzle diameter of %s extruder does not match the actual nozzle diameter.\n"
                               "Please click the Sync button above and restart the calibration."), name);
            else
                error_message = _L("The nozzle diameter does not match the actual printer nozzle diameter.\n"
                               "Please click the Sync button above and restart the calibration.");
            return false;
        }

        if (is_multi_extruder) {
            if (nozzle_volume_types[cali_info.extruder_id] == NozzleFlowType::NONE_FLOWTYPE) {

                error_message = wxString::Format(_L("Printer %s nozzle information has not been set. Please configure it before proceeding with the calibration."), name);
                return false;
            }

            if (NozzleVolumeType(nozzle_volume_types[cali_info.extruder_id] - 1) != cali_info.nozzle_volume_type) {
                error_message = wxString::Format(_L("The currently selected nozzle type of %s extruder does not match the actual printer nozzle type.\n"
                                                    "Please click the Sync button above and restart the calibration."), name);
                return false;
            }
        }
    }
    return true;
}

bool CalibUtils::check_printable_status_before_cali(const MachineObject* obj, const CalibInfo& cali_info, wxString& error_message)
{
    if (!obj) {
        error_message = _L("Need select printer");
        return false;
    }

    const ConfigOptionFloatsNullable *nozzle_diameter_config = cali_info.printer_prest->config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
    float nozzle_diameter = nozzle_diameter_config->values[0];

    float diameter = obj->m_extder_data.extders[cali_info.extruder_id].current_nozzle_diameter;
    bool  is_multi_extruder = obj->is_multi_extruders();
    std::vector<NozzleFlowType> nozzle_volume_types;
    if (is_multi_extruder) {
        for (const Extder& extruder : obj->m_extder_data.extders) {
            nozzle_volume_types.emplace_back(extruder.current_nozzle_flow_type);
        }
    }

    wxString name = _L("left");
    if (cali_info.extruder_id == 0) {
        name = _L("right");
    }

    if (!is_approx(nozzle_diameter, diameter)) {
        if (is_multi_extruder)
            error_message = wxString::Format(_L("The currently selected nozzle diameter of %s extruder does not match the actual nozzle diameter.\n"
                               "Please click the Sync button above and restart the calibration."), name);
        else
            error_message = _L("The nozzle diameter does not match the actual printer nozzle diameter.\n"
                               "Please click the Sync button above and restart the calibration.");
        return false;
    }

    if (is_multi_extruder) {
        if (nozzle_volume_types[cali_info.extruder_id] == NozzleFlowType::NONE_FLOWTYPE) {
            error_message = wxString::Format(_L("Printer %s nozzle information has not been set. Please configure it before proceeding with the calibration."), name);
            return false;
        }

        if (NozzleVolumeType(nozzle_volume_types[cali_info.extruder_id] - 1) != cali_info.nozzle_volume_type) {
            error_message = wxString::Format(_L("The currently selected nozzle type of %s extruder does not match the actual printer nozzle type.\n"
                                                "Please click the Sync button above and restart the calibration."), name);
            return false;
        }
    }

    return true;
}

bool CalibUtils::process_and_store_3mf(Model *model, const DynamicPrintConfig &full_config, const Calib_Params &params, wxString &error_message)
{
    Pointfs bedfs         = full_config.opt<ConfigOptionPoints>("printable_area")->values;
    std::vector<Pointfs> extruder_areas = full_config.option<ConfigOptionPointsGroups>("extruder_printable_area")->values;
    std::vector<double> extruder_heights = full_config.option<ConfigOptionFloatsNullable>("extruder_printable_height")->values;
    double  print_height  = full_config.opt_float("printable_height");
    double  current_width = bedfs[2].x() - bedfs[0].x();
    double  current_depth = bedfs[2].y() - bedfs[0].y();
    Vec3i   plate_size;
    plate_size[0] = bedfs[2].x() - bedfs[0].x();
    plate_size[1] = bedfs[2].y() - bedfs[0].y();
    plate_size[2] = print_height;

    if (params.mode == CalibMode::Calib_PA_Line) {
        double space_y       = 3.5;
        int    max_line_nums = int(plate_size[1] - 10) / space_y;
        int    count         = std::llround(std::ceil((params.end - params.start) / params.step)) + 1;
        if (count > max_line_nums) {
            error_message = _L("Unable to calibrate: maybe because the set calibration value range is too large, or the step is too small");
            return false;
        }
    }

    if (params.mode == CalibMode::Calib_PA_Pattern) {
        ModelInstance *instance = model->objects[0]->instances[0];
        instance->set_offset(Axis::X, model->calib_pa_pattern->get_start_offset().x() + model->calib_pa_pattern->handle_xy_size() / 2);
        instance->set_offset(Axis::Y, model->calib_pa_pattern->get_start_offset().y() - model->calib_pa_pattern->handle_xy_size() / 2 - model->calib_pa_pattern->handle_spacing());

    }
    else if (model->objects.size() == 1) {
        ModelInstance *instance = model->objects[0]->instances[0];
        instance->set_offset(instance->get_offset() + Vec3d(current_width / 2, current_depth / 2, 0));
    } else {
        BoundingBoxf3 bbox = model->bounding_box();
        Vec3d bbox_center = bbox.center();
        for (auto object : model->objects) {
            ModelInstance *instance = object->instances[0];
            instance->set_offset(instance->get_offset() + Vec3d(current_width / 2 - bbox_center.x(), current_depth / 2 - bbox_center.y(), 0));
        }
    }

    Slic3r::GUI::PartPlateList partplate_list(nullptr, model, PrinterTechnology::ptFFF);
    partplate_list.reset_size(plate_size.x(), plate_size.y(), plate_size.z(), false);

    Slic3r::GUI::PartPlate *part_plate = partplate_list.get_plate(0);

    PrintBase *               print        = NULL;
    Slic3r::GUI::GCodeResult *gcode_result = NULL;
    int                       print_index;
    part_plate->get_print(&print, &gcode_result, &print_index);

    BuildVolume build_volume(bedfs, print_height, extruder_areas, extruder_heights);
    unsigned int count = model->update_print_volume_state(build_volume);
    if (count == 0) {
        error_message = _L("Unable to calibrate: maybe because the set calibration value range is too large, or the step is too small");
        return false;
    }

    // apply the new print config
    DynamicPrintConfig new_print_config = full_config;
    print->apply(*model, new_print_config);

    Print *fff_print = dynamic_cast<Print *>(print);
    fff_print->set_calib_params(params);

    //StringObjectException warning;
    //auto err = print->validate(&warning);
    //if (!err.string.empty()) {
    //    error_message = "slice validate: " + err.string;
    //    return;
    //}

    if (!check_nozzle_diameter_and_type(full_config, error_message))
        return false;

    fff_print->process();
    part_plate->update_slice_result_valid_state(true);

    gcode_result->reset();
    fff_print->export_gcode(temp_gcode_path, gcode_result, nullptr);

    std::vector<ThumbnailData*> thumbnails;
    PlateDataPtrs plate_data_list;
    partplate_list.store_to_3mf_structure(plate_data_list, true, 0);

    for (auto plate_data : plate_data_list) {
        plate_data->gcode_file      = temp_gcode_path;
        plate_data->is_sliced_valid = true;
        FilamentInfo& filament_info = plate_data->slice_filaments_info.front();
        filament_info.type          = full_config.opt_string("filament_type", 0);
    }

    //draw thumbnails
    {
        GLVolumeCollection glvolume_collection;
        std::vector<std::array<float, 4>> colors_out(1);
        unsigned char  rgb_color[4] = {255, 255, 255, 255};
        std::array<float, 4> new_color {1.0f, 1.0f, 1.0f, 1.0f};
        colors_out.push_back(new_color);

        ThumbnailData* thumbnail_data = &plate_data_list[0]->plate_thumbnail;
        unsigned int thumbnail_width = 512, thumbnail_height = 512;
        const ThumbnailsParams thumbnail_params = {{}, false, true, true, true, 0};
        const auto& shader = wxGetApp().get_shader("thumbnail");

        for (unsigned int obj_idx = 0; obj_idx < (unsigned int)model->objects.size(); ++ obj_idx) {
            const ModelObject &model_object = *model->objects[obj_idx];

            for (int volume_idx = 0; volume_idx < (int)model_object.volumes.size(); ++ volume_idx) {
                const ModelVolume &model_volume = *model_object.volumes[volume_idx];
                for (int instance_idx = 0; instance_idx < (int)model_object.instances.size(); ++ instance_idx) {
                    const ModelInstance &model_instance = *model_object.instances[instance_idx];
                    glvolume_collection.load_object_volume(&model_object, obj_idx, volume_idx, instance_idx, "volume", true, false, true, false);
                    glvolume_collection.volumes.back()->set_render_color( new_color[0], new_color[1], new_color[2], new_color[3]);
                    glvolume_collection.volumes.back()->set_color(new_color);
                    //glvolume_collection.volumes.back()->printable = model_instance.printable;
                }
            }
        }

        const auto fb_type = Slic3r::GUI::OpenGLManager::get_framebuffers_type();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": framebuffer_type: %1%") % Slic3r::GUI::OpenGLManager::framebuffer_type_to_string(fb_type).c_str();
        switch (fb_type)
        {
            case Slic3r::GUI::OpenGLManager::EFramebufferType::Supported:
            case Slic3r::GUI::OpenGLManager::EFramebufferType::Arb:
            {
                Slic3r::GUI::GLCanvas3D::render_thumbnail_framebuffer(*thumbnail_data,
                   thumbnail_width, thumbnail_height, thumbnail_params,
                   partplate_list, model->objects, glvolume_collection, colors_out, shader, Slic3r::GUI::Camera::EType::Ortho);
                break;
            }
            case Slic3r::GUI::OpenGLManager::EFramebufferType::Ext:
            {
                Slic3r::GUI::GLCanvas3D::render_thumbnail_framebuffer_ext(*thumbnail_data,
                   thumbnail_width, thumbnail_height, thumbnail_params,
                   partplate_list, model->objects, glvolume_collection, colors_out, shader, Slic3r::GUI::Camera::EType::Ortho);
                break;
            }
            default:{
                Slic3r::GUI::GLCanvas3D::render_thumbnail_legacy(*thumbnail_data, thumbnail_width, thumbnail_height, thumbnail_params, partplate_list, model->objects, glvolume_collection, colors_out, shader, Slic3r::GUI::Camera::EType::Ortho);
                break;
            }
        }
        thumbnails.push_back(thumbnail_data);
    }

    StoreParams store_params;
    store_params.path            = path.c_str();
    store_params.model           = model;
    store_params.plate_data_list = plate_data_list;
    store_params.config = &new_print_config;

    store_params.export_plate_idx = 0;
    store_params.thumbnail_data = thumbnails;


    store_params.strategy = SaveStrategy::Silence | SaveStrategy::WithGcode | SaveStrategy::SplitModel | SaveStrategy::SkipModel;

    bool success = Slic3r::store_bbs_3mf(store_params);

    store_params.strategy = SaveStrategy::Silence | SaveStrategy::SplitModel | SaveStrategy::WithSliceInfo | SaveStrategy::SkipAuxiliary;
    store_params.path = config_3mf_path.c_str();
    success           = Slic3r::store_bbs_3mf(store_params);

    release_PlateData_list(plate_data_list);
    return true;
}

void CalibUtils::send_to_print(const CalibInfo &calib_info, wxString &error_message, int flow_ratio_mode)
{
    {  // before send
        json j;
        j["print"]["cali_mode"]       = calib_info.params.mode;
        j["print"]["start"]           = calib_info.params.start;
        j["print"]["end"]             = calib_info.params.end;
        j["print"]["step"]            = calib_info.params.step;
        j["print"]["print_numbers"]   = calib_info.params.print_numbers;
        j["print"]["flow_ratio_mode"] = flow_ratio_mode;
        j["print"]["tray_id"]         = calib_info.select_ams;
        j["print"]["dev_id"]          = calib_info.dev_id;
        j["print"]["bed_type"]        = calib_info.bed_type;
        j["print"]["printer_prest"]   = calib_info.printer_prest ? calib_info.printer_prest->name : "";
        j["print"]["filament_prest"]  = calib_info.filament_prest ? calib_info.filament_prest->name : "";
        j["print"]["print_prest"]     = calib_info.print_prest ? calib_info.print_prest->name : "";
        BOOST_LOG_TRIVIAL(info) << "send_cali_job - before send: " << j.dump();
    }

    std::string dev_id = calib_info.dev_id;
    std::string select_ams = calib_info.select_ams;
    std::shared_ptr<ProgressIndicator> process_bar = calib_info.process_bar;
    BedType bed_type = calib_info.bed_type;

    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) {
        error_message = _L("Need select printer");
        return;
    }

    MachineObject* obj_ = dev->get_selected_machine();
    if (obj_ == nullptr) {
        error_message = _L("Need select printer");
        return;
    }

    if (obj_->is_in_upgrading()) {
        error_message = _L("Cannot send the print job when the printer is updating firmware");
        return;
    }
    else if (obj_->is_system_printing()) {
        error_message = _L("The printer is executing instructions. Please restart printing after it ends");
        return;
    }
    else if (obj_->is_in_printing()) {
        error_message = _L("The printer is busy on other print job");
        return;
    }

    else if (!obj_->is_support_print_without_sd && (obj_->get_sdcard_state() == MachineObject::SdcardState::NO_SDCARD)) {
        error_message = _L("Storage needs to be inserted before printing.");
        return;
    }
    if (obj_->is_lan_mode_printer()) {
        if (obj_->get_sdcard_state() == MachineObject::SdcardState::NO_SDCARD) {
            error_message = _L("Storage needs to be inserted before printing via LAN.");
            return;
        }
    }

    print_job                   = std::make_shared<PrintJob>(std::move(process_bar), wxGetApp().plater(), dev_id);
    print_job->m_dev_ip         = obj_->dev_ip;
    print_job->m_ftp_folder     = obj_->get_ftp_folder();
    print_job->m_access_code    = obj_->get_access_code();


#if !BBL_RELEASE_TO_PUBLIC
    print_job->m_local_use_ssl_for_ftp = wxGetApp().app_config->get("enable_ssl_for_ftp") == "true" ? true : false;
    print_job->m_local_use_ssl_for_mqtt = wxGetApp().app_config->get("enable_ssl_for_mqtt") == "true" ? true : false;
#else
    print_job->m_local_use_ssl_for_ftp = obj_->local_use_ssl_for_ftp;
    print_job->m_local_use_ssl_for_mqtt = obj_->local_use_ssl_for_mqtt;
#endif

    print_job->connection_type  = obj_->connection_type();
    print_job->cloud_print_only = obj_->is_support_cloud_print_only;

    PrintPrepareData job_data;
    job_data.is_from_plater = false;
    job_data.plate_idx = 0;
    job_data._3mf_config_path = config_3mf_path;
    job_data._3mf_path = path;
    job_data._temp_path = temp_dir;

    PlateListData plate_data;
    plate_data.is_valid = true;
    plate_data.plate_count = 1;
    plate_data.cur_plate_index = 0;
    plate_data.bed_type = bed_type;

    print_job->job_data = job_data;
    print_job->plate_data = plate_data;
    print_job->m_print_type = "from_normal";

    print_job->task_ams_mapping = select_ams;
    print_job->task_ams_mapping_info = "";
    print_job->task_use_ams = select_ams == "[254]" ? false : true;

    std::string new_ams_mapping = "[{\"ams_id\":" + std::to_string(calib_info.ams_id) + ", \"slot_id\":" + std::to_string(calib_info.slot_id) + "}]";
    print_job->task_ams_mapping2 = new_ams_mapping;

    CalibMode cali_mode       = calib_info.params.mode;
    print_job->m_project_name = get_calib_mode_name(cali_mode, flow_ratio_mode);
    print_job->set_calibration_task(true);

    print_job->has_sdcard = obj_->get_sdcard_state() == MachineObject::SdcardState::HAS_SDCARD_NORMAL;
    print_job->set_print_config(MachineBedTypeString[bed_type], true, false, false, false, true, 0, 0, 0);
    print_job->set_print_job_finished_event(wxGetApp().plater()->get_send_calibration_finished_event(), print_job->m_project_name);

    {  // after send: record the print job
        json j;
        j["print"]["project_name"]    = print_job->m_project_name;
        j["print"]["is_cali_task"]    = print_job->m_is_calibration_task;
        BOOST_LOG_TRIVIAL(info) << "send_cali_job - after send: " << j.dump();
    }

    print_job->start();
}

}
}

