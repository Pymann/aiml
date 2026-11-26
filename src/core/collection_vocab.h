#pragma once
// spec/71: the collection vocabulary — one name, one meaning, one spelling.
// This header is the single place that knows which names spec/71 §8 retired and what replaces
// them, so that every call path (method calls on locals, on fields, namespace functions,
// properties) gives the same diagnostic.
#include <string>

namespace aiml::vocab {

inline bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

inline bool isList(const std::string& c)     { return startsWith(c, "list<"); }
inline bool isMap(const std::string& c)      { return startsWith(c, "map<"); }
inline bool isHashmap(const std::string& c)  { return startsWith(c, "hashmap<"); }
inline bool isTypedBuffer(const std::string& c) { return startsWith(c, "buffer<"); }
inline bool isRawBuffer(const std::string& c)   { return c == "buffer"; }
inline bool isAnyBuffer(const std::string& c)   { return isTypedBuffer(c) || isRawBuffer(c); }
inline bool isKv(const std::string& c)       { return startsWith(c, "Kv<"); }
inline bool isStore(const std::string& c)    { return startsWith(c, "Store<"); }
inline bool isVecStore(const std::string& c) { return c == "VecStore"; }
inline bool isTimeSeries(const std::string& c) { return startsWith(c, "TimeSeries<"); }
inline bool isTensor(const std::string& c)   { return c == "tensor"; }
inline bool isCode(const std::string& c)     { return c == "Code"; }
inline bool isGpuObject(const std::string& c) {
    return c == "gl_buffer" || c == "gl_texture" || c == "gl_cubemap" || c == "gl_vao" ||
           c == "gl_timer_query";
}
inline bool isArray(const std::string& c)    { return startsWith(c, "array<"); }
inline bool isDynArray(const std::string& c) { return startsWith(c, "dyn_array<"); }

// Types whose queries (.len/.empty/.byte_len/.capacity) are properties (spec/71 §1.1, §2).
inline bool hasQueryProperties(const std::string& c) {
    return isList(c) || isMap(c) || isHashmap(c) || isAnyBuffer(c) || isKv(c) || isStore(c) ||
           isVecStore(c) || isTimeSeries(c) || isTensor(c) || isCode(c) || isArray(c) ||
           isDynArray(c) || c == "string";
}

inline bool isQueryName(const std::string& m) {
    return m == "len" || m == "empty" || m == "byte_len" || m == "capacity";
}

// spec/71 §8.  Returns the replacement for `<cls>.<method>` called as a METHOD, or "" if the
// name is not retired on that type.  `cls` is the receiver's type name (ptr<>/ref<> stripped).
// cascade<T> is outside spec/71 and never matches.
inline std::string retiredMethod(const std::string& cls, const std::string& m) {
    if (startsWith(cls, "cascade<")) return "";
    if (hasQueryProperties(cls) && isQueryName(m))
        return "the property `." + m + "` (spec/71 §2: queries are properties)";
    const bool seq = isList(cls) || isTypedBuffer(cls) || isTensor(cls);
    if ((m == "size" || m == "length") &&
        (isList(cls) || isHashmap(cls) || isAnyBuffer(cls) || isTensor(cls) || isCode(cls)))
        return "the property `.len`";
    if (m == "count" && (isList(cls) || isTypedBuffer(cls)))
        return "the property `.len` for the element count, or `count_of(v)` for the occurrences of v";
    if (m == "get" && seq)
        return "`{ at: [<seq>, <index>] }`";
    if ((m == "set" || m == "put" || m == "replace") && (isList(cls) || isTensor(cls)))
        return "`idx: [<seq>, <index>, <value>]`";
    if (isMap(cls) || isHashmap(cls)) {
        if (m == "put")      return "`set(k, v)`";
        if (m == "contains") return "`has(k)`";
        if (m == "remove")   return "`delete(k)`";
    }
    if (isHashmap(cls) && m == "add") return "`incr(k, d)`";
    if (isAnyBuffer(cls) && m == "append") return "`extend(other)`";
    if (isStore(cls) && m == "insert") return "`add(r)`";
    if (isVecStore(cls) && m == "insert") return "`set(k, vec)`";
    if (isTimeSeries(cls) && m == "at") return "`at_time(t)`";
    if (isGpuObject(cls) && m == "delete") return "`free()`";
    return "";
}

// spec/71 §8 for properties: `b.count` / `t.size` read as a property.
inline std::string retiredProperty(const std::string& cls, const std::string& p) {
    if (startsWith(cls, "cascade<")) return "";
    if ((p == "size" || p == "length") &&
        (isList(cls) || isHashmap(cls) || isAnyBuffer(cls) || isTensor(cls) || isCode(cls)))
        return "the property `.len`";
    if (p == "count" && (isList(cls) || isTypedBuffer(cls)))
        return "the property `.len`";
    return "";
}

// spec/71 §8 for namespace functions.
inline std::string retiredFunction(const std::string& ns, const std::string& fn) {
    if (ns == "storage" && fn == "remove") return "`storage.delete(k)`";
    if (ns == "storage" && fn == "session_remove") return "`storage.session_delete(k)`";
    if (ns == "yaml" && fn == "size") return "`yaml.len(node)`";
    return "";
}

inline std::string retiredMessage(const std::string& what, const std::string& replacement) {
    return "`" + what + "` is retired (spec/71 §8) — use " + replacement;
}

} // namespace aiml::vocab
