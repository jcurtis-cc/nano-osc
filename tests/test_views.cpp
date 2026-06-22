#include "doctest.h"
#include "nano-osc.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using namespace NanoOsc;

namespace {

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

TEST_CASE("MessageView fields are views into source buffer")
{
    Message m("/abc");
    m.add_int32(7);
    m.add_string("hi");
    auto bytes = m.encode();

    MessageView v;
    REQUIRE(decode_message_view(v, bytes.data(), bytes.size()));
    CHECK(v.address == "/abc");
    CHECK(v.tags == ",is");
    // address.data() points inside the source buffer, not a freshly allocated copy
    CHECK(reinterpret_cast<const uint8_t*>(v.address.data()) >= bytes.data());
    CHECK(reinterpret_cast<const uint8_t*>(v.address.data()) < bytes.data() + bytes.size());
}

TEST_CASE("MessageView lazy iteration yields each arg")
{
    Message m("/mix");
    m.add_int32(42);
    m.add_float(2.5f);
    m.add_string("hi");
    m.add_blob(reinterpret_cast<const uint8_t*>("\x01\x02\x03"), 3);
    m.add_char('Q');
    m.add_true();
    m.add_color(1, 2, 3, 4);
    m.add_nil();
    m.add_midi(0, 0x90, 0x40, 0x7F);
    m.add_false();
    m.add_impulse();
    auto bytes = m.encode();

    MessageView v;
    REQUIRE(decode_message_view(v, bytes.data(), bytes.size()));

    std::vector<OSCValueView> collected;
    for (auto it = v.begin(); it != v.end(); ++it)
        collected.push_back(*it);

    REQUIRE(collected.size() == 11);
    CHECK(std::get<OSCInt>(collected[0]) == 42);
    CHECK(std::get<OSCFloat>(collected[1]) == 2.5f);
    CHECK(std::get<std::string_view>(collected[2]) == "hi");
    const auto& blob = std::get<OSCBlobView>(collected[3]);
    CHECK(blob.size == 3);
    CHECK(blob.data[0] == 0x01);
    CHECK(std::get<OSCChar>(collected[4]).value == 'Q');
    CHECK(std::holds_alternative<OSCTrue>(collected[5]));
    CHECK(std::get<OSCColor>(collected[6]).b == 3);
    CHECK(std::holds_alternative<OSCNil>(collected[7]));
    CHECK(std::get<OSCMidi>(collected[8]).status == 0x90);
    CHECK(std::holds_alternative<OSCFalse>(collected[9]));
    CHECK(std::holds_alternative<OSCImpulse>(collected[10]));
}

TEST_CASE("MessageView iteration over malformed args sets failed()")
{
    // Valid addr+tags claim an int32 arg but no payload bytes follow.
    std::vector<uint8_t> buf = {'/', 'x', 0, 0, ',', 'i', 0, 0};
    MessageView          v;
    REQUIRE(decode_message_view(v, buf.data(), buf.size()));

    auto it = v.begin();
    CHECK(it == v.end()); // first ++ failed → done
    CHECK(it.failed());   // … because of malformed payload, not natural end
}

TEST_CASE("BundleView lazy iteration over sub-packets")
{
    Bundle outer;
    outer.timetag = 0xCAFE;
    Message m1("/a");
    m1.add_int32(1);
    outer.add_message(m1);
    Message m2("/b");
    m2.add_string("two");
    outer.add_message(m2);
    Bundle inner;
    inner.timetag = 0xBEEF;
    Message m3("/c");
    m3.add_true();
    inner.add_message(m3);
    outer.add_bundle(inner);

    auto       bytes = outer.encode();
    BundleView bv;
    REQUIRE(decode_bundle_view(bv, bytes.data(), bytes.size()));
    CHECK(bv.timetag == 0xCAFE);

    int msgs = 0, buns = 0;
    for (auto it = bv.begin(); it != bv.end(); ++it)
    {
        const auto sub = *it;
        if (std::holds_alternative<MessageView>(sub))
            ++msgs;
        else
            ++buns;
    }
    CHECK(msgs == 2);
    CHECK(buns == 1);
}

TEST_CASE("OSCServer view handler takes precedence over owning handler")
{
    Message m("/v");
    m.add_int32(99);
    auto      packet = m.encode();
    auto      t      = std::make_unique<ReplayTransport>(1, packet);
    OSCServer server(std::move(t));

    int owning_calls = 0;
    int view_calls   = 0;
    server.set_message_handler([&](const Message&) { ++owning_calls; });
    server.set_message_view_handler([&](const MessageView& v) {
        ++view_calls;
        CHECK(v.address == "/v");
        auto it = v.begin();
        REQUIRE(it != v.end());
        CHECK(std::get<OSCInt>(*it) == 99);
    });

    CHECK(server.process_one());
    CHECK(view_calls == 1);
    CHECK(owning_calls == 0);
}

TEST_CASE("OSCServer error handler fires before malformed message view handler")
{
    std::vector<uint8_t> bad = {'/', 'x', 0, 0, ',', 'i', 0, 0};
    auto                 t   = std::make_unique<ReplayTransport>(1, bad);
    OSCServer            server(std::move(t));

    int handled = 0;
    int errored = 0;
    server.set_message_view_handler([&](const MessageView&) { ++handled; });
    server.set_error_handler([&](const uint8_t*, size_t) { ++errored; });

    CHECK_FALSE(server.process_one());
    CHECK(handled == 0);
    CHECK(errored == 1);
}

TEST_CASE("OSCServer error handler fires before malformed bundle view handler")
{
    std::vector<uint8_t> bad = {
        '#', 'b', 'u', 'n', 'd', 'l', 'e', 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 8, '/', 'x', 0, 0, ',', 'i', 0, 0,
    };
    auto      t = std::make_unique<ReplayTransport>(1, bad);
    OSCServer server(std::move(t));

    int handled = 0;
    int errored = 0;
    server.set_bundle_view_handler([&](const BundleView&) { ++handled; });
    server.set_error_handler([&](const uint8_t*, size_t) { ++errored; });

    CHECK_FALSE(server.process_one());
    CHECK(handled == 0);
    CHECK(errored == 1);
}

TEST_CASE("OSCServer error handler fires on malformed packet, owning handler does not")
{
    // Valid addr+tags but no payload for declared int arg → assign() fails.
    std::vector<uint8_t> bad = {'/', 'x', 0, 0, ',', 'i', 0, 0};
    auto                 t   = std::make_unique<ReplayTransport>(1, bad);
    OSCServer            server(std::move(t));

    int handled = 0;
    int errored = 0;
    server.set_message_handler([&](const Message&) { ++handled; });
    server.set_error_handler([&](const uint8_t*, size_t) { ++errored; });

    CHECK_FALSE(server.process_one());
    CHECK(handled == 0);
    CHECK(errored == 1);
}

TEST_CASE("OSCServer process_one is noexcept-equivalent: malformed never throws")
{
    std::vector<uint8_t> bad = {0xFF, 0xFF, 0xFF, 0xFF}; // bogus
    auto                 t   = std::make_unique<ReplayTransport>(1, bad);
    OSCServer            server(std::move(t));
    server.set_message_handler([&](const Message&) {});

    bool threw = false;
    try
    {
        server.process_one();
    }
    catch (...)
    {
        threw = true;
    }
    CHECK_FALSE(threw);
}
