#include "PresentationExport.hpp"

#include <QFileInfo>
#include <QTemporaryDir>

#include "Common/Exceptions.hpp"
#include "Export/Pptx.hpp"
#include "Model/Markers.hpp"
#include "QtRender.hpp"
#include "Utils/File.hpp"

namespace ad::ui
{

std::vector<std::pair<double, double>> PresentationSegments(const Model &m, bool by_markers)
{
    if (!by_markers)
    {
        return {{0.0, std::max(1.0, m.scenario.duration)}};
    }
    std::vector<std::pair<double, double>> out;
    for (const Segment &s : ScenarioSegments(m))
    {
        out.emplace_back(s.start, std::max(s.start + 1, s.end));
    }
    return out;
}

std::expected<ExportResult, QString> ExportPresentation(const Model &m, const ExportOptions &o, const Registry &reg, std::stop_token stop,
                                                        const ProgressFn &progress)
{
    const PresentationOptions &po = o.presentation;
    pptx::Target               target;
    target.output = PathFromUtf8(Us(o.output_path));
    if (!po.insert_into.isEmpty())
    {
        target.insert_into = PathFromUtf8(Us(po.insert_into));
        if (!QFileInfo::exists(po.insert_into))
        {
            return std::unexpected(QObject::tr("Presentation not found: %1").arg(po.insert_into));
        }
    }
    target.insert_after = po.insert_after;
    target.size         = po.wide ? pptx::kWide : pptx::kStandard;
    target.title        = m.meta.name;
    const auto segments = PresentationSegments(m, po.by_markers);

    try
    {
        if (po.mode == PptxMode::Animated || po.mode == PptxMode::Morph)
        {
            pptx::VectorOptions vo;
            vo.mode          = po.mode == PptxMode::Morph ? pptx::VectorOptions::Mode::Morph : pptx::VectorOptions::Mode::Animated;
            vo.segments      = segments;
            vo.morph_step_ms = po.morph_step_ms;
            pptx::WriteVectorSlides(target, m, reg, QtTextMeasurer{}, vo);
            const auto info = pptx::Inspect(target.output);
            return ExportResult{o.output_path, info.slides, QFileInfo(o.output_path).size(), {}};
        }

        // media slides: one clip per segment, rendered into a temporary folder
        const bool    gif = po.mode == PptxMode::Gif;
        QTemporaryDir tmp;
        if (!tmp.isValid())
        {
            return std::unexpected(QObject::tr("Cannot create a temporary folder"));
        }
        if (EncoderFor(gif ? ExportFormat::Gif : ExportFormat::Mp4).isEmpty())
        {
            return std::unexpected(
                QObject::tr("Video and GIF export are not available in this build: choose editable shapes or Morph key frames"));
        }
        std::vector<pptx::MediaSlide> slides;
        int                           frames = 0;
        QString                       encoder;
        for (size_t i = 0; i < segments.size(); ++i)
        {
            ExportOptions clip = o;
            clip.format        = gif ? ExportFormat::Gif : ExportFormat::Mp4;
            clip.output_path =
                tmp.filePath(QStringLiteral("clip%1.%2").arg(i + 1).arg(gif ? QStringLiteral("gif") : QStringLiteral("mp4")));
            clip.start_ms = segments[i].first;
            clip.end_ms   = segments[i].second;
            clip.loop     = segments.size() == 1; // a single clip loops, segments wait for a click
            const auto r  = RunExport(m, clip, reg, stop,
                                      [&](int done, int total)
                                      {
                                         if (progress)
                                         {
                                             progress(static_cast<int>(i) * 1000 + done * 1000 / std::max(1, total),
                                                       static_cast<int>(segments.size()) * 1000);
                                         }
                                     });
            if (!r.has_value())
            {
                return std::unexpected(r.error());
            }
            frames += r->frames;
            encoder = r->encoder;

            const ExportGeometry g = PlanExport(m, clip, reg);
            pptx::MediaSlide     s;
            s.media = PathFromUtf8(Us(clip.output_path));
            if (!gif)
            {
                const QString poster = tmp.filePath(QStringLiteral("poster%1.png").arg(i + 1));
                if (!RenderExportFrame(m, segments[i].first, g, clip.background, reg).save(poster, "PNG"))
                {
                    return std::unexpected(QObject::tr("Cannot write %1").arg(poster));
                }
                s.poster = PathFromUtf8(Us(poster));
            }
            s.width       = g.px_w;
            s.height      = g.px_h;
            s.duration_ms = segments[i].second - segments[i].first;
            s.loop        = clip.loop;
            s.background  = FromQColor(clip.background);
            slides.push_back(std::move(s));
        }
        pptx::WriteMediaSlides(target, slides);
        return ExportResult{o.output_path, frames, QFileInfo(o.output_path).size(), encoder};
    }
    catch (const AdError &e)
    {
        return std::unexpected(QString::fromUtf8(e.what()));
    }
    catch (const std::exception &e)
    {
        return std::unexpected(QObject::tr("PowerPoint export failed: %1").arg(QString::fromUtf8(e.what())));
    }
}

} // namespace ad::ui
