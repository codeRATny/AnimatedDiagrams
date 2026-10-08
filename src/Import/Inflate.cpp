#include "Inflate.hpp"

#include <array>

#include "Common/Exceptions.hpp"

namespace ad
{

namespace
{

constexpr int kMaxBits  = 15;
constexpr int kMaxLCode = 286;
constexpr int kMaxDCode = 30;
constexpr int kFixLCode = 288;

constexpr std::array<int16_t, 29> kLengthBase{3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                              31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<int16_t, 29> kLengthExtra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<int16_t, 30> kDistBase{1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                            193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<int16_t, 30> kDistExtra{0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                             6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
constexpr std::array<int16_t, 19> kCodeOrder{16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

struct Huffman
{
    std::array<int16_t, kMaxBits + 1> count{};
    std::vector<int16_t>              symbol;
};

/// Build a canonical Huffman decoding table; returns 0 for a complete code,
/// > 0 for an incomplete one and < 0 for an over-subscribed (invalid) one.
int Construct(Huffman &h, const int16_t *length, int n)
{
    h.count.fill(0);
    for (int sym = 0; sym < n; ++sym)
    {
        ++h.count[static_cast<size_t>(length[sym])];
    }
    if (h.count[0] == n)
    {
        return 0;
    }
    int left = 1;
    for (int len = 1; len <= kMaxBits; ++len)
    {
        left <<= 1;
        left -= h.count[static_cast<size_t>(len)];
        if (left < 0)
        {
            return left;
        }
    }
    std::array<int16_t, kMaxBits + 1> offs{};
    for (int len = 1; len < kMaxBits; ++len)
    {
        offs[static_cast<size_t>(len + 1)] = static_cast<int16_t>(offs[static_cast<size_t>(len)] + h.count[static_cast<size_t>(len)]);
    }
    h.symbol.assign(static_cast<size_t>(n), 0);
    for (int sym = 0; sym < n; ++sym)
    {
        if (length[sym] != 0)
        {
            h.symbol[static_cast<size_t>(offs[static_cast<size_t>(length[sym])]++)] = static_cast<int16_t>(sym);
        }
    }
    return left;
}

class Inflater
{
public:
    Inflater(std::span<const uint8_t> in, size_t max_output) : _in(in), _max(max_output) {}

    std::vector<uint8_t> Run()
    {
        int last = 0;
        do
        {
            last            = _Bits(1);
            const int btype = _Bits(2);
            switch (btype)
            {
            case 0:
                _Stored();
                break;
            case 1:
                _Fixed();
                break;
            case 2:
                _Dynamic();
                break;
            default:
                throw ParseError("deflate: invalid block type");
            }
        } while (last == 0);
        return std::move(_out);
    }

private:
    int _Bits(int need)
    {
        uint32_t val = _bitbuf;
        while (_bitcnt < need)
        {
            if (_pos >= _in.size())
            {
                throw ParseError("deflate: unexpected end of data");
            }
            val |= static_cast<uint32_t>(_in[_pos++]) << static_cast<uint32_t>(_bitcnt);
            _bitcnt += 8;
        }
        _bitbuf = val >> static_cast<uint32_t>(need);
        _bitcnt -= need;
        return static_cast<int>(val & ((1U << static_cast<uint32_t>(need)) - 1U));
    }

    int _Decode(const Huffman &h)
    {
        int code  = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len <= kMaxBits; ++len)
        {
            code |= _Bits(1);
            const int count = h.count[static_cast<size_t>(len)];
            if (code - count < first)
            {
                return h.symbol[static_cast<size_t>(index + (code - first))];
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        throw ParseError("deflate: invalid Huffman code");
    }

    void _Put(uint8_t b)
    {
        if (_out.size() >= _max)
        {
            throw ParseError("deflate: output exceeds the size limit");
        }
        _out.push_back(b);
    }

    void _Stored()
    {
        _bitbuf = 0;
        _bitcnt = 0;
        if (_pos + 4 > _in.size())
        {
            throw ParseError("deflate: truncated stored block");
        }
        const uint32_t len  = _in[_pos] | (static_cast<uint32_t>(_in[_pos + 1]) << 8U);
        const uint32_t nlen = _in[_pos + 2] | (static_cast<uint32_t>(_in[_pos + 3]) << 8U);
        _pos += 4;
        if (len != (~nlen & 0xFFFFU))
        {
            throw ParseError("deflate: stored block length mismatch");
        }
        if (_pos + len > _in.size())
        {
            throw ParseError("deflate: truncated stored block");
        }
        for (uint32_t i = 0; i < len; ++i)
        {
            _Put(_in[_pos++]);
        }
    }

    void _Codes(const Huffman &lencode, const Huffman &distcode)
    {
        while (true)
        {
            int sym = _Decode(lencode);
            if (sym < 256)
            {
                _Put(static_cast<uint8_t>(sym));
                continue;
            }
            if (sym == 256)
            {
                return;
            }
            sym -= 257;
            if (sym >= 29)
            {
                throw ParseError("deflate: invalid length symbol");
            }
            const int len  = kLengthBase[static_cast<size_t>(sym)] + _Bits(kLengthExtra[static_cast<size_t>(sym)]);
            const int dsym = _Decode(distcode);
            if (dsym < 0 || dsym >= 30)
            {
                throw ParseError("deflate: invalid distance symbol");
            }
            const auto dist = static_cast<size_t>(kDistBase[static_cast<size_t>(dsym)] + _Bits(kDistExtra[static_cast<size_t>(dsym)]));
            if (dist > _out.size())
            {
                throw ParseError("deflate: distance too far back");
            }
            for (int i = 0; i < len; ++i)
            {
                _Put(_out[_out.size() - dist]);
            }
        }
    }

    void _Fixed()
    {
        static const auto kTables = []
        {
            std::array<int16_t, kFixLCode> lengths{};
            for (size_t s = 0; s < 144; ++s)
            {
                lengths[s] = 8;
            }
            for (size_t s = 144; s < 256; ++s)
            {
                lengths[s] = 9;
            }
            for (size_t s = 256; s < 280; ++s)
            {
                lengths[s] = 7;
            }
            for (size_t s = 280; s < kFixLCode; ++s)
            {
                lengths[s] = 8;
            }
            std::pair<Huffman, Huffman> t;
            Construct(t.first, lengths.data(), kFixLCode);
            std::array<int16_t, kMaxDCode> dist{};
            dist.fill(5);
            Construct(t.second, dist.data(), kMaxDCode);
            return t;
        }();
        _Codes(kTables.first, kTables.second);
    }

    void _Dynamic()
    {
        const int nlen  = _Bits(5) + 257;
        const int ndist = _Bits(5) + 1;
        const int ncode = _Bits(4) + 4;
        if (nlen > kMaxLCode || ndist > kMaxDCode)
        {
            throw ParseError("deflate: bad dynamic block counts");
        }
        std::array<int16_t, kMaxLCode + kMaxDCode> lengths{};
        for (int i = 0; i < ncode; ++i)
        {
            lengths[static_cast<size_t>(kCodeOrder[static_cast<size_t>(i)])] = static_cast<int16_t>(_Bits(3));
        }
        Huffman lencode;
        Huffman distcode;
        if (Construct(lencode, lengths.data(), 19) != 0)
        {
            throw ParseError("deflate: incomplete code-length code");
        }
        int index = 0;
        while (index < nlen + ndist)
        {
            int sym = _Decode(lencode);
            if (sym < 16)
            {
                lengths[static_cast<size_t>(index++)] = static_cast<int16_t>(sym);
                continue;
            }
            int16_t len = 0;
            if (sym == 16)
            {
                if (index == 0)
                {
                    throw ParseError("deflate: repeat without a previous length");
                }
                len = lengths[static_cast<size_t>(index - 1)];
                sym = 3 + _Bits(2);
            }
            else if (sym == 17)
            {
                sym = 3 + _Bits(3);
            }
            else
            {
                sym = 11 + _Bits(7);
            }
            if (index + sym > nlen + ndist)
            {
                throw ParseError("deflate: too many lengths");
            }
            while (sym-- > 0)
            {
                lengths[static_cast<size_t>(index++)] = len;
            }
        }
        if (lengths[256] == 0)
        {
            throw ParseError("deflate: missing end-of-block code");
        }
        int err = Construct(lencode, lengths.data(), nlen);
        if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1))
        {
            throw ParseError("deflate: invalid literal/length code");
        }
        err = Construct(distcode, lengths.data() + nlen, ndist);
        if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1))
        {
            throw ParseError("deflate: invalid distance code");
        }
        _Codes(lencode, distcode);
    }

    std::span<const uint8_t> _in;
    size_t                   _pos    = 0;
    uint32_t                 _bitbuf = 0;
    int                      _bitcnt = 0;
    size_t                   _max;
    std::vector<uint8_t>     _out;
};

} // namespace

std::vector<uint8_t> Inflate(std::span<const uint8_t> data, size_t max_output) { return Inflater(data, max_output).Run(); }

std::vector<uint8_t> InflateZlib(std::span<const uint8_t> data, size_t max_output)
{
    if (data.size() < 2)
    {
        throw ParseError("zlib: stream too short");
    }
    const uint8_t cmf = data[0];
    const uint8_t flg = data[1];
    if ((cmf & 0x0FU) != 8U || ((cmf << 8U) | flg) % 31U != 0U || (flg & 0x20U) != 0U)
    {
        throw ParseError("zlib: invalid header");
    }
    return Inflate(data.subspan(2), max_output);
}

} // namespace ad
