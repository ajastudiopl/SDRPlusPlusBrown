#pragma once
// Minimal self-contained test framework (no external dependency).
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace satest {
    struct TestCase {
        const char* name;
        std::function<void()> fn;
    };

    inline std::vector<TestCase>& registry() {
        static std::vector<TestCase> r;
        return r;
    }

    struct Registrar {
        Registrar(const char* name, std::function<void()> fn) { registry().push_back({ name, fn }); }
    };

    struct Failure {
        std::string msg;
    };

    inline int& checks() {
        static int c = 0;
        return c;
    }
}

#define SA_CAT2(a, b) a##b
#define SA_CAT(a, b) SA_CAT2(a, b)
#define TEST(name)                                                         \
    static void name();                                                    \
    static satest::Registrar SA_CAT(reg_, name)(#name, name);              \
    static void name()

#define CHECK(cond)                                                                                         \
    do {                                                                                                    \
        satest::checks()++;                                                                                 \
        if (!(cond)) {                                                                                      \
            char _b[512];                                                                                   \
            snprintf(_b, sizeof(_b), "%s:%d: CHECK(%s) failed", __FILE__, __LINE__, #cond);                \
            throw satest::Failure{ _b };                                                                    \
        }                                                                                                   \
    } while (0)

#define CHECK_MSG(cond, ...)                                                                                \
    do {                                                                                                    \
        satest::checks()++;                                                                                 \
        if (!(cond)) {                                                                                      \
            char _m[384];                                                                                   \
            snprintf(_m, sizeof(_m), __VA_ARGS__);                                                          \
            char _b[512];                                                                                   \
            snprintf(_b, sizeof(_b), "%s:%d: CHECK(%s) failed: %s", __FILE__, __LINE__, #cond, _m);        \
            throw satest::Failure{ _b };                                                                    \
        }                                                                                                   \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                               \
    do {                                                                                                    \
        satest::checks()++;                                                                                 \
        double _a = (double)(a), _bb = (double)(b);                                                         \
        if (!(std::fabs(_a - _bb) <= (double)(tol))) {                                                      \
            char _b[512];                                                                                   \
            snprintf(_b, sizeof(_b), "%s:%d: CHECK_NEAR(%s, %s, %s) failed: %.9g vs %.9g", __FILE__, __LINE__, \
                     #a, #b, #tol, _a, _bb);                                                                \
            throw satest::Failure{ _b };                                                                    \
        }                                                                                                   \
    } while (0)
