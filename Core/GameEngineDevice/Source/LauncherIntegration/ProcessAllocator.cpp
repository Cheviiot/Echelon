// Echelon @refactor Codex 05/09/2026 Keep process allocation in the platform integration layer.
#include <cstdlib>
#include <cstring>
#include <new>
#if defined(_WIN32)
#include <malloc.h>
#endif

// GeneralsX @feature Codex 11/08/2026 Provide one zero-initializing process allocator across the host and both modules.
// Legacy game code relies on ordinary new returning cleared memory. Keeping this allocator in the executable also
// prevents objects allocated by libstdc++ or a static dependency from crossing incompatible module-local allocators.
void *operator new(std::size_t size)
{
	if (void *memory = std::calloc(size == 0 ? 1 : size, 1)) {
		return memory;
	}
	throw std::bad_alloc();
}

void *operator new[](std::size_t size)
{
	return ::operator new(size);
}

void operator delete(void *memory) noexcept
{
	std::free(memory);
}

void operator delete[](void *memory) noexcept
{
	std::free(memory);
}

void operator delete(void *memory, std::size_t) noexcept
{
	std::free(memory);
}

void operator delete[](void *memory, std::size_t) noexcept
{
	std::free(memory);
}

void *operator new(std::size_t size, std::align_val_t alignment)
{
	void *memory = nullptr;
	const std::size_t allocationSize = size == 0 ? 1 : size;
#if defined(_WIN32)
	memory = _aligned_malloc(allocationSize, static_cast<std::size_t>(alignment));
	if (memory) std::memset(memory, 0, allocationSize);
#else
	if (posix_memalign(&memory, static_cast<std::size_t>(alignment), allocationSize) == 0) {
		std::memset(memory, 0, allocationSize);
	}
#endif
	if (!memory) throw std::bad_alloc();
	return memory;
}

void *operator new[](std::size_t size, std::align_val_t alignment)
{
	return ::operator new(size, alignment);
}

void operator delete(void *memory, std::align_val_t) noexcept
{
#if defined(_WIN32)
	_aligned_free(memory);
#else
	std::free(memory);
#endif
}

void operator delete[](void *memory, std::align_val_t alignment) noexcept
{
	::operator delete(memory, alignment);
}

void operator delete(void *memory, std::size_t, std::align_val_t alignment) noexcept
{
	::operator delete(memory, alignment);
}

void operator delete[](void *memory, std::size_t, std::align_val_t alignment) noexcept
{
	::operator delete(memory, alignment);
}

