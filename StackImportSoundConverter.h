#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>

#include "stackimport_platform_internal.h"
#include "rsrcd.hpp"

namespace stackimport {

class PlatformByteVector {
public:
	PlatformByteVector() = default;
	~PlatformByteVector() { release(); }

	PlatformByteVector(const PlatformByteVector&) = delete;
	PlatformByteVector& operator=(const PlatformByteVector&) = delete;

	PlatformByteVector(PlatformByteVector&& other) noexcept
		: data_(std::exchange(other.data_, nullptr))
		, size_(std::exchange(other.size_, 0))
		, capacity_(std::exchange(other.capacity_, 0))
		, deallocate_(std::exchange(other.deallocate_, nullptr))
		, user_data_(std::exchange(other.user_data_, nullptr))
		, failed_(std::exchange(other.failed_, false))
	{
	}

	PlatformByteVector& operator=(PlatformByteVector&& other) noexcept
	{
		if(this != &other)
		{
			release();
			data_ = std::exchange(other.data_, nullptr);
			size_ = std::exchange(other.size_, 0);
			capacity_ = std::exchange(other.capacity_, 0);
			deallocate_ = std::exchange(other.deallocate_, nullptr);
			user_data_ = std::exchange(other.user_data_, nullptr);
			failed_ = std::exchange(other.failed_, false);
		}
		return *this;
	}

	void clear() noexcept
	{
		size_ = 0;
		failed_ = false;
	}

	bool reserve(std::size_t capacity)
	{
		if(capacity <= capacity_)
			return true;
		uint8_t* newData = static_cast<uint8_t*>(stackimport_internal_allocate(capacity, alignof(uint8_t)));
		if(!newData)
		{
			stackimport_internal_note_allocation_failure();
			failed_ = true;
			return false;
		}
		const auto& platform = stackimport_current_internal_platform();
		if(size_ > 0)
			std::memcpy(newData, data_, size_);
		release();
		data_ = newData;
		capacity_ = capacity;
		deallocate_ = platform.deallocate;
		user_data_ = platform.user_data;
		return true;
	}

	bool push_back(uint8_t value)
	{
		if(size_ == capacity_ && !reserve(capacity_ == 0 ? 64 : capacity_ * 2))
			return false;
		data_[size_++] = value;
		return true;
	}

	bool append(const uint8_t* first, const uint8_t* last)
	{
		if(last < first)
			return false;
		const std::size_t count = static_cast<std::size_t>(last - first);
		if(count == 0)
			return true;
		if(count > static_cast<std::size_t>(-1) - size_)
		{
			failed_ = true;
			stackimport_internal_note_allocation_failure();
			return false;
		}
		if(!reserve(size_ + count))
			return false;
		std::memcpy(data_ + size_, first, count);
		size_ += count;
		return true;
	}

	const uint8_t* data() const noexcept { return data_; }
	uint8_t* data() noexcept { return data_; }
	std::size_t size() const noexcept { return size_; }
	bool empty() const noexcept { return size_ == 0; }
	bool failed() const noexcept { return failed_; }
	uint8_t operator[](std::size_t index) const noexcept { return data_[index]; }

private:
	void release() noexcept
	{
		if(data_ && deallocate_)
			stackimport_internal_deallocate(data_, deallocate_, user_data_);
		data_ = nullptr;
		size_ = 0;
		capacity_ = 0;
		deallocate_ = nullptr;
		user_data_ = nullptr;
	}

	uint8_t* data_ = nullptr;
	std::size_t size_ = 0;
	std::size_t capacity_ = 0;
	stackimport_deallocate_fn deallocate_ = nullptr;
	void* user_data_ = nullptr;
	bool failed_ = false;
};

bool ConvertSndResourceToWav(rsrcd::Bytes snd, PlatformByteVector& wav, std::string& error);

} // namespace stackimport
