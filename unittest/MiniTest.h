/*
 *  Minimal dependency-free test harness for ESP32DigitalSynth.
 *  gtest/libsndfile are not required so the tests build anywhere (the
 *  platform-independence goal). Usage:
 *
 *    TEST(name) { CHECK(cond); CHECK_NEAR(a, b, eps); }
 *    int main(){ return mini::runAll(); }
 */
#pragma once
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <functional>

namespace mini
{
    struct Case { std::string name; std::function<void(bool &)> fn; };
    inline std::vector<Case> &cases() { static std::vector<Case> c; return c; }
    struct Reg { Reg(const std::string &n, std::function<void(bool &)> f) { cases().push_back({n, f}); } };

    inline int runAll()
    {
        int failed = 0;
        for (auto &c : cases())
        {
            bool ok = true;
            c.fn(ok);
            printf("[%s] %s\n", ok ? "PASS" : "FAIL", c.name.c_str());
            if (!ok) ++failed;
        }
        printf("\n%zu tests, %d failed\n", cases().size(), failed);
        return failed == 0 ? 0 : 1;
    }
}

#define TEST(NAME)                                                       \
    static void NAME(bool &_ok);                                         \
    static mini::Reg _reg_##NAME(#NAME, NAME);                           \
    static void NAME(bool &_ok)

#define CHECK(COND)                                                      \
    do {                                                                 \
        if (!(COND)) { _ok = false;                                      \
            printf("    CHECK failed: %s (%s:%d)\n", #COND, __FILE__, __LINE__); } \
    } while (0)

#define CHECK_NEAR(A, B, EPS)                                            \
    do {                                                                 \
        double _a = (A), _b = (B);                                       \
        if (std::fabs(_a - _b) > (EPS)) { _ok = false;                   \
            printf("    CHECK_NEAR failed: %s=%g vs %s=%g (%s:%d)\n",     \
                   #A, _a, #B, _b, __FILE__, __LINE__); }                \
    } while (0)
