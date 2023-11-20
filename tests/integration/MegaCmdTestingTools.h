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

#include <gtest/gtest.h>

#include "megacmdcommonutils.h"
#include "megacmd.h"
#include "client/megacmdclient.h"

#include <gtest/gtest.h>
#include "Instruments.h"

#include <chrono>
#include <future>

#define ASSERT_CONTAINS(container, item) \
    ASSERT_NE(std::find(container.begin(), container.end(), item), container.end())

#define ASSERT_NCONTAINS(container, item) \
    ASSERT_EQ(std::find(container.begin(), container.end(), item), container.end())

#define EXPECT_CONTAINS(container, item) \
    EXPECT_NE(std::find(container.begin(), container.end(), item), container.end())

#define EXPECT_NCONTAINS(container, item) \
    EXPECT_EQ(std::find(container.begin(), container.end(), item), container.end())

std::vector<std::string> splitByNewline(const std::string& str);

class ClientResponse
{
    int mStatus = -1;
    OUTSTRING mErr;
    OUTSTRING mOut;
#ifdef _WIN32
    std::string mUtf8String;
#endif

public:
    ClientResponse(int status, OUTSTRINGSTREAM &streamOut)
        : mStatus(status)
        , mOut (streamOut.str())
    #ifdef _WIN32
        , mUtf8String(megacmd::getutf8fromUtf16(mOut.c_str()))
    #endif
    {}

    int status() { return mStatus; }
    bool ok() { return !mStatus; }

    // returns an utf8 std::string with the response from the server
    std::string &out()
    {
#ifdef _WIN32
        return mUtf8String;
#else
        return mOut;
#endif
    }
};

ClientResponse executeInClient(const std::vector<std::string>& command, bool nonInteractive = true);
void ensureLoggedIn();
void ensureReadStructure();

class BasicGenericTest : public ::testing::Test
{
};

class LoggedInTest : public BasicGenericTest
{
    void SetUp() override
    {
        ensureLoggedIn();
    }
};

class ReadTest : public LoggedInTest
{
    void SetUp() override
    {
        ensureReadStructure();
    }
};
