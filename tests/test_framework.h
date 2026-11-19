// test_framework.h - a deliberately tiny unit-test harness, so the project needs no downloads.
//
//   TEST_CASE(name) { ... }      defines and registers a test
//   CHECK(condition)             records a failure if condition is false
//   CHECK_EQ(actual, expected)   records a failure and prints both values if they differ
//   CHECK_THROWS(statement)      records a failure if statement does not throw
#pragma once

#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

namespace minitest {

struct TestCase {
    const char* name;
    void (*body)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Register {
    Register(const char* name, void (*body)()) { registry().push_back({name, body}); }
};

inline void fail(const char* file, int line, const std::string& message) {
    ++failures();
    std::cerr << file << ":" << line << ": " << message << "\n";
}

// Compare integers by value even when one is signed and the other unsigned.
template <typename T>
bool is_negative(T value) {
    if constexpr (std::is_signed_v<T>) {
        return value < 0;
    } else {
        return false;
    }
}

template <typename A, typename B>
bool values_equal(const A& a, const B& b) {
    if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
        if (is_negative(a) || is_negative(b)) {
            return is_negative(a) && is_negative(b) && static_cast<long long>(a) == static_cast<long long>(b);
        }
        return static_cast<unsigned long long>(a) == static_cast<unsigned long long>(b);
    } else {
        return a == b;
    }
}

inline std::string show(const std::string& text) { return "\"" + text + "\""; }
inline std::string show(const char* text) { return show(std::string(text)); }
inline std::string show(bool value) { return value ? "true" : "false"; }
template <typename T>
std::string show(const T& value) {
    if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
        return std::to_string(static_cast<long long>(value));
    } else if constexpr (std::is_integral_v<T>) {
        return std::to_string(static_cast<unsigned long long>(value));
    } else if constexpr (std::is_floating_point_v<T>) {
        return std::to_string(value);
    } else {
        return "(value not printable)";
    }
}

inline int run_all() {
    int failed_tests = 0;
    for (const TestCase& test : registry()) {
        const int before = failures();
        try {
            test.body();
        } catch (const std::exception& e) {
            fail(test.name, 0, std::string("unexpected exception: ") + e.what());
        }
        const bool passed = failures() == before;
        std::cout << (passed ? "[PASS] " : "[FAIL] ") << test.name << "\n";
        if (!passed) ++failed_tests;
    }
    std::cout << (registry().size() - static_cast<std::size_t>(failed_tests)) << " of " << registry().size()
              << " tests passed\n";
    return failed_tests == 0 ? 0 : 1;
}

}  // namespace minitest

#define TEST_CASE(name)                                                     \
    static void name();                                                     \
    static const ::minitest::Register name##_registration(#name, &name);    \
    static void name()

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) ::minitest::fail(__FILE__, __LINE__, "CHECK(" #condition ") failed"); \
    } while (false)

#define CHECK_EQ(actual, expected)                                                                       \
    do {                                                                                                 \
        const auto& check_actual_ = (actual);                                                            \
        const auto& check_expected_ = (expected);                                                        \
        if (!::minitest::values_equal(check_actual_, check_expected_)) {                                 \
            ::minitest::fail(__FILE__, __LINE__,                                                         \
                             "CHECK_EQ(" #actual ", " #expected ") failed\n    actual:   " +             \
                                 ::minitest::show(check_actual_) +                                       \
                                 "\n    expected: " + ::minitest::show(check_expected_));                \
        }                                                                                                \
    } while (false)

#define CHECK_THROWS(statement)                                                                      \
    do {                                                                                             \
        bool threw_ = false;                                                                         \
        try {                                                                                        \
            statement;                                                                               \
        } catch (...) {                                                                              \
            threw_ = true;                                                                           \
        }                                                                                            \
        if (!threw_) ::minitest::fail(__FILE__, __LINE__, "CHECK_THROWS(" #statement ") did not throw"); \
    } while (false)
