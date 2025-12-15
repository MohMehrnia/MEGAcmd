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

#include "MegaCmdTestingTools.h"
#include "TestUtils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class PutTests: public NOINTERACTIVELoggedInTest
{
    SelfDeletingTmpFolder mTmpDir;

    void SetUp() override
    {
        NOINTERACTIVELoggedInTest::SetUp();
        // Clean up any existing test files
        executeInClient({"rm", "-rf", "put_test_dir"});
        executeInClient({"rm", "-f", "test_file*.txt"});
    }

    void TearDown() override
    {
        // Clean up test files
        executeInClient({"rm", "-rf", "put_test_dir"});
        executeInClient({"rm", "-f", "test_file*.txt"});
        NOINTERACTIVELoggedInTest::TearDown();
    }

protected:
    fs::path localPath() const
    {
        return mTmpDir.path();
    }

    void createLocalFile(const std::string& filename, const std::string& content = "")
    {
        std::ofstream file(localPath() / filename);
        file << content;
    }

    void createLocalDir(const std::string& dirname)
    {
        fs::create_directories(localPath() / dirname);
    }
};

TEST_F(PutTests, SingleFileUpload)
{
    const std::string filename = "test_file.txt";
    const std::string content = "Hello, MEGAcmd!";

    createLocalFile(filename, content);

    auto result = executeInClient({"put", (localPath() / filename).string()});
    ASSERT_TRUE(result.ok());

    // Verify file was uploaded
    result = executeInClient({"cat", filename});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(content, result.out());
}

TEST_F(PutTests, SingleFileUploadToDestination)
{
    const std::string filename = "test_file.txt";
    const std::string content = "Hello, MEGAcmd!";
    const std::string remoteDir = "put_test_dir";

    createLocalFile(filename, content);

    // Create remote directory
    auto result = executeInClient({"mkdir", remoteDir});
    ASSERT_TRUE(result.ok());

    // Upload file to specific directory
    result = executeInClient({"put", (localPath() / filename).string(), remoteDir});
    ASSERT_TRUE(result.ok());

    // Verify file was uploaded to correct location
    result = executeInClient({"cat", remoteDir + "/" + filename});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(content, result.out());
}

TEST_F(PutTests, EmptyFileUpload)
{
    const std::string filename = "empty_file.txt";

    createLocalFile(filename);

    auto result = executeInClient({"put", (localPath() / filename).string()});
    ASSERT_TRUE(result.ok());

    // Verify empty file exists
    result = executeInClient({"ls", filename});
    ASSERT_TRUE(result.ok());
    EXPECT_THAT(result.out(), testing::HasSubstr(filename));
}

TEST_F(PutTests, MultipleFileUpload)
{
    const std::string remoteDir = "put_test_dir";

    // Create remote directory
    auto result = executeInClient({"mkdir", remoteDir});
    ASSERT_TRUE(result.ok());

    const std::vector<std::string> files = {"file1.txt", "file2.txt", "file3.txt"};

    for (const auto& file: files)
    {
        createLocalFile(file, "Content of " + file);
    }

    std::vector<std::string> command = {"put"};
    for (const auto& file: files)
    {
        command.push_back((localPath() / file).string());
    }

    command.push_back(remoteDir);

    result = executeInClient(command);

    ASSERT_TRUE(result.ok()) << " command = " << joinString(command);

    // Verify all files were uploaded
    for (const auto& file: files)
    {
        result = executeInClient({"cat", remoteDir + "/" + file});
        ASSERT_TRUE(result.ok()) << "Failed to verify file: " << file;
        EXPECT_EQ("Content of " + file, result.out());
    }
}

TEST_F(PutTests, DirectoryUpload)
{
    const std::string dirname = "test_dir";
    const std::string subfile = "subfile.txt";
    const std::string content = "Content in subdirectory";

    // Create local directory structure
    createLocalDir(dirname);
    createLocalFile(dirname + "/" + subfile, content);

    auto result = executeInClient({"put", (localPath() / dirname).string()});
    ASSERT_TRUE(result.ok());

    // Verify directory and file were uploaded
    result = executeInClient({"ls", dirname});
    ASSERT_TRUE(result.ok());
    EXPECT_THAT(result.out(), testing::HasSubstr(subfile));

    result = executeInClient({"cat", dirname + "/" + subfile});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(content, result.out());
}

TEST_F(PutTests, DirectoryUploadToDestination)
{
    const std::string dirname = "test_dir";
    const std::string subfile = "subfile.txt";
    const std::string content = "Content in subdirectory";
    const std::string remoteDest = "put_test_dir";

    // Create local directory structure
    createLocalDir(dirname);
    createLocalFile(dirname + "/" + subfile, content);

    // Create remote destination directory
    auto result = executeInClient({"mkdir", remoteDest});
    ASSERT_TRUE(result.ok());

    // Upload directory to specific location
    result = executeInClient({"put", (localPath() / dirname).string(), remoteDest});
    ASSERT_TRUE(result.ok());

    // Verify directory was uploaded to correct location
    result = executeInClient({"ls", remoteDest + "/" + dirname});
    ASSERT_TRUE(result.ok());
    EXPECT_THAT(result.out(), testing::HasSubstr(subfile));

    result = executeInClient({"cat", remoteDest + "/" + dirname + "/" + subfile});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(content, result.out());
}

TEST_F(PutTests, UploadWithCreateFlag)
{
    const std::string filename = "test_file.txt";
    const std::string content = "Hello, MEGAcmd!";
    const std::string remotePath = "new_dir/test_file.txt";

    createLocalFile(filename, content);

    // Use -c flag to create destination directory
    auto result = executeInClient({"put", "-c", (localPath() / filename).string(), remotePath});
    ASSERT_TRUE(result.ok());

    // Verify file was uploaded and directory was created
    result = executeInClient({"cat", remotePath});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(content, result.out());
}

TEST_F(PutTests, UploadNonAsciiFilename)
{
    const std::string filename = "\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88\xE3\x83\x95\xE3\x82\xA1"
                                 "\xE3\x82\xA4\xE3\x83\xAB.txt";
    const std::string content = "Content with non-ASCII filename";

    createLocalFile(filename, content);

    auto result = executeInClient({"put", (localPath() / filename).string()});
    ASSERT_TRUE(result.ok());

    // Verify file was uploaded
    result = executeInClient({"ls"});
    ASSERT_TRUE(result.ok());
    EXPECT_THAT(result.out(), testing::HasSubstr(filename));
}

TEST_F(PutTests, UploadFileWithSpaces)
{
    const std::string filename = "test file with spaces.txt";
    const std::string content = "Content in file with spaces";

    createLocalFile(filename, content);

    auto result = executeInClient({"put", (localPath() / filename).string()});
    ASSERT_TRUE(result.ok());

    // Verify file was uploaded
    result = executeInClient({"ls"});
    ASSERT_TRUE(result.ok());
    EXPECT_THAT(result.out(), testing::HasSubstr(filename));
}

TEST_F(PutTests, ReplaceExistingFile)
{
    const std::string filename = "test_file.txt";
    const std::string originalContent = "Original content";
    const std::string newContent = "New content replacing original";

    // Upload original file
    createLocalFile(filename, originalContent);
    auto result = executeInClient({"put", (localPath() / filename).string()});
    ASSERT_TRUE(result.ok());

    // Verify original content
    result = executeInClient({"cat", filename});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(originalContent, result.out());

    // Replace with new content
    createLocalFile(filename, newContent);
    result = executeInClient({"put", (localPath() / filename).string(), filename});
    ASSERT_TRUE(result.ok());

    // Verify file was replaced
    result = executeInClient({"cat", filename});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(newContent, result.out());
}

TEST_F(PutTests, ReplaceFileInSubdirectory)
{
    const std::string dirname = "test_dir";
    const std::string filename = "test_file.txt";
    const std::string originalContent = "Original content";
    const std::string newContent = "New content in subdirectory";

    // Create directory and upload original file
    executeInClient({"mkdir", dirname});
    createLocalFile(filename, originalContent);
    auto result = executeInClient({"put", (localPath() / filename).string(), dirname});
    ASSERT_TRUE(result.ok());

    // Replace file in subdirectory
    createLocalFile(filename, newContent);
    result = executeInClient({"put", (localPath() / filename).string(), dirname + "/" + filename});
    ASSERT_TRUE(result.ok());

    // Verify file was replaced
    result = executeInClient({"cat", dirname + "/" + filename});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(newContent, result.out());
}

TEST_F(PutTests, MultipleFilesToFileTargetShouldFail)
{
    const std::string file1 = "file1.txt";
    const std::string file2 = "file2.txt";
    const std::string targetFile = "target.txt";

    // Create target file
    createLocalFile(file1, "Content 1");
    createLocalFile(file2, "Content 2");

    // Upload first file to create target
    auto result = executeInClient({"put", (localPath() / file1).string(), targetFile});
    ASSERT_TRUE(result.ok());

    // Attempt to upload multiple files to a file target (should fail)
    result = executeInClient({"put", (localPath() / file1).string(), (localPath() / file2).string(), targetFile});
    ASSERT_FALSE(result.ok());
    EXPECT_THAT(result.err(), testing::HasSubstr("Destination is not valid"));
}

TEST_F(PutTests, UploadToNonExistentPathWithoutCreateFlag)
{
    const std::string filename = "test_file.txt";
    const std::string nonExistentPath = "non_existent_dir/file.txt";

    createLocalFile(filename, "Test content");

    // Attempt to upload to non-existent path without -c flag (should fail)
    auto result = executeInClient({"put", (localPath() / filename).string(), nonExistentPath});
    ASSERT_FALSE(result.ok());
    EXPECT_THAT(result.err(), testing::HasSubstr("Couldn't find destination folder"));
}

TEST_F(PutTests, UploadDirectoryToFileTargetShouldFail)
{
    const std::string dirname = "test_dir";
    const std::string targetFile = "target.txt";

    // Create local directory and remote target file
    createLocalDir(dirname);
    createLocalFile("temp.txt", "temp");
    auto result = executeInClient({"put", (localPath() / "temp.txt").string(), targetFile});
    ASSERT_TRUE(result.ok());

    // Attempt to upload directory to a file target (should fail)
    result = executeInClient({"put", (localPath() / dirname).string(), targetFile});
    ASSERT_FALSE(result.ok());
    EXPECT_THAT(result.err(), testing::HasSubstr("Destination is not valid"));
}

TEST_F(PutTests, ReplaceFileWithDifferentCase)
{
    const std::string filename = "test_file.txt";
    const std::string originalContent = "Original content";
    const std::string newContent = "New content";

    // Upload original file
    createLocalFile(filename, originalContent);
    auto result = executeInClient({"put", (localPath() / filename).string()});
    ASSERT_TRUE(result.ok());

    // Replace with same filename but different case (case sensitivity test)
    createLocalFile(filename, newContent);
    result = executeInClient({"put", (localPath() / filename).string(), filename});
    ASSERT_TRUE(result.ok());

    // Verify file was replaced
    result = executeInClient({"cat", filename});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(newContent, result.out());
}
