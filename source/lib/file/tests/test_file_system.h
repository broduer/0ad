/* Copyright (C) 2026 Wildfire Games.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "lib/self_test.h"

#include "lib/file/file_system.h"
#include "lib/posix/posix_types.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
const OsPath TEST_FOLDER(DataDir()/"_test_file_system.temp");

// The mtime must be in the system clock (Unix epoch) domain, not the file
// clock domain (e.g. 1601 on Windows), so one day of tolerance suffices.
constexpr time_t MTIME_TOLERANCE{86400};
} // namespace

class TestFileSystem : public CxxTest::TestSuite
{
	void writeFile(const OsPath& path, const std::string& content)
	{
		std::ofstream stream(path.fileSystemPath(), std::ofstream::out | std::ofstream::trunc);
		TS_ASSERT(stream.is_open());
		stream << content;
		TS_ASSERT(stream.good());
	}

public:
	void setUp()
	{
		if (DirectoryExists(TEST_FOLDER))
			TS_ASSERT_OK(DeleteDirectory(TEST_FOLDER));
		TS_ASSERT_OK(CreateDirectories(TEST_FOLDER, 0700, false));
	}

	void tearDown()
	{
		if (DirectoryExists(TEST_FOLDER))
			TS_ASSERT_OK(DeleteDirectory(TEST_FOLDER));
	}

	void test_get_file_info()
	{
		// Writes to stdout:
		// const OsPath missing(TEST_FOLDER/"missing.txt");
		// CFileInfo missingInfo;
		// TS_ASSERT_EQUALS(GetFileInfo(missing, &missingInfo), ERR::EXCEPTION);

		const OsPath file(TEST_FOLDER/"file.txt");
		writeFile(file, "012345");

		CFileInfo info;
		TS_ASSERT_OK(GetFileInfo(file, &info));
		TS_ASSERT_PATH_EQUALS(info.Name(), file.Filename());
		TS_ASSERT_EQUALS(info.Size(), static_cast<off_t>(6));

		// The mtime must be in the system clock (Unix epoch) domain, i.e.
		// close to the current time.
		const time_t now{std::time(nullptr)};
		TS_ASSERT_DELTA(info.MTime(), now, MTIME_TOLERANCE);

		// Advance the mtime by one hour; the duration must be preserved.
		const std::filesystem::path fsPath{file.fileSystemPath()};
		std::filesystem::last_write_time(fsPath, std::filesystem::last_write_time(fsPath) + std::chrono::hours(1));

		CFileInfo after;
		TS_ASSERT_OK(GetFileInfo(file, &after));
		TS_ASSERT_EQUALS(after.MTime() - info.MTime(), static_cast<time_t>(3600));
	}
};
