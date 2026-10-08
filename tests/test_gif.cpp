#include <gtest/gtest.h>

#include <numeric>
#include <random>
#include <set>

#include "ad/gif.hpp"

using namespace ad;

namespace {

// ---- эталонный LZW-декодер GIF (как в giflib) — для проверки энкодера --------
std::vector<std::uint8_t> lzwDecode(const std::vector<std::uint8_t>& data, int minCodeSize) {
    const int clearCode = 1 << minCodeSize;
    const int eoi = clearCode + 1;
    std::size_t bitPos = 0;
    auto read = [&](int width) -> int {
        int v = 0;
        for (int i = 0; i < width; ++i, ++bitPos) {
            if (bitPos / 8 >= data.size()) return -1;
            v |= ((data[bitPos / 8] >> (bitPos % 8)) & 1) << i;
        }
        return v;
    };
    std::vector<std::vector<std::uint8_t>> dict;
    auto resetDict = [&] {
        dict.clear();
        for (int i = 0; i < clearCode; ++i) dict.push_back({static_cast<std::uint8_t>(i)});
        dict.emplace_back();  // clear
        dict.emplace_back();  // eoi
    };
    resetDict();
    int width = minCodeSize + 1;
    int prev = -1;
    std::vector<std::uint8_t> out;
    for (;;) {
        const int code = read(width);
        if (code < 0) throw std::runtime_error("unexpected end of data");
        if (code == clearCode) {
            resetDict();
            width = minCodeSize + 1;
            prev = -1;
            continue;
        }
        if (code == eoi) break;
        std::vector<std::uint8_t> entry;
        if (code < static_cast<int>(dict.size()))
            entry = dict[static_cast<std::size_t>(code)];
        else if (code == static_cast<int>(dict.size()) && prev >= 0) {
            entry = dict[static_cast<std::size_t>(prev)];
            entry.push_back(entry.front());
        } else
            throw std::runtime_error("bad code");
        out.insert(out.end(), entry.begin(), entry.end());
        if (prev >= 0 && dict.size() < 4096) {
            auto e = dict[static_cast<std::size_t>(prev)];
            e.push_back(entry.front());
            dict.push_back(std::move(e));
        }
        prev = code;
        if (static_cast<int>(dict.size()) == (1 << width) && width < 12) ++width;
    }
    return out;
}

struct ParsedGif {
    int width = 0;
    int height = 0;
    bool loop = false;
    std::vector<int> delays;
    std::vector<std::vector<gif::Rgb>> palettes;
    std::vector<std::vector<std::uint8_t>> frames;  // индексы
};

ParsedGif parseGif(const std::vector<std::uint8_t>& b) {
    ParsedGif g;
    if (std::string(b.begin(), b.begin() + 6) != "GIF89a") throw std::runtime_error("bad header");
    std::size_t p = 6;
    auto u16 = [&] {
        const int v = b.at(p) | (b.at(p + 1) << 8);
        p += 2;
        return v;
    };
    g.width = u16();
    g.height = u16();
    const int packed = b.at(p);
    p += 3;
    if (packed & 0x80) p += 3u * (1u << ((packed & 7) + 1));
    auto readBlocks = [&] {
        std::vector<std::uint8_t> data;
        for (;;) {
            const std::size_t len = b.at(p++);
            if (len == 0) break;
            data.insert(data.end(), b.begin() + static_cast<std::ptrdiff_t>(p),
                        b.begin() + static_cast<std::ptrdiff_t>(p + len));
            p += len;
        }
        return data;
    };
    for (;;) {
        const int tag = b.at(p++);
        if (tag == 0x3B) break;
        if (tag == 0x21) {
            const int label = b.at(p++);
            if (label == 0xF9) {
                ++p;  // размер блока
                ++p;  // packed
                g.delays.push_back(u16());
                p += 2;  // прозрачность + терминатор
            } else {
                const auto data = readBlocks();
                if (label == 0xFF && data.size() >= 11 && std::string(data.begin(), data.begin() + 11) == "NETSCAPE2.0")
                    g.loop = true;
            }
            continue;
        }
        if (tag != 0x2C) throw std::runtime_error("unknown block");
        p += 8;
        const int ip = b.at(p++);
        std::vector<gif::Rgb> pal;
        if (ip & 0x80) {
            const int n = 1 << ((ip & 7) + 1);
            for (int i = 0; i < n; ++i, p += 3) pal.push_back({b.at(p), b.at(p + 1), b.at(p + 2)});
        }
        const int minCode = b.at(p++);
        g.frames.push_back(lzwDecode(readBlocks(), minCode));
        g.palettes.push_back(std::move(pal));
    }
    return g;
}

std::vector<std::uint8_t> solidFrame(int w, int h, gif::Rgb c) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w * h * 4));
    for (std::size_t i = 0; i < px.size(); i += 4) {
        px[i] = c[0];
        px[i + 1] = c[1];
        px[i + 2] = c[2];
        px[i + 3] = 255;
    }
    return px;
}

}  // namespace

class LzwRoundTrip : public ::testing::TestWithParam<std::tuple<int, std::size_t>> {};

TEST_P(LzwRoundTrip, DecodesBackToInput) {
    const auto [minCode, size] = GetParam();
    std::mt19937 rng(static_cast<std::uint32_t>(minCode * 1000 + size));
    std::uniform_int_distribution<int> dist(0, (1 << minCode) - 1);
    std::vector<std::uint8_t> idx(size);
    // смесь шума и длинных повторов: и рост ширины кода, и переполнение таблицы (clear)
    for (std::size_t i = 0; i < size; ++i) idx[i] = static_cast<std::uint8_t>(i % 97 < 60 ? dist(rng) : idx[i / 2]);
    EXPECT_EQ(lzwDecode(gif::lzwEncode(idx, minCode), minCode), idx);
}

INSTANTIATE_TEST_SUITE_P(Sizes, LzwRoundTrip,
                         ::testing::Combine(::testing::Values(2, 4, 8),
                                            ::testing::Values(std::size_t{0}, std::size_t{1}, std::size_t{17},
                                                              std::size_t{5000}, std::size_t{200000})));

TEST(Lzw, CompressesUniformData) {
    const std::vector<std::uint8_t> idx(100000, 3);
    const auto enc = gif::lzwEncode(idx, 2);
    EXPECT_LT(enc.size(), 2000u);
    EXPECT_EQ(lzwDecode(enc, 2), idx);
}

TEST(Quantize, ExactPaletteForFewColors) {
    std::vector<std::uint8_t> px = solidFrame(4, 4, {10, 20, 30});
    px[0] = 200;  // второй цвет в первом пикселе
    const auto q = gif::quantize(px);
    ASSERT_EQ(q.palette.size(), 2u);
    EXPECT_EQ(q.palette[q.indices[0]], (gif::Rgb{200, 20, 30}));
    EXPECT_EQ(q.palette[q.indices[5]], (gif::Rgb{10, 20, 30}));
}

TEST(Quantize, MedianCutLimitsPaletteAndKeepsColorsClose) {
    // градиент на 64k цветов
    const int w = 256, h = 256;
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w * h * 4));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const auto i = static_cast<std::size_t>((y * w + x) * 4);
            px[i] = static_cast<std::uint8_t>(x);
            px[i + 1] = static_cast<std::uint8_t>(y);
            px[i + 2] = static_cast<std::uint8_t>((x + y) / 2);
            px[i + 3] = 255;
        }
    const auto q = gif::quantize(px, 256);
    EXPECT_LE(q.palette.size(), 256u);
    EXPECT_GT(q.palette.size(), 128u);
    double err = 0;
    for (std::size_t i = 0; i < q.indices.size(); ++i) {
        const auto& c = q.palette[q.indices[i]];
        for (int k = 0; k < 3; ++k) err += std::abs(int(c[static_cast<std::size_t>(k)]) - int(px[i * 4 + static_cast<std::size_t>(k)]));
    }
    EXPECT_LT(err / static_cast<double>(q.indices.size() * 3), 12.0);  // средняя ошибка канала
}

TEST(GifEncoder, ProducesValidFile) {
    gif::Encoder enc(8, 6, true);
    enc.addFrame(solidFrame(8, 6, {255, 0, 0}), 10);
    enc.addFrame(solidFrame(8, 6, {0, 0, 255}), 20);
    const auto& bytes = enc.finish();
    EXPECT_EQ(bytes.back(), 0x3B);
    const ParsedGif g = parseGif(bytes);
    EXPECT_EQ(g.width, 8);
    EXPECT_EQ(g.height, 6);
    EXPECT_TRUE(g.loop);
    EXPECT_EQ(g.delays, (std::vector<int>{10, 20}));
    ASSERT_EQ(g.frames.size(), 2u);
    for (std::size_t f = 0; f < 2; ++f) ASSERT_EQ(g.frames[f].size(), 48u);
    EXPECT_EQ(g.palettes[0][g.frames[0][0]], (gif::Rgb{255, 0, 0}));
    EXPECT_EQ(g.palettes[1][g.frames[1][47]], (gif::Rgb{0, 0, 255}));
    EXPECT_EQ(&enc.finish(), &bytes);  // повторный finish не дописывает trailer
    EXPECT_EQ(bytes.size(), enc.finish().size());
}

TEST(GifEncoder, NoLoopExtensionWhenDisabled) {
    gif::Encoder enc(2, 2, false);
    enc.addFrame(solidFrame(2, 2, {1, 2, 3}), 5);
    EXPECT_FALSE(parseGif(enc.finish()).loop);
}

TEST(GifEncoder, RejectsWrongFrameSize) {
    gif::Encoder enc(4, 4, false);
    EXPECT_THROW(enc.addFrame(solidFrame(2, 2, {0, 0, 0}), 5), std::invalid_argument);
    EXPECT_THROW(gif::Encoder(0, 4, false), std::invalid_argument);
}

TEST(GifDelays, NoDriftAndMinimum) {
    const auto d15 = gif::frameDelaysCs(15, 15);  // 1 секунда
    EXPECT_EQ(std::accumulate(d15.begin(), d15.end(), 0), 100);
    const auto d30 = gif::frameDelaysCs(300, 30);  // 10 секунд
    EXPECT_EQ(std::accumulate(d30.begin(), d30.end(), 0), 1000);
    for (int v : gif::frameDelaysCs(60, 60)) EXPECT_GE(v, 2);
    EXPECT_TRUE(gif::frameDelaysCs(0, 30).empty());
}
