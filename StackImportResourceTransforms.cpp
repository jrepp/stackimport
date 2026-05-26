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
	ResourcePayload descriptor = make_converted_resource_payload(
		ref,
		ResourcePayloadFormat::JsonUtf8,
		rsrcd::Bytes{nullptr, 0},
		"application/json",
		"decoded STR# string list");
	if(!output.wants_resource_payload(descriptor))
		return true;

	rsrcd::text::StringList<256> strings;
	auto text_result = rsrcd::text::parse_str_list(resource.data, strings);
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

	if(resource_type_is(resource, "PLTE"))
		return emit_plte_transform(resource, ref, output);

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
