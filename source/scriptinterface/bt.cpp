#include "bt.h"

#include <execinfo.h>
#include <cxxabi.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <array>
#include <memory>
#include <sstream>
#include <iomanip>

std::string getExecutablePath()
{
	char path[PATH_MAX];
	ssize_t len = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
	if (len == -1)
		throw std::runtime_error("Cannot resolve /proc/self/exe");

	path[len] = '\0';
	return std::string(path);
}

std::string addr2line_resolve(void* addr)
{
	// Use addr2line to resolve address -> symbol + file:line.
	// NOTE: Change the executable path if necessary.
	const std::string exe = getExecutablePath();

	char cmd[512];
	snprintf(cmd, sizeof(cmd), "addr2line -f -p -e %s %p", exe.c_str(), addr);

	std::array<char, 256> buffer{};
	std::string result;
	const std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
	if (!pipe)
		return "?? (addr2line failed)";

	while (fgets(buffer.data(), buffer.size(), pipe.get()))
		result += buffer.data();

	// Trim trailing newline
	if (!result.empty() && result.back() == '\n')
		result.pop_back();

	if (result.empty())
		return "??";

	// Demangle the function name
	const std::string at_str = " at ";
	const size_t at_pos = result.find(at_str);
	if (at_pos != std::string::npos) {
		const std::string mangled_name = result.substr(0, at_pos);
		int status = 0;
		char* demangled_name = abi::__cxa_demangle(mangled_name.c_str(), nullptr, nullptr, &status);
		if (!status && demangled_name) {
			result = std::string(demangled_name) + result.substr(at_pos);
			free(demangled_name);
		}
	}

	return result;
}

void BackTrace::easyPrint(const unsigned int maxFrames)
{
	// Array to store the stack frames
	void *addrlist[maxFrames + 1];

	// Get the backtrace frames
	const int num_frames = backtrace(addrlist, sizeof(addrlist) / sizeof(void *));

	if (!num_frames) {
		std::cerr << "No stack trace available\n";
		return;
	}

	// Get symbolic names for the frames
	char **symbollist = backtrace_symbols(addrlist, num_frames);

	for (int i = 1; i < num_frames; i++) { // Skip the first frame as it is this function itself
		char *begin_name = nullptr;
		char *begin_offset = nullptr;
		char *end_offset = nullptr;

		// Find parentheses and the + offset surrounded by parentheses
		for (char *p = symbollist[i]; *p; p++) {
			if (*p == '(') {
				begin_name = p;
			} else if (*p == '+') {
				begin_offset = p;
			} else if (*p == ')') {
				end_offset = p;
				break;
			}
		}

		if (begin_name && begin_offset && end_offset && begin_name < begin_offset) {
			char *func_name = nullptr;
			*begin_name++ = '\0';
			*begin_offset++ = '\0';
			*end_offset = '\0';

			// Demangle the function name
			int status = 0;
			func_name = abi::__cxa_demangle(begin_name, nullptr, nullptr, &status);

			if (!status) {
				printf("[%d] %s : %s+%s\n", i, symbollist[i], func_name, begin_offset);
			} else {
				// If demangling failed, print the mangled function name
				printf("[%d] %s : %s+%s\n", i, symbollist[i], begin_name, begin_offset);
			}
			free(func_name);
		} else {
			// Print the whole line if no parentheses or + sign found
			printf("[%d] %s\n", i, symbollist[i]);
		}
	}

	free(symbollist);
}

std::span<void *> backtrace_stripes(const int max_frames)
{
	// Array to store the stack frames
	void** addrlist = new void *[max_frames](nullptr);

	// Get the backtrace frames
	const int num_frames = backtrace(addrlist, max_frames);

	if (!num_frames) {
		delete [] addrlist;
		std::cerr << "Failed to generate backtrace.\n";
		return std::span<void *, 0>();
	}

	return std::span(addrlist, num_frames);
}

std::span<BackTraceEntry> backtrace_entries(int max_frames)
{
    // Array to store the stack frames
    void* addrlist[max_frames + 1];

    // Get the backtrace frames
    const int num_frames = backtrace(addrlist, sizeof(addrlist) / sizeof(void*));

    if (!num_frames) {
        std::cerr << "Failed to generate backtrace.\n";
        return std::span<BackTraceEntry, 0>();
    }

    // Get symbolic names for the frames
    char** symbollist = backtrace_symbols(addrlist, num_frames);
    auto entries = std::span(new BackTraceEntry[num_frames], num_frames);

    for (int i = 1; i < num_frames; i++) { // Skip the first frame (this function itself)
        const int j = i - 1;
        entries[j].frame = j;
        entries[j].source = symbollist[i];

        char *begin_name = nullptr, *begin_offset = nullptr, *end_offset = nullptr;
        for (char* p = symbollist[i]; *p; ++p) {
            if (*p == '(') begin_name = p;
            else if (*p == '+') begin_offset = p;
            else if (*p == ')') { end_offset = p; break; }
        }

        if (begin_name && begin_offset && end_offset && begin_name < begin_offset) {
            *begin_name++ = '\0';
            *begin_offset++ = '\0';
            *end_offset = '\0';

            // Demangle the function name
            int status = 0;
            char* func_name = abi::__cxa_demangle(begin_name, nullptr, nullptr, &status);

            entries[j].offset = std::strtoll(begin_offset, nullptr, 16);

            if (!status && func_name) {
                entries[j].symbol = func_name;
            } else {
                // Fallback to addr2line when demangling or parsing fails
                entries[j].symbol = addr2line_resolve(addrlist[i]);
            }

            free(func_name);
        } else {
            // No usable symbol info — use addr2line as fallback
            entries[j].offset = -1;
            entries[j].symbol = addr2line_resolve(addrlist[i]);
        }
    }

    free(symbollist);
    return entries;
}

std::span<BackTraceEntry> convert_stripes(const std::span<void *> &stripes)
{
    if (stripes.empty()) {
        return std::span<BackTraceEntry, 0>();
    }

    // Get symbolic names for the frames
    char** symbollist = backtrace_symbols(stripes.data(), stripes.size());
    auto entries = std::span(new BackTraceEntry[stripes.size()], stripes.size());

    for (size_t i = 1; i < stripes.size(); i++) { // Skip the first frame (this function itself)
        const int j = i - 1;
        entries[j].frame = j;
        entries[j].source = symbollist[i];

        char *begin_name = nullptr, *begin_offset = nullptr, *end_offset = nullptr;
        for (char* p = symbollist[i]; *p; ++p) {
            if (*p == '(') begin_name = p;
            else if (*p == '+') begin_offset = p;
            else if (*p == ')') { end_offset = p; break; }
        }

        if (begin_name && begin_offset && end_offset && begin_name < begin_offset) {
            *begin_name++ = '\0';
            *begin_offset++ = '\0';
            *end_offset = '\0';

            // Demangle the function name
            int status = 0;
            char* func_name = abi::__cxa_demangle(begin_name, nullptr, nullptr, &status);

            entries[j].offset = std::strtoll(begin_offset, nullptr, 16);

            if (!status && func_name) {
                entries[j].symbol = func_name;
            } else {
                // Fallback to addr2line when demangling or parsing fails
                entries[j].symbol = addr2line_resolve(stripes[i]);
            }

            free(func_name);
        } else {
            // No usable symbol info — use addr2line as fallback
            entries[j].offset = -1;
            entries[j].symbol = addr2line_resolve(stripes[i]);
        }
    }

    free(symbollist);
    return entries;
}


BackTrace::BackTrace(const unsigned int maxFrames)
	: stripes(backtrace_stripes(maxFrames))
	, bt(entries.data(), [](const BackTraceEntry *d) {delete [] d;})
{
}

BackTrace::BackTrace(const BackTrace &other)
	: stripes(other.stripes)
	, bt(other.bt)
{
}

void BackTrace::generate()
{
	entries = convert_stripes(stripes);
}

void BackTrace::print()
{
	if (stripes.empty())
	{
		std::cout << "[MISSING]" << std::endl;
		return;
	}

	if (entries.empty())
	{
		generate();
	}

	for (const auto &e : entries) {
		std::cout << '[' << e.frame << "] " << e.source << " : " << e.symbol << std::hex << std::showpos << std::showbase << e.offset << std::endl;
	}
}
