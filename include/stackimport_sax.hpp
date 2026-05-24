// stackimport_sax.hpp - Zero-allocation Streaming Stack Parser
//
// Design principles:
// - Zero allocations in hot path
// - Client owns all buffers and file handles
// - Client injects IStackReader for streaming input
// - Client injects IBlockOutput for output (callbacks, not storage)
// - Non-owning references (BlockRef) - owns nothing
// - parse_view: zero-copy path, payload is a view into caller's buffer
// - parse: streaming path, payload read from IStackReader during callback
// - resource payload callbacks let clients opt into native or converted data
// - Strong domain types for block type/id

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "vendor/rsrcd/include/rsrcd.hpp"

namespace stackimport {

// ============================================================================
// Block type as strong domain type
// ============================================================================

struct BlockType {
    uint8_t v[4];

    constexpr BlockType() : v{0,0,0,0} {}
    constexpr BlockType(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : v{a,b,c,d} {}
    constexpr BlockType(const char* s) : v{
        static_cast<uint8_t>(s[0]),
        static_cast<uint8_t>(s[1]),
        static_cast<uint8_t>(s[2]),
        static_cast<uint8_t>(s[3])
    } {}

    static constexpr auto from_bytes(const uint8_t* p) -> BlockType {
        return BlockType{p[0], p[1], p[2], p[3]};
    }

    auto as_sv() const -> std::string_view {
        return std::string_view{reinterpret_cast<const char*>(v), 4};
    }
    constexpr auto to_uint32() const -> uint32_t {
        return static_cast<uint32_t>(v[0]) << 24 |
               static_cast<uint32_t>(v[1]) << 16 |
               static_cast<uint32_t>(v[2]) << 8 |
               static_cast<uint32_t>(v[3]);
    }
    constexpr auto eq(const BlockType& o) const -> bool {
        return v[0] == o.v[0] && v[1] == o.v[1] && v[2] == o.v[2] && v[3] == o.v[3];
    }
    constexpr auto eq(const char* s) const -> bool {
        return v[0] == static_cast<uint8_t>(s[0]) &&
               v[1] == static_cast<uint8_t>(s[1]) &&
               v[2] == static_cast<uint8_t>(s[2]) &&
               v[3] == static_cast<uint8_t>(s[3]);
    }
};

constexpr bool operator==(BlockType a, BlockType b) { return a.eq(b); }
constexpr bool operator!=(BlockType a, BlockType b) { return !a.eq(b); }

// ============================================================================
// Block identifier
// ============================================================================

struct BlockID {
    int32_t value;

    constexpr BlockID() : value(0) {}
    constexpr explicit BlockID(int32_t v) : value(v) {}

    constexpr auto get() const -> int32_t { return value; }
    constexpr operator int32_t() const { return value; }
};

constexpr bool operator==(BlockID a, BlockID b) { return a.value == b.value; }
constexpr bool operator!=(BlockID a, BlockID b) { return a.value != b.value; }

// ============================================================================
// Block reference - non-owning, no allocation
// ============================================================================

struct BlockRef {
    BlockType type;
    BlockID id;
    rsrcd::Bytes payload;
    uint64_t file_offset;
    uint32_t payload_bytes;

    constexpr auto empty() const -> bool { return payload.size == 0; }
    constexpr auto size() const -> uint32_t { return payload_bytes; }
};

// ============================================================================
// Resource streaming references - non-owning, no allocation
// ============================================================================

enum class ResourcePayloadFormat : uint8_t {
    Native = 0,
    Rgba32 = 1,
    JsonUtf8 = 2,
    TextUtf8 = 3,
};

struct ResourceRef {
    rsrcd::FourCC type;
    int32_t id;
    rsrcd::Bytes name;
    uint8_t flags;
    uint32_t order;
    size_t native_size;
};

struct ResourcePayload {
    ResourceRef resource;
    ResourcePayloadFormat format;
    rsrcd::Bytes data;
    uint32_t variant_index;
    uint32_t width;
    uint32_t height;
    uint32_t row_bytes;
    int32_t hotspot_x;
    int32_t hotspot_y;
    const char* media_type;
    const char* description;
};

// ============================================================================
// Well-known block types
// ============================================================================

namespace block_type {
    constexpr BlockType STAK{"STAK"};
    constexpr BlockType CARD{"CARD"};
    constexpr BlockType LIST{"LIST"};
    constexpr BlockType PAGE{"PAGE"};
    constexpr BlockType BKGD{"BKGD"};
    constexpr BlockType BMAP{"BMAP"};
    constexpr BlockType FTBL{"FTBL"};
    constexpr BlockType STBL{"STBL"};
    constexpr BlockType MAST{"MAST"};
    constexpr BlockType PRNT{"PRNT"};
    constexpr BlockType PRST{"PRST"};
    constexpr BlockType PRFT{"PRFT"};
    constexpr BlockType FREE{"FREE"};
    constexpr BlockType TAIL{"TAIL"};

    constexpr auto is_card(BlockType t) { return t == CARD; }
    constexpr auto is_background(BlockType t) { return t == BKGD; }
    constexpr auto is_list(BlockType t) { return t == LIST; }
    constexpr auto is_page(BlockType t) { return t == PAGE; }
    constexpr auto is_free(BlockType t) { return t == FREE; }
    constexpr auto is_tail(BlockType t) { return t == TAIL; }
    constexpr auto is_metadata(BlockType t) { return t == STAK || t == FTBL || t == STBL; }
}

// ============================================================================
// IStackReader - streaming input interface (client implements)
// ============================================================================

class IStackReader {
public:
    virtual ~IStackReader() = default;
    virtual auto read(uint8_t* dst, size_t len) -> size_t = 0;
    virtual auto seek(size_t pos) -> bool = 0;
    virtual auto position() const -> size_t = 0;
    virtual auto size() const -> size_t = 0;
};

// ============================================================================
// IBlockOutput - callback interface for parsed blocks (client implements)
//
// Streaming path (parse): payload data is at the reader's current position.
//   The callback reads payload_bytes from reader, then returns.
//   The parser advances past the payload after the callback returns.
//
// Zero-copy path (parse_view): ref.payload is a view into the source buffer.
//   No reader access needed; the callback processes ref.payload directly.
// ============================================================================

class IBlockOutput {
public:
    virtual ~IBlockOutput() = default;

    virtual auto on_block(const BlockRef& block, IStackReader& reader) -> bool = 0;
    virtual auto on_error(const char* msg) -> bool = 0;
};

// ============================================================================
// IResourceOutput - callback interface for native and converted resources
//
// The importer first calls wants_resource_payload with a descriptor whose data
// may be empty. Returning false lets the client skip conversion and delivery.
// on_resource_payload receives a non-owning view valid only for the callback.
// ============================================================================

class IResourceOutput {
public:
    virtual ~IResourceOutput() = default;

    virtual auto wants_resource_payload(const ResourcePayload& payload) -> bool {
        (void)payload;
        return true;
    }
    virtual auto on_resource_payload(const ResourcePayload& payload) -> bool = 0;
    virtual auto on_resource_error(const ResourceRef& resource, const char* msg) -> bool {
        (void)resource;
        (void)msg;
        return true;
    }
};

struct ResourceWalkOptions {
    bool emit_native;
    bool emit_converted;

    constexpr ResourceWalkOptions() : emit_native(true), emit_converted(true) {}
};

inline auto resource_ref_from_resref(const rsrcd::ResRef& res) -> ResourceRef {
    ResourceRef ref{};
    if (res.type.size == 4 && res.type.data != nullptr) {
        ref.type = rsrcd::FourCC::from_bytes(res.type.data);
    }
    ref.id = res.id;
    ref.name = res.name;
    ref.flags = res.flags;
    ref.order = res.order;
    ref.native_size = res.data.size;
    return ref;
}

inline void swap_resource_bgra_to_rgba(uint8_t* data, size_t pixel_count) {
    for (size_t i = 0; i < pixel_count; ++i) {
        uint8_t tmp = data[i * 4];
        data[i * 4] = data[i * 4 + 2];
        data[i * 4 + 2] = tmp;
    }
}

inline auto emit_resource_payload(IResourceOutput& output, ResourcePayload payload) -> bool {
    ResourcePayload descriptor = payload;
    descriptor.data.data = nullptr;
    if (!output.wants_resource_payload(descriptor)) {
        return true;
    }
    return output.on_resource_payload(payload);
}

class ResourceForkParser {
public:
    auto parse_fork(rsrcd::Bytes buf,
                    IResourceOutput& output,
                    ResourceWalkOptions options = ResourceWalkOptions{}) -> rsrcd::Result {
        rsrcd::VecParserOutput<256> parsed;
        auto result = rsrcd::Parser{}.parse_fork(buf, parsed);
        if (!result) {
            ResourceRef empty{};
            output.on_resource_error(empty, result.message());
            return result;
        }

        for (size_t i = 0; i < parsed.count(); ++i) {
            const rsrcd::ResRef& res = parsed.at(i);
            if (res.type.size != 4 || res.type.data == nullptr) {
                ResourceRef invalid{};
                invalid.id = res.id;
                invalid.flags = res.flags;
                invalid.order = res.order;
                invalid.native_size = res.data.size;
                if (!output.on_resource_error(invalid, "invalid resource type")) {
                    break;
                }
                continue;
            }

            const ResourceRef ref = resource_ref_from_resref(res);
            if (options.emit_native) {
                ResourcePayload native{};
                native.resource = ref;
                native.format = ResourcePayloadFormat::Native;
                native.data = res.data;
                native.media_type = "application/octet-stream";
                native.description = "native resource data";
                if (!emit_resource_payload(output, native)) {
                    break;
                }
            }

            if (!options.emit_converted) {
                continue;
            }

            if (res.type.size == 4 && std::memcmp(res.type.data, "ICON", 4) == 0 && res.data.size == 128) {
                ResourcePayload descriptor{};
                descriptor.resource = ref;
                descriptor.format = ResourcePayloadFormat::Rgba32;
                descriptor.data = rsrcd::Bytes{nullptr, 32u * 32u * 4u};
                descriptor.width = 32;
                descriptor.height = 32;
                descriptor.row_bytes = 32 * 4;
                descriptor.media_type = "image/x-rgba32";
                descriptor.description = "decoded 32x32 ICON pixels";
                if (output.wants_resource_payload(descriptor)) {
                    uint8_t rgba[32 * 32 * 4];
                    rsrcd::MutableBytes dst{rgba, sizeof(rgba)};
                    if (rsrcd::img::decode_icon_bw(res.data, dst)) {
                        swap_resource_bgra_to_rgba(rgba, 32u * 32u);
                        descriptor.data = rsrcd::Bytes{rgba, sizeof(rgba)};
                        if (!output.on_resource_payload(descriptor)) {
                            break;
                        }
                    }
                }
            } else if (res.type.size == 4 && std::memcmp(res.type.data, "CURS", 4) == 0 && res.data.size >= 68) {
                ResourcePayload descriptor{};
                descriptor.resource = ref;
                descriptor.format = ResourcePayloadFormat::Rgba32;
                descriptor.data = rsrcd::Bytes{nullptr, 16u * 16u * 4u};
                descriptor.width = 16;
                descriptor.height = 16;
                descriptor.row_bytes = 16 * 4;
                descriptor.media_type = "image/x-rgba32";
                descriptor.description = "decoded 16x16 CURS pixels";
                if (output.wants_resource_payload(descriptor)) {
                    uint8_t rgba[16 * 16 * 4];
                    rsrcd::MutableBytes dst{rgba, sizeof(rgba)};
                    int16_t hot_x = 0;
                    int16_t hot_y = 0;
                    if (rsrcd::img::decode_curs(res.data, dst, hot_x, hot_y)) {
                        swap_resource_bgra_to_rgba(rgba, 16u * 16u);
                        descriptor.data = rsrcd::Bytes{rgba, sizeof(rgba)};
                        descriptor.hotspot_x = hot_x;
                        descriptor.hotspot_y = hot_y;
                        if (!output.on_resource_payload(descriptor)) {
                            break;
                        }
                    }
                }
            } else if (res.type.size == 4 && std::memcmp(res.type.data, "PAT#", 4) == 0) {
                const size_t pat_count = rsrcd::patlist::count(res.data);
                for (size_t pi = 0; pi < pat_count; ++pi) {
                    rsrcd::Bytes pat = rsrcd::patlist::pattern_at(res.data, pi);
                    if (pat.size != 8) {
                        continue;
                    }
                    ResourcePayload descriptor{};
                    descriptor.resource = ref;
                    descriptor.format = ResourcePayloadFormat::Rgba32;
                    descriptor.data = rsrcd::Bytes{nullptr, 8u * 8u * 4u};
                    descriptor.variant_index = static_cast<uint32_t>(pi);
                    descriptor.width = 8;
                    descriptor.height = 8;
                    descriptor.row_bytes = 8 * 4;
                    descriptor.media_type = "image/x-rgba32";
                    descriptor.description = "decoded 8x8 PAT# pattern pixels";
                    if (!output.wants_resource_payload(descriptor)) {
                        continue;
                    }
                    uint8_t rgba[8 * 8 * 4];
                    rsrcd::MutableBytes dst{rgba, sizeof(rgba)};
                    if (rsrcd::img::decode_pat(pat, dst)) {
                        swap_resource_bgra_to_rgba(rgba, 8u * 8u);
                        descriptor.data = rsrcd::Bytes{rgba, sizeof(rgba)};
                        if (!output.on_resource_payload(descriptor)) {
                            return rsrcd::Result::ok();
                        }
                    }
                }
            }
        }

        return rsrcd::Result::ok();
    }
};

// ============================================================================
// Error codes for block parsing
// ============================================================================

enum class BlockErr : uint8_t {
    None = 0,
    InvalidHeader,
    TruncatedHeader,
    TruncatedPayload,
    InvalidSize,
    ReadFailed,
    UnexpectedBlock,
};

// ============================================================================
// Block parser
// ============================================================================

class BlockParser {
public:
    BlockParser() = default;

    // Streaming parse: reads headers, calls on_block with reader positioned at
    // payload. Callback reads payload from reader, parser advances after return.
    auto parse(IStackReader& input, IBlockOutput& output) -> BlockErr {
        constexpr size_t HEADER_SIZE = 12;

        while (true) {
            uint8_t header[HEADER_SIZE];

            size_t r = input.read(header, HEADER_SIZE);
            if (r == 0) break;
            if (r != HEADER_SIZE) {
                if (!output.on_error("truncated header")) return BlockErr::TruncatedHeader;
                return BlockErr::TruncatedHeader;
            }

            uint32_t block_size = rsrcd::read_u32be(header + 0);
            BlockType block_type = BlockType::from_bytes(header + 4);
            int32_t block_id = rsrcd::read_i32be(header + 8);

            if (block_size < HEADER_SIZE) {
                if (!output.on_error("invalid block size")) return BlockErr::InvalidSize;
                return BlockErr::InvalidSize;
            }

            uint32_t payload_bytes = block_size - HEADER_SIZE;
            size_t file_offset = input.position() - HEADER_SIZE;

            if (block_type == block_type::TAIL && block_id == -1) {
                break;
            }

            if (block_type == block_type::FREE) {
                if (payload_bytes > 0) {
                    if (!input.seek(input.position() + payload_bytes)) {
                        output.on_error("seek failed on FREE block");
                        return BlockErr::ReadFailed;
                    }
                }
                continue;
            }

            BlockRef ref;
            ref.type = block_type;
            ref.id = BlockID{block_id};
            ref.file_offset = file_offset;
            ref.payload_bytes = payload_bytes;
            // ref.payload is empty; reader is positioned at payload data

            if (!output.on_block(ref, input)) {
                break;
            }

            {
                size_t next_pos = file_offset + HEADER_SIZE + payload_bytes;
                if (input.position() != next_pos) {
                    if (!input.seek(next_pos)) {
                        output.on_error("seek failed after payload");
                        return BlockErr::ReadFailed;
                    }
                }
            }
        }

        return BlockErr::None;
    }

    // Zero-copy parse: walks buf reading block headers, sets ref.payload as a
    // view into buf. No allocation, no copies. Caller owns the buffer lifetime.
    auto parse_view(rsrcd::Bytes buf, IBlockOutput& output) -> BlockErr {
        constexpr size_t HEADER_SIZE = 12;
        size_t pos = 0;

        while (pos + HEADER_SIZE <= buf.size) {
            uint32_t block_size = rsrcd::read_u32be(buf.data + pos);
            BlockType block_type = BlockType::from_bytes(buf.data + pos + 4);
            int32_t block_id = rsrcd::read_i32be(buf.data + pos + 8);

            if (block_size < HEADER_SIZE) {
                output.on_error("invalid block size");
                return BlockErr::InvalidSize;
            }

            uint32_t payload_bytes = block_size - HEADER_SIZE;
            uint64_t file_offset = pos;

            if (block_type == block_type::TAIL && block_id == -1) {
                break;
            }

            pos += HEADER_SIZE;

            if (block_type == block_type::FREE) {
                pos += payload_bytes;
                continue;
            }

            BlockRef ref;
            ref.type = block_type;
            ref.id = BlockID{block_id};
            ref.file_offset = file_offset;
            ref.payload_bytes = payload_bytes;
            if (payload_bytes > 0 && pos + payload_bytes <= buf.size) {
                ref.payload = buf.slice(pos, payload_bytes);
            }

            pos += payload_bytes;

            struct NullReader : IStackReader {
                auto read(uint8_t*, size_t) -> size_t override { return 0; }
                auto seek(size_t) -> bool override { return false; }
                auto position() const -> size_t override { return 0; }
                auto size() const -> size_t override { return 0; }
            };
            static NullReader null_reader;
            if (!output.on_block(ref, null_reader)) {
                break;
            }
        }

        return BlockErr::None;
    }

private:
    class NullReader : IStackReader {
    public:
        auto read(uint8_t*, size_t) -> size_t override { return 0; }
        auto seek(size_t) -> bool override { return false; }
        auto position() const -> size_t override { return 0; }
        auto size() const -> size_t override { return 0; }
    };
    NullReader null_reader_;
};

// ============================================================================
// FileStackReader - file-based IStackReader (owns the FILE*)
// ============================================================================

class FileStackReader : public IStackReader {
public:
    FileStackReader() : file_(nullptr), pos_(0), size_(0) {}

    bool open(const char* path) {
        file_ = fopen(path, "rb");
        if (!file_) return false;
        fseek(file_, 0, SEEK_END);
        size_ = static_cast<size_t>(ftell(file_));
        fseek(file_, 0, SEEK_SET);
        pos_ = 0;
        return true;
    }

    void close() {
        if (file_) {
            fclose(file_);
            file_ = nullptr;
        }
        pos_ = 0;
        size_ = 0;
    }

    ~FileStackReader() override { close(); }

    auto read(uint8_t* dst, size_t len) -> size_t override {
        if (!file_) return 0;
        size_t r = fread(dst, 1, len, file_);
        pos_ += r;
        return r;
    }

    auto seek(size_t p) -> bool override {
        if (!file_) return false;
        if (p > size_) p = size_;
        pos_ = p;
        return fseek(file_, static_cast<long>(p), SEEK_SET) == 0;
    }

    auto position() const -> size_t override { return pos_; }
    auto size() const -> size_t override { return size_; }

private:
    FILE* file_;
    size_t pos_;
    size_t size_;
};

// ============================================================================
// Block summary for diagnostics
// ============================================================================

struct BlockSummary {
    BlockType type;
    BlockID id;
    uint64_t offset;
    uint32_t size;
    uint32_t payload_bytes;
    const char* status;

    constexpr auto file_offset() const -> uint64_t { return offset; }
    constexpr auto payload_offset() const -> uint64_t { return offset + 12; }
};

} // namespace stackimport
