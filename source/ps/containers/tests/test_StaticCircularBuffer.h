/* Copyright (C) 2026 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "lib/self_test.h"

#include "ps/containers/StaticCircularBuffer.h"

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <iterator>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#include <memory>
#include <string>

class TestStaticCircularBuffer : public CxxTest::TestSuite
{
public:
	class ConstructionCounter
	{
	public:
		ConstructionCounter(size_t& count) :
			m_Count{count}
		{
			++m_Count;
		}

		ConstructionCounter(const ConstructionCounter& other) :
			m_Count{other.m_Count}
		{
			++m_Count;
		}

		ConstructionCounter& operator=(const ConstructionCounter&)
		{
			return *this;
		}

		ConstructionCounter(ConstructionCounter&& other) :
			m_Count{other.m_Count}
		{
			++m_Count;
		}

		ConstructionCounter& operator=(ConstructionCounter&&)
		{
			return *this;
		}

		~ConstructionCounter()
		{
			--m_Count;
		}

		friend bool operator==(const ConstructionCounter&, const ConstructionCounter&)
		{
			return true;
		}

	private:
		size_t& m_Count;
	};

	void test_basic_operations()
	{
		PS::StaticCircularBuffer<int, 5> buffer;

		TS_ASSERT(buffer.empty());
		TS_ASSERT(!buffer.full());
		TS_ASSERT_EQUALS(buffer.size(), 0);
		TS_ASSERT_EQUALS(buffer.capacity(), 5);

		// Push elements
		buffer.push_back(1);
		buffer.push_back(2);
		buffer.push_back(3);

		TS_ASSERT(!buffer.empty());
		TS_ASSERT(!buffer.full());
		TS_ASSERT_EQUALS(buffer.size(), 3);
		TS_ASSERT_EQUALS(buffer.front(), 1);
		TS_ASSERT_EQUALS(buffer.back(), 3);

		// Access by index
		TS_ASSERT_EQUALS(buffer[0], 1);
		TS_ASSERT_EQUALS(buffer[1], 2);
		TS_ASSERT_EQUALS(buffer[2], 3);

		// Pop front
		buffer.pop_front();
		TS_ASSERT_EQUALS(buffer.size(), 2);
		TS_ASSERT_EQUALS(buffer.front(), 2);
		TS_ASSERT_EQUALS(buffer.back(), 3);

		// Pop back
		buffer.pop_back();
		TS_ASSERT_EQUALS(buffer.size(), 1);
		TS_ASSERT_EQUALS(buffer.front(), 2);
		TS_ASSERT_EQUALS(buffer.back(), 2);

		// Clear
		buffer.clear();
		TS_ASSERT(buffer.empty());
		TS_ASSERT_EQUALS(buffer.size(), 0);
	}

	void test_circular_behavior()
	{
		PS::StaticCircularBuffer<int, 3> buffer;

		// Fill buffer
		buffer.push_back(1);
		buffer.push_back(2);
		buffer.push_back(3);

		TS_ASSERT(buffer.full());
		TS_ASSERT_EQUALS(buffer.size(), 3);
		TS_ASSERT_EQUALS(buffer[0], 1);
		TS_ASSERT_EQUALS(buffer[1], 2);
		TS_ASSERT_EQUALS(buffer[2], 3);

		// Push when full - should overwrite oldest
		buffer.push_back(4);
		TS_ASSERT(buffer.full());
		TS_ASSERT_EQUALS(buffer.size(), 3);
		TS_ASSERT_EQUALS(buffer[0], 2); // 1 was overwritten
		TS_ASSERT_EQUALS(buffer[1], 3);
		TS_ASSERT_EQUALS(buffer[2], 4);

		// Push another
		buffer.push_back(5);
		TS_ASSERT_EQUALS(buffer[0], 3);
		TS_ASSERT_EQUALS(buffer[1], 4);
		TS_ASSERT_EQUALS(buffer[2], 5);

		// Use push_back alias
		buffer.push_back(6);
		TS_ASSERT_EQUALS(buffer[0], 4);
		TS_ASSERT_EQUALS(buffer[1], 5);
		TS_ASSERT_EQUALS(buffer[2], 6);
	}

	void test_construction_and_destruction()
	{
		size_t count{0};
		{
			PS::StaticCircularBuffer<ConstructionCounter, 5> buffer;
			TS_ASSERT_EQUALS(count, 0);

			buffer.emplace_back(count);
			TS_ASSERT_EQUALS(count, 1);

			{
				ConstructionCounter lval{count};
				buffer.push_back(lval);
			}
			TS_ASSERT_EQUALS(count, 2);

			buffer.emplace_back(count);
			TS_ASSERT_EQUALS(count, 3);

			// Fill buffer
			buffer.emplace_back(count);
			buffer.emplace_back(count);
			TS_ASSERT_EQUALS(count, 5);
			TS_ASSERT(buffer.full());

			// Overwrite oldest - should destroy one element
			buffer.emplace_back(count);
			TS_ASSERT_EQUALS(count, 5); // Count stays same - one destroyed, one created

			buffer.clear();
			TS_ASSERT_EQUALS(count, 0);
		}
		TS_ASSERT_EQUALS(count, 0);
	}

	void test_copy_and_move()
	{
		size_t count{0};

		// Test copy constructor
		{
			PS::StaticCircularBuffer<ConstructionCounter, 5> buffer1;
			buffer1.emplace_back(count);
			buffer1.emplace_back(count);
			buffer1.emplace_back(count);
			TS_ASSERT_EQUALS(count, 3);

			PS::StaticCircularBuffer<ConstructionCounter, 5> buffer2(buffer1);
			TS_ASSERT_EQUALS(count, 6);
			TS_ASSERT_EQUALS(buffer1.size(), 3);
			TS_ASSERT_EQUALS(buffer2.size(), 3);
		}
		TS_ASSERT_EQUALS(count, 0);

		// Test move constructor
		{
			PS::StaticCircularBuffer<ConstructionCounter, 5> buffer1;
			buffer1.emplace_back(count);
			buffer1.emplace_back(count);
			TS_ASSERT_EQUALS(count, 2);

			PS::StaticCircularBuffer<ConstructionCounter, 5> buffer2(std::move(buffer1));
			TS_ASSERT_EQUALS(count, 2);
			TS_ASSERT_EQUALS(buffer1.size(), 0); // Moved-from is empty
			TS_ASSERT_EQUALS(buffer2.size(), 2);
		}
		TS_ASSERT_EQUALS(count, 0);

		// Test copy assignment
		{
			PS::StaticCircularBuffer<ConstructionCounter, 5> buffer1;
			buffer1.emplace_back(count);
			buffer1.emplace_back(count);

			PS::StaticCircularBuffer<ConstructionCounter, 5> buffer2;
			buffer2.emplace_back(count);

			buffer2 = buffer1;
			TS_ASSERT_EQUALS(count, 4); // One from buffer2 was destroyed, two new ones created
			TS_ASSERT_EQUALS(buffer2.size(), 2);
		}
		TS_ASSERT_EQUALS(count, 0);

		// Test move assignment
		{
			PS::StaticCircularBuffer<ConstructionCounter, 5> buffer1;
			buffer1.emplace_back(count);
			buffer1.emplace_back(count);

			PS::StaticCircularBuffer<ConstructionCounter, 5> buffer2;
			buffer2.emplace_back(count);

			buffer2 = std::move(buffer1);
			TS_ASSERT_EQUALS(count, 2); // One from buffer2 was destroyed, two moved
			TS_ASSERT_EQUALS(buffer1.size(), 0);
			TS_ASSERT_EQUALS(buffer2.size(), 2);
		}
		TS_ASSERT_EQUALS(count, 0);
	}

	void test_iterators()
	{
		PS::StaticCircularBuffer<int, 5> buffer;

		// Test empty buffer
		TS_ASSERT(buffer.begin() == buffer.end());
		TS_ASSERT(buffer.cbegin() == buffer.cend());
		TS_ASSERT(buffer.rbegin() == buffer.rend());

		// Add elements
		buffer.push_back(1);
		buffer.push_back(2);
		buffer.push_back(3);

		// Forward iteration
		int sum = 0;
		for (auto it = buffer.begin(); it != buffer.end(); ++it)
			sum += *it;
		TS_ASSERT_EQUALS(sum, 6);

		// Range-based for loop
		sum = 0;
		for (int val : buffer)
			sum += val;
		TS_ASSERT_EQUALS(sum, 6);

		// Const iteration
		sum = 0;
		for (auto it = buffer.cbegin(); it != buffer.cend(); ++it)
			sum += *it;
		TS_ASSERT_EQUALS(sum, 6);

		// Reverse iteration
		std::vector<int> reversed;
		for (auto it = buffer.rbegin(); it != buffer.rend(); ++it)
			reversed.push_back(*it);
		TS_ASSERT_EQUALS(reversed.size(), 3);
		TS_ASSERT_EQUALS(reversed[0], 3);
		TS_ASSERT_EQUALS(reversed[1], 2);
		TS_ASSERT_EQUALS(reversed[2], 1);

		// Test iterator arithmetic
		auto it = buffer.begin();
		TS_ASSERT_EQUALS(*(it + 1), 2);
		TS_ASSERT_EQUALS(*(it + 2), 3);
		TS_ASSERT_EQUALS(it[1], 2);
		TS_ASSERT_EQUALS(it[2], 3);

		// Test with circular wrap
		buffer.push_back(4);
		buffer.push_back(5);
		buffer.push_back(6); // Overwrites 1

		std::vector<int> values(buffer.begin(), buffer.end());
		TS_ASSERT_EQUALS(values.size(), 5);
		TS_ASSERT_EQUALS(values[0], 2); // Oldest
		TS_ASSERT_EQUALS(values[4], 6); // Newest
	}

	void test_exceptions()
	{
		PS::StaticCircularBuffer<int, 3> buffer;

		// Test at() with empty buffer
		TS_ASSERT_THROWS(buffer.at(0), std::out_of_range&);

		// Test at() with out of bounds
		buffer.push_back(1);
		buffer.push_back(2);
		TS_ASSERT_THROWS(buffer.at(2), std::out_of_range&);
		TS_ASSERT_THROWS(buffer.at(5), std::out_of_range&);

		// Test pop_front on empty
		buffer.clear();
		TS_ASSERT_THROWS(buffer.pop_front(), std::out_of_range&);

		// Test pop_back on empty
		TS_ASSERT_THROWS(buffer.pop_back(), std::out_of_range&);

		// Test operator[] - should not throw (debug assertion only)
		// We can't test ASSERT failures easily in unit tests
	}

	void test_algorithm_compatibility()
	{
		PS::StaticCircularBuffer<int, 10> buffer;

		// Fill with values
		for (int i = 0; i < 7; ++i)
			buffer.push_back(i * 2);

		// Test with std::copy_if
		std::vector<int> even_values;
		std::copy_if(buffer.begin(), buffer.end(), std::back_inserter(even_values),
			[](int x) { return x % 4 == 0; });

		TS_ASSERT_EQUALS(even_values.size(), 4); // 0, 4, 8, 12
		TS_ASSERT_EQUALS(even_values[0], 0);
		TS_ASSERT_EQUALS(even_values[3], 12);

		// Test with std::find
		auto it = std::find(buffer.begin(), buffer.end(), 6);
		TS_ASSERT(it != buffer.end());
		TS_ASSERT_EQUALS(*it, 6);

		// Test with std::accumulate
		int sum = std::accumulate(buffer.begin(), buffer.end(), 0);
		TS_ASSERT_EQUALS(sum, 0 + 2 + 4 + 6 + 8 + 10 + 12);

		// Test with std::transform
		std::vector<int> doubled;
		std::transform(buffer.begin(), buffer.end(), std::back_inserter(doubled),
			[](int x) { return x * 2; });

		TS_ASSERT_EQUALS(doubled.size(), 7);
		TS_ASSERT_EQUALS(doubled[0], 0);
		TS_ASSERT_EQUALS(doubled[6], 24);
	}

	void test_edge_cases()
	{
		// Test with capacity 1
		{
			PS::StaticCircularBuffer<int, 1> buffer;
			TS_ASSERT(buffer.empty());

			buffer.push_back(42);
			TS_ASSERT(buffer.full());
			TS_ASSERT_EQUALS(buffer.front(), 42);
			TS_ASSERT_EQUALS(buffer.back(), 42);
			TS_ASSERT_EQUALS(buffer[0], 42);

			buffer.push_back(99);
			TS_ASSERT_EQUALS(buffer.front(), 99);
			TS_ASSERT_EQUALS(buffer.back(), 99);
		}

		// Test wrap-around multiple times
		{
			PS::StaticCircularBuffer<int, 3> buffer;

			// Fill and overwrite multiple times
			for (int i = 0; i < 10; ++i)
				buffer.push_back(i);

			// After 10 pushes to size-3 buffer, should have 7, 8, 9
			TS_ASSERT_EQUALS(buffer.size(), 3);
			TS_ASSERT_EQUALS(buffer[0], 7);
			TS_ASSERT_EQUALS(buffer[1], 8);
			TS_ASSERT_EQUALS(buffer[2], 9);
		}

		// Test mixed push/pop operations
		{
			PS::StaticCircularBuffer<int, 4> buffer;

			buffer.push_back(1);
			buffer.push_back(2);
			buffer.pop_front();
			TS_ASSERT_EQUALS(buffer.size(), 1);
			TS_ASSERT_EQUALS(buffer.front(), 2);

			buffer.push_back(3);
			buffer.push_back(4);
			buffer.push_back(5); // Buffer now full: 2, 3, 4, 5

			buffer.pop_front(); // Remove 2
			buffer.push_back(6); // Add 6: 3, 4, 5, 6

			TS_ASSERT_EQUALS(buffer.size(), 4);
			TS_ASSERT_EQUALS(buffer[0], 3);
			TS_ASSERT_EQUALS(buffer[3], 6);
		}
	}

	void test_emplace_back_variadic()
	{
		// Test emplace_back with multiple constructor arguments
		struct MultiArg
		{
			int a;
			float b;
			std::string c;

			MultiArg(int x, float y, std::string z) : a(x), b(y), c(std::move(z)) {}
		};

		PS::StaticCircularBuffer<MultiArg, 4> buffer;

		// Test emplace_back with multiple arguments
		buffer.emplace_back(1, 2.5f, "first");
		TS_ASSERT_EQUALS(buffer.size(), 1);
		TS_ASSERT_EQUALS(buffer[0].a, 1);
		TS_ASSERT_DELTA(buffer[0].b, 2.5f, 0.001f);
		TS_ASSERT_EQUALS(buffer[0].c, "first");

		buffer.emplace_back(42, 3.14f, "pi");
		TS_ASSERT_EQUALS(buffer.size(), 2);
		TS_ASSERT_EQUALS(buffer[1].a, 42);
		TS_ASSERT_DELTA(buffer[1].b, 3.14f, 0.001f);
		TS_ASSERT_EQUALS(buffer[1].c, "pi");

		// Test emplace_back with overwriting
		buffer.emplace_back(100, 1.0f, "full");
		buffer.emplace_back(200, 2.0f, "buffer");
		TS_ASSERT(buffer.full());

		// Overwrite the first element
		buffer.emplace_back(300, 3.0f, "overwrite");
		TS_ASSERT_EQUALS(buffer.size(), 4);
		// First element was overwritten
		TS_ASSERT_EQUALS(buffer[0].a, 42);
		TS_ASSERT_DELTA(buffer[0].b, 3.14f, 0.001f);
		TS_ASSERT_EQUALS(buffer[0].c, "pi");
		// New element
		TS_ASSERT_EQUALS(buffer[3].a, 300);
		TS_ASSERT_DELTA(buffer[3].b, 3.0f, 0.001f);
		TS_ASSERT_EQUALS(buffer[3].c, "overwrite");

		// Test emplace_back with move-only arguments
		struct MoveOnlyArg
		{
			std::unique_ptr<int> ptr;

			explicit MoveOnlyArg(std::unique_ptr<int> p) : ptr(std::move(p)) {}

			MoveOnlyArg(MoveOnlyArg&&) = default;
			MoveOnlyArg& operator=(MoveOnlyArg&&) = default;

			MoveOnlyArg(const MoveOnlyArg&) = delete;
			MoveOnlyArg& operator=(const MoveOnlyArg&) = delete;

			int value() const { return ptr ? *ptr : -1; }
		};

		PS::StaticCircularBuffer<MoveOnlyArg, 3> moveBuffer;

		moveBuffer.emplace_back(std::make_unique<int>(42));
		TS_ASSERT_EQUALS(moveBuffer.size(), 1);
		TS_ASSERT_EQUALS(moveBuffer[0].value(), 42);

		moveBuffer.emplace_back(std::make_unique<int>(100));
		TS_ASSERT_EQUALS(moveBuffer.size(), 2);
		TS_ASSERT_EQUALS(moveBuffer[1].value(), 100);

		// Test overwriting move-only type
		moveBuffer.emplace_back(std::make_unique<int>(200));
		moveBuffer.emplace_back(std::make_unique<int>(300)); // Should overwrite first
		TS_ASSERT_EQUALS(moveBuffer.size(), 3);
		TS_ASSERT_EQUALS(moveBuffer[0].value(), 100); // First element was overwritten
		TS_ASSERT_EQUALS(moveBuffer[2].value(), 300);
	}

	void test_push_back_methods()
	{
		PS::StaticCircularBuffer<int, 3> buffer;

		// Test push_back(const T&) with lvalue
		int value1 = 42;
		buffer.push_back(value1);
		TS_ASSERT_EQUALS(buffer.size(), 1);
		TS_ASSERT_EQUALS(buffer[0], 42);

		int value2 = 100;
		buffer.push_back(value2);
		TS_ASSERT_EQUALS(buffer.size(), 2);
		TS_ASSERT_EQUALS(buffer[1], 100);

		// Test push_back(T&&) with rvalue
		buffer.push_back(200); // rvalue
		TS_ASSERT_EQUALS(buffer.size(), 3);
		TS_ASSERT(buffer.full());
		TS_ASSERT_EQUALS(buffer[2], 200);

		// Test push_back with overwriting (using const T&)
		int value3 = 300;
		buffer.push_back(value3);
		TS_ASSERT_EQUALS(buffer.size(), 3);
		TS_ASSERT_EQUALS(buffer[0], 100); // First element (42) was overwritten
		TS_ASSERT_EQUALS(buffer[1], 200);
		TS_ASSERT_EQUALS(buffer[2], 300);

		// Test push_back with overwriting (using T&&)
		buffer.push_back(400); // rvalue
		TS_ASSERT_EQUALS(buffer.size(), 3);
		TS_ASSERT_EQUALS(buffer[0], 200);
		TS_ASSERT_EQUALS(buffer[1], 300);
		TS_ASSERT_EQUALS(buffer[2], 400);

		// Test that push_back and push_back are truly aliases
		PS::StaticCircularBuffer<int, 2> buffer2;
		buffer2.push_back(1);
		buffer2.push_back(2);

		// Both should produce identical results
		PS::StaticCircularBuffer<int, 2> buffer3;
		buffer3.push_back(1);
		buffer3.push_back(2);

		TS_ASSERT_EQUALS(buffer2.size(), buffer3.size());
		TS_ASSERT_EQUALS(buffer2[0], buffer3[0]);
		TS_ASSERT_EQUALS(buffer2[1], buffer3[1]);

		// Test with custom type
		struct CustomType
		{
			int id;
			std::string name;

			CustomType(int i, std::string n) : id(i), name(std::move(n)) {}
		};

		PS::StaticCircularBuffer<CustomType, 2> customBuffer;
		CustomType item1(1, "first");

		customBuffer.push_back(item1);
		TS_ASSERT_EQUALS(customBuffer.size(), 1);
		TS_ASSERT_EQUALS(customBuffer[0].id, 1);
		TS_ASSERT_EQUALS(customBuffer[0].name, "first");

		// Test rvalue push_back with custom type
		customBuffer.push_back(CustomType(2, "second"));
		TS_ASSERT_EQUALS(customBuffer.size(), 2);
		TS_ASSERT_EQUALS(customBuffer[1].id, 2);
		TS_ASSERT_EQUALS(customBuffer[1].name, "second");

		// Test overwriting with push_back
		CustomType item3(3, "third");
		customBuffer.push_back(item3);
		TS_ASSERT_EQUALS(customBuffer.size(), 2);
		TS_ASSERT_EQUALS(customBuffer[0].id, 2); // First element was overwritten
		TS_ASSERT_EQUALS(customBuffer[0].name, "second");
		TS_ASSERT_EQUALS(customBuffer[1].id, 3);
		TS_ASSERT_EQUALS(customBuffer[1].name, "third");
	}

	void test_emplace_back_edge_cases()
	{
		// Test emplace_back with zero arguments (default construction)
		struct DefaultConstructible
		{
			int value = 42;
			std::string text = "default";

			DefaultConstructible() = default;

			explicit DefaultConstructible(int v, std::string t = "")
				: value(v), text(std::move(t)) {}
		};

		PS::StaticCircularBuffer<DefaultConstructible, 3> buffer;

		// Default construction via emplace_back()
		buffer.emplace_back();
		TS_ASSERT_EQUALS(buffer.size(), 1);
		TS_ASSERT_EQUALS(buffer[0].value, 42);
		TS_ASSERT_EQUALS(buffer[0].text, "default");

		// Construction with some arguments
		buffer.emplace_back(100, "custom");
		TS_ASSERT_EQUALS(buffer.size(), 2);
		TS_ASSERT_EQUALS(buffer[1].value, 100);
		TS_ASSERT_EQUALS(buffer[1].text, "custom");

		// Test emplace_back with perfect forwarding of references
		struct ReferenceWrapper
		{
			const int& ref;
			explicit ReferenceWrapper(const int& r) : ref(r) {}
		};

		PS::StaticCircularBuffer<ReferenceWrapper, 2> refBuffer;
		int externalValue = 42;

		// Should store a reference, not a copy
		refBuffer.emplace_back(externalValue);
		TS_ASSERT_EQUALS(refBuffer.size(), 1);
		TS_ASSERT_EQUALS(refBuffer[0].ref, 42);

		externalValue = 100;
		// Reference should reflect the change
		TS_ASSERT_EQUALS(refBuffer[0].ref, 100);

		// Test emplace_back with initializer_list argument
		struct HasInitializerListCtor
		{
			std::vector<int> values;

			HasInitializerListCtor(std::initializer_list<int> init)
				: values(init.begin(), init.end()) {}
		};

		PS::StaticCircularBuffer<HasInitializerListCtor, 2> ilBuffer;

		// Create a temporary object
		ilBuffer.emplace_back(std::initializer_list<int>{1, 2, 3, 4, 5});
		TS_ASSERT_EQUALS(ilBuffer.size(), 1);
		TS_ASSERT_EQUALS(ilBuffer[0].values.size(), 5);
		TS_ASSERT_EQUALS(ilBuffer[0].values[0], 1);
		TS_ASSERT_EQUALS(ilBuffer[0].values[4], 5);

		// Use push_back with a temporary
		ilBuffer.push_back(HasInitializerListCtor{10, 20}); // Should not overwrite yet
		TS_ASSERT_EQUALS(ilBuffer.size(), 2);

		// Use emplace_back with explicit initializer_list - should overwrite first
		ilBuffer.emplace_back(std::initializer_list<int>{100});
		TS_ASSERT_EQUALS(ilBuffer.size(), 2);
		TS_ASSERT_EQUALS(ilBuffer[0].values.size(), 2); // First element was {10, 20}
		TS_ASSERT_EQUALS(ilBuffer[0].values[0], 10);
		TS_ASSERT_EQUALS(ilBuffer[0].values[1], 20);
		TS_ASSERT_EQUALS(ilBuffer[1].values.size(), 1); // New element is {100}
		TS_ASSERT_EQUALS(ilBuffer[1].values[0], 100);

		// Test with mixed argument types to ensure perfect forwarding works
		struct MixedArgs
		{
			int a;
			double b;
			const char* c;

			MixedArgs(int x, double y, const char* z) : a(x), b(y), c(z) {}
		};

		PS::StaticCircularBuffer<MixedArgs, 2> mixedBuffer;

		// Test perfect forwarding with different argument types
		int x = 5;
		double y = 3.14;
		const char* z = "test";

		mixedBuffer.emplace_back(x, y, z);
		TS_ASSERT_EQUALS(mixedBuffer.size(), 1);
		TS_ASSERT_EQUALS(mixedBuffer[0].a, 5);
		TS_ASSERT_DELTA(mixedBuffer[0].b, 3.14, 0.001);
		TS_ASSERT_EQUALS(mixedBuffer[0].c, z); // Should be same pointer

		// Test with literal arguments
		mixedBuffer.emplace_back(10, 2.71, "another");
		TS_ASSERT_EQUALS(mixedBuffer.size(), 2);
		TS_ASSERT_EQUALS(mixedBuffer[1].a, 10);
		TS_ASSERT_DELTA(mixedBuffer[1].b, 2.71, 0.001);
		TS_ASSERT_EQUALS(std::string(mixedBuffer[1].c), "another");
	}

	void test_return_references()
	{
		// Test that emplace_back returns a reference to the constructed element
		PS::StaticCircularBuffer<std::string, 3> buffer;

		// Test emplace_back returns reference
		std::string& ref1 = buffer.emplace_back("hello");
		TS_ASSERT_EQUALS(ref1, "hello");
		ref1 += " world";
		TS_ASSERT_EQUALS(buffer[0], "hello world");

		// Test push_back returns reference
		std::string value = "test";
		std::string& ref2 = buffer.push_back(value);
		TS_ASSERT_EQUALS(ref2, "test");
		ref2 += "ing";
		TS_ASSERT_EQUALS(buffer[1], "testing");

		// Test push_back returns reference
		std::string& ref3 = buffer.push_back("foo");
		TS_ASSERT_EQUALS(ref3, "foo");
		ref3 = "bar";
		TS_ASSERT_EQUALS(buffer[2], "bar");

		// Test with overwriting - should still return reference
		// Buffer now contains: ["hello world", "testing", "bar"]
		std::string& ref4 = buffer.push_back("new");
		TS_ASSERT_EQUALS(ref4, "new");
		ref4 += " element";
		// The newest element is at logical index 2 (since oldest was overwritten)
		TS_ASSERT_EQUALS(buffer[2], "new element");
		// Verify the oldest element was indeed overwritten
		TS_ASSERT_EQUALS(buffer[0], "testing");
		TS_ASSERT_EQUALS(buffer[1], "bar");
	}

	void test_iterator_spaceship_operator()
	{
		PS::StaticCircularBuffer<int, 5> buffer;
		buffer.push_back(1);
		buffer.push_back(2);
		buffer.push_back(3);

		auto it1 = buffer.begin();
		auto it2 = buffer.begin();
		auto it3 = buffer.begin() + 1;
		auto it4 = buffer.begin() + 2;
		auto cit = buffer.cbegin();

		// Test equality (still needed even with spaceship operator)
		TS_ASSERT(it1 == it2);
		TS_ASSERT(it1 != it3);

		// Test three-way comparison operators
		TS_ASSERT(it1 < it3);
		TS_ASSERT(it1 <= it2);
		TS_ASSERT(it1 <= it3);
		TS_ASSERT(it3 > it1);
		TS_ASSERT(it3 >= it1);
		TS_ASSERT(it4 >= it3);

		// Test full spaceship comparisons
		TS_ASSERT((it1 <=> it2) == std::partial_ordering::equivalent);
		TS_ASSERT((it1 <=> it3) == std::partial_ordering::less);
		TS_ASSERT((it3 <=> it1) == std::partial_ordering::greater);

		// Test compatibility between const and non-const iterators
		TS_ASSERT(it1 == cit);
		TS_ASSERT((it1 <=> cit) == std::partial_ordering::equivalent);

		// Test unordered comparison for different buffers
		PS::StaticCircularBuffer<int, 5> buffer2;
		buffer2.push_back(1);
		auto it5 = buffer2.begin();

		// Comparing iterators from different buffers should be unordered
		TS_ASSERT((it1 <=> it5) == std::partial_ordering::unordered);
	}

	void test_const_iterator_conversions()
	{
		PS::StaticCircularBuffer<int, 5> buffer;
		buffer.push_back(1);
		buffer.push_back(2);
		buffer.push_back(3);

		// Test conversion from iterator to const_iterator
		PS::StaticCircularBuffer<int, 5>::iterator it = buffer.begin();
		PS::StaticCircularBuffer<int, 5>::const_iterator cit = it;

		TS_ASSERT_EQUALS(*it, *cit);
		TS_ASSERT(it == cit);

		// Test that const_iterator can be incremented
		++cit;
		TS_ASSERT_EQUALS(*cit, 2);

		// Test reverse iterators
		auto rit = buffer.rbegin();
		auto crit = buffer.crbegin();

		TS_ASSERT_EQUALS(*rit, 3);
		TS_ASSERT_EQUALS(*crit, 3);
	}

	void test_iterator_subscript_operator()
	{
		PS::StaticCircularBuffer<int, 5> buffer;
		buffer.push_back(10);
		buffer.push_back(20);
		buffer.push_back(30);
		buffer.push_back(40);

		auto it = buffer.begin();

		// Test subscript operator on iterator
		TS_ASSERT_EQUALS(it[0], 10);
		TS_ASSERT_EQUALS(it[1], 20);
		TS_ASSERT_EQUALS(it[2], 30);
		TS_ASSERT_EQUALS(it[3], 40);

		// Test with offset
		auto it2 = it + 2;
		TS_ASSERT_EQUALS(it2[0], 30);
		TS_ASSERT_EQUALS(it2[-1], 20);  // Negative subscript
		TS_ASSERT_EQUALS(it2[-2], 10);
	}

	void test_iterator_arithmetic_operators()
	{
		PS::StaticCircularBuffer<int, 5> buffer;
		buffer.push_back(1);
		buffer.push_back(2);
		buffer.push_back(3);
		buffer.push_back(4);

		auto it = buffer.begin();

		// Test addition
		auto it2 = it + 2;
		TS_ASSERT_EQUALS(*it2, 3);

		// Test subtraction
		auto it3 = buffer.end() - 1;
		TS_ASSERT_EQUALS(*it3, 4);

		// Test difference between iterators
		TS_ASSERT_EQUALS(it2 - it, 2);
		TS_ASSERT_EQUALS(buffer.end() - buffer.begin(), 4);

		// Test compound assignment
		it += 3;
		TS_ASSERT_EQUALS(*it, 4);
		it -= 2;
		TS_ASSERT_EQUALS(*it, 2);

		// Test pre/post increment/decrement
		auto it4 = buffer.begin();
		TS_ASSERT_EQUALS(*(++it4), 2);
		TS_ASSERT_EQUALS(*(it4++), 2);
		TS_ASSERT_EQUALS(*it4, 3);

		auto it5 = buffer.begin() + 2;
		TS_ASSERT_EQUALS(*(--it5), 2);
		TS_ASSERT_EQUALS(*(it5--), 2);
		TS_ASSERT_EQUALS(*it5, 1);
	}

	void test_at_method_error_messages()
	{
		PS::StaticCircularBuffer<int, 3> buffer;

		// Test at() with empty buffer - should include size in error message
		try {
			buffer.at(0);
			TS_ASSERT(false); // Should not reach here
		} catch (const std::out_of_range& e) {
			std::string msg = e.what();
			// Check that error message contains size information
			TS_ASSERT(msg.find("size=0") != std::string::npos);
		}

		// Test at() with out of bounds - should include index and size
		buffer.push_back(1);
		buffer.push_back(2);

		try {
			buffer.at(2);
			TS_ASSERT(false); // Should not reach here
		} catch (const std::out_of_range& e) {
			std::string msg = e.what();
			// Check that error message contains both index and size
			TS_ASSERT(msg.find("index 2") != std::string::npos);
			TS_ASSERT(msg.find("size=2") != std::string::npos);
		}
	}

	void test_clear_with_partial_destruction()
	{
		size_t count{0};
		PS::StaticCircularBuffer<ConstructionCounter, 5> buffer;

		// Add elements to test wrap-around destruction
		buffer.emplace_back(count);
		buffer.emplace_back(count);
		buffer.emplace_back(count);
		TS_ASSERT_EQUALS(count, 3);

		// Move start to middle (simulate some pops)
		buffer.pop_front();
		buffer.pop_front();
		// Now start is at index 2, size is 1
		TS_ASSERT_EQUALS(count, 1);

		// Add more elements to cause wrap
		buffer.emplace_back(count);
		buffer.emplace_back(count);
		buffer.emplace_back(count);
		buffer.emplace_back(count); // This should wrap and overwrite
		TS_ASSERT_EQUALS(count, 5);

		// Clear should destroy all elements regardless of wrap
		buffer.clear();
		TS_ASSERT_EQUALS(count, 0);
		TS_ASSERT(buffer.empty());
		TS_ASSERT_EQUALS(buffer.size(), 0);
	}

	// Type traits tests
	static_assert(std::is_same_v<decltype(std::declval<PS::StaticCircularBuffer<int, 1>>().begin()),
		PS::StaticCircularBuffer<int, 1>::iterator>);
	static_assert(std::is_same_v<decltype(std::declval<const PS::StaticCircularBuffer<int, 1>>().begin()),
		PS::StaticCircularBuffer<int, 1>::const_iterator>);
	static_assert(std::is_same_v<decltype(std::declval<PS::StaticCircularBuffer<int, 1>>().end()),
		PS::StaticCircularBuffer<int, 1>::iterator>);
	static_assert(std::is_same_v<decltype(std::declval<const PS::StaticCircularBuffer<int, 1>>().end()),
		PS::StaticCircularBuffer<int, 1>::const_iterator>);
	static_assert(std::is_same_v<decltype(std::declval<PS::StaticCircularBuffer<int, 1>>().front()), int&>);
	static_assert(std::is_same_v<decltype(std::declval<const PS::StaticCircularBuffer<int, 1>>().front()),
		const int&>);

	// Size-types - should match StaticVector
	static_assert(std::is_same_v<PS::StaticCircularBuffer<int, 1>::size_type, uint_fast8_t>);
	static_assert(std::is_same_v<PS::StaticCircularBuffer<int, 180>::size_type, uint_fast8_t>);
	static_assert(std::is_same_v<PS::StaticCircularBuffer<int, 180>::difference_type, int_fast16_t>);
	static_assert(std::numeric_limits<PS::StaticCircularBuffer<int, 500>::size_type>::max() > 255);
	static_assert(std::numeric_limits<PS::StaticCircularBuffer<int, 1000>::difference_type>::min() < -128);
};