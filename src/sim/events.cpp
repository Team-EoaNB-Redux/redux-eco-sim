#include "sim/events.h"

#include "builtins/common.h"

#include <algorithm>

namespace sim {

using pdx::Fixed;
using pdx::Node;
using pdx::ScopeRef;
using pdx::Sym;

void Events::load(const pdx::ScriptFile& f) {
    for (const Node* n = f.root_begin(); n != f.root_end(); ++n) {
        const std::string_view k = pdx::sym_view(n->key);
        if (!n->is_block || (k != "country_event" && k != "state_event" && k != "news_event")) continue;
        const Node* id = pdx::bi::find_child(f, *n, "id");
        if (!id) continue;
        EventDef e;
        e.file = &f;
        e.node = n;
        events_[id->value] = std::move(e);
    }
}

void Events::install_hooks() {
    in_.hooks.fire_event = [this](pdx::Ctx& c, Sym id, int64_t days, pdx::SrcLoc loc) {
        fire(c.self(), c.root, id, days, loc);
    };
}

void Events::compile(EventDef& e) {
    const pdx::ScriptFile& f = *e.file;
    for (const Node* c = f.begin(*e.node); c != f.end(*e.node); ++c) {
        const std::string_view k = pdx::sym_view(c->key);
        if (k == "trigger" && c->is_block) {
            e.has_trigger = true;
            e.trigger = in_.compile_triggers(f, *c);
        } else if (k == "immediate" && c->is_block) {
            e.immediate = in_.compile_effects(f, *c);
        } else if (k == "option" && c->is_block) {
            Option o;
            for (const Node* x = f.begin(*c); x != f.end(*c); ++x) {
                const std::string_view xk = pdx::sym_view(x->key);
                if (xk == "ai_chance" && x->is_block) {
                    for (const Node* a = f.begin(*x); a != f.end(*x); ++a) {
                        const std::string_view ak = pdx::sym_view(a->key);
                        if (ak == "base") Fixed::parse(pdx::sym_view(a->value), o.base);
                        else if (ak == "factor") Fixed::parse(pdx::sym_view(a->value), o.factor);
                        else if (ak == "modifier" && a->is_block) {
                            Option::Mod m;
                            if (const Node* fa = pdx::bi::find_child(f, *a, "factor")) Fixed::parse(pdx::sym_view(fa->value), m.factor);
                            if (const Node* ad = pdx::bi::find_child(f, *a, "add")) {
                                m.has_add = Fixed::parse(pdx::sym_view(ad->value), m.add);
                            }
                            m.cond = pdx::bi::compile_triggers_except(in_, f, *a, {"factor", "add", "base"});
                            o.mods.push_back(std::move(m));
                        }
                    }
                } else if (xk == "trigger" && x->is_block) {
                    o.has_trigger = true;
                    o.trigger = in_.compile_triggers(f, *x);
                }
            }
            o.effects = pdx::bi::compile_effects_except(in_, f, *c, {"name", "ai_chance", "trigger", "original_recipient_only"});
            e.options.push_back(std::move(o));
        }
    }
    e.compiled = true;
}

void Events::fire(ScopeRef target, ScopeRef from, Sym id, int64_t days, pdx::SrcLoc loc) {
    if (!events_.count(id)) {
        in_.diag(DiagKind::EventNotLoaded, loc, "event " + pdx::sym_str(id) + " is not in the scenario's event whitelist");
        return;
    }
    if (days > 0) {
        pending_.push_back({in_.world().date.ordinal() + days, target, from, id, loc});
        return;
    }
    execute(events_[id], id, target, from);
}

void Events::tick(const world::Date& today) {
    const int64_t now = today.ordinal();
    std::vector<Pending> due;
    auto it = std::stable_partition(pending_.begin(), pending_.end(), [&](const Pending& p) { return p.due > now; });
    due.assign(it, pending_.end());
    pending_.erase(it, pending_.end());
    for (const auto& p : due) {
        if (events_.count(p.id)) execute(events_[p.id], p.id, p.target, p.from);
    }
}

void Events::execute(EventDef& e, Sym id, ScopeRef target, ScopeRef from) {
    if (!e.compiled) compile(e);
    world::VarStore temps;
    pdx::Ctx c(in_, in_.world());
    c.scopes.push_back(target);
    c.root = target;
    c.from = from;
    c.temps = &temps;
    if (e.has_trigger && !in_.eval_and(c, e.trigger)) return;
    in_.run_block(c, e.immediate);

    FiredRecord rec;
    rec.date = in_.world().date.to_string();
    rec.id = pdx::sym_str(id);
    rec.target = in_.world().scope_name(target);

    std::vector<int64_t> weights;
    int64_t total = 0;
    const world::Country* co = target.is_country() ? in_.world().country(target.id) : nullptr;
    const bool first_only = player_first_option_ && co && !co->is_ai;
    for (const auto& o : e.options) {
        if (o.has_trigger && !in_.eval_and(c, o.trigger)) {
            weights.push_back(0);
            continue;
        }
        Fixed w = in_.arith(c, '*', o.base, o.factor, pdx::SrcLoc{});
        for (const auto& m : o.mods) {
            if (!in_.eval_and(c, m.cond)) continue;
            if (m.has_add) w = in_.arith(c, '+', w, m.add, pdx::SrcLoc{});
            w = in_.arith(c, '*', w, m.factor, pdx::SrcLoc{});
        }
        weights.push_back(w.raw() > 0 ? w.raw() : 0);
        total += weights.back();
    }
    int chosen = -1;
    if (first_only) {
        for (size_t i = 0; i < weights.size(); ++i) {
            if (!e.options[i].has_trigger || weights[i] > 0) {
                chosen = static_cast<int>(i);
                break;
            }
        }
    } else if (total > 0) {
        int64_t pick = static_cast<int64_t>(in_.world().rng.below(static_cast<uint64_t>(total)));
        for (size_t i = 0; i < weights.size(); ++i) {
            if (pick < weights[i]) {
                chosen = static_cast<int>(i);
                break;
            }
            pick -= weights[i];
        }
    } else if (!e.options.empty()) {
        chosen = 0;
    }
    if (chosen >= 0) in_.run_block(c, e.options[static_cast<size_t>(chosen)].effects);
    rec.option = chosen;
    history_.push_back(std::move(rec));
}

} // namespace sim
