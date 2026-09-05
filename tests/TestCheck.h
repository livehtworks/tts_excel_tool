#pragma once

#include <cmath>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <filesystem>
#include <chrono>
#include <random>

namespace adayo::test {

inline const std::filesystem::path& IsolatedRoot() {
    static const auto root=[] {
        for(int attempt=0;attempt<32;++attempt) {
            const auto path=std::filesystem::temp_directory_path()/("adayo-native-"+
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+
                std::to_string(std::random_device{}()));
            if(std::filesystem::create_directory(path)) return path;
        }
        throw std::runtime_error("Cannot reserve an isolated test root");
    }();
    return root;
}

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline std::string Location(const char* file, int line) {
    std::ostringstream out;
    out << file << ":" << line;
    return out.str();
}

inline void Require(bool condition, const char* expression, const char* file, int line) {
    if (condition) return;
    throw TestFailure("REQUIRE failed at " + Location(file, line) + ": " + expression);
}

template <typename A, typename B>
void RequireEq(const A& actual, const B& expected, const char* actual_expr, const char* expected_expr, const char* file, int line) {
    if (actual == expected) return;
    std::ostringstream out;
    out << "REQUIRE_EQ failed at " << Location(file, line) << ": " << actual_expr << " == " << expected_expr;
    throw TestFailure(out.str());
}

template <typename A, typename B, typename E>
void RequireNear(const A& actual, const B& expected, const E& epsilon, const char* actual_expr, const char* expected_expr, const char* epsilon_expr, const char* file, int line) {
    const auto diff = std::fabs(static_cast<double>(actual) - static_cast<double>(expected));
    if (diff <= static_cast<double>(epsilon)) return;
    std::ostringstream out;
    out << "REQUIRE_NEAR failed at " << Location(file, line) << ": "
        << actual_expr << " ~= " << expected_expr << " within " << epsilon_expr
        << " (diff=" << diff << ")";
    throw TestFailure(out.str());
}

template <typename Func>
int RunTestMain(const char* test_name, Func&& func) {
    try {
        std::cout << "Isolated artifacts: " << IsolatedRoot().string() << "\n";
        std::forward<Func>(func)();
        std::cout << test_name << ": PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << test_name << ": FAIL\n" << ex.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << test_name << ": FAIL\nunknown exception\n";
        return 1;
    }
}

} // namespace adayo::test

#define REQUIRE(expr) ::adayo::test::Require(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
#define REQUIRE_EQ(actual, expected) ::adayo::test::RequireEq((actual), (expected), #actual, #expected, __FILE__, __LINE__)
#define REQUIRE_NEAR(actual, expected, epsilon) ::adayo::test::RequireNear((actual), (expected), (epsilon), #actual, #expected, #epsilon, __FILE__, __LINE__)
