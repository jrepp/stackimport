#pragma once

#include "stackimport_platform_internal.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

class StackImportRapidJsonAllocator {
public:
	static const bool kNeedFree = true;

	void* Malloc(size_t size)
	{
		if(size == 0)
			return nullptr;
		void* ptr = allocate_with_header(size);
		if(!ptr)
			stackimport_internal_note_allocation_failure();
		return ptr;
	}

	void* Realloc(void* originalPtr, size_t originalSize, size_t newSize)
	{
		if(newSize == 0)
		{
			Free(originalPtr);
			return nullptr;
		}
		void* newPtr = allocate_with_header(newSize);
		if(!newPtr)
		{
			stackimport_internal_note_allocation_failure();
			return nullptr;
		}
		if(newPtr && originalPtr && originalSize > 0)
			std::memcpy(newPtr, originalPtr, std::min(originalSize, newSize));
		Free(originalPtr);
		return newPtr;
	}

	static void Free(void* ptr)
	{
		if(!ptr)
			return;
		auto* header = reinterpret_cast<Header*>(static_cast<unsigned char*>(ptr) - sizeof(Header));
		header->deallocate(header, header->user_data);
	}

private:
	struct Header {
		stackimport_deallocate_fn deallocate;
		void* user_data;
	};

	static void* allocate_with_header(size_t size)
	{
		const auto& platform = stackimport_current_internal_platform();
		void* raw = platform.allocate(sizeof(Header) + size, alignof(Header), platform.user_data);
		if(!raw)
			return nullptr;
		auto* header = static_cast<Header*>(raw);
		header->deallocate = platform.deallocate;
		header->user_data = platform.user_data;
		return static_cast<unsigned char*>(raw) + sizeof(Header);
	}
};
