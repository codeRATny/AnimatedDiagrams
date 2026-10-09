#include <gtest/gtest.h>

#include <filesystem>
#include <random>
#include <string>

#include "Engine/DisplayList.hpp"
#include "Export/Opc.hpp"
#include "Export/Pptx.hpp"
#include "Model/Registry.hpp"
#include "Model/Sample.hpp"
#include "Utils/File.hpp"

using namespace ad;
namespace fs = std::filesystem;

namespace
{

class PptxTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        dir = fs::temp_directory_path() / ("ad-pptx-test-" + std::to_string(std::random_device{}()));
        fs::create_directories(dir);
    }
    void TearDown() override { fs::remove_all(dir); }

    [[nodiscard]] fs::path File(const std::string &name, std::string_view content = {}) const
    {
        const fs::path p = dir / name;
        if (!content.empty())
        {
            WriteFile(p, content);
        }
        return p;
    }

    fs::path                 dir;
    const ApproxTextMeasurer tm;
};

} // namespace

TEST(OpcTest, RelativeAndResolvedTargets)
{
    EXPECT_EQ(opc::Package::RelativeTarget("ppt/slides/slide1.xml", "ppt/media/media1.mp4"), "../media/media1.mp4");
    EXPECT_EQ(opc::Package::RelativeTarget("ppt/presentation.xml", "ppt/slides/slide2.xml"), "slides/slide2.xml");
    EXPECT_EQ(opc::Package::ResolveTarget("ppt/slides/slide1.xml", "../slideLayouts/slideLayout7.xml"),
              "ppt/slideLayouts/slideLayout7.xml");
    EXPECT_EQ(opc::Package::ResolveTarget("ppt/presentation.xml", "/ppt/theme/theme1.xml"), "ppt/theme/theme1.xml");
}

TEST_F(PptxTest, AnimatedSlidesHaveShapesAndEffects)
{
    const Model         m = SampleModel();
    pptx::Target        t{File("animated.pptx"), {}, -1, pptx::kWide, "Sample"};
    pptx::VectorOptions o;
    pptx::WriteVectorSlides(t, m, Registry::Default(), tm, o);
    const auto info = pptx::Inspect(t.output);
    EXPECT_EQ(info.slides, 1);
    EXPECT_EQ(info.size, pptx::kWide);
    EXPECT_GT(info.shapes, 20);
    EXPECT_GT(info.animations, 10); // messages (appear, motion, disappear), states, notes, timers
    EXPECT_EQ(info.morph, 0);
}

TEST_F(PptxTest, SegmentsBecomeSlides)
{
    const Model         m = SampleModel();
    pptx::Target        t{File("segments.pptx"), {}, -1, pptx::kStandard, {}};
    pptx::VectorOptions o;
    o.segments = {{0, 4000}, {4000, m.scenario.duration}};
    pptx::WriteVectorSlides(t, m, Registry::Default(), tm, o);
    const auto info = pptx::Inspect(t.output);
    EXPECT_EQ(info.slides, 2);
    EXPECT_EQ(info.size, pptx::kStandard);
}

TEST_F(PptxTest, MorphKeyFrames)
{
    const Model         m = SampleModel();
    pptx::Target        t{File("morph.pptx"), {}, -1, pptx::kWide, {}};
    pptx::VectorOptions o;
    o.mode          = pptx::VectorOptions::Mode::Morph;
    o.morph_step_ms = 1000;
    pptx::WriteVectorSlides(t, m, Registry::Default(), tm, o);
    const auto info = pptx::Inspect(t.output);
    EXPECT_GE(info.slides, static_cast<int>(m.scenario.duration / 1000));
    EXPECT_EQ(info.morph, info.slides - 1); // every slide but the first morphs from the previous one
}

TEST_F(PptxTest, MediaSlidesAndInsertion)
{
    const fs::path    video  = File("clip.mp4", "not really a video");
    const fs::path    poster = File("poster.png", "not really a png");
    const fs::path    gif    = File("clip.gif", "GIF89a");
    pptx::MediaSlide  v{video, poster, 1280, 720, 5000, true, Color::Rgb(0x0a111f)};
    pptx::MediaSlide  g{gif, {}, 640, 480, 5000, true, Color::Rgb(0xffffff)};
    pptx::Target      t{File("media.pptx"), {}, -1, pptx::kWide, {}};
    const std::vector slides{v, g};
    pptx::WriteMediaSlides(t, slides);
    auto info = pptx::Inspect(t.output);
    EXPECT_EQ(info.slides, 2);
    EXPECT_EQ(info.autoplay, 1);
    EXPECT_EQ(info.media.size(), 3U); // video, poster, gif

    // insert an animated slide after the first one of that presentation (into a copy)
    pptx::Target        ins{File("inserted.pptx"), t.output, 1, {}, {}};
    pptx::VectorOptions o;
    pptx::WriteVectorSlides(ins, SampleModel(), Registry::Default(), tm, o);
    info = pptx::Inspect(ins.output);
    EXPECT_EQ(info.slides, 3);
    EXPECT_EQ(info.media.size(), 3U);
    EXPECT_GT(info.animations, 10);
    EXPECT_EQ(pptx::Inspect(t.output).slides, 2); // the original is untouched

    auto        pkg   = opc::Package::Open(ins.output);
    std::string pres  = pkg.Read("ppt/presentation.xml");
    const auto  first = pres.find("<p:sldId ");
    const auto  rels  = pkg.Relationships("ppt/presentation.xml");
    // the second entry of sldIdLst is the new slide
    const auto second = pres.find("<p:sldId ", first + 1);
    ASSERT_NE(second, std::string::npos);
    const auto rid_pos = pres.find("r:id=\"", second) + 6;
    const auto rid     = pres.substr(rid_pos, pres.find('"', rid_pos) - rid_pos);
    const auto rel     = std::ranges::find(rels, rid, &opc::Relationship::id);
    ASSERT_NE(rel, rels.end());
    EXPECT_EQ(rel->target, "slides/slide3.xml");
}
