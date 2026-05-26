#include "../StackImportPhosgHashAdapter.h"

#include <phosg/Hash.hh>

namespace stackimport {

std::string Sha256WithPhosg(const uint8_t* data, size_t size)
{
	return phosg::SHA256(data, size).hex();
}

} // namespace stackimport
