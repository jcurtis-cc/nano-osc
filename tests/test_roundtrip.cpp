#include "doctest.h"
#include "nano-osc.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

using namespace NanoOsc;

namespace {

Message roundtrip(const Message& in)
{
    auto bytes = in.encode();
    return Message::decode(bytes.data(), bytes.size());
}

Bundle roundtrip(const Bundle& in)
{
    auto bytes = in.encode();
    return Bundle::decode(bytes.data(), bytes.size());
}

}  // namespace

TEST_CASE("int32 roundtrip")
{
    Message m("/i");
    m.add_int32(-2147483648);
    auto r = roundtrip(m);
    CHECK(r.address == "/i");
    CHECK(r.tags == ",i");
    REQUIRE(r.arguments.size() == 1);
    CHECK(std::get<OSCInt>(r.arguments[0]) == -2147483648);
}

TEST_CASE("float32 roundtrip")
{
    Message m("/f");
    m.add_float(-0.5f);
    auto r = roundtrip(m);
    CHECK(r.tags == ",f");
    REQUIRE(r.arguments.size() == 1);
    CHECK(std::get<OSCFloat>(r.arguments[0]) == -0.5f);
}

TEST_CASE("string roundtrip")
{
    Message m("/s");
    m.add_string("hello");
    auto r = roundtrip(m);
    CHECK(r.tags == ",s");
    REQUIRE(r.arguments.size() == 1);
    CHECK(std::get<OSCString>(r.arguments[0]) == "hello");
}

TEST_CASE("blob roundtrip — multiple lengths")
{
    for (size_t len : {size_t {0}, size_t {1}, size_t {3}, size_t {4}, size_t {5}, size_t {16}})
    {
        std::vector<uint8_t> data(len);
        for (size_t i = 0; i < len; ++i) data[i] = static_cast<uint8_t>(i + 1);

        Message m("/b");
        m.add_blob(data.data(), data.size());
        auto r = roundtrip(m);
        CHECK(r.tags == ",b");
        REQUIRE(r.arguments.size() == 1);
        const auto& got = std::get<OSCBlob>(r.arguments[0]);
        CHECK(got.size() == len);
        CHECK(got == data);
    }
}

TEST_CASE("int64 roundtrip (h)")
{
    Message m("/h");
    m.tags.push_back('h');
    m.arguments.emplace_back(OSCInt64 {-1234567890123LL});
    auto r = roundtrip(m);
    CHECK(r.tags == ",h");
    REQUIRE(r.arguments.size() == 1);
    CHECK(std::get<OSCInt64>(r.arguments[0]) == -1234567890123LL);
}

TEST_CASE("float64 roundtrip (d) — proves B1 fix")
{
    Message m("/d");
    m.tags.push_back('d');
    const double v = 3.141592653589793;
    m.arguments.emplace_back(OSCFloat64 {v});
    auto r = roundtrip(m);
    CHECK(r.tags == ",d");
    REQUIRE(r.arguments.size() == 1);
    const double got = std::get<OSCFloat64>(r.arguments[0]);
    CHECK(got == v);
    // Pre-B1 fix, upper 32 bits of mantissa were zero → got != v
}

TEST_CASE("symbol roundtrip (S)")
{
    Message m("/S");
    m.tags.push_back('S');
    m.arguments.emplace_back(OSCString {"sym"});
    auto r = roundtrip(m);
    CHECK(r.tags == ",S");
    REQUIRE(r.arguments.size() == 1);
    CHECK(std::get<OSCString>(r.arguments[0]) == "sym");
}

TEST_CASE("timetag-as-arg roundtrip (t)")
{
    Message m("/t");
    m.tags.push_back('t');
    const OSCTimeTag tt = 0xDEADBEEFCAFEBABEULL;
    m.arguments.emplace_back(tt);
    auto r = roundtrip(m);
    CHECK(r.tags == ",t");
    REQUIRE(r.arguments.size() == 1);
    CHECK(std::get<OSCTimeTag>(r.arguments[0]) == tt);
}

TEST_CASE("mixed-arg message")
{
    Message m("/test");
    m.add_int32(1);
    m.add_float(2.5f);
    m.add_string("hi");
    auto r = roundtrip(m);
    CHECK(r.address == "/test");
    CHECK(r.tags == ",ifs");
    REQUIRE(r.arguments.size() == 3);
    CHECK(std::get<OSCInt>(r.arguments[0]) == 1);
    CHECK(std::get<OSCFloat>(r.arguments[1]) == 2.5f);
    CHECK(std::get<OSCString>(r.arguments[2]) == "hi");
}

TEST_CASE("empty-arg message")
{
    Message m("/ping");
    auto r = roundtrip(m);
    CHECK(r.address == "/ping");
    CHECK(r.tags == ",");
    CHECK(r.arguments.empty());
}

TEST_CASE("address padding edge cases")
{
    // OSC strings are null-terminated and padded to 4-byte boundary.
    // Addresses of these lengths exercise every alignment branch.
    for (const char* addr : {"/ab", "/abc", "/abcd", "/abcdef", "/abcdefg"})
    {
        Message m(addr);
        m.add_int32(42);
        auto r = roundtrip(m);
        CHECK(r.address == addr);
        REQUIRE(r.arguments.size() == 1);
        CHECK(std::get<OSCInt>(r.arguments[0]) == 42);
    }
}

TEST_CASE("bundle with one message")
{
    Bundle b;
    b.timetag = 42;
    Message m("/a");
    m.add_int32(7);
    b.add_message(m);

    auto r = roundtrip(b);
    CHECK(r.timetag == 42);
    REQUIRE(r.messages.size() == 1);
    CHECK(r.messages[0].address == "/a");
    CHECK(std::get<OSCInt>(r.messages[0].arguments[0]) == 7);
}

TEST_CASE("bundle with two messages preserves timetag — proves B2 fix")
{
    Bundle b;
    b.timetag = 0x0123456789ABCDEFULL;

    Message m1("/one");
    m1.add_int32(1);
    Message m2("/two");
    m2.add_float(2.0f);
    b.add_message(m1);
    b.add_message(m2);

    auto r = roundtrip(b);
    CHECK(r.timetag == 0x0123456789ABCDEFULL);
    REQUIRE(r.messages.size() == 2);
    CHECK(r.messages[0].address == "/one");
    CHECK(r.messages[1].address == "/two");
    CHECK(std::get<OSCInt>(r.messages[0].arguments[0]) == 1);
    CHECK(std::get<OSCFloat>(r.messages[1].arguments[0]) == 2.0f);
}

TEST_CASE("bundle of bundles")
{
    Bundle inner;
    inner.timetag = 99;
    Message m("/leaf");
    m.add_string("deep");
    inner.add_message(m);

    Bundle outer;
    outer.timetag = 1;
    outer.add_bundle(inner);

    auto r = roundtrip(outer);
    CHECK(r.timetag == 1);
    REQUIRE(r.bundles.size() == 1);
    CHECK(r.bundles[0].timetag == 99);
    REQUIRE(r.bundles[0].messages.size() == 1);
    CHECK(r.bundles[0].messages[0].address == "/leaf");
    CHECK(std::get<OSCString>(r.bundles[0].messages[0].arguments[0]) == "deep");
}
