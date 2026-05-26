// rsrcd - Zero-allocation Resource Fork Parser
//
// MIT License
// Derived from rsrcdump by Iliyas Jorio (https://github.com/jorio/rsrcdump)
//
// Zero-allocation design principles:
// - All parsing produces non-owning views (Bytes, ResRef)
// - Client provides IReader, IWriter, IAllocator implementations
// - Iterator protocol for zero-allocation traversal
// - No heap allocations in hot path
// - Header-only for maximum inlining

#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <optional>
#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

namespace rsrcd {

// ============================================================================
// Byte references - no ownership, no allocation
// ============================================================================

struct Bytes {
    const uint8_t* data;
    size_t size;

    constexpr Bytes() : data(nullptr), size(0) {}
    constexpr Bytes(const uint8_t* d, size_t s) : data(d), size(s) {}

    constexpr auto empty() const noexcept -> bool { return size == 0; }
    constexpr auto data_at(size_t offset) const noexcept -> const uint8_t* { return data + offset; }
    constexpr auto operator[](size_t offset) const noexcept -> uint8_t { return data[offset]; }
    constexpr auto slice(size_t offset, size_t count) const noexcept -> Bytes {
        return size <= offset ? Bytes{} : Bytes{data + offset, std::min(count, size - offset)};
    }
    constexpr auto last(size_t count) const noexcept -> Bytes {
        return count > size ? Bytes{} : Bytes{data + (size - count), count};
    }
    constexpr auto as_ptr() const -> const void* { return data; }

    template<typename T>
    constexpr auto as() const -> const T* { return reinterpret_cast<const T*>(data); }

    template<typename T>
    constexpr auto as_array() const -> std::span<const T> {
        return {reinterpret_cast<const T*>(data), size / sizeof(T)};
    }
};

struct MutableBytes {
    uint8_t* data;
    size_t size;

    constexpr MutableBytes() : data(nullptr), size(0) {}
    constexpr MutableBytes(uint8_t* d, size_t s) : data(d), size(s) {}

    constexpr auto empty() const noexcept -> bool { return size == 0; }
    constexpr auto data_at(size_t offset) const noexcept -> uint8_t* { return data + offset; }
    constexpr auto slice(size_t offset, size_t count) const noexcept -> MutableBytes {
        return size <= offset ? MutableBytes{} : MutableBytes{data + offset, std::min(count, size - offset)};
    }
    constexpr auto as_ptr() -> void* { return data; }

    template<typename T>
    constexpr auto as() -> T* { return reinterpret_cast<T*>(data); }
};

using ByteSpan = std::span<const uint8_t>;
using MutableByteSpan = std::span<uint8_t>;

// ============================================================================
// System interfaces - client implements these
// ============================================================================

class IReader {
public:
    virtual ~IReader() = default;
    virtual auto read(uint8_t* dst, size_t len) -> size_t = 0;
    virtual auto skip(size_t n) -> size_t = 0;
    virtual auto position() const -> size_t = 0;
    virtual auto seek(size_t pos) -> bool = 0;
    virtual auto size() const -> size_t = 0;
};

class IWriter {
public:
    virtual ~IWriter() = default;
    virtual auto write(const uint8_t* src, size_t len) -> size_t = 0;
    virtual auto position() const -> size_t = 0;
};

class IAllocator {
public:
    virtual ~IAllocator() = default;
    virtual auto alloc(size_t size, size_t align) -> void* = 0;
    virtual void free(void* ptr, size_t size) = 0;
};

// ============================================================================
// Error handling
// ============================================================================

enum class Err : uint8_t {
    None = 0,
    InvalidData,
    UnexpectedEnd,
    CompressionUnsupported,
    InvalidFormat,
    Bounds,
};

struct Result {
    uint8_t code;
    const char* msg;

    constexpr operator bool() const noexcept { return code == 0; }
    static constexpr auto ok() -> Result { return {0, nullptr}; }
    static constexpr auto err(uint8_t c, const char* m) -> Result { return {c, m}; }

    constexpr auto error_code() const -> Err { return static_cast<Err>(code); }
    constexpr auto message() const -> const char* { return msg; }
};

struct Error {
    static constexpr auto invalid_data(const char* msg = "invalid data") -> Result {
        return {static_cast<uint8_t>(Err::InvalidData), msg};
    }
    static constexpr auto unexpected_end() -> Result {
        return {static_cast<uint8_t>(Err::UnexpectedEnd), "unexpected end"};
    }
    static constexpr auto compression_unsupported() -> Result {
        return {static_cast<uint8_t>(Err::CompressionUnsupported), "compressed not supported"};
    }
    static constexpr auto invalid_format(const char* msg = "invalid format") -> Result {
        return {static_cast<uint8_t>(Err::InvalidFormat), msg};
    }
    static constexpr auto bounds() -> Result {
        return {static_cast<uint8_t>(Err::Bounds), "out of bounds"};
    }
};

// ============================================================================
// Memory operations - no allocation
// ============================================================================

constexpr auto read_u16be(const uint8_t* p) -> uint16_t {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
}

constexpr auto read_i16be(const uint8_t* p) -> int16_t {
    return static_cast<int16_t>(read_u16be(p));
}

constexpr auto read_u32be(const uint8_t* p) -> uint32_t {
    return static_cast<uint32_t>(p[0]) << 24 |
           static_cast<uint32_t>(p[1]) << 16 |
           static_cast<uint32_t>(p[2]) << 8 |
           static_cast<uint32_t>(p[3]);
}

constexpr auto read_i32be(const uint8_t* p) -> int32_t {
    return static_cast<int32_t>(read_u32be(p));
}

constexpr void write_u16be(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}

constexpr void write_u32be(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

// ============================================================================
// FourCC type helper
// ============================================================================

struct FourCC {
    uint8_t v[4];

    constexpr FourCC() : v{0,0,0,0} {}
    constexpr FourCC(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : v{a,b,c,d} {}
    constexpr FourCC(const char* s) : v{static_cast<uint8_t>(s[0]),
                                      static_cast<uint8_t>(s[1]),
                                      static_cast<uint8_t>(s[2]),
                                      static_cast<uint8_t>(s[3])} {}

    static constexpr auto from_bytes(const uint8_t* p) -> FourCC {
        return FourCC{p[0], p[1], p[2], p[3]};
    }

    static constexpr auto from_uint32(uint32_t x) -> FourCC {
        return FourCC{
            static_cast<uint8_t>(x >> 24),
            static_cast<uint8_t>(x >> 16),
            static_cast<uint8_t>(x >> 8),
            static_cast<uint8_t>(x)
        };
    }

    constexpr auto as_bytes() const -> Bytes { return Bytes{v, 4}; }
    constexpr auto to_uint32() const -> uint32_t {
        return static_cast<uint32_t>(v[0]) << 24 |
               static_cast<uint32_t>(v[1]) << 16 |
               static_cast<uint32_t>(v[2]) << 8 |
               static_cast<uint32_t>(v[3]);
    }
    constexpr auto eq(const FourCC& o) const -> bool {
        return v[0] == o.v[0] && v[1] == o.v[1] && v[2] == o.v[2] && v[3] == o.v[3];
    }
    constexpr auto eq(const char* s) const -> bool {
        return v[0] == static_cast<uint8_t>(s[0]) &&
               v[1] == static_cast<uint8_t>(s[1]) &&
               v[2] == static_cast<uint8_t>(s[2]) &&
               v[3] == static_cast<uint8_t>(s[3]);
    }
};

constexpr auto operator""_4cc(const char* s, size_t n) -> FourCC {
    (void)n;
    return FourCC{s};
}

// ============================================================================
// Resource reference - no allocation, no ownership
// ============================================================================

struct ResRef {
    Bytes type;
    int32_t id;
    Bytes data;
    Bytes name;
    uint8_t flags;
    uint32_t junk;
    uint32_t order;

    constexpr auto is_compressed() const -> bool { return (flags & 1) != 0; }
};

// ============================================================================
// Parser output interface - client implements to receive resources
// ============================================================================

class IParserOutput {
public:
    virtual ~IParserOutput() = default;
    virtual auto reserve(size_t count) -> Result = 0;
    virtual auto add(ResRef&& ref) -> Result = 0;
    virtual auto count() const -> size_t = 0;
};

// Simple vector-based output for when allocation is acceptable
// Client can provide their own implementation for zero-allocation
template<size_t InlineCapacity = 64>
class VecParserOutput : public IParserOutput {
public:
    auto reserve(size_t count) -> Result override {
        if (count > inline_capacity_) {
            heap_ = std::make_unique<ResRef[]>(count);
            capacity_ = count;
        }
        return Result::ok();
    }
    auto add(ResRef&& ref) -> Result override {
        if (count_ < inline_capacity_) {
            inline_[count_++] = ref;
        } else if (count_ < capacity_) {
            heap_[count_ - inline_capacity_] = ref;
            ++count_;
        } else {
            return Error::invalid_data("capacity exceeded");
        }
        return Result::ok();
    }
    auto count() const -> size_t override { return count_; }
    auto at(size_t i) const -> const ResRef& {
        return i < inline_capacity_ ? inline_[i] : heap_[i - inline_capacity_];
    }

private:
    static constexpr size_t inline_capacity_ = InlineCapacity;
    ResRef inline_[InlineCapacity];
    std::unique_ptr<ResRef[]> heap_;
    size_t capacity_ = InlineCapacity;
    size_t count_ = 0;
};

// ============================================================================
// Parser - zero-allocation hot path
// ============================================================================

class Parser {
public:
    Parser() = default;
    explicit Parser(IAllocator& alloc) : alloc_(&alloc) {}

    // Parse resource fork from reader
    auto parse(IReader& input, IParserOutput& output) -> Result {
        auto sz = input.size();
        if (sz > sizeof(buf_)) return Error::invalid_data("fork too large");
        size_t r = input.read(buf_, sz);
        if (r != sz) return Error::unexpected_end();
        return parse_fork(Bytes{buf_, sz}, output);
    }

    // Parse from direct buffer - preferred for zero allocation
    auto parse_fork(Bytes buf, IParserOutput& output) -> Result;
    auto parse_adf(Bytes buf, IParserOutput& output) -> Result;

    auto with_allocator(IAllocator& alloc) -> Parser { alloc_ = &alloc; return *this; }

private:
    uint8_t buf_[8192];
    IAllocator* alloc_ = nullptr;
};

// ============================================================================
// Fork view - owns nothing, just references
// ============================================================================

struct ForkView {
    Bytes original;
    uint32_t junk_nextresmap;
    uint16_t junk_filerefnum;
    uint16_t file_attributes;
    ResRef* resources;
    size_t count;
};

// ============================================================================
// Iterator - zero allocation traversal
// ============================================================================

struct Iter {
    const ResRef* resources;
    size_t count;
    size_t index;

    constexpr auto operator*() const -> const ResRef& { return resources[index]; }
    constexpr auto operator++() -> Iter& { ++index; return *this; }
    constexpr auto operator!=(const Iter& other) const -> bool { return index != other.index; }
    constexpr auto operator==(const Iter& other) const -> bool { return index == other.index; }
    constexpr explicit operator bool() const { return index < count; }

    constexpr auto operator-(const Iter& o) const -> size_t { return index - o.index; }
};

constexpr auto begin(const ForkView& f) -> Iter { return {f.resources, f.count, 0}; }
constexpr auto end(const ForkView& f) -> Iter { return {f.resources, f.count, f.count}; }

// Find by type/id - no allocation
constexpr auto find(const ForkView& f, FourCC type, int32_t id) -> const ResRef* {
    for (auto& r : f) {
        if (r.type.size == 4 && std::memcmp(r.type.data, type.v, 4) == 0 && r.id == id) {
            return &r;
        }
    }
    return nullptr;
}

// Find first by type - no allocation
constexpr auto find_first(const ForkView& f, FourCC type) -> const ResRef* {
    for (auto& r : f) {
        if (r.type.size == 4 && std::memcmp(r.type.data, type.v, 4) == 0) {
            return &r;
        }
    }
    return nullptr;
}

// ============================================================================
// Implementation
// ============================================================================

constexpr auto range_in_bounds(size_t offset, size_t length, size_t size) -> bool {
    return offset <= size && length <= size - offset;
}

inline Result Parser::parse_fork(Bytes buf, IParserOutput& output) {
    if (buf.size < 32) return Error::invalid_data("fork too small");

    uint32_t data_off = read_u32be(buf.data);
    uint32_t map_off = read_u32be(buf.data + 4);
    uint32_t data_len = read_u32be(buf.data + 8);
    uint32_t map_len = read_u32be(buf.data + 12);

    if (!range_in_bounds(data_off, data_len, buf.size) ||
        !range_in_bounds(map_off, map_len, buf.size)) {
        return Error::invalid_data("invalid offsets");
    }

    auto map = buf.slice(map_off, map_len);
    if (map.size < 30) return Error::invalid_data("map too small");

    uint16_t type_list_off = read_u16be(map.data + 24);
    uint16_t name_list_off = read_u16be(map.data + 26);
    if (type_list_off + 2 > map.size) return Error::invalid_data("type list out of bounds");
    uint16_t num_types = read_u16be(map.data + type_list_off);
    ++num_types;

    struct InlineType { FourCC type; size_t count; uint16_t offset; };
    InlineType types[64];
    size_t type_count = 0;

    for (uint16_t i = 0; i < num_types && i < 64; ++i) {
        size_t off = type_list_off + 2 + i * 8;
        if (off + 8 > map.size) break;
        types[i].type = FourCC::from_bytes(map.data + off);
        types[i].count = static_cast<size_t>(read_u16be(map.data + off + 4)) + 1;
        types[i].offset = read_u16be(map.data + off + 6);
        size_t res_off = type_list_off + types[i].offset;
        size_t available_refs = res_off <= map.size ? (map.size - res_off) / 12 : 0;
        types[i].count = std::min(types[i].count, available_refs);
        type_count = i + 1;
    }

    size_t total = 0;
    for (size_t i = 0; i < type_count; ++i) total += types[i].count;
    if (auto r = output.reserve(total); !r) return r;

    auto data_sec = buf.slice(data_off, data_len);
    size_t order = 0;

    for (size_t ti = 0; ti < type_count; ++ti) {
        size_t res_off = type_list_off + types[ti].offset;

        for (size_t ri = 0; ri < types[ti].count; ++ri) {
            if (!range_in_bounds(res_off, 12, map.size)) break;

            ResRef ref{};
            ref.type = Bytes{map.data + type_list_off + 2 + ti * 8, 4};
            ref.id = read_i16be(map.data + res_off);
            uint16_t name_off = read_u16be(map.data + res_off + 2);
            uint32_t packed = read_u32be(map.data + res_off + 4);
            ref.junk = read_u32be(map.data + res_off + 8);
            ref.flags = static_cast<uint8_t>(packed >> 24);
            uint32_t d_off = packed & 0x00FFFFFF;

            if (ref.flags & 1) return Error::compression_unsupported();

            if (name_off != 0xFFFF) {
                size_t np = name_list_off + name_off;
                if (np < map.size) {
                    uint8_t len = map.data[np];
                    if (np + 1 + len <= map.size) {
                        ref.name = map.slice(np + 1, len);
                    }
                }
            }

            if (d_off + 4 <= data_sec.size) {
                int32_t sz = read_i32be(data_sec.data + d_off);
                if (sz >= 0 && d_off + 4 + static_cast<uint32_t>(sz) <= data_sec.size) {
                    ref.data = data_sec.slice(d_off + 4, static_cast<size_t>(sz));
                }
            }

            ref.order = static_cast<uint32_t>(order++);
            if (auto r = output.add(std::move(ref)); !r) return r;
            res_off += 12;
        }
    }

    return Result::ok();
}

inline Result Parser::parse_adf(Bytes buf, IParserOutput& output) {
    if (buf.size < 26) return Error::invalid_data("ADF too small");

    uint32_t magic = read_u32be(buf.data);
    if (magic != 0x00051607) return Error::invalid_data("not AppleDouble");

    uint32_t version = read_u32be(buf.data + 4);
    if (version != 0x00020000) return Error::invalid_data("unsupported ADF version");

    uint16_t num_entries = read_u16be(buf.data + 22);
    if (num_entries > 16) return Error::invalid_data("too many entries");

    size_t off = 26;
    struct RawEntry { uint32_t id; uint32_t o; uint32_t l; };
    RawEntry entries[16];

    for (uint16_t i = 0; i < num_entries; ++i) {
        if (off + 12 > buf.size) return Error::unexpected_end();
        entries[i].id = read_u32be(buf.data + off);
        entries[i].o = read_u32be(buf.data + off + 4);
        entries[i].l = read_u32be(buf.data + off + 8);
        off += 12;
    }

    if (auto r = output.reserve(num_entries); !r) return r;

    for (uint16_t i = 0; i < num_entries; ++i) {
        ResRef ref;
        if (!range_in_bounds(entries[i].o, entries[i].l, buf.size)) {
            return Error::invalid_data("ADF entry out of bounds");
        }

        ref.type = Bytes{buf.data + 26 + i * 12, 4};
        ref.id = static_cast<int32_t>(entries[i].id);
        ref.data = buf.slice(entries[i].o, entries[i].l);
        ref.name = Bytes{};
        ref.flags = 0;
        ref.junk = 0;
        ref.order = i;
        if (auto r = output.add(std::move(ref)); !r) return r;
    }

    return Result::ok();
}

// ============================================================================
// Image conversion helpers (inlined, no allocation in hot path)
// ============================================================================

namespace img {

// Standard Mac palettes
constexpr uint32_t clut8[256] = {
    0xFFFFFFFF, 0xFFFFFFCC, 0xFFFFFF99, 0xFFFFFF66, 0xFFFFFF33, 0xFFFFFF00, 0xFFFFCCFF, 0xFFFFCCCC,
    0xFFFFCC99, 0xFFFFCC66, 0xFFFFCC33, 0xFFFFCC00, 0xFFFF99FF, 0xFFFF99CC, 0xFFFF9999, 0xFFFF9966,
    0xFFFF9933, 0xFFFF9900, 0xFFFF66FF, 0xFFFF66CC, 0xFFFF6699, 0xFFFF6666, 0xFFFF6633, 0xFFFF6600,
    0xFFFF33FF, 0xFFFF33CC, 0xFFFF3399, 0xFFFF3366, 0xFFFF3333, 0xFFFF3300, 0xFFFF00FF, 0xFFFF00CC,
    0xFFFF0099, 0xFFFF0066, 0xFFFF0033, 0xFFFF0000, 0xFFCCFFFF, 0xFFCCFFCC, 0xFFCCFF99, 0xFFCCFF66,
    0xFFCCFF33, 0xFFCCFF00, 0xFFCCCCFF, 0xFFCCCCCC, 0xFFCCCC99, 0xFFCCCC66, 0xFFCCCC33, 0xFFCCCC00,
    0xFFCC99FF, 0xFFCC99CC, 0xFFCC9999, 0xFFCC9966, 0xFFCC9933, 0xFFCC9900, 0xFFCC66FF, 0xFFCC66CC,
    0xFFCC6699, 0xFFCC6666, 0xFFCC6633, 0xFFCC6600, 0xFFCC33FF, 0xFFCC33CC, 0xFFCC3399, 0xFFCC3366,
    0xFFCC3333, 0xFFCC3300, 0xFFCC00FF, 0xFFCC00CC, 0xFFCC0099, 0xFFCC0066, 0xFFCC0033, 0xFFCC0000,
    0xFF99FFFF, 0xFF99FFCC, 0xFF99FF99, 0xFF99FF66, 0xFF99FF33, 0xFF99FF00, 0xFF99CCFF, 0xFF99CCCC,
    0xFF99CC99, 0xFF99CC66, 0xFF99CC33, 0xFF99CC00, 0xFF9999FF, 0xFF9999CC, 0xFF999999, 0xFF999966,
    0xFF999933, 0xFF999900, 0xFF9966FF, 0xFF9966CC, 0xFF996699, 0xFF996666, 0xFF996633, 0xFF996600,
    0xFF9933FF, 0xFF9933CC, 0xFF993399, 0xFF993366, 0xFF993333, 0xFF993300, 0xFF9900FF, 0xFF9900CC,
    0xFF990099, 0xFF990066, 0xFF990033, 0xFF990000, 0xFF66FFFF, 0xFF66FFCC, 0xFF66FF99, 0xFF66FF66,
    0xFF66FF33, 0xFF66FF00, 0xFF66CCFF, 0xFF66CCCC, 0xFF66CC99, 0xFF66CC66, 0xFF66CC33, 0xFF66CC00,
    0xFF6699FF, 0xFF6699CC, 0xFF669999, 0xFF669966, 0xFF669933, 0xFF669900, 0xFF6666FF, 0xFF6666CC,
    0xFF666699, 0xFF666666, 0xFF666633, 0xFF666600, 0xFF6633FF, 0xFF6633CC, 0xFF663399, 0xFF663366,
    0xFF663333, 0xFF663300, 0xFF6600FF, 0xFF6600CC, 0xFF660099, 0xFF660066, 0xFF660033, 0xFF660000,
    0xFF33FFFF, 0xFF33FFCC, 0xFF33FF99, 0xFF33FF66, 0xFF33FF33, 0xFF33FF00, 0xFF33CCFF, 0xFF33CCCC,
    0xFF33CC99, 0xFF33CC66, 0xFF33CC33, 0xFF33CC00, 0xFF3399FF, 0xFF3399CC, 0xFF339999, 0xFF339966,
    0xFF339933, 0xFF339900, 0xFF3366FF, 0xFF3366CC, 0xFF336699, 0xFF336666, 0xFF336633, 0xFF336600,
    0xFF3333FF, 0xFF3333CC, 0xFF333399, 0xFF333366, 0xFF333333, 0xFF333300, 0xFF3300FF, 0xFF3300CC,
    0xFF330099, 0xFF330066, 0xFF330033, 0xFF330000, 0xFF00FFFF, 0xFF00FFCC, 0xFF00FF99, 0xFF00FF66,
    0xFF00FF33, 0xFF00FF00, 0xFF00CCFF, 0xFF00CCCC, 0xFF00CC99, 0xFF00CC66, 0xFF00CC33, 0xFF00CC00,
    0xFF0099FF, 0xFF0099CC, 0xFF009999, 0xFF009966, 0xFF009933, 0xFF009900, 0xFF0066FF, 0xFF0066CC,
    0xFF006699, 0xFF006666, 0xFF006633, 0xFF006600, 0xFF0033FF, 0xFF0033CC, 0xFF003399, 0xFF003366,
    0xFF003333, 0xFF003300, 0xFF0000FF, 0xFF0000CC, 0xFF000099, 0xFF000066, 0xFF000033, 0xFFEE0000,
    0xFFDD0000, 0xFFBB0000, 0xFFAA0000, 0xFF880000, 0xFF770000, 0xFF550000, 0xFF440000, 0xFF220000,
    0xFF110000, 0xFF00EE00, 0xFF00DD00, 0xFF00BB00, 0xFF00AA00, 0xFF008800, 0xFF007700, 0xFF005500,
    0xFF004400, 0xFF002200, 0xFF001100, 0xFF0000EE, 0xFF0000DD, 0xFF0000BB, 0xFF0000AA, 0xFF000088,
    0xFF000077, 0xFF000055, 0xFF000044, 0xFF000022, 0xFF000011, 0xFFEEEEEE, 0xFFDDDDDD, 0xFFBBBBBB,
    0xFFAAAAAA, 0xFF888888, 0xFF777777, 0xFF555555, 0xFF444444, 0xFF222222, 0xFF111111, 0xFF000000,
};

constexpr uint32_t clut4[16] = {
    0xFFFFFFFF, 0xFFFCF305, 0xFFFF6402, 0xFFDD0806, 0xFFF20884, 0xFF4600A5, 0xFF0000D4, 0xFF02ABEA,
    0xFF1FB714, 0xFF006411, 0xFF562C05, 0xFF90713A, 0xFFC0C0C0, 0xFF808080, 0xFF404040, 0xFF000000,
};

constexpr auto color8(uint8_t idx) -> uint32_t { return clut8[idx & 0xFF]; }
constexpr auto color4(uint8_t idx) -> uint32_t { return clut4[idx & 0x0F]; }

// Write BGRA pixel
constexpr void put_bgra(MutableBytes& out, uint32_t c) {
    out.data[0] = static_cast<uint8_t>(c);
    out.data[1] = static_cast<uint8_t>(c >> 8);
    out.data[2] = static_cast<uint8_t>(c >> 16);
    out.data[3] = static_cast<uint8_t>(c >> 24);
    out.data += 4;
    out.size -= 4;
}

// 1-bit icon decode
inline void decode_1bit(const Bytes& src, const Bytes& mask, int w, int h, MutableBytes& out) {
    for (int y = 0; y < h; ++y) {
        uint32_t sd = w == 32 ? read_u32be(src.data + y * 4) :
                      read_u16be(src.data + y * 2);
        uint32_t mk = w == 32 ? read_u32be(mask.data + y * 4) :
                      read_u16be(mask.data + y * 2);
        for (int x = 0; x < w; ++x) {
            bool black = (sd >> (w - 1 - x)) & 1;
            bool opaque = (mk >> (w - 1 - x)) & 1;
            uint32_t c = black ? 0xFF000000u : 0xFFFFFFFFu;
            if (!opaque) c &= 0x00FFFFFFu;
            put_bgra(out, c);
        }
    }
}

// 4-bit icon decode
inline void decode_4bit(const Bytes& src, const Bytes& mask, int w, int h, MutableBytes& out) {
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; x += 2) {
            uint8_t pixel = src.data[y * (w >> 1) + (x >> 1)];
            uint8_t m = mask.data[y * ((w + 15) >> 4) + (x >> 4)];
            bool opaque = (m >> (7 - (x & 7))) & 1;
            uint32_t c = color4(pixel >> 4);
            if (!opaque) c &= 0x00FFFFFFu;
            put_bgra(out, c);
            c = color4(pixel & 0x0F);
            if (!opaque) c &= 0x00FFFFFFu;
            put_bgra(out, c);
        }
    }
}

// 8-bit icon decode
inline void decode_8bit(const Bytes& src, const Bytes& mask, int w, int h, MutableBytes& out) {
    (void)w;
    (void)h;
    for (size_t i = 0; i < src.size; ++i) {
        uint8_t m = (i < mask.size) ? mask.data[i] : 0xFF;
        bool opaque = (m & 0x80) != 0;
        uint32_t c = color8(src.data[i]);
        if (!opaque) c &= 0x00FFFFFFu;
        put_bgra(out, c);
    }
}

// Decode plain ICON resource (128 bytes, 32x32 1-bit, no mask).
// out must be 32*32*4 = 4096 bytes.
inline auto decode_icon_bw(Bytes data, MutableBytes out) -> Result {
    constexpr int w = 32, h = 32;
    constexpr size_t expected = 128;
    constexpr size_t bgra_size = static_cast<size_t>(w * h * 4);
    if (data.size < expected) return Error::unexpected_end();
    if (out.size < bgra_size) return Error::bounds();

    uint8_t all_ones[128];
    for (auto& b : all_ones) b = 0xFF;
    Bytes mask{all_ones, expected};
    decode_1bit(data, mask, w, h, out);
    return Result::ok();
}

// Decode ICN# resource (128 bitmap bytes + 128 mask bytes, 32x32 1-bit).
// out must be 32*32*4 = 4096 bytes.
inline auto decode_icn_bw(Bytes data, MutableBytes out) -> Result {
    constexpr int w = 32, h = 32;
    constexpr size_t expected = 256;
    constexpr size_t bgra_size = static_cast<size_t>(w * h * 4);
    if (data.size < expected) return Error::unexpected_end();
    if (out.size < bgra_size) return Error::bounds();

    Bytes bitmap{data.data, 128};
    Bytes mask{data.data + 128, 128};
    decode_1bit(bitmap, mask, w, h, out);
    return Result::ok();
}

// Decode CURS resource (68 bytes: 32 bitmap + 32 mask + 2 hotspot_y + 2 hotspot_x).
// out must be 16*16*4 = 1024 bytes.
inline auto decode_curs(Bytes data, MutableBytes out,
                        int16_t& hot_x, int16_t& hot_y) -> Result {
    constexpr int w = 16, h = 16;
    constexpr size_t expected = 68;
    constexpr size_t bgra_size = static_cast<size_t>(w * h * 4);
    if (data.size < expected) return Error::unexpected_end();
    if (out.size < bgra_size) return Error::bounds();

    Bytes bitmap{data.data, 32};
    Bytes mask{data.data + 32, 32};
    decode_1bit(bitmap, mask, w, h, out);
    hot_y = read_i16be(data.data + 64);
    hot_x = read_i16be(data.data + 66);
    return Result::ok();
}

// Decode single 8x8 1-bit pattern (8 bytes) to BGRA (8*8*4 = 256 bytes).
inline auto decode_pat(Bytes data, MutableBytes out) -> Result {
    constexpr int w = 8, h = 8;
    constexpr size_t bgra_size = static_cast<size_t>(w * h * 4);
    if (data.size < 8) return Error::unexpected_end();
    if (out.size < bgra_size) return Error::bounds();

    for (int y = 0; y < h; ++y) {
        uint8_t row = data.data[y];
        for (int x = 0; x < w; ++x) {
            bool black = (row >> (7 - x)) & 1;
            uint32_t c = black ? 0xFF000000u : 0xFFFFFFFFu;
            put_bgra(out, c);
        }
    }
    return Result::ok();
}

} // namespace img

// ============================================================================
// AddColor resource parser (HCcd / HCbg)
// ============================================================================

namespace ac {

enum ObjType : uint8_t {
    ObjButton    = 0x01,
    ObjField     = 0x02,
    ObjRect      = 0x03,
    ObjPictRes   = 0x04,
    ObjPictFile  = 0x05,
};

struct RGBColor {
    uint16_t red;
    uint16_t green;
    uint16_t blue;
};

struct QDRect {
    int16_t top;
    int16_t left;
    int16_t bottom;
    int16_t right;
};

struct Object {
    ObjType  type;
    bool     hidden;
    int16_t  part_id;
    int16_t  bevel;
    RGBColor color;
    QDRect   rect;
    bool     transparent;
    Bytes    name;
};

template<size_t InlineCapacity = 32>
class ObjectList {
public:
    auto add(const Object& obj) -> Result {
        if (count_ < InlineCapacity) {
            objects_[count_++] = obj;
            return Result::ok();
        }
        return Error::invalid_data("too many AddColor objects");
    }
    auto count() const -> size_t { return count_; }
    auto operator[](size_t i) const -> const Object& { return objects_[i]; }
    auto begin() const -> const Object* { return objects_; }
    auto end() const -> const Object* { return objects_ + count_; }

private:
    Object objects_[InlineCapacity];
    size_t count_ = 0;
};

template<typename Output>
auto parse(Bytes data, Output& output) -> Result {
    size_t i = 0;
    while (i < data.size) {
        uint8_t header = data[i];
        auto otype = static_cast<ObjType>(header & 0x7F);
        bool hidden = (header & 0x80) != 0;

        Object obj{};
        obj.type = otype;
        obj.hidden = hidden;
        obj.transparent = false;

        switch (otype) {
        case ObjButton:
        case ObjField: {
            if (i + 11 > data.size) return Error::unexpected_end();
            obj.part_id = read_i16be(data.data + i + 1);
            obj.bevel   = read_i16be(data.data + i + 3);
            obj.color.red   = read_u16be(data.data + i + 5);
            obj.color.green = read_u16be(data.data + i + 7);
            obj.color.blue  = read_u16be(data.data + i + 9);
            i += 11;
            break;
        }
        case ObjRect: {
            if (i + 17 > data.size) return Error::unexpected_end();
            obj.rect.top    = read_i16be(data.data + i + 1);
            obj.rect.left   = read_i16be(data.data + i + 3);
            obj.rect.bottom = read_i16be(data.data + i + 5);
            obj.rect.right  = read_i16be(data.data + i + 7);
            obj.bevel       = read_i16be(data.data + i + 9);
            obj.color.red   = read_u16be(data.data + i + 11);
            obj.color.green = read_u16be(data.data + i + 13);
            obj.color.blue  = read_u16be(data.data + i + 15);
            i += 17;
            break;
        }
        case ObjPictRes:
        case ObjPictFile: {
            if (i + 11 > data.size) return Error::unexpected_end();
            obj.rect.top    = read_i16be(data.data + i + 1);
            obj.rect.left   = read_i16be(data.data + i + 3);
            obj.rect.bottom = read_i16be(data.data + i + 5);
            obj.rect.right  = read_i16be(data.data + i + 7);
            obj.transparent = data.data[i + 9] != 0;
            uint8_t name_len = data.data[i + 10];
            if (i + 11 + name_len > data.size) return Error::unexpected_end();
            obj.name = data.slice(i + 11, name_len);
            i += 11 + name_len;
            break;
        }
        default:
            return Error::invalid_format("unknown AddColor object type");
        }

        if (auto r = output.add(obj); !r) return r;
    }
    return Result::ok();
}

} // namespace ac

// ============================================================================
// PLTE (Palette) resource parser
// ============================================================================

namespace plte {

struct PaletteButton {
    int16_t top;
    int16_t left;
    int16_t bottom;
    int16_t right;
    Bytes   message;
};

template<size_t InlineCapacity = 64>
struct Palette {
    uint16_t wdef;
    bool     show_name;
    int16_t  selection;
    bool     frame;
    uint16_t pict_ref;
    int16_t  top;
    int16_t  left;
    PaletteButton buttons[InlineCapacity];
    size_t   button_count = 0;
};

template<size_t Cap = 64>
auto parse(Bytes data, Palette<Cap>& pal) -> Result {
    if (data.size < 24) return Error::unexpected_end();

    uint16_t raw2 = read_u16be(data.data + 2);
    pal.wdef       = raw2 / 16;
    pal.show_name  = (raw2 % 16 & 1) != 0;
    pal.selection  = read_i16be(data.data + 4);
    pal.frame      = read_u16be(data.data + 6) != 0;
    pal.pict_ref   = read_u16be(data.data + 8);
    pal.top        = read_i16be(data.data + 10);
    pal.left       = read_i16be(data.data + 12);

    uint16_t count = read_u16be(data.data + 22);
    size_t i = 24;

    for (uint16_t b = 0; b < count; ++b) {
        if (i + 11 > data.size) return Error::unexpected_end();
        if (pal.button_count >= Cap) return Error::invalid_data("too many palette buttons");

        auto& btn = pal.buttons[pal.button_count++];
        btn.top    = read_i16be(data.data + i);
        btn.left   = read_i16be(data.data + i + 2);
        btn.bottom = read_i16be(data.data + i + 4);
        btn.right  = read_i16be(data.data + i + 6);

        uint8_t msg_len = data.data[i + 10];
        if (i + 11 + msg_len > data.size) return Error::unexpected_end();
        btn.message = data.slice(i + 11, msg_len);

        size_t entry_size = 11 + msg_len + 1 - (msg_len % 2);
        i += entry_size;
    }
    return Result::ok();
}

} // namespace plte

// ============================================================================
// Classic Mac text resource parsers
// ============================================================================

namespace text {

struct StringRef {
    Bytes bytes;
};

template<size_t InlineCapacity = 64>
class StringList {
public:
    auto add(StringRef value) -> Result {
        if (count_ < InlineCapacity) {
            strings_[count_++] = value;
            return Result::ok();
        }
        return Error::invalid_data("too many strings");
    }
    auto count() const -> size_t { return count_; }
    auto operator[](size_t i) const -> const StringRef& { return strings_[i]; }
    auto begin() const -> const StringRef* { return strings_; }
    auto end() const -> const StringRef* { return strings_ + count_; }

private:
    StringRef strings_[InlineCapacity];
    size_t count_ = 0;
};

inline auto parse_str(Bytes data, StringRef& out) -> Result {
    if (data.size < 1) return Error::unexpected_end();
    uint8_t len = data.data[0];
    if (1u + static_cast<size_t>(len) > data.size) return Error::unexpected_end();
    out.bytes = data.slice(1, len);
    return Result::ok();
}

template<size_t Cap = 64>
auto parse_str_list(Bytes data, StringList<Cap>& out) -> Result {
    if (data.size < 2) return Error::unexpected_end();
    uint16_t count = read_u16be(data.data);
    size_t offset = 2;
    for (uint16_t i = 0; i < count; ++i) {
        if (offset >= data.size) return Error::unexpected_end();
        uint8_t len = data.data[offset];
        if (offset + 1u + static_cast<size_t>(len) > data.size) return Error::unexpected_end();
        if (auto r = out.add(StringRef{data.slice(offset + 1, len)}); !r) return r;
        offset += 1u + static_cast<size_t>(len);
    }
    return Result::ok();
}

inline auto parse_text(Bytes data, StringRef& out) -> Result {
    out.bytes = data;
    return Result::ok();
}

} // namespace text

// ============================================================================
// vers metadata resource parser
// ============================================================================

namespace vers {

struct Version {
    uint8_t major_revision;
    uint8_t minor_and_bug_revision;
    uint8_t development_stage;
    uint8_t prerelease_revision;
    uint16_t region_code;
    Bytes short_version;
    Bytes long_version;
};

inline auto parse(Bytes data, Version& version) -> Result {
    if (data.size < 8) return Error::unexpected_end();
    version.major_revision = data.data[0];
    version.minor_and_bug_revision = data.data[1];
    version.development_stage = data.data[2];
    version.prerelease_revision = data.data[3];
    version.region_code = read_u16be(data.data + 4);

    size_t offset = 6;
    uint8_t short_len = data.data[offset];
    if (offset + 1u + static_cast<size_t>(short_len) > data.size) return Error::unexpected_end();
    version.short_version = data.slice(offset + 1, short_len);
    offset += 1u + static_cast<size_t>(short_len);

    if (offset >= data.size) return Error::unexpected_end();
    uint8_t long_len = data.data[offset];
    if (offset + 1u + static_cast<size_t>(long_len) > data.size) return Error::unexpected_end();
    version.long_version = data.slice(offset + 1, long_len);
    return Result::ok();
}

} // namespace vers

// ============================================================================
// Color table resources (clut / CTBL)
// ============================================================================

namespace color_table {

struct Entry {
    uint16_t value;
    uint16_t red;
    uint16_t green;
    uint16_t blue;
};

template<size_t InlineCapacity = 256>
class Table {
public:
    uint32_t seed = 0;
    uint16_t flags = 0;

    auto add(Entry entry) -> Result {
        if (count_ < InlineCapacity) {
            entries_[count_++] = entry;
            return Result::ok();
        }
        return Error::invalid_data("too many color table entries");
    }
    auto count() const -> size_t { return count_; }
    auto operator[](size_t i) const -> const Entry& { return entries_[i]; }
    auto begin() const -> const Entry* { return entries_; }
    auto end() const -> const Entry* { return entries_ + count_; }

private:
    Entry entries_[InlineCapacity];
    size_t count_ = 0;
};

template<size_t Cap = 256>
auto parse(Bytes data, Table<Cap>& table) -> Result {
    if (data.size < 8) return Error::unexpected_end();
    table.seed = read_u32be(data.data);
    table.flags = read_u16be(data.data + 4);
    uint16_t last_index = read_u16be(data.data + 6);
    size_t entry_count = static_cast<size_t>(last_index) + 1u;
    if (entry_count > Cap) return Error::invalid_data("too many color table entries");
    if (!range_in_bounds(8, entry_count * 8u, data.size)) return Error::unexpected_end();

    size_t offset = 8;
    for (size_t i = 0; i < entry_count; ++i) {
        Entry entry{};
        entry.value = read_u16be(data.data + offset);
        entry.red = read_u16be(data.data + offset + 2);
        entry.green = read_u16be(data.data + offset + 4);
        entry.blue = read_u16be(data.data + offset + 6);
        if (auto r = table.add(entry); !r) return r;
        offset += 8;
    }
    return Result::ok();
}

} // namespace color_table

// ============================================================================
// SIZE metadata resource parser
// ============================================================================

namespace size_resource {

struct Size {
    uint16_t flags;
    bool save_screen;
    bool accept_suspend_events;
    bool disable_option;
    bool can_background;
    bool activate_on_fg_switch;
    bool only_background;
    bool get_front_clicks;
    bool accept_died_events;
    bool clean_addressing;
    bool high_level_event_aware;
    bool local_and_remote_high_level_events;
    bool stationery_aware;
    bool use_text_edit_services;
    uint32_t preferred_size;
    uint32_t minimum_size;
};

inline auto parse(Bytes data, Size& size) -> Result {
    if (data.size < 10) return Error::unexpected_end();
    size.flags = read_u16be(data.data);
    size.save_screen = (size.flags & 0x8000u) != 0;
    size.accept_suspend_events = (size.flags & 0x4000u) != 0;
    size.disable_option = (size.flags & 0x2000u) != 0;
    size.can_background = (size.flags & 0x1000u) != 0;
    size.activate_on_fg_switch = (size.flags & 0x0800u) != 0;
    size.only_background = (size.flags & 0x0400u) != 0;
    size.get_front_clicks = (size.flags & 0x0200u) != 0;
    size.accept_died_events = (size.flags & 0x0100u) != 0;
    size.clean_addressing = (size.flags & 0x0080u) != 0;
    size.high_level_event_aware = (size.flags & 0x0040u) != 0;
    size.local_and_remote_high_level_events = (size.flags & 0x0020u) != 0;
    size.stationery_aware = (size.flags & 0x0010u) != 0;
    size.use_text_edit_services = (size.flags & 0x0008u) != 0;
    size.preferred_size = read_u32be(data.data + 2);
    size.minimum_size = read_u32be(data.data + 6);
    return Result::ok();
}

} // namespace size_resource

// ============================================================================
// PAT# (Pattern List) helpers
// ============================================================================

namespace patlist {

inline auto count(Bytes data) -> size_t {
    if (data.size < 2) return 0;
    return read_u16be(data.data);
}

inline auto pattern_at(Bytes data, size_t index) -> Bytes {
    if (data.size < 2 || index >= read_u16be(data.data)) return {};
    size_t off = 2 + index * 8;
    if (off + 8 > data.size) return {};
    return data.slice(off, 8);
}

} // namespace patlist

// ============================================================================
// I/O helpers - client can override
// ============================================================================

class MemoryReader : public IReader {
public:
    MemoryReader(const uint8_t* data, size_t size) : data_(data), size_(size), pos_(0) {}

    auto read(uint8_t* dst, size_t len) -> size_t override {
        size_t remaining = size_ - pos_;
        size_t to_read = std::min(len, remaining);
        std::memcpy(dst, data_ + pos_, to_read);
        pos_ += to_read;
        return to_read;
    }
    auto skip(size_t n) -> size_t override {
        size_t remaining = size_ - pos_;
        size_t to_skip = std::min(n, remaining);
        pos_ += to_skip;
        return to_skip;
    }
    auto position() const -> size_t override { return pos_; }
    auto seek(size_t p) -> bool override {
        pos_ = p < size_ ? p : size_;
        return true;
    }
    auto size() const -> size_t override { return size_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_;
};

class MemoryWriter : public IWriter {
public:
    MemoryWriter(uint8_t* data, size_t capacity) : data_(data), capacity_(capacity), pos_(0) {}

    auto write(const uint8_t* src, size_t len) -> size_t override {
        size_t remaining = capacity_ - pos_;
        size_t to_write = std::min(len, remaining);
        std::memcpy(data_ + pos_, src, to_write);
        pos_ += to_write;
        return to_write;
    }
    auto position() const -> size_t override { return pos_; }

private:
    uint8_t* data_;
    size_t capacity_;
    size_t pos_;
};

} // namespace rsrcd
