// Multiplayer Editor — made by x7wv
#pragma once
#include <Geode/Geode.hpp>
#include <matjson.hpp>
#include <algorithm>
#include <chrono>
#include <random>

namespace me {
using namespace geode::prelude;
// ---------------------------------------------------------------- json helpers
inline std::string jstr(matjson::Value const& v, std::string_view k, std::string def = "") {
    if (!v.isObject() || !v.contains(k)) return def;
    auto r = v[k].asString();
    return r ? r.unwrap() : def;
}
inline double jnum(matjson::Value const& v, std::string_view k, double def = 0) {
    if (!v.isObject() || !v.contains(k)) return def;
    auto r = v[k].asDouble();
    return r ? r.unwrap() : def;
}
inline int jint(matjson::Value const& v, std::string_view k, int def = 0) {
    return static_cast<int>(jnum(v, k, def));
}
inline bool jbool(matjson::Value const& v, std::string_view k, bool def = false) {
    if (!v.isObject() || !v.contains(k)) return def;
    auto r = v[k].asBool();
    return r ? r.unwrap() : def;
}
inline bool jarr(matjson::Value const& v, std::string_view k) {
    return v.isObject() && v.contains(k) && v[k].isArray();
}
inline ccColor3B jcolor(matjson::Value const& v, std::string_view k, ccColor3B def = {0, 200, 255}) {
    if (!jarr(v, k)) return def;
    auto const& a = v[k];
    auto get = [&](size_t i) -> GLubyte {
        auto r = a[i].asDouble();
        return r ? static_cast<GLubyte>(std::clamp(r.unwrap(), 0.0, 255.0)) : 0;
    };
    if (a.size() < 3) return def;
    return {get(0), get(1), get(2)};
}
inline matjson::Value colorJson(ccColor3B c) {
    auto a = matjson::Value::array();
    a.push(static_cast<int>(c.r)); a.push(static_cast<int>(c.g)); a.push(static_cast<int>(c.b));
    return a;
}
inline double nowMs() {
    using namespace std::chrono;
    return duration_cast<duration<double, std::milli>>(steady_clock::now().time_since_epoch()).count();
}
inline std::string randomStr(size_t n) {
    static char const* a = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<int> d(0, 35);
    std::string s;
    for (size_t i = 0; i < n; i++) s += a[d(gen)];
    return s;
}
inline std::string base36(uint64_t v) {
    static char const* a = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string s;
    do { s.insert(s.begin(), a[v % 36]); v /= 36; } while (v);
    return s;
}


inline double wallMs() {
    using namespace std::chrono;
    return static_cast<double>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}
}
