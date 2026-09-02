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
#include "lib/os_path.h"
#include "lib/path.h"
#include "lib/sysdep/os.h"

#include <string>

class TestPath : public CxxTest::TestSuite
{
public:
	void test_ctor()
	{
		const char* s1 = "a/b/c";
		const wchar_t* w1 = L"a/b/c";

		// Empty strings
		Path p0a;
		Path p0b = Path(std::string());
		Path p0c = Path(std::wstring());
		TS_ASSERT(p0a.empty());
		TS_ASSERT_WSTR_EQUALS(p0a.string(), p0b.string());
		TS_ASSERT_WSTR_EQUALS(p0a.string(), p0c.string());

		// Construct from various types
		Path ps1a = Path(s1);
		Path ps1b = Path(std::string(s1));
		Path pw1a = Path(w1);
		Path pw1b = Path(std::wstring(w1));

		TS_ASSERT_WSTR_EQUALS(ps1a.string(), w1);
		TS_ASSERT_WSTR_EQUALS(ps1b.string(), w1);
		TS_ASSERT_WSTR_EQUALS(pw1a.string(), w1);
		TS_ASSERT_WSTR_EQUALS(pw1b.string(), w1);
	}

	void test_non_basic_latin()
	{
		TS_ASSERT_EQUALS(Path{"\u1337"}, Path{L"\u1337"});

		Path utf8Path{DataDir() / "_test\u1337.temp"};
		try
		{
			std::filesystem::path{utf8Path.string()};
		}
		catch(...)
		{
			TS_WARN("Its not possible to test Path when std::filesystem::path doesn't work.");
			return;
		}

		if (CreateDirectories(utf8Path, {}, false) != INFO::OK)
		{
			constexpr auto printChars = [](const auto& string)
			{
				for (const auto c : string)
				{
					if constexpr (sizeof(c) == 1)
						debug_printf("%02hhx ", static_cast<unsigned char>(c));
					else if constexpr (sizeof(c) == 2)
						debug_printf("%04hx ", static_cast<unsigned short>(c));
					else
					{
						static_assert(sizeof(c) == 4);
						debug_printf("%08x ", static_cast<unsigned int>(c));
					}
				}
				debug_printf("\n");
			};
			printChars(utf8Path.string());
			printChars(std::filesystem::path{utf8Path.string()}.native());
			TS_FAIL("Failed to create the directory");
		}

		TS_ASSERT(DirectoryExists(DataDir() / L"_test\u1337.temp"));
		TS_ASSERT_OK(DeleteDirectory(utf8Path));
	}
};
