#ifndef INCLUDED_COMMON_STORAGE
#define INCLUDED_COMMON_STORAGE

#if USE_SPIRV
#if USE_DESCRIPTOR_INDEXING
#define STORAGE_BUFFER(LOCATION) \
	layout(std430, set = 2, binding = LOCATION)
#else
#define STORAGE_BUFFER(LOCATION) \
	layout(std430, set = 1, binding = LOCATION)
#endif
#else
#define STORAGE_BUFFER(LOCATION) \
	layout(std430, binding = LOCATION)
#endif

#endif // INCLUDED_COMMON_STORAGE
