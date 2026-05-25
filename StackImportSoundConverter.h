#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#include "include/stackimport_sax.hpp"
#include "stackimport_platform_internal.h"

namespace stackimport {

template <typename T>
class PlatformAllocator {
public:
	using value_type = T;

	PlatformAllocator() noexcept = default;

	template <typename U>
	PlatformAllocator(const PlatformAllocator<U>&) noexcept {}

	T* allocate(std::size_t count)
	{
		void* ptr = stackimport_internal_allocate(count * sizeof(T), alignof(T));
		if(!ptr)
			std::abort();
		return static_cast<T*>(ptr);
	}

	void deallocate(T* ptr, std::size_t) noexcept
	{
		const auto& platform = stackimport_current_internal_platform();
		stackimport_internal_deallocate(ptr, platform.deallocate, platform.user_data);
	}
};

template <typename T, typename U>
bool operator==(const PlatformAllocator<T>&, const PlatformAllocator<U>&) noexcept { return true; }

template <typename T, typename U>
bool operator!=(const PlatformAllocator<T>&, const PlatformAllocator<U>&) noexcept { return false; }

using PlatformByteVector = std::vector<uint8_t, PlatformAllocator<uint8_t>>;

bool ConvertSndResourceToWav(rsrcd::Bytes snd, PlatformByteVector& wav, std::string& error);

} // namespace stackimport
