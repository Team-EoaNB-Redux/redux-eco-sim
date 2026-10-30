#pragma once

// EcoSim — the ONLY class exposed to GDScript. A thin adapter over sim::Simulation: Godot types in, core calls,
// Godot types out. UI code never reaches past this surface. Values are addressed with HOI4 variable references
// plus a scope prefix ("ENG.treasury", "952.base_wage", "global.base_price@token:steel").
//
// Threading: run_months_async/run_days_async simulate on a worker thread. While is_busy() is true every query
// returns an empty/zero result; the UI polls `poll_finished()` once per frame and refreshes when it returns true.

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace sim {
class Simulation;
struct StateMap;
}

namespace godot {

class EcoSim : public RefCounted {
    GDCLASS(EcoSim, RefCounted)

public:
    EcoSim();
    ~EcoSim() override;

    String get_version() const;
    String format_fixed(const String& literal) const;

    // ---- lifecycle ----
    String find_mod_root(const String& start_dir) const; // "" if not found
    String load_scenario(const String& mod_root, const String& scenario_path, const String& semantics); // "" on success
    void startup();
    void run_months_async(int months);
    void run_days_async(int days);
    void cancel();
    bool is_busy() const { return busy_.load(); }
    bool is_loaded() const;
    bool poll_finished(); // true once after each async run completes
    String get_date() const;
    int get_months_elapsed() const;
    String get_scenario_name() const;
    double get_seconds_per_month() const;

    // ---- queries ----
    double get_value(const String& ref) const;
    String get_value_text(const String& ref) const; // exact 5-decimal text, or "ERR: ..."
    PackedStringArray get_array(const String& ref) const; // element values as text (tags / state ids / numbers)
    PackedStringArray list_countries() const;             // existing tags, load order
    PackedInt32Array list_states(const String& tag) const; // controlled states of a tag ("" = all)
    Dictionary get_country_info(const String& tag) const;
    Dictionary get_state_info(int state_id) const;
    String get_country_name(const String& tag) const; // localised ("Britain"); the tag if none
    // Every variable/array of a scope ("ENG", "952", "global"), filtered by substring.
    // Returns { "vars": { name: float }, "arrays": { name: size } }.
    Dictionary get_scope_vars(const String& scope, const String& filter) const;
    // modifier@name breakdown: [{ source, value }]
    Array get_modifier_breakdown(const String& scope, const String& modifier) const;

    // ---- actions ----
    PackedStringArray run_effect(const String& scope, const String& script_text); // log lines; "ERROR: ..." on failure

    // ---- watched series (sampled after every simulated month) ----
    void add_watch(const String& ref);
    void remove_watch(const String& ref);
    PackedStringArray get_watches() const;
    PackedFloat64Array get_series(const String& ref) const;
    PackedStringArray get_series_dates() const;

    // ---- diagnostics / output ----
    Array get_diagnostics(int min_severity) const; // [{kind, severity, file, line, count, message}]
    PackedStringArray get_log(int since) const;
    int get_log_size() const;
    Array get_profile(int top) const; // [{name, calls, seconds}]

    // ---- state map (provinces.bmp -> loaded states) ----
    String build_state_map(int downscale);   // "" on success
    Ref<Image> get_state_map_image() const;  // RGB8, pixel = dense state index (r + g<<8 + b<<16)
    Vector2i get_state_map_size() const;
    PackedInt32Array get_state_map_ids() const; // index -> state id (index 0 = none)
    int get_state_at_pixel(int x, int y) const;
    Vector2 get_state_centroid(int state_id) const;

protected:
    static void _bind_methods();

private:
    void join_worker();
    void sample_watches();

    std::unique_ptr<sim::Simulation> sim_;
    std::unique_ptr<sim::StateMap> map_;
    std::thread worker_;
    std::atomic<bool> busy_{false};
    std::atomic<bool> finished_{false};
    std::atomic<bool> cancel_{false};
    std::vector<std::string> watches_;
    std::vector<std::vector<double>> series_;
    std::vector<std::string> series_dates_;
};

} // namespace godot
