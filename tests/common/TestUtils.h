﻿/**
 * (c) 2013 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of MEGAcmd.
 *
 * MEGAcmd is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#pragma once

#include <regex>
#include <stdio.h>
#include <stdarg.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include <memory>
#include <future>
#include <filesystem>

#include <sstream>

#include <gmock/gmock.h>

#include "megacmdcommonutils.h"

#define ULOG_COLOR(color, fmt, ...) \
    do { if (isatty(1)) printf(color fmt "\033[0m\n", ##__VA_ARGS__); \
         else printf(fmt "\n", ##__VA_ARGS__); \
    } while(0)

#define ULOG(fmt, ...) ULOG_COLOR("\033[38;5;250m", fmt, ##__VA_ARGS__)
#define ULOG_ERR(fmt, ...) ULOG_COLOR("\033[31m", fmt, ##__VA_ARGS__)
#define ULOG_WARN(fmt, ...) ULOG_COLOR("\033[33m", fmt, ##__VA_ARGS__)
#define ULOG_NOTE(fmt, ...) ULOG_COLOR("\033[38;5;81m", fmt, ##__VA_ARGS__)

namespace gti
{
    enum GTestColor
    {
        COLOR_DEFAULT,
        COLOR_RED,
        COLOR_GREEN,
        COLOR_YELLOW
    };
    static void ColoredPrintf(GTestColor color, const char* fmt, ...)
    {
#ifndef _WIN32
        va_list args;
        va_start(args, fmt);

#ifdef WIN32
        static const bool in_color_mode = _isatty(_fileno(stdout)) != 0;
#else
        static const bool in_color_mode = isatty(STDOUT_FILENO) != 0;
#endif

        const bool use_color = in_color_mode && (color != GTestColor::COLOR_DEFAULT);

        if (use_color)
        {
            printf("\033[0;3%sm", std::to_string(static_cast<int>(color)).c_str());
        }
        vprintf(fmt, args);
        if (use_color)
        {
            printf("\033[m");
        }
        va_end(args);
#endif
    }
}

class GTestMessager
{
    using GTestColor = gti::GTestColor;
    std::ostringstream mOstr;
    GTestColor mColor;
    std::string mTitle;

public:
    GTestMessager(GTestColor color, std::string title = {}) : mColor(color), mTitle(title)
    {
    }
    ~GTestMessager()
    {
        gti::ColoredPrintf(GTestColor::COLOR_GREEN, "[ %8s ] ", mTitle.c_str());
        gti::ColoredPrintf(GTestColor::COLOR_DEFAULT, "%s\n", mOstr.str().c_str());
    }

    template <typename T>
    GTestMessager& operator<<(T* obj)
    {
        if (obj)
        {
            mOstr << obj;
        }
        else
        {
            mOstr << "(NULL)";
        }
        return *this;
    }

    template <typename T, typename = typename std::enable_if<std::is_scalar<T>::value>::type>
    GTestMessager& operator<<(const T obj)
    {
        static_assert(!std::is_same<T, std::nullptr_t>::value, "T cannot be nullptr_t");
        mOstr << obj;
        return *this;
    }

    template <typename T, typename = typename std::enable_if<!std::is_scalar<T>::value>::type>
    GTestMessager& operator<<(const T& obj)
    {
        mOstr << obj;
        return *this;
    }
};
// Fancy message defines
#define G_TEST_TITLED_MSG(title) GTestMessager(gti::COLOR_GREEN, title)
#define G_TEST_MSG   GTestMessager(gti::COLOR_GREEN)
#define G_TEST_INFO  GTestMessager(gti::COLOR_GREEN, "INFO")
#define G_SUBTEST    GTestMessager(gti::COLOR_GREEN, "SUBTEST")
#define G_SUBSUBTEST GTestMessager(gti::COLOR_GREEN, "SSUBTEST")

// For some reason, the regex matches included with GTest fail to match against
// regex expressions using ERE syntax. The following two custom matchers use std::regex,
// which provides consistent, cross-platform behavior, as opposed to GTest's regex
// peculiarities (https://google.github.io/googletest/advanced.html#regular-expression-syntax)

MATCHER_P(MatchesStdRegex, pattern, "") {
    *result_listener << "where the string '" << arg << "' matches the std::regex '" << pattern << "'";
    std::regex regex(pattern);
    return std::regex_match(arg, regex);
}

MATCHER_P(ContainsStdRegex, pattern, "")
{
    *result_listener << "where the string '" << arg << "' contains the std::regex '" << pattern << "'";
    std::regex regex(pattern);
    return std::regex_search(arg, regex);
}

class SelfDeletingTmpFolder
{
    fs::path mTempDir;

public:
    SelfDeletingTmpFolder()
    {
        mTempDir = fs::temp_directory_path() / fs::path(megacmd::generateRandomAlphaNumericString(10));
        fs::create_directory(mTempDir);
    }

    SelfDeletingTmpFolder(const fs::path& subPath) :
        SelfDeletingTmpFolder()
    {
        mTempDir /= subPath;
        fs::create_directories(mTempDir);
    }

    ~SelfDeletingTmpFolder()
    {
        fs::remove_all(mTempDir);
    }

    std::string string() const
    {
        return mTempDir.string();
    }

    fs::path path() const
    {
        return mTempDir;
    }
};

#ifdef WIN32
/**
 * @brief Google tests outputing needs stdout mode to be _O_TEXT
 * otherwise low level printing will fail.
 * This is a custom listener to wrapp writing routines
 */
struct CustomTestEventListener : public ::testing::EmptyTestEventListener {
    std::unique_ptr<::testing::TestEventListener> mDefault;

    void OnTestProgramStart(const ::testing::UnitTest& unit_test) override {
        megacmd::WindowsNarrowStdoutGuard smg;
        mDefault->OnTestProgramStart(unit_test);
    }

    void OnTestIterationStart(const ::testing::UnitTest& unit_test, int iteration) override {
        megacmd::WindowsNarrowStdoutGuard smg;
        mDefault->OnTestIterationStart(unit_test, iteration);
    }

    void OnEnvironmentsSetUpStart(const ::testing::UnitTest& unit_test) override {
        megacmd::WindowsNarrowStdoutGuard smg;
        mDefault->OnEnvironmentsSetUpStart(unit_test);
    }

    void OnEnvironmentsSetUpEnd(const ::testing::UnitTest& unit_test) override {
        megacmd::WindowsNarrowStdoutGuard smg;
        mDefault->OnEnvironmentsSetUpEnd(unit_test);
    }

    void OnTestSuiteStart(const ::testing::TestSuite& test_suite) override {
        megacmd::WindowsNarrowStdoutGuard smg;
        mDefault->OnTestSuiteStart(test_suite);
    }

#ifndef GTEST_REMOVE_LEGACY_TEST_CASEAPI_
    void OnTestCaseStart(const ::testing::TestCase& test_case) override {
        megacmd::WindowsNarrowStdoutGuard smg;
        mDefault->OnTestCaseStart(test_case);
    }
#endif  // GTEST_REMOVE_LEGACY_TEST_CASEAPI_

    void OnTestStart(const ::testing::TestInfo& test_info) override {
        megacmd::WindowsNarrowStdoutGuard smg;
        mDefault->OnTestStart(test_info);
    }

    void OnTestDisabled(const ::testing::TestInfo& test_info) override {
        megacmd::WindowsNarrowStdoutGuard smg;
        mDefault->OnTestDisabled(test_info);
    }

    void OnTestPartResult(const ::testing::TestPartResult& result) override {
        megacmd::WindowsNarrowStdoutGuard smg;
        mDefault->OnTestPartResult(result);
    }

    void OnTestEnd(const ::testing::TestInfo& test_info) override {
        megacmd::WindowsNarrowStdoutGuard smg;
        mDefault->OnTestEnd(test_info);
    }

    void OnTestSuiteEnd(const ::testing::TestSuite& test_suite) override {
        megacmd::WindowsNarrowStdoutGuard smg;
        mDefault->OnTestSuiteEnd(test_suite);
    }

#ifndef GTEST_REMOVE_LEGACY_TEST_CASEAPI_
    void OnTestCaseEnd(const ::testing::TestCase& test_case) override {
        megacmd::WindowsNarrowStdoutGuard smg;
        mDefault->OnTestCaseEnd(test_case);
    }
#endif  // GTEST_REMOVE_LEGACY_TEST_CASEAPI_

    void OnEnvironmentsTearDownStart(const ::testing::UnitTest& unit_test) override {
        megacmd::WindowsNarrowStdoutGuard smg;
        mDefault->OnEnvironmentsTearDownStart(unit_test);
    }

    void OnEnvironmentsTearDownEnd(const ::testing::UnitTest& unit_test) override {
        megacmd::WindowsNarrowStdoutGuard smg;
        mDefault->OnEnvironmentsTearDownEnd(unit_test);
    }

    void OnTestIterationEnd(const ::testing::UnitTest& unit_test, int iteration) override {
        megacmd::WindowsNarrowStdoutGuard smg;
        mDefault->OnTestIterationEnd(unit_test, iteration);
    }

    void OnTestProgramEnd(const ::testing::UnitTest& unit_test) override {
        megacmd::WindowsNarrowStdoutGuard smg;
        mDefault->OnTestProgramEnd(unit_test);
    }
};
#endif
