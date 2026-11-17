#include "DeckCode.h"
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace DeckCode {
namespace {
using U32 = std::uint32_t;
using U64 = std::uint64_t;
using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t MaxCodeLength = 1024;
constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}

U32 crc32(const Bytes &bytes)
{
    U32 crc = 0xffffffffu;
    for (auto byte : bytes) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return crc ^ 0xffffffffu;
}

struct Writer
{
    Bytes bytes;
    std::size_t bits = 0;
    void bit(unsigned value)
    {
        if (bits % 8 == 0)
            bytes.push_back(0);
        if (value)
            bytes.back() |= std::uint8_t(0x80u >> (bits % 8));
        ++bits;
    }
    void write(U32 value, unsigned n)
    {
        while (n) {
            --n;
            bit((value >> n) & 1u);
        }
    }
    void gamma(U32 value)
    {
        ++value; // Callers supply only values 0..49.
        unsigned n = 0;
        for (U32 copy = value; copy; copy >>= 1)
            ++n;
        write(0, n - 1);
        write(value, n);
    }
    void rice(U32 gap, unsigned k)
    {
        for (U32 q = gap >> k; q; --q)
            bit(1);
        bit(0);
        write(gap & ((U32(1) << k) - 1u), k);
    }
};

struct Reader
{
    const Bytes &bytes;
    std::size_t pos = 0;
    unsigned bit()
    {
        require(pos < bytes.size() * 8, "Truncated deck code.");
        const unsigned value = (bytes[pos / 8] >> (7 - pos % 8)) & 1u;
        ++pos;
        return value;
    }
    U32 read(unsigned n)
    {
        U32 value = 0;
        while (n--)
            value = (value << 1) | bit();
        return value;
    }
    U32 gamma()
    {
        unsigned zeros = 0;
        while (!bit()) {
            ++zeros;
            require(zeros <= 5, "Deck count exceeds 50.");
        }
        const U32 value = ((U32(1) << zeros) | read(zeros)) - 1;
        require(value < MaxCards, "Deck count exceeds 50.");
        return value;
    }
    U32 rice(unsigned k, U32 maximum)
    {
        U64 q = 0;
        while (bit()) {
            ++q;
            require(q <= (U64(maximum) >> k), "Card ID overflow.");
        }
        const U64 value = (q << k) | read(k);
        require(value <= maximum, "Card ID overflow.");
        return U32(value);
    }
    void finish()
    {
        require(bytes.size() * 8 - pos < 8, "Unexpected trailing data.");
        while (pos < bytes.size() * 8)
            require(bit() == 0, "Nonzero padding in deck code.");
    }
};

std::string base64(const Bytes &bytes)
{
    std::string out;
    U32 acc = 0;
    unsigned bits = 0;
    for (auto byte : bytes) {
        acc = (acc << 8) | byte;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out += Alphabet[(acc >> bits) & 63u];
        }
    }

    if (bits)
        out += Alphabet[(acc << (6 - bits)) & 63u];

    return out;
}
Bytes unbase64(const std::string &text)
{
    require(!text.empty() && text.size() % 4 != 1, "Invalid Base64URL length.");

    Bytes out;
    U32 acc = 0;
    unsigned bits = 0;
    for (char c : text) {
        const std::size_t index = std::string(Alphabet).find(c);
        require(index != std::string::npos, "Invalid Base64URL character.");
        acc = (acc << 6) | U32(index);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(std::uint8_t((acc >> bits) & 255u));
        }
    }

    require(base64(out) == text, "Noncanonical Base64URL encoding.");
    return out;
}
} // namespace

std::string encode(const Deck &deck, std::string &error)
{
    error.clear();
    try {
        require(!deck.empty(), "Cannot share an empty deck.");
        require(deck.size() <= MaxCards, "A deck may contain at most 50 cards.");

        U64 total = 0;
        const U32 firstCount = deck.begin()->second;
        bool uniform = true;
        for (const auto &[id, count] : deck) {
            require(id != 0, "Card IDs must be positive.");
            require(count >= 1 && count <= MaxCards, "Quantity must be between 1 and 50.");
            total += count;
            uniform = uniform && count == firstCount;
        }
        require(total <= MaxCards, "A deck may contain at most 50 cards.");

        unsigned bestK = 0;
        U64 bestCost = std::numeric_limits<U64>::max();
        for (unsigned k = 0; k <= 31; ++k) {
            U64 cost = 0;
            U32 previous = 0;
            for (const auto &[id, count] : deck) {
                const U32 gap = id - previous - 1;
                cost += (U64(gap) >> k) + 1 + k;
                previous = id;
            }
            if (cost < bestCost) {
                bestCost = cost;
                bestK = k;
            }
        }

        Writer w;
        w.write(bestK, 5);
        w.bit(uniform);
        w.write(0, 2); // Reserved, must remain zero in v1.
        w.gamma(U32(deck.size()) - 1);

        if (uniform)
            w.gamma(firstCount - 1);

        U32 previous = 0;
        for (const auto &[id, count] : deck) {
            w.rice(id - previous - 1, bestK);
            previous = id;
            if (!uniform)
                w.gamma(count - 1);
        }

        const U32 checksum = crc32(w.bytes);
        for (int shift = 24; shift >= 0; shift -= 8)
            w.bytes.push_back(std::uint8_t(checksum >> shift));

        const std::string result = std::string(Prefix) + " " + base64(w.bytes);
        require(result.size() <= MaxCodeLength, "Deck code is too long.");

        return result;
    } catch (const std::runtime_error &e) {
        error = e.what();
        return {};
    }
}
bool decode(const std::string &code, Deck &output, std::string &error)
{
    error.clear();
    try {
        require(code.size() <= MaxCodeLength, "Deck code is too long.");

        const auto begin = code.find_first_not_of(" \t\r\n");
        require(begin != std::string::npos, "Paste a deck code first.");
        const auto end = code.find_last_not_of(" \t\r\n");
        const std::string text = code.substr(begin, end - begin + 1);
        const std::string prefix(Prefix);

        require(text.compare(0, prefix.size(), prefix) == 0,
                "Expected a WSDeckImporter/1: deck code. Other versions are unsupported.");
        const auto start = text.find_first_not_of(" ", prefix.size());
        require(start != std::string::npos, "Paste the encoded data after the prefix.");

        Bytes bytes = unbase64(text.substr(start));
        require(bytes.size() >= 6, "Truncated deck code.");
        const auto n = bytes.size();
        const U32 expected = (U32(bytes[n - 4]) << 24) | (U32(bytes[n - 3]) << 16)
                             | (U32(bytes[n - 2]) << 8) | U32(bytes[n - 1]);
        bytes.resize(n - 4);
        require(crc32(bytes) == expected, "Deck code checksum failed. Copy the code again.");

        Reader r{bytes};
        const unsigned k = r.read(5);
        const bool uniform = r.bit() != 0;
        require(r.read(2) == 0, "Unsupported deck code flags.");
        const U32 count = r.gamma() + 1;
        const U32 uniformCount = uniform ? r.gamma() + 1 : 0;

        Deck temporary;
        U32 previous = 0, total = 0;
        for (U32 i = 0; i < count; ++i) {
            require(previous < std::numeric_limits<U32>::max(), "Card ID overflow.");
            const U32 maximum = std::numeric_limits<U32>::max() - previous - 1;
            const U32 id = previous + r.rice(k, maximum) + 1;
            const U32 quantity = uniform ? uniformCount : r.gamma() + 1;
            require(quantity <= MaxCards - total, "A deck may contain at most 50 cards.");
            total += quantity;
            temporary.emplace(id, quantity);
            previous = id;
        }

        r.finish();
        output = std::move(temporary);
        return true;
    } catch (const std::runtime_error &e) {
        error = e.what();
        return false;
    }
}
} // namespace DeckCode