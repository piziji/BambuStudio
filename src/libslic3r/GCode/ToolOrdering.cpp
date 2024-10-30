#include "Print.hpp"
#include "ToolOrdering.hpp"
#include "Layer.hpp"
#include "ClipperUtils.hpp"
#include "ParameterUtils.hpp"
#include "GCode/ToolOrderUtils.hpp"
// #define SLIC3R_DEBUG

// Make assert active if SLIC3R_DEBUG
#ifdef SLIC3R_DEBUG
    #define DEBUG
    #define _DEBUG
    #undef NDEBUG
#endif

#include <cassert>
#include <limits>
#include <algorithm>
#include <unordered_map>

#include <libslic3r.h>

namespace Slic3r {

const static bool g_wipe_into_objects = false;
constexpr double similar_color_threshold_de2000 = 20.0;

static std::set<int>get_filament_by_type(const std::vector<unsigned int>& used_filaments, const PrintConfig* print_config, const std::string& type)
{
    std::set<int> target_filaments;
    for (unsigned int filament_id : used_filaments) {
        std::string filament_type = print_config->filament_type.get_at(filament_id);
        if (filament_type == type)
            target_filaments.insert(filament_id);
    }
    return target_filaments;
}


/**
 * @brief Determines the unprintable filaments for each extruder based on its physical attributes
 *
 * Currently, the criteria for determining unprintable filament include the following:
 * 1. TPU filaments can only be placed in the master extruder and must be grouped alone.
 * 2. We only support at most 1 tpu filament.
 * 3. An extruder can only accommodate filament with a hardness requirement lower than that of its nozzle.
 * If extruder num is 1, just return an empty vector.
 *
 * @param used_filaments Totally used filaments when slicing
 * @param config Config that stores releted params
 * @return A vector of sets representing unprintable filaments for each extruder
 */
std::vector<std::set<int>> ToolOrdering::get_physical_unprintables(const std::vector<unsigned int>& used_filaments, const PrintConfig* config)
{
    // master saved in config is 1 based,so we should transfer to 0 based here
    int master_extruder_id = config->master_extruder_id.value - 1;
    auto tpu_filaments = get_filament_by_type(used_filaments, config, "TPU");
    if (tpu_filaments.size() > 1) {
        throw Slic3r::RuntimeError(std::string("Only supports up to one TPU filament."));
    }

    int extruder_num = config->nozzle_diameter.size();
    // consider tpu, only place tpu in extruder with ams
    std::vector<std::set<int>>physical_unprintables(extruder_num);
    if (extruder_num < 2)
        return physical_unprintables;

    int extruder_without_tpu = 1 - master_extruder_id;
    for (auto& f : tpu_filaments)
        physical_unprintables[extruder_without_tpu].insert(f);

    // consider nozzle hrc, nozzle hrc should larger than filament hrc
    for (size_t eid = 0; eid < physical_unprintables.size(); ++eid) {
        auto nozzle_type = config->nozzle_type.get_at(eid);
        int nozzle_hrc = Print::get_hrc_by_nozzle_type(NozzleType(nozzle_type));
        for (auto& f : used_filaments) {
            int filament_hrc = config->required_nozzle_HRC.get_at(f);
            if(filament_hrc>nozzle_hrc){
                physical_unprintables[eid].insert(f);
            }
        }
    }

    return physical_unprintables;
}

/**
 * @brief Determines the unprintable filaments for each extruder based on its printable area.
 *
 * The returned array will always have the same size as the number of extruders.
 * If extruder num is 1, just return an empty vector.
 * If an extruder has no unprintable filaments, an empty set will also be returned
 *
 * @param unprintable_arrs An array of unprintable filaments for each extruder
 * @param config Containing extruder nums or any other info requested
 * @return A vector of sets representing unprintable filaments for each extruder
 */
std::vector<std::set<int>> ToolOrdering::get_geometrical_unprintables(const std::vector<std::vector<int>>& unprintable_arrs, const PrintConfig* config)
{
    auto arrs_idx_switched = unprintable_arrs;
    int extruder_nums = config->nozzle_diameter.size();
    std::vector<std::set<int>> unprintables(extruder_nums);
    if(extruder_nums < 2)
        return unprintables;

    for (auto& arr : arrs_idx_switched)
        for (auto& item : arr)
            item -= 1;

    for (size_t idx = 0; idx < arrs_idx_switched.size(); ++idx)
        unprintables[idx] = std::set<int>(arrs_idx_switched[idx].begin(), arrs_idx_switched[idx].end());

    return unprintables;
}

// Returns true in case that extruder a comes before b (b does not have to be present). False otherwise.
bool LayerTools::is_extruder_order(unsigned int a, unsigned int b) const
{
    if (a == b)
        return false;

    for (auto extruder : extruders) {
        if (extruder == a)
            return true;
        if (extruder == b)
            return false;
    }

    return false;
}

// Return a zero based extruder from the region, or extruder_override if overriden.
unsigned int LayerTools::wall_filament(const PrintRegion &region) const
{
	assert(region.config().wall_filament.value > 0);
	return ((this->extruder_override == 0) ? region.config().wall_filament.value : this->extruder_override) - 1;
}

unsigned int LayerTools::sparse_infill_filament(const PrintRegion &region) const
{
	assert(region.config().sparse_infill_filament.value > 0);
	return ((this->extruder_override == 0) ? region.config().sparse_infill_filament.value : this->extruder_override) - 1;
}

unsigned int LayerTools::solid_infill_filament(const PrintRegion &region) const
{
	assert(region.config().solid_infill_filament.value > 0);
	return ((this->extruder_override == 0) ? region.config().solid_infill_filament.value : this->extruder_override) - 1;
}

// Returns a zero based extruder this eec should be printed with, according to PrintRegion config or extruder_override if overriden.
unsigned int LayerTools::extruder(const ExtrusionEntityCollection &extrusions, const PrintRegion &region) const
{
	assert(region.config().wall_filament.value > 0);
	assert(region.config().sparse_infill_filament.value > 0);
	assert(region.config().solid_infill_filament.value > 0);
	// 1 based extruder ID.
	unsigned int extruder = ((this->extruder_override == 0) ?
	    (is_infill(extrusions.role()) ?
	    	(is_solid_infill(extrusions.entities.front()->role()) ? region.config().solid_infill_filament : region.config().sparse_infill_filament) :
			region.config().wall_filament.value) :
		this->extruder_override);
	return (extruder == 0) ? 0 : extruder - 1;
}

static double calc_max_layer_height(const PrintConfig &config, double max_object_layer_height)
{
    double max_layer_height = std::numeric_limits<double>::max();
    for (size_t i = 0; i < config.nozzle_diameter.values.size(); ++ i) {
        double mlh = config.max_layer_height.values[i];
        if (mlh == 0.)
            mlh = 0.75 * config.nozzle_diameter.values[i];
        max_layer_height = std::min(max_layer_height, mlh);
    }
    // The Prusa3D Fast (0.35mm layer height) print profile sets a higher layer height than what is normally allowed
    // by the nozzle. This is a hack and it works by increasing extrusion width. See GH #3919.
    return std::max(max_layer_height, max_object_layer_height);
}

//calculate the flush weight (first value) and filament change count(second value)
static FilamentChangeStats calc_filament_change_info_by_toolorder(const PrintConfig* config, const std::vector<int>& filament_map, const std::vector<FlushMatrix>& flush_matrix, const std::vector<std::vector<unsigned int>>& layer_sequences)
{
    FilamentChangeStats ret;
    std::unordered_map<int, int> flush_volume_per_filament;
    std::vector<unsigned int>last_filament_per_extruder(2, -1);

    int total_filament_change_count = 0;
    float total_filament_flush_weight = 0;
    for (const auto& ls : layer_sequences) {
        for (const auto& item : ls) {
            int extruder_id = filament_map[item];
            int last_filament = last_filament_per_extruder[extruder_id];
            if (last_filament != -1 && last_filament != item) {
                int flush_volume = flush_matrix[extruder_id][last_filament][item];
                flush_volume_per_filament[item] += flush_volume;
                total_filament_change_count += 1;
            }
            last_filament_per_extruder[extruder_id] = item;
        }
    }

    for (auto& fv : flush_volume_per_filament) {
        float weight = config->filament_density.get_at(fv.first) * 0.001 * fv.second;
        total_filament_flush_weight += weight;
    }

    ret.filament_change_count = total_filament_change_count;
    ret.filament_flush_weight = (int)total_filament_flush_weight;

    return ret;
}


// For the use case when each object is printed separately
// (print->config().print_sequence == PrintSequence::ByObject is true).
ToolOrdering::ToolOrdering(const PrintObject &object, unsigned int first_extruder, bool prime_multi_material)
{
    m_print_object_ptr = &object;
    m_print = const_cast<Print*>(object.print());
    if (object.layers().empty())
        return;

    // Initialize the print layers for just a single object.
    {
        std::vector<coordf_t> zs;
        zs.reserve(zs.size() + object.layers().size() + object.support_layers().size());
        for (auto layer : object.layers())
            zs.emplace_back(layer->print_z);
        for (auto layer : object.support_layers())
            zs.emplace_back(layer->print_z);
        this->initialize_layers(zs);
    }
    double max_layer_height = calc_max_layer_height(object.print()->config(), object.config().layer_height);

    // Collect extruders reuqired to print the layers.
    this->collect_extruders(object, std::vector<std::pair<double, unsigned int>>());

    // BBS
    // Reorder the extruders to minimize tool switches.
    if (first_extruder == (unsigned int)-1) {
        this->reorder_extruders(generate_first_layer_tool_order(object));
    }
    else {
        this->reorder_extruders(first_extruder);
    }

    this->fill_wipe_tower_partitions(object.print()->config(), object.layers().front()->print_z - object.layers().front()->height, max_layer_height);

    this->collect_extruder_statistics(prime_multi_material);

    this->mark_skirt_layers(object.print()->config(), max_layer_height);
}

// For the use case when all objects are printed at once.
// (print->config().print_sequence == PrintSequence::ByObject is false).
ToolOrdering::ToolOrdering(const Print &print, unsigned int first_extruder, bool prime_multi_material)
{
    m_print = const_cast<Print *>(&print);  // for update the context of print
    m_print_config_ptr = &print.config();

    // Initialize the print layers for all objects and all layers.
    coordf_t object_bottom_z = 0.;
    coordf_t max_layer_height = 0.;
    {
        std::vector<coordf_t> zs;
        for (auto object : print.objects()) {
            zs.reserve(zs.size() + object->layers().size() + object->support_layers().size());
            for (auto layer : object->layers())
                zs.emplace_back(layer->print_z);
            for (auto layer : object->support_layers())
                zs.emplace_back(layer->print_z);

            // Find first object layer that is not empty and save its print_z
            for (const Layer* layer : object->layers())
                if (layer->has_extrusions()) {
                    object_bottom_z = layer->print_z - layer->height;
                    break;
                }

            max_layer_height = std::max(max_layer_height, object->config().layer_height.value);
        }
        this->initialize_layers(zs);
    }
    max_layer_height = calc_max_layer_height(print.config(), max_layer_height);

	// Use the extruder switches from Model::custom_gcode_per_print_z to override the extruder to print the object.
	// Do it only if all the objects were configured to be printed with a single extruder.
	std::vector<std::pair<double, unsigned int>> per_layer_extruder_switches;

    // BBS
	if (auto num_filaments = unsigned(print.config().filament_diameter.size());
		num_filaments > 1 && print.object_extruders().size() == 1 && // the current Print's configuration is CustomGCode::MultiAsSingle
        //BBS: replace model custom gcode with current plate custom gcode
        print.model().get_curr_plate_custom_gcodes().mode == CustomGCode::MultiAsSingle) {
		// Printing a single extruder platter on a printer with more than 1 extruder (or single-extruder multi-material).
		// There may be custom per-layer tool changes available at the model.
        per_layer_extruder_switches = custom_tool_changes(print.model().get_curr_plate_custom_gcodes(), num_filaments);
	}

    // Collect extruders reuqired to print the layers.
    for (auto object : print.objects())
        this->collect_extruders(*object, per_layer_extruder_switches);

    // Reorder the extruders to minimize tool switches.
    std::vector<unsigned int> first_layer_tool_order;
    if (first_extruder == (unsigned int)-1) {
        first_layer_tool_order = generate_first_layer_tool_order(print);
    }

    if (!first_layer_tool_order.empty()) {
        this->reorder_extruders(first_layer_tool_order);
    }
    else {
        this->reorder_extruders(first_extruder);
    }

    this->fill_wipe_tower_partitions(print.config(), object_bottom_z, max_layer_height);

    this->collect_extruder_statistics(prime_multi_material);

    this->mark_skirt_layers(print.config(), max_layer_height);
}

// BBS
std::vector<unsigned int> ToolOrdering::generate_first_layer_tool_order(const Print& print)
{
    std::vector<unsigned int> tool_order;
    int initial_extruder_id = -1;
    std::map<int, double> min_areas_per_extruder;

    for (auto object : print.objects()) {
        auto first_layer = object->get_layer(0);
        for (auto layerm : first_layer->regions()) {
            int extruder_id = layerm->region().config().option("wall_filament")->getInt();

            for (auto expoly : layerm->raw_slices) {
                if (offset_ex(expoly, -0.2 * scale_(print.config().initial_layer_line_width)).empty())
                    continue;

                double contour_area = expoly.contour.area();
                auto iter = min_areas_per_extruder.find(extruder_id);
                if (iter == min_areas_per_extruder.end()) {
                    min_areas_per_extruder.insert({ extruder_id, contour_area });
                }
                else {
                    if (contour_area < min_areas_per_extruder.at(extruder_id)) {
                        min_areas_per_extruder[extruder_id] = contour_area;
                    }
                }
            }
        }
    }

    double max_minimal_area = 0.;
    for (auto ape : min_areas_per_extruder) {
        auto iter = tool_order.begin();
        for (; iter != tool_order.end(); iter++) {
            if (min_areas_per_extruder.at(*iter) < min_areas_per_extruder.at(ape.first))
                break;
        }

        tool_order.insert(iter, ape.first);
    }

    const ConfigOptionInts* first_layer_print_sequence_op = print.full_print_config().option<ConfigOptionInts>("first_layer_print_sequence");
    if (first_layer_print_sequence_op) {
        const std::vector<int>& print_sequence_1st = first_layer_print_sequence_op->values;
        if (print_sequence_1st.size() >= tool_order.size()) {
            std::sort(tool_order.begin(), tool_order.end(), [&print_sequence_1st](int lh, int rh) {
                auto lh_it = std::find(print_sequence_1st.begin(), print_sequence_1st.end(), lh);
                auto rh_it = std::find(print_sequence_1st.begin(), print_sequence_1st.end(), rh);

                if (lh_it == print_sequence_1st.end() || rh_it == print_sequence_1st.end())
                    return false;

                return lh_it < rh_it;
            });
        }
    }

    return tool_order;
}

std::vector<unsigned int> ToolOrdering::generate_first_layer_tool_order(const PrintObject& object)
{
    std::vector<unsigned int> tool_order;
    int initial_extruder_id = -1;
    std::map<int, double> min_areas_per_extruder;
    auto first_layer = object.get_layer(0);
    for (auto layerm : first_layer->regions()) {
        int extruder_id = layerm->region().config().option("wall_filament")->getInt();
        for (auto expoly : layerm->raw_slices) {
            if (offset_ex(expoly, -0.2 * scale_(object.config().line_width)).empty())
                continue;

            double contour_area = expoly.contour.area();
            auto iter = min_areas_per_extruder.find(extruder_id);
            if (iter == min_areas_per_extruder.end()) {
                min_areas_per_extruder.insert({ extruder_id, contour_area });
            }
            else {
                if (contour_area < min_areas_per_extruder.at(extruder_id)) {
                    min_areas_per_extruder[extruder_id] = contour_area;
                }
            }
        }
    }

    double max_minimal_area = 0.;
    for (auto ape : min_areas_per_extruder) {
        auto iter = tool_order.begin();
        for (; iter != tool_order.end(); iter++) {
            if (min_areas_per_extruder.at(*iter) < min_areas_per_extruder.at(ape.first))
                break;
        }

        tool_order.insert(iter, ape.first);
    }

    const ConfigOptionInts* first_layer_print_sequence_op = object.print()->full_print_config().option<ConfigOptionInts>("first_layer_print_sequence");
    if (first_layer_print_sequence_op) {
        const std::vector<int>& print_sequence_1st = first_layer_print_sequence_op->values;
        if (print_sequence_1st.size() >= tool_order.size()) {
            std::sort(tool_order.begin(), tool_order.end(), [&print_sequence_1st](int lh, int rh) {
                auto lh_it = std::find(print_sequence_1st.begin(), print_sequence_1st.end(), lh);
                auto rh_it = std::find(print_sequence_1st.begin(), print_sequence_1st.end(), rh);

                if (lh_it == print_sequence_1st.end() || rh_it == print_sequence_1st.end())
                    return false;

                return lh_it < rh_it;
            });
        }
    }

    return tool_order;
}

void ToolOrdering::initialize_layers(std::vector<coordf_t> &zs)
{
    sort_remove_duplicates(zs);
    // Merge numerically very close Z values.
    for (size_t i = 0; i < zs.size();) {
        // Find the last layer with roughly the same print_z.
        size_t j = i + 1;
        coordf_t zmax = zs[i] + EPSILON;
        for (; j < zs.size() && zs[j] <= zmax; ++ j) ;
        // Assign an average print_z to the set of layers with nearly equal print_z.
        m_layer_tools.emplace_back(LayerTools(0.5 * (zs[i] + zs[j-1])));
        i = j;
    }
}

// Collect extruders reuqired to print layers.
void ToolOrdering::collect_extruders(const PrintObject &object, const std::vector<std::pair<double, unsigned int>> &per_layer_extruder_switches)
{
    // Collect the support extruders.
    for (auto support_layer : object.support_layers()) {
        LayerTools   &layer_tools = this->tools_for_layer(support_layer->print_z);
        ExtrusionRole role = support_layer->support_fills.role();
        bool         has_support        = role == erMixed || role == erSupportMaterial || role == erSupportTransition;
        bool         has_interface      = role == erMixed || role == erSupportMaterialInterface;
        unsigned int extruder_support   = object.config().support_filament.value;
        unsigned int extruder_interface = object.config().support_interface_filament.value;
        if (has_support)
            layer_tools.extruders.push_back(extruder_support);
        if (has_interface)
            layer_tools.extruders.push_back(extruder_interface);
        if (has_support || has_interface) {
            layer_tools.has_support = true;
            layer_tools.wiping_extrusions().is_support_overriddable_and_mark(role, object);
        }
    }

    // Extruder overrides are ordered by print_z.
    std::vector<std::pair<double, unsigned int>>::const_iterator it_per_layer_extruder_override;
	it_per_layer_extruder_override = per_layer_extruder_switches.begin();
    unsigned int extruder_override = 0;

    // BBS: collect first layer extruders of an object's wall, which will be used by brim generator
    int layerCount = 0;
    std::vector<int> firstLayerExtruders;
    firstLayerExtruders.clear();

    // Collect the object extruders.
    for (auto layer : object.layers()) {
        LayerTools &layer_tools = this->tools_for_layer(layer->print_z);

        // Override extruder with the next
    	for (; it_per_layer_extruder_override != per_layer_extruder_switches.end() && it_per_layer_extruder_override->first < layer->print_z + EPSILON; ++ it_per_layer_extruder_override)
    		extruder_override = (int)it_per_layer_extruder_override->second;

        // Store the current extruder override (set to zero if no overriden), so that layer_tools.wiping_extrusions().is_overridable_and_mark() will use it.
        layer_tools.extruder_override = extruder_override;

        // What extruders are required to print this object layer?
        for (const LayerRegion *layerm : layer->regions()) {
            const PrintRegion &region = layerm->region();

            if (! layerm->perimeters.entities.empty()) {
                bool something_nonoverriddable = true;

                if (m_print_config_ptr) { // in this case print->config().print_sequence != PrintSequence::ByObject (see ToolOrdering constructors)
                    something_nonoverriddable = false;
                    for (const auto& eec : layerm->perimeters.entities) // let's check if there are nonoverriddable entities
                        if (!layer_tools.wiping_extrusions().is_overriddable_and_mark(dynamic_cast<const ExtrusionEntityCollection&>(*eec), *m_print_config_ptr, object, region))
                            something_nonoverriddable = true;
                }

                if (something_nonoverriddable){
               		layer_tools.extruders.emplace_back((extruder_override == 0) ? region.config().wall_filament.value : extruder_override);
                    if (layerCount == 0) {
                        firstLayerExtruders.emplace_back((extruder_override == 0) ? region.config().wall_filament.value : extruder_override);
                    }
                }

                layer_tools.has_object = true;
            }

            bool has_infill       = false;
            bool has_solid_infill = false;
            bool something_nonoverriddable = false;
            for (const ExtrusionEntity *ee : layerm->fills.entities) {
                // fill represents infill extrusions of a single island.
                const auto *fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                ExtrusionRole role = fill->entities.empty() ? erNone : fill->entities.front()->role();
                if (is_solid_infill(role))
                    has_solid_infill = true;
                else if (role != erNone)
                    has_infill = true;

                if (m_print_config_ptr) {
                    if (! layer_tools.wiping_extrusions().is_overriddable_and_mark(*fill, *m_print_config_ptr, object, region))
                        something_nonoverriddable = true;
                }
            }

            if (something_nonoverriddable || !m_print_config_ptr) {
            	if (extruder_override == 0) {
	                if (has_solid_infill)
	                    layer_tools.extruders.emplace_back(region.config().solid_infill_filament);
	                if (has_infill)
	                    layer_tools.extruders.emplace_back(region.config().sparse_infill_filament);
            	} else if (has_solid_infill || has_infill)
            		layer_tools.extruders.emplace_back(extruder_override);
            }
            if (has_solid_infill || has_infill)
                layer_tools.has_object = true;
        }
        layerCount++;
    }

    sort_remove_duplicates(firstLayerExtruders);
    const_cast<PrintObject&>(object).object_first_layer_wall_extruders = firstLayerExtruders;

    for (auto& layer : m_layer_tools) {
        // Sort and remove duplicates
        sort_remove_duplicates(layer.extruders);

        // make sure that there are some tools for each object layer (e.g. tall wiping object will result in empty extruders vector)
        if (layer.extruders.empty() && layer.has_object)
            layer.extruders.emplace_back(0); // 0="dontcare" extruder - it will be taken care of in reorder_extruders
    }
}

bool ToolOrdering::check_tpu_group(const std::vector<unsigned int>&used_filaments,const std::vector<int>& filament_maps,const PrintConfig* config)
{
    int check_extruder_id = -1;
    int master_extruder_id = config->master_extruder_id.value - 1; // transfer to 0 based idx
    std::map<int, std::vector<int>> extruder_filament_nums;
    for (unsigned int filament_id : used_filaments) {
        int extruder_id = filament_maps[filament_id];
        extruder_filament_nums[extruder_id].push_back(filament_id);

        std::string filament_type = config->filament_type.get_at(filament_id);
        if (filament_type == "TPU") {
            // if we meet two TPU filaments, just return false
            if(check_extruder_id==-1)
                check_extruder_id = filament_maps[filament_id];
            else
                return false;
        }
    }

    // TPU can only place in master extruder, and it should have no other filaments in the same extruder
    if (check_extruder_id != -1 && (check_extruder_id != master_extruder_id || extruder_filament_nums[check_extruder_id].size() > 1)) {
        return false;
    }

    return true;
}

// Reorder extruders to minimize layer changes.
void ToolOrdering::reorder_extruders(unsigned int last_extruder_id)
{
    if (m_layer_tools.empty())
        return;

    if (last_extruder_id == (unsigned int)-1) {
        // The initial print extruder has not been decided yet.
        // Initialize the last_extruder_id with the first non-zero extruder id used for the print.
        last_extruder_id = 0;
        for (size_t i = 0; i < m_layer_tools.size() && last_extruder_id == 0; ++ i) {
            const LayerTools &lt = m_layer_tools[i];
            for (unsigned int extruder_id : lt.extruders)
                if (extruder_id > 0) {
                    last_extruder_id = extruder_id;
                    break;
                }
        }
        if (last_extruder_id == 0)
            // Nothing to extrude.
            return;
    } else
        // 1 based index
        ++ last_extruder_id;

    for (LayerTools &lt : m_layer_tools) {
        if (lt.extruders.empty())
            continue;
        if (lt.extruders.size() == 1 && lt.extruders.front() == 0)
            lt.extruders.front() = last_extruder_id;
        else {
            if (lt.extruders.front() == 0)
                // Pop the "don't care" extruder, the "don't care" region will be merged with the next one.
                lt.extruders.erase(lt.extruders.begin());
            // Reorder the extruders to start with the last one.
            for (size_t i = 1; i < lt.extruders.size(); ++ i)
                if (lt.extruders[i] == last_extruder_id) {
                    // Move the last extruder to the front.
                    memmove(lt.extruders.data() + 1, lt.extruders.data(), i * sizeof(unsigned int));
                    lt.extruders.front() = last_extruder_id;
                    break;
                }

            // On first layer with wipe tower, prefer a soluble extruder
            // at the beginning, so it is not wiped on the first layer.
            if (lt == m_layer_tools[0] && m_print_config_ptr && m_print_config_ptr->enable_prime_tower) {
                for (size_t i = 0; i<lt.extruders.size(); ++i)
                    if (m_print_config_ptr->filament_soluble.get_at(lt.extruders[i]-1)) { // 1-based...
                        std::swap(lt.extruders[i], lt.extruders.front());
                        break;
                    }
            }
        }
        last_extruder_id = lt.extruders.back();
    }

    // Reindex the extruders, so they are zero based, not 1 based.
    for (LayerTools &lt : m_layer_tools)
        for (unsigned int &extruder_id : lt.extruders) {
            assert(extruder_id > 0);
            -- extruder_id;
        }

    // reorder the extruders for minimum flush volume
    reorder_extruders_for_minimum_flush_volume(true);
}

// BBS
void ToolOrdering::reorder_extruders(std::vector<unsigned int> tool_order_layer0)
{
    if (m_layer_tools.empty())
        return;

    // Reorder the extruders of first layer
    {
        LayerTools& lt = m_layer_tools[0];
        std::vector<unsigned int> layer0_extruders = lt.extruders;
        lt.extruders.clear();
        for (unsigned int extruder_id : tool_order_layer0) {
            auto iter = std::find(layer0_extruders.begin(), layer0_extruders.end(), extruder_id);
            if (iter != layer0_extruders.end()) {
                lt.extruders.push_back(extruder_id);
                *iter = (unsigned int)-1;
            }
        }

        for (unsigned int extruder_id : layer0_extruders) {
            if (extruder_id == 0)
                continue;

            if (extruder_id != (unsigned int)-1)
                lt.extruders.push_back(extruder_id);
        }

        // all extruders are zero
        if (lt.extruders.empty() && !tool_order_layer0.empty()) {
            lt.extruders.push_back(tool_order_layer0[0]);
        }
    }

    int last_extruder_id = 0;
    // BBS: fist layer may be empty or only has defult extrude id
    if (m_layer_tools[0].extruders.empty()) {
        // search for first extrude filament id
        for (size_t layer_id = 1; layer_id < m_layer_tools.size(); layer_id++) {
            for (auto const &extrude : m_layer_tools[layer_id].extruders) {
                if (extrude != 0) {
                    last_extruder_id = extrude;
                    break;
                }
            }

            if (last_extruder_id != 0)
                break;
        }
    } else {
        //use first layer last extruder
        last_extruder_id = m_layer_tools[0].extruders.back();
    }

    for (int i = 1; i < m_layer_tools.size(); i++) {
        LayerTools& lt = m_layer_tools[i];

        if (lt.extruders.empty())
            continue;
        if (lt.extruders.size() == 1 && lt.extruders.front() == 0)
            lt.extruders.front() = last_extruder_id;
        else {
            if (lt.extruders.front() == 0)
                // Pop the "don't care" extruder, the "don't care" region will be merged with the next one.
                lt.extruders.erase(lt.extruders.begin());
            // Reorder the extruders to start with the last one.
            for (size_t i = 1; i < lt.extruders.size(); ++i)
                if (lt.extruders[i] == last_extruder_id) {
                    // Move the last extruder to the front.
                    memmove(lt.extruders.data() + 1, lt.extruders.data(), i * sizeof(unsigned int));
                    lt.extruders.front() = last_extruder_id;
                    break;
                }
        }
        last_extruder_id = lt.extruders.back();
    }

    // Reindex the extruders, so they are zero based, not 1 based.
    for (LayerTools& lt : m_layer_tools)
        for (unsigned int& extruder_id : lt.extruders) {
            assert(extruder_id > 0);
            --extruder_id;
        }

    // reorder the extruders for minimum flush volume
    reorder_extruders_for_minimum_flush_volume(false);
}

void ToolOrdering::fill_wipe_tower_partitions(const PrintConfig &config, coordf_t object_bottom_z, coordf_t max_layer_height)
{
    if (m_layer_tools.empty())
        return;

    // Count the minimum number of tool changes per layer.
    size_t last_extruder = size_t(-1);
    for (LayerTools &lt : m_layer_tools) {
        lt.wipe_tower_partitions = lt.extruders.size();
        if (! lt.extruders.empty()) {
            if (last_extruder == size_t(-1) || last_extruder == lt.extruders.front())
                // The first extruder on this layer is equal to the current one, no need to do an initial tool change.
                -- lt.wipe_tower_partitions;
            last_extruder = lt.extruders.back();
        }
    }

    // Propagate the wipe tower partitions down to support the upper partitions by the lower partitions.
    for (int i = int(m_layer_tools.size()) - 2; i >= 0; -- i)
        m_layer_tools[i].wipe_tower_partitions = std::max(m_layer_tools[i + 1].wipe_tower_partitions, m_layer_tools[i].wipe_tower_partitions);

    //FIXME this is a hack to get the ball rolling.
    for (LayerTools &lt : m_layer_tools)
        lt.has_wipe_tower = (lt.has_object && (config.timelapse_type == TimelapseType::tlSmooth || lt.wipe_tower_partitions > 0))
            || lt.print_z < object_bottom_z + EPSILON;

    // Test for a raft, insert additional wipe tower layer to fill in the raft separation gap.
    for (size_t i = 0; i + 1 < m_layer_tools.size(); ++ i) {
        const LayerTools &lt      = m_layer_tools[i];
        const LayerTools &lt_next = m_layer_tools[i + 1];
        if (lt.print_z < object_bottom_z + EPSILON && lt_next.print_z >= object_bottom_z + EPSILON) {
            // lt is the last raft layer. Find the 1st object layer.
            size_t j = i + 1;
            for (; j < m_layer_tools.size() && ! m_layer_tools[j].has_wipe_tower; ++ j);
            if (j < m_layer_tools.size()) {
                const LayerTools &lt_object = m_layer_tools[j];
                coordf_t gap = lt_object.print_z - lt.print_z;
                assert(gap > 0.f);
                if (gap > max_layer_height + EPSILON) {
                    // Insert one additional wipe tower layer between lh.print_z and lt_object.print_z.
                    LayerTools lt_new(0.5f * (lt.print_z + lt_object.print_z));
                    // Find the 1st layer above lt_new.
                    for (j = i + 1; j < m_layer_tools.size() && m_layer_tools[j].print_z < lt_new.print_z - EPSILON; ++ j);
                    if (std::abs(m_layer_tools[j].print_z - lt_new.print_z) < EPSILON) {
						m_layer_tools[j].has_wipe_tower = true;
					} else {
						LayerTools &lt_extra = *m_layer_tools.insert(m_layer_tools.begin() + j, lt_new);
                        //LayerTools &lt_prev  = m_layer_tools[j];
                        LayerTools &lt_next  = m_layer_tools[j + 1];
                        assert(! m_layer_tools[j - 1].extruders.empty() && ! lt_next.extruders.empty());
                        // FIXME: Following assert tripped when running combine_infill.t. I decided to comment it out for now.
                        // If it is a bug, it's likely not critical, because this code is unchanged for a long time. It might
                        // still be worth looking into it more and decide if it is a bug or an obsolete assert.
                        //assert(lt_prev.extruders.back() == lt_next.extruders.front());
                        lt_extra.has_wipe_tower = true;
                        lt_extra.extruders.push_back(lt_next.extruders.front());
                        lt_extra.wipe_tower_partitions = lt_next.wipe_tower_partitions;
                    }
                }
            }
            break;
        }
    }

    // If the model contains empty layers (such as https://github.com/prusa3d/Slic3r/issues/1266), there might be layers
    // that were not marked as has_wipe_tower, even when they should have been. This produces a crash with soluble supports
    // and maybe other problems. We will therefore go through layer_tools and detect and fix this.
    // So, if there is a non-object layer starting with different extruder than the last one ended with (or containing more than one extruder),
    // we'll mark it with has_wipe tower.
    for (unsigned int i=0; i+1<m_layer_tools.size(); ++i) {
        LayerTools& lt = m_layer_tools[i];
        LayerTools& lt_next = m_layer_tools[i+1];
        if (lt.extruders.empty() || lt_next.extruders.empty())
            break;
        if (!lt_next.has_wipe_tower && (lt_next.extruders.front() != lt.extruders.back() || lt_next.extruders.size() > 1))
            lt_next.has_wipe_tower = true;
        // We should also check that the next wipe tower layer is no further than max_layer_height:
        unsigned int j = i+1;
        double last_wipe_tower_print_z = lt_next.print_z;
        while (++j < m_layer_tools.size()-1 && !m_layer_tools[j].has_wipe_tower)
            if (m_layer_tools[j+1].print_z - last_wipe_tower_print_z > max_layer_height + EPSILON) {
                m_layer_tools[j].has_wipe_tower = true;
                last_wipe_tower_print_z = m_layer_tools[j].print_z;
            }
    }

    // Calculate the wipe_tower_layer_height values.
    coordf_t wipe_tower_print_z_last = 0.;
    for (LayerTools &lt : m_layer_tools)
        if (lt.has_wipe_tower) {
            lt.wipe_tower_layer_height = lt.print_z - wipe_tower_print_z_last;
            wipe_tower_print_z_last = lt.print_z;
        }
}

void ToolOrdering::collect_extruder_statistics(bool prime_multi_material)
{
    m_first_printing_extruder = (unsigned int)-1;
    for (const auto &lt : m_layer_tools)
        if (! lt.extruders.empty()) {
            m_first_printing_extruder = lt.extruders.front();
            break;
        }

    m_last_printing_extruder = (unsigned int)-1;
    for (auto lt_it = m_layer_tools.rbegin(); lt_it != m_layer_tools.rend(); ++ lt_it)
        if (! lt_it->extruders.empty()) {
            m_last_printing_extruder = lt_it->extruders.back();
            break;
        }

    m_all_printing_extruders.clear();
    for (const auto &lt : m_layer_tools) {
        append(m_all_printing_extruders, lt.extruders);
        sort_remove_duplicates(m_all_printing_extruders);
    }

    if (prime_multi_material && ! m_all_printing_extruders.empty()) {
        // Reorder m_all_printing_extruders in the sequence they will be primed, the last one will be m_first_printing_extruder.
        // Then set m_first_printing_extruder to the 1st extruder primed.
        m_all_printing_extruders.erase(
            std::remove_if(m_all_printing_extruders.begin(), m_all_printing_extruders.end(),
                [ this ](const unsigned int eid) { return eid == m_first_printing_extruder; }),
            m_all_printing_extruders.end());
        m_all_printing_extruders.emplace_back(m_first_printing_extruder);
        m_first_printing_extruder = m_all_printing_extruders.front();
    }
}

std::set<std::pair<std::vector<unsigned int>, std::vector<unsigned int>>> generate_combinations(const std::vector<unsigned int> &extruders)
{
    int                                                                       n = extruders.size();
    std::vector<bool>                                                         flags(n);
    std::set<std::pair<std::vector<unsigned int>, std::vector<unsigned int>>> unique_combinations;

    if (extruders.empty())
        return unique_combinations;

    for (int i = 1; i <= n / 2; ++i) {
        std::fill(flags.begin(), flags.begin() + i, true);
        std::fill(flags.begin() + i, flags.end(), false);

        do {
            std::vector<unsigned int> group1, group2;
            for (int j = 0; j < n; ++j) {
                if (flags[j]) {
                    group1.push_back(extruders[j]);
                } else {
                    group2.push_back(extruders[j]);
                }
            }

            if (group1.size() > group2.size()) { std::swap(group1, group2); }

            unique_combinations.insert({group1, group2});

        } while (std::prev_permutation(flags.begin(), flags.end()));
    }

    return unique_combinations;
}

float get_flush_volume(const std::vector<int> &filament_maps, const std::vector<unsigned int> &extruders, const std::vector<FlushMatrix> &matrix, size_t nozzle_nums)
{
    std::vector<std::vector<unsigned int>> nozzle_filaments;
    nozzle_filaments.resize(nozzle_nums);

    for (unsigned int filament_id : extruders) {
        nozzle_filaments[filament_maps[filament_id]].emplace_back(filament_id);
    }

    float flush_volume = 0;
    for (size_t nozzle_id = 0; nozzle_id < nozzle_nums; ++nozzle_id) {
        for (size_t i = 0; i + 1 < nozzle_filaments[nozzle_id].size(); ++i) {
            flush_volume += matrix[nozzle_id][nozzle_filaments[nozzle_id][i]][nozzle_filaments[nozzle_id][i+1]];
        }
    }

    return flush_volume;
}

std::vector<int> ToolOrdering::get_recommended_filament_maps(const std::vector<std::vector<unsigned int>>& layer_filaments, const PrintConfig* print_config, const Print* print, const std::vector<std::set<int>>&physical_unprintables,const std::vector<std::set<int>>&geometric_unprintables)
{
    if (!print_config || layer_filaments.empty())
        return std::vector<int>();

    const unsigned int filament_nums = (unsigned int)(print_config->filament_colour.values.size() + EPSILON);

    // get flush matrix
    std::vector<FlushMatrix> nozzle_flush_mtx;
    size_t extruder_nums = print_config->nozzle_diameter.values.size();
    for (size_t nozzle_id = 0; nozzle_id < extruder_nums; ++nozzle_id) {
        std::vector<float>              flush_matrix(cast<float>(get_flush_volumes_matrix(print_config->flush_volumes_matrix.values, nozzle_id, extruder_nums)));
        std::vector<std::vector<float>> wipe_volumes;
        for (unsigned int i = 0; i < filament_nums; ++i)
            wipe_volumes.push_back(std::vector<float>(flush_matrix.begin() + i * filament_nums, flush_matrix.begin() + (i + 1) * filament_nums));

        nozzle_flush_mtx.emplace_back(wipe_volumes);
    }

    std::vector<LayerPrintSequence> other_layers_seqs;
    const ConfigOptionInts* other_layers_print_sequence_op = print_config->option<ConfigOptionInts>("other_layers_print_sequence");
    const ConfigOptionInt* other_layers_print_sequence_nums_op = print_config->option<ConfigOptionInt>("other_layers_print_sequence_nums");
    if (other_layers_print_sequence_op && other_layers_print_sequence_nums_op) {
        const std::vector<int>& print_sequence = other_layers_print_sequence_op->values;
        int                     sequence_nums = other_layers_print_sequence_nums_op->value;
        other_layers_seqs = get_other_layers_print_sequence(sequence_nums, print_sequence);
    }

    // other_layers_seq: the layer_idx and extruder_idx are base on 1
    auto get_custom_seq = [&other_layers_seqs](int layer_idx, std::vector<int>& out_seq) -> bool {
        for (size_t idx = other_layers_seqs.size() - 1; idx != size_t(-1); --idx) {
            const auto& other_layers_seq = other_layers_seqs[idx];
            if (layer_idx + 1 >= other_layers_seq.first.first && layer_idx + 1 <= other_layers_seq.first.second) {
                out_seq = other_layers_seq.second;
                return true;
            }
        }
        return false;
        };

    std::vector<int>ret(filament_nums, 0);
    // if mutli_extruder, calc group,otherwise set to 0
    if (extruder_nums == 2) {
        std::vector<std::string> extruder_ams_count_str = print_config->extruder_ams_count.values;
        auto                     extruder_ams_counts    = get_extruder_ams_count(extruder_ams_count_str);
        std::vector<int>         group_size             = {16, 16};
        if (extruder_ams_counts.size() > 0) {
            assert(extruder_ams_counts.size() == 2);
            for (int i = 0; i < extruder_ams_counts.size(); ++i) {
                group_size[i]         = 0;
                const auto &ams_count = extruder_ams_counts[i];
                for (auto iter = ams_count.begin(); iter != ams_count.end(); ++iter) { group_size[i] += iter->first * iter->second; }
            }
            // When the AMS count is 0, only external filament can be used, so set the capacity to 1.
            for(auto& size: group_size)
                if(size == 0)
                    size = 1;
        }

        FilamentGroupContext context;
        {
            context.flush_matrix = std::move(nozzle_flush_mtx);
            context.geometric_unprintables = geometric_unprintables;
            context.physical_unprintables = physical_unprintables;
            context.max_group_size = std::move(group_size);
            context.total_filament_num = (int)filament_nums;
            context.master_extruder_id = print_config->master_extruder_id.value - 1; // transfer to 0 based idx
        }
        // speacially handle tpu filaments
        auto used_filaments = collect_sorted_used_filaments(layer_filaments);
        auto tpu_filaments = get_filament_by_type(used_filaments, print_config, "TPU");

        if (!tpu_filaments.empty()) {
            for (size_t fidx = 0; fidx < filament_nums; ++fidx) {
                if (tpu_filaments.count(fidx))
                    ret[fidx] = context.master_extruder_id;
                else
                    ret[fidx] = 1 - context.master_extruder_id;
            }
        }
        else {
            FilamentGroup fg(context);
            fg.set_memory_threshold(0.02);
            fg.get_custom_seq = get_custom_seq;

            ret = fg.calc_filament_group(layer_filaments, FGStrategy::BestCost);

            // optimize for master extruder id
            optimize_group_for_master_extruder(used_filaments, context, ret);

            // optimize according to AMS filaments
            std::vector<std::vector<int>>memoryed_maps{ ret };
            {
                auto tmp_maps = fg.get_memoryed_groups();
                memoryed_maps.insert(memoryed_maps.end(), std::make_move_iterator(tmp_maps.begin()), std::make_move_iterator(tmp_maps.end()));
            }

            std::vector<std::string>used_colors;
            for (size_t idx = 0; idx < used_filaments.size(); ++idx)
                used_colors.emplace_back(print_config->filament_colour.get_at(used_filaments[idx]));

            auto ams_filament_info = print->get_extruder_filament_info();
            std::vector<std::vector<std::string>> ams_colors(extruder_nums);
            for (size_t i = 0; i < ams_filament_info.size(); ++i) {
                auto& arr = ams_filament_info[i];
                std::vector<std::string>colors;
                for (auto& item : arr)
                    colors.emplace_back(item.option<ConfigOptionStrings>("filament_colour")->get_at(0));
                ams_colors[i] = std::move(colors);
            }
            ret = select_best_group_for_ams(memoryed_maps, used_filaments, used_colors, ams_colors, similar_color_threshold_de2000);
        }
    }

    return ret;
}

FilamentChangeStats ToolOrdering::get_filament_change_stats(FilamentChangeMode mode)
{
    switch (mode)
    {
    case Slic3r::ToolOrdering::SingleExt:
        return m_stats_by_single_extruder;
    case Slic3r::ToolOrdering::MultiExtAuto:
        return m_stats_by_multi_extruder_auto;
    case Slic3r::ToolOrdering::MultiExtManual:
        return m_stats_by_multi_extruder_manual;
    default:
        break;
    }
    return m_stats_by_single_extruder;
}

void ToolOrdering::reorder_extruders_for_minimum_flush_volume(bool reorder_first_layer)
{
    const PrintConfig* print_config = m_print_config_ptr;
    if (!print_config && m_print_object_ptr) {
        print_config = &(m_print_object_ptr->print()->config());
    }

    if (!print_config || m_layer_tools.empty())
        return;

    const unsigned int number_of_extruders = (unsigned int)(print_config->filament_colour.values.size() + EPSILON);

    using FlushMatrix = std::vector<std::vector<float>>;
    size_t             nozzle_nums = print_config->nozzle_diameter.values.size();

    std::vector<FlushMatrix> nozzle_flush_mtx;
    for (size_t nozzle_id = 0; nozzle_id < nozzle_nums; ++nozzle_id) {
        std::vector<float> flush_matrix(cast<float>(get_flush_volumes_matrix(print_config->flush_volumes_matrix.values, nozzle_id, nozzle_nums)));
        std::vector<std::vector<float>> wipe_volumes;
        for (unsigned int i = 0; i < number_of_extruders; ++i)
            wipe_volumes.push_back(std::vector<float>(flush_matrix.begin() + i * number_of_extruders, flush_matrix.begin() + (i + 1) * number_of_extruders));

        nozzle_flush_mtx.emplace_back(wipe_volumes);
    }

    std::vector<int>filament_maps(number_of_extruders, 0);
    FilamentMapMode map_mode = FilamentMapMode::fmmAuto;

    std::vector<std::vector<unsigned int>> layer_filaments;
    for (auto& lt : m_layer_tools) {
        layer_filaments.emplace_back(lt.extruders);
    }

    std::vector<unsigned int> used_filaments = collect_sorted_used_filaments(layer_filaments);

    std::vector<std::set<int>>geometric_unprintables = get_geometrical_unprintables(m_print->get_unprintable_filament_ids(), print_config);
    std::vector<std::set<int>>physical_unprintables = get_physical_unprintables(used_filaments, print_config);

    if (nozzle_nums > 1) {
        filament_maps = m_print->get_filament_maps();
        map_mode = m_print->get_filament_map_mode();
        // only check and map in sequence mode, in by object mode, we check the map in print.cpp
        if (print_config->print_sequence != PrintSequence::ByObject || m_print->objects().size() == 1) {
            if (map_mode == FilamentMapMode::fmmAuto) {
                const PrintConfig* print_config = m_print_config_ptr;
                if (!print_config && m_print_object_ptr) {
                    print_config = &(m_print_object_ptr->print()->config());
                }

                filament_maps = ToolOrdering::get_recommended_filament_maps(layer_filaments, print_config, m_print, physical_unprintables, geometric_unprintables);

                if (filament_maps.empty())
                    return;
                std::transform(filament_maps.begin(), filament_maps.end(), filament_maps.begin(), [](int value) { return value + 1; });
                m_print->update_filament_maps_to_config(filament_maps);
            }
            std::transform(filament_maps.begin(), filament_maps.end(), filament_maps.begin(), [](int value) { return value - 1; });

            if (!check_tpu_group(used_filaments, filament_maps, print_config)) {
                if (map_mode == FilamentMapMode::fmmManual) {
                    throw Slic3r::RuntimeError(std::string("Manual grouping error: TPU can only be placed in a nozzle alone."));
                }
                else {
                    throw Slic3r::RuntimeError(std::string("Auto grouping error: TPU can only be placed in a nozzle alone."));
                }
            }
        }
        else {
            // we just need to change the map to 0 based
            std::transform(filament_maps.begin(), filament_maps.end(), filament_maps.begin(), [](int value) {return value - 1; });
        }
    }
    else if (nozzle_nums == 1) {
        filament_maps = m_print->get_filament_maps();
        bool invalid = std::any_of(filament_maps.begin(), filament_maps.end(), [](int value) { return value != 1; });
        if (invalid) {
            assert(false);
            std::stringstream sstream;
            for (size_t i = 0; i < filament_maps.size(); ++i) {
                if (i != 0)
                    sstream << " ";
                sstream << filament_maps[i];
            }
            BOOST_LOG_TRIVIAL(error) << "The filament_map of single printer is invalid. filament_map = " << sstream.str();
            std::fill(filament_maps.begin(), filament_maps.end(), 1);
            m_print->update_filament_maps_to_config(filament_maps);
        }
        std::transform(filament_maps.begin(), filament_maps.end(), filament_maps.begin(), [](int value) { return value - 1; });
    }

    std::vector<std::vector<unsigned int>>filament_sequences;
    std::vector<unsigned int>filament_lists(number_of_extruders);
    std::iota(filament_lists.begin(), filament_lists.end(), 0);

    std::vector<LayerPrintSequence> other_layers_seqs;
    const ConfigOptionInts* other_layers_print_sequence_op = print_config->option<ConfigOptionInts>("other_layers_print_sequence");
    const ConfigOptionInt* other_layers_print_sequence_nums_op = print_config->option<ConfigOptionInt>("other_layers_print_sequence_nums");
    if (other_layers_print_sequence_op && other_layers_print_sequence_nums_op) {
        const std::vector<int>& print_sequence = other_layers_print_sequence_op->values;
        int                     sequence_nums = other_layers_print_sequence_nums_op->value;
        other_layers_seqs = get_other_layers_print_sequence(sequence_nums, print_sequence);
    }

    std::vector<unsigned int>first_layer_filaments;
    if (!m_layer_tools.empty())
        first_layer_filaments = m_layer_tools[0].extruders;

    // other_layers_seq: the layer_idx and extruder_idx are base on 1
    auto get_custom_seq = [&other_layers_seqs,&reorder_first_layer,&first_layer_filaments](int layer_idx, std::vector<int>& out_seq) -> bool {
        if (!reorder_first_layer && layer_idx == 0) {
            out_seq.resize(first_layer_filaments.size());
            std::transform(first_layer_filaments.begin(), first_layer_filaments.end(), out_seq.begin(), [](auto item) {return item + 1; });
            return true;
        }
        for (size_t idx = other_layers_seqs.size() - 1; idx != size_t(-1); --idx) {
            const auto& other_layers_seq = other_layers_seqs[idx];
            if (layer_idx + 1 >= other_layers_seq.first.first && layer_idx + 1 <= other_layers_seq.first.second) {
                out_seq = other_layers_seq.second;
                return true;
            }
        }
        return false;
        };

    reorder_filaments_for_minimum_flush_volume(
        filament_lists,
        filament_maps,
        layer_filaments,
        nozzle_flush_mtx,
        get_custom_seq,
        &filament_sequences
    );

    auto curr_flush_info = calc_filament_change_info_by_toolorder(print_config, filament_maps, nozzle_flush_mtx, filament_sequences);
    if (nozzle_nums <= 1)
        m_stats_by_single_extruder = curr_flush_info;
    else if (map_mode == fmmAuto)
        m_stats_by_multi_extruder_auto = curr_flush_info;
    else if (map_mode == fmmManual)
        m_stats_by_multi_extruder_manual = curr_flush_info;

    // in multi extruder mode,collect data with other mode
    if (nozzle_nums > 1) {
        // always calculate the info by one extruder
        {
            std::vector<std::vector<unsigned int>>filament_sequences_one_extruder;
            auto maps_without_group = filament_maps;
            for (auto& item : maps_without_group)
                item = 0;
            reorder_filaments_for_minimum_flush_volume(
                filament_lists,
                maps_without_group,
                layer_filaments,
                nozzle_flush_mtx,
                get_custom_seq,
                &filament_sequences_one_extruder
            );
            m_stats_by_single_extruder = calc_filament_change_info_by_toolorder(print_config, maps_without_group, nozzle_flush_mtx, filament_sequences_one_extruder);
        }
        // if in manual mode,also calculate the info by auto mode
        if (map_mode == fmmManual)
        {
            std::vector<std::vector<unsigned int>>filament_sequences_one_extruder;
            std::vector<int>filament_maps_auto = get_recommended_filament_maps(layer_filaments, print_config, m_print, physical_unprintables, geometric_unprintables);
            reorder_filaments_for_minimum_flush_volume(
                filament_lists,
                filament_maps_auto,
                layer_filaments,
                nozzle_flush_mtx,
                get_custom_seq,
                &filament_sequences_one_extruder
            );
            m_stats_by_multi_extruder_auto = calc_filament_change_info_by_toolorder(print_config, filament_maps_auto, nozzle_flush_mtx, filament_sequences_one_extruder);
        }
    }

    for (size_t i = 0; i < filament_sequences.size(); ++i)
        m_layer_tools[i].extruders = std::move(filament_sequences[i]);
}
// Layers are marked for infinite skirt aka draft shield. Not all the layers have to be printed.
void ToolOrdering::mark_skirt_layers(const PrintConfig &config, coordf_t max_layer_height)
{
    if (m_layer_tools.empty())
        return;

    if (m_layer_tools.front().extruders.empty()) {
        // Empty first layer, no skirt will be printed.
        //FIXME throw an exception?
        return;
    }

    size_t i = 0;
    for (;;) {
        m_layer_tools[i].has_skirt = true;
        size_t j = i + 1;
        for (; j < m_layer_tools.size() && ! m_layer_tools[j].has_object; ++ j);
        // i and j are two successive layers printing an object.
        if (j == m_layer_tools.size())
            // Don't print skirt above the last object layer.
            break;
        // Mark some printing intermediate layers as having skirt.
        double last_z = m_layer_tools[i].print_z;
        for (size_t k = i + 1; k < j; ++ k) {
            if (m_layer_tools[k + 1].print_z - last_z > max_layer_height + EPSILON) {
                // Layer k is the last one not violating the maximum layer height.
                // Don't extrude skirt on empty layers.
                while (m_layer_tools[k].extruders.empty())
                    -- k;
                if (m_layer_tools[k].has_skirt) {
                    // Skirt cannot be generated due to empty layers, there would be a missing layer in the skirt.
                    //FIXME throw an exception?
                    break;
                }
                m_layer_tools[k].has_skirt = true;
                last_z = m_layer_tools[k].print_z;
            }
        }
        i = j;
    }
}

// Assign a pointer to a custom G-code to the respective ToolOrdering::LayerTools.
// Ignore color changes, which are performed on a layer and for such an extruder, that the extruder will not be printing above that layer.
// If multiple events are planned over a span of a single layer, use the last one.

// BBS: replace model custom gcode with current plate custom gcode
static CustomGCode::Info custom_gcode_per_print_z;
void ToolOrdering::assign_custom_gcodes(const Print& print)
{
    // Only valid for non-sequential print.
    assert(print.config().print_sequence == PrintSequence::ByLayer);

    custom_gcode_per_print_z = print.model().get_curr_plate_custom_gcodes();
    if (custom_gcode_per_print_z.gcodes.empty())
        return;

    // BBS
    auto num_filaments = unsigned(print.config().filament_diameter.size());
    CustomGCode::Mode mode =
        (num_filaments == 1) ? CustomGCode::SingleExtruder :
        print.object_extruders().size() == 1 ? CustomGCode::MultiAsSingle : CustomGCode::MultiExtruder;
    CustomGCode::Mode           model_mode = print.model().get_curr_plate_custom_gcodes().mode;
    auto custom_gcode_it = custom_gcode_per_print_z.gcodes.rbegin();
    // Tool changes and color changes will be ignored, if the model's tool/color changes were entered in mm mode and the print is in non mm mode
    // or vice versa.
    bool ignore_tool_and_color_changes = (mode == CustomGCode::MultiExtruder) != (model_mode == CustomGCode::MultiExtruder);
    // If printing on a single extruder machine, make the tool changes trigger color change (M600) events.
    bool tool_changes_as_color_changes = mode == CustomGCode::SingleExtruder && model_mode == CustomGCode::MultiAsSingle;

    auto apply_custom_gcode_to_layer = [mode,
        ignore_tool_and_color_changes,
        tool_changes_as_color_changes,
        num_filaments](LayerTools& lt, const std::vector<unsigned char>& extruder_printing_above, const CustomGCode::Item& item)
        {
            bool color_change = item.type == CustomGCode::ColorChange;
            bool tool_change = item.type == CustomGCode::ToolChange;
            bool pause_or_custom_gcode = !color_change && !tool_change;
            bool apply_color_change = !ignore_tool_and_color_changes &&
                // If it is color change, it will actually be useful as the exturder above will print.
                // BBS
                (color_change ?
                    mode == CustomGCode::SingleExtruder ||
                    (item.extruder <= int(num_filaments) && extruder_printing_above[unsigned(item.extruder - 1)]) :
                    tool_change && tool_changes_as_color_changes);
            if (pause_or_custom_gcode || apply_color_change)
                lt.custom_gcode = &item;
        };

    std::unordered_map<int, std::vector<unsigned char>> extruder_print_above_by_layer;
    {
        std::vector<unsigned char> extruder_printing_above(num_filaments, false);
        for (auto iter = m_layer_tools.rbegin(); iter != m_layer_tools.rend(); ++iter) {
            for (unsigned int i : iter->extruders)
                extruder_printing_above[i] = true;
            int layer_idx = m_layer_tools.rend() - iter - 1;
            extruder_print_above_by_layer.emplace(layer_idx, extruder_printing_above);
        }
    }

    for (auto custom_gcode_it = custom_gcode_per_print_z.gcodes.rbegin(); custom_gcode_it != custom_gcode_per_print_z.gcodes.rend(); ++custom_gcode_it) {
        if (custom_gcode_it->type == CustomGCode::ToolChange)
            continue;

        auto layer_it_upper = std::upper_bound(m_layer_tools.begin(), m_layer_tools.end(), custom_gcode_it->print_z, [](double z,const LayerTools& lt) {
            return z < lt.print_z;
            });

        int upper_layer_idx = layer_it_upper - m_layer_tools.begin();
        if (layer_it_upper == m_layer_tools.begin()) {
            apply_custom_gcode_to_layer(*layer_it_upper, extruder_print_above_by_layer[0], *custom_gcode_it);
        }
        else if (layer_it_upper == m_layer_tools.end()) {
            auto layer_it_lower = std::prev(layer_it_upper);
            int lower_layer_idx = layer_it_lower - m_layer_tools.begin();
            apply_custom_gcode_to_layer(*layer_it_lower, extruder_print_above_by_layer[lower_layer_idx], *custom_gcode_it);
        }
        else {
            auto layer_it_lower = std::prev(layer_it_upper);
            int lower_layer_idx = layer_it_lower - m_layer_tools.begin();
            double gap_to_lower = std::fabs(custom_gcode_it->print_z - layer_it_lower->print_z);
            double gap_to_upper = std::fabs(custom_gcode_it->print_z - layer_it_upper->print_z);
            if (gap_to_lower < gap_to_upper)
                apply_custom_gcode_to_layer(*layer_it_lower, extruder_print_above_by_layer[lower_layer_idx], *custom_gcode_it);
            else
                apply_custom_gcode_to_layer(*layer_it_upper, extruder_print_above_by_layer[upper_layer_idx], *custom_gcode_it);
        }
    }
}

const LayerTools& ToolOrdering::tools_for_layer(coordf_t print_z) const
{
    auto it_layer_tools = std::lower_bound(m_layer_tools.begin(), m_layer_tools.end(), LayerTools(print_z - EPSILON));
    assert(it_layer_tools != m_layer_tools.end());
    coordf_t dist_min = std::abs(it_layer_tools->print_z - print_z);
    for (++ it_layer_tools; it_layer_tools != m_layer_tools.end(); ++ it_layer_tools) {
        coordf_t d = std::abs(it_layer_tools->print_z - print_z);
        if (d >= dist_min)
            break;
        dist_min = d;
    }
    -- it_layer_tools;
    assert(dist_min < EPSILON);
    return *it_layer_tools;
}

// This function is called from Print::mark_wiping_extrusions and sets extruder this entity should be printed with (-1 .. as usual)
void WipingExtrusions::set_extruder_override(const ExtrusionEntity* entity, const PrintObject* object, size_t copy_id, int extruder, size_t num_of_copies)
{
    something_overridden = true;

    auto entity_map_it = (entity_map.emplace(std::make_tuple(entity, object), ExtruderPerCopy())).first; // (add and) return iterator
    ExtruderPerCopy& copies_vector = entity_map_it->second;
    copies_vector.resize(num_of_copies, -1);

    if (copies_vector[copy_id] != -1)
        std::cout << "ERROR: Entity extruder overriden multiple times!!!\n";    // A debugging message - this must never happen.

    copies_vector[copy_id] = extruder;
}

// BBS
void WipingExtrusions::set_support_extruder_override(const PrintObject* object, size_t copy_id, int extruder, size_t num_of_copies)
{
    something_overridden = true;
    support_map.emplace(object, extruder);
}

void WipingExtrusions::set_support_interface_extruder_override(const PrintObject* object, size_t copy_id, int extruder, size_t num_of_copies)
{
    something_overridden = true;
    support_intf_map.emplace(object, extruder);
}

// Finds first non-soluble extruder on the layer
int WipingExtrusions::first_nonsoluble_extruder_on_layer(const PrintConfig& print_config) const
{
    const LayerTools& lt = *m_layer_tools;
    for (auto extruders_it = lt.extruders.begin(); extruders_it != lt.extruders.end(); ++extruders_it)
        if (!print_config.filament_soluble.get_at(*extruders_it) && !print_config.filament_is_support.get_at(*extruders_it))
            return (*extruders_it);

    return (-1);
}

// Finds last non-soluble extruder on the layer
int WipingExtrusions::last_nonsoluble_extruder_on_layer(const PrintConfig& print_config) const
{
    const LayerTools& lt = *m_layer_tools;
    for (auto extruders_it = lt.extruders.rbegin(); extruders_it != lt.extruders.rend(); ++extruders_it)
        if (!print_config.filament_soluble.get_at(*extruders_it) && !print_config.filament_is_support.get_at(*extruders_it))
            return (*extruders_it);

    return (-1);
}

// Decides whether this entity could be overridden
bool WipingExtrusions::is_overriddable(const ExtrusionEntityCollection& eec, const PrintConfig& print_config, const PrintObject& object, const PrintRegion& region) const
{
    if (print_config.filament_soluble.get_at(m_layer_tools->extruder(eec, region)))
        return false;

    if (object.config().flush_into_objects)
        return true;

    if (!object.config().flush_into_infill || eec.role() != erInternalInfill)
        return false;

    return true;
}

// BBS
bool WipingExtrusions::is_support_overriddable(const ExtrusionRole role, const PrintObject& object) const
{
    if (!object.config().flush_into_support)
        return false;

    if (role == erMixed) {
        return object.config().support_filament == 0 || object.config().support_interface_filament == 0;
    }
    else if (role == erSupportMaterial || role == erSupportTransition) {
        return object.config().support_filament == 0;
    }
    else if (role == erSupportMaterialInterface) {
        return object.config().support_interface_filament == 0;
    }

    return false;
}

// Following function iterates through all extrusions on the layer, remembers those that could be used for wiping after toolchange
// and returns volume that is left to be wiped on the wipe tower.
float WipingExtrusions::mark_wiping_extrusions(const Print& print, unsigned int old_extruder, unsigned int new_extruder, float volume_to_wipe)
{
    const LayerTools& lt = *m_layer_tools;
    const float min_infill_volume = 0.f; // ignore infill with smaller volume than this

    if (! this->something_overridable || volume_to_wipe <= 0. || print.config().filament_soluble.get_at(old_extruder) || print.config().filament_soluble.get_at(new_extruder))
        return std::max(0.f, volume_to_wipe); // Soluble filament cannot be wiped in a random infill, neither the filament after it

    // BBS
    if (print.config().filament_is_support.get_at(old_extruder) || print.config().filament_is_support.get_at(new_extruder))
        return std::max(0.f, volume_to_wipe); // Support filament cannot be used to print support, infill, wipe_tower, etc.

    // we will sort objects so that dedicated for wiping are at the beginning:
    ConstPrintObjectPtrs object_list = print.objects().vector();
    // BBS: fix the exception caused by not fixed order between different objects
    std::sort(object_list.begin(), object_list.end(), [object_list](const PrintObject* a, const PrintObject* b) {
        if (a->config().flush_into_objects != b->config().flush_into_objects) {
            return a->config().flush_into_objects.getBool();
        }
        else {
            return a->id() < b->id();
        }
    });

    // We will now iterate through
    //  - first the dedicated objects to mark perimeters or infills (depending on infill_first)
    //  - second through the dedicated ones again to mark infills or perimeters (depending on infill_first)
    //  - then all the others to mark infills (in case that !infill_first, we must also check that the perimeter is finished already
    // this is controlled by the following variable:
    bool perimeters_done = false;

    for (int i=0 ; i<(int)object_list.size() + (perimeters_done ? 0 : 1); ++i) {
        if (!perimeters_done && (i==(int)object_list.size() || !object_list[i]->config().flush_into_objects)) { // we passed the last dedicated object in list
            perimeters_done = true;
            i=-1;   // let's go from the start again
            continue;
        }

        const PrintObject* object = object_list[i];

        // Finds this layer:
        const Layer* this_layer = object->get_layer_at_printz(lt.print_z, EPSILON);
        if (this_layer == nullptr)
        	continue;

        size_t num_of_copies = object->instances().size();

        // iterate through copies (aka PrintObject instances) first, so that we mark neighbouring infills to minimize travel moves
        for (unsigned int copy = 0; copy < num_of_copies; ++copy) {
            for (const LayerRegion *layerm : this_layer->regions()) {
                const auto &region = layerm->region();

                if (!object->config().flush_into_infill && !object->config().flush_into_objects && !object->config().flush_into_support)
                    continue;
                bool wipe_into_infill_only = !object->config().flush_into_objects && object->config().flush_into_infill;
                bool is_infill_first = print.config().is_infill_first;
                if (is_infill_first != perimeters_done || wipe_into_infill_only) {
                    for (const ExtrusionEntity* ee : layerm->fills.entities) {                      // iterate through all infill Collections
                        auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);

                        if (!is_overriddable(*fill, print.config(), *object, region))
                            continue;

                        if (wipe_into_infill_only && ! is_infill_first)
                            // In this case we must check that the original extruder is used on this layer before the one we are overridding
                            // (and the perimeters will be finished before the infill is printed):
                            if (!lt.is_extruder_order(lt.wall_filament(region), new_extruder))
                                continue;

                        if ((!is_entity_overridden(fill, object, copy) && fill->total_volume() > min_infill_volume))
                        {     // this infill will be used to wipe this extruder
                            set_extruder_override(fill, object, copy, new_extruder, num_of_copies);
                            if ((volume_to_wipe -= float(fill->total_volume())) <= 0.f)
                            	// More material was purged already than asked for.
	                            return 0.f;
                        }
                    }
                }

                // Now the same for perimeters - see comments above for explanation:
                if (object->config().flush_into_objects && is_infill_first == perimeters_done)
                {
                    for (const ExtrusionEntity* ee : layerm->perimeters.entities) {
                        auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                        if (is_overriddable(*fill, print.config(), *object, region) && !is_entity_overridden(fill, object, copy) && fill->total_volume() > min_infill_volume) {
                            set_extruder_override(fill, object, copy, new_extruder, num_of_copies);
                            if ((volume_to_wipe -= float(fill->total_volume())) <= 0.f)
                            	// More material was purged already than asked for.
	                            return 0.f;
                        }
                    }
                }
            }

            // BBS
            if (object->config().flush_into_support) {
                auto& object_config = object->config();
                const SupportLayer* this_support_layer = object->get_support_layer_at_printz(lt.print_z, EPSILON);

                do {
                    if (this_support_layer == nullptr)
                        break;

                    bool support_overriddable = object_config.support_filament == 0;
                    bool support_intf_overriddable = object_config.support_interface_filament == 0;
                    if (!support_overriddable && !support_intf_overriddable)
                        break;

                    auto &entities = this_support_layer->support_fills.entities;
                    if (support_overriddable && !is_support_overridden(object) && !(object_config.support_interface_not_for_body.value && !support_intf_overriddable &&(new_extruder==object_config.support_interface_filament-1||old_extruder==object_config.support_interface_filament-1))) {
                        set_support_extruder_override(object, copy, new_extruder, num_of_copies);
                        for (const ExtrusionEntity* ee : entities) {
                            if (ee->role() == erSupportMaterial || ee->role() == erSupportTransition)
                                volume_to_wipe -= ee->total_volume();

                            if (volume_to_wipe <= 0.f)
                                return 0.f;
                        }
                    }

                    if (support_intf_overriddable && !is_support_interface_overridden(object)) {
                        set_support_interface_extruder_override(object, copy, new_extruder, num_of_copies);
                        for (const ExtrusionEntity* ee : entities) {
                            if (ee->role() == erSupportMaterialInterface)
                                volume_to_wipe -= ee->total_volume();

                            if (volume_to_wipe <= 0.f)
                                return 0.f;
                        }
                    }
                } while (0);
            }
        }
    }
	// Some purge remains to be done on the Wipe Tower.
    assert(volume_to_wipe > 0.);
    return volume_to_wipe;
}



// Called after all toolchanges on a layer were mark_infill_overridden. There might still be overridable entities,
// that were not actually overridden. If they are part of a dedicated object, printing them with the extruder
// they were initially assigned to might mean violating the perimeter-infill order. We will therefore go through
// them again and make sure we override it.
void WipingExtrusions::ensure_perimeters_infills_order(const Print& print)
{
	if (! this->something_overridable)
		return;

    const LayerTools& lt = *m_layer_tools;
    unsigned int first_nonsoluble_extruder = first_nonsoluble_extruder_on_layer(print.config());
    unsigned int last_nonsoluble_extruder = last_nonsoluble_extruder_on_layer(print.config());

    for (const PrintObject* object : print.objects()) {
        // Finds this layer:
        const Layer* this_layer = object->get_layer_at_printz(lt.print_z, EPSILON);
        if (this_layer == nullptr)
        	continue;
        size_t num_of_copies = object->instances().size();

        for (size_t copy = 0; copy < num_of_copies; ++copy) {    // iterate through copies first, so that we mark neighbouring infills to minimize travel moves
            for (const LayerRegion *layerm : this_layer->regions()) {
                const auto &region = layerm->region();
                //BBS
                if (!object->config().flush_into_infill && !object->config().flush_into_objects)
                    continue;

                bool is_infill_first = print.config().is_infill_first;
                for (const ExtrusionEntity* ee : layerm->fills.entities) {                      // iterate through all infill Collections
                    auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);

                    if (!is_overriddable(*fill, print.config(), *object, region)
                     || is_entity_overridden(fill, object, copy) )
                        continue;

                    // This infill could have been overridden but was not - unless we do something, it could be
                    // printed before its perimeter, or not be printed at all (in case its original extruder has
                    // not been added to LayerTools
                    // Either way, we will now force-override it with something suitable:
                    //BBS
                    if (is_infill_first
                    //BBS
                    //|| object->config().flush_into_objects  // in this case the perimeter is overridden, so we can override by the last one safely
                    || lt.is_extruder_order(lt.wall_filament(region), last_nonsoluble_extruder    // !infill_first, but perimeter is already printed when last extruder prints
                    || ! lt.has_extruder(lt.sparse_infill_filament(region)))) // we have to force override - this could violate infill_first (FIXME)
                        set_extruder_override(fill, object, copy, (is_infill_first ? first_nonsoluble_extruder : last_nonsoluble_extruder), num_of_copies);
                    else {
                        // In this case we can (and should) leave it to be printed normally.
                        // Force overriding would mean it gets printed before its perimeter.
                    }
                }

                // Now the same for perimeters - see comments above for explanation:
                for (const ExtrusionEntity* ee : layerm->perimeters.entities) {                      // iterate through all perimeter Collections
                    auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                    if (is_overriddable(*fill, print.config(), *object, region) && ! is_entity_overridden(fill, object, copy))
                        set_extruder_override(fill, object, copy, (is_infill_first ? last_nonsoluble_extruder : first_nonsoluble_extruder), num_of_copies);
                }
            }
        }
    }
}

// Following function is called from GCode::process_layer and returns pointer to vector with information about which extruders should be used for given copy of this entity.
// If this extrusion does not have any override, nullptr is returned.
// Otherwise it modifies the vector in place and changes all -1 to correct_extruder_id (at the time the overrides were created, correct extruders were not known,
// so -1 was used as "print as usual").
// The resulting vector therefore keeps track of which extrusions are the ones that were overridden and which were not. If the extruder used is overridden,
// its number is saved as is (zero-based index). Regular extrusions are saved as -number-1 (unfortunately there is no negative zero).
const WipingExtrusions::ExtruderPerCopy* WipingExtrusions::get_extruder_overrides(const ExtrusionEntity* entity, const PrintObject* object, int correct_extruder_id, size_t num_of_copies)
{
	ExtruderPerCopy *overrides = nullptr;
    auto entity_map_it = entity_map.find(std::make_tuple(entity, object));
    if (entity_map_it != entity_map.end()) {
        overrides = &entity_map_it->second;
    	overrides->resize(num_of_copies, -1);
	    // Each -1 now means "print as usual" - we will replace it with actual extruder id (shifted it so we don't lose that information):
	    std::replace(overrides->begin(), overrides->end(), -1, -correct_extruder_id-1);
	}
    return overrides;
}

// BBS
int WipingExtrusions::get_support_extruder_overrides(const PrintObject* object)
{
    auto iter = support_map.find(object);
    if (iter != support_map.end())
        return iter->second;

    return -1;
}

int WipingExtrusions::get_support_interface_extruder_overrides(const PrintObject* object)
{
    auto iter = support_intf_map.find(object);
    if (iter != support_intf_map.end())
        return iter->second;

    return -1;
}


} // namespace Slic3r
