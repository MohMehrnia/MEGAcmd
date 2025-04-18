/**
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

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "Instruments.h"
#include "TestUtils.h"

#include "configurationmanager.h"

TEST(PlatformDirectoriesTest, runtimeDirPath)
{
    using megacmd::PlatformDirectories;

    auto dirs = PlatformDirectories::getPlatformSpecificDirectories();
    ASSERT_TRUE(dirs != nullptr);

#ifndef __APPLE__
    EXPECT_EQ(dirs->runtimeDirPath(), dirs->configDirPath());
#endif

#ifndef WIN32
    {
        G_SUBTEST << "Non existing HOME folder";
        auto homeGuard = TestInstrumentsEnvVarGuard("HOME", "/non-existent-dir");
        EXPECT_STREQ(dirs->runtimeDirPath().c_str(), megacmd::PosixDirectories::noHomeFallbackFolder().c_str());
    }
    {
        G_SUBTEST << "Empty HOME folder";
        auto homeGuard = TestInstrumentsEnvVarGuard("HOME", "");
        EXPECT_STREQ(dirs->runtimeDirPath().c_str(), megacmd::PosixDirectories::noHomeFallbackFolder().c_str());
    }

    #ifdef __APPLE__
    {
        SelfDeletingTmpFolder tmpFolder;
        auto homeGuard = TestInstrumentsEnvVarGuard("HOME", tmpFolder.string());
        fs::create_directories(tmpFolder.path() / "Library" / "Caches");
        EXPECT_STREQ(dirs->runtimeDirPath().c_str(), tmpFolder.string().append("/Library/Caches/megacmd.mac").c_str());
    }
    #else
    {
        auto homeGuard = TestInstrumentsEnvVarGuard("HOME", "/tmp");
        EXPECT_EQ(dirs->runtimeDirPath(), "/tmp/.megaCmd");
    }
    #endif

    {
        G_SUBTEST << "Existing non-ascii HOME folder";
        SelfDeletingTmpFolder tmpFolder("file_\u5f20\u4e09");

    #ifdef __APPLE__
        fs::path runtimeFolder = tmpFolder.path() / "Library" / "Caches" / "megacmd.mac";
        fs::create_directories(runtimeFolder);
    #else
        fs::path runtimeFolder = tmpFolder.path() / ".megaCmd";
    #endif

        auto homeGuard = TestInstrumentsEnvVarGuard("HOME", tmpFolder.string());
        EXPECT_EQ(dirs->runtimeDirPath().string(), runtimeFolder.string());
    }

#endif
}

TEST(PlatformDirectoriesTest, configDirPath)
{
    using megacmd::PlatformDirectories;

    auto dirs = PlatformDirectories::getPlatformSpecificDirectories();
    ASSERT_TRUE(dirs != nullptr);

#ifdef WIN32
    {
        G_SUBTEST << "With $MEGACMD_WORKING_FOLDER_SUFFIX";
        auto guard = TestInstrumentsEnvVarGuard("MEGACMD_WORKING_FOLDER_SUFFIX", "foobar");
        EXPECT_THAT(dirs->configDirPath().wstring(), testing::EndsWith(L".megaCmd_foobar"));
    }
    {
        G_SUBTEST << "Without $MEGACMD_WORKING_FOLDER_SUFFIX";
        auto guard = TestInstrumentsUnsetEnvVarGuard("MEGACMD_WORKING_FOLDER_SUFFIX");
        EXPECT_THAT(dirs->configDirPath().wstring(), testing::EndsWith(L".megaCmd"));
    }
    {
        G_SUBTEST << "With non-ascii $MEGACMD_WORKING_FOLDER_SUFFIX";
        auto guard = TestInstrumentsEnvVarGuardW(L"MEGACMD_WORKING_FOLDER_SUFFIX", L"file_\u5f20\u4e09");
        EXPECT_THAT(dirs->configDirPath().wstring(), testing::EndsWith(L"file_\u5f20\u4e09"));
    }

    {
        G_SUBTEST << "With path length exceeding MAX_PATH";

        // To reach maximum length for the folder path; since len(".megaCmd_")=9, and 9+246=255
        // Full path can exceed MAX_PATH=260, but each individual file or folder must have length < 255
        constexpr int suffixLength = 246;

        const std::string suffix = megacmd::generateRandomAlphaNumericString(suffixLength);
        auto guard = TestInstrumentsEnvVarGuard("MEGACMD_WORKING_FOLDER_SUFFIX", suffix);

        const fs::path configDir = dirs->configDirPath();
        EXPECT_THAT(configDir.string(), testing::EndsWith(suffix));
        EXPECT_THAT(configDir.string(), testing::StartsWith(R"(\\?\)"));

        // This would throw an exception without the \\?\ prefix
        SelfDeletingTmpFolder tmpFolder(configDir);
    }
#else
    {
        G_SUBTEST << "With alternative existing HOME";
        auto homeGuard = TestInstrumentsEnvVarGuard("HOME", "/tmp");
        EXPECT_EQ(dirs->configDirPath().string(), "/tmp/.megaCmd");
    }
    {
        G_SUBTEST << "With alternative existing non-ascii HOME";
        SelfDeletingTmpFolder tmpFolder("file_\u5f20\u4e09");
        fs::path configFolder = tmpFolder.path() / ".megaCmd";

        auto homeGuard = TestInstrumentsEnvVarGuard("HOME", tmpFolder.string());
        EXPECT_EQ(dirs->configDirPath().string(), configFolder.string());
    }
    {
        G_SUBTEST << "With alternative NON existing HOME";
        auto homeGuard = TestInstrumentsEnvVarGuard("HOME", "/non-existent-dir");
        EXPECT_EQ(dirs->configDirPath().string(), megacmd::PosixDirectories::noHomeFallbackFolder());
    }
    {
        using megacmd::ConfigurationManager;
        using megacmd::ScopeGuard;
        G_SUBTEST << "Correct 0700 permissions";

        const mode_t oldUmask = umask(0);
        ScopeGuard g([oldUmask] { umask(oldUmask); });

        SelfDeletingTmpFolder tmpFolder;
        const fs::path dirPath = tmpFolder.path() / "some_folder";
        ConfigurationManager::createFolderIfNotExisting(dirPath);

        struct stat info;
        ASSERT_EQ(stat(dirPath.c_str(), &info), 0);
        ASSERT_TRUE(S_ISDIR(info.st_mode));

        const mode_t actualPerms = info.st_mode & 0777;
        const mode_t expectedPerms = 0700;
        EXPECT_EQ(actualPerms, expectedPerms);
    }
#endif
}

TEST(PlatformDirectoriesTest, lockExecution)
{
    using megacmd::ConfigurationManager;
    {
        G_SUBTEST << "With default paths:";
        ASSERT_TRUE(ConfigurationManager::lockExecution());
        ASSERT_TRUE(ConfigurationManager::unlockExecution());
    }

#ifndef WIN32

    using megacmd::PlatformDirectories;
    auto dirs = PlatformDirectories::getPlatformSpecificDirectories();

    {
        G_SUBTEST << "Another HOME";

        SelfDeletingTmpFolder tmpFolder;
        auto homeGuard = TestInstrumentsEnvVarGuard("HOME", tmpFolder.string());

#ifdef __APPLE__
        fs::create_directories(tmpFolder.path() / "Library" / "Caches");
        EXPECT_STREQ(dirs->runtimeDirPath().c_str(), tmpFolder.string().append("/Library/Caches/megacmd.mac").c_str());
#endif
        ASSERT_TRUE(ConfigurationManager::lockExecution());
        ASSERT_TRUE(ConfigurationManager::unlockExecution());

    }

    {
        G_SUBTEST << "Non-ascii HOME";

        SelfDeletingTmpFolder tmpFolder("file_\u5f20\u4e09");
        auto homeGuard = TestInstrumentsEnvVarGuard("HOME", tmpFolder.string());

#ifdef __APPLE__
        fs::create_directories(tmpFolder.path() / "Library" / "Caches");
        EXPECT_STREQ(dirs->runtimeDirPath().c_str(), tmpFolder.string().append("/Library/Caches/megacmd.mac").c_str());
#endif
        ASSERT_TRUE(ConfigurationManager::lockExecution());
        ASSERT_TRUE(ConfigurationManager::unlockExecution());

    }

    {
        G_SUBTEST << "With legacy one";

        auto legacyLockFolder = ConfigurationManager::getConfigFolder();
        if (dirs->runtimeDirPath() != legacyLockFolder)
        {
            EXPECT_STRNE(dirs->runtimeDirPath().c_str(), legacyLockFolder.c_str());

            ASSERT_TRUE(ConfigurationManager::lockExecution(legacyLockFolder));

            ASSERT_FALSE(ConfigurationManager::lockExecution());

            ASSERT_TRUE(ConfigurationManager::unlockExecution(legacyLockFolder));

            // All good after that
            ASSERT_TRUE(ConfigurationManager::lockExecution());
            ASSERT_TRUE(ConfigurationManager::unlockExecution());
        }
        else
        {
            G_TEST_INFO << "LEGACY path is the same as runtime one. All good.";
        }
    }
#endif
}

#ifndef WIN32
TEST(PlatformDirectoriesTest, getOrCreateSocketPath)
{
    using megacmd::getOrCreateSocketPath;
    using megacmd::PlatformDirectories;

    auto dirs = PlatformDirectories::getPlatformSpecificDirectories();

    {
        G_SUBTEST << "With $MEGACMD_SOCKET_NAME (normal or fallback case)";
        auto guard = TestInstrumentsEnvVarGuard("MEGACMD_SOCKET_NAME", "test.sock");
        auto socketPath = getOrCreateSocketPath(false);

        auto expectedNormalFile = (dirs->runtimeDirPath() / "test.sock").string(); // normal case
        auto expectedFallbackFile = megacmd::PosixDirectories::noHomeFallbackFolder().append("/test.sock"); // too length case
        ASSERT_THAT(socketPath, testing::AnyOf(expectedNormalFile, expectedFallbackFile));
    }

    {
        G_SUBTEST << "With non-ascii $MEGACMD_SOCKET_NAME (normal or fallback case)";
        auto guard = TestInstrumentsEnvVarGuard("MEGACMD_SOCKET_NAME", "file_\u5f20\u4e09");
        auto socketPath = getOrCreateSocketPath(false);

        auto expectedNormalFile = (dirs->runtimeDirPath() / "file_\u5f20\u4e09").string(); // normal case
        auto expectedFallbackFile = megacmd::PosixDirectories::noHomeFallbackFolder().append("/file_\u5f20\u4e09"); // too length case
        ASSERT_THAT(socketPath, testing::AnyOf(expectedNormalFile, expectedFallbackFile));
    }

    {
        G_SUBTEST << "Without $MEGACMD_SOCKET_NAME (normal or fallback case)";
        auto socketPath = getOrCreateSocketPath(false);

        auto expectedNormalFile = (dirs->runtimeDirPath() / "megacmd.socket").string(); // normal case
        auto expectedFallbackFile = megacmd::PosixDirectories::noHomeFallbackFolder().append("/megacmd.socket"); // too length case
        ASSERT_THAT(socketPath, testing::AnyOf(expectedNormalFile, expectedFallbackFile));
    }

    {
        G_SUBTEST << "Without $MEGACMD_SOCKET_NAME, short path: no /tmp/megacmd-UID fallback";
        auto homeGuard = TestInstrumentsEnvVarGuard("HOME", "/tmp");
        auto guard = TestInstrumentsUnsetEnvVarGuard("MEGACMD_SOCKET_NAME");
        auto runtimeDir = dirs->runtimeDirPath();
        auto socketPath = getOrCreateSocketPath(false);

        ASSERT_STREQ(socketPath.c_str(), (dirs->runtimeDirPath() / "megacmd.socket").string().c_str());
    }

    {
        G_SUBTEST << "Without $MEGACMD_SOCKET_NAME, longth path: /tmp/megacmd-UID fallback";

        SelfDeletingTmpFolder tmpFolder("this_is_a_very_very_very_lengthy_folder_name_meant_to_make_socket_path_exceed_max_unix_socket_path_allowance");
#ifdef __APPLE__
        fs::create_directories(tmpFolder.path() / "Library" / "Caches");
#endif
        auto homeGuard = TestInstrumentsEnvVarGuard("HOME", tmpFolder.string());
        auto guard = TestInstrumentsUnsetEnvVarGuard("MEGACMD_SOCKET_NAME");
        auto runtimeDir = dirs->runtimeDirPath();
        auto socketPath = getOrCreateSocketPath(false);

        ASSERT_STRNE(socketPath.c_str(), (dirs->runtimeDirPath() / "megacmd.socket").string().c_str());
        ASSERT_STREQ(socketPath.c_str(), megacmd::PosixDirectories::noHomeFallbackFolder().append("/megacmd.socket").c_str());
    }
}
#else
TEST(PlatformDirectoriesTest, getNamedPipeName)
{
    using megacmd::getNamedPipeName;

    {
        G_SUBTEST << "With $MEGACMD_PIPE_SUFFIX";
        auto guard = TestInstrumentsEnvVarGuard("MEGACMD_PIPE_SUFFIX", "foobar");
        auto name = getNamedPipeName();
        EXPECT_THAT(name, testing::EndsWith(L"foobar"));
    }

    {
        G_SUBTEST << "Without $MEGACMD_PIPE_SUFFIX";
        auto guard = TestInstrumentsUnsetEnvVarGuard("MEGACMD_PIPE_SUFFIX");
        auto name = getNamedPipeName();
        EXPECT_THAT(name, testing::Not(testing::EndsWith(L"foobar")));
    }

    {
        G_SUBTEST << "With non-ascii $MEGACMD_PIPE_SUFFIX";
        auto guard = TestInstrumentsEnvVarGuardW(L"MEGACMD_PIPE_SUFFIX", L"file_\u5f20\u4e09");
        auto name = getNamedPipeName();
        EXPECT_THAT(name, testing::EndsWith(L"file_\u5f20\u4e09"));
    }
}
#endif
