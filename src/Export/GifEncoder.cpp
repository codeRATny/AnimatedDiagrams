#include "GifEncoder.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace ad::gif
{

namespace
{

constexpr int kMaxCode = 4096; // 12-bit codes

uint16_t Key15(uint8_t r, uint8_t g, uint8_t b) { return static_cast<uint16_t>(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)); }

int Channel(uint16_t key, int ch) { return (key >> (10 - 5 * ch)) & 31; }

uint8_t Expand5(int v) { return static_cast<uint8_t>((v << 3) | (v >> 2)); }

std::optional<Quantized> ExactPalette(std::span<const uint8_t> rgba, int max_colors)
{
    Quantized q;
    q.indices.resize(rgba.size() / 4);
    std::unordered_map<uint32_t, uint8_t> map;
    map.reserve(static_cast<size_t>(max_colors) * 2);
    for (size_t i = 0; i < q.indices.size(); ++i)
    {
        const uint8_t *p   = &rgba[i * 4];
        const uint32_t key = (uint32_t{p[0]} << 16) | (uint32_t{p[1]} << 8) | p[2];
        auto           it  = map.find(key);
        if (it == map.end())
        {
            if (static_cast<int>(map.size()) >= max_colors)
            {
                return std::nullopt;
            }
            it = map.emplace(key, static_cast<uint8_t>(map.size())).first;
            q.palette.push_back({p[0], p[1], p[2]});
        }
        q.indices[i] = it->second;
    }
    if (q.palette.empty())
    {
        q.palette.push_back({0, 0, 0});
    }
    return q;
}

class BitWriter
{
public:
    explicit BitWriter(std::vector<uint8_t> &out) : _out(out) {}
    void Write(int code, int width)
    {
        _acc |= static_cast<uint32_t>(code) << _bits;
        _bits += width;
        while (_bits >= 8)
        {
            _out.push_back(static_cast<uint8_t>(_acc & 0xff));
            _acc >>= 8;
            _bits -= 8;
        }
    }
    void Flush()
    {
        if (_bits > 0)
        {
            _out.push_back(static_cast<uint8_t>(_acc & 0xff));
        }
        _acc  = 0;
        _bits = 0;
    }

private:
    std::vector<uint8_t> &_out;
    uint32_t              _acc  = 0;
    int                   _bits = 0;
};

} // namespace

Quantized Quantize(std::span<const uint8_t> rgba, int max_colors)
{
    max_colors = std::clamp(max_colors, 2, 256);
    if (auto exact = ExactPalette(rgba, max_colors))
    {
        return std::move(*exact);
    }

    // 5:5:5 histogram
    std::vector<uint32_t> hist(1 << 15, 0);
    const size_t          n = rgba.size() / 4;
    for (size_t i = 0; i < n; ++i)
    {
        ++hist[Key15(rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2])];
    }
    std::vector<uint16_t> keys;
    for (size_t k = 0; k < hist.size(); ++k)
    {
        if (hist[k] != 0)
        {
            keys.push_back(static_cast<uint16_t>(k));
        }
    }

    struct Box
    {
        size_t   begin, end;
        uint64_t pop;
        int      range;
        int      axis;
    };
    auto make_box = [&](size_t b, size_t e)
    {
        int      lo[3] = {31, 31, 31}, hi[3] = {0, 0, 0};
        uint64_t pop = 0;
        for (size_t i = b; i < e; ++i)
        {
            for (int c = 0; c < 3; ++c)
            {
                lo[c] = std::min(lo[c], Channel(keys[i], c));
                hi[c] = std::max(hi[c], Channel(keys[i], c));
            }
            pop += hist[keys[i]];
        }
        int axis = 0;
        for (int c = 1; c < 3; ++c)
        {
            if (hi[c] - lo[c] > hi[axis] - lo[axis])
            {
                axis = c;
            }
        }
        return Box{b, e, pop, hi[axis] - lo[axis], axis};
    };

    std::vector<Box> boxes{make_box(0, keys.size())};
    while (static_cast<int>(boxes.size()) < max_colors)
    {
        // split the "heaviest" box: range width * sqrt(population)
        size_t pick = boxes.size();
        double best = 0;
        for (size_t i = 0; i < boxes.size(); ++i)
        {
            const Box &b = boxes[i];
            if (b.end - b.begin < 2 || b.range == 0)
            {
                continue;
            }
            const double score = b.range * std::sqrt(static_cast<double>(b.pop));
            if (score > best)
            {
                best = score;
                pick = i;
            }
        }
        if (pick == boxes.size())
        {
            break;
        }
        const Box  b     = boxes[pick];
        const auto first = keys.begin() + static_cast<std::ptrdiff_t>(b.begin);
        const auto last  = keys.begin() + static_cast<std::ptrdiff_t>(b.end);
        std::sort(first, last,
                  [&](uint16_t x, uint16_t y)
                  {
                      return Channel(x, b.axis) < Channel(y, b.axis);
                  });
        uint64_t acc   = 0;
        size_t   split = b.begin + 1;
        for (size_t i = b.begin; i < b.end - 1; ++i)
        {
            acc += hist[keys[i]];
            split = i + 1;
            if (acc * 2 >= b.pop)
            {
                break;
            }
        }
        boxes[pick] = make_box(b.begin, split);
        boxes.push_back(make_box(split, b.end));
    }

    Quantized            q;
    std::vector<uint8_t> lut(1 << 15, 0);
    for (size_t bi = 0; bi < boxes.size(); ++bi)
    {
        double sum[3] = {0, 0, 0};
        double total  = 0;
        for (size_t i = boxes[bi].begin; i < boxes[bi].end; ++i)
        {
            const double w = hist[keys[i]];
            for (int c = 0; c < 3; ++c)
            {
                sum[c] += w * Expand5(Channel(keys[i], c));
            }
            total += w;
            lut[keys[i]] = static_cast<uint8_t>(bi);
        }
        q.palette.push_back({static_cast<uint8_t>(std::lround(sum[0] / total)), static_cast<uint8_t>(std::lround(sum[1] / total)),
                             static_cast<uint8_t>(std::lround(sum[2] / total))});
    }
    q.indices.resize(n);
    for (size_t i = 0; i < n; ++i)
    {
        q.indices[i] = lut[Key15(rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2])];
    }
    return q;
}

std::vector<uint8_t> LzwEncode(std::span<const uint8_t> indices, int min_code_size)
{
    // classic compress/GIF scheme: a code is written with the current width, the width grows
    // when the next free code no longer fits; a full table (4096) emits a clear code and resets
    std::vector<uint8_t> out;
    BitWriter            bw(out);
    const int            clear_code = 1 << min_code_size;
    const int            eoi_code   = clear_code + 1;
    int                  width      = min_code_size + 1;
    int                  maxcode    = (1 << width) - 1;
    int                  free_ent   = clear_code + 2;
    bool                 clear_flag = false;

    constexpr size_t      kHashSize = 8192;
    std::vector<uint32_t> hkeys(kHashSize, 0);
    std::vector<uint16_t> hvals(kHashSize, 0);

    auto output = [&](int code)
    {
        bw.Write(code, width);
        if (clear_flag)
        {
            width      = min_code_size + 1;
            maxcode    = (1 << width) - 1;
            clear_flag = false;
        }
        else if (free_ent > maxcode)
        {
            ++width;
            maxcode = width >= 12 ? kMaxCode : (1 << width) - 1;
        }
    };

    output(clear_code);
    if (indices.empty())
    {
        output(eoi_code);
        bw.Flush();
        return out;
    }

    int ent = indices[0];
    for (size_t i = 1; i < indices.size(); ++i)
    {
        const int      c   = indices[i];
        const uint32_t key = ((static_cast<uint32_t>(ent) << 8) | static_cast<uint32_t>(c)) + 1;
        size_t         h   = (key * 2654435761u) >> 19; // 13 bits
        while (hkeys[h] != 0 && hkeys[h] != key)
        {
            h = (h + 1) & (kHashSize - 1);
        }
        if (hkeys[h] == key)
        {
            ent = hvals[h];
            continue;
        }
        output(ent);
        ent = c;
        if (free_ent < kMaxCode)
        {
            hkeys[h] = key;
            hvals[h] = static_cast<uint16_t>(free_ent++);
        }
        else
        {
            std::ranges::fill(hkeys, 0u);
            free_ent   = clear_code + 2;
            clear_flag = true;
            output(clear_code);
        }
    }
    output(ent);
    output(eoi_code);
    bw.Flush();
    return out;
}

std::vector<int> FrameDelaysCs(int frames, double fps)
{
    std::vector<int> d;
    if (frames <= 0 || !(fps > 0))
    {
        return d;
    }
    d.reserve(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i)
    {
        const auto a = std::lround(i * 100.0 / fps);
        const auto b = std::lround((i + 1) * 100.0 / fps);
        // browsers stretch delays below 2 cs to 10 cs -- never go below 2
        d.push_back(std::max(2, static_cast<int>(b - a)));
    }
    return d;
}

// ---- Encoder ----------------------------------------------------------------

GifEncoder::GifEncoder(int width, int height, bool loop) : _width(width), _height(height)
{
    if (width <= 0 || height <= 0 || width > 65535 || height > 65535)
    {
        throw std::invalid_argument("bad GIF size");
    }
    for (char c : std::string_view("GIF89a"))
    {
        _U8(static_cast<uint8_t>(c));
    }
    _U16(width);
    _U16(height);
    _U8(0x70); // no global palette, 8-bit color resolution
    _U8(0);    // background
    _U8(0);    // aspect ratio
    if (loop)
    {
        _U8(0x21);
        _U8(0xFF);
        _U8(0x0B);
        for (char c : std::string_view("NETSCAPE2.0"))
        {
            _U8(static_cast<uint8_t>(c));
        }
        _U8(0x03);
        _U8(0x01);
        _U16(0); // loop forever
        _U8(0x00);
    }
}

void GifEncoder::_U16(int v)
{
    _U8(static_cast<uint8_t>(v & 0xff));
    _U8(static_cast<uint8_t>((v >> 8) & 0xff));
}

void GifEncoder::AddFrame(std::span<const uint8_t> rgba, int delay_cs)
{
    if (_finished)
    {
        throw std::logic_error("GIF already finished");
    }
    if (rgba.size() != static_cast<size_t>(_width) * static_cast<size_t>(_height) * 4)
    {
        throw std::invalid_argument("frame size mismatch");
    }

    const Quantized q    = Quantize(rgba, 256);
    int             bits = 1;
    while ((1 << bits) < static_cast<int>(q.palette.size()))
    {
        ++bits;
    }
    const int table_size = 1 << bits;
    const int min_code   = std::max(2, bits);

    // Graphic Control Extension
    _U8(0x21);
    _U8(0xF9);
    _U8(0x04);
    _U8(0x00);
    _U16(std::clamp(delay_cs, 0, 65535));
    _U8(0x00);
    _U8(0x00);
    // Image Descriptor + local palette
    _U8(0x2C);
    _U16(0);
    _U16(0);
    _U16(_width);
    _U16(_height);
    _U8(static_cast<uint8_t>(0x80 | (bits - 1)));
    for (int i = 0; i < table_size; ++i)
    {
        const Rgb c = i < static_cast<int>(q.palette.size()) ? q.palette[static_cast<size_t>(i)] : Rgb{0, 0, 0};
        _U8(c[0]);
        _U8(c[1]);
        _U8(c[2]);
    }
    _U8(static_cast<uint8_t>(min_code));
    const auto data = LzwEncode(q.indices, min_code);
    for (size_t pos = 0; pos < data.size(); pos += 255)
    {
        const size_t len = std::min<size_t>(255, data.size() - pos);
        _U8(static_cast<uint8_t>(len));
        _out.insert(_out.end(), data.begin() + static_cast<std::ptrdiff_t>(pos), data.begin() + static_cast<std::ptrdiff_t>(pos + len));
    }
    _U8(0x00);
    ++_frames;
}

const std::vector<uint8_t> &GifEncoder::Finish()
{
    if (!_finished)
    {
        _U8(0x3B);
        _finished = true;
    }
    return _out;
}

} // namespace ad::gif
