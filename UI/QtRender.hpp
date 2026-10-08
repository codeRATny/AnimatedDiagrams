#ifndef _UI_QT_RENDER_HPP_
#define _UI_QT_RENDER_HPP_

#include <QColor>
#include <QPainterPath>
#include <QString>

#include <string>
#include <string_view>

#include "Engine/DisplayList.hpp"

class QPainter;

/// @file QtRender.hpp
/// @brief Bridge between ad_core and Qt: type conversions, text measurement and
///        drawing a frame (display list) with QPainter.

namespace ad::ui
{

inline QString     Qs(std::string_view s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }
inline std::string Us(const QString &s) { return s.toStdString(); }

QColor       ToQColor(Color c, double alpha = 1.0);
Color        FromQColor(const QColor &c);
QPainterPath ToQPath(const Path &path);

/// Text width from real font metrics (100 px reference font scaled -- exact fractional sizes).
class QtTextMeasurer final : public TextMeasurer
{
public:
    [[nodiscard]] double Width(std::string_view utf8, const Font &font) const override;
};

/// Draw a frame in the current painter coordinates (world units).
void RenderFrame(QPainter &p, const Frame &frame);

} // namespace ad::ui

#endif // _UI_QT_RENDER_HPP_
