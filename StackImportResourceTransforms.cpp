#include "Mac68kDisassembly.h"
#include "StackImportResourceTransforms.h"

#include "StackImportResourceDasmPictAdapter.h"
#include "StackImportSoundConverter.h"
#include "stackimport_rapidjson_allocator.h"

#include <cstring>
#include <span>
#include <string>
#include <vector>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

using JsonPoolAllocator = rapidjson::MemoryPoolAllocator<StackImportRapidJsonAllocator>;
using JsonDocument = rapidjson::GenericDocument<rapidjson::UTF8<>, JsonPoolAllocator, StackImportRapidJsonAllocator>;
using JsonValue = rapidjson::GenericValue<rapidjson::UTF8<>, JsonPoolAllocator>;
using JsonStringBuffer = rapidjson::GenericStringBuffer<rapidjson::UTF8<>, StackImportRapidJsonAllocator>;
using JsonWriter = rapidjson::PrettyWriter<JsonStringBuffer, rapidjson::UTF8<>, rapidjson::UTF8<>, StackImportRapidJsonAllocator>;

extern unsigned char sMacRomanToUTF8Table[128][5];

namespace stackimport {

namespace {

void swap_bgra_to_rgba(uint8_t* data, size_t pixel_count)
{
	for(size_t i = 0; i < pixel_count; ++i)
	{
		uint8_t tmp = data[i * 4];
		data[i * 4] = data[i * 4 + 2];
		data[i * 4 + 2] = tmp;
	}
}

auto resource_type_is(const rsrcd::ResRef& resource, const char* type) -> bool
{
	return resource.type.size == 4 && resource.type.data != nullptr &&
		std::memcmp(resource.type.data, type, 4) == 0;
}

struct IndexedIconSpec
{
	uint16_t width;
	uint16_t height;
	uint8_t bits_per_pixel;
	const char* description;
};

struct MonochromeIconListSpec
{
	uint16_t width;
	uint16_t height;
	const char* image_description;
	const char* composite_description;
};

auto indexed_icon_spec(const rsrcd::ResRef& resource, IndexedIconSpec& spec) -> bool
{
	if(resource_type_is(resource, "icl4"))
		spec = {32, 32, 4, "decoded 32x32 4-bit large color icon pixels"};
	else if(resource_type_is(resource, "icl8"))
		spec = {32, 32, 8, "decoded 32x32 8-bit large color icon pixels"};
	else if(resource_type_is(resource, "icm4"))
		spec = {16, 12, 4, "decoded 16x12 4-bit mini color icon pixels"};
	else if(resource_type_is(resource, "icm8"))
		spec = {16, 12, 8, "decoded 16x12 8-bit mini color icon pixels"};
	else if(resource_type_is(resource, "ics4"))
		spec = {16, 16, 4, "decoded 16x16 4-bit small color icon pixels"};
	else if(resource_type_is(resource, "ics8"))
		spec = {16, 16, 8, "decoded 16x16 8-bit small color icon pixels"};
	else
		return false;
	return true;
}

auto monochrome_icon_list_spec(const rsrcd::ResRef& resource, MonochromeIconListSpec& spec) -> bool
{
	if(resource_type_is(resource, "icm#"))
		spec = {
			16,
			12,
			"decoded 16x12 1-bit mini icon pixels",
			"decoded 16x12 1-bit mini icon bitmap/mask pixels",
		};
	else if(resource_type_is(resource, "ics#"))
		spec = {
			16,
			16,
			"decoded 16x16 1-bit small icon pixels",
			"decoded 16x16 1-bit small icon bitmap/mask pixels",
		};
	else
		return false;
	return true;
}

auto json_string(const std::string& value, JsonPoolAllocator& allocator) -> JsonValue
{
	JsonValue result;
	result.SetString(value.c_str(), static_cast<rapidjson::SizeType>(value.size()), allocator);
	return result;
}

auto mac_roman_from_bytes(const uint8_t* data, size_t len) -> std::string
{
	std::string result;
	for(size_t i = 0; i < len; i++)
	{
		unsigned char ch = data[i];
		if(ch >= 128)
			result.append(reinterpret_cast<const char*>(sMacRomanToUTF8Table[ch - 128]));
		else if(ch == 0x11)
		{
			static unsigned char command_key[4] = {0xe2, 0x8c, 0x98, 0};
			result.append(reinterpret_cast<const char*>(command_key));
		}
		else
			result.push_back(static_cast<char>(ch));
	}
	return result;
}

auto bytes_to_hex(rsrcd::Bytes bytes) -> std::string
{
	static const char hex[] = "0123456789ABCDEF";
	std::string result;
	result.reserve(bytes.size * 2u);
	for(size_t i = 0; i < bytes.size; ++i)
	{
		const uint8_t byte = bytes.data[i];
		result.push_back(hex[(byte >> 4u) & 0x0Fu]);
		result.push_back(hex[byte & 0x0Fu]);
	}
	return result;
}

auto resource_type_string(uint32_t type) -> std::string
{
	std::string result;
	result.reserve(4);
	for(int shift = 24; shift >= 0; shift -= 8)
	{
		char ch = static_cast<char>((type >> shift) & 0xFFu);
		result.push_back(ch >= 0x20 && ch <= 0x7E ? ch : '.');
	}
	return result;
}

auto cfrg_usage_name(uint8_t usage) -> const char*
{
	static const char* names[] = {
		"importLibrary",
		"application",
		"dropInAddition",
		"stubLibrary",
		"weakStubLibrary",
	};
	return usage < 5 ? names[usage] : "invalid";
}

auto cfrg_where_name(uint8_t where) -> const char*
{
	static const char* names[] = {
		"memory",
		"dataFork",
		"resourceFork",
		"byteStream",
		"namedFragment",
	};
	return where < 5 ? names[where] : "invalid";
}

auto emit_plte_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed PLTE palette metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::plte::Palette<64> pal;
	auto plte_result = rsrcd::plte::parse(resource.data, pal);
	if(!plte_result)
		return output.on_resource_error(ref, plte_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(1024, &base_alloc);
	JsonDocument doc(&pool, 1024, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	doc.AddMember("wdef", pal.wdef, allocator);
	doc.AddMember("showName", pal.show_name, allocator);
	doc.AddMember("selection", pal.selection, allocator);
	doc.AddMember("frame", pal.frame, allocator);
	doc.AddMember("pictRef", pal.pict_ref, allocator);
	doc.AddMember("top", pal.top, allocator);
	doc.AddMember("left", pal.left, allocator);

	JsonValue buttons(rapidjson::kArrayType);
	for(size_t bi = 0; bi < pal.button_count; bi++)
	{
		JsonValue btn(rapidjson::kObjectType);
		const auto& b = pal.buttons[bi];
		btn.AddMember("top", b.top, allocator);
		btn.AddMember("left", b.left, allocator);
		btn.AddMember("bottom", b.bottom, allocator);
		btn.AddMember("right", b.right, allocator);
		if(!b.message.empty())
		{
			std::string msg = mac_roman_from_bytes(b.message.data, b.message.size);
			btn.AddMember("message", json_string(msg, allocator), allocator);
		}
		buttons.PushBack(btn, allocator);
	}
	doc.AddMember("buttons", buttons, allocator);

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_cfrg_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed code fragment metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::cfrg::FragmentList<128> fragments;
	auto parse_result = rsrcd::cfrg::parse(resource.data, fragments);
	if(!parse_result)
		return output.on_resource_error(ref, parse_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(4096, &base_alloc);
	JsonDocument doc(&pool, 4096, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	doc.AddMember("version", fragments.version, allocator);
	doc.AddMember("reserved3", fragments.reserved3, allocator);
	doc.AddMember("reserved8", fragments.reserved8, allocator);

	JsonValue entries(rapidjson::kArrayType);
	for(const auto& entry : fragments)
	{
		JsonValue item(rapidjson::kObjectType);
		item.AddMember("architecture", entry.architecture, allocator);
		item.AddMember("architectureType", json_string(resource_type_string(entry.architecture), allocator), allocator);
		item.AddMember("reserved1", entry.reserved1, allocator);
		item.AddMember("reserved2", entry.reserved2, allocator);
		item.AddMember("updateLevel", entry.update_level, allocator);
		item.AddMember("currentVersion", entry.current_version, allocator);
		item.AddMember("oldDefVersion", entry.old_def_version, allocator);
		item.AddMember("appStackSize", entry.app_stack_size, allocator);
		item.AddMember("appSubdirIdOrLibFlags", entry.app_subdir_id_or_lib_flags, allocator);
		item.AddMember("usage", entry.usage, allocator);
		item.AddMember("usageName", json_string(cfrg_usage_name(entry.usage), allocator), allocator);
		item.AddMember("where", entry.where, allocator);
		item.AddMember("whereName", json_string(cfrg_where_name(entry.where), allocator), allocator);
		item.AddMember("offset", entry.offset, allocator);
		item.AddMember("length", entry.length, allocator);
		item.AddMember("spaceIdOrForkKind", entry.space_id_or_fork_kind, allocator);
		item.AddMember("forkInstance", entry.fork_instance, allocator);
		item.AddMember("extensionCount", entry.extension_count, allocator);
		item.AddMember("entrySize", entry.entry_size, allocator);
		std::string name = mac_roman_from_bytes(entry.name.data, entry.name.size);
		item.AddMember("name", json_string(name, allocator), allocator);
		if(entry.extension_data.size > 0)
		{
			std::string extension_hex = bytes_to_hex(entry.extension_data);
			item.AddMember("extensionDataHex", json_string(extension_hex, allocator), allocator);
		}
		entries.PushBack(item, allocator);
	}
	doc.AddMember("entries", entries, allocator);

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_mbar_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed menu bar metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::mbar::MenuIdList<128> menu_ids;
	auto parse_result = rsrcd::mbar::parse(resource.data, menu_ids);
	if(!parse_result)
		return output.on_resource_error(ref, parse_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(1024, &base_alloc);
	JsonDocument doc(&pool, 1024, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	JsonValue ids(rapidjson::kArrayType);
	for(uint16_t id : menu_ids)
		ids.PushBack(id, allocator);
	doc.AddMember("menuIds", ids, allocator);

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

const char* addcolor_object_type_name(rsrcd::ac::ObjType type)
{
	switch(type)
	{
		case rsrcd::ac::ObjButton:
			return "button";
		case rsrcd::ac::ObjField:
			return "field";
		case rsrcd::ac::ObjRect:
			return "rect";
		case rsrcd::ac::ObjPictRes:
			return "pictResource";
		case rsrcd::ac::ObjPictFile:
			return "pictFile";
	}
	return "unknown";
}

void add_json_rect(JsonValue& object, const rsrcd::ac::QDRect& rect, JsonPoolAllocator& allocator)
{
	JsonValue value(rapidjson::kObjectType);
	value.AddMember("top", rect.top, allocator);
	value.AddMember("left", rect.left, allocator);
	value.AddMember("bottom", rect.bottom, allocator);
	value.AddMember("right", rect.right, allocator);
	object.AddMember("rect", value, allocator);
}

void add_json_color(JsonValue& object, const rsrcd::ac::RGBColor& color, JsonPoolAllocator& allocator)
{
	JsonValue value(rapidjson::kObjectType);
	value.AddMember("red", color.red, allocator);
	value.AddMember("green", color.green, allocator);
	value.AddMember("blue", color.blue, allocator);
	object.AddMember("color", value, allocator);
}

template<typename JsonObject>
void add_json_ui_rect(JsonObject& object, const rsrcd::ui::Rect& rect, JsonPoolAllocator& allocator)
{
	JsonValue value(rapidjson::kObjectType);
	value.AddMember("top", rect.top, allocator);
	value.AddMember("left", rect.left, allocator);
	value.AddMember("bottom", rect.bottom, allocator);
	value.AddMember("right", rect.right, allocator);
	object.AddMember("bounds", value, allocator);
}

auto emit_alrt_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed alert metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::finder::Alert alert{};
	auto parse_result = rsrcd::finder::parse_alrt(resource.data, alert);
	if(!parse_result)
		return output.on_resource_error(ref, parse_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(1024, &base_alloc);
	JsonDocument doc(&pool, 1024, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	add_json_ui_rect(doc, alert.bounds, allocator);
	doc.AddMember("itemListId", alert.item_list_id, allocator);
	doc.AddMember("stage4Flags", alert.stage_4_flags, allocator);
	doc.AddMember("stage2Flags", alert.stage_2_flags, allocator);
	doc.AddMember("hasAutoPosition", alert.has_auto_position, allocator);
	if(alert.has_auto_position)
		doc.AddMember("autoPosition", alert.auto_position, allocator);

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_fref_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed file reference metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::finder::FileReference fref{};
	auto parse_result = rsrcd::finder::parse_fref(resource.data, fref);
	if(!parse_result)
		return output.on_resource_error(ref, parse_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(1024, &base_alloc);
	JsonDocument doc(&pool, 1024, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	doc.AddMember("fileType", fref.file_type, allocator);
	doc.AddMember("fileTypeString", json_string(resource_type_string(fref.file_type), allocator), allocator);
	doc.AddMember("localId", fref.local_id, allocator);
	std::string name = mac_roman_from_bytes(fref.file_name.data, fref.file_name.size);
	doc.AddMember("fileName", json_string(name, allocator), allocator);

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_bndl_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed bundle metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::finder::Bundle<64, 256> bundle;
	auto parse_result = rsrcd::finder::parse_bndl(resource.data, bundle);
	if(!parse_result)
		return output.on_resource_error(ref, parse_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(4096, &base_alloc);
	JsonDocument doc(&pool, 4096, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	doc.AddMember("ownerName", bundle.owner_name, allocator);
	doc.AddMember("ownerNameString", json_string(resource_type_string(bundle.owner_name), allocator), allocator);
	doc.AddMember("ownerId", bundle.owner_id, allocator);

	JsonValue types(rapidjson::kArrayType);
	for(const auto& type : bundle)
	{
		JsonValue type_value(rapidjson::kObjectType);
		type_value.AddMember("type", type.resource_type, allocator);
		type_value.AddMember("typeString", json_string(resource_type_string(type.resource_type), allocator), allocator);
		JsonValue ids(rapidjson::kArrayType);
		for(const auto& id : type)
		{
			JsonValue id_value(rapidjson::kObjectType);
			id_value.AddMember("localId", id.local_id, allocator);
			id_value.AddMember("resourceId", id.resource_id, allocator);
			ids.PushBack(id_value, allocator);
		}
		type_value.AddMember("ids", ids, allocator);
		types.PushBack(type_value, allocator);
	}
	doc.AddMember("types", types, allocator);

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_rov_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed ROM override metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::rov::OverrideList<256> overrides;
	auto parse_result = rsrcd::rov::parse(resource.data, overrides);
	if(!parse_result)
		return output.on_resource_error(ref, parse_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(2048, &base_alloc);
	JsonDocument doc(&pool, 2048, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	doc.AddMember("romVersion", overrides.rom_version, allocator);
	JsonValue items(rapidjson::kArrayType);
	for(const auto& item : overrides)
	{
		JsonValue value(rapidjson::kObjectType);
		value.AddMember("type", item.type, allocator);
		value.AddMember("typeString", json_string(resource_type_string(item.type), allocator), allocator);
		value.AddMember("id", item.id, allocator);
		items.PushBack(value, allocator);
	}
	doc.AddMember("overrides", items, allocator);

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_rssc_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed RSSC metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::rssc::Resource rssc{};
	auto parse_result = rsrcd::rssc::parse(resource.data, rssc);
	if(!parse_result)
		return output.on_resource_error(ref, parse_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(1024, &base_alloc);
	JsonDocument doc(&pool, 1024, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	doc.AddMember("typeSignature", rssc.type_signature, allocator);
	doc.AddMember("typeSignatureString", json_string(resource_type_string(rssc.type_signature), allocator), allocator);
	doc.AddMember("codeStartOffset", 22, allocator);
	doc.AddMember("codeSize", static_cast<uint64_t>(rssc.code.size), allocator);
	JsonValue offsets(rapidjson::kArrayType);
	for(uint16_t offset : rssc.function_offsets)
	{
		JsonValue value(rapidjson::kObjectType);
		value.AddMember("set", offset != 0, allocator);
		value.AddMember("offset", offset, allocator);
		offsets.PushBack(value, allocator);
	}
	doc.AddMember("functionOffsets", offsets, allocator);

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_txst_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed text style metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::txst::TextStyle style{};
	auto parse_result = rsrcd::txst::parse(resource.data, style);
	if(!parse_result)
		return output.on_resource_error(ref, parse_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(1024, &base_alloc);
	JsonDocument doc(&pool, 1024, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	doc.AddMember("fontStyle", style.font_style, allocator);
	doc.AddMember("fontSize", style.font_size, allocator);
	JsonValue color(rapidjson::kObjectType);
	color.AddMember("red", style.red, allocator);
	color.AddMember("green", style.green, allocator);
	color.AddMember("blue", style.blue, allocator);
	doc.AddMember("textColor", color, allocator);
	std::string font_name = mac_roman_from_bytes(style.font_name.data, style.font_name.size);
	doc.AddMember("fontName", json_string(font_name, allocator), allocator);

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_styl_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed text style run metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::styl::StyleRunList<512> style_runs;
	auto parse_result = rsrcd::styl::parse(resource.data, style_runs);
	if(!parse_result)
		return output.on_resource_error(ref, parse_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(4096, &base_alloc);
	JsonDocument doc(&pool, 4096, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	JsonValue runs(rapidjson::kArrayType);
	for(const auto& run : style_runs)
	{
		JsonValue item(rapidjson::kObjectType);
		item.AddMember("offset", run.offset, allocator);
		item.AddMember("lineHeight", run.line_height, allocator);
		item.AddMember("fontAscent", run.font_ascent, allocator);
		item.AddMember("fontId", run.font_id, allocator);
		item.AddMember("styleFlags", run.style_flags, allocator);
		item.AddMember("fontSize", run.font_size, allocator);
		JsonValue color(rapidjson::kObjectType);
		color.AddMember("red", run.color.red, allocator);
		color.AddMember("green", run.color.green, allocator);
		color.AddMember("blue", run.color.blue, allocator);
		item.AddMember("color", color, allocator);
		runs.PushBack(item, allocator);
	}
	doc.AddMember("runs", runs, allocator);

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_rect_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed rectangle metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::ui::Rect rect{};
	auto parse_result = rsrcd::simple_metadata::parse_rect(resource.data, rect);
	if(!parse_result)
		return output.on_resource_error(ref, parse_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(512, &base_alloc);
	JsonDocument doc(&pool, 512, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	add_json_ui_rect(doc, rect, allocator);

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_tool_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed tool palette metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::simple_metadata::ToolList<256> tools;
	auto parse_result = rsrcd::simple_metadata::parse_tool(resource.data, tools);
	if(!parse_result)
		return output.on_resource_error(ref, parse_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(1024, &base_alloc);
	JsonDocument doc(&pool, 1024, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	doc.AddMember("toolsPerRow", tools.tools_per_row, allocator);
	doc.AddMember("rowCount", tools.row_count, allocator);
	JsonValue cursor_ids(rapidjson::kArrayType);
	for(uint16_t cursor_id : tools)
		cursor_ids.PushBack(cursor_id, allocator);
	doc.AddMember("cursorIds", cursor_ids, allocator);

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_pick_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed picker metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::simple_metadata::Picker<256> picker;
	auto parse_result = rsrcd::simple_metadata::parse_pick(resource.data, picker);
	if(!parse_result)
		return output.on_resource_error(ref, parse_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(2048, &base_alloc);
	JsonDocument doc(&pool, 2048, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	doc.AddMember("type", picker.type, allocator);
	doc.AddMember("typeString", json_string(resource_type_string(picker.type), allocator), allocator);
	doc.AddMember("useColor", picker.use_color, allocator);
	doc.AddMember("pickerType", picker.picker_type, allocator);
	doc.AddMember("viewBy", picker.view_by, allocator);
	doc.AddMember("reserved", picker.reserved, allocator);
	doc.AddMember("verticalCellSize", picker.vertical_cell_size, allocator);
	JsonValue resources(rapidjson::kArrayType);
	for(const auto& item : picker)
	{
		JsonValue value(rapidjson::kObjectType);
		value.AddMember("type", item.type, allocator);
		value.AddMember("typeString", json_string(resource_type_string(item.type), allocator), allocator);
		value.AddMember("id", item.id, allocator);
		resources.PushBack(value, allocator);
	}
	doc.AddMember("resources", resources, allocator);

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_kbdn_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed keyboard name metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::simple_metadata::KeyboardName keyboard{};
	auto parse_result = rsrcd::simple_metadata::parse_kbdn(resource.data, keyboard);
	if(!parse_result)
		return output.on_resource_error(ref, parse_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(512, &base_alloc);
	JsonDocument doc(&pool, 512, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	std::string name = mac_roman_from_bytes(keyboard.name.data, keyboard.name.size);
	doc.AddMember("name", json_string(name, allocator), allocator);

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_papa_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed printer parameter metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::simple_metadata::PrinterParameters params{};
	auto parse_result = rsrcd::simple_metadata::parse_papa(resource.data, params);
	if(!parse_result)
		return output.on_resource_error(ref, parse_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(1024, &base_alloc);
	JsonDocument doc(&pool, 1024, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	std::string name = mac_roman_from_bytes(params.name.data, params.name.size);
	std::string type = mac_roman_from_bytes(params.type.data, params.type.size);
	std::string zone = mac_roman_from_bytes(params.zone.data, params.zone.size);
	doc.AddMember("name", json_string(name, allocator), allocator);
	doc.AddMember("type", json_string(type, allocator), allocator);
	doc.AddMember("zone", json_string(zone, allocator), allocator);
	doc.AddMember("addressBlock", params.address_block, allocator);
	if(params.data.size > 0)
	{
		std::string data_hex = bytes_to_hex(params.data);
		doc.AddMember("dataHex", json_string(data_hex, allocator), allocator);
	}

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_layo_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed layout metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::simple_metadata::Layout<256> layout;
	auto parse_result = rsrcd::simple_metadata::parse_layo(resource.data, layout);
	if(!parse_result)
		return output.on_resource_error(ref, parse_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(2048, &base_alloc);
	JsonDocument doc(&pool, 2048, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	doc.AddMember("fontId", layout.font_id, allocator);
	doc.AddMember("fontSize", layout.font_size, allocator);
	doc.AddMember("screenHeaderHeight", layout.screen_header_height, allocator);
	doc.AddMember("topLineBreak", layout.top_line_break, allocator);
	doc.AddMember("bottomLineBreak", layout.bottom_line_break, allocator);
	doc.AddMember("printingHeaderHeight", layout.printing_header_height, allocator);
	doc.AddMember("printingFooterHeight", layout.printing_footer_height, allocator);
	add_json_ui_rect(doc, layout.window_rect, allocator);
	JsonValue rects(rapidjson::kArrayType);
	for(const auto& rect : layout)
	{
		JsonValue item(rapidjson::kObjectType);
		add_json_ui_rect(item, rect, allocator);
		rects.PushBack(item, allocator);
	}
	doc.AddMember("rectangles", rects, allocator);

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_code_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed CODE metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(4096, &base_alloc);
	JsonDocument doc(&pool, 4096, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	if(resource.id == 0)
	{
		rsrcd::code_resource::Code0<512> code0;
		auto parse_result = rsrcd::code_resource::parse_code0(resource.data, code0);
		if(!parse_result)
			return output.on_resource_error(ref, parse_result.message());
		doc.AddMember("kind", json_string("jumpTable", allocator), allocator);
		doc.AddMember("aboveA5Size", code0.above_a5_size, allocator);
		doc.AddMember("belowA5Size", code0.below_a5_size, allocator);
		doc.AddMember("jumpTableSize", code0.jump_table_size, allocator);
		doc.AddMember("jumpTableOffset", code0.jump_table_offset, allocator);
		JsonValue entries(rapidjson::kArrayType);
		for(const auto& entry : code0)
		{
			JsonValue value(rapidjson::kObjectType);
			value.AddMember("offset", entry.offset, allocator);
			value.AddMember("pushOpcode", entry.push_opcode, allocator);
			value.AddMember("resourceId", entry.resource_id, allocator);
			value.AddMember("trapOpcode", entry.trap_opcode, allocator);
			value.AddMember("loadSegmentTrap", entry.load_segment_trap, allocator);
			entries.PushBack(value, allocator);
		}
		doc.AddMember("jumpTableEntries", entries, allocator);
	}
	else
	{
		rsrcd::code_resource::Segment segment;
		auto parse_result = rsrcd::code_resource::parse_segment(resource.data, segment);
		if(!parse_result)
			return output.on_resource_error(ref, parse_result.message());
		doc.AddMember("kind", json_string(segment.far_header ? "farSegment" : "nearSegment", allocator), allocator);
		doc.AddMember("firstJumpTableEntry", segment.first_jump_table_entry, allocator);
		doc.AddMember("jumpTableEntryCount", segment.jump_table_entry_count, allocator);
		doc.AddMember("codeStartOffset", segment.code_start_offset, allocator);
		doc.AddMember("codeSize", static_cast<uint64_t>(segment.code.size), allocator);
		if(segment.far_header)
		{
			doc.AddMember("nearEntryStartA5Offset", segment.near_entry_start_a5_offset, allocator);
			doc.AddMember("nearEntryCount", segment.near_entry_count, allocator);
			doc.AddMember("farEntryStartA5Offset", segment.far_entry_start_a5_offset, allocator);
			doc.AddMember("farEntryCount", segment.far_entry_count, allocator);
			doc.AddMember("a5RelocationDataOffset", segment.a5_relocation_data_offset, allocator);
			doc.AddMember("a5", segment.a5, allocator);
			doc.AddMember("pcRelocationDataOffset", segment.pc_relocation_data_offset, allocator);
			doc.AddMember("loadAddress", segment.load_address, allocator);
			doc.AddMember("reserved", segment.reserved, allocator);
		}
	}

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_drvr_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed driver metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::drvr::Driver driver{};
	auto parse_result = rsrcd::drvr::parse(resource.data, driver);
	if(!parse_result)
		return output.on_resource_error(ref, parse_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(1024, &base_alloc);
	JsonDocument doc(&pool, 1024, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	doc.AddMember("flags", driver.flags, allocator);
	doc.AddMember("delay", driver.delay, allocator);
	doc.AddMember("eventMask", driver.event_mask, allocator);
	doc.AddMember("menuId", driver.menu_id, allocator);
	doc.AddMember("openLabel", driver.open_label, allocator);
	doc.AddMember("primeLabel", driver.prime_label, allocator);
	doc.AddMember("controlLabel", driver.control_label, allocator);
	doc.AddMember("statusLabel", driver.status_label, allocator);
	doc.AddMember("closeLabel", driver.close_label, allocator);
	std::string name = mac_roman_from_bytes(driver.name.data, driver.name.size);
	doc.AddMember("name", json_string(name, allocator), allocator);
	doc.AddMember("codeStartOffset", driver.code_start_offset, allocator);
	doc.AddMember("codeSize", static_cast<uint64_t>(driver.code.size), allocator);

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_dcmp_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed decompressor metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::dcmp::Decompressor decompressor{};
	auto parse_result = rsrcd::dcmp::parse(resource.data, decompressor);
	if(!parse_result)
		return output.on_resource_error(ref, parse_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(512, &base_alloc);
	JsonDocument doc(&pool, 512, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	doc.AddMember("initLabel", decompressor.init_label, allocator);
	doc.AddMember("decompressLabel", decompressor.decompress_label, allocator);
	doc.AddMember("exitLabel", decompressor.exit_label, allocator);
	doc.AddMember("pcOffset", decompressor.pc_offset, allocator);
	doc.AddMember("codeSize", static_cast<uint64_t>(decompressor.code.size), allocator);

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_addcolor_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output,
	const char* target_kind) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed AddColor overlay metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::ac::ObjectList<64> objects;
	auto ac_result = rsrcd::ac::parse(resource.data, objects);
	if(!ac_result)
		return output.on_resource_error(ref, ac_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(1024, &base_alloc);
	JsonDocument doc(&pool, 1024, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	doc.AddMember("targetKind", JsonValue().SetString(target_kind, allocator), allocator);
	doc.AddMember("targetId", resource.id, allocator);

	JsonValue object_array(rapidjson::kArrayType);
	for(const rsrcd::ac::Object& object : objects)
	{
		JsonValue item(rapidjson::kObjectType);
		item.AddMember("type", JsonValue().SetString(addcolor_object_type_name(object.type), allocator), allocator);
		item.AddMember("hidden", object.hidden, allocator);
		switch(object.type)
		{
			case rsrcd::ac::ObjButton:
			case rsrcd::ac::ObjField:
				item.AddMember("partId", object.part_id, allocator);
				item.AddMember("bevel", object.bevel, allocator);
				add_json_color(item, object.color, allocator);
				break;
			case rsrcd::ac::ObjRect:
				item.AddMember("bevel", object.bevel, allocator);
				add_json_rect(item, object.rect, allocator);
				add_json_color(item, object.color, allocator);
				break;
			case rsrcd::ac::ObjPictRes:
			case rsrcd::ac::ObjPictFile:
				add_json_rect(item, object.rect, allocator);
				item.AddMember("transparent", object.transparent, allocator);
				if(!object.name.empty())
				{
					std::string name = mac_roman_from_bytes(object.name.data, object.name.size);
					item.AddMember("name", json_string(name, allocator), allocator);
				}
				break;
		}
		object_array.PushBack(item, allocator);
	}
	doc.AddMember("objects", object_array, allocator);

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto finish_json_resource_payload(
	ResourcePayload& descriptor,
	JsonDocument& doc,
	StackImportRapidJsonAllocator& base_alloc,
	IResourceOutput& output) -> bool
{
	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_text_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output,
	bool pascal_string)
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::TextUtf8,
		rsrcd::Bytes{nullptr, 0},
		"text/plain; charset=utf-8",
		pascal_string ? "decoded STR text" : "decoded TEXT resource");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::text::StringRef text;
	auto text_result = pascal_string
		? rsrcd::text::parse_str(resource.data, text)
		: rsrcd::text::parse_text(resource.data, text);
	if(!text_result)
		return output.on_resource_error(ref, text_result.message());

	std::string utf8 = mac_roman_from_bytes(text.bytes.data, text.bytes.size);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(utf8.data()), utf8.size()};
	return output.on_resource_payload(descriptor);
}

auto emit_cntl_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed CNTL control metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::ui::Control control;
	auto control_result = rsrcd::ui::parse_cntl(resource.data, control);
	if(!control_result)
		return output.on_resource_error(ref, control_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(1024, &base_alloc);
	JsonDocument doc(&pool, 1024, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	add_json_ui_rect(doc, control.bounds, allocator);
	doc.AddMember("value", control.value, allocator);
	doc.AddMember("visible", control.visible, allocator);
	doc.AddMember("maximum", control.maximum, allocator);
	doc.AddMember("minimum", control.minimum, allocator);
	doc.AddMember("procId", control.proc_id, allocator);
	doc.AddMember("refCon", control.ref_con, allocator);
	std::string title = mac_roman_from_bytes(control.title.data, control.title.size);
	doc.AddMember("title", json_string(title, allocator), allocator);
	return finish_json_resource_payload(descriptor, doc, base_alloc, output);
}

auto emit_dlog_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed DLOG dialog metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::ui::Dialog dialog;
	auto dialog_result = rsrcd::ui::parse_dlog(resource.data, dialog);
	if(!dialog_result)
		return output.on_resource_error(ref, dialog_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(1024, &base_alloc);
	JsonDocument doc(&pool, 1024, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	add_json_ui_rect(doc, dialog.bounds, allocator);
	doc.AddMember("procId", dialog.proc_id, allocator);
	doc.AddMember("visible", dialog.visible, allocator);
	doc.AddMember("goAway", dialog.go_away, allocator);
	doc.AddMember("refCon", dialog.ref_con, allocator);
	doc.AddMember("itemsId", dialog.items_id, allocator);
	std::string title = mac_roman_from_bytes(dialog.title.data, dialog.title.size);
	doc.AddMember("title", json_string(title, allocator), allocator);
	if(dialog.has_auto_position)
		doc.AddMember("autoPosition", dialog.auto_position, allocator);
	return finish_json_resource_payload(descriptor, doc, base_alloc, output);
}

auto emit_wind_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed WIND window metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::ui::Window window;
	auto window_result = rsrcd::ui::parse_wind(resource.data, window);
	if(!window_result)
		return output.on_resource_error(ref, window_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(1024, &base_alloc);
	JsonDocument doc(&pool, 1024, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	add_json_ui_rect(doc, window.bounds, allocator);
	doc.AddMember("procId", window.proc_id, allocator);
	doc.AddMember("visible", window.visible, allocator);
	doc.AddMember("goAway", window.go_away, allocator);
	doc.AddMember("refCon", window.ref_con, allocator);
	std::string title = mac_roman_from_bytes(window.title.data, window.title.size);
	doc.AddMember("title", json_string(title, allocator), allocator);
	if(window.has_auto_position)
		doc.AddMember("autoPosition", window.auto_position, allocator);
	return finish_json_resource_payload(descriptor, doc, base_alloc, output);
}

auto emit_menu_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed MENU metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::ui::MenuItemList<128> menu;
	auto menu_result = rsrcd::ui::parse_menu(resource.data, menu);
	if(!menu_result)
		return output.on_resource_error(ref, menu_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(1024, &base_alloc);
	JsonDocument doc(&pool, 1024, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	doc.AddMember("menuId", menu.menu_id, allocator);
	doc.AddMember("procId", menu.proc_id, allocator);
	doc.AddMember("enabledFlags", menu.enabled_flags, allocator);
	doc.AddMember("enabled", menu.enabled, allocator);
	std::string title = mac_roman_from_bytes(menu.title.data, menu.title.size);
	doc.AddMember("title", json_string(title, allocator), allocator);

	JsonValue items(rapidjson::kArrayType);
	for(const rsrcd::ui::MenuItem& menu_item : menu)
	{
		JsonValue item(rapidjson::kObjectType);
		std::string name = mac_roman_from_bytes(menu_item.name.data, menu_item.name.size);
		item.AddMember("name", json_string(name, allocator), allocator);
		item.AddMember("iconNumber", menu_item.icon_number, allocator);
		item.AddMember("keyEquivalent", menu_item.key_equivalent, allocator);
		item.AddMember("markCharacter", menu_item.mark_character, allocator);
		item.AddMember("styleFlags", menu_item.style_flags, allocator);
		item.AddMember("enabled", menu_item.enabled, allocator);
		items.PushBack(item, allocator);
	}
	doc.AddMember("items", items, allocator);
	return finish_json_resource_payload(descriptor, doc, base_alloc, output);
}

const char* dialog_item_kind_name(rsrcd::ui::DialogItemKind kind)
{
	switch(kind)
	{
		case rsrcd::ui::DialogItemKind::Button:
			return "button";
		case rsrcd::ui::DialogItemKind::Checkbox:
			return "checkbox";
		case rsrcd::ui::DialogItemKind::RadioButton:
			return "radioButton";
		case rsrcd::ui::DialogItemKind::ResourceControl:
			return "resourceControl";
		case rsrcd::ui::DialogItemKind::HelpBalloon:
			return "helpBalloon";
		case rsrcd::ui::DialogItemKind::Text:
			return "text";
		case rsrcd::ui::DialogItemKind::EditText:
			return "editText";
		case rsrcd::ui::DialogItemKind::Icon:
			return "icon";
		case rsrcd::ui::DialogItemKind::Picture:
			return "picture";
		case rsrcd::ui::DialogItemKind::Custom:
			return "custom";
		case rsrcd::ui::DialogItemKind::Unknown:
			return "unknown";
	}
	return "unknown";
}

auto emit_ditl_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed DITL dialog item metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::ui::DialogItemList<128> dialog_items;
	auto ditl_result = rsrcd::ui::parse_ditl(resource.data, dialog_items);
	if(!ditl_result)
		return output.on_resource_error(ref, ditl_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(1024, &base_alloc);
	JsonDocument doc(&pool, 1024, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();

	JsonValue items(rapidjson::kArrayType);
	for(const rsrcd::ui::DialogItem& dialog_item : dialog_items)
	{
		JsonValue item(rapidjson::kObjectType);
		add_json_ui_rect(item, dialog_item.bounds, allocator);
		item.AddMember("enabled", dialog_item.enabled, allocator);
		item.AddMember("kind", JsonValue().SetString(dialog_item_kind_name(dialog_item.kind), allocator), allocator);
		item.AddMember("rawType", dialog_item.raw_type, allocator);
		if(dialog_item.has_resource_id)
			item.AddMember("resourceId", dialog_item.resource_id, allocator);
		else if(!dialog_item.info.empty())
		{
			std::string info = mac_roman_from_bytes(dialog_item.info.data, dialog_item.info.size);
			item.AddMember("info", json_string(info, allocator), allocator);
		}
		items.PushBack(item, allocator);
	}
	doc.AddMember("items", items, allocator);
	return finish_json_resource_payload(descriptor, doc, base_alloc, output);
}

auto emit_string_list_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	const bool is_twcs = resource_type_is(resource, "TwCS");
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		is_twcs ? "decoded TwCS string list" : "decoded STR# string list");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::text::StringList<256> strings;
	auto text_result = is_twcs
		? rsrcd::text::parse_twcs(resource.data, strings)
		: rsrcd::text::parse_str_list(resource.data, strings);
	if(!text_result)
		return output.on_resource_error(ref, text_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(1024, &base_alloc);
	JsonDocument doc(&pool, 1024, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();

	JsonValue array(rapidjson::kArrayType);
	for(const rsrcd::text::StringRef& string_ref : strings)
	{
		std::string value = mac_roman_from_bytes(string_ref.bytes.data, string_ref.bytes.size);
		array.PushBack(json_string(value, allocator), allocator);
	}
	doc.AddMember("strings", array, allocator);

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_vers_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed vers metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::vers::Version version;
	auto vers_result = rsrcd::vers::parse(resource.data, version);
	if(!vers_result)
		return output.on_resource_error(ref, vers_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(1024, &base_alloc);
	JsonDocument doc(&pool, 1024, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	doc.AddMember("majorRevision", version.major_revision, allocator);
	doc.AddMember("minorAndBugRevision", version.minor_and_bug_revision, allocator);
	doc.AddMember("developmentStage", version.development_stage, allocator);
	doc.AddMember("prereleaseRevision", version.prerelease_revision, allocator);
	doc.AddMember("regionCode", version.region_code, allocator);
	std::string short_version = mac_roman_from_bytes(version.short_version.data, version.short_version.size);
	std::string long_version = mac_roman_from_bytes(version.long_version.data, version.long_version.size);
	doc.AddMember("shortVersion", json_string(short_version, allocator), allocator);
	doc.AddMember("longVersion", json_string(long_version, allocator), allocator);

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_color_table_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed color table metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::color_table::Table<256> table;
	auto table_result = rsrcd::color_table::parse(resource.data, table);
	if(!table_result)
		return output.on_resource_error(ref, table_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(1024, &base_alloc);
	JsonDocument doc(&pool, 1024, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	doc.AddMember("seed", table.seed, allocator);
	doc.AddMember("flags", table.flags, allocator);

	JsonValue entries(rapidjson::kArrayType);
	for(const rsrcd::color_table::Entry& entry : table)
	{
		JsonValue item(rapidjson::kObjectType);
		item.AddMember("value", entry.value, allocator);
		item.AddMember("red", entry.red, allocator);
		item.AddMember("green", entry.green, allocator);
		item.AddMember("blue", entry.blue, allocator);
		entries.PushBack(item, allocator);
	}
	doc.AddMember("entries", entries, allocator);

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_pltt_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed pltt palette metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::pltt::Palette<256> palette;
	auto palette_result = rsrcd::pltt::parse(resource.data, palette);
	if(!palette_result)
		return output.on_resource_error(ref, palette_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(1024, &base_alloc);
	JsonDocument doc(&pool, 1024, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();

	JsonValue entries(rapidjson::kArrayType);
	for(const rsrcd::pltt::Entry& entry : palette)
	{
		JsonValue item(rapidjson::kObjectType);
		item.AddMember("red", entry.red, allocator);
		item.AddMember("green", entry.green, allocator);
		item.AddMember("blue", entry.blue, allocator);
		entries.PushBack(item, allocator);
	}
	doc.AddMember("entries", entries, allocator);

	return finish_json_resource_payload(descriptor, doc, base_alloc, output);
}

void add_json_size_flag(JsonValue& flags, JsonPoolAllocator& allocator, const char* name, bool value)
{
	flags.AddMember(JsonValue().SetString(name, allocator), value, allocator);
}

auto emit_size_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed SIZE metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::size_resource::Size size;
	auto size_result = rsrcd::size_resource::parse(resource.data, size);
	if(!size_result)
		return output.on_resource_error(ref, size_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(1024, &base_alloc);
	JsonDocument doc(&pool, 1024, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();
	doc.AddMember("flags", size.flags, allocator);
	doc.AddMember("preferredSize", size.preferred_size, allocator);
	doc.AddMember("minimumSize", size.minimum_size, allocator);

	JsonValue flag_object(rapidjson::kObjectType);
	add_json_size_flag(flag_object, allocator, "saveScreen", size.save_screen);
	add_json_size_flag(flag_object, allocator, "acceptSuspendEvents", size.accept_suspend_events);
	add_json_size_flag(flag_object, allocator, "disableOption", size.disable_option);
	add_json_size_flag(flag_object, allocator, "canBackground", size.can_background);
	add_json_size_flag(flag_object, allocator, "activateOnForegroundSwitch", size.activate_on_fg_switch);
	add_json_size_flag(flag_object, allocator, "onlyBackground", size.only_background);
	add_json_size_flag(flag_object, allocator, "getFrontClicks", size.get_front_clicks);
	add_json_size_flag(flag_object, allocator, "acceptDiedEvents", size.accept_died_events);
	add_json_size_flag(flag_object, allocator, "cleanAddressing", size.clean_addressing);
	add_json_size_flag(flag_object, allocator, "highLevelEventAware", size.high_level_event_aware);
	add_json_size_flag(flag_object, allocator, "localAndRemoteHighLevelEvents", size.local_and_remote_high_level_events);
	add_json_size_flag(flag_object, allocator, "stationeryAware", size.stationery_aware);
	add_json_size_flag(flag_object, allocator, "useTextEditServices", size.use_text_edit_services);
	doc.AddMember("decodedFlags", flag_object, allocator);

	JsonStringBuffer json_buffer(&base_alloc);
	JsonWriter writer(json_buffer, &base_alloc);
	doc.Accept(writer);
	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(json_buffer.GetString()), json_buffer.GetSize()};
	return output.on_resource_payload(descriptor);
}

auto emit_finf_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"parsed finf font info metadata");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::finf::FontInfoList<128> font_info;
	auto finf_result = rsrcd::finf::parse(resource.data, font_info);
	if(!finf_result)
		return output.on_resource_error(ref, finf_result.message());

	StackImportRapidJsonAllocator base_alloc;
	JsonPoolAllocator pool(1024, &base_alloc);
	JsonDocument doc(&pool, 1024, &base_alloc);
	doc.SetObject();
	JsonPoolAllocator& allocator = doc.GetAllocator();

	JsonValue entries(rapidjson::kArrayType);
	for(const rsrcd::finf::Entry& entry : font_info)
	{
		JsonValue item(rapidjson::kObjectType);
		item.AddMember("fontId", entry.font_id, allocator);
		item.AddMember("styleFlags", entry.style_flags, allocator);
		item.AddMember("size", entry.size, allocator);
		entries.PushBack(item, allocator);
	}
	doc.AddMember("entries", entries, allocator);
	return finish_json_resource_payload(descriptor, doc, base_alloc, output);
}

auto emit_snd_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::Binary,
		rsrcd::Bytes{nullptr, 0},
		"audio/wav",
		"converted 'snd ' resource audio");
	if(!output.wants_resource_payload(descriptor))
		return true;

	PlatformByteVector wav_data;
	std::string error;
	if(!ConvertSndResourceToWav(resource.data, wav_data, error))
		return output.on_resource_error(ref, error.c_str());

	descriptor.data = rsrcd::Bytes{wav_data.data(), wav_data.size()};
	return output.on_resource_payload(descriptor);
}

auto emit_code_resource_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output,
	bool powerpc) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::TextUtf8,
		rsrcd::Bytes{nullptr, 0},
		"text/x-asm; charset=utf-8",
		powerpc ? "PowerPC disassembly" : "Mac 68K disassembly");
	if(!output.wants_resource_payload(descriptor))
		return true;

	Mac68kDisassemblyResult disassembly = powerpc
		? DisassemblePowerPCCodeResource(std::span<const uint8_t>(resource.data.data, resource.data.size), 0, resource.data.size, 0)
		: DisassembleMac68kCodeResource(std::span<const uint8_t>(resource.data.data, resource.data.size), 0, resource.data.size, 0);
	if(!disassembly.ok)
		return output.on_resource_error(ref, disassembly.error.c_str());

	descriptor.data = rsrcd::Bytes{reinterpret_cast<const uint8_t*>(disassembly.text.data()), disassembly.text.size()};
	return output.on_resource_payload(descriptor);
}

auto emit_pict_transform(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::Binary,
		rsrcd::Bytes{nullptr, 0},
		"image/png",
		"rendered PICT image");
	if(!output.wants_resource_payload(descriptor))
		return true;

	std::vector<uint8_t> png;
	uint32_t width = 0;
	uint32_t height = 0;
	std::string error;
	if(!DecodePictWithResourceDasmToPng(resource.data.data, resource.data.size, png, width, height, error))
		return output.on_resource_error(ref, error.c_str());

	descriptor.data = rsrcd::Bytes{png.data(), png.size()};
	descriptor.width = width;
	descriptor.height = height;
	return output.on_resource_payload(descriptor);
}

} // namespace

auto emit_builtin_resource_transforms(
	const rsrcd::ResRef& resource,
	const ResourceRef& ref,
	IResourceOutput& output) -> bool
{
	if(resource_type_is(resource, "XCMD") || resource_type_is(resource, "XFCN"))
		return emit_code_resource_transform(resource, ref, output, false);

	if(resource_type_is(resource, "xcmd") || resource_type_is(resource, "xfcn"))
		return emit_code_resource_transform(resource, ref, output, true);

	if(resource_type_is(resource, "PICT"))
		return emit_pict_transform(resource, ref, output);

	if(resource_type_is(resource, "ICON") && resource.data.size == 128)
	{
		ResourcePayload descriptor = make_converted_resource_payload(
			ref,
			ResourcePayloadFormat::Rgba32,
			rsrcd::Bytes{nullptr, 32u * 32u * 4u},
			"image/x-rgba32",
			"decoded 32x32 ICON pixels");
		descriptor.width = 32;
		descriptor.height = 32;
		descriptor.row_bytes = 32 * 4;
		if(!output.wants_resource_payload(descriptor))
			return true;

		uint8_t rgba[32 * 32 * 4];
		rsrcd::MutableBytes dst{rgba, sizeof(rgba)};
		if(!rsrcd::img::decode_icon_bw(resource.data, dst))
			return true;
		swap_bgra_to_rgba(rgba, 32u * 32u);
		descriptor.data = rsrcd::Bytes{rgba, sizeof(rgba)};
		return output.on_resource_payload(descriptor);
	}

	if(resource_type_is(resource, "ICN#") && resource.data.size >= 256)
	{
		ResourcePayload descriptor = make_converted_resource_payload(
			ref,
			ResourcePayloadFormat::Rgba32,
			rsrcd::Bytes{nullptr, 32u * 32u * 4u},
			"image/x-rgba32",
			"decoded 32x32 ICN# pixels");
		descriptor.width = 32;
		descriptor.height = 32;
		descriptor.row_bytes = 32 * 4;
		if(!output.wants_resource_payload(descriptor))
			return true;

		uint8_t rgba[32 * 32 * 4];
		rsrcd::MutableBytes dst{rgba, sizeof(rgba)};
		if(!rsrcd::img::decode_icn_bw(resource.data, dst))
			return true;
		swap_bgra_to_rgba(rgba, 32u * 32u);
		descriptor.data = rsrcd::Bytes{rgba, sizeof(rgba)};
		return output.on_resource_payload(descriptor);
	}

	if(resource_type_is(resource, "CURS") && resource.data.size >= 68)
	{
		ResourcePayload descriptor = make_converted_resource_payload(
			ref,
			ResourcePayloadFormat::Rgba32,
			rsrcd::Bytes{nullptr, 16u * 16u * 4u},
			"image/x-rgba32",
			"decoded 16x16 CURS pixels");
		descriptor.width = 16;
		descriptor.height = 16;
		descriptor.row_bytes = 16 * 4;
		if(!output.wants_resource_payload(descriptor))
			return true;

		uint8_t rgba[16 * 16 * 4];
		rsrcd::MutableBytes dst{rgba, sizeof(rgba)};
		int16_t hot_x = 0;
		int16_t hot_y = 0;
		if(!rsrcd::img::decode_curs(resource.data, dst, hot_x, hot_y))
			return true;
		swap_bgra_to_rgba(rgba, 16u * 16u);
		descriptor.data = rsrcd::Bytes{rgba, sizeof(rgba)};
		descriptor.hotspot_x = hot_x;
		descriptor.hotspot_y = hot_y;
		return output.on_resource_payload(descriptor);
	}

	if(resource_type_is(resource, "PAT#"))
	{
		const size_t pat_count = rsrcd::patlist::count(resource.data);
		for(size_t pi = 0; pi < pat_count; ++pi)
		{
			rsrcd::Bytes pat = rsrcd::patlist::pattern_at(resource.data, pi);
			if(pat.size != 8)
				continue;

			ResourcePayload descriptor = make_converted_resource_payload(
				ref,
				ResourcePayloadFormat::Rgba32,
				rsrcd::Bytes{nullptr, 8u * 8u * 4u},
				"image/x-rgba32",
				"decoded 8x8 PAT# pattern pixels");
			descriptor.variant_index = static_cast<uint32_t>(pi);
			descriptor.width = 8;
			descriptor.height = 8;
			descriptor.row_bytes = 8 * 4;
			if(!output.wants_resource_payload(descriptor))
				continue;

			uint8_t rgba[8 * 8 * 4];
			rsrcd::MutableBytes dst{rgba, sizeof(rgba)};
			if(!rsrcd::img::decode_pat(pat, dst))
				continue;
			swap_bgra_to_rgba(rgba, 8u * 8u);
			descriptor.data = rsrcd::Bytes{rgba, sizeof(rgba)};
			if(!output.on_resource_payload(descriptor))
				return false;
		}
	}

	if(resource_type_is(resource, "PAT ") && resource.data.size >= 8)
	{
		ResourcePayload descriptor = make_converted_resource_payload(
			ref,
			ResourcePayloadFormat::Rgba32,
			rsrcd::Bytes{nullptr, 8u * 8u * 4u},
			"image/x-rgba32",
			"decoded 8x8 PAT pattern pixels");
		descriptor.width = 8;
		descriptor.height = 8;
		descriptor.row_bytes = 8 * 4;
		if(!output.wants_resource_payload(descriptor))
			return true;

		uint8_t rgba[8 * 8 * 4];
		rsrcd::MutableBytes dst{rgba, sizeof(rgba)};
		if(!rsrcd::img::decode_pat(resource.data, dst))
			return true;
		swap_bgra_to_rgba(rgba, 8u * 8u);
		descriptor.data = rsrcd::Bytes{rgba, sizeof(rgba)};
		return output.on_resource_payload(descriptor);
	}

	if(resource_type_is(resource, "SICN"))
	{
		if((resource.data.size % 32u) != 0)
			return output.on_resource_error(ref, "SICN size is not a multiple of 32 bytes");
		const size_t icon_count = resource.data.size / 32u;
		for(size_t si = 0; si < icon_count; ++si)
		{
			ResourcePayload descriptor = make_converted_resource_payload(
				ref,
				ResourcePayloadFormat::Rgba32,
				rsrcd::Bytes{nullptr, 16u * 16u * 4u},
				"image/x-rgba32",
				"decoded 16x16 SICN pixels");
			descriptor.variant_index = static_cast<uint32_t>(si);
			descriptor.width = 16;
			descriptor.height = 16;
			descriptor.row_bytes = 16 * 4;
			if(!output.wants_resource_payload(descriptor))
				continue;

			uint8_t rgba[16 * 16 * 4];
			rsrcd::MutableBytes dst{rgba, sizeof(rgba)};
			rsrcd::Bytes sicn = resource.data.slice(si * 32u, 32u);
			if(!rsrcd::img::decode_sicn(sicn, dst))
				continue;
			swap_bgra_to_rgba(rgba, 16u * 16u);
			descriptor.data = rsrcd::Bytes{rgba, sizeof(rgba)};
			if(!output.on_resource_payload(descriptor))
				return false;
		}
	}

	IndexedIconSpec indexedSpec{};
	if(indexed_icon_spec(resource, indexedSpec))
	{
		const size_t pixelCount = static_cast<size_t>(indexedSpec.width) * indexedSpec.height;
		ResourcePayload descriptor = make_converted_resource_payload(
			ref,
			ResourcePayloadFormat::Rgba32,
			rsrcd::Bytes{nullptr, pixelCount * 4u},
			"image/x-rgba32",
			indexedSpec.description);
		descriptor.width = indexedSpec.width;
		descriptor.height = indexedSpec.height;
		descriptor.row_bytes = indexedSpec.width * 4;
		if(!output.wants_resource_payload(descriptor))
			return true;

		uint8_t rgba[32 * 32 * 4];
		rsrcd::MutableBytes dst{rgba, sizeof(rgba)};
		rsrcd::Result decodeResult = indexedSpec.bits_per_pixel == 4 ?
			rsrcd::img::decode_icon_4bit(resource.data, indexedSpec.width, indexedSpec.height, dst) :
			rsrcd::img::decode_icon_8bit(resource.data, indexedSpec.width, indexedSpec.height, dst);
		if(!decodeResult)
			return output.on_resource_error(ref, decodeResult.message());
		swap_bgra_to_rgba(rgba, pixelCount);
		descriptor.data = rsrcd::Bytes{rgba, pixelCount * 4u};
		return output.on_resource_payload(descriptor);
	}

	MonochromeIconListSpec monoSpec{};
	if(monochrome_icon_list_spec(resource, monoSpec))
	{
		size_t iconCount = 0;
		rsrcd::Result countResult = rsrcd::img::count_icon_1bit_images(resource.data, monoSpec.width, monoSpec.height, iconCount);
		if(!countResult)
			return output.on_resource_error(ref, countResult.message());

		const size_t pixelCount = static_cast<size_t>(monoSpec.width) * monoSpec.height;
		if(iconCount == 2)
		{
			ResourcePayload descriptor = make_converted_resource_payload(
				ref,
				ResourcePayloadFormat::Rgba32,
				rsrcd::Bytes{nullptr, pixelCount * 4u},
				"image/x-rgba32",
				monoSpec.composite_description);
			descriptor.width = monoSpec.width;
			descriptor.height = monoSpec.height;
			descriptor.row_bytes = monoSpec.width * 4;
			if(!output.wants_resource_payload(descriptor))
				return true;

			uint8_t rgba[16 * 16 * 4];
			rsrcd::MutableBytes dst{rgba, sizeof(rgba)};
			rsrcd::Result decodeResult = rsrcd::img::decode_icon_1bit_masked_pair(resource.data, monoSpec.width, monoSpec.height, dst);
			if(!decodeResult)
				return output.on_resource_error(ref, decodeResult.message());
			swap_bgra_to_rgba(rgba, pixelCount);
			descriptor.data = rsrcd::Bytes{rgba, pixelCount * 4u};
			return output.on_resource_payload(descriptor);
		}

		for(size_t ii = 0; ii < iconCount; ++ii)
		{
			ResourcePayload descriptor = make_converted_resource_payload(
				ref,
				ResourcePayloadFormat::Rgba32,
				rsrcd::Bytes{nullptr, pixelCount * 4u},
				"image/x-rgba32",
				monoSpec.image_description);
			descriptor.variant_index = static_cast<uint32_t>(ii);
			descriptor.width = monoSpec.width;
			descriptor.height = monoSpec.height;
			descriptor.row_bytes = monoSpec.width * 4;
			if(!output.wants_resource_payload(descriptor))
				continue;

			uint8_t rgba[16 * 16 * 4];
			rsrcd::MutableBytes dst{rgba, sizeof(rgba)};
			rsrcd::Result decodeResult = rsrcd::img::decode_icon_1bit_list_image(resource.data, monoSpec.width, monoSpec.height, ii, dst);
			if(!decodeResult)
				return output.on_resource_error(ref, decodeResult.message());
			swap_bgra_to_rgba(rgba, pixelCount);
			descriptor.data = rsrcd::Bytes{rgba, pixelCount * 4u};
			if(!output.on_resource_payload(descriptor))
				return false;
		}
	}

	if(resource_type_is(resource, "PLTE"))
		return emit_plte_transform(resource, ref, output);

	if(resource_type_is(resource, "cfrg"))
		return emit_cfrg_transform(resource, ref, output);

	if(resource_type_is(resource, "MBAR"))
		return emit_mbar_transform(resource, ref, output);

	if(resource_type_is(resource, "ALRT"))
		return emit_alrt_transform(resource, ref, output);

	if(resource_type_is(resource, "FREF"))
		return emit_fref_transform(resource, ref, output);

	if(resource_type_is(resource, "BNDL"))
		return emit_bndl_transform(resource, ref, output);

	if(resource_type_is(resource, "ROv#"))
		return emit_rov_transform(resource, ref, output);

	if(resource_type_is(resource, "RSSC"))
		return emit_rssc_transform(resource, ref, output);

	if(resource_type_is(resource, "TxSt"))
		return emit_txst_transform(resource, ref, output);

	if(resource_type_is(resource, "styl"))
		return emit_styl_transform(resource, ref, output);

	if(resource_type_is(resource, "RECT"))
		return emit_rect_transform(resource, ref, output);

	if(resource_type_is(resource, "TOOL"))
		return emit_tool_transform(resource, ref, output);

	if(resource_type_is(resource, "PICK"))
		return emit_pick_transform(resource, ref, output);

	if(resource_type_is(resource, "KBDN"))
		return emit_kbdn_transform(resource, ref, output);

	if(resource_type_is(resource, "PAPA"))
		return emit_papa_transform(resource, ref, output);

	if(resource_type_is(resource, "LAYO"))
		return emit_layo_transform(resource, ref, output);

	if(resource_type_is(resource, "CODE"))
		return emit_code_transform(resource, ref, output);

	if(resource_type_is(resource, "DRVR"))
		return emit_drvr_transform(resource, ref, output);

	if(resource_type_is(resource, "dcmp"))
		return emit_dcmp_transform(resource, ref, output);

	if(resource_type_is(resource, "HCbg"))
		return emit_addcolor_transform(resource, ref, output, "background");

	if(resource_type_is(resource, "HCcd"))
		return emit_addcolor_transform(resource, ref, output, "card");

	if(resource_type_is(resource, "STR "))
		return emit_text_transform(resource, ref, output, true);

	if(resource_type_is(resource, "TEXT"))
		return emit_text_transform(resource, ref, output, false);

	if(resource_type_is(resource, "STR#"))
		return emit_string_list_transform(resource, ref, output);

	if(resource_type_is(resource, "TwCS"))
		return emit_string_list_transform(resource, ref, output);

	if(resource_type_is(resource, "vers"))
		return emit_vers_transform(resource, ref, output);

	if(resource_type_is(resource, "clut") || resource_type_is(resource, "CTBL") ||
		resource_type_is(resource, "actb") || resource_type_is(resource, "cctb") ||
		resource_type_is(resource, "dctb") || resource_type_is(resource, "fctb") ||
		resource_type_is(resource, "wctb"))
		return emit_color_table_transform(resource, ref, output);

	if(resource_type_is(resource, "pltt"))
		return emit_pltt_transform(resource, ref, output);

	if(resource_type_is(resource, "SIZE"))
		return emit_size_transform(resource, ref, output);

	if(resource_type_is(resource, "finf"))
		return emit_finf_transform(resource, ref, output);

	if(resource_type_is(resource, "CNTL"))
		return emit_cntl_transform(resource, ref, output);

	if(resource_type_is(resource, "DLOG"))
		return emit_dlog_transform(resource, ref, output);

	if(resource_type_is(resource, "WIND"))
		return emit_wind_transform(resource, ref, output);

	if(resource_type_is(resource, "MENU"))
		return emit_menu_transform(resource, ref, output);

	if(resource_type_is(resource, "DITL"))
		return emit_ditl_transform(resource, ref, output);

	if(resource_type_is(resource, "snd "))
		return emit_snd_transform(resource, ref, output);

	return true;
}

} // namespace stackimport
