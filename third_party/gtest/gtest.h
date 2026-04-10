#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace testing {

using TestFn = std::function<void()>;

struct TestCaseEntry {
    std::string name;
    TestFn fn;
};

inline std::vector<TestCaseEntry>& registry() {
    static std::vector<TestCaseEntry> r;
    return r;
}

inline int& failureCount() {
    static int c = 0;
    return c;
}

inline void InitGoogleTest(int*, char**) {}

inline int RUN_ALL_TESTS() {
    for (const auto& t : registry()) {
        try {
            t.fn();
        } catch (...) {
            std::cerr << "[  FAILED  ] " << t.name << " (exception)\n";
            ++failureCount();
        }
    }
    if (failureCount() == 0) {
        std::cout << "[  PASSED  ] " << registry().size() << " tests\n";
    }
    return failureCount() == 0 ? 0 : 1;
}

struct Registrar {
    Registrar(const std::string& name, TestFn fn) {
        registry().push_back({name, std::move(fn)});
    }
};

} // namespace testing

#define TEST(SUITE, NAME) \
    static void SUITE##_##NAME##_Test(); \
    static testing::Registrar SUITE##_##NAME##_registrar(#SUITE "." #NAME, SUITE##_##NAME##_Test); \
    static void SUITE##_##NAME##_Test()

#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "EXPECT_TRUE failed: " #cond "\n"; ++testing::failureCount(); } } while(0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a,b) do { auto _a=(a); auto _b=(b); if (!(_a==_b)) { std::cerr << "EXPECT_EQ failed: " #a " != " #b "\n"; ++testing::failureCount(); } } while(0)
#define ASSERT_TRUE(cond) do { if (!(cond)) { std::cerr << "ASSERT_TRUE failed: " #cond "\n"; ++testing::failureCount(); return; } } while(0)
#define ASSERT_EQ(a,b) do { auto _a=(a); auto _b=(b); if (!(_a==_b)) { std::cerr << "ASSERT_EQ failed: " #a " != " #b "\n"; ++testing::failureCount(); return; } } while(0)
