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

#include "lib/code_annotation.h"
#include "lib/os_path.h"
#include "lib/posix/posix_dlfcn.h"
#include "lib/posix/posix_filesystem.h"
#include "lib/posix/posix_types.h"
#include "lib/secure_crt.h"
#include "lib/sysdep/filesystem.h"
#include "lib/sysdep/os.h"
#include "lib/sysdep/sysdep.h"
#include "lib/types.h"

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <string>

class TestSysdep : public CxxTest::TestSuite
{
public:
	void test_random()
	{
		u64 a = 0, b = 0;
		TS_ASSERT_OK(sys_generate_random_bytes((u8*)&a, sizeof(a)));
		TS_ASSERT_OK(sys_generate_random_bytes((u8*)&b, sizeof(b)));
		TS_ASSERT_DIFFERS(a, b);
	}

	void test_sys_ExecutablePathname()
	{
		OsPath path = sys_ExecutablePathname();

		// Try it first with the real executable (i.e. the
		// one that's running this test code)
		// Check it's absolute
		TSM_ASSERT(L"Path: "+path.string(), path_is_absolute(path.string().c_str()));
		// Check the file exists
		TS_ASSERT(std::filesystem::is_regular_file(path));
	}

private:
	bool path_is_absolute(const wchar_t* path)
	{
		// UNIX-style absolute paths
		if (path[0] == '/')
			return true;

		// Windows UNC absolute paths
		if (path[0] == '\\' && path[1] == '\\')
			return true;

		// Windows drive-letter absolute paths
		if (iswalpha(path[0]) && path[1] == ':' && (path[2] == '/' || path[2] == '\\'))
			return true;

		return false;
	}

};
