#include <gtest/gtest.h>

#include "Engine/FrameBuffer.hpp"
#include "Engine/Scene.hpp"
#include "Model/Sample.hpp"

using namespace ad;

namespace
{

/// Minimal reader of the FrameBuffer layout (the same walk as player/player.js).
class Reader
{
public:
    explicit Reader(const FrameBuffer &fb) : _d(fb.Data()), _s(fb.Strings()) {}

    double             Next() { return _d.at(_pos++); }
    [[nodiscard]] bool AtEnd() const { return _pos == _d.size(); }

    void SkipPath()
    {
        const auto n = static_cast<int>(Next());
        for (int i = 0; i < n; ++i)
        {
            switch (static_cast<int>(Next()))
            {
            case 0:
            case 1:
                _pos += 2;
                break;
            case 2:
                _pos += 4;
                break;
            case 3:
                _pos += 6;
                break;
            default:
                break;
            }
        }
    }

    std::string String()
    {
        const auto off = static_cast<size_t>(Next());
        const auto len = static_cast<size_t>(Next());
        return _s.substr(off, len);
    }

    /// Walk one item; returns its shape tag and collects text strings.
    int Item(std::vector<std::string> &texts)
    {
        const int      tag   = static_cast<int>(Next());
        const double   op    = Next();
        const double   fx    = Next();
        const unsigned flags = static_cast<unsigned>(Next());
        EXPECT_GE(op, 0);
        EXPECT_GE(fx, 0);
        if ((flags & 1U) != 0)
        {
            Next();
        }
        if ((flags & 2U) != 0)
        {
            _pos += 4;
            _pos += static_cast<size_t>(Next());
        }
        if ((flags & 4U) != 0)
        {
            _pos += 6;
        }
        if ((flags & 8U) != 0)
        {
            SkipPath();
        }
        switch (tag)
        {
        case 1:
            _pos += 5;
            break;
        case 2:
            _pos += 4;
            break;
        case 3:
            SkipPath();
            break;
        case 4:
            _pos += 5;
            break;
        case 5:
        {
            _pos += 2;
            texts.push_back(String());
            _pos += 2;
            String(); // family
            _pos += 2;
            if (Next() != 0)
            {
                _pos += 2;
            }
            break;
        }
        default:
            ADD_FAILURE() << "unknown tag " << tag;
        }
        return tag;
    }

private:
    const std::vector<double> &_d;
    const std::string         &_s;
    size_t                     _pos = 0;
};

} // namespace

TEST(FrameBufferTest, EncodesEveryShape)
{
    Frame f;
    Paint stroke_paint{std::nullopt, Stroke{Color::Rgb(0x112233), 2, {4, 2}, 1, true}, 0.5, PaintEffect::Glow};
    f.items.push_back({RectShape{{1, 2, 30, 40}, 6}, Paint{Color::Rgb(0xff0000), std::nullopt, 1, PaintEffect::Shadow}, {}, {}, {}});
    f.items.push_back({EllipseShape{{5, 6}, 7, 8}, stroke_paint, Transform{{1, 1}, 45, 2, {3, 4}}, {}, {}});
    Path p;
    p.MoveTo({0, 0});
    p.LineTo({1, 0});
    p.QuadTo({2, 1}, {3, 0});
    p.CubicTo({4, 1}, {5, 1}, {6, 0});
    p.Close();
    f.items.push_back({PathShape{p}, stroke_paint, {}, Path::Ellipse({0, 0}, 10, 10), {}});
    f.items.push_back({ArcShape{{0, 0}, 9, -90, 180}, stroke_paint, {}, {}, {}});
    TextShape t{{10, 20}, "Сервис A", Font{13, true, "Inter"}, HAlign::Center, VAlign::Middle, Stroke{Color::Rgb(0), 3, {}, 0, false}};
    f.items.push_back({t, Paint{Color::Rgb(0xffffff), std::nullopt, 1, PaintEffect::None}, {}, {}, {}});

    FrameBuffer fb;
    fb.Encode(f);
    const auto &d = fb.Data();
    ASSERT_GE(d.size(), 2U);
    EXPECT_EQ(d[0], kFrameBufferVersion);
    EXPECT_EQ(d[1], 5);
    // first item: rect, opacity 1, shadow, fill only, fill color, x y w h radius
    EXPECT_EQ(d[2], 1);
    EXPECT_EQ(d[4], 1);
    EXPECT_EQ(d[5], 1);
    EXPECT_EQ(d[6], 0xff0000);
    EXPECT_EQ(d[7], 1);
    EXPECT_EQ(d[11], 6);

    Reader                   r(fb);
    std::vector<std::string> texts;
    r.Next();
    r.Next();
    std::vector<int> tags;
    for (int i = 0; i < 5; ++i)
    {
        tags.push_back(r.Item(texts));
    }
    EXPECT_TRUE(r.AtEnd());
    EXPECT_EQ(tags, (std::vector<int>{1, 2, 3, 4, 5}));
    EXPECT_EQ(texts, std::vector<std::string>{"Сервис A"});
    EXPECT_NE(fb.Strings().find("Inter"), std::string::npos);
}

TEST(FrameBufferTest, SampleFramesDecodeCompletely)
{
    const Model              m = SampleModel();
    const ApproxTextMeasurer tm;
    SceneOptions             opt;
    opt.editor_chrome = false;
    FrameBuffer fb;
    for (const double t : {0.0, 2500.0, 6000.0, m.scenario.duration})
    {
        const Frame frame = BuildFrame(m, t, opt, tm);
        fb.Encode(frame); // buffers are reused
        Reader                   r(fb);
        std::vector<std::string> texts;
        r.Next();
        ASSERT_EQ(static_cast<size_t>(r.Next()), frame.items.size());
        for (size_t i = 0; i < frame.items.size(); ++i)
        {
            r.Item(texts);
        }
        EXPECT_TRUE(r.AtEnd()) << "t=" << t;
        EXPECT_FALSE(texts.empty());
    }
}
