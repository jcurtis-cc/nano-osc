#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "nano-osc.hpp"

TEST_CASE("harness wiring: encode/decode trivial message")
{
    NanoOsc::Message msg("/ping");
    auto             bytes = msg.encode();
    REQUIRE(bytes.size() > 0);

    auto decoded = NanoOsc::Message::decode(bytes.data(), bytes.size());
    CHECK(decoded.address == "/ping");
    CHECK(decoded.tags == ",");
    CHECK(decoded.arguments.empty());
}
