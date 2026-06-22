#include "doctest.h"
#include "nano-osc.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace NanoOsc;

TEST_CASE("truncated packet — fewer than 4 bytes")
{
    for (size_t n : {size_t {0}, size_t {1}, size_t {2}, size_t {3}})
    {
        std::vector<uint8_t> buf(n, 0x41); // non-null bytes
        CHECK_THROWS_AS(Message::decode(buf.data(), buf.size()), std::runtime_error);
    }
}

TEST_CASE("address with no null terminator")
{
    // 8 non-null bytes — null-scan exhausts buffer
    std::vector<uint8_t> buf(8, 0x41);
    CHECK_THROWS_AS(Message::decode(buf.data(), buf.size()), std::runtime_error);
}

TEST_CASE("tag string with no null terminator")
{
    // valid address "/x\0\0" (4 bytes), then non-null tag bytes that never terminate
    std::vector<uint8_t> buf = {'/', 'x', 0x00, 0x00, ',', 'i', 'f', 's'};
    CHECK_THROWS_AS(Message::decode(buf.data(), buf.size()), std::runtime_error);
}

TEST_CASE("blob length prefix exceeds remaining buffer")
{
    // address "/b\0\0" (4) + tags ",b\0\0" (4) + 4-byte length = 0xFFFFFFFF, no data
    std::vector<uint8_t> buf = {
        '/',
        'b',
        0x00,
        0x00,
        ',',
        'b',
        0x00,
        0x00,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
    };
    CHECK_THROWS_AS(Message::decode(buf.data(), buf.size()), std::runtime_error);
}

TEST_CASE("int32 arg with no payload bytes")
{
    // address "/i\0\0" (4) + tags ",i\0\0" (4); no 4 bytes for the int payload
    std::vector<uint8_t> buf = {
        '/',
        'i',
        0x00,
        0x00,
        ',',
        'i',
        0x00,
        0x00,
    };
    CHECK_THROWS_AS(Message::decode(buf.data(), buf.size()), std::runtime_error);
}

TEST_CASE("not a bundle — wrong magic")
{
    std::vector<uint8_t> buf(16, 0x00); // 16 bytes of zeros: not "#bundle"
    CHECK_THROWS_AS(Bundle::decode(buf.data(), buf.size()), std::runtime_error);
}

TEST_CASE("bundle too short for header + timetag")
{
    std::vector<uint8_t> buf = {'#', 'b', 'u', 'n', 'd', 'l', 'e', 0x00}; // 8 bytes — no timetag
    CHECK_THROWS_AS(Bundle::decode(buf.data(), buf.size()), std::runtime_error);
}

TEST_CASE("bundle sub-packet length exceeds remaining bytes")
{
    std::vector<uint8_t> buf = {
        '#',  'b',  'u',  'n',  'd', 'l', 'e', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // timetag
        0xFF, 0xFF, 0xFF, 0xFF, // sub-packet length: huge
    };
    CHECK_THROWS_AS(Bundle::decode(buf.data(), buf.size()), std::runtime_error);
}

TEST_CASE("bundle sub-packet too small to be a message")
{
    std::vector<uint8_t> buf = {
        '#',  'b',  'u',  'n',  'd', 'l', 'e', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // timetag
        0x00, 0x00, 0x00, 0x02, // sub-packet length: 2
        0x41, 0x42,             // 2 bytes — no valid address
    };
    CHECK_THROWS_AS(Bundle::decode(buf.data(), buf.size()), std::runtime_error);
}

TEST_CASE("missing tag string after valid address")
{
    // address "/x\0\0" (4 bytes), then nothing
    std::vector<uint8_t> buf = {'/', 'x', 0x00, 0x00};
    CHECK_THROWS_AS(Message::decode(buf.data(), buf.size()), std::runtime_error);
}

TEST_CASE("Message::encode_into rejects malformed address and tags")
{
    std::vector<uint8_t> buf(64);
    size_t               written = 0;

    Message no_slash;
    no_slash.address = "bad";
    CHECK_FALSE(no_slash.encode_into(buf.data(), buf.size(), written));

    Message empty_addr;
    empty_addr.address = "";
    CHECK_FALSE(empty_addr.encode_into(buf.data(), buf.size(), written));

    Message bad_tags("/x");
    bad_tags.tags = ";bad"; // tags must start with ','
    CHECK_FALSE(bad_tags.encode_into(buf.data(), buf.size(), written));

    Message missing_arg("/x");
    missing_arg.tags = ",i";
    CHECK_FALSE(missing_arg.encode_into(buf.data(), buf.size(), written));

    Message missing_tag("/x");
    missing_tag.arguments.emplace_back(OSCInt {1});
    CHECK_FALSE(missing_tag.encode_into(buf.data(), buf.size(), written));

    Message wrong_type("/x");
    wrong_type.tags = ",f";
    wrong_type.arguments.emplace_back(OSCInt {1});
    CHECK_FALSE(wrong_type.encode_into(buf.data(), buf.size(), written));

    Message wrong_no_payload_type("/x");
    wrong_no_payload_type.tags = ",T";
    wrong_no_payload_type.arguments.emplace_back(OSCFalse {});
    CHECK_FALSE(wrong_no_payload_type.encode_into(buf.data(), buf.size(), written));
}

TEST_CASE("Message::encode_into rejects marker tag/value mismatches")
{
    std::vector<uint8_t> buf(64);
    size_t               written = 0;

    Message true_as_false("/x");
    true_as_false.tags = ",T";
    true_as_false.arguments.emplace_back(OSCFalse {});
    CHECK_FALSE(true_as_false.encode_into(buf.data(), buf.size(), written));

    Message false_as_nil("/x");
    false_as_nil.tags = ",F";
    false_as_nil.arguments.emplace_back(OSCNil {});
    CHECK_FALSE(false_as_nil.encode_into(buf.data(), buf.size(), written));

    Message nil_as_impulse("/x");
    nil_as_impulse.tags = ",N";
    nil_as_impulse.arguments.emplace_back(OSCImpulse {});
    CHECK_FALSE(nil_as_impulse.encode_into(buf.data(), buf.size(), written));

    Message impulse_as_true("/x");
    impulse_as_true.tags = ",I";
    impulse_as_true.arguments.emplace_back(OSCTrue {});
    CHECK_FALSE(impulse_as_true.encode_into(buf.data(), buf.size(), written));
}

TEST_CASE("Bundle::decode rejects nesting deeper than MAX_BUNDLE_DEPTH")
{
    // Build a bundle nested MAX_BUNDLE_DEPTH + 1 levels deep.
    Bundle  root;
    Bundle* cursor = &root;
    for (int i = 0; i < MAX_BUNDLE_DEPTH; ++i)
    {
        Bundle child;
        cursor->add_bundle(child);
        cursor = &cursor->bundles.back();
    }
    // root depth = MAX_BUNDLE_DEPTH + 1 total levels (root + MAX_BUNDLE_DEPTH children)

    auto bytes = root.encode();
    REQUIRE_FALSE(bytes.empty());
    CHECK_THROWS_AS(Bundle::decode(bytes.data(), bytes.size()), std::runtime_error);
}

TEST_CASE("write_blob rejects size > UINT32_MAX")
{
    // We can't actually construct a 4 GB blob in a test, so call the detail
    // helper directly with a bogus large size and a small destination.
    std::vector<uint8_t> dst(16);
    size_t               off = 0;
    uint8_t              src = 0;
    CHECK_FALSE(NanoOsc::detail::write_blob(dst.data(), dst.size(), off, &src, static_cast<size_t>(UINT32_MAX) + 1));
}
