#pragma once

// A very small test framework: no dependencies beyond the standard library.
//
//   ARTFREQ_TEST (bypassIsTransparent)
//   {
//       CHECK (something);
//       CHECK_NEAR (measuredDb, expectedDb, 0.25);
//   }
//
// Run every test:            ArtFreqTests
// Run tests matching a name: ArtFreqTests bypass

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace test
{

struct Case
{
    const char* name;
    void (*run)();
};

inline std::vector<Case>& registry()
{
    static std::vector<Case> cases;
    return cases;
}

struct Registrar
{
    Registrar (const char* name, void (*run)()) { registry().push_back ({ name, run }); }
};

struct State
{
    int checks = 0;
    int failures = 0;
    int failuresInCurrentTest = 0;
};

inline State& state()
{
    static State s;
    return s;
}

inline void recordFailure (const char* file, int line, const std::string& message)
{
    auto& s = state();
    ++s.failures;
    ++s.failuresInCurrentTest;

    // Keep the log readable when one broken thing fails hundreds of checks.
    if (s.failuresInCurrentTest <= 12)
        std::printf ("    FAIL %s:%d  %s\n", file, line, message.c_str());
    else if (s.failuresInCurrentTest == 13)
        std::printf ("    ... further failures in this test not shown\n");
}

inline void check (bool ok, const char* file, int line, const std::string& message)
{
    ++state().checks;

    if (! ok)
        recordFailure (file, line, message);
}

template <typename A, typename B, typename T>
void checkNear (A actual, B expected, T tolerance, const char* what, const char* file, int line)
{
    const auto a = static_cast<double> (actual);
    const auto e = static_cast<double> (expected);
    const auto ok = std::isfinite (a) && std::abs (a - e) <= static_cast<double> (tolerance);

    std::ostringstream message;
    message << what << ": got " << a << ", expected " << e << " +/- " << static_cast<double> (tolerance);
    check (ok, file, line, message.str());
}

template <typename A, typename B>
void checkLess (A actual, B limit, const char* what, const char* file, int line)
{
    const auto a = static_cast<double> (actual);
    const auto l = static_cast<double> (limit);

    std::ostringstream message;
    message << what << ": got " << a << ", must be below " << l;
    check (std::isfinite (a) && a < l, file, line, message.str());
}

template <typename A, typename B>
void checkGreater (A actual, B limit, const char* what, const char* file, int line)
{
    const auto a = static_cast<double> (actual);
    const auto l = static_cast<double> (limit);

    std::ostringstream message;
    message << what << ": got " << a << ", must be above " << l;
    check (std::isfinite (a) && a > l, file, line, message.str());
}

/** Runs every registered test whose name contains the filter. Returns the process exit code. */
inline int runAll (int argc, char* argv[])
{
    const std::string filter = argc > 1 ? argv[1] : "";
    int ran = 0, failedTests = 0;

    for (const auto& testCase : registry())
    {
        if (! filter.empty() && std::string (testCase.name).find (filter) == std::string::npos)
            continue;

        state().failuresInCurrentTest = 0;
        std::printf ("[ RUN  ] %s\n", testCase.name);
        std::fflush (stdout);

        testCase.run();
        ++ran;

        if (state().failuresInCurrentTest > 0)
        {
            ++failedTests;
            std::printf ("[ FAIL ] %s (%d failed checks)\n", testCase.name, state().failuresInCurrentTest);
        }
        else
        {
            std::printf ("[  OK  ] %s\n", testCase.name);
        }
    }

    std::printf ("\n%d tests, %d checks, %d failed tests\n", ran, state().checks, failedTests);

    if (ran == 0)
    {
        std::printf ("No tests matched.\n");
        return 2;
    }

    return failedTests == 0 ? 0 : 1;
}

} // namespace test

#define ARTFREQ_TEST(name)                                              \
    static void artfreqTest_##name();                                   \
    static const test::Registrar artfreqRegistrar_##name (#name, artfreqTest_##name); \
    static void artfreqTest_##name()

#define CHECK(condition)                test::check ((condition), __FILE__, __LINE__, #condition)
// The message may be built with <<:  CHECK_MSG (ok, "gain " << gain << " dB at " << frequency);
#define CHECK_MSG(condition, message)                                     \
    do                                                                    \
    {                                                                     \
        std::ostringstream artfreqMessage;                                  \
        artfreqMessage << message;                                          \
        test::check ((condition), __FILE__, __LINE__, artfreqMessage.str()); \
    } while (false)
#define CHECK_NEAR(actual, expected, tolerance) \
    test::checkNear ((actual), (expected), (tolerance), #actual, __FILE__, __LINE__)
#define CHECK_LESS(actual, limit)       test::checkLess ((actual), (limit), #actual, __FILE__, __LINE__)
#define CHECK_GREATER(actual, limit)    test::checkGreater ((actual), (limit), #actual, __FILE__, __LINE__)
