#include "godot/eco_sim.h"

#include "loader/mod_fs.h"
#include "pdx/fixed.h"
#include "sim/simulation.h"
#include "sim/state_map.h"
#include "sim/version.h"

#include <algorithm>
#include <cmath>

namespace godot {

namespace {

std::string s(const String& g) { return std::string(g.utf8().get_data()); }
String g(const std::string& v) { return String::utf8(v.c_str()); }

const char* severity_text(sim::Severity sev) {
    switch (sev) {
    case sim::Severity::Error: return "error";
    case sim::Severity::Warning: return "warning";
    case sim::Severity::Lint: return "lint";
    default: return "info";
    }
}

} // namespace

EcoSim::EcoSim() = default;

EcoSim::~EcoSim() {
    cancel_ = true;
    join_worker();
}

void EcoSim::join_worker() {
    if (worker_.joinable()) worker_.join();
}

String EcoSim::get_version() const { return String(ecosim::kVersion); }

String EcoSim::format_fixed(const String& literal) const {
    pdx::Fixed f;
    if (!pdx::Fixed::parse(s(literal), f)) return String();
    return g(f.to_string_exact());
}

String EcoSim::find_mod_root(const String& start_dir) const {
    return g(loader::find_mod_root("", s(start_dir)));
}

String EcoSim::load_scenario(const String& mod_root, const String& scenario_path, const String& semantics) {
    if (busy_) return "busy";
    join_worker();
    sim_ = std::make_unique<sim::Simulation>();
    sim_->semantics_overrides = s(semantics);
    std::string error;
    if (!sim_->load(s(mod_root), s(scenario_path), error)) {
        sim_.reset();
        return g(error);
    }
    series_.assign(watches_.size(), {});
    series_dates_.clear();
    sim_->on_month = [this]() { sample_watches(); };
    sim_->interp().profiling = true; // the Log & Profile panel shows it
    return String();
}

void EcoSim::startup() {
    if (!sim_ || busy_) return;
    sim_->startup();
    sample_watches();
}

bool EcoSim::is_loaded() const { return sim_ != nullptr; }

void EcoSim::run_months_async(int months) {
    if (!sim_ || busy_) return;
    join_worker();
    busy_ = true;
    cancel_ = false;
    worker_ = std::thread([this, months]() {
        for (int i = 0; i < months && !cancel_; ++i) sim_->run_months(1);
        busy_ = false;
        finished_ = true;
    });
}

void EcoSim::run_days_async(int days) {
    if (!sim_ || busy_) return;
    join_worker();
    busy_ = true;
    cancel_ = false;
    worker_ = std::thread([this, days]() {
        for (int i = 0; i < days && !cancel_; ++i) sim_->step_day();
        busy_ = false;
        finished_ = true;
    });
}

void EcoSim::cancel() { cancel_ = true; }

bool EcoSim::poll_finished() {
    if (finished_.exchange(false)) {
        join_worker();
        return true;
    }
    return false;
}

String EcoSim::get_date() const {
    if (!sim_ || busy_) return String();
    return g(sim_->world().date.to_string());
}

int EcoSim::get_months_elapsed() const { return sim_ && !busy_ ? sim_->months_elapsed() : 0; }

String EcoSim::get_scenario_name() const { return sim_ ? g(sim_->scenario().name) : String(); }

double EcoSim::get_seconds_per_month() const {
    if (!sim_ || busy_ || sim_->months_elapsed() == 0) return 0.0;
    return sim_->tick_seconds / sim_->months_elapsed();
}

double EcoSim::get_value(const String& ref) const {
    if (!sim_ || busy_) return 0.0;
    pdx::Value v;
    std::string error;
    return sim_->eval(s(ref), v, error) ? v.num.to_double() : std::nan("");
}

String EcoSim::get_value_text(const String& ref) const {
    if (!sim_ || busy_) return String();
    pdx::Value v;
    std::string error;
    if (!sim_->eval(s(ref), v, error)) return g("ERR: " + error);
    return g(v.num.to_string_exact());
}

PackedStringArray EcoSim::get_array(const String& ref) const {
    PackedStringArray out;
    if (!sim_ || busy_) return out;
    pdx::VarRef vr;
    std::string error;
    if (!pdx::parse_varref(s(ref), vr, &error)) return out;
    world::VarStore temps;
    pdx::Ctx c(sim_->interp(), sim_->world());
    c.scopes.push_back(pdx::ScopeRef::global());
    c.root = pdx::ScopeRef::global();
    c.temps = &temps;
    const std::vector<pdx::Value>* a = sim_->interp().array(c, vr, false, false, pdx::SrcLoc{});
    if (!a) return out;
    for (const auto& v : *a) {
        switch (v.kind) {
        case pdx::Kind::Country: {
            const pdx::ScopeRef sc = v.as_scope();
            const world::Country* co = sim_->world().country(sc.id);
            out.push_back(co ? g(pdx::sym_str(co->tag)) : String("?"));
            break;
        }
        case pdx::Kind::State: out.push_back(String::num_int64(v.id())); break;
        case pdx::Kind::Token: out.push_back(g(pdx::sym_str(v.token_sym()))); break;
        default: out.push_back(g(v.num.to_string_fixed(3))); break;
        }
    }
    return out;
}

PackedStringArray EcoSim::list_countries() const {
    PackedStringArray out;
    if (!sim_ || busy_) return out;
    for (int32_t i : sim_->world().existing_countries()) out.push_back(g(pdx::sym_str(sim_->world().countries[static_cast<size_t>(i)].tag)));
    return out;
}

PackedInt32Array EcoSim::list_states(const String& tag) const {
    PackedInt32Array out;
    if (!sim_ || busy_) return out;
    const world::World& w = sim_->world();
    if (tag.is_empty()) {
        for (int32_t id : w.existing_states()) out.push_back(id);
        return out;
    }
    const world::Country* co = w.country(w.country_index(pdx::intern(s(tag))));
    if (co) {
        for (int32_t id : co->controlled_states) out.push_back(id);
    }
    return out;
}

Dictionary EcoSim::get_country_info(const String& tag) const {
    Dictionary d;
    if (!sim_ || busy_) return d;
    const world::World& w = sim_->world();
    const world::Country* co = w.country(w.country_index(pdx::intern(s(tag))));
    if (!co) return d;
    d["tag"] = tag;
    d["exists"] = co->exists;
    d["is_ai"] = co->is_ai;
    d["capital"] = co->capital;
    d["overlord"] = co->overlord >= 0 ? g(pdx::sym_str(w.countries[static_cast<size_t>(co->overlord)].tag)) : String();
    d["num_states"] = static_cast<int>(co->controlled_states.size());
    PackedStringArray ideas;
    for (pdx::Sym i : co->ideas) ideas.push_back(g(pdx::sym_str(i)));
    d["ideas"] = ideas;
    d["num_techs"] = static_cast<int>(co->techs.size());
    return d;
}

Dictionary EcoSim::get_state_info(int state_id) const {
    Dictionary d;
    if (!sim_ || busy_) return d;
    const world::World& w = sim_->world();
    const world::State* st = w.state(state_id);
    if (!st) return d;
    d["id"] = st->id;
    d["name"] = g(pdx::sym_str(st->name));
    d["label"] = g(w.display_name(pdx::ScopeRef::state(state_id))); // localised ("London")
    d["owner"] = st->owner >= 0 ? g(pdx::sym_str(w.countries[static_cast<size_t>(st->owner)].tag)) : String();
    d["controller"] = st->controller >= 0 ? g(pdx::sym_str(w.countries[static_cast<size_t>(st->controller)].tag)) : String();
    d["category"] = g(pdx::sym_str(st->category));
    d["population"] = st->manpower.to_double();
    d["coastal"] = st->coastal;
    return d;
}

String EcoSim::get_country_name(const String& tag) const {
    if (!sim_ || busy_) return tag;
    return g(sim_->world().localised(s(tag)));
}

Dictionary EcoSim::get_scope_vars(const String& scope, const String& filter) const {
    Dictionary out;
    if (!sim_ || busy_) return out;
    const pdx::ScopeRef sc = sim_->parse_scope(s(scope));
    world::VarStore* vs = sim_->world().vars(sc);
    if (!vs) return out;
    const std::string f = s(filter);
    auto key_text = [this](world::VarKey k) {
        std::string name = pdx::sym_str(world::key_name(k));
        const uint32_t t = world::key_target(k);
        if (t != 0) {
            const uint32_t kind = t >> 30, payload = t & ((1u << 30) - 1u);
            if (kind == 1) name += "@token:" + pdx::sym_str(payload);
            else if (kind == 2) name += "@" + pdx::sym_str(sim_->world().countries[payload].tag);
            else name += "@" + std::to_string(static_cast<int32_t>(payload << 2) >> 2);
        }
        return name;
    };
    Dictionary vars, arrays;
    for (const auto& [k, v] : vs->vars()) {
        const std::string name = key_text(k);
        if (!f.empty() && name.find(f) == std::string::npos) continue;
        vars[g(name)] = v.num.to_double();
    }
    for (const auto& [k, a] : vs->arrays()) {
        const std::string name = key_text(k);
        if (!f.empty() && name.find(f) == std::string::npos) continue;
        arrays[g(name)] = static_cast<int>(a.size());
    }
    out["vars"] = vars;
    out["arrays"] = arrays;
    return out;
}

Array EcoSim::get_modifier_breakdown(const String& scope, const String& modifier) const {
    Array out;
    if (!sim_ || busy_) return out;
    for (const auto& src : sim_->modifiers().breakdown(sim_->parse_scope(s(scope)), pdx::intern(s(modifier)))) {
        Dictionary d;
        d["source"] = g(src.source);
        d["value"] = src.value.to_double();
        out.push_back(d);
    }
    return out;
}

PackedStringArray EcoSim::run_effect(const String& scope, const String& script_text) {
    PackedStringArray out;
    if (!sim_ || busy_) {
        out.push_back("ERROR: simulation not loaded or busy");
        return out;
    }
    std::vector<std::string> lines;
    std::string error;
    if (!sim_->run_effect(sim_->parse_scope(s(scope)), s(script_text), lines, error)) {
        out.push_back(g("ERROR: " + error));
        return out;
    }
    for (const auto& l : lines) out.push_back(g(l));
    return out;
}

void EcoSim::add_watch(const String& ref) {
    const std::string r = s(ref);
    if (std::find(watches_.begin(), watches_.end(), r) != watches_.end()) return;
    watches_.push_back(r);
    series_.emplace_back(series_dates_.size(), std::nan(""));
    if (sim_ && !busy_ && !series_dates_.empty()) {
        pdx::Value v;
        std::string error;
        series_.back().back() = sim_->eval(r, v, error) ? v.num.to_double() : std::nan("");
    }
}

void EcoSim::remove_watch(const String& ref) {
    auto it = std::find(watches_.begin(), watches_.end(), s(ref));
    if (it == watches_.end()) return;
    const size_t i = static_cast<size_t>(it - watches_.begin());
    watches_.erase(it);
    if (i < series_.size()) series_.erase(series_.begin() + static_cast<std::ptrdiff_t>(i));
}

PackedStringArray EcoSim::get_watches() const {
    PackedStringArray out;
    for (const auto& w : watches_) out.push_back(g(w));
    return out;
}

void EcoSim::sample_watches() {
    if (!sim_) return;
    series_dates_.push_back(sim_->world().date.to_string());
    series_.resize(watches_.size());
    for (size_t i = 0; i < watches_.size(); ++i) {
        pdx::Value v;
        std::string error;
        series_[i].resize(series_dates_.size() - 1, std::nan(""));
        series_[i].push_back(sim_->eval(watches_[i], v, error) ? v.num.to_double() : std::nan(""));
    }
}

PackedFloat64Array EcoSim::get_series(const String& ref) const {
    PackedFloat64Array out;
    if (busy_) return out;
    auto it = std::find(watches_.begin(), watches_.end(), s(ref));
    if (it == watches_.end()) return out;
    for (double v : series_[static_cast<size_t>(it - watches_.begin())]) out.push_back(v);
    return out;
}

PackedStringArray EcoSim::get_series_dates() const {
    PackedStringArray out;
    if (busy_) return out;
    for (const auto& d : series_dates_) out.push_back(g(d));
    return out;
}

Array EcoSim::get_diagnostics(int min_severity) const {
    Array out;
    if (!sim_ || busy_) return out;
    for (const auto& d : sim_->diagnostics().all()) {
        if (static_cast<int>(d.severity) < min_severity) continue;
        Dictionary e;
        e["kind"] = String(sim::diag_kind_name(d.kind));
        e["severity"] = String(severity_text(d.severity));
        e["file"] = g(d.file);
        e["line"] = static_cast<int>(d.line);
        e["count"] = static_cast<int64_t>(d.count);
        e["message"] = g(d.message);
        out.push_back(e);
    }
    return out;
}

PackedStringArray EcoSim::get_log(int since) const {
    PackedStringArray out;
    if (!sim_ || busy_) return out;
    const auto& log = sim_->log();
    for (size_t i = static_cast<size_t>(std::max(since, 0)); i < log.size(); ++i) out.push_back(g(log[i]));
    return out;
}

int EcoSim::get_log_size() const { return sim_ && !busy_ ? static_cast<int>(sim_->log().size()) : 0; }

Array EcoSim::get_profile(int top) const {
    Array out;
    if (!sim_ || busy_) return out;
    std::vector<std::pair<pdx::Sym, pdx::Interp::CallStats>> prof(sim_->interp().profile.begin(), sim_->interp().profile.end());
    std::sort(prof.begin(), prof.end(), [](const auto& a, const auto& b) { return a.second.inclusive_seconds > b.second.inclusive_seconds; });
    for (size_t i = 0; i < prof.size() && static_cast<int>(i) < top; ++i) {
        Dictionary d;
        d["name"] = g(pdx::sym_str(prof[i].first));
        d["calls"] = static_cast<int64_t>(prof[i].second.calls);
        d["seconds"] = prof[i].second.inclusive_seconds;
        out.push_back(d);
    }
    return out;
}

String EcoSim::build_state_map(int downscale) {
    if (!sim_ || busy_) return "not loaded or busy";
    auto m = std::make_unique<sim::StateMap>();
    std::string error;
    if (!m->build(sim_->mod_root(), sim_->world(), downscale, error)) return g(error);
    map_ = std::move(m);
    return String();
}

Ref<Image> EcoSim::get_state_map_image() const {
    if (!map_) return Ref<Image>();
    PackedByteArray bytes;
    bytes.resize(static_cast<int64_t>(map_->pixel_index.size()) * 3);
    uint8_t* out = bytes.ptrw();
    for (size_t i = 0; i < map_->pixel_index.size(); ++i) {
        const uint16_t v = map_->pixel_index[i];
        out[i * 3] = static_cast<uint8_t>(v & 0xFF);
        out[i * 3 + 1] = static_cast<uint8_t>(v >> 8);
        out[i * 3 + 2] = 0;
    }
    return Image::create_from_data(map_->width, map_->height, false, Image::FORMAT_RGB8, bytes);
}

Vector2i EcoSim::get_state_map_size() const { return map_ ? Vector2i(map_->width, map_->height) : Vector2i(); }

PackedInt32Array EcoSim::get_state_map_ids() const {
    PackedInt32Array out;
    if (map_) {
        for (int32_t id : map_->index_to_state) out.push_back(id);
    }
    return out;
}

int EcoSim::get_state_at_pixel(int x, int y) const { return map_ ? map_->state_at(x, y) : -1; }

Vector2 EcoSim::get_state_centroid(int state_id) const {
    double x = 0, y = 0;
    if (map_ && map_->centroid(state_id, x, y)) return Vector2(static_cast<real_t>(x), static_cast<real_t>(y));
    return Vector2(-1, -1);
}

void EcoSim::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_version"), &EcoSim::get_version);
    ClassDB::bind_method(D_METHOD("format_fixed", "literal"), &EcoSim::format_fixed);
    ClassDB::bind_method(D_METHOD("find_mod_root", "start_dir"), &EcoSim::find_mod_root);
    ClassDB::bind_method(D_METHOD("load_scenario", "mod_root", "scenario_path", "semantics"), &EcoSim::load_scenario);
    ClassDB::bind_method(D_METHOD("startup"), &EcoSim::startup);
    ClassDB::bind_method(D_METHOD("run_months_async", "months"), &EcoSim::run_months_async);
    ClassDB::bind_method(D_METHOD("run_days_async", "days"), &EcoSim::run_days_async);
    ClassDB::bind_method(D_METHOD("cancel"), &EcoSim::cancel);
    ClassDB::bind_method(D_METHOD("is_busy"), &EcoSim::is_busy);
    ClassDB::bind_method(D_METHOD("is_loaded"), &EcoSim::is_loaded);
    ClassDB::bind_method(D_METHOD("poll_finished"), &EcoSim::poll_finished);
    ClassDB::bind_method(D_METHOD("get_date"), &EcoSim::get_date);
    ClassDB::bind_method(D_METHOD("get_months_elapsed"), &EcoSim::get_months_elapsed);
    ClassDB::bind_method(D_METHOD("get_scenario_name"), &EcoSim::get_scenario_name);
    ClassDB::bind_method(D_METHOD("get_seconds_per_month"), &EcoSim::get_seconds_per_month);
    ClassDB::bind_method(D_METHOD("get_value", "ref"), &EcoSim::get_value);
    ClassDB::bind_method(D_METHOD("get_value_text", "ref"), &EcoSim::get_value_text);
    ClassDB::bind_method(D_METHOD("get_array", "ref"), &EcoSim::get_array);
    ClassDB::bind_method(D_METHOD("list_countries"), &EcoSim::list_countries);
    ClassDB::bind_method(D_METHOD("list_states", "tag"), &EcoSim::list_states);
    ClassDB::bind_method(D_METHOD("get_country_info", "tag"), &EcoSim::get_country_info);
    ClassDB::bind_method(D_METHOD("get_state_info", "state_id"), &EcoSim::get_state_info);
    ClassDB::bind_method(D_METHOD("get_country_name", "tag"), &EcoSim::get_country_name);
    ClassDB::bind_method(D_METHOD("get_scope_vars", "scope", "filter"), &EcoSim::get_scope_vars);
    ClassDB::bind_method(D_METHOD("get_modifier_breakdown", "scope", "modifier"), &EcoSim::get_modifier_breakdown);
    ClassDB::bind_method(D_METHOD("run_effect", "scope", "script_text"), &EcoSim::run_effect);
    ClassDB::bind_method(D_METHOD("add_watch", "ref"), &EcoSim::add_watch);
    ClassDB::bind_method(D_METHOD("remove_watch", "ref"), &EcoSim::remove_watch);
    ClassDB::bind_method(D_METHOD("get_watches"), &EcoSim::get_watches);
    ClassDB::bind_method(D_METHOD("get_series", "ref"), &EcoSim::get_series);
    ClassDB::bind_method(D_METHOD("get_series_dates"), &EcoSim::get_series_dates);
    ClassDB::bind_method(D_METHOD("get_diagnostics", "min_severity"), &EcoSim::get_diagnostics);
    ClassDB::bind_method(D_METHOD("get_log", "since"), &EcoSim::get_log);
    ClassDB::bind_method(D_METHOD("get_log_size"), &EcoSim::get_log_size);
    ClassDB::bind_method(D_METHOD("get_profile", "top"), &EcoSim::get_profile);
    ClassDB::bind_method(D_METHOD("build_state_map", "downscale"), &EcoSim::build_state_map);
    ClassDB::bind_method(D_METHOD("get_state_map_image"), &EcoSim::get_state_map_image);
    ClassDB::bind_method(D_METHOD("get_state_map_size"), &EcoSim::get_state_map_size);
    ClassDB::bind_method(D_METHOD("get_state_map_ids"), &EcoSim::get_state_map_ids);
    ClassDB::bind_method(D_METHOD("get_state_at_pixel", "x", "y"), &EcoSim::get_state_at_pixel);
    ClassDB::bind_method(D_METHOD("get_state_centroid", "state_id"), &EcoSim::get_state_centroid);
}

} // namespace godot
