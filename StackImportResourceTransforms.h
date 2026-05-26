#pragma once

#include "StackImportResourceTypes.h"

namespace stackimport {

auto emit_builtin_resource_transforms(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool;

} // namespace stackimport
