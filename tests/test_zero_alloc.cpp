#include "doctest.h"
#include "nano-osc.hpp"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

// Global new/delete override: counts every heap allocation across the entire
// linked executable. Other test TUs (and doctest itself) allocate, but each
// test below snapshots the counter before and after a measured scope, so the
// delta isolates the code under test.

namespace {
std::atomic<std::size_t> g_alloc_count {0};
}

void* operator new(std::size_t n)
{
    g_alloc_count.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n)) return p;
    throw std::bad_alloc {};
}
void* operator new[](std::size_t n)
{
    g_alloc_count.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n)) return p;
    throw std::bad_alloc {};
}
void operator delete(void* p) noexcept
{
    std::free(p);
}
void operator delete[](void* p) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}

using namespace NanoOsc;

namespace {

class ReplayTransport : public Transport
{
public:
    ReplayTransport(int count, std::vector<uint8_t> packet) : m_remaining(count), m_packet(std::move(packet)) {}

    bool   send(const uint8_t*, size_t) override { return true; }
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

TEST_CASE("encode_into is zero-alloc")
{
    Message m("/perf");
    m.add_int32(1);
    m.add_float(2.5f);
    m.add_string("hello");
    m.add_true();

    std::vector<uint8_t> buf(BUFFER_MAX_SIZE);
    size_t               written = 0;

    // Warmup (covers any first-touch costs in the underlying lambda machinery)
    (void)m.encode_into(buf.data(), buf.size(), written);

    auto before = g_alloc_count.load(std::memory_order_relaxed);
    bool ok     = m.encode_into(buf.data(), buf.size(), written);
    auto delta  = g_alloc_count.load(std::memory_order_relaxed) - before;
    CHECK(ok);
    CHECK(delta == 0);
}

TEST_CASE("Bundle::encode_into is zero-alloc")
{
    Bundle b;
    b.timetag = 42;
    Message m1("/x");
    m1.add_int32(1);
    b.add_message(m1);
    Message m2("/y");
    m2.add_float(3.14f);
    b.add_message(m2);

    std::vector<uint8_t> buf(BUFFER_MAX_SIZE);
    size_t               written = 0;
    (void)b.encode_into(buf.data(), buf.size(), written); // warmup

    auto before = g_alloc_count.load(std::memory_order_relaxed);
    bool ok     = b.encode_into(buf.data(), buf.size(), written);
    auto delta  = g_alloc_count.load(std::memory_order_relaxed) - before;
    CHECK(ok);
    CHECK(delta == 0);
}

TEST_CASE("decode_message_view + arg iteration is zero-alloc")
{
    Message m("/d");
    m.add_int32(1);
    m.add_string("hello");
    m.add_blob(reinterpret_cast<const uint8_t*>("\x01\x02\x03"), 3);
    auto bytes = m.encode();

    // Warmup
    {
        MessageView v;
        (void)decode_message_view(v, bytes.data(), bytes.size());
        for (auto it = v.begin(); it != v.end(); ++it)
            (void)*it;
    }

    auto before = g_alloc_count.load(std::memory_order_relaxed);
    {
        MessageView v;
        (void)decode_message_view(v, bytes.data(), bytes.size());
        std::size_t sum = 0;
        for (auto it = v.begin(); it != v.end(); ++it)
        {
            // Touch each value to defeat dead-code elimination
            const auto& val = *it;
            if (std::holds_alternative<OSCInt>(val)) sum += static_cast<std::size_t>(std::get<OSCInt>(val));
        }
        CHECK(sum == 1);
    }
    auto delta = g_alloc_count.load(std::memory_order_relaxed) - before;
    CHECK(delta == 0);
}

TEST_CASE("OSCClient::send_message is zero-alloc after construction")
{
    Message m("/send");
    m.add_int32(7);
    m.add_string("hot path");

    auto      transport = std::make_unique<ReplayTransport>(0, std::vector<uint8_t> {});
    OSCClient client(std::move(transport));

    client.send_message(m); // warmup

    auto before = g_alloc_count.load(std::memory_order_relaxed);
    client.send_message(m);
    auto delta = g_alloc_count.load(std::memory_order_relaxed) - before;
    CHECK(delta == 0);
}

TEST_CASE("OSCServer::process_one with owning handler is zero-alloc after warmup")
{
    Message m("/srv");
    m.add_int32(1);
    m.add_string("steady"); // exercise the string-arg path
    auto packet = m.encode();

    auto      transport = std::make_unique<ReplayTransport>(10, packet);
    OSCServer server(std::move(transport));
    int       handled = 0;
    server.set_message_handler([&](const Message&) { ++handled; });

    // Warmup: process a couple of packets so internal m_msg.address/tags/arguments
    // and per-arg storage have grown to high-water mark.
    server.process_one();
    server.process_one();

    auto before = g_alloc_count.load(std::memory_order_relaxed);
    bool ok     = server.process_one();
    auto delta  = g_alloc_count.load(std::memory_order_relaxed) - before;
    CHECK(ok);
    CHECK(handled == 3);
    CHECK(delta == 0);
}

TEST_CASE("OSCServer::process_one with view handler is zero-alloc")
{
    Message m("/srv");
    m.add_int32(1);
    auto packet = m.encode();

    auto      transport = std::make_unique<ReplayTransport>(10, packet);
    OSCServer server(std::move(transport));
    int       seen = 0;
    server.set_message_view_handler([&](const MessageView&) { ++seen; });

    server.process_one(); // warmup (should already be alloc-free)

    auto before = g_alloc_count.load(std::memory_order_relaxed);
    server.process_one();
    auto delta = g_alloc_count.load(std::memory_order_relaxed) - before;
    CHECK(delta == 0);
    CHECK(seen == 2);
}
