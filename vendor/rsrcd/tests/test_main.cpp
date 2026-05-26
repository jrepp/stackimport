#include "rsrcd.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

static int g_failures = 0;

#define CHECK(cond, msg)                                                \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL line %d: %s\n", __LINE__, msg);          \
            g_failures++;                                               \
        }                                                               \
    } while (0)

#define CHECK_RESULT(r, msg)                                            \
    do {                                                                \
        if (!static_cast<bool>(r)) {                                    \
            std::printf("FAIL line %d: %s (err=%d)\n",                  \
                        __LINE__, msg, (r).code);                       \
            g_failures++;                                               \
        }                                                               \
    } while (0)

// ============================================================================
// Existing: big-endian helpers
// ============================================================================

static void test_big_endian() {
    const uint8_t u16[] = {0x12, 0x34};
    CHECK(rsrcd::read_u16be(u16) == 0x1234, "read_u16be");

    const uint8_t i16_neg[] = {0xFF, 0xFE};
    CHECK(rsrcd::read_i16be(i16_neg) == -2, "read_i16be negative");

    const uint8_t u32[] = {0x01, 0x02, 0x03, 0x04};
    CHECK(rsrcd::read_u32be(u32) == 0x01020304, "read_u32be");

    const uint8_t i32_neg[] = {0xFF, 0xFF, 0xFF, 0xFE};
    CHECK(rsrcd::read_i32be(i32_neg) == -2, "read_i32be negative");
}

// ============================================================================
// FourCC
// ============================================================================

static void test_fourcc() {
    rsrcd::FourCC a("ICON");
    CHECK(a.eq("ICON"), "FourCC eq string");
    CHECK(!a.eq("CURS"), "FourCC neq string");

    rsrcd::FourCC b("ICON");
    CHECK(a.eq(b), "FourCC eq FourCC");

    using rsrcd::operator""_4cc;
    auto c = "CURS"_4cc;
    CHECK(c.eq("CURS"), "FourCC literal");

    CHECK(a.to_uint32() == 0x49434F4E, "FourCC to_uint32");

    auto d = rsrcd::FourCC::from_uint32(0x49434F4E);
    CHECK(d.eq("ICON"), "FourCC from_uint32");
}

// ============================================================================
// Bytes slicing
// ============================================================================

static void test_bytes() {
    uint8_t buf[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
    rsrcd::Bytes b{buf, 6};

    CHECK(!b.empty(), "not empty");
    CHECK(b[0] == 0x00, "index 0");
    CHECK(b[5] == 0x05, "index 5");

    auto s = b.slice(2, 3);
    CHECK(s.size == 3, "slice size");
    CHECK(s[0] == 0x02 && s[1] == 0x03 && s[2] == 0x04, "slice data");

    auto s2 = b.slice(10, 2);
    CHECK(s2.empty(), "slice out of bounds");

    auto tail = b.last(2);
    CHECK(tail.size == 2 && tail[0] == 0x04, "last");
}

// ============================================================================
// VecParserOutput storage
// ============================================================================

static void test_vec_parser_output_heap_indexing() {
    uint8_t types[4][4] = {
        {'T', '0', '0', '0'},
        {'T', '0', '0', '1'},
        {'T', '0', '0', '2'},
        {'T', '0', '0', '3'},
    };

    rsrcd::VecParserOutput<2> out;
    CHECK_RESULT(out.reserve(4), "reserve heap-backed output");

    for (int32_t i = 0; i < 4; ++i) {
        rsrcd::ResRef ref{};
        ref.type = rsrcd::Bytes{types[i], 4};
        ref.id = 100 + i;
        CHECK_RESULT(out.add(std::move(ref)), "add resource ref");
    }

    CHECK(out.count() == 4, "heap-backed output count");
    CHECK(out.at(0).id == 100, "inline item 0 preserved");
    CHECK(out.at(1).id == 101, "inline item 1 preserved");
    CHECK(out.at(2).id == 102, "heap item 0 preserved");
    CHECK(out.at(3).id == 103, "heap item 1 preserved");
    CHECK(out.at(2).type.data == types[2], "heap item type pointer preserved");
    CHECK(out.at(3).type.data == types[3], "second heap item type pointer preserved");
}

static void test_vec_parser_output_capacity_overflow() {
    uint8_t type[4] = {'T', 'E', 'S', 'T'};
    rsrcd::VecParserOutput<2> out;
    CHECK_RESULT(out.reserve(3), "reserve limited heap-backed output");

    for (int32_t i = 0; i < 3; ++i) {
        rsrcd::ResRef ref{};
        ref.type = rsrcd::Bytes{type, 4};
        ref.id = i;
        CHECK_RESULT(out.add(std::move(ref)), "add within capacity");
    }

    rsrcd::ResRef overflow{};
    overflow.type = rsrcd::Bytes{type, 4};
    overflow.id = 3;
    auto r = out.add(std::move(overflow));
    CHECK(!r, "add beyond capacity fails");
    CHECK(out.count() == 3, "failed add does not change count");
    CHECK(out.at(2).id == 2, "last valid heap item remains readable");
}

// ============================================================================
// Resource fork parser
// ============================================================================

static void write_basic_fork_header(uint8_t* fork, uint32_t data_off, uint32_t map_off, uint32_t data_len, uint32_t map_len) {
    rsrcd::write_u32be(fork + 0, data_off);
    rsrcd::write_u32be(fork + 4, map_off);
    rsrcd::write_u32be(fork + 8, data_len);
    rsrcd::write_u32be(fork + 12, map_len);
}

static uint8_t* write_single_type_map(uint8_t* map, uint16_t type_list_off, uint16_t name_list_off, const char* type, uint16_t resource_count) {
    rsrcd::write_u16be(map + 24, type_list_off);
    rsrcd::write_u16be(map + 26, name_list_off);

    uint8_t* type_list = map + type_list_off;
    rsrcd::write_u16be(type_list, 0);
    std::memcpy(type_list + 2, type, 4);
    rsrcd::write_u16be(type_list + 6, static_cast<uint16_t>(resource_count - 1));
    rsrcd::write_u16be(type_list + 8, 10);
    return type_list + 10;
}

static void test_parse_fork_empty() {
    uint8_t fork[64] = {};
    uint32_t data_off = 0;
    uint32_t map_off = 16;
    uint32_t data_len = 0;
    uint32_t map_len = 48;

    rsrcd::write_u32be(fork + 0, data_off);
    rsrcd::write_u32be(fork + 4, map_off);
    rsrcd::write_u32be(fork + 8, data_len);
    rsrcd::write_u32be(fork + 12, map_len);

    uint8_t* map = fork + map_off;
    rsrcd::write_u16be(map + 24, 28);
    rsrcd::write_u16be(map + 26, 28);
    rsrcd::write_u16be(map + 28, 0xFFFF);

    rsrcd::VecParserOutput<16> out;
    auto r = rsrcd::Parser{}.parse_fork({fork, 64}, out);
    CHECK_RESULT(r, "parse empty fork");
    CHECK(out.count() == 0, "empty fork has 0 resources");
}

static void test_parse_fork_too_small() {
    uint8_t small[16] = {};
    rsrcd::VecParserOutput<4> out;
    auto r = rsrcd::Parser{}.parse_fork({small, 16}, out);
    CHECK(!r, "fork too small should fail");
}

static void test_parse_fork_single_resource() {
    uint8_t fork[256] = {};

    uint32_t data_off = 16;
    uint32_t map_off = 64;
    uint32_t data_len = 48;
    uint32_t map_len = 128;

    rsrcd::write_u32be(fork + 0, data_off);
    rsrcd::write_u32be(fork + 4, map_off);
    rsrcd::write_u32be(fork + 8, data_len);
    rsrcd::write_u32be(fork + 12, map_len);

    uint8_t res_data[] = {'H', 'e', 'l', 'l', 'o'};
    uint32_t res_data_len = 5;
    rsrcd::write_u32be(fork + data_off, res_data_len);
    std::memcpy(fork + data_off + 4, res_data, res_data_len);

    uint8_t* map = fork + map_off;
    uint16_t type_list_off = 28;
    uint16_t name_list_off = 64;
    rsrcd::write_u16be(map + 24, type_list_off);
    rsrcd::write_u16be(map + 26, name_list_off);

    uint8_t* type_list = map + type_list_off;
    rsrcd::write_u16be(type_list, 0);
    std::memcpy(type_list + 2, "ICON", 4);
    rsrcd::write_u16be(type_list + 6, 0);
    uint16_t ref_list_off = 10;
    rsrcd::write_u16be(type_list + 8, ref_list_off);

    uint8_t* ref = type_list + ref_list_off;
    rsrcd::write_u16be(ref, 128);
    rsrcd::write_u16be(ref + 2, 0xFFFF);
    uint32_t packed = (0u << 24) | 0u;
    rsrcd::write_u32be(ref + 4, packed);
    rsrcd::write_u32be(ref + 8, 0);

    rsrcd::VecParserOutput<16> out;
    auto r = rsrcd::Parser{}.parse_fork({fork, 256}, out);
    CHECK_RESULT(r, "parse single-resource fork");
    CHECK(out.count() == 1, "single resource count");

    auto& res = out.at(0);
    CHECK(res.id == 128, "resource id");
    CHECK(res.type.size == 4, "resource type size");
    CHECK(res.type.data[0] == 'I' && res.type.data[1] == 'C'
          && res.type.data[2] == 'O' && res.type.data[3] == 'N',
          "resource type ICON");
    CHECK(res.data.size == 5, "resource data size");
    CHECK(std::memcmp(res.data.data, "Hello", 5) == 0, "resource data content");
    CHECK(!res.is_compressed(), "not compressed");
}

static void test_parse_fork_many_resources_cross_inline_capacity() {
    uint8_t fork[512] = {};
    uint32_t data_off = 16;
    uint32_t map_off = 128;
    uint32_t data_len = 96;
    uint32_t map_len = 256;
    write_basic_fork_header(fork, data_off, map_off, data_len, map_len);

    uint8_t* map = fork + map_off;
    uint8_t* ref = write_single_type_map(map, 28, 128, "CURS", 4);
    for (uint32_t i = 0; i < 4; ++i) {
        uint32_t d_off = i * 8;
        rsrcd::write_u32be(fork + data_off + d_off, 1);
        fork[data_off + d_off + 4] = static_cast<uint8_t>('A' + i);
        rsrcd::write_u16be(ref + 0, static_cast<uint16_t>(100 + i));
        rsrcd::write_u16be(ref + 2, 0xFFFF);
        rsrcd::write_u32be(ref + 4, d_off);
        rsrcd::write_u32be(ref + 8, 0);
        ref += 12;
    }

    rsrcd::VecParserOutput<2> out;
    auto r = rsrcd::Parser{}.parse_fork({fork, 512}, out);
    CHECK_RESULT(r, "parse resources crossing inline output capacity");
    CHECK(out.count() == 4, "all resources parsed across inline capacity");
    for (int32_t i = 0; i < 4; ++i) {
        const auto& res = out.at(static_cast<size_t>(i));
        CHECK(res.id == 100 + i, "heap-boundary resource id preserved");
        CHECK(res.type.data != nullptr, "heap-boundary resource type is non-null");
        CHECK(res.type.size == 4 && std::memcmp(res.type.data, "CURS", 4) == 0, "heap-boundary resource type preserved");
        CHECK(res.data.size == 1 && res.data.data[0] == static_cast<uint8_t>('A' + i), "heap-boundary resource data preserved");
    }
}

static void test_parse_fork_truncated_ref_list_stops_cleanly() {
    uint8_t fork[128] = {};
    uint32_t data_off = 16;
    uint32_t map_off = 64;
    uint32_t data_len = 16;
    uint32_t map_len = 52;
    write_basic_fork_header(fork, data_off, map_off, data_len, map_len);
    rsrcd::write_u32be(fork + data_off, 1);
    fork[data_off + 4] = 'Z';

    uint8_t* ref = write_single_type_map(fork + map_off, 28, 52, "ICON", 3);
    rsrcd::write_u16be(ref + 0, 128);
    rsrcd::write_u16be(ref + 2, 0xFFFF);
    rsrcd::write_u32be(ref + 4, 0);
    rsrcd::write_u32be(ref + 8, 0);

    rsrcd::VecParserOutput<2> out;
    auto r = rsrcd::Parser{}.parse_fork({fork, 128}, out);
    CHECK_RESULT(r, "parse truncated ref list");
    CHECK(out.count() == 1, "truncated ref list adds only complete refs");
    CHECK(out.at(0).type.data != nullptr, "complete ref keeps type pointer");
    CHECK(out.at(0).data.size == 1 && out.at(0).data.data[0] == 'Z', "complete ref keeps data");
}

static void test_parse_fork_declared_count_clamps_to_complete_refs() {
    uint8_t fork[128] = {};
    uint32_t data_off = 16;
    uint32_t map_off = 64;
    uint32_t data_len = 16;
    uint32_t map_len = 52;
    write_basic_fork_header(fork, data_off, map_off, data_len, map_len);
    rsrcd::write_u32be(fork + data_off, 1);
    fork[data_off + 4] = 'C';

    uint8_t* map = fork + map_off;
    rsrcd::write_u16be(map + 24, 28);
    rsrcd::write_u16be(map + 26, 52);

    uint8_t* type_list = map + 28;
    rsrcd::write_u16be(type_list, 0);
    std::memcpy(type_list + 2, "CLMP", 4);
    rsrcd::write_u16be(type_list + 6, 0xFFFF);
    rsrcd::write_u16be(type_list + 8, 10);

    uint8_t* ref = type_list + 10;
    rsrcd::write_u16be(ref + 0, 203);
    rsrcd::write_u16be(ref + 2, 0xFFFF);
    rsrcd::write_u32be(ref + 4, 0);
    rsrcd::write_u32be(ref + 8, 0);

    rsrcd::VecParserOutput<2> out;
    auto r = rsrcd::Parser{}.parse_fork({fork, sizeof(fork)}, out);
    CHECK_RESULT(r, "parse count clamped fork");
    CHECK(out.count() == 1, "declared max count clamps to complete refs");
    CHECK(out.at(0).id == 203, "clamped ref id preserved");
    CHECK(out.at(0).data.size == 1 && out.at(0).data.data[0] == 'C', "clamped ref data preserved");
}

static void test_parse_fork_invalid_data_offset_leaves_empty_data() {
    uint8_t fork[160] = {};
    uint32_t data_off = 16;
    uint32_t map_off = 64;
    uint32_t data_len = 16;
    uint32_t map_len = 96;
    write_basic_fork_header(fork, data_off, map_off, data_len, map_len);

    uint8_t* ref = write_single_type_map(fork + map_off, 28, 64, "snd ", 1);
    rsrcd::write_u16be(ref + 0, 200);
    rsrcd::write_u16be(ref + 2, 0xFFFF);
    rsrcd::write_u32be(ref + 4, 32);
    rsrcd::write_u32be(ref + 8, 0);

    rsrcd::VecParserOutput<2> out;
    auto r = rsrcd::Parser{}.parse_fork({fork, 160}, out);
    CHECK_RESULT(r, "parse invalid data offset");
    CHECK(out.count() == 1, "invalid data offset still reports ref");
    CHECK(out.at(0).type.data != nullptr, "invalid data offset keeps type pointer");
    CHECK(out.at(0).data.empty(), "invalid data offset leaves empty data view");
    CHECK(out.at(0).data.data == nullptr, "invalid data offset leaves null data pointer");
}

static void test_parse_fork_invalid_name_offset_leaves_empty_name() {
    uint8_t fork[192] = {};
    uint32_t data_off = 16;
    uint32_t map_off = 64;
    uint32_t data_len = 16;
    uint32_t map_len = 128;
    write_basic_fork_header(fork, data_off, map_off, data_len, map_len);
    rsrcd::write_u32be(fork + data_off, 1);
    fork[data_off + 4] = 'N';

    uint8_t* ref = write_single_type_map(fork + map_off, 28, 64, "NAME", 1);
    rsrcd::write_u16be(ref + 0, 201);
    rsrcd::write_u16be(ref + 2, 1000);
    rsrcd::write_u32be(ref + 4, 0);
    rsrcd::write_u32be(ref + 8, 0);

    rsrcd::VecParserOutput<2> out;
    auto r = rsrcd::Parser{}.parse_fork({fork, 192}, out);
    CHECK_RESULT(r, "parse invalid name offset");
    CHECK(out.count() == 1, "invalid name offset still reports ref");
    CHECK(out.at(0).name.empty(), "invalid name offset leaves empty name view");
    CHECK(out.at(0).name.data == nullptr, "invalid name offset leaves null name pointer");
    CHECK(out.at(0).data.size == 1 && out.at(0).data.data[0] == 'N', "invalid name offset keeps data view");
}

static void test_parse_fork_ref_exactly_at_map_boundary() {
    uint8_t fork[128] = {};
    uint32_t data_off = 16;
    uint32_t map_off = 64;
    uint32_t data_len = 16;
    uint32_t map_len = 50;
    write_basic_fork_header(fork, data_off, map_off, data_len, map_len);
    rsrcd::write_u32be(fork + data_off, 1);
    fork[data_off + 4] = 'B';

    uint8_t* ref = write_single_type_map(fork + map_off, 28, 50, "BNDY", 1);
    rsrcd::write_u16be(ref + 0, 202);
    rsrcd::write_u16be(ref + 2, 0xFFFF);
    rsrcd::write_u32be(ref + 4, 0);
    rsrcd::write_u32be(ref + 8, 0);

    rsrcd::VecParserOutput<2> out;
    auto r = rsrcd::Parser{}.parse_fork({fork, sizeof(fork)}, out);
    CHECK_RESULT(r, "parse boundary-ending ref list");
    CHECK(out.count() == 1, "ref ending at map boundary is parsed");
    CHECK(out.at(0).id == 202, "boundary ref id preserved");
    CHECK(out.at(0).data.size == 1 && out.at(0).data.data[0] == 'B', "boundary ref data preserved");
}

static void test_parse_fork_overflowing_data_range_fails() {
    uint8_t fork[128] = {};
    write_basic_fork_header(fork, 0xFFFFFFF0u, 32, 64, 64);

    uint8_t* map = fork + 32;
    rsrcd::write_u16be(map + 24, 28);
    rsrcd::write_u16be(map + 26, 28);
    rsrcd::write_u16be(map + 28, 0xFFFF);

    rsrcd::VecParserOutput<2> out;
    auto r = rsrcd::Parser{}.parse_fork({fork, sizeof(fork)}, out);
    CHECK(!r, "overflowing data range should fail");
    CHECK(out.count() == 0, "overflowing data range adds no refs");
}

// ============================================================================
// AppleDouble parser
// ============================================================================

static void test_parse_adf_too_small() {
    uint8_t buf[16] = {};
    rsrcd::VecParserOutput<4> out;
    auto r = rsrcd::Parser{}.parse_adf({buf, 16}, out);
    CHECK(!r, "ADF too small");
}

static void test_parse_adf_bad_magic() {
    uint8_t buf[32] = {};
    rsrcd::write_u32be(buf, 0xDEADBEEF);
    rsrcd::VecParserOutput<4> out;
    auto r = rsrcd::Parser{}.parse_adf({buf, 32}, out);
    CHECK(!r, "ADF bad magic");
}

static void test_parse_adf_single_entry() {
    uint8_t buf[64] = {};
    rsrcd::write_u32be(buf + 0, 0x00051607);
    rsrcd::write_u32be(buf + 4, 0x00020000);
    rsrcd::write_u16be(buf + 22, 1);

    rsrcd::write_u32be(buf + 26, 2);
    rsrcd::write_u32be(buf + 30, 38);
    rsrcd::write_u32be(buf + 34, 4);

    buf[38] = 'T';
    buf[39] = 'E';
    buf[40] = 'S';
    buf[41] = 'T';

    rsrcd::VecParserOutput<4> out;
    auto r = rsrcd::Parser{}.parse_adf({buf, 42}, out);
    CHECK_RESULT(r, "parse ADF single entry");
    CHECK(out.count() == 1, "ADF entry count");
    CHECK(out.at(0).data.size == 4, "ADF entry data size");
    CHECK(std::memcmp(out.at(0).data.data, "TEST", 4) == 0, "ADF entry data");
    CHECK(out.at(0).type.data == buf + 26, "ADF entry type points into source buffer");
    CHECK(out.at(0).type.size == 4, "ADF entry type size");
    CHECK(out.at(0).type.data[3] == 2, "ADF entry type bytes preserved");
}

static void test_parse_adf_invalid_entry_range_fails() {
    uint8_t buf[64] = {};
    rsrcd::write_u32be(buf + 0, 0x00051607);
    rsrcd::write_u32be(buf + 4, 0x00020000);
    rsrcd::write_u16be(buf + 22, 1);

    rsrcd::write_u32be(buf + 26, 2);
    rsrcd::write_u32be(buf + 30, 60);
    rsrcd::write_u32be(buf + 34, 8);

    rsrcd::VecParserOutput<4> out;
    auto r = rsrcd::Parser{}.parse_adf({buf, sizeof(buf)}, out);
    CHECK(!r, "ADF entry extending past buffer should fail");
    CHECK(out.count() == 0, "invalid ADF entry adds no refs");
}

// ============================================================================
// ForkView find
// ============================================================================

static void test_fork_view_find() {
    rsrcd::FourCC icon("ICON");
    rsrcd::FourCC curs("CURS");
    rsrcd::ResRef refs[3] = {};
    refs[0].type = icon.as_bytes();
    refs[0].id = 128;
    refs[1].type = curs.as_bytes();
    refs[1].id = 256;
    refs[2].type = icon.as_bytes();
    refs[2].id = 129;

    rsrcd::ForkView view{};
    view.resources = refs;
    view.count = 3;

    auto f1 = rsrcd::find(view, rsrcd::FourCC("ICON"), 128);
    CHECK(f1 != nullptr && f1->id == 128, "find ICON 128");

    auto f2 = rsrcd::find(view, rsrcd::FourCC("ICON"), 129);
    CHECK(f2 != nullptr && f2->id == 129, "find ICON 129");

    auto f3 = rsrcd::find(view, rsrcd::FourCC("CURS"), 256);
    CHECK(f3 != nullptr && f3->id == 256, "find CURS 256");

    auto f4 = rsrcd::find(view, rsrcd::FourCC("snd "), 1);
    CHECK(f4 == nullptr, "find nonexistent");

    auto f5 = rsrcd::find_first(view, rsrcd::FourCC("ICON"));
    CHECK(f5 != nullptr && f5->id == 128, "find_first ICON");

    auto f6 = rsrcd::find_first(view, rsrcd::FourCC("snd "));
    CHECK(f6 == nullptr, "find_first nonexistent");
}

// ============================================================================
// Image decode: ICON B&W
// ============================================================================

static void test_decode_icon_bw_all_white() {
    uint8_t icon[128] = {};
    uint8_t bgra[32 * 32 * 4];
    rsrcd::MutableBytes out{bgra, sizeof(bgra)};

    auto r = rsrcd::img::decode_icon_bw({icon, 128}, out);
    CHECK_RESULT(r, "decode all-white icon");

    for (int i = 0; i < 32 * 32; ++i) {
        uint8_t a = bgra[i * 4 + 3];
        CHECK(a == 0xFF, "all-white icon pixel alpha");
        if (a != 0xFF) break;
    }
}

static void test_decode_icon_bw_all_black() {
    uint8_t icon[128];
    std::memset(icon, 0xFF, 128);
    uint8_t bgra[32 * 32 * 4];
    rsrcd::MutableBytes out{bgra, sizeof(bgra)};

    auto r = rsrcd::img::decode_icon_bw({icon, 128}, out);
    CHECK_RESULT(r, "decode all-black icon");

    for (int i = 0; i < 32 * 32; ++i) {
        uint8_t b = bgra[i * 4 + 0];
        CHECK(b == 0x00, "all-black icon pixel B");
        uint8_t a = bgra[i * 4 + 3];
        CHECK(a == 0xFF, "all-black icon alpha");
        if (b != 0x00 || a != 0xFF) break;
    }
}

static void test_decode_icon_bw_too_small() {
    uint8_t icon[64] = {};
    uint8_t bgra[4096];
    rsrcd::MutableBytes out{bgra, sizeof(bgra)};
    auto r = rsrcd::img::decode_icon_bw({icon, 64}, out);
    CHECK(!r, "icon too small should fail");
}

static void test_decode_icon_bw_top_left_pixel() {
    uint8_t icon[128] = {};
    icon[0] = 0x80;
    uint8_t bgra[32 * 32 * 4];
    rsrcd::MutableBytes out{bgra, sizeof(bgra)};

    auto r = rsrcd::img::decode_icon_bw({icon, 128}, out);
    CHECK_RESULT(r, "decode icon single pixel");

    CHECK(bgra[0] == 0x00 && bgra[1] == 0x00 && bgra[2] == 0x00 && bgra[3] == 0xFF,
          "top-left pixel is black");
    CHECK(bgra[4] == 0xFF && bgra[5] == 0xFF && bgra[6] == 0xFF && bgra[7] == 0xFF,
          "next pixel is white");
}

static void test_decode_icn_bw_mask() {
    uint8_t icn[256] = {};
    icn[0] = 0x80;
    icn[128] = 0x80;
    uint8_t bgra[32 * 32 * 4];
    rsrcd::MutableBytes out{bgra, sizeof(bgra)};

    auto r = rsrcd::img::decode_icn_bw({icn, sizeof(icn)}, out);
    CHECK_RESULT(r, "decode ICN#");

    CHECK(bgra[0] == 0x00 && bgra[1] == 0x00 && bgra[2] == 0x00 && bgra[3] == 0xFF,
          "ICN# top-left black opaque");
    CHECK(bgra[7] == 0x00, "ICN# next pixel transparent");
}

// ============================================================================
// Image decode: CURS
// ============================================================================

static void test_decode_curs() {
    uint8_t curs[68] = {};
    curs[0] = 0x80;
    curs[32] = 0xFF;
    rsrcd::write_u16be(curs + 64, 5);
    rsrcd::write_u16be(curs + 66, 3);

    uint8_t bgra[16 * 16 * 4];
    rsrcd::MutableBytes out{bgra, sizeof(bgra)};
    int16_t hx = 0, hy = 0;

    auto r = rsrcd::img::decode_curs({curs, 68}, out, hx, hy);
    CHECK_RESULT(r, "decode cursor");
    CHECK(hx == 3, "cursor hot_x");
    CHECK(hy == 5, "cursor hot_y");

    CHECK(bgra[0] == 0x00 && bgra[3] == 0xFF,
          "cursor top-left black+opaque");
}

static void test_decode_curs_transparent() {
    uint8_t curs[68] = {};
    uint8_t bgra[16 * 16 * 4];
    rsrcd::MutableBytes out{bgra, sizeof(bgra)};
    int16_t hx = 0, hy = 0;

    auto r = rsrcd::img::decode_curs({curs, 68}, out, hx, hy);
    CHECK_RESULT(r, "decode transparent cursor");

    CHECK(bgra[3] == 0x00, "transparent cursor alpha is 0");
}

static void test_decode_curs_too_small() {
    uint8_t curs[32] = {};
    uint8_t bgra[1024];
    rsrcd::MutableBytes out{bgra, sizeof(bgra)};
    int16_t hx = 0, hy = 0;
    auto r = rsrcd::img::decode_curs({curs, 32}, out, hx, hy);
    CHECK(!r, "cursor too small should fail");
}

// ============================================================================
// Image decode: pattern
// ============================================================================

static void test_decode_pat_all_white() {
    uint8_t pat[8] = {};
    uint8_t bgra[8 * 8 * 4];
    rsrcd::MutableBytes out{bgra, sizeof(bgra)};

    auto r = rsrcd::img::decode_pat({pat, 8}, out);
    CHECK_RESULT(r, "decode all-white pattern");

    for (int i = 0; i < 8 * 8; ++i) {
        CHECK(bgra[i * 4] == 0xFF, "white pattern pixel");
        if (bgra[i * 4] != 0xFF) break;
    }
}

static void test_decode_pat_checkerboard() {
    uint8_t pat[8] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    uint8_t bgra[8 * 8 * 4];
    rsrcd::MutableBytes out{bgra, sizeof(bgra)};

    auto r = rsrcd::img::decode_pat({pat, 8}, out);
    CHECK_RESULT(r, "decode checkerboard pattern");

    CHECK(bgra[0] == 0x00, "checker pixel 0 black");
    CHECK(bgra[4] == 0xFF, "checker pixel 1 white");
}

static void test_decode_sicn() {
    uint8_t sicn[32] = {};
    sicn[0] = 0x80;
    uint8_t bgra[16 * 16 * 4];
    rsrcd::MutableBytes out{bgra, sizeof(bgra)};

    auto r = rsrcd::img::decode_sicn({sicn, sizeof(sicn)}, out);
    CHECK_RESULT(r, "decode small icon");

    CHECK(bgra[0] == 0x00, "SICN top-left pixel black");
    CHECK(bgra[3] == 0xFF, "SICN top-left pixel opaque");
    CHECK(bgra[4] == 0xFF, "SICN second pixel white");
    CHECK(bgra[7] == 0xFF, "SICN second pixel opaque");
}

static void test_decode_sicn_too_small() {
    uint8_t sicn[31] = {};
    uint8_t bgra[16 * 16 * 4];
    rsrcd::MutableBytes out{bgra, sizeof(bgra)};

    auto r = rsrcd::img::decode_sicn({sicn, sizeof(sicn)}, out);
    CHECK(!r, "small icon too small should fail");
}

static void test_decode_icon_4bit() {
    uint8_t data[16 * 16 / 2] = {};
    data[0] = 0x0F;
    uint8_t bgra[16 * 16 * 4];
    rsrcd::MutableBytes out{bgra, sizeof(bgra)};

    auto r = rsrcd::img::decode_icon_4bit({data, sizeof(data)}, 16, 16, out);
    CHECK_RESULT(r, "decode 4-bit icon");
    CHECK(bgra[0] == 0xFF, "4-bit first pixel blue channel");
    CHECK(bgra[1] == 0xFF, "4-bit first pixel green channel");
    CHECK(bgra[2] == 0xFF, "4-bit first pixel red channel");
    CHECK(bgra[3] == 0xFF, "4-bit first pixel opaque");
    CHECK(bgra[4] == 0x00, "4-bit second pixel black");
    CHECK(bgra[7] == 0xFF, "4-bit second pixel opaque");
}

static void test_decode_icon_8bit() {
    uint8_t data[16 * 16] = {};
    data[0] = 0xFF;
    uint8_t bgra[16 * 16 * 4];
    rsrcd::MutableBytes out{bgra, sizeof(bgra)};

    auto r = rsrcd::img::decode_icon_8bit({data, sizeof(data)}, 16, 16, out);
    CHECK_RESULT(r, "decode 8-bit icon");
    CHECK(bgra[0] == 0x00, "8-bit first pixel black");
    CHECK(bgra[3] == 0xFF, "8-bit first pixel opaque");
    CHECK(bgra[4] == 0xFF, "8-bit second pixel white");
    CHECK(bgra[7] == 0xFF, "8-bit second pixel opaque");
}

static void test_decode_icon_4bit_bad_size() {
    uint8_t data[127] = {};
    uint8_t bgra[16 * 16 * 4];
    rsrcd::MutableBytes out{bgra, sizeof(bgra)};

    auto r = rsrcd::img::decode_icon_4bit({data, sizeof(data)}, 16, 16, out);
    CHECK(!r, "4-bit icon bad size should fail");
}

static void test_decode_icon_8bit_bad_size() {
    uint8_t data[255] = {};
    uint8_t bgra[16 * 16 * 4];
    rsrcd::MutableBytes out{bgra, sizeof(bgra)};

    auto r = rsrcd::img::decode_icon_8bit({data, sizeof(data)}, 16, 16, out);
    CHECK(!r, "8-bit icon bad size should fail");
}

static void test_decode_icon_1bit_list_image() {
    uint8_t data[32] = {};
    data[0] = 0x80;
    uint8_t bgra[16 * 16 * 4];
    rsrcd::MutableBytes out{bgra, sizeof(bgra)};

    size_t count = 0;
    auto count_result = rsrcd::img::count_icon_1bit_images({data, sizeof(data)}, 16, 16, count);
    CHECK_RESULT(count_result, "count 1-bit icon list");
    CHECK(count == 1, "1-bit icon list count");

    auto r = rsrcd::img::decode_icon_1bit_list_image({data, sizeof(data)}, 16, 16, 0, out);
    CHECK_RESULT(r, "decode 1-bit icon list image");
    CHECK(bgra[0] == 0x00, "1-bit icon list first pixel black");
    CHECK(bgra[3] == 0xFF, "1-bit icon list first pixel opaque");
    CHECK(bgra[4] == 0xFF, "1-bit icon list second pixel white");
    CHECK(bgra[7] == 0xFF, "1-bit icon list second pixel opaque");
}

static void test_decode_icon_1bit_masked_pair() {
    uint8_t data[64] = {};
    data[0] = 0x80;
    data[32] = 0x80;
    uint8_t bgra[16 * 16 * 4];
    rsrcd::MutableBytes out{bgra, sizeof(bgra)};

    auto r = rsrcd::img::decode_icon_1bit_masked_pair({data, sizeof(data)}, 16, 16, out);
    CHECK_RESULT(r, "decode 1-bit icon bitmap/mask pair");
    CHECK(bgra[0] == 0x00, "masked pair first pixel black");
    CHECK(bgra[3] == 0xFF, "masked pair first pixel opaque");
    CHECK(bgra[4] == 0xFF, "masked pair second pixel white");
    CHECK(bgra[7] == 0x00, "masked pair second pixel transparent");
}

static void test_decode_icon_1bit_list_bad_size() {
    uint8_t data[31] = {};
    size_t count = 0;

    auto r = rsrcd::img::count_icon_1bit_images({data, sizeof(data)}, 16, 16, count);
    CHECK(!r, "1-bit icon list bad size should fail");
}

static void test_parse_cfrg_single_entry() {
    uint8_t data[79] = {};
    rsrcd::write_u16be(data + 10, 1);
    rsrcd::write_u16be(data + 30, 1);
    size_t o = 32;
    rsrcd::write_u32be(data + o, 0x70777063);
    data[o + 7] = 2;
    rsrcd::write_u32be(data + o + 8, 0x01020304);
    rsrcd::write_u32be(data + o + 12, 0x00010000);
    rsrcd::write_u32be(data + o + 16, 0x00002000);
    rsrcd::write_u16be(data + o + 20, 0x0007);
    data[o + 22] = 1;
    data[o + 23] = 2;
    rsrcd::write_u32be(data + o + 24, 0x434F4445);
    rsrcd::write_u32be(data + o + 28, 128);
    rsrcd::write_u32be(data + o + 32, 0);
    rsrcd::write_u16be(data + o + 36, 1);
    rsrcd::write_u16be(data + o + 40, 47);
    data[o + 42] = 4;
    data[o + 43] = 'M';
    data[o + 44] = 'a';
    data[o + 45] = 'i';
    data[o + 46] = 'n';

    rsrcd::cfrg::FragmentList<4> fragments;
    auto r = rsrcd::cfrg::parse({data, sizeof(data)}, fragments);
    CHECK_RESULT(r, "parse cfrg");
    CHECK(fragments.count() == 1, "cfrg entry count");
    CHECK(fragments[0].architecture == 0x70777063, "cfrg architecture");
    CHECK(fragments[0].where == 2, "cfrg where");
    CHECK(fragments[0].name.size == 4, "cfrg name size");
}

static void test_parse_cfrg_bad_version() {
    uint8_t data[32] = {};
    rsrcd::write_u16be(data + 10, 2);

    rsrcd::cfrg::FragmentList<4> fragments;
    auto r = rsrcd::cfrg::parse({data, sizeof(data)}, fragments);
    CHECK(!r, "cfrg bad version should fail");
}

static void test_parse_mbar() {
    uint8_t data[6] = {};
    rsrcd::write_u16be(data, 2);
    rsrcd::write_u16be(data + 2, 128);
    rsrcd::write_u16be(data + 4, 129);

    rsrcd::mbar::MenuIdList<4> menus;
    auto r = rsrcd::mbar::parse({data, sizeof(data)}, menus);
    CHECK_RESULT(r, "parse MBAR");
    CHECK(menus.count() == 2, "MBAR menu count");
    CHECK(menus[0] == 128, "MBAR first menu ID");
    CHECK(menus[1] == 129, "MBAR second menu ID");
}

static void test_parse_mbar_truncated() {
    uint8_t data[5] = {};
    rsrcd::write_u16be(data, 2);

    rsrcd::mbar::MenuIdList<4> menus;
    auto r = rsrcd::mbar::parse({data, sizeof(data)}, menus);
    CHECK(!r, "truncated MBAR should fail");
}

static void test_parse_alrt() {
    uint8_t data[14] = {};
    rsrcd::write_u16be(data, 1);
    rsrcd::write_u16be(data + 2, 2);
    rsrcd::write_u16be(data + 4, 101);
    rsrcd::write_u16be(data + 6, 202);
    rsrcd::write_u16be(data + 8, 300);
    data[10] = 0x12;
    data[11] = 0x34;
    rsrcd::write_u16be(data + 12, 0x0A00);

    rsrcd::finder::Alert alert{};
    auto r = rsrcd::finder::parse_alrt({data, sizeof(data)}, alert);
    CHECK_RESULT(r, "parse ALRT");
    CHECK(alert.bounds.bottom == 101, "ALRT bottom");
    CHECK(alert.item_list_id == 300, "ALRT item list ID");
    CHECK(alert.stage_4_flags == 0x12, "ALRT stage 4 flags");
    CHECK(alert.has_auto_position, "ALRT auto position present");
    CHECK(alert.auto_position == 0x0A00, "ALRT auto position");
}

static void test_parse_fref() {
    uint8_t data[11] = {};
    rsrcd::write_u32be(data, 0x4150504C);
    rsrcd::write_u16be(data + 4, 42);
    data[6] = 4;
    data[7] = 'A';
    data[8] = 'p';
    data[9] = 'p';
    data[10] = 's';

    rsrcd::finder::FileReference fref{};
    auto r = rsrcd::finder::parse_fref({data, sizeof(data)}, fref);
    CHECK_RESULT(r, "parse FREF");
    CHECK(fref.file_type == 0x4150504C, "FREF file type");
    CHECK(fref.local_id == 42, "FREF local ID");
    CHECK(fref.file_name.size == 4, "FREF name size");
}

static void test_parse_bndl() {
    uint8_t data[22] = {};
    rsrcd::write_u32be(data, 0x4F574E52);
    rsrcd::write_u16be(data + 4, 128);
    rsrcd::write_u16be(data + 6, 0);
    rsrcd::write_u32be(data + 8, 0x49434F4E);
    rsrcd::write_u16be(data + 12, 1);
    rsrcd::write_u16be(data + 14, 0);
    rsrcd::write_u16be(data + 16, 1000);
    rsrcd::write_u16be(data + 18, 1);
    rsrcd::write_u16be(data + 20, 1001);

    rsrcd::finder::Bundle<4, 8> bundle;
    auto r = rsrcd::finder::parse_bndl({data, sizeof(data)}, bundle);
    CHECK_RESULT(r, "parse BNDL");
    CHECK(bundle.owner_name == 0x4F574E52, "BNDL owner");
    CHECK(bundle.type_count() == 1, "BNDL type count");
    CHECK(bundle.type_at(0).resource_type == 0x49434F4E, "BNDL type");
    CHECK(bundle.type_at(0).count() == 2, "BNDL ID count");
    CHECK(bundle.type_at(0)[1].resource_id == 1001, "BNDL second resource ID");
}

static void test_parse_bndl_truncated() {
    uint8_t data[21] = {};
    rsrcd::write_u32be(data, 0x4F574E52);
    rsrcd::write_u16be(data + 6, 0);
    rsrcd::write_u32be(data + 8, 0x49434F4E);
    rsrcd::write_u16be(data + 12, 1);

    rsrcd::finder::Bundle<4, 8> bundle;
    auto r = rsrcd::finder::parse_bndl({data, sizeof(data)}, bundle);
    CHECK(!r, "truncated BNDL should fail");
}

static void test_parse_rov() {
    uint8_t data[10] = {};
    rsrcd::write_u16be(data, 0x0750);
    rsrcd::write_u16be(data + 2, 1);
    rsrcd::write_u32be(data + 4, 0x4D454E55);
    rsrcd::write_u16be(data + 8, 128);

    rsrcd::rov::OverrideList<4> overrides;
    auto r = rsrcd::rov::parse({data, sizeof(data)}, overrides);
    CHECK_RESULT(r, "parse ROv#");
    CHECK(overrides.rom_version == 0x0750, "ROv# ROM version");
    CHECK(overrides.count() == 1, "ROv# count");
    CHECK(overrides[0].type == 0x4D454E55, "ROv# type");
    CHECK(overrides[0].id == 128, "ROv# id");
}

static void test_parse_rov_empty() {
    rsrcd::rov::OverrideList<4> overrides;
    auto r = rsrcd::rov::parse({}, overrides);
    CHECK_RESULT(r, "parse empty ROv#");
    CHECK(overrides.count() == 0, "empty ROv# count");
}

static void test_parse_rov_truncated() {
    uint8_t data[9] = {};
    rsrcd::write_u16be(data + 2, 1);

    rsrcd::rov::OverrideList<4> overrides;
    auto r = rsrcd::rov::parse({data, sizeof(data)}, overrides);
    CHECK(!r, "truncated ROv# should fail");
}

static void test_parse_rssc() {
    uint8_t data[24] = {};
    rsrcd::write_u32be(data, 0x52535343);
    rsrcd::write_u16be(data + 4, 22);
    data[22] = 0x4E;
    data[23] = 0x75;

    rsrcd::rssc::Resource rssc{};
    auto r = rsrcd::rssc::parse({data, sizeof(data)}, rssc);
    CHECK_RESULT(r, "parse RSSC");
    CHECK(rssc.type_signature == 0x52535343, "RSSC signature");
    CHECK(rssc.function_offsets[0] == 22, "RSSC first function offset");
    CHECK(rssc.code.size == 2, "RSSC code size");
}

static void test_parse_rssc_bad_signature() {
    uint8_t data[22] = {};

    rsrcd::rssc::Resource rssc{};
    auto r = rsrcd::rssc::parse({data, sizeof(data)}, rssc);
    CHECK(!r, "RSSC bad signature should fail");
}

static void test_parse_rssc_bad_offset() {
    uint8_t data[22] = {};
    rsrcd::write_u32be(data, 0x52535343);
    rsrcd::write_u16be(data + 4, 10);

    rsrcd::rssc::Resource rssc{};
    auto r = rsrcd::rssc::parse({data, sizeof(data)}, rssc);
    CHECK(!r, "RSSC header offset should fail");
}

static void test_parse_txst() {
    uint8_t data[20] = {};
    data[0] = 0x03;
    rsrcd::write_u16be(data + 2, 12);
    rsrcd::write_u16be(data + 4, 0x1111);
    rsrcd::write_u16be(data + 6, 0x2222);
    rsrcd::write_u16be(data + 8, 0x3333);
    data[10] = 9;
    data[11] = 'H';
    data[12] = 'e';
    data[13] = 'l';
    data[14] = 'v';
    data[15] = 'e';
    data[16] = 't';
    data[17] = 'i';
    data[18] = 'c';
    data[19] = 'a';

    rsrcd::txst::TextStyle style{};
    auto r = rsrcd::txst::parse({data, sizeof(data)}, style);
    CHECK_RESULT(r, "parse TxSt");
    CHECK(style.font_style == 0x03, "TxSt font style");
    CHECK(style.font_size == 12, "TxSt font size");
    CHECK(style.green == 0x2222, "TxSt green");
    CHECK(style.font_name.size == 9, "TxSt font name size");
}

static void test_parse_txst_truncated_name() {
    uint8_t data[11] = {};
    data[10] = 1;

    rsrcd::txst::TextStyle style{};
    auto r = rsrcd::txst::parse({data, sizeof(data)}, style);
    CHECK(!r, "truncated TxSt name should fail");
}

static void test_parse_rect_resource() {
    uint8_t data[8] = {};
    rsrcd::write_u16be(data, 1);
    rsrcd::write_u16be(data + 2, 2);
    rsrcd::write_u16be(data + 4, 101);
    rsrcd::write_u16be(data + 6, 202);

    rsrcd::ui::Rect rect{};
    auto r = rsrcd::simple_metadata::parse_rect({data, sizeof(data)}, rect);
    CHECK_RESULT(r, "parse RECT");
    CHECK(rect.top == 1, "RECT top");
    CHECK(rect.right == 202, "RECT right");
}

static void test_parse_tool() {
    uint8_t data[10] = {};
    rsrcd::write_u16be(data, 2);
    rsrcd::write_u16be(data + 2, 1);
    rsrcd::write_u16be(data + 4, 128);
    rsrcd::write_u16be(data + 6, 129);
    rsrcd::write_u16be(data + 8, 130);

    rsrcd::simple_metadata::ToolList<4> tools;
    auto r = rsrcd::simple_metadata::parse_tool({data, sizeof(data)}, tools);
    CHECK_RESULT(r, "parse TOOL");
    CHECK(tools.tools_per_row == 2, "TOOL tools per row");
    CHECK(tools.row_count == 1, "TOOL row count");
    CHECK(tools.count() == 3, "TOOL cursor count");
    CHECK(tools[2] == 130, "TOOL third cursor ID");
}

static void test_parse_tool_odd_length() {
    uint8_t data[5] = {};

    rsrcd::simple_metadata::ToolList<4> tools;
    auto r = rsrcd::simple_metadata::parse_tool({data, sizeof(data)}, tools);
    CHECK(!r, "odd-length TOOL should fail");
}

static void test_parse_pick() {
    uint8_t data[24] = {};
    rsrcd::write_u32be(data, 0x50494354);
    data[4] = 1;
    data[5] = 2;
    data[6] = 3;
    data[7] = 0;
    rsrcd::write_u16be(data + 8, 16);
    rsrcd::write_u16be(data + 10, 1);
    rsrcd::write_u32be(data + 12, 0x49434F4E);
    rsrcd::write_u16be(data + 16, 128);
    rsrcd::write_u32be(data + 18, 0x50494354);
    rsrcd::write_u16be(data + 22, 129);

    rsrcd::simple_metadata::Picker<4> picker;
    auto r = rsrcd::simple_metadata::parse_pick({data, sizeof(data)}, picker);
    CHECK_RESULT(r, "parse PICK");
    CHECK(picker.type == 0x50494354, "PICK type");
    CHECK(picker.use_color == 1, "PICK use color");
    CHECK(picker.vertical_cell_size == 16, "PICK cell size");
    CHECK(picker.count() == 2, "PICK resource count");
    CHECK(picker[1].id == 129, "PICK second resource ID");
}

static void test_parse_pick_truncated() {
    uint8_t data[23] = {};
    rsrcd::write_u16be(data + 10, 1);

    rsrcd::simple_metadata::Picker<4> picker;
    auto r = rsrcd::simple_metadata::parse_pick({data, sizeof(data)}, picker);
    CHECK(!r, "truncated PICK should fail");
}

// ============================================================================
// AddColor parser
// ============================================================================

static void test_addcolor_button() {
    uint8_t data[11] = {};
    data[0] = 0x01;
    rsrcd::write_u16be(data + 1, 42);
    rsrcd::write_u16be(data + 3, 3);
    rsrcd::write_u16be(data + 5, 0xFFFF);
    rsrcd::write_u16be(data + 7, 0x8000);
    rsrcd::write_u16be(data + 9, 0x0000);

    rsrcd::ac::ObjectList<4> list;
    auto r = rsrcd::ac::parse({data, 11}, list);
    CHECK_RESULT(r, "parse addcolor button");
    CHECK(list.count() == 1, "button count");
    auto& obj = list[0];
    CHECK(obj.type == rsrcd::ac::ObjButton, "button type");
    CHECK(!obj.hidden, "button not hidden");
    CHECK(obj.part_id == 42, "button part_id");
    CHECK(obj.bevel == 3, "button bevel");
    CHECK(obj.color.red == 0xFFFF, "button red");
    CHECK(obj.color.green == 0x8000, "button green");
    CHECK(obj.color.blue == 0x0000, "button blue");
}

static void test_addcolor_hidden_field() {
    uint8_t data[11] = {};
    data[0] = 0x82;
    rsrcd::write_u16be(data + 1, 10);
    rsrcd::write_u16be(data + 3, 0);
    rsrcd::write_u16be(data + 5, 0x1234);
    rsrcd::write_u16be(data + 7, 0x5678);
    rsrcd::write_u16be(data + 9, 0x9ABC);

    rsrcd::ac::ObjectList<4> list;
    auto r = rsrcd::ac::parse({data, 11}, list);
    CHECK_RESULT(r, "parse hidden field");
    auto& obj = list[0];
    CHECK(obj.type == rsrcd::ac::ObjField, "field type");
    CHECK(obj.hidden, "field hidden");
    CHECK(obj.part_id == 10, "field part_id");
}

static void test_addcolor_rect() {
    uint8_t data[17] = {};
    data[0] = 0x03;
    rsrcd::write_u16be(data + 1, 10);
    rsrcd::write_u16be(data + 3, 20);
    rsrcd::write_u16be(data + 5, 100);
    rsrcd::write_u16be(data + 7, 200);
    rsrcd::write_u16be(data + 9, static_cast<uint16_t>(-2));
    rsrcd::write_u16be(data + 11, 0x1111);
    rsrcd::write_u16be(data + 13, 0x2222);
    rsrcd::write_u16be(data + 15, 0x3333);

    rsrcd::ac::ObjectList<4> list;
    auto r = rsrcd::ac::parse({data, 17}, list);
    CHECK_RESULT(r, "parse addcolor rect");
    auto& obj = list[0];
    CHECK(obj.type == rsrcd::ac::ObjRect, "rect type");
    CHECK(obj.rect.top == 10, "rect top");
    CHECK(obj.rect.left == 20, "rect left");
    CHECK(obj.rect.bottom == 100, "rect bottom");
    CHECK(obj.rect.right == 200, "rect right");
    CHECK(obj.bevel == -2, "rect bevel negative");
    CHECK(obj.color.red == 0x1111, "rect red");
}

static void test_addcolor_picture() {
    uint8_t data[17] = {};
    data[0] = 0x04;
    rsrcd::write_u16be(data + 1, 0);
    rsrcd::write_u16be(data + 3, 0);
    rsrcd::write_u16be(data + 5, 50);
    rsrcd::write_u16be(data + 7, 100);
    data[9] = 1;
    data[10] = 4;
    data[11] = 'T';
    data[12] = 'E';
    data[13] = 'S';
    data[14] = 'T';

    rsrcd::ac::ObjectList<4> list;
    auto r = rsrcd::ac::parse({data, 15}, list);
    CHECK_RESULT(r, "parse addcolor picture");
    auto& obj = list[0];
    CHECK(obj.type == rsrcd::ac::ObjPictRes, "picture type");
    CHECK(obj.transparent, "picture transparent");
    CHECK(obj.name.size == 4, "picture name length");
    CHECK(std::memcmp(obj.name.data, "TEST", 4) == 0, "picture name content");
}

static void test_addcolor_picture_file() {
    const char* fname = "hello.pict";
    uint8_t data[22] = {};
    data[0] = 0x05;
    rsrcd::write_u16be(data + 1, 10);
    rsrcd::write_u16be(data + 3, 20);
    rsrcd::write_u16be(data + 5, 30);
    rsrcd::write_u16be(data + 7, 40);
    data[9] = 0;
    data[10] = 10;
    std::memcpy(data + 11, fname, 10);

    rsrcd::ac::ObjectList<4> list;
    auto r = rsrcd::ac::parse({data, 21}, list);
    CHECK_RESULT(r, "parse addcolor picture file");
    auto& obj = list[0];
    CHECK(obj.type == rsrcd::ac::ObjPictFile, "picture file type");
    CHECK(!obj.transparent, "picture file not transparent");
    CHECK(obj.name.size == 10, "picture file name length");
}

static void test_addcolor_multiple() {
    uint8_t data[28] = {};
    data[0] = 0x01;
    rsrcd::write_u16be(data + 1, 1);
    std::memset(data + 3, 0, 8);

    data[11] = 0x03;
    rsrcd::write_u16be(data + 12, 5);
    rsrcd::write_u16be(data + 14, 10);
    rsrcd::write_u16be(data + 16, 50);
    rsrcd::write_u16be(data + 18, 100);
    rsrcd::write_u16be(data + 20, 0);
    rsrcd::write_u16be(data + 22, 0xFFFF);
    rsrcd::write_u16be(data + 24, 0);
    rsrcd::write_u16be(data + 26, 0);

    rsrcd::ac::ObjectList<8> list;
    auto r = rsrcd::ac::parse({data, 28}, list);
    CHECK_RESULT(r, "parse multiple addcolor");
    CHECK(list.count() == 2, "multiple count");
    CHECK(list[0].type == rsrcd::ac::ObjButton, "first is button");
    CHECK(list[1].type == rsrcd::ac::ObjRect, "second is rect");
}

static void test_addcolor_truncated() {
    uint8_t data[5] = {0x01, 0x00, 0x01, 0x00, 0x00};
    rsrcd::ac::ObjectList<4> list;
    auto r = rsrcd::ac::parse({data, 5}, list);
    CHECK(!r, "truncated addcolor should fail");
}

static void test_addcolor_bad_type() {
    uint8_t data[11] = {0x06};
    rsrcd::ac::ObjectList<4> list;
    auto r = rsrcd::ac::parse({data, 11}, list);
    CHECK(!r, "bad addcolor type should fail");
}

static void test_addcolor_empty() {
    rsrcd::ac::ObjectList<4> list;
    auto r = rsrcd::ac::parse({nullptr, 0}, list);
    CHECK_RESULT(r, "parse empty addcolor");
    CHECK(list.count() == 0, "empty addcolor count");
}

// ============================================================================
// PLTE parser
// ============================================================================

static void test_plte_empty_buttons() {
    uint8_t data[24] = {};
    rsrcd::write_u16be(data + 2, 0x0030);
    rsrcd::write_u16be(data + 6, 0);
    rsrcd::write_u16be(data + 22, 0);

    rsrcd::plte::Palette<8> pal;
    auto r = rsrcd::plte::parse({data, 24}, pal);
    CHECK_RESULT(r, "parse empty PLTE");
    CHECK(pal.wdef == 3, "PLTE wdef");
    CHECK(!pal.show_name, "PLTE show_name false");
    CHECK(pal.button_count == 0, "PLTE no buttons");
}

static void test_plte_show_name() {
    uint8_t data[24] = {};
    rsrcd::write_u16be(data + 2, 0x0031);
    rsrcd::write_u16be(data + 22, 0);

    rsrcd::plte::Palette<8> pal;
    auto r = rsrcd::plte::parse({data, 24}, pal);
    CHECK_RESULT(r, "parse PLTE show_name");
    CHECK(pal.wdef == 3, "PLTE wdef with show_name");
    CHECK(pal.show_name, "PLTE show_name true");
}

static void test_plte_with_buttons() {
    uint8_t msg[] = {'O', 'K'};

    uint8_t data[40] = {};
    rsrcd::write_u16be(data + 2, 0);
    rsrcd::write_u16be(data + 6, 1);
    rsrcd::write_u16be(data + 8, 42);
    rsrcd::write_u16be(data + 22, 1);

    size_t i = 24;
    rsrcd::write_u16be(data + i, 10);
    rsrcd::write_u16be(data + i + 2, 20);
    rsrcd::write_u16be(data + i + 4, 30);
    rsrcd::write_u16be(data + i + 6, 40);
    data[i + 10] = 2;
    std::memcpy(data + i + 11, msg, 2);

    rsrcd::plte::Palette<8> pal;
    auto r = rsrcd::plte::parse({data, 37}, pal);
    CHECK_RESULT(r, "parse PLTE with buttons");
    CHECK(pal.frame, "PLTE frame");
    CHECK(pal.pict_ref == 42, "PLTE pict_ref");
    CHECK(pal.button_count == 1, "PLTE button count");
    CHECK(pal.buttons[0].top == 10, "PLTE button top");
    CHECK(pal.buttons[0].left == 20, "PLTE button left");
    CHECK(pal.buttons[0].message.size == 2, "PLTE button msg len");
    CHECK(std::memcmp(pal.buttons[0].message.data, "OK", 2) == 0, "PLTE button msg");
}

static void test_plte_too_small() {
    uint8_t data[16] = {};
    rsrcd::plte::Palette<8> pal;
    auto r = rsrcd::plte::parse({data, 16}, pal);
    CHECK(!r, "PLTE too small should fail");
}

// ============================================================================
// Text helpers
// ============================================================================

static void test_text_str() {
    uint8_t data[] = {3, 'O', 'n', 'e'};
    rsrcd::text::StringRef text;
    auto r = rsrcd::text::parse_str({data, sizeof(data)}, text);
    CHECK_RESULT(r, "parse STR");
    CHECK(text.bytes.size == 3, "STR size");
    CHECK(std::memcmp(text.bytes.data, "One", 3) == 0, "STR bytes");
}

static void test_text_str_too_small() {
    uint8_t data[] = {4, 'O', 'n', 'e'};
    rsrcd::text::StringRef text;
    auto r = rsrcd::text::parse_str({data, sizeof(data)}, text);
    CHECK(!r, "truncated STR should fail");
}

static void test_text_str_list() {
    uint8_t data[] = {0, 2, 3, 'O', 'n', 'e', 3, 'T', 'w', 'o'};
    rsrcd::text::StringList<4> strings;
    auto r = rsrcd::text::parse_str_list({data, sizeof(data)}, strings);
    CHECK_RESULT(r, "parse STR#");
    CHECK(strings.count() == 2, "STR# count");
    CHECK(std::memcmp(strings[0].bytes.data, "One", 3) == 0, "STR# first");
    CHECK(std::memcmp(strings[1].bytes.data, "Two", 3) == 0, "STR# second");
}

static void test_text_str_list_capacity() {
    uint8_t data[] = {0, 2, 1, 'A', 1, 'B'};
    rsrcd::text::StringList<1> strings;
    auto r = rsrcd::text::parse_str_list({data, sizeof(data)}, strings);
    CHECK(!r, "STR# capacity overflow should fail");
}

static void test_text_twcs() {
    uint8_t data[] = {0, 2, 0, 3, 'O', 'n', 'e', 0, 3, 'T', 'w', 'o'};
    rsrcd::text::StringList<4> strings;
    auto r = rsrcd::text::parse_twcs({data, sizeof(data)}, strings);
    CHECK_RESULT(r, "parse TwCS");
    CHECK(strings.count() == 2, "TwCS count");
    CHECK(std::memcmp(strings[1].bytes.data, "Two", 3) == 0, "TwCS second");
}

static void test_text_twcs_encrypted_rejected() {
    uint8_t data[] = {0, 1, 1, 1, 0};
    rsrcd::text::StringList<4> strings;
    auto r = rsrcd::text::parse_twcs({data, sizeof(data)}, strings);
    CHECK(!r, "encrypted TwCS should fail");
}

static void test_text_text() {
    uint8_t data[] = {'T', 'E', 'X', 'T'};
    rsrcd::text::StringRef text;
    auto r = rsrcd::text::parse_text({data, sizeof(data)}, text);
    CHECK_RESULT(r, "parse TEXT");
    CHECK(text.bytes.size == sizeof(data), "TEXT size");
    CHECK(text.bytes.data == data, "TEXT view");
}

// ============================================================================
// vers metadata
// ============================================================================

static void test_vers() {
    uint8_t data[] = {
        0x01, 0x23, 0x80, 0x00, 0x00, 0x00,
        3, '1', '.', '2',
        8, 'V', 'e', 'r', 's', 'i', 'o', 'n', '!'
    };
    rsrcd::vers::Version version;
    auto r = rsrcd::vers::parse({data, sizeof(data)}, version);
    CHECK_RESULT(r, "parse vers");
    CHECK(version.major_revision == 0x01, "vers major raw");
    CHECK(version.minor_and_bug_revision == 0x23, "vers minor raw");
    CHECK(version.development_stage == 0x80, "vers stage raw");
    CHECK(version.short_version.size == 3, "vers short size");
    CHECK(std::memcmp(version.short_version.data, "1.2", 3) == 0, "vers short");
    CHECK(version.long_version.size == 8, "vers long size");
}

static void test_vers_truncated() {
    uint8_t data[] = {0x01, 0x00, 0x80, 0x00, 0x00, 0x00, 5, '1'};
    rsrcd::vers::Version version;
    auto r = rsrcd::vers::parse({data, sizeof(data)}, version);
    CHECK(!r, "truncated vers should fail");
}

// ============================================================================
// Color tables
// ============================================================================

static void test_color_table() {
    uint8_t data[24] = {};
    rsrcd::write_u32be(data, 0x12345678);
    rsrcd::write_u16be(data + 4, 0x8000);
    rsrcd::write_u16be(data + 6, 1);
    rsrcd::write_u16be(data + 8, 0);
    rsrcd::write_u16be(data + 10, 0xFFFF);
    rsrcd::write_u16be(data + 12, 0);
    rsrcd::write_u16be(data + 14, 0);
    rsrcd::write_u16be(data + 16, 1);
    rsrcd::write_u16be(data + 18, 0);
    rsrcd::write_u16be(data + 20, 0xFFFF);
    rsrcd::write_u16be(data + 22, 0);

    rsrcd::color_table::Table<4> table;
    auto r = rsrcd::color_table::parse({data, sizeof(data)}, table);
    CHECK_RESULT(r, "parse color table");
    CHECK(table.seed == 0x12345678, "color table seed");
    CHECK(table.flags == 0x8000, "color table flags");
    CHECK(table.count() == 2, "color table count");
    CHECK(table[1].green == 0xFFFF, "color table entry");
}

static void test_color_table_truncated() {
    uint8_t data[15] = {};
    rsrcd::write_u16be(data + 6, 1);
    rsrcd::color_table::Table<4> table;
    auto r = rsrcd::color_table::parse({data, sizeof(data)}, table);
    CHECK(!r, "truncated color table should fail");
}

static void test_pltt() {
    uint8_t data[32] = {};
    rsrcd::write_u16be(data, 1);
    rsrcd::write_u16be(data + 18, 0x1111);
    rsrcd::write_u16be(data + 20, 0x2222);
    rsrcd::write_u16be(data + 22, 0x3333);

    rsrcd::pltt::Palette<4> palette;
    auto r = rsrcd::pltt::parse({data, sizeof(data)}, palette);
    CHECK_RESULT(r, "parse pltt");
    CHECK(palette.count() == 1, "pltt count");
    CHECK(palette[0].green == 0x2222, "pltt green");
}

static void test_pltt_truncated() {
    uint8_t data[31] = {};
    rsrcd::write_u16be(data, 1);
    rsrcd::pltt::Palette<4> palette;
    auto r = rsrcd::pltt::parse({data, sizeof(data)}, palette);
    CHECK(!r, "truncated pltt should fail");
}

// ============================================================================
// SIZE metadata
// ============================================================================

static void test_size_resource() {
    uint8_t data[10] = {};
    rsrcd::write_u16be(data, 0xD048);
    rsrcd::write_u32be(data + 2, 0x00100000);
    rsrcd::write_u32be(data + 6, 0x00080000);

    rsrcd::size_resource::Size size;
    auto r = rsrcd::size_resource::parse({data, sizeof(data)}, size);
    CHECK_RESULT(r, "parse SIZE");
    CHECK(size.flags == 0xD048, "SIZE flags");
    CHECK(size.save_screen, "SIZE save screen");
    CHECK(size.accept_suspend_events, "SIZE accept suspend");
    CHECK(size.can_background, "SIZE can background");
    CHECK(size.high_level_event_aware, "SIZE high-level aware");
    CHECK(size.use_text_edit_services, "SIZE text edit services");
    CHECK(size.preferred_size == 0x00100000, "SIZE preferred");
    CHECK(size.minimum_size == 0x00080000, "SIZE minimum");
}

static void test_size_resource_truncated() {
    uint8_t data[9] = {};
    rsrcd::size_resource::Size size;
    auto r = rsrcd::size_resource::parse({data, sizeof(data)}, size);
    CHECK(!r, "truncated SIZE should fail");
}

static void test_finf() {
    uint8_t data[14] = {};
    rsrcd::write_u16be(data, 2);
    rsrcd::write_u16be(data + 2, 128);
    rsrcd::write_u16be(data + 4, 1);
    rsrcd::write_u16be(data + 6, 12);
    rsrcd::write_u16be(data + 8, 129);
    rsrcd::write_u16be(data + 10, 2);
    rsrcd::write_u16be(data + 12, 18);

    rsrcd::finf::FontInfoList<4> font_info;
    auto r = rsrcd::finf::parse({data, sizeof(data)}, font_info);
    CHECK_RESULT(r, "parse finf");
    CHECK(font_info.count() == 2, "finf count");
    CHECK(font_info[1].font_id == 129, "finf font id");
    CHECK(font_info[1].size == 18, "finf size");
}

static void test_finf_truncated() {
    uint8_t data[7] = {};
    rsrcd::write_u16be(data, 1);
    rsrcd::finf::FontInfoList<4> font_info;
    auto r = rsrcd::finf::parse({data, sizeof(data)}, font_info);
    CHECK(!r, "truncated finf should fail");
}

// ============================================================================
// UI metadata
// ============================================================================

static void write_test_rect(uint8_t* data) {
    rsrcd::write_u16be(data, 1);
    rsrcd::write_u16be(data + 2, 2);
    rsrcd::write_u16be(data + 4, 30);
    rsrcd::write_u16be(data + 6, 40);
}

static void test_ui_cntl() {
    uint8_t data[25] = {};
    write_test_rect(data);
    rsrcd::write_u16be(data + 8, 5);
    rsrcd::write_u16be(data + 10, 1);
    rsrcd::write_u16be(data + 12, 100);
    rsrcd::write_u16be(data + 14, 0);
    rsrcd::write_u16be(data + 16, 8);
    rsrcd::write_u32be(data + 18, 0x12345678);
    data[22] = 2;
    data[23] = 'O';
    data[24] = 'K';

    rsrcd::ui::Control control;
    auto r = rsrcd::ui::parse_cntl({data, sizeof(data)}, control);
    CHECK_RESULT(r, "parse CNTL");
    CHECK(control.bounds.bottom == 30, "CNTL bounds");
    CHECK(control.value == 5, "CNTL value");
    CHECK(control.visible, "CNTL visible");
    CHECK(control.title.size == 2, "CNTL title size");
}

static void test_ui_dlog() {
    uint8_t data[30] = {};
    write_test_rect(data);
    rsrcd::write_u16be(data + 8, 4);
    rsrcd::write_u16be(data + 10, 1);
    rsrcd::write_u16be(data + 12, 1);
    rsrcd::write_u32be(data + 14, 0x12345678);
    rsrcd::write_u16be(data + 18, 128);
    data[20] = 5;
    std::memcpy(data + 21, "Hello", 5);
    rsrcd::write_u16be(data + 26, 0xABCD);

    rsrcd::ui::Dialog dialog;
    auto r = rsrcd::ui::parse_dlog({data, 28}, dialog);
    CHECK_RESULT(r, "parse DLOG");
    CHECK(dialog.items_id == 128, "DLOG items");
    CHECK(dialog.go_away, "DLOG go away");
    CHECK(dialog.has_auto_position, "DLOG auto position");
    CHECK(dialog.auto_position == 0xABCD, "DLOG auto position value");
}

static void test_ui_wind() {
    uint8_t data[26] = {};
    write_test_rect(data);
    rsrcd::write_u16be(data + 8, 16);
    rsrcd::write_u16be(data + 10, 1);
    rsrcd::write_u16be(data + 12, 0);
    rsrcd::write_u32be(data + 14, 0x12345678);
    data[18] = 3;
    std::memcpy(data + 19, "Win", 3);

    rsrcd::ui::Window window;
    auto r = rsrcd::ui::parse_wind({data, 22}, window);
    CHECK_RESULT(r, "parse WIND");
    CHECK(window.proc_id == 16, "WIND proc");
    CHECK(window.visible, "WIND visible");
    CHECK(!window.has_auto_position, "WIND no auto position");
}

static void test_ui_truncated() {
    uint8_t data[8] = {};
    rsrcd::ui::Control control;
    auto r = rsrcd::ui::parse_cntl({data, sizeof(data)}, control);
    CHECK(!r, "truncated CNTL should fail");
}

static void test_ui_menu() {
    uint8_t data[64] = {};
    rsrcd::write_u16be(data, 128);
    rsrcd::write_u16be(data + 6, 0);
    rsrcd::write_u32be(data + 10, 0x00000003);
    size_t offset = 14;
    data[offset++] = 4;
    std::memcpy(data + offset, "File", 4);
    offset += 4;
    data[offset++] = 4;
    std::memcpy(data + offset, "Open", 4);
    offset += 4;
    data[offset++] = 0;
    data[offset++] = 'O';
    data[offset++] = 0;
    data[offset++] = 0;
    data[offset++] = 0;

    rsrcd::ui::MenuItemList<4> menu;
    auto r = rsrcd::ui::parse_menu({data, offset}, menu);
    CHECK_RESULT(r, "parse MENU");
    CHECK(menu.menu_id == 128, "MENU id");
    CHECK(menu.enabled, "MENU enabled");
    CHECK(menu.title.size == 4, "MENU title");
    CHECK(menu.count() == 1, "MENU item count");
    CHECK(menu[0].enabled, "MENU item enabled");
    CHECK(menu[0].key_equivalent == 'O', "MENU key");
}

static void test_ui_menu_capacity() {
    uint8_t data[64] = {};
    rsrcd::write_u32be(data + 10, 0x00000007);
    size_t offset = 14;
    data[offset++] = 1;
    data[offset++] = 'M';
    for (int i = 0; i < 2; ++i) {
        data[offset++] = 1;
        data[offset++] = static_cast<uint8_t>('A' + i);
        data[offset++] = 0;
        data[offset++] = 0;
        data[offset++] = 0;
        data[offset++] = 0;
    }
    data[offset++] = 0;

    rsrcd::ui::MenuItemList<1> menu;
    auto r = rsrcd::ui::parse_menu({data, offset}, menu);
    CHECK(!r, "MENU capacity overflow should fail");
}

static void test_ui_ditl() {
    uint8_t data[64] = {};
    rsrcd::write_u16be(data, 1);
    size_t offset = 2;
    offset += 4;
    write_test_rect(data + offset);
    offset += 8;
    data[offset++] = 0x04;
    data[offset++] = 2;
    data[offset++] = 'O';
    data[offset++] = 'K';
    if (offset & 1u) {
        data[offset++] = 0;
    }
    offset += 4;
    write_test_rect(data + offset);
    offset += 8;
    data[offset++] = 0x20;
    data[offset++] = 2;
    rsrcd::write_u16be(data + offset, 128);
    offset += 2;
    if (offset & 1u) {
        data[offset++] = 0;
    }

    rsrcd::ui::DialogItemList<4> items;
    auto r = rsrcd::ui::parse_ditl({data, offset}, items);
    CHECK_RESULT(r, "parse DITL");
    CHECK(items.count() == 2, "DITL item count");
    CHECK(items[0].kind == rsrcd::ui::DialogItemKind::Button, "DITL button kind");
    CHECK(items[0].info.size == 2, "DITL button info");
    CHECK(items[1].kind == rsrcd::ui::DialogItemKind::Icon, "DITL icon kind");
    CHECK(items[1].has_resource_id, "DITL icon resource id");
    CHECK(items[1].resource_id == 128, "DITL resource id value");
}

static void test_ui_ditl_bad_resource_id() {
    uint8_t data[32] = {};
    rsrcd::write_u16be(data, 0);
    size_t offset = 2 + 4;
    write_test_rect(data + offset);
    offset += 8;
    data[offset++] = 0x20;
    data[offset++] = 1;
    data[offset++] = 0;
    if (offset & 1u) {
        data[offset++] = 0;
    }

    rsrcd::ui::DialogItemList<4> items;
    auto r = rsrcd::ui::parse_ditl({data, offset}, items);
    CHECK(!r, "DITL bad resource id length should fail");
}

// ============================================================================
// PAT# helpers
// ============================================================================

static void test_patlist_count() {
    uint8_t data[18] = {};
    rsrcd::write_u16be(data, 2);
    data[2] = 0xFF;
    data[10] = 0xAA;

    CHECK(rsrcd::patlist::count({data, 18}) == 2, "patlist count");

    CHECK(rsrcd::patlist::count({data, 1}) == 0, "patlist count too small");

    auto p0 = rsrcd::patlist::pattern_at({data, 18}, 0);
    CHECK(p0.size == 8 && p0.data[0] == 0xFF, "patlist pattern 0");

    auto p1 = rsrcd::patlist::pattern_at({data, 18}, 1);
    CHECK(p1.size == 8 && p1.data[0] == 0xAA, "patlist pattern 1");

    auto p2 = rsrcd::patlist::pattern_at({data, 18}, 2);
    CHECK(p2.empty(), "patlist out of bounds");
}

// ============================================================================
// Palette lookups
// ============================================================================

static void test_palette_lookup() {
    auto white = rsrcd::img::color8(0xFF);
    CHECK((white & 0xFF000000) != 0, "clut8 white alpha");

    auto black = rsrcd::img::color8(0x00);
    CHECK(black == rsrcd::img::clut8[0], "clut8[0] white");

    auto c4 = rsrcd::img::color4(0);
    CHECK(c4 == rsrcd::img::clut4[0], "clut4[0]");
}

// ============================================================================
// Result type
// ============================================================================

static void test_result() {
    auto ok = rsrcd::Result::ok();
    CHECK(ok, "ok result is truthy");
    CHECK(ok.code == 0, "ok code");
    CHECK(ok.error_code() == rsrcd::Err::None, "ok error code");

    auto err = rsrcd::Error::invalid_data("test");
    CHECK(!err, "error result is falsy");
    CHECK(err.error_code() == rsrcd::Err::InvalidData, "invalid data error code");

    auto comp = rsrcd::Error::compression_unsupported();
    CHECK(comp.error_code() == rsrcd::Err::CompressionUnsupported, "compression error");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    test_big_endian();
    test_fourcc();
    test_bytes();
    test_vec_parser_output_heap_indexing();
    test_vec_parser_output_capacity_overflow();
    test_result();
    test_parse_fork_empty();
    test_parse_fork_too_small();
    test_parse_fork_single_resource();
    test_parse_fork_many_resources_cross_inline_capacity();
    test_parse_fork_truncated_ref_list_stops_cleanly();
    test_parse_fork_declared_count_clamps_to_complete_refs();
    test_parse_fork_invalid_data_offset_leaves_empty_data();
    test_parse_fork_invalid_name_offset_leaves_empty_name();
    test_parse_fork_ref_exactly_at_map_boundary();
    test_parse_fork_overflowing_data_range_fails();
    test_parse_adf_too_small();
    test_parse_adf_bad_magic();
    test_parse_adf_single_entry();
    test_parse_adf_invalid_entry_range_fails();
    test_fork_view_find();
    test_decode_icon_bw_all_white();
    test_decode_icon_bw_all_black();
    test_decode_icon_bw_too_small();
    test_decode_icon_bw_top_left_pixel();
    test_decode_icn_bw_mask();
    test_decode_curs();
    test_decode_curs_transparent();
    test_decode_curs_too_small();
    test_decode_pat_all_white();
    test_decode_pat_checkerboard();
    test_decode_sicn();
    test_decode_sicn_too_small();
    test_decode_icon_4bit();
    test_decode_icon_8bit();
    test_decode_icon_4bit_bad_size();
    test_decode_icon_8bit_bad_size();
    test_decode_icon_1bit_list_image();
    test_decode_icon_1bit_masked_pair();
    test_decode_icon_1bit_list_bad_size();
    test_parse_cfrg_single_entry();
    test_parse_cfrg_bad_version();
    test_parse_mbar();
    test_parse_mbar_truncated();
    test_parse_alrt();
    test_parse_fref();
    test_parse_bndl();
    test_parse_bndl_truncated();
    test_parse_rov();
    test_parse_rov_empty();
    test_parse_rov_truncated();
    test_parse_rssc();
    test_parse_rssc_bad_signature();
    test_parse_rssc_bad_offset();
    test_parse_txst();
    test_parse_txst_truncated_name();
    test_parse_rect_resource();
    test_parse_tool();
    test_parse_tool_odd_length();
    test_parse_pick();
    test_parse_pick_truncated();
    test_addcolor_button();
    test_addcolor_hidden_field();
    test_addcolor_rect();
    test_addcolor_picture();
    test_addcolor_picture_file();
    test_addcolor_multiple();
    test_addcolor_truncated();
    test_addcolor_bad_type();
    test_addcolor_empty();
    test_plte_empty_buttons();
    test_plte_show_name();
    test_plte_with_buttons();
    test_plte_too_small();
    test_text_str();
    test_text_str_too_small();
    test_text_str_list();
    test_text_str_list_capacity();
    test_text_twcs();
    test_text_twcs_encrypted_rejected();
    test_text_text();
    test_vers();
    test_vers_truncated();
    test_color_table();
    test_color_table_truncated();
    test_pltt();
    test_pltt_truncated();
    test_size_resource();
    test_size_resource_truncated();
    test_finf();
    test_finf_truncated();
    test_ui_cntl();
    test_ui_dlog();
    test_ui_wind();
    test_ui_truncated();
    test_ui_menu();
    test_ui_menu_capacity();
    test_ui_ditl();
    test_ui_ditl_bad_resource_id();
    test_patlist_count();
    test_palette_lookup();

    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) failed.\n", g_failures);
    return 1;
}
