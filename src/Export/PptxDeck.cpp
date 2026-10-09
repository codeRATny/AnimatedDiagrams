#include "PptxDeck.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>

#include "Common/Exceptions.hpp"
#include "Utils/File.hpp"

namespace ad::pptx
{

namespace
{

constexpr std::string_view kPresentation = "ppt/presentation.xml";
constexpr std::string_view kSlideType    = "application/vnd.openxmlformats-officedocument.presentationml.slide+xml";
constexpr std::string_view kSlideRoot =
    R"(<p:sld xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" )"
    R"(xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" )"
    R"(xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main">)"
    R"(<p:cSld><p:spTree><p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>)"
    R"(<p:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/><a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm>)"
    R"(</p:grpSpPr></p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sld>)";

std::string Serialize(const pugi::xml_document &doc)
{
    std::ostringstream out;
    out << R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)" << '\n';
    doc.save(out, "", pugi::format_raw | pugi::format_no_declaration, pugi::encoding_utf8);
    return out.str();
}

pugi::xml_document Parse(const std::string &text, std::string_view what)
{
    pugi::xml_document doc;
    if (const auto r = doc.load_buffer(text.data(), text.size()); !r)
    {
        throw ParseError(std::string(what) + ": " + r.description());
    }
    return doc;
}

std::string Ext(const std::filesystem::path &file)
{
    std::string e = PathToUtf8(file.extension());
    if (!e.empty() && e.front() == '.')
    {
        e.erase(0, 1);
    }
    std::ranges::transform(e, e.begin(),
                           [](unsigned char c)
                           {
                               return static_cast<char>(std::tolower(c));
                           });
    return e;
}

std::string_view MediaType(std::string_view ext)
{
    if (ext == "png")
    {
        return "image/png";
    }
    if (ext == "gif")
    {
        return "image/gif";
    }
    if (ext == "jpg" || ext == "jpeg")
    {
        return "image/jpeg";
    }
    if (ext == "mp4" || ext == "m4v")
    {
        return "video/mp4";
    }
    if (ext == "webm")
    {
        return "video/webm";
    }
    throw InvalidArgument("unsupported media type ." + std::string(ext));
}

int64_t Ms(double ms) { return static_cast<int64_t>(std::llround(std::max(0.0, ms))); }

} // namespace

pugi::xml_node AppendXml(pugi::xml_node parent, std::string_view fragment)
{
    pugi::xml_node last = parent.last_child();
    if (const auto r = parent.append_buffer(fragment.data(), fragment.size(), pugi::parse_default); !r)
    {
        throw ParseError("internal XML fragment: " + std::string(r.description()));
    }
    return !last.empty() ? last.next_sibling() : parent.first_child();
}

std::string Hex(Color c) { return std::format("{:02X}{:02X}{:02X}", c.r, c.g, c.b); }

// ---------------------------------------------------------------------------
// Slide
// ---------------------------------------------------------------------------

Slide::Slide(Deck &deck, std::string part) : _deck(&deck), _part(std::move(part))
{
    _doc.load_buffer(kSlideRoot.data(), kSlideRoot.size());
    _tree = _doc.document_element().child("p:cSld").child("p:spTree");
}

void Slide::SetBackground(Color c)
{
    auto csld = Root().child("p:cSld");
    csld.remove_child("p:bg");
    auto bg = csld.prepend_child("p:bg");
    AppendXml(bg, std::format(R"(<p:bgPr><a:solidFill><a:srgbClr val="{}"/></a:solidFill><a:effectLst/></p:bgPr>)", Hex(c)));
}

std::string Slide::AddImage(const std::filesystem::path &file)
{
    const std::string ext  = Ext(file);
    auto             &pkg  = _deck->Package();
    const std::string part = pkg.FreeName("ppt/media/image", "." + ext);
    pkg.WriteFile(part, file);
    pkg.AddDefaultContentType(ext, MediaType(ext));
    return pkg.AddRelationship(_part, opc::kRelImage, opc::Package::RelativeTarget(_part, part));
}

std::pair<std::string, std::string> Slide::AddVideo(const std::filesystem::path &file)
{
    const std::string ext  = Ext(file);
    auto             &pkg  = _deck->Package();
    const std::string part = pkg.FreeName("ppt/media/media", "." + ext);
    pkg.WriteFile(part, file);
    pkg.AddDefaultContentType(ext, MediaType(ext));
    const std::string target = opc::Package::RelativeTarget(_part, part);
    const std::string video  = pkg.AddRelationship(_part, opc::kRelVideo, target);
    const std::string media  = pkg.AddRelationship(_part, opc::kRelMedia, target);
    return {video, media};
}

void Slide::SetMorphTransition(double duration_ms, std::optional<double> advance_ms)
{
    auto root = Root();
    root.remove_child("mc:AlternateContent");
    const std::string adv = advance_ms.has_value() ? std::format(R"( advClick="0" advTm="{}")", Ms(*advance_ms)) : std::string();
    const std::string xml =
        std::format(R"(<mc:AlternateContent xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006">)"
                    R"(<mc:Choice xmlns:p159="http://schemas.microsoft.com/office/powerpoint/2015/09/main" Requires="p159">)"
                    R"(<p:transition xmlns:p14="http://schemas.microsoft.com/office/powerpoint/2010/main" spd="slow" p14:dur="{0}"{1}>)"
                    R"(<p159:morph option="byObject"/></p:transition></mc:Choice>)"
                    R"(<mc:Fallback><p:transition spd="slow"{1}><p:fade/></p:transition></mc:Fallback></mc:AlternateContent>)",
                    Ms(duration_ms), adv);
    // after p:clrMapOvr, before p:timing
    pugi::xml_document frag;
    frag.load_buffer(xml.data(), xml.size());
    auto clr = root.child("p:clrMapOvr");
    root.insert_copy_after(frag.first_child(), clr);
}

pugi::xml_node Slide::Timing()
{
    auto root = Root();
    if (auto t = root.child("p:timing"); !t.empty())
    {
        return t;
    }
    if (auto ac = root.child("mc:AlternateContent"); !ac.empty())
    {
        return root.insert_child_after("p:timing", ac);
    }
    return root.insert_child_after("p:timing", root.child("p:clrMapOvr"));
}

// ---------------------------------------------------------------------------
// Deck
// ---------------------------------------------------------------------------

Deck::Deck(const Target &target)
    : _target(target), _pkg(target.insert_into.empty() ? opc::Package::Create(target.output, TemplateBytes()) : [&]
                                {
                                    if (target.insert_into != target.output)
                                    {
                                        std::filesystem::copy_file(target.insert_into, target.output,
                                                                   std::filesystem::copy_options::overwrite_existing);
                                    }
                                    return opc::Package::Open(target.output);
                                }())
{
    _LoadPresentation();
}

void Deck::_LoadPresentation()
{
    _pres     = Parse(_pkg.Read(kPresentation), kPresentation);
    auto pres = _pres.document_element();
    auto sz   = pres.child("p:sldSz");
    if (_target.insert_into.empty())
    {
        // new presentation: the requested size (the template is 4:3)
        sz.attribute("cx").set_value(_target.size.cx);
        sz.attribute("cy").set_value(_target.size.cy);
        sz.remove_attribute("type");
        if (_target.size == kStandard)
        {
            sz.append_attribute("type").set_value("screen4x3");
        }
    }
    _size.cx = sz.attribute("cx").as_llong(kWide.cx);
    _size.cy = sz.attribute("cy").as_llong(kWide.cy);

    int count = 0;
    for ([[maybe_unused]] auto s : pres.child("p:sldIdLst").children("p:sldId"))
    {
        ++count;
    }
    _insert_at = _target.insert_after < 0 ? count : std::min(_target.insert_after, count);
    _layout    = _BlankLayout();
}

std::string Deck::_BlankLayout()
{
    // the first slide master, its layouts: "blank" type, then the one named Blank, then the last one
    std::string master;
    for (const auto &r : _pkg.Relationships(kPresentation))
    {
        if (r.type.ends_with("/slideMaster"))
        {
            master = opc::Package::ResolveTarget(kPresentation, r.target);
            break;
        }
    }
    if (master.empty())
    {
        throw ParseError("presentation without a slide master");
    }
    std::string fallback;
    std::string named;
    for (const auto &r : _pkg.Relationships(master))
    {
        if (!r.type.ends_with("/slideLayout"))
        {
            continue;
        }
        const std::string part = opc::Package::ResolveTarget(master, r.target);
        const auto        doc  = Parse(_pkg.Read(part), part);
        const auto        root = doc.document_element();
        if (std::string_view(root.attribute("type").value()) == "blank")
        {
            return part;
        }
        if (named.empty() && std::string_view(root.child("p:cSld").attribute("name").value()) == "Blank")
        {
            named = part;
        }
        fallback = part;
    }
    if (!named.empty())
    {
        return named;
    }
    if (fallback.empty())
    {
        throw ParseError("slide master without layouts");
    }
    return fallback;
}

Slide &Deck::AddSlide()
{
    // slides are written by Finish(): names of pending slides are reserved here
    std::string part;
    for (int n = 1; part.empty(); ++n)
    {
        std::string candidate = "ppt/slides/slide" + std::to_string(n) + ".xml";
        const bool  pending   = std::ranges::any_of(_slides,
                                                    [&](const auto &s)
                                                    {
                                                     return s->Part() == candidate;
                                                 });
        if (!pending && !_pkg.Has(candidate))
        {
            part = std::move(candidate);
        }
    }
    _slides.push_back(std::unique_ptr<Slide>(new Slide(*this, part)));
    Slide &slide = *_slides.back();
    _pkg.AddRelationship(part, opc::kRelSlideLayout, opc::Package::RelativeTarget(part, _layout));
    _pkg.AddOverrideContentType(part, kSlideType);

    // presentation: relationship + sldId at the insertion point
    const std::string rid  = _pkg.AddRelationship(kPresentation, opc::kRelSlide, opc::Package::RelativeTarget(kPresentation, part));
    auto              pres = _pres.document_element();
    auto              list = pres.child("p:sldIdLst");
    if (list.empty())
    {
        // order: sldMasterIdLst, notesMasterIdLst, handoutMasterIdLst, sldIdLst, sldSz
        auto after = pres.child("p:handoutMasterIdLst");
        after      = !after.empty() ? after : pres.child("p:notesMasterIdLst");
        after      = !after.empty() ? after : pres.child("p:sldMasterIdLst");
        list       = !after.empty() ? pres.insert_child_after("p:sldIdLst", after) : pres.prepend_child("p:sldIdLst");
    }
    unsigned max_id = 255;
    for (auto s : list.children("p:sldId"))
    {
        max_id = std::max(max_id, s.attribute("id").as_uint());
    }
    pugi::xml_node before;
    int            i = 0;
    for (auto s : list.children("p:sldId"))
    {
        if (i++ == _insert_at)
        {
            before = s;
            break;
        }
    }
    auto id = !before.empty() ? list.insert_child_before("p:sldId", before) : list.append_child("p:sldId");
    id.append_attribute("id").set_value(max_id + 1);
    id.append_attribute("r:id").set_value(rid.c_str());
    ++_insert_at;
    return slide;
}

void Deck::Finish()
{
    for (const auto &s : _slides)
    {
        _pkg.Write(s->Part(), Serialize(s->_doc));
    }
    _pkg.Write(kPresentation, Serialize(_pres));
    if (_target.insert_into.empty() && !_target.title.empty() && _pkg.Has("docProps/core.xml"))
    {
        auto core = Parse(_pkg.Read("docProps/core.xml"), "core.xml");
        auto root = core.document_element();
        auto t    = root.child("dc:title");
        if (t.empty())
        {
            t = root.append_child("dc:title");
        }
        t.text().set(_target.title.c_str());
        _pkg.Write("docProps/core.xml", Serialize(core));
    }
    _pkg.Commit();
}

// ---------------------------------------------------------------------------
// Timeline
// ---------------------------------------------------------------------------

Timeline::Timeline(Slide &slide) : _slide(slide)
{
    auto timing = slide.Timing();
    timing.remove_children();
    // root -> main sequence -> a group started with the slide -> an inner "with previous" group
    auto root = AppendXml(timing, std::format(R"(<p:tnLst><p:par><p:cTn id="{}" dur="indefinite" restart="never" nodeType="tmRoot">)"
                                              R"(<p:childTnLst/></p:cTn></p:par></p:tnLst>)",
                                              _Id()));
    auto root_children = root.child("p:par").child("p:cTn").child("p:childTnLst");
    auto seq =
        AppendXml(root_children,
                  std::format(R"(<p:seq concurrent="1" nextAc="seek">)"
                              R"(<p:cTn id="{}" dur="indefinite" nodeType="mainSeq"><p:childTnLst/></p:cTn>)"
                              R"(<p:prevCondLst><p:cond evt="onPrev" delay="0"><p:tgtEl><p:sldTgt/></p:tgtEl></p:cond></p:prevCondLst>)"
                              R"(<p:nextCondLst><p:cond evt="onNext" delay="0"><p:tgtEl><p:sldTgt/></p:tgtEl></p:cond></p:nextCondLst>)"
                              R"(</p:seq>)",
                              _Id()));
    const int seq_id = seq.child("p:cTn").attribute("id").as_int();
    auto      outer  = AppendXml(seq.child("p:cTn").child("p:childTnLst"),
                                 std::format(R"(<p:par><p:cTn id="{}" fill="hold"><p:stCondLst><p:cond delay="indefinite"/>)"
                                                   R"(<p:cond evt="onBegin" delay="0"><p:tn val="{}"/></p:cond></p:stCondLst>)"
                                                   R"(<p:childTnLst/></p:cTn></p:par>)",
                                             _Id(), seq_id));
    auto      inner  = AppendXml(outer.child("p:cTn").child("p:childTnLst"),
                                 std::format(R"(<p:par><p:cTn id="{}" fill="hold"><p:stCondLst><p:cond delay="0"/></p:stCondLst>)"
                                                   R"(<p:childTnLst/></p:cTn></p:par>)",
                                             _Id()));
    _group           = inner.child("p:cTn").child("p:childTnLst");
}

pugi::xml_node Timeline::_Effect(int preset_id, std::string_view preset_class, int subtype, double delay_ms)
{
    ++_effects;
    auto par = AppendXml(_group, std::format(R"(<p:par><p:cTn id="{}" presetID="{}" presetClass="{}" presetSubtype="{}" fill="hold" )"
                                             R"(nodeType="withEffect"><p:stCondLst><p:cond delay="{}"/></p:stCondLst>)"
                                             R"(<p:childTnLst/></p:cTn></p:par>)",
                                             _Id(), preset_id, preset_class, subtype, Ms(delay_ms)));
    return par.child("p:cTn").child("p:childTnLst");
}

namespace
{

std::string TargetEl(int spid) { return std::format(R"(<p:tgtEl><p:spTgt spid="{}"/></p:tgtEl>)", spid); }

std::string SetVisibility(int id, int spid, std::string_view value, double delay_ms)
{
    return std::format(R"(<p:set><p:cBhvr><p:cTn id="{}" dur="1" fill="hold"><p:stCondLst><p:cond delay="{}"/></p:stCondLst></p:cTn>)"
                       R"({}<p:attrNameLst><p:attrName>style.visibility</p:attrName></p:attrNameLst></p:cBhvr>)"
                       R"(<p:to><p:strVal val="{}"/></p:to></p:set>)",
                       id, Ms(delay_ms), TargetEl(spid), value);
}

} // namespace

void Timeline::MediaPlay(int spid, double duration_ms, bool loop)
{
    auto effect = _Effect(1, "mediacall", 0, 0);
    AppendXml(effect,
              std::format(R"x(<p:cmd type="call" cmd="playFrom(0.0)"><p:cBhvr><p:cTn id="{}" dur="{}" fill="hold"/>{}</p:cBhvr></p:cmd>)x",
                          _Id(), Ms(duration_ms), TargetEl(spid)));
    // the media node (volume, looping) is a sibling of the main sequence
    auto root_children = _slide.Timing().child("p:tnLst").child("p:par").child("p:cTn").child("p:childTnLst");
    AppendXml(root_children, std::format(R"(<p:video><p:cMediaNode vol="80000" mute="1"><p:cTn id="{}"{} fill="hold" display="0">)"
                                         R"(<p:stCondLst><p:cond delay="indefinite"/></p:stCondLst></p:cTn>{}</p:cMediaNode></p:video>)",
                                         _Id(), loop ? R"( repeatCount="indefinite")" : "", TargetEl(spid)));
}

void Timeline::Entrance(int spid, double delay_ms, double fade_ms)
{
    auto effect = _Effect(fade_ms > 0 ? 10 : 1, "entr", 0, delay_ms);
    AppendXml(effect, SetVisibility(_Id(), spid, "visible", 0));
    if (fade_ms > 0)
    {
        AppendXml(
            effect,
            std::format(R"(<p:animEffect transition="in" filter="fade"><p:cBhvr><p:cTn id="{}" dur="{}"/>{}</p:cBhvr></p:animEffect>)",
                        _Id(), Ms(fade_ms), TargetEl(spid)));
    }
}

void Timeline::Exit(int spid, double delay_ms, double fade_ms)
{
    auto effect = _Effect(fade_ms > 0 ? 10 : 1, "exit", 0, delay_ms);
    if (fade_ms > 0)
    {
        AppendXml(
            effect,
            std::format(R"(<p:animEffect transition="out" filter="fade"><p:cBhvr><p:cTn id="{}" dur="{}"/>{}</p:cBhvr></p:animEffect>)",
                        _Id(), Ms(fade_ms), TargetEl(spid)));
    }
    AppendXml(effect, SetVisibility(_Id(), spid, "hidden", std::max(0.0, fade_ms - 1)));
}

void Timeline::Motion(int spid, double delay_ms, double duration_ms, const std::string &path, int points)
{
    auto        effect = _Effect(0, "path", 0, delay_ms);
    std::string types(static_cast<size_t>(std::max(points, 1)), 'A');
    auto        motion =
        AppendXml(effect, std::format(R"(<p:animMotion origin="layout" pathEditMode="relative" rAng="0" ptsTypes="{}">)"
                                      R"(<p:cBhvr><p:cTn id="{}" dur="{}" fill="hold"/>{})"
                                      R"(<p:attrNameLst><p:attrName>ppt_x</p:attrName><p:attrName>ppt_y</p:attrName></p:attrNameLst>)"
                                      R"(</p:cBhvr></p:animMotion>)",
                                      types, _Id(), Ms(duration_ms), TargetEl(spid)));
    motion.prepend_attribute("path").set_value(path.c_str()); // escaped by pugixml
}

void Timeline::Pulse(int spid, double delay_ms, double duration_ms, int percent, int repeat)
{
    auto effect = _Effect(6, "emph", 0, delay_ms);
    AppendXml(effect, std::format(R"(<p:animScale><p:cBhvr><p:cTn id="{}" dur="{}" autoRev="1" repeatCount="{}" fill="hold"/>{}</p:cBhvr>)"
                                  R"(<p:by x="{}" y="{}"/></p:animScale>)",
                                  _Id(), Ms(duration_ms / std::max(1, repeat) / 2), std::max(1, repeat) * 1000, TargetEl(spid),
                                  percent * 1000, percent * 1000));
}

void Timeline::Rotate(int spid, double delay_ms, double duration_ms, double degrees, bool back, int repeat)
{
    auto      effect = _Effect(back ? 32 : 8, "emph", 0, delay_ms);
    const int n      = std::max(1, repeat);
    AppendXml(effect, std::format(R"(<p:animRot by="{}"><p:cBhvr><p:cTn id="{}" dur="{}"{} repeatCount="{}" fill="hold"/>{})"
                                  R"(<p:attrNameLst><p:attrName>r</p:attrName></p:attrNameLst></p:cBhvr></p:animRot>)",
                                  static_cast<int64_t>(std::llround(degrees * 60000)), _Id(), Ms(duration_ms / n / (back ? 2 : 1)),
                                  back ? R"( autoRev="1")" : "", n * 1000, TargetEl(spid)));
}

void Timeline::Blink(int spid, double delay_ms, double duration_ms, int repeat)
{
    auto      effect = _Effect(35, "emph", 0, delay_ms);
    const int n      = std::max(1, repeat);
    AppendXml(effect, std::format(R"(<p:anim calcmode="discrete" valueType="str"><p:cBhvr override="childStyle">)"
                                  R"(<p:cTn id="{}" dur="{}" repeatCount="{}"/>{})"
                                  R"(<p:attrNameLst><p:attrName>style.visibility</p:attrName></p:attrNameLst></p:cBhvr>)"
                                  R"(<p:tavLst><p:tav tm="0"><p:val><p:strVal val="hidden"/></p:val></p:tav>)"
                                  R"(<p:tav tm="50000"><p:val><p:strVal val="visible"/></p:val></p:tav></p:tavLst></p:anim>)",
                                  _Id(), Ms(duration_ms / n), n * 1000, TargetEl(spid)));
}

} // namespace ad::pptx
