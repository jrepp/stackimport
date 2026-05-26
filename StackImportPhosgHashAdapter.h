#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace stackimport {

std::string Sha256WithPhosg(const uint8_t* data, size_t size);

} // namespace stackimport
