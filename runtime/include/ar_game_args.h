#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "ar_sarec.h"
namespace ar {
inline bool valid_game_arg(const std::string &kv) {
    const size_t eq = kv.find('=');
    return eq != std::string::npos && eq > 0 && eq + 1 < kv.size() && kv.find(',') == std::string::npos && kv.rfind("--", 0) != 0;
}
// Ordered by key, so a setup never depends on the order its options were given in (a replay compares setups).
inline std::string canonical_setup(const std::string &setup) {
    std::vector<std::string> items;
    for (size_t s = 0; s <= setup.size() && !setup.empty();) {
        const size_t e = setup.find(',', s);
        items.push_back(setup.substr(s, e == std::string::npos ? std::string::npos : e - s));
        if (e == std::string::npos) break;
        s = e + 1;
    }
    std::stable_sort(items.begin(), items.end(), [](const std::string &a, const std::string &b) {
        return a.substr(0, a.find('=')) < b.substr(0, b.find('='));
    });
    std::string out;
    for (const auto &x : items) { if (!out.empty()) out += ','; out += x; }
    return out;
}
inline std::string setup_with_item(const std::string &setup, const std::string &item) {
    const size_t eq = item.find('=');
    if (item.empty() || eq == std::string::npos) return canonical_setup(setup);
    const std::string key = item.substr(0, eq + 1);
    for (size_t s = 0; s < setup.size();) {
        if (setup.compare(s, key.size(), key) == 0) return canonical_setup(setup);
        const size_t e = setup.find(',', s);
        if (e == std::string::npos) break;
        s = e + 1;
    }
    return canonical_setup(setup.empty() ? item : setup + "," + item);
}
inline std::string setup_with_items(const std::string &setup, const std::string &items) {
    std::string out = canonical_setup(setup);
    for (size_t s = 0; s < items.size();) {
        const size_t e = items.find(',', s);
        out = setup_with_item(out, items.substr(s, e == std::string::npos ? std::string::npos : e - s));
        if (e == std::string::npos) break;
        s = e + 1;
    }
    return out;
}
inline std::string join_game_args(const std::vector<std::string> &a) {
    std::string s;
    for (const auto &x : a) { if (!s.empty()) s += ','; s += x; }
    return canonical_setup(s);
}
inline std::string setup_too_long(const std::string &with) {
    return "setup string longer than " + std::to_string(SAREC_SETUP_MAX) + " characters" + (with.empty() ? "" : " with " + with);
}
inline bool resolve_setup(const std::string &args, const std::string &recorded, std::string *out, std::string *err,
                          bool have_recording = false) {
    if (have_recording && recorded.empty() && !args.empty()) {
        *err = "the recording was made with no setup but --game-arg gives '" + args + "'";
        return false;
    }
    if (!recorded.empty() && !args.empty() && canonical_setup(recorded) != canonical_setup(args)) {
        *err = "the recording was made with setup '" + recorded + "' but --game-arg gives '" + args + "'";
        return false;
    }
    *out = recorded.empty() ? args : recorded;
    if (out->size() > SAREC_SETUP_MAX) { *err = setup_too_long(""); return false; }
    return true;
}
}
