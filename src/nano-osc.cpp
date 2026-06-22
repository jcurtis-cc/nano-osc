#include "nanoosc/core.hpp"
#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <variant>
#include <type_traits>

namespace NanoOsc {

namespace {

bool tag_matches_argument(char tag, const OSCValue& arg) noexcept
{
    return std::visit(
        [tag](const auto& value) noexcept {
            using T = std::decay_t<decltype(value)>;
            (void)value;
            if constexpr (std::is_same_v<T, OSCInt>)
                return tag == 'i';
            else if constexpr (std::is_same_v<T, OSCFloat>)
                return tag == 'f';
            else if constexpr (std::is_same_v<T, OSCString>)
                return tag == 's' || tag == 'S';
            else if constexpr (std::is_same_v<T, OSCBlob>)
                return tag == 'b';
            else if constexpr (std::is_same_v<T, OSCInt64>)
                return tag == 'h';
            else if constexpr (std::is_same_v<T, OSCFloat64>)
                return tag == 'd';
            else if constexpr (std::is_same_v<T, OSCTimeTag>)
                return tag == 't';
            else if constexpr (std::is_same_v<T, OSCChar>)
                return tag == 'c';
            else if constexpr (std::is_same_v<T, OSCColor>)
                return tag == 'r';
            else if constexpr (std::is_same_v<T, OSCMidi>)
                return tag == 'm';
            else if constexpr (std::is_same_v<T, OSCTrue>)
                return tag == 'T';
            else if constexpr (std::is_same_v<T, OSCFalse>)
                return tag == 'F';
            else if constexpr (std::is_same_v<T, OSCNil>)
                return tag == 'N';
            else if constexpr (std::is_same_v<T, OSCImpulse>)
                return tag == 'I';
            else
                return false;
        },
        arg
    );
}

bool message_tags_match_arguments(const Message& msg) noexcept
{
    if (msg.tags.empty() || msg.tags[0] != ',') return false;
    if (msg.tags.size() != msg.arguments.size() + 1) return false;

    for (size_t i = 0; i < msg.arguments.size(); ++i)
    {
        if (!tag_matches_argument(msg.tags[i + 1], msg.arguments[i])) return false;
    }
    return true;
}

} // namespace

bool validate_message_view(const MessageView& view) noexcept
{
    if (view.tags.empty() || view.tags[0] != ',') return false;

    auto it  = view.begin();
    auto end = view.end();
    for (; it != end; ++it)
    {
        (void)*it;
    }
    return !it.failed();
}

namespace {

bool validate_bundle_view_impl(const BundleView& view, int depth_remaining) noexcept
{
    if (depth_remaining <= 0) return false;

    using namespace detail;
    const uint8_t* payload = view.payload();
    const size_t   size    = view.payload_size();
    size_t         offset  = 0;

    while (offset < size)
    {
        if (!ensure(size, offset, 4)) return false;
        const uint32_t sub_size  = read_u32_be(payload + offset);
        offset                  += 4;
        if (!ensure(size, offset, sub_size)) return false;

        if (is_bundle(payload + offset, sub_size))
        {
            BundleView sub;
            if (!decode_bundle_view(sub, payload + offset, sub_size)) return false;
            if (!validate_bundle_view_impl(sub, depth_remaining - 1)) return false;
        }
        else
        {
            MessageView sub;
            if (!decode_message_view(sub, payload + offset, sub_size)) return false;
            if (!validate_message_view(sub)) return false;
        }

        offset += sub_size;
    }

    return true;
}

} // namespace

bool validate_bundle_view(const BundleView& view) noexcept
{
    return validate_bundle_view_impl(view, MAX_BUNDLE_DEPTH);
}

// Layer 1 — view decoders + iterator advancement

bool decode_message_view(MessageView& out, const uint8_t* data, size_t size) noexcept
{
    using namespace detail;
    size_t           offset = 0;
    std::string_view addr;
    std::string_view tags;
    if (!read_osc_string_view(addr, data, size, offset)) return false;
    if (!read_osc_string_view(tags, data, size, offset)) return false;
    if (tags.empty() || tags[0] != ',') return false;
    out.address = addr;
    out.tags    = tags;
    out.assign_payload(data + offset, size - offset);
    return true;
}

bool decode_bundle_view(BundleView& out, const uint8_t* data, size_t size) noexcept
{
    using namespace detail;
    if (!is_bundle(data, size)) return false;
    size_t   offset = 8; // "#bundle\0"
    uint64_t tt;
    if (!read_osc_timetag(tt, data, size, offset)) return false;
    out.timetag = OSCTimeTag {tt};
    out.assign_payload(data + offset, size - offset);
    return true;
}

void MessageView::arg_iterator::advance() noexcept
{
    if (!mv_ || tag_idx_ >= mv_->tags.size())
    {
        done_ = true;
        return;
    }
    using namespace detail;
    const uint8_t* data = mv_->payload();
    const size_t   cap  = mv_->payload_size();
    const char     tag  = mv_->tags[tag_idx_++];

    switch (tag)
    {
        case 'i': {
            int32_t v;
            if (!read_osc_int32(v, data, cap, payload_off_))
            {
                failed_ = true;
                done_   = true;
                return;
            }
            cur_.emplace<OSCInt>(v);
            break;
        }
        case 'f': {
            float v;
            if (!read_osc_float32(v, data, cap, payload_off_))
            {
                failed_ = true;
                done_   = true;
                return;
            }
            cur_.emplace<OSCFloat>(v);
            break;
        }
        case 'S':
        case 's': {
            std::string_view v;
            if (!read_osc_string_view(v, data, cap, payload_off_))
            {
                failed_ = true;
                done_   = true;
                return;
            }
            cur_.emplace<std::string_view>(v);
            break;
        }
        case 'b': {
            OSCBlobView v;
            if (!read_osc_blob_view(v, data, cap, payload_off_))
            {
                failed_ = true;
                done_   = true;
                return;
            }
            cur_.emplace<OSCBlobView>(v);
            break;
        }
        case 'h': {
            int64_t v;
            if (!read_osc_int64(v, data, cap, payload_off_))
            {
                failed_ = true;
                done_   = true;
                return;
            }
            cur_.emplace<OSCInt64>(v);
            break;
        }
        case 't': {
            uint64_t v;
            if (!read_osc_timetag(v, data, cap, payload_off_))
            {
                failed_ = true;
                done_   = true;
                return;
            }
            cur_.emplace<OSCTimeTag>(v);
            break;
        }
        case 'd': {
            double v;
            if (!read_osc_float64(v, data, cap, payload_off_))
            {
                failed_ = true;
                done_   = true;
                return;
            }
            cur_.emplace<OSCFloat64>(v);
            break;
        }
        case 'c': {
            // ascii char, sent as 32 bit BE
            int32_t v;
            if (!read_osc_int32(v, data, cap, payload_off_))
            {
                failed_ = true;
                done_   = true;
                return;
            }
            cur_.emplace<OSCChar>(OSCChar {static_cast<char>(static_cast<uint32_t>(v) & 0xFFu)});
            break;
        }
        case 'r': {
            // 32 bit RGBA
            if (!ensure(cap, payload_off_, 4))
            {
                failed_ = true;
                done_   = true;
                return;
            }
            cur_.emplace<OSCColor>(
                OSCColor {data[payload_off_], data[payload_off_ + 1], data[payload_off_ + 2], data[payload_off_ + 3]}
            );
            payload_off_ += 4;
            break;
        }
        case 'm': {
            // 4 byte MIDI: port, status, data1, data2
            if (!ensure(cap, payload_off_, 4))
            {
                failed_ = true;
                done_   = true;
                return;
            }
            cur_.emplace<OSCMidi>(
                OSCMidi {data[payload_off_], data[payload_off_ + 1], data[payload_off_ + 2], data[payload_off_ + 3]}
            );
            payload_off_ += 4;
            break;
        }
        case 'T': {
            cur_.emplace<OSCTrue>();
            break;
        }
        case 'F': {
            cur_.emplace<OSCFalse>();
            break;
        }
        case 'N': {
            cur_.emplace<OSCNil>();
            break;
        }
        case 'I': {
            cur_.emplace<OSCImpulse>();
            break;
        }
        default:
            failed_ = true;
            done_   = true;
            return;
    }
}

void BundleView::subpacket_iterator::advance() noexcept
{
    if (!bv_)
    {
        done_ = true;
        return;
    }
    using namespace detail;
    const uint8_t* p  = bv_->payload();
    const size_t   sz = bv_->payload_size();

    if (offset_ >= sz)
    {
        done_ = true;
        return;
    }
    if (!ensure(sz, offset_, 4))
    {
        failed_ = true;
        done_   = true;
        return;
    }
    uint32_t len  = read_u32_be(p + offset_);
    offset_      += 4;
    if (!ensure(sz, offset_, len))
    {
        failed_ = true;
        done_   = true;
        return;
    }

    cur_offset_     = offset_;
    cur_len_        = len;
    cur_is_bundle_  = is_bundle(p + offset_, len);
    offset_        += len;
}

BundleView::SubPacket BundleView::subpacket_iterator::operator*() const noexcept
{
    SubPacket out;
    if (!bv_) return out;
    const uint8_t* sub = bv_->payload() + cur_offset_;
    if (cur_is_bundle_)
    {
        BundleView b;
        (void)decode_bundle_view(b, sub, cur_len_); // failure → empty view; consumer can detect via tags.empty()
        out.emplace<BundleView>(b);
    }
    else
    {
        MessageView m;
        (void)decode_message_view(m, sub, cur_len_);
        out.emplace<MessageView>(m);
    }
    return out;
}

// Layer 2 — Message owning wrapper

bool Message::encode_into(uint8_t* dst, size_t cap, size_t& written) const noexcept
{
    using namespace detail;
    // Spec precondition: address must start with '/', tags must start with ','.
    if (address.empty() || address[0] != '/') return false;
    if (!message_tags_match_arguments(*this)) return false;
    size_t off = 0;
    if (!write_string(dst, cap, off, address)) return false;
    if (!write_string(dst, cap, off, tags)) return false;

    for (const auto& arg : arguments)
    {
        bool ok = std::visit(
            [&](const auto& value) -> bool {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, OSCInt>)
                    return write_int32(dst, cap, off, value);
                else if constexpr (std::is_same_v<T, OSCInt64>)
                    return write_int64(dst, cap, off, value);
                else if constexpr (std::is_same_v<T, OSCFloat>)
                    return write_float32(dst, cap, off, value);
                else if constexpr (std::is_same_v<T, OSCFloat64>)
                    return write_float64(dst, cap, off, value);
                else if constexpr (std::is_same_v<T, OSCString>)
                    return write_string(dst, cap, off, value);
                else if constexpr (std::is_same_v<T, OSCBlob>)
                    return write_blob(dst, cap, off, value.data(), value.size());
                else if constexpr (std::is_same_v<T, OSCTimeTag>)
                    return write_u64(dst, cap, off, value);
                else if constexpr (std::is_same_v<T, OSCChar>)
                    return write_int32(dst, cap, off, static_cast<int32_t>(static_cast<uint8_t>(value.value)));
                else if constexpr (std::is_same_v<T, OSCColor>)
                {
                    if (cap < off || cap - off < 4) return false;
                    dst[off + 0]  = value.r;
                    dst[off + 1]  = value.g;
                    dst[off + 2]  = value.b;
                    dst[off + 3]  = value.a;
                    off          += 4;
                    return true;
                }
                else if constexpr (std::is_same_v<T, OSCMidi>)
                {
                    if (cap < off || cap - off < 4) return false;
                    dst[off + 0]  = value.port;
                    dst[off + 1]  = value.status;
                    dst[off + 2]  = value.data1;
                    dst[off + 3]  = value.data2;
                    off          += 4;
                    return true;
                }
                else if constexpr (
                    std::is_same_v<T, OSCTrue> || std::is_same_v<T, OSCFalse> || std::is_same_v<T, OSCNil> ||
                    std::is_same_v<T, OSCImpulse>
                )
                {
                    (void)value;
                    return true;
                }
                else
                {
                    return false;
                }
            },
            arg
        );
        if (!ok) return false;
    }
    written = off;
    return true;
}

bool Message::assign(const MessageView& v)
{
    // Empty / invalid view (e.g. produced by a failed decode_message_view) is rejected.
    if (v.tags.empty() || v.tags[0] != ',') return false;

    address.assign(v.address);
    tags.assign(v.tags);
    arguments.clear();
    auto it  = v.begin();
    auto end = v.end();
    for (; it != end; ++it)
    {
        std::visit(
            [&](const auto& x) {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, std::string_view>)
                {
                    arguments.emplace_back(OSCString(x));
                }
                else if constexpr (std::is_same_v<T, OSCBlobView>)
                {
                    arguments.emplace_back(OSCBlob(x.data, x.data + x.size));
                }
                else
                {
                    arguments.emplace_back(x);
                }
            },
            *it
        );
    }
    return !it.failed();
}

std::vector<uint8_t> Message::encode() const
{
    std::vector<uint8_t> out;
    size_t               cap = 256;
    while (true)
    {
        out.resize(cap);
        size_t written = 0;
        if (encode_into(out.data(), out.size(), written))
        {
            out.resize(written);
            return out;
        }
        if (cap >= BUFFER_MAX_SIZE)
        {
            out.clear();
            return out;
        }
        cap = std::min<size_t>(cap * 2, BUFFER_MAX_SIZE);
    }
}

Message Message::decode(const uint8_t* data, size_t size)
{
    MessageView view;
    if (!decode_message_view(view, data, size))
    {
        throw std::runtime_error("OSC message decode failed");
    }
    Message m;
    if (!m.assign(view))
    {
        throw std::runtime_error("OSC message decode failed");
    }
    return m;
}

// Layer 2 — Bundle owning wrapper

bool Bundle::encode_into(uint8_t* dst, size_t cap, size_t& written) const noexcept
{
    using namespace detail;
    size_t off = 0;
    if (!write_string(dst, cap, off, std::string_view {BUNDLE_ID.data(), 7})) return false;
    if (!write_u64(dst, cap, off, timetag)) return false;

    auto write_sub = [&](auto& sub) -> bool {
        // Reserve 4 bytes for the length prefix, encode the sub-packet directly
        // into the parent buffer, then backfill the prefix with the actual size.
        if (cap < off || cap - off < 4) return false;
        size_t len_off      = off;
        off                += 4;
        size_t sub_written  = 0;
        if (!sub.encode_into(dst + off, cap - off, sub_written)) return false;
        off              += sub_written;
        size_t throwaway  = len_off;
        return write_u32(dst, cap, throwaway, static_cast<uint32_t>(sub_written));
    };

    for (const auto& msg : messages)
    {
        if (!write_sub(msg)) return false;
    }
    for (const auto& sub : bundles)
    {
        if (!write_sub(sub)) return false;
    }

    written = off;
    return true;
}

bool Bundle::assign(const BundleView& v)
{
    return assign_impl(v, MAX_BUNDLE_DEPTH);
}

bool Bundle::assign_impl(const BundleView& v, int depth_remaining)
{
    if (depth_remaining <= 0) return false;
    timetag = v.timetag;
    messages.clear();
    bundles.clear();
    auto it  = v.begin();
    auto end = v.end();
    for (; it != end; ++it)
    {
        const BundleView::SubPacket sub = *it;
        if (std::holds_alternative<MessageView>(sub))
        {
            Message m;
            if (!m.assign(std::get<MessageView>(sub))) return false;
            messages.emplace_back(std::move(m));
        }
        else
        {
            Bundle b;
            if (!b.assign_impl(std::get<BundleView>(sub), depth_remaining - 1)) return false;
            bundles.emplace_back(std::move(b));
        }
    }
    return !it.failed();
}

std::vector<uint8_t> Bundle::encode() const
{
    std::vector<uint8_t> out;
    size_t               cap = 512;
    while (true)
    {
        out.resize(cap);
        size_t written = 0;
        if (encode_into(out.data(), out.size(), written))
        {
            out.resize(written);
            return out;
        }
        if (cap >= BUFFER_MAX_SIZE)
        {
            out.clear();
            return out;
        }
        cap = std::min<size_t>(cap * 2, BUFFER_MAX_SIZE);
    }
}

Bundle Bundle::decode(const uint8_t* data, size_t size)
{
    BundleView view;
    if (!decode_bundle_view(view, data, size))
    {
        throw std::runtime_error("OSC bundle decode failed");
    }
    Bundle b;
    if (!b.assign(view))
    {
        throw std::runtime_error("OSC bundle decode failed");
    }
    return b;
}

} // namespace NanoOsc
