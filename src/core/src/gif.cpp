#include "ad/gif.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace ad::gif {

namespace {

constexpr int kMaxCode = 4096;  // 12-битные коды

std::uint16_t key15(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return static_cast<std::uint16_t>(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

int channel(std::uint16_t key, int ch) { return (key >> (10 - 5 * ch)) & 31; }

std::uint8_t expand5(int v) { return static_cast<std::uint8_t>((v << 3) | (v >> 2)); }

std::optional<Quantized> exactPalette(std::span<const std::uint8_t> rgba, int maxColors) {
    Quantized q;
    q.indices.resize(rgba.size() / 4);
    std::unordered_map<std::uint32_t, std::uint8_t> map;
    map.reserve(static_cast<std::size_t>(maxColors) * 2);
    for (std::size_t i = 0; i < q.indices.size(); ++i) {
        const std::uint8_t* p = &rgba[i * 4];
        const std::uint32_t key = (std::uint32_t{p[0]} << 16) | (std::uint32_t{p[1]} << 8) | p[2];
        auto it = map.find(key);
        if (it == map.end()) {
            if (static_cast<int>(map.size()) >= maxColors) return std::nullopt;
            it = map.emplace(key, static_cast<std::uint8_t>(map.size())).first;
            q.palette.push_back({p[0], p[1], p[2]});
        }
        q.indices[i] = it->second;
    }
    if (q.palette.empty()) q.palette.push_back({0, 0, 0});
    return q;
}

class BitWriter {
public:
    explicit BitWriter(std::vector<std::uint8_t>& out) : out_(out) {}
    void write(int code, int width) {
        acc_ |= static_cast<std::uint32_t>(code) << bits_;
        bits_ += width;
        while (bits_ >= 8) {
            out_.push_back(static_cast<std::uint8_t>(acc_ & 0xff));
            acc_ >>= 8;
            bits_ -= 8;
        }
    }
    void flush() {
        if (bits_ > 0) out_.push_back(static_cast<std::uint8_t>(acc_ & 0xff));
        acc_ = 0;
        bits_ = 0;
    }

private:
    std::vector<std::uint8_t>& out_;
    std::uint32_t acc_ = 0;
    int bits_ = 0;
};

}  // namespace

Quantized quantize(std::span<const std::uint8_t> rgba, int maxColors) {
    maxColors = std::clamp(maxColors, 2, 256);
    if (auto exact = exactPalette(rgba, maxColors)) return std::move(*exact);

    // гистограмма 5:5:5
    std::vector<std::uint32_t> hist(1 << 15, 0);
    const std::size_t n = rgba.size() / 4;
    for (std::size_t i = 0; i < n; ++i) ++hist[key15(rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2])];
    std::vector<std::uint16_t> keys;
    for (std::size_t k = 0; k < hist.size(); ++k)
        if (hist[k]) keys.push_back(static_cast<std::uint16_t>(k));

    struct Box {
        std::size_t begin, end;
        std::uint64_t pop;
        int range;
        int axis;
    };
    auto makeBox = [&](std::size_t b, std::size_t e) {
        int lo[3] = {31, 31, 31}, hi[3] = {0, 0, 0};
        std::uint64_t pop = 0;
        for (std::size_t i = b; i < e; ++i) {
            for (int c = 0; c < 3; ++c) {
                lo[c] = std::min(lo[c], channel(keys[i], c));
                hi[c] = std::max(hi[c], channel(keys[i], c));
            }
            pop += hist[keys[i]];
        }
        int axis = 0;
        for (int c = 1; c < 3; ++c)
            if (hi[c] - lo[c] > hi[axis] - lo[axis]) axis = c;
        return Box{b, e, pop, hi[axis] - lo[axis], axis};
    };

    std::vector<Box> boxes{makeBox(0, keys.size())};
    while (static_cast<int>(boxes.size()) < maxColors) {
        // делим «самую тяжёлую» коробку: ширина диапазона × √популяции
        std::size_t pick = boxes.size();
        double best = 0;
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            const Box& b = boxes[i];
            if (b.end - b.begin < 2 || b.range == 0) continue;
            const double score = b.range * std::sqrt(static_cast<double>(b.pop));
            if (score > best) {
                best = score;
                pick = i;
            }
        }
        if (pick == boxes.size()) break;
        const Box b = boxes[pick];
        const auto first = keys.begin() + static_cast<std::ptrdiff_t>(b.begin);
        const auto last = keys.begin() + static_cast<std::ptrdiff_t>(b.end);
        std::sort(first, last, [&](std::uint16_t x, std::uint16_t y) { return channel(x, b.axis) < channel(y, b.axis); });
        std::uint64_t acc = 0;
        std::size_t split = b.begin + 1;
        for (std::size_t i = b.begin; i < b.end - 1; ++i) {
            acc += hist[keys[i]];
            split = i + 1;
            if (acc * 2 >= b.pop) break;
        }
        boxes[pick] = makeBox(b.begin, split);
        boxes.push_back(makeBox(split, b.end));
    }

    Quantized q;
    std::vector<std::uint8_t> lut(1 << 15, 0);
    for (std::size_t bi = 0; bi < boxes.size(); ++bi) {
        double sum[3] = {0, 0, 0};
        double total = 0;
        for (std::size_t i = boxes[bi].begin; i < boxes[bi].end; ++i) {
            const double w = hist[keys[i]];
            for (int c = 0; c < 3; ++c) sum[c] += w * expand5(channel(keys[i], c));
            total += w;
            lut[keys[i]] = static_cast<std::uint8_t>(bi);
        }
        q.palette.push_back({static_cast<std::uint8_t>(std::lround(sum[0] / total)),
                             static_cast<std::uint8_t>(std::lround(sum[1] / total)),
                             static_cast<std::uint8_t>(std::lround(sum[2] / total))});
    }
    q.indices.resize(n);
    for (std::size_t i = 0; i < n; ++i) q.indices[i] = lut[key15(rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2])];
    return q;
}

std::vector<std::uint8_t> lzwEncode(std::span<const std::uint8_t> indices, int minCodeSize) {
    // классическая схема compress/GIF: код пишется текущей шириной, ширина растёт,
    // когда следующий свободный код перестаёт в неё помещаться; при заполнении
    // таблицы (4096) — clear-код и сброс
    std::vector<std::uint8_t> out;
    BitWriter bw(out);
    const int clearCode = 1 << minCodeSize;
    const int eoiCode = clearCode + 1;
    int width = minCodeSize + 1;
    int maxcode = (1 << width) - 1;
    int freeEnt = clearCode + 2;
    bool clearFlag = false;

    constexpr std::size_t kHashSize = 8192;
    std::vector<std::uint32_t> hkeys(kHashSize, 0);
    std::vector<std::uint16_t> hvals(kHashSize, 0);

    auto output = [&](int code) {
        bw.write(code, width);
        if (clearFlag) {
            width = minCodeSize + 1;
            maxcode = (1 << width) - 1;
            clearFlag = false;
        } else if (freeEnt > maxcode) {
            ++width;
            maxcode = width >= 12 ? kMaxCode : (1 << width) - 1;
        }
    };

    output(clearCode);
    if (indices.empty()) {
        output(eoiCode);
        bw.flush();
        return out;
    }

    int ent = indices[0];
    for (std::size_t i = 1; i < indices.size(); ++i) {
        const int c = indices[i];
        const std::uint32_t key = ((static_cast<std::uint32_t>(ent) << 8) | static_cast<std::uint32_t>(c)) + 1;
        std::size_t h = (key * 2654435761u) >> 19;  // 13 бит
        while (hkeys[h] != 0 && hkeys[h] != key) h = (h + 1) & (kHashSize - 1);
        if (hkeys[h] == key) {
            ent = hvals[h];
            continue;
        }
        output(ent);
        ent = c;
        if (freeEnt < kMaxCode) {
            hkeys[h] = key;
            hvals[h] = static_cast<std::uint16_t>(freeEnt++);
        } else {
            std::ranges::fill(hkeys, 0u);
            freeEnt = clearCode + 2;
            clearFlag = true;
            output(clearCode);
        }
    }
    output(ent);
    output(eoiCode);
    bw.flush();
    return out;
}

std::vector<int> frameDelaysCs(int frames, double fps) {
    std::vector<int> d;
    if (frames <= 0 || !(fps > 0)) return d;
    d.reserve(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        const auto a = std::lround(i * 100.0 / fps);
        const auto b = std::lround((i + 1) * 100.0 / fps);
        // браузеры растягивают задержки < 2 сс до 10 сс — не опускаемся ниже 2
        d.push_back(std::max(2, static_cast<int>(b - a)));
    }
    return d;
}

// ---- Encoder ----------------------------------------------------------------

Encoder::Encoder(int width, int height, bool loop) : width_(width), height_(height) {
    if (width <= 0 || height <= 0 || width > 65535 || height > 65535) throw std::invalid_argument("bad GIF size");
    for (char c : std::string_view("GIF89a")) u8(static_cast<std::uint8_t>(c));
    u16(width);
    u16(height);
    u8(0x70);  // без глобальной палитры, глубина цвета 8 бит
    u8(0);     // фон
    u8(0);     // соотношение сторон
    if (loop) {
        u8(0x21);
        u8(0xFF);
        u8(0x0B);
        for (char c : std::string_view("NETSCAPE2.0")) u8(static_cast<std::uint8_t>(c));
        u8(0x03);
        u8(0x01);
        u16(0);  // бесконечный повтор
        u8(0x00);
    }
}

void Encoder::u16(int v) {
    u8(static_cast<std::uint8_t>(v & 0xff));
    u8(static_cast<std::uint8_t>((v >> 8) & 0xff));
}

void Encoder::addFrame(std::span<const std::uint8_t> rgba, int delayCs) {
    if (finished_) throw std::logic_error("GIF already finished");
    if (rgba.size() != static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4)
        throw std::invalid_argument("frame size mismatch");

    const Quantized q = quantize(rgba, 256);
    int bits = 1;
    while ((1 << bits) < static_cast<int>(q.palette.size())) ++bits;
    const int tableSize = 1 << bits;
    const int minCode = std::max(2, bits);

    // Graphic Control Extension
    u8(0x21);
    u8(0xF9);
    u8(0x04);
    u8(0x00);
    u16(std::clamp(delayCs, 0, 65535));
    u8(0x00);
    u8(0x00);
    // Image Descriptor + локальная палитра
    u8(0x2C);
    u16(0);
    u16(0);
    u16(width_);
    u16(height_);
    u8(static_cast<std::uint8_t>(0x80 | (bits - 1)));
    for (int i = 0; i < tableSize; ++i) {
        const Rgb c = i < static_cast<int>(q.palette.size()) ? q.palette[static_cast<std::size_t>(i)] : Rgb{0, 0, 0};
        u8(c[0]);
        u8(c[1]);
        u8(c[2]);
    }
    u8(static_cast<std::uint8_t>(minCode));
    const auto data = lzwEncode(q.indices, minCode);
    for (std::size_t pos = 0; pos < data.size(); pos += 255) {
        const std::size_t len = std::min<std::size_t>(255, data.size() - pos);
        u8(static_cast<std::uint8_t>(len));
        out_.insert(out_.end(), data.begin() + static_cast<std::ptrdiff_t>(pos),
                    data.begin() + static_cast<std::ptrdiff_t>(pos + len));
    }
    u8(0x00);
    ++frames_;
}

const std::vector<std::uint8_t>& Encoder::finish() {
    if (!finished_) {
        u8(0x3B);
        finished_ = true;
    }
    return out_;
}

}  // namespace ad::gif
