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
