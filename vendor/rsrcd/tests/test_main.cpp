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

static void test_text_text() {
    uint8_t data[] = {'T', 'E', 'X', 'T'};
    rsrcd::text::StringRef text;
    auto r = rsrcd::text::parse_text({data, sizeof(data)}, text);
    CHECK_RESULT(r, "parse TEXT");
    CHECK(text.bytes.size == sizeof(data), "TEXT size");
    CHECK(text.bytes.data == data, "TEXT view");
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
    test_decode_curs();
    test_decode_curs_transparent();
    test_decode_curs_too_small();
    test_decode_pat_all_white();
    test_decode_pat_checkerboard();
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
    test_text_text();
    test_patlist_count();
    test_palette_lookup();

    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) failed.\n", g_failures);
    return 1;
}
