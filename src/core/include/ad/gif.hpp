#pragma once
// GIF89a-энкодер без внешних зависимостей: квантование median cut (RGB 5:5:5)
// и LZW-сжатие. У каждого кадра своя палитра (до 256 цветов).

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace ad::gif {

using Rgb = std::array<std::uint8_t, 3>;

struct Quantized {
    std::vector<Rgb> palette;           // 1..256 цветов
    std::vector<std::uint8_t> indices;  // индекс палитры на пиксель
};

/// Квантование RGBA-кадра (альфа игнорируется). Если различных цветов ≤ maxColors —
/// палитра точная, иначе median cut по гистограмме 5:5:5.
Quantized quantize(std::span<const std::uint8_t> rgba, int maxColors = 256);

/// LZW в варианте GIF (коды переменной длины, LSB-first, без упаковки в под-блоки).
std::vector<std::uint8_t> lzwEncode(std::span<const std::uint8_t> indices, int minCodeSize);

/// Задержки кадров в сотых долях секунды без накопления ошибки округления.
std::vector<int> frameDelaysCs(int frames, double fps);

class Encoder {
public:
    Encoder(int width, int height, bool loop);
    /// rgba.size() == width*height*4.
    void addFrame(std::span<const std::uint8_t> rgba, int delayCs);
    /// Завершить файл (trailer) и вернуть байты; повторные вызовы возвращают тот же результат.
    const std::vector<std::uint8_t>& finish();

    [[nodiscard]] int frameCount() const { return frames_; }

private:
    void u8(std::uint8_t v) { out_.push_back(v); }
    void u16(int v);

    int width_;
    int height_;
    int frames_ = 0;
    bool finished_ = false;
    std::vector<std::uint8_t> out_;
};

}  // namespace ad::gif
