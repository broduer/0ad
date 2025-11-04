#ifndef CX_COMMON_BT_H
#define CX_COMMON_BT_H

#include <memory>
#include <span>
#include <string>


struct BackTraceEntry
{
	/// @brief ID of the frame from the bt, i.e.: 0, 1, 2...
	int frame = -1;
	/// @brief Source of the function, i.e.: /usr/lib/mylib.so.7
	std::string source {};
	/// @brief The symbol corresponding to the call, may be empty or mangled.
	std::string symbol {};
	/// @brief offset from the stack.
	long long offset = -1;
};

/**
 * @brief Class used to generate backtraces at runtime.
 * If you need to debug stuff at runtime, without a debugger, or for log report,
 * you may want to use this class.
 */
class BackTrace
{
public:
    explicit BackTrace(unsigned int maxFrames);
    BackTrace(const BackTrace &other);

	std::span<BackTraceEntry> entries {};
	const std::span<void *> stripes {};

	void generate();
	void print();

	static void easyPrint(const unsigned int maxFrames);

private:
	std::shared_ptr<BackTraceEntry> bt {};
};


#endif // BT_H
