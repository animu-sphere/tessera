#include <tessera/snapshot/snapshot_bundle.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
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

} // namespace tessera
