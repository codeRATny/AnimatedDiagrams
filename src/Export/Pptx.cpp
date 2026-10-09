#include "Pptx.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include "Common/Exceptions.hpp"
#include "Export/PptxDeck.hpp"
#include "Utils/File.hpp"

namespace ad::pptx
{

namespace
{

/// Picture of `w` x `h` pixels fitted into the slide, centered (EMU).
struct Box
{
    int64_t x, y, cx, cy;
};

Box Fit(SlideSize size, int w, int h)
{
    const double sx = static_cast<double>(size.cx) / std::max(1, w);
    const double sy = static_cast<double>(size.cy) / std::max(1, h);
    const double s  = std::min(sx, sy);
    const auto   cx = static_cast<int64_t>(std::llround(w * s));
    const auto   cy = static_cast<int64_t>(std::llround(h * s));
    return {(size.cx - cx) / 2, (size.cy - cy) / 2, cx, cy};
}

std::string Xfrm(const Box &b)
{
    return std::format(R"(<a:xfrm><a:off x="{}" y="{}"/><a:ext cx="{}" cy="{}"/></a:xfrm>)", b.x, b.y, b.cx, b.cy);
}

bool IsGif(const std::filesystem::path &p)
{
    std::string e = PathToUtf8(p.extension());
    std::ranges::transform(e, e.begin(),
                           [](unsigned char c)
                           {
                               return static_cast<char>(std::tolower(c));
                           });
    return e == ".gif";
}

} // namespace

void WriteMediaSlides(const Target &target, std::span<const MediaSlide> slides)
{
    Deck deck(target);
    int  n = 0;
    for (const MediaSlide &m : slides)
    {
        ++n;
        Slide    &slide = deck.AddSlide();
        const Box box   = Fit(deck.Size(), m.width, m.height);
        slide.SetBackground(m.background);
        const int         id   = slide.NextId();
        const std::string name = std::format("Animated diagram {}", n);
        if (IsGif(m.media))
        {
            // an animated GIF plays by itself (PowerPoint, Keynote, Google Slides, Impress)
            const std::string rid = slide.AddImage(m.media);
            auto              pic = AppendXml(slide.Tree(),
                                              std::format(R"(<p:pic><p:nvPicPr><p:cNvPr id="{}" name=""/><p:cNvPicPr><a:picLocks noChangeAspect="1"/>)"
                                                                       R"(</p:cNvPicPr><p:nvPr/></p:nvPicPr><p:blipFill><a:blip r:embed="{}"/>)"
                                                                       R"(<a:stretch><a:fillRect/></a:stretch></p:blipFill><p:spPr>{}<a:prstGeom prst="rect">)"
                                                                       R"(<a:avLst/></a:prstGeom></p:spPr></p:pic>)",
                                                          id, rid, Xfrm(box)));
            pic.child("p:nvPicPr").child("p:cNvPr").attribute("name").set_value(name.c_str());
            continue;
        }
        if (m.poster.empty())
        {
            throw InvalidArgument("a video slide needs a poster image");
        }
        const auto [video, media] = slide.AddVideo(m.media);
        const std::string poster  = slide.AddImage(m.poster);
        auto              pic     = AppendXml(slide.Tree(),
                                              std::format(R"(<p:pic><p:nvPicPr><p:cNvPr id="{}" name=""><a:hlinkClick r:id="" action="ppaction://media"/>)"
                                                                           R"(</p:cNvPr><p:cNvPicPr><a:picLocks noChangeAspect="1"/></p:cNvPicPr><p:nvPr>)"
                                                                           R"(<a:videoFile r:link="{}"/><p:extLst><p:ext uri="{{DAA4B4D4-6D71-4841-9C94-3DA6A1C6D4CB}}">)"
                                                                           R"(<p14:media xmlns:p14="http://schemas.microsoft.com/office/powerpoint/2010/main" r:embed="{}"/>)"
                                                                           R"(</p:ext></p:extLst></p:nvPr></p:nvPicPr><p:blipFill><a:blip r:embed="{}"/>)"
                                                                           R"(<a:stretch><a:fillRect/></a:stretch></p:blipFill><p:spPr>{}<a:prstGeom prst="rect">)"
                                                                           R"(<a:avLst/></a:prstGeom></p:spPr></p:pic>)",
                                                          id, video, media, poster, Xfrm(box)));
        pic.child("p:nvPicPr").child("p:cNvPr").attribute("name").set_value(name.c_str());
        Timeline timeline(slide);
        timeline.MediaPlay(id, m.duration_ms, m.loop);
    }
    deck.Finish();
}

DeckInfo Inspect(const std::filesystem::path &path)
{
    auto     pkg = opc::Package::Open(path);
    DeckInfo info;
    auto     load = [&pkg](const std::string &part)
    {
        pugi::xml_document doc;
        const std::string  text = pkg.Read(part);
        if (!doc.load_buffer(text.data(), text.size()))
        {
            throw ParseError(part + ": invalid XML");
        }
        return doc;
    };
    const std::string pres_part = "ppt/presentation.xml";
    const auto        pres      = load(pres_part);
    const auto        root      = pres.document_element();
    info.size.cx                = root.child("p:sldSz").attribute("cx").as_llong();
    info.size.cy                = root.child("p:sldSz").attribute("cy").as_llong();
    const auto rels             = pkg.Relationships(pres_part);
    for (auto id : root.child("p:sldIdLst").children("p:sldId"))
    {
        ++info.slides;
        const std::string rid = id.attribute("r:id").value();
        const auto        rel = std::ranges::find(rels, rid, &opc::Relationship::id);
        if (rel == rels.end())
        {
            throw ParseError("dangling slide relationship " + rid);
        }
        const auto slide = load(opc::Package::ResolveTarget(pres_part, rel->target));
        for (const char *tag : {"p:sp", "p:pic", "p:cxnSp", "p:grpSp"})
        {
            info.shapes += static_cast<int>(slide.select_nodes((std::string("//") + tag).c_str()).size());
        }
        info.animations += static_cast<int>(slide.select_nodes("//p:cTn[@presetClass]").size());
        info.autoplay += static_cast<int>(slide.select_nodes("//p:cmd[@cmd='playFrom(0.0)']").size());
        if (!slide.select_nodes("//p159:morph").empty())
        {
            ++info.morph;
        }
    }
    for (const auto &p : pkg.Parts())
    {
        if (p.starts_with("ppt/media/"))
        {
            info.media.push_back(p);
        }
    }
    return info;
}

} // namespace ad::pptx
