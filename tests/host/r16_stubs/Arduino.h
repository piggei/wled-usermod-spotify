#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <sstream>
#include <cassert>
#define F(x) x
#define FPSTR(x) x
#define CONTENT_TYPE_PLAIN "text/plain"
extern int hostCriticalDepth;
class String {
 public:
  String() { assert(hostCriticalDepth == 0); }
  String(const char* v): v_(v ? v : "") { assert(hostCriticalDepth == 0); }
  String(const std::string& v): v_(v) { assert(hostCriticalDepth == 0); }
  template<typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
  String(T v): v_(std::to_string(v)) { assert(hostCriticalDepth == 0); }
  const char* c_str() const { return v_.c_str(); }
  size_t length() const { return v_.size(); }
  void reserve(size_t n) { assert(hostCriticalDepth == 0); v_.reserve(n); }
  template<class T> String& operator+=(const T& v) { v_ += String(v).v_; return *this; }
  bool operator==(const char* v) const { return v_ == v; }
  bool operator!=(const char* v) const { return v_ != v; }
 private:
  std::string v_;
};
template<class T> String operator+(String left,const T& right) { left += right; return left; }
struct EspStub { uint32_t getFreeHeap() const { return 123456u; } uint32_t getMinFreeHeap() const { return 100000u; } };
extern EspStub ESP;
uint32_t millis();
uint32_t micros();
