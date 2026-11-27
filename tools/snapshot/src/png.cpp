#include <tessera/snapshot/snapshot_bundle.hpp>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tessera {
namespace {

std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> result{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            result[n] = c;
        }
        return result;
    }();
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}

std::uint32_t adler32(const std::vector<std::uint8_t>& data) {
    std::uint32_t a = 1, b = 0;
    for (std::size_t i = 0; i < data.size();) {
        // 5552 is the largest block that cannot overflow 32-bit sums before the modulo.
        const std::size_t end = std::min(data.size(), i + 5552);
        for (; i < end; ++i) {
            a += data[i];
            b += a;
        }
        a %= 65521;
        b %= 65521;
    }
    return (b << 16) | a;
}

// LSB-first deflate bit stream.
class Bits {
public:
    std::vector<std::uint8_t> bytes;
    void put(std::uint32_t value, unsigned count) {
        for (unsigned i = 0; i < count; ++i) {
            if (!used_) bytes.push_back(0);
            bytes.back() |= std::uint8_t(((value >> i) & 1) << used_);
            used_ = (used_ + 1) & 7;
        }
    }
    // Huffman codes are packed starting with their most significant bit.
    void code(std::uint32_t value, unsigned count) {
        std::uint32_t reversed = 0;
        for (unsigned i = 0; i < count; ++i) reversed |= ((value >> i) & 1) << (count - 1 - i);
        put(reversed, count);
    }

private:
    unsigned used_ = 0;
};

void literal_length(Bits& out, unsigned symbol) {
    if (symbol < 144)
        out.code(0x30 + symbol, 8);
    else if (symbol < 256)
        out.code(0x190 + symbol - 144, 9);
    else if (symbol < 280)
        out.code(symbol - 256, 7);
    else
        out.code(0xc0 + symbol - 280, 8);
}

constexpr std::array<std::uint16_t, 29> length_base{3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                                    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<std::uint8_t, 29> length_extra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<std::uint16_t, 30> distance_base{1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                                      33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                                      1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<std::uint8_t, 30> distance_extra{0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                      6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void match(Bits& out, unsigned length, unsigned distance) {
    unsigned l = 28;
    while (length_base[l] > length) --l;
    literal_length(out, 257 + l);
    out.put(length - length_base[l], length_extra[l]);
    unsigned d = 29;
    while (distance_base[d] > distance) --d;
    out.code(d, 5);
    out.put(distance - distance_base[d], distance_extra[d]);
}

// One final fixed-Huffman block with a bounded greedy hash-chain matcher. Deterministic for equal input.
std::vector<std::uint8_t> deflate(const std::vector<std::uint8_t>& data) {
    constexpr std::size_t window = 32768, max_length = 258, max_chain = 32;
    constexpr std::size_t hash_size = 1 << 15;
    constexpr std::uint32_t none = 0xffffffffu;
    std::vector<std::uint32_t> head(hash_size, none), previous(window, none);
    const auto hash = [&](std::size_t i) {
        return ((std::uint32_t(data[i]) << 10) ^ (std::uint32_t(data[i + 1]) << 5) ^ data[i + 2]) & (hash_size - 1);
    };
    const auto insert = [&](std::size_t i) {
        if (i + 2 >= data.size()) return;
        const auto h = hash(i);
        previous[i % window] = head[h];
        head[h] = std::uint32_t(i);
    };
    Bits out;
    out.put(1, 1); // BFINAL
    out.put(1, 2); // BTYPE = fixed Huffman
    std::size_t i = 0;
    while (i < data.size()) {
        std::size_t best = 0, distance = 0;
        if (i + 2 < data.size()) {
            const std::size_t limit = std::min(max_length, data.size() - i);
            std::uint32_t candidate = head[hash(i)];
            for (std::size_t chain = 0; candidate != none && chain < max_chain; ++chain) {
                if (i - candidate > window - 1) break;
                std::size_t length = 0;
                while (length < limit && data[candidate + length] == data[i + length]) ++length;
                if (length > best) {
                    best = length;
                    distance = i - candidate;
                    if (length == limit) break;
                }
                const auto next = previous[candidate % window];
                if (next == none || next >= candidate) break;
                candidate = next;
            }
        }
        if (best >= 3) {
            match(out, unsigned(best), unsigned(distance));
            for (std::size_t k = 0; k < best; ++k) insert(i + k);
            i += best;
        } else {
            literal_length(out, data[i]);
            insert(i);
            ++i;
        }
    }
    literal_length(out, 256);
    return std::move(out.bytes);
}

void be32(std::string& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(char((value >> shift) & 0xff));
}

void chunk(std::string& out, const char (&type)[5], const std::vector<std::uint8_t>& data) {
    be32(out, std::uint32_t(data.size()));
    const auto begin = out.size();
    out.append(type, 4);
    out.append(reinterpret_cast<const char*>(data.data()), data.size());
    be32(out, crc32(reinterpret_cast<const std::uint8_t*>(out.data() + begin), out.size() - begin));
}

} // namespace

Result<std::string> encode_png(const CapturedImage& image) {
    const auto width = image.extent.width, height = image.extent.height;
    const std::size_t row = std::size_t(width) * 4;
    if (!width || !height || width > 0x7fffffffu || height > 0x7fffffffu || image.pixels.size() != row * height)
        return {std::nullopt,
                {{"capture_mismatch", Severity::error, "/pixels",
                  "Supply width * height * 4 RGBA8 bytes for a nonempty extent."}}};
    std::vector<std::uint8_t> raw;
    raw.reserve((row + 1) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        raw.push_back(0); // Filter type None keeps the encoding a function of the pixels alone.
        raw.insert(raw.end(), image.pixels.begin() + std::ptrdiff_t(y * row),
                   image.pixels.begin() + std::ptrdiff_t((y + 1) * row));
    }
    std::vector<std::uint8_t> zlib{0x78, 0x01};
    const auto compressed = deflate(raw);
    zlib.insert(zlib.end(), compressed.begin(), compressed.end());
    const auto check = adler32(raw);
    for (int shift = 24; shift >= 0; shift -= 8) zlib.push_back(std::uint8_t((check >> shift) & 0xff));

    std::string png("\x89PNG\r\n\x1a\n", 8);
    std::vector<std::uint8_t> header;
    for (const auto value : {width, height})
        for (int shift = 24; shift >= 0; shift -= 8) header.push_back(std::uint8_t((value >> shift) & 0xff));
    header.insert(header.end(), {8, 6, 0, 0, 0}); // 8-bit RGBA, deflate, adaptive filtering, no interlace.
    chunk(png, "IHDR", header);
    chunk(png, "sRGB", {0}); // Captured bytes are sRGB-encoded; perceptual rendering intent.
    chunk(png, "IDAT", zlib);
    chunk(png, "IEND", {});
    return {std::move(png), {}};
}

namespace {

// Canonical Huffman table in the counts/symbols form of RFC 1951 section 3.2.2.
struct Huffman {
    std::array<std::uint16_t, 16> count{};
    std::vector<std::uint16_t> symbol;
};

// Returns 0 for a complete code, a positive value for an incomplete one and -1 when oversubscribed.
int build(Huffman& h, const std::uint8_t* lengths, std::size_t n) {
    h.count.fill(0);
    for (std::size_t i = 0; i < n; ++i) ++h.count[lengths[i]];
    if (h.count[0] == n) return 1; // No codes at all.
    int left = 1;
    for (std::size_t len = 1; len < 16; ++len) {
        left = left * 2 - h.count[len];
        if (left < 0) return -1;
    }
    std::array<std::uint16_t, 16> offset{};
    for (std::size_t len = 1; len < 15; ++len) offset[len + 1] = std::uint16_t(offset[len] + h.count[len]);
    h.symbol.assign(n, 0);
    for (std::size_t i = 0; i < n; ++i)
        if (lengths[i]) h.symbol[offset[lengths[i]]++] = std::uint16_t(i);
    return left;
}

class Inflater {
public:
    Inflater(std::string_view data, std::size_t size) : data_(data), size_(size) {}

    // Inflates a zlib stream that must yield exactly `size` bytes.
    std::optional<std::string> run(std::string& error) {
        if (data_.size() < 6) return fail(error, "zlib stream is too short.");
        const unsigned cmf = static_cast<unsigned char>(data_[0]), flags = static_cast<unsigned char>(data_[1]);
        if ((cmf & 0x0f) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flags) % 31 || (flags & 0x20))
            return fail(error, "Invalid zlib header.");
        at_ = 2;
        for (bool last = false; !last;) {
            last = bits(1) != 0;
            const auto type = bits(2);
            const bool ok = type == 0   ? stored()
                            : type == 1 ? fixed()
                            : type == 2 ? dynamic()
                                        : stop("Reserved block type.");
            if (!ok || broken_) return fail(error, message_.empty() ? "Invalid deflate block." : message_);
        }
        if (bit_) { // The Adler-32 trailer starts on a byte boundary.
            bit_ = 0;
            ++at_;
        }
        if (at_ + 4 != data_.size()) return fail(error, "zlib stream must end with its Adler-32 after the last block.");
        if (out_.size() != size_) return fail(error, "Inflated image data does not match the image size.");
        std::uint32_t a = 1, b = 0;
        for (std::size_t i = 0; i < out_.size();) {
            const std::size_t end = std::min(out_.size(), i + 5552);
            for (; i < end; ++i) {
                a += static_cast<unsigned char>(out_[i]);
                b += a;
            }
            a %= 65521;
            b %= 65521;
        }
        std::uint32_t check = 0;
        for (std::size_t i = 0; i < 4; ++i) check = (check << 8) | static_cast<unsigned char>(data_[at_ + i]);
        if (check != ((b << 16) | a)) return fail(error, "Adler-32 does not match the image data.");
        return std::move(out_);
    }

private:
    static std::optional<std::string> fail(std::string& error, std::string message) {
        error = std::move(message);
        return std::nullopt;
    }
    bool stop(std::string message) {
        if (message_.empty()) message_ = std::move(message);
        broken_ = true;
        return false;
    }
    unsigned bits(unsigned count) {
        unsigned value = 0;
        for (unsigned i = 0; i < count; ++i) {
            if (at_ >= data_.size()) {
                stop("Deflate stream ended early.");
                return 0;
            }
            value |= unsigned((static_cast<unsigned char>(data_[at_]) >> bit_) & 1) << i;
            if (++bit_ == 8) {
                bit_ = 0;
                ++at_;
            }
        }
        return value;
    }
    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (std::size_t len = 1; len < 16; ++len) {
            code |= int(bits(1));
            if (broken_) return -1;
            const int count = h.count[len];
            if (code - count < first) return h.symbol[std::size_t(index + (code - first))];
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        stop("Invalid Huffman code.");
        return -1;
    }
    bool emit(std::size_t count) {
        if (out_.size() + count > size_) return stop("Inflated image data exceeds the image size.");
        return true;
    }
    bool stored() {
        if (bit_) {
            bit_ = 0;
            ++at_;
        }
        if (at_ + 4 > data_.size()) return stop("Stored block header is truncated.");
        const auto byte = [&](std::size_t i) { return unsigned(static_cast<unsigned char>(data_[at_ + i])); };
        const unsigned length = byte(0) | (byte(1) << 8), complement = byte(2) | (byte(3) << 8);
        if (length != (~complement & 0xffffu)) return stop("Stored block length check failed.");
        at_ += 4;
        if (data_.size() - at_ < length) return stop("Stored block is truncated.");
        if (!emit(length)) return false;
        out_.append(data_.substr(at_, length));
        at_ += length;
        return true;
    }
    bool codes(const Huffman& lengths, const Huffman& distances) {
        for (;;) {
            const int symbol = decode(lengths);
            if (symbol < 0) return false;
            if (symbol < 256) {
                if (!emit(1)) return false;
                out_.push_back(char(symbol));
                continue;
            }
            if (symbol == 256) return true;
            const auto l = std::size_t(symbol - 257);
            if (l >= length_base.size()) return stop("Invalid length symbol.");
            const std::size_t length = length_base[l] + bits(length_extra[l]);
            const int d = decode(distances);
            if (d < 0) return false;
            if (std::size_t(d) >= distance_base.size()) return stop("Invalid distance symbol.");
            const std::size_t distance = distance_base[std::size_t(d)] + bits(distance_extra[std::size_t(d)]);
            if (broken_) return false;
            if (distance > out_.size()) return stop("Distance reaches before the image data.");
            if (!emit(length)) return false;
            for (std::size_t i = 0; i < length; ++i) out_.push_back(out_[out_.size() - distance]);
        }
    }
    bool fixed() {
        static const auto tables = [] {
            std::array<std::uint8_t, 318> lengths{};
            for (std::size_t i = 0; i < 288; ++i) lengths[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
            for (std::size_t i = 288; i < 318; ++i) lengths[i] = 5;
            std::pair<Huffman, Huffman> result;
            build(result.first, lengths.data(), 288);
            build(result.second, lengths.data() + 288, 30);
            return result;
        }();
        return codes(tables.first, tables.second);
    }
    bool dynamic() {
        static constexpr std::array<std::uint8_t, 19> order{16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                            11, 4,  12, 3, 13, 2, 14, 1, 15};
        const unsigned nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
        if (broken_) return false;
        if (nlen > 286 || ndist > 30) return stop("Dynamic block declares too many codes.");
        std::array<std::uint8_t, 316> lengths{};
        for (unsigned i = 0; i < ncode; ++i) lengths[order[i]] = std::uint8_t(bits(3));
        Huffman code_lengths;
        if (broken_ || build(code_lengths, lengths.data(), 19) != 0) return stop("Invalid code length code.");
        lengths.fill(0);
        for (unsigned i = 0; i < nlen + ndist;) {
            const int symbol = decode(code_lengths);
            if (symbol < 0) return false;
            if (symbol < 16) {
                lengths[i++] = std::uint8_t(symbol);
                continue;
            }
            std::uint8_t repeat_value = 0;
            unsigned repeat = 0;
            if (symbol == 16) {
                if (i == 0) return stop("Repeat without a previous code length.");
                repeat_value = lengths[i - 1];
                repeat = 3 + bits(2);
            } else if (symbol == 17) {
                repeat = 3 + bits(3);
            } else {
                repeat = 11 + bits(7);
            }
            if (broken_) return false;
            if (i + repeat > nlen + ndist) return stop("Code lengths exceed the declared count.");
            while (repeat--) lengths[i++] = repeat_value;
        }
        if (lengths[256] == 0) return stop("Dynamic block has no end-of-block code.");
        Huffman literal, distance;
        const int literal_left = build(literal, lengths.data(), nlen);
        if (literal_left < 0 || (literal_left > 0 && nlen - literal.count[0] != 1))
            return stop("Incomplete literal/length code.");
        const int distance_left = build(distance, lengths.data() + nlen, ndist);
        if (distance_left < 0 || (distance_left > 0 && ndist - distance.count[0] > 1))
            return stop("Incomplete distance code.");
        return codes(literal, distance);
    }

    std::string_view data_;
    std::size_t size_;
    std::size_t at_ = 0;
    unsigned bit_ = 0;
    bool broken_ = false;
    std::string message_;
    std::string out_;
};

std::uint32_t read_be32(std::string_view bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) value = (value << 8) | static_cast<unsigned char>(bytes[at + i]);
    return value;
}

unsigned paeth(unsigned a, unsigned b, unsigned c) {
    const int p = int(a) + int(b) - int(c);
    const int pa = std::abs(p - int(a)), pb = std::abs(p - int(b)), pc = std::abs(p - int(c));
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

} // namespace

Result<CapturedImage> decode_png(std::string_view png) {
    const auto fail = [](std::string message) -> Result<CapturedImage> {
        return {std::nullopt, {{"invalid_png", Severity::error, "", std::move(message)}}};
    };
    if (png.substr(0, 8) != std::string_view("\x89PNG\r\n\x1a\n", 8)) return fail("Missing PNG signature.");
    CapturedImage image;
    std::string idat;
    bool header = false, ended = false, idat_closed = false;
    for (std::size_t at = 8; at < png.size();) {
        if (ended) return fail("Data follows the IEND chunk.");
        if (png.size() - at < 12) return fail("Truncated PNG chunk.");
        const auto length = read_be32(png, at);
        if (length > 0x7fffffffu || png.size() - at - 12 < length) return fail("Truncated PNG chunk.");
        const auto type = png.substr(at + 4, 4);
        const auto data = png.substr(at + 8, length);
        const auto crc = crc32(reinterpret_cast<const std::uint8_t*>(png.data() + at + 4), std::size_t(length) + 4);
        if (crc != read_be32(png, at + 8 + length)) return fail("CRC mismatch in chunk " + std::string(type) + ".");
        at += 12 + std::size_t(length);
        if (!header && type != "IHDR") return fail("The first chunk must be IHDR.");
        if (!idat.empty() && type != "IDAT") idat_closed = true;
        if (type == "IHDR") {
            if (header || length != 13) return fail("Invalid IHDR chunk.");
            header = true;
            image.extent = {read_be32(data, 0), read_be32(data, 4)};
            if (data.substr(8) != std::string_view("\x08\x06\x00\x00\x00", 5))
                return fail("Only 8-bit RGBA PNG without interlacing is supported.");
            if (!image.extent.width || !image.extent.height || image.extent.width > max_png_dimension ||
                image.extent.height > max_png_dimension ||
                std::uint64_t(image.extent.width) * image.extent.height * 4 > max_png_pixel_bytes)
                return fail("PNG extent is empty or exceeds the decoding bound.");
        } else if (type == "IDAT") {
            if (idat_closed) return fail("IDAT chunks must be consecutive.");
            idat.append(data);
        } else if (type == "IEND") {
            ended = true;
        } else if ((static_cast<unsigned char>(type[0]) & 0x20) == 0 && type != "PLTE") {
            return fail("Unsupported critical chunk " + std::string(type) + ".");
        }
    }
    if (!ended) return fail("Missing IEND chunk.");
    if (idat.empty()) return fail("Missing IDAT chunk.");
    const std::size_t row = std::size_t(image.extent.width) * 4;
    std::string error;
    auto raw = Inflater(idat, (row + 1) * image.extent.height).run(error);
    if (!raw) return fail(error);
    image.pixels.resize(row * image.extent.height);
    for (std::uint32_t y = 0; y < image.extent.height; ++y) {
        const auto filter = static_cast<unsigned char>((*raw)[y * (row + 1)]);
        if (filter > 4) return fail("Invalid filter type on row " + std::to_string(y) + ".");
        const auto* in = reinterpret_cast<const std::uint8_t*>(raw->data() + y * (row + 1) + 1);
        auto* out = image.pixels.data() + y * row;
        const auto* above = y ? out - row : nullptr;
        for (std::size_t x = 0; x < row; ++x) {
            const unsigned a = x >= 4 ? out[x - 4] : 0, b = above ? above[x] : 0,
                           c = above && x >= 4 ? above[x - 4] : 0;
            const unsigned predictor = filter == 0   ? 0
                                       : filter == 1 ? a
                                       : filter == 2 ? b
                                       : filter == 3 ? (a + b) / 2
                                                     : paeth(a, b, c);
            out[x] = std::uint8_t(in[x] + predictor);
        }
    }
    return {std::move(image), {}};
}

} // namespace tessera
