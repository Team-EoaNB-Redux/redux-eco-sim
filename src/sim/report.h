#pragma once

// Run outputs for the AI workflow:
//   series.csv        month x watch values
//   report.md         the first thing to read: summary, watch table, diagnostics (eco files first), profile,
//                     stub/approx builtins used, known approximations
//   diagnostics.json  every diagnostic (machine-readable)
//   sim.log           `log` effect output, game.log style

#include "sim/simulation.h"

#include <string>
#include <vector>

namespace sim {

class Recorder {
public:
    explicit Recorder(Simulation& s) : sim_(s) {}
    void add_watch(const std::string& ref) { watches_.push_back(ref); }
    // Reads `watches/<name>.txt`-style files: one reference per line, '#' comments.
    bool add_watch_file(const std::string& path, std::string& error);
    void sample(); // record one row at the current date
    const std::vector<std::string>& watches() const { return watches_; }
    const std::vector<std::string>& dates() const { return dates_; }
    const std::vector<std::vector<double>>& rows() const { return rows_; }

private:
    Simulation& sim_;
    std::vector<std::string> watches_;
    std::vector<std::string> dates_;
    std::vector<std::vector<double>> rows_;
    std::vector<std::vector<std::string>> exact_; // exact 5-decimal text for the CSV
    friend bool write_run_outputs(const std::string&, Simulation&, const Recorder&, int, std::string&);
};

// Writes all run outputs into `dir` (created if missing). `months` = months simulated.
bool write_run_outputs(const std::string& dir, Simulation& s, const Recorder& r, int months, std::string& error);

// Compares two run directories (series.csv). Returns a markdown table.
bool compare_runs(const std::string& dir_a, const std::string& dir_b, std::string& out, std::string& error);

} // namespace sim
