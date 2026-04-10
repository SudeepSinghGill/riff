/**
 * Minimal JSI stub for unit tests.
 *
 * LayoutCache.cpp and ListLayout.cpp call JSI methods only inside
 * installJSIBindings() and its helpers (attrsFromJSI, paramsFromJSI, etc.).
 * Unit tests never call those methods. We still need to compile them, so
 * all JSI types and methods are stubbed with no-op implementations.
 *
 * Nothing in this header is ever executed during tests.
 */
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <stdexcept>
#include <cstddef>

namespace facebook {
namespace jsi {

class Runtime;
class String;
class Object;
class Array;
class Function;
class Value;
class PropNameID;

// ── PropNameID ────────────────────────────────────────────────────────────────

class PropNameID {
public:
  static PropNameID forAscii(Runtime&, const char*, size_t) { return {}; }
  static PropNameID forAscii(Runtime& rt, const std::string& s) {
    return forAscii(rt, s.c_str(), s.size());
  }
  static PropNameID forUtf8(Runtime& rt, const uint8_t* utf8, size_t len) {
    return forAscii(rt, reinterpret_cast<const char*>(utf8), len);
  }
  std::string utf8(Runtime&) const { return {}; }
};

// ── String ────────────────────────────────────────────────────────────────────

class String {
public:
  static String createFromAscii(Runtime&, const char*, size_t) { return {}; }
  static String createFromAscii(Runtime& rt, const std::string& s) {
    return createFromAscii(rt, s.c_str(), s.size());
  }
  static String createFromUtf8(Runtime& rt, const std::string& s) {
    return createFromAscii(rt, s.c_str(), s.size());
  }
  static String createFromUtf8(Runtime& rt, const uint8_t* utf8, size_t len) {
    return createFromAscii(rt, reinterpret_cast<const char*>(utf8), len);
  }
  std::string utf8(Runtime&) const { return {}; }
};

// ── Forward-declare Array and Function before Value/Object ───────────────────

class Array;
class Function;

// ── Value ─────────────────────────────────────────────────────────────────────

class Value {
public:
  // Default / scalar constructors
  Value() = default;
  explicit Value(bool)   {}
  explicit Value(double) {}
  explicit Value(int)    {}
  Value(Value&&) noexcept {}
  Value(const Value&) {}
  Value& operator=(Value&&) noexcept { return *this; }
  Value& operator=(const Value&) { return *this; }

  // Object-wrapping constructor (used by attrsToJSI: Value(rt, frame))
  Value(Runtime&, Object&&) {}
  Value(Runtime&, const Object&) {}
  Value(Runtime&, Array&&) {}
  Value(Runtime&, const Array&) {}
  Value(Runtime&, String&&) {}
  Value(Runtime&, const String&) {}

  // Static factories
  static Value undefined() { return {}; }
  static Value null()      { return {}; }
  static Value from(Runtime&, bool b)   { return Value(b); }
  static Value from(Runtime&, double d) { return Value(d); }

  // Type predicates
  bool isUndefined() const { return true; }
  bool isNull()      const { return false; }
  bool isBool()      const { return false; }
  bool isNumber()    const { return false; }
  bool isString()    const { return false; }
  bool isObject()    const { return false; }
  bool isSymbol()    const { return false; }
  bool isBigInt()    const { return false; }

  // Extractors
  bool   asBool()   const { return false; }
  double asNumber() const { return 0.0; }
  bool   getBool()  const { return false; }
  double getNumber()const { return 0.0; }
  String getString(Runtime&) const { return {}; }
  Object getObject(Runtime&) const;  // defined after Object
};

// ── Array ─────────────────────────────────────────────────────────────────────

class Array {
public:
  Array() = default;
  Array(Runtime&, size_t) {}  // Array(rt, n) constructor used in LayoutCache
  size_t size(Runtime&) const { return 0; }
  Value  getValueAtIndex(Runtime&, size_t) const { return {}; }
  void   setValueAtIndex(Runtime&, size_t, Value) {}
  void   setValueAtIndex(Runtime&, size_t, const Object&) {}
  static Array createWithLength(Runtime&, size_t) { return {}; }
};

// ── Object ────────────────────────────────────────────────────────────────────

class Object {
public:
  Object() = default;
  explicit Object(Runtime&) {}  // Object obj(rt) pattern used in attrsToJSI

  Value getProperty(Runtime&, const char*)       const { return {}; }
  Value getProperty(Runtime&, const PropNameID&) const { return {}; }

  void setProperty(Runtime&, const char*,       Value) {}
  void setProperty(Runtime&, const PropNameID&, Value) {}
  void setProperty(Runtime&, const char*,       double) {}
  void setProperty(Runtime&, const char*,       bool) {}
  void setProperty(Runtime&, const char*,       int) {}
  void setProperty(Runtime&, const char*,       Object) {}
  void setProperty(Runtime&, const char*,       String) {}
  void setProperty(Runtime&, const char*,       Array) {}

  bool  isArray(Runtime&)    const { return false; }
  bool  isFunction(Runtime&) const { return false; }

  Array    asArray(Runtime&)    const { return {}; }
  Array    getArray(Runtime&)   const { return {}; }
  Function asFunction(Runtime&) const;  // defined after Function

  Array getPropertyNames(Runtime&) const { return {}; }

  template<typename T>
  T getHostObject() const { throw std::runtime_error("stub"); }
};

// Define Value::getObject after Object is complete.
inline Object Value::getObject(Runtime&) const { return {}; }

// ── Function ──────────────────────────────────────────────────────────────────

using HostFunctionType = Value(Runtime&, const Value& thisVal,
                               const Value* args, size_t count);

class Function : public Object {
public:
  static Function createFromHostFunction(Runtime&, const PropNameID&,
                                          unsigned int,
                                          std::function<HostFunctionType>) {
    return {};
  }
  Value call(Runtime&) const { return {}; }
  Value call(Runtime&, const Value*, size_t) const { return {}; }
};

inline Function Object::asFunction(Runtime&) const { return {}; }

// ── Runtime ───────────────────────────────────────────────────────────────────

class Runtime {
public:
  Object global() { return {}; }

  // Enough of the property access API to satisfy LayoutCache installJSIBindings.
  void setProperty(Object&, const PropNameID&, Value) {}
  Value getProperty(const Object&, const char*) const { return {}; }
};

} // namespace jsi
} // namespace facebook
