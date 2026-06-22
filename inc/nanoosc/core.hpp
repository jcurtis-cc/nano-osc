#ifndef NANOOSC_CORE_HPP
#define NANOOSC_CORE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <variant>
#include <string>
#include <string_view>
#include <vector>

namespace NanoOsc {

constexpr size_t              BUFFER_MAX_SIZE  = 65536;
// Max nested-bundle depth honoured by Bundle::assign / Bundle::decode. Caps
// stack use against adversarial input; a real UDP packet of 64 KB cannot legally
// reach this depth (each bundle frame is at least 20 bytes).
constexpr int                 MAX_BUNDLE_DEPTH = 64;
constexpr std::array<char, 8> BUNDLE_ID        = {'#', 'b', 'u', 'n', 'd', 'l', 'e', 0};

using OSCInt     = int32_t;
using OSCInt64   = int64_t;
using OSCTimeTag = uint64_t;
using OSCFloat   = float;
using OSCFloat64 = double;
using OSCString  = std::string;
using OSCBlob    = std::vector<uint8_t>;

struct OSCChar
{
    char value;
};
struct OSCColor
{
    uint8_t r, g, b, a;
};
struct OSCMidi
{
    uint8_t port, status, data1, data2;
};

// T/F/N/I are OSC arguments even though they encode no payload bytes. They stay
// as marker values so Message::arguments represents OSC-level arguments.
struct OSCTrue
{
};
struct OSCFalse
{
};
struct OSCNil
{
};
struct OSCImpulse
{
};

using OSCValue = std::variant<
    OSCInt,
    OSCInt64,
    OSCFloat,
    OSCFloat64,
    OSCString,
    OSCBlob,
    OSCTimeTag,
    OSCChar,
    OSCColor,
    OSCMidi,
    OSCTrue,
    OSCFalse,
    OSCNil,
    OSCImpulse>;

struct OSCBlobView
{
    const uint8_t* data;
    size_t         size;
};

using OSCValueView = std::variant<
    OSCInt,
    OSCInt64,
    OSCFloat,
    OSCFloat64,
    std::string_view,
    OSCBlobView,
    OSCTimeTag,
    OSCChar,
    OSCColor,
    OSCMidi,
    OSCTrue,
    OSCFalse,
    OSCNil,
    OSCImpulse>;

class MessageView
{
public:
    std::string_view address;
    std::string_view tags;

    MessageView() noexcept : payload_(nullptr), payload_size_(0) {}

    class arg_iterator
    {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type        = OSCValueView;
        using difference_type   = std::ptrdiff_t;
        using pointer           = const OSCValueView*;
        using reference         = const OSCValueView&;

        arg_iterator() noexcept : mv_(nullptr), tag_idx_(0), payload_off_(0), done_(true) {}
        explicit arg_iterator(const MessageView& mv) noexcept : mv_(&mv), tag_idx_(1), payload_off_(0), done_(false)
        {
            advance();
        }

        const OSCValueView& operator*() const noexcept { return cur_; }
        const OSCValueView* operator->() const noexcept { return &cur_; }
        arg_iterator&       operator++() noexcept
        {
            advance();
            return *this;
        }
        bool operator==(const arg_iterator& o) const noexcept { return done_ && o.done_; }
        bool operator!=(const arg_iterator& o) const noexcept { return !(*this == o); }

        [[nodiscard]] bool failed() const noexcept { return failed_; }

    private:
        void advance() noexcept;

        const MessageView* mv_;
        size_t             tag_idx_;
        size_t             payload_off_;
        OSCValueView       cur_;
        bool               done_;
        bool               failed_ = false;
    };

    arg_iterator begin() const noexcept { return arg_iterator(*this); }
    arg_iterator end() const noexcept { return arg_iterator(); }

    const uint8_t* payload() const noexcept { return payload_; }
    size_t         payload_size() const noexcept { return payload_size_; }

    void assign_payload(const uint8_t* p, size_t s) noexcept
    {
        payload_      = p;
        payload_size_ = s;
    }

private:
    const uint8_t* payload_;
    size_t         payload_size_;
};

class BundleView
{
public:
    OSCTimeTag timetag {0};

    BundleView() noexcept : payload_(nullptr), payload_size_(0) {}

    using SubPacket = std::variant<MessageView, BundleView>;

    class subpacket_iterator
    {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type        = SubPacket;
        using difference_type   = std::ptrdiff_t;
        using pointer           = void;
        using reference         = SubPacket;

        subpacket_iterator() noexcept = default;
        explicit subpacket_iterator(const BundleView& bv) noexcept : bv_(&bv), done_(false) { advance(); }

        SubPacket           operator*() const noexcept;
        subpacket_iterator& operator++() noexcept
        {
            advance();
            return *this;
        }
        bool operator==(const subpacket_iterator& o) const noexcept { return done_ && o.done_; }
        bool operator!=(const subpacket_iterator& o) const noexcept { return !(*this == o); }

        [[nodiscard]] bool failed() const noexcept { return failed_; }

    private:
        void advance() noexcept;

        const BundleView* bv_            = nullptr;
        size_t            offset_        = 0;
        size_t            cur_offset_    = 0;
        uint32_t          cur_len_       = 0;
        bool              cur_is_bundle_ = false;
        bool              done_          = true;
        bool              failed_        = false;
    };

    subpacket_iterator begin() const noexcept { return subpacket_iterator(*this); }
    subpacket_iterator end() const noexcept { return subpacket_iterator(); }

    const uint8_t* payload() const noexcept { return payload_; }
    size_t         payload_size() const noexcept { return payload_size_; }

    void assign_payload(const uint8_t* p, size_t s) noexcept
    {
        payload_      = p;
        payload_size_ = s;
    }

private:
    const uint8_t* payload_;
    size_t         payload_size_;
};

[[nodiscard]] bool decode_message_view(MessageView& out, const uint8_t* data, size_t size) noexcept;
[[nodiscard]] bool decode_bundle_view(BundleView& out, const uint8_t* data, size_t size) noexcept;
[[nodiscard]] bool validate_message_view(const MessageView& view) noexcept;
[[nodiscard]] bool validate_bundle_view(const BundleView& view) noexcept;

class Message
{
public:
    std::string           address;
    std::string           tags;
    std::vector<OSCValue> arguments;

    Message() { tags.push_back(','); }
    explicit Message(const std::string& addr) : address(addr) { tags.push_back(','); }
    explicit Message(const MessageView& v) { (void)assign(v); }

    void clear()
    {
        tags.assign(1, ',');
        arguments.clear();
    }
    void add_int32(int32_t value)
    {
        tags.push_back('i');
        arguments.emplace_back(value);
    }
    void add_float(float value)
    {
        tags.push_back('f');
        arguments.emplace_back(value);
    }
    void add_string(const std::string& value)
    {
        tags.push_back('s');
        arguments.emplace_back(value);
    }
    void add_blob(const uint8_t* data, size_t size)
    {
        tags.push_back('b');
        arguments.emplace_back(OSCBlob(data, data + size));
    }
    void add_int64(int64_t value)
    {
        tags.push_back('h');
        arguments.emplace_back(OSCInt64 {value});
    }
    void add_double(double value)
    {
        tags.push_back('d');
        arguments.emplace_back(OSCFloat64 {value});
    }
    void add_symbol(const std::string& value)
    {
        tags.push_back('S');
        arguments.emplace_back(OSCString(value));
    }
    void add_timetag(OSCTimeTag value)
    {
        tags.push_back('t');
        arguments.emplace_back(value);
    }
    void add_char(char c)
    {
        tags.push_back('c');
        arguments.emplace_back(OSCChar {c});
    }
    void add_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        tags.push_back('r');
        arguments.emplace_back(OSCColor {r, g, b, a});
    }
    void add_midi(uint8_t port, uint8_t status, uint8_t data1, uint8_t data2)
    {
        tags.push_back('m');
        arguments.emplace_back(OSCMidi {port, status, data1, data2});
    }
    void add_true()
    {
        tags.push_back('T');
        arguments.emplace_back(OSCTrue {});
    }
    void add_false()
    {
        tags.push_back('F');
        arguments.emplace_back(OSCFalse {});
    }
    void add_nil()
    {
        tags.push_back('N');
        arguments.emplace_back(OSCNil {});
    }
    void add_impulse()
    {
        tags.push_back('I');
        arguments.emplace_back(OSCImpulse {});
    }

    [[nodiscard]] bool encode_into(uint8_t* dst, size_t cap, size_t& written) const noexcept;
    [[nodiscard]] bool assign(const MessageView& v);

    std::vector<uint8_t> encode() const;
    static Message       decode(const uint8_t* data, size_t size);
};

class Bundle
{
public:
    std::vector<Message> messages;
    std::vector<Bundle>  bundles;
    OSCTimeTag           timetag {1};

    Bundle() = default;
    explicit Bundle(const BundleView& v) { (void)assign(v); }

    void clear()
    {
        messages.clear();
        bundles.clear();
        timetag = 1;
    }

    void add_message(const Message& msg) { messages.emplace_back(msg); }

    void add_bundle(const Bundle& bundle) { bundles.emplace_back(bundle); }

    [[nodiscard]] bool encode_into(uint8_t* dst, size_t cap, size_t& written) const noexcept;
    [[nodiscard]] bool assign(const BundleView& v);

    std::vector<uint8_t> encode() const;
    static Bundle        decode(const uint8_t* data, size_t size);

private:
    [[nodiscard]] bool assign_impl(const BundleView& v, int depth_remaining);
};

namespace detail {

inline size_t align4(size_t n) noexcept
{
    return (4 - (n & 3)) & 3;
}

inline bool write_u32(uint8_t* dst, size_t cap, size_t& off, uint32_t v) noexcept
{
    if (cap < off || cap - off < 4) return false;
    dst[off + 0]  = static_cast<uint8_t>(v >> 24);
    dst[off + 1]  = static_cast<uint8_t>(v >> 16);
    dst[off + 2]  = static_cast<uint8_t>(v >> 8);
    dst[off + 3]  = static_cast<uint8_t>(v);
    off          += 4;
    return true;
}

inline bool write_u64(uint8_t* dst, size_t cap, size_t& off, uint64_t v) noexcept
{
    if (cap < off || cap - off < 8) return false;
    dst[off + 0]  = static_cast<uint8_t>(v >> 56);
    dst[off + 1]  = static_cast<uint8_t>(v >> 48);
    dst[off + 2]  = static_cast<uint8_t>(v >> 40);
    dst[off + 3]  = static_cast<uint8_t>(v >> 32);
    dst[off + 4]  = static_cast<uint8_t>(v >> 24);
    dst[off + 5]  = static_cast<uint8_t>(v >> 16);
    dst[off + 6]  = static_cast<uint8_t>(v >> 8);
    dst[off + 7]  = static_cast<uint8_t>(v);
    off          += 8;
    return true;
}

inline bool write_int32(uint8_t* dst, size_t cap, size_t& off, int32_t v) noexcept
{
    return write_u32(dst, cap, off, static_cast<uint32_t>(v));
}

inline bool write_int64(uint8_t* dst, size_t cap, size_t& off, int64_t v) noexcept
{
    return write_u64(dst, cap, off, static_cast<uint64_t>(v));
}

inline bool write_float32(uint8_t* dst, size_t cap, size_t& off, float f) noexcept
{
    uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof bits);
    return write_u32(dst, cap, off, bits);
}

inline bool write_float64(uint8_t* dst, size_t cap, size_t& off, double d) noexcept
{
    uint64_t bits = 0;
    std::memcpy(&bits, &d, sizeof bits);
    return write_u64(dst, cap, off, bits);
}

inline bool write_string(uint8_t* dst, size_t cap, size_t& off, std::string_view s) noexcept
{
    size_t pad  = align4(s.size() + 1);
    size_t need = s.size() + 1 + pad;
    if (cap < off || cap - off < need) return false;
    std::memcpy(dst + off, s.data(), s.size());
    off        += s.size();
    dst[off++]  = 0;
    for (size_t i = 0; i < pad; ++i)
        dst[off++] = 0;
    return true;
}

inline bool write_blob(uint8_t* dst, size_t cap, size_t& off, const uint8_t* data, size_t size) noexcept
{
    if (size > UINT32_MAX) return false;
    if (!write_u32(dst, cap, off, static_cast<uint32_t>(size))) return false;
    size_t pad = align4(size);
    if (cap < off || cap - off < size + pad) return false;
    if (size > 0) std::memcpy(dst + off, data, size);
    off += size;
    for (size_t i = 0; i < pad; ++i)
        dst[off++] = 0;
    return true;
}

inline bool write_bytes(uint8_t* dst, size_t cap, size_t& off, const uint8_t* src, size_t n) noexcept
{
    if (cap < off || cap - off < n) return false;
    if (n > 0) std::memcpy(dst + off, src, n);
    off += n;
    return true;
}

inline uint32_t read_u32_be(const uint8_t* p) noexcept
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

inline uint64_t read_u64_be(const uint8_t* p) noexcept
{
    return (uint64_t(p[0]) << 56) | (uint64_t(p[1]) << 48) | (uint64_t(p[2]) << 40) | (uint64_t(p[3]) << 32) |
           (uint64_t(p[4]) << 24) | (uint64_t(p[5]) << 16) | (uint64_t(p[6]) << 8) | (uint64_t(p[7]));
}

inline bool ensure(size_t have, size_t offset, size_t need) noexcept
{
    return offset <= have && have - offset >= need;
}

inline bool is_bundle(const uint8_t* p, size_t size) noexcept
{
    return size >= 8 && std::memcmp(p, BUNDLE_ID.data(), 8) == 0;
}

inline bool read_osc_int32(int32_t& out, const uint8_t* data, size_t size, size_t& offset) noexcept
{
    if (!ensure(size, offset, 4)) return false;
    out     = static_cast<int32_t>(read_u32_be(data + offset));
    offset += 4;
    return true;
}

inline bool read_osc_int64(int64_t& out, const uint8_t* data, size_t size, size_t& offset) noexcept
{
    if (!ensure(size, offset, 8)) return false;
    out     = static_cast<int64_t>(read_u64_be(data + offset));
    offset += 8;
    return true;
}

inline bool read_osc_float32(float& out, const uint8_t* data, size_t size, size_t& offset) noexcept
{
    if (!ensure(size, offset, 4)) return false;
    uint32_t bits = read_u32_be(data + offset);
    std::memcpy(&out, &bits, sizeof(out));
    offset += 4;
    return true;
}

inline bool read_osc_float64(double& out, const uint8_t* data, size_t size, size_t& offset) noexcept
{
    if (!ensure(size, offset, 8)) return false;
    uint64_t bits = read_u64_be(data + offset);
    std::memcpy(&out, &bits, sizeof(out));
    offset += 8;
    return true;
}

inline bool read_osc_timetag(uint64_t& out, const uint8_t* data, size_t size, size_t& offset) noexcept
{
    if (!ensure(size, offset, 8)) return false;
    out     = read_u64_be(data + offset);
    offset += 8;
    return true;
}

inline bool read_osc_string_view(std::string_view& out, const uint8_t* data, size_t size, size_t& offset) noexcept
{
    size_t start = offset;
    while (offset < size && data[offset] != 0x00)
        ++offset;
    if (offset >= size) return false;
    out    = std::string_view(reinterpret_cast<const char*>(data + start), offset - start);
    offset = (offset + 4) & ~size_t {0x3};
    if (offset > size) return false;
    return true;
}

inline bool read_osc_blob_view(OSCBlobView& out, const uint8_t* data, size_t size, size_t& offset) noexcept
{
    if (!ensure(size, offset, 4)) return false;
    uint32_t len  = read_u32_be(data + offset);
    offset       += 4;
    if (!ensure(size, offset, len)) return false;
    out     = OSCBlobView {data + offset, len};
    offset += len;
    offset  = (offset + 3) & ~size_t {0x3};
    if (offset > size) return false;
    return true;
}

} // namespace detail

} // namespace NanoOsc

#endif // NANOOSC_CORE_HPP
