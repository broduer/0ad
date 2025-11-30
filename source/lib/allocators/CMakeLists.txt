target_sources(lowlevel
	PRIVATE
		DynamicArena.h
		STLAllocators.h
		allocator_checker.h
		allocator_policies.h
		dynarray.cpp
		dynarray.h
		freelist.cpp
		freelist.h
		overrun_protector.h
		pool.cpp
		pool.h
		shared_ptr.h
		stateless_allocators.h
)

if(NOT without-tests)
	add_subdirectory(tests/)
endif()
