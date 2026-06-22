#include "doctest.h"
#include "nano-osc.hpp"

#include <cstring>
#include <memory>
#include <vector>

using namespace NanoOsc;

namespace {

// Hands out the same packet up to `count` times, then reports empty.
class ReplayTransport : public Transport
{
public:
    ReplayTransport(int count, std::vector<uint8_t> packet) : m_remaining(count), m_packet(std::move(packet)) {}

    bool send(const uint8_t*, size_t) override { return true; }

    size_t receive(uint8_t* buf, size_t buf_size) override
    {
        if (m_remaining <= 0) return 0;
        size_t n = m_packet.size();
        if (n > buf_size) n = buf_size;
        std::memcpy(buf, m_packet.data(), n);
        --m_remaining;
        return n;
    }

    bool is_ready() const override { return true; }
    void close() override {}

private:
    int                  m_remaining;
    std::vector<uint8_t> m_packet;
};

} // namespace

TEST_CASE("process_all caps at max")
{
    Message m("/x");
    m.add_int32(1);

    auto      packet = m.encode();
    auto      t      = std::make_unique<ReplayTransport>(10, packet);
    OSCServer server(std::move(t));

    int handled = 0;
    server.set_message_handler([&](const Message&) { ++handled; });

    int processed = server.process_all(3);
    CHECK(processed == 3);
    CHECK(handled == 3);
}

TEST_CASE("process_all drains all when max < 0")
{
    Message m("/x");
    m.add_int32(1);

    auto      packet = m.encode();
    auto      t      = std::make_unique<ReplayTransport>(5, packet);
    OSCServer server(std::move(t));

    int handled = 0;
    server.set_message_handler([&](const Message&) { ++handled; });

    int processed = server.process_all();
    CHECK(processed == 5);
    CHECK(handled == 5);
}
