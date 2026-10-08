#pragma once
// Мост ad_core ↔ Qt: конвертеры типов, измерение текста и отрисовка кадра через QPainter.

#include <QColor>
#include <QPainterPath>
#include <QString>

#include <string>
#include <string_view>

#include "ad/scene.hpp"

class QPainter;

namespace app {

inline QString qs(std::string_view s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }
inline std::string us(const QString& s) { return s.toStdString(); }

QColor toQColor(ad::Color c, double alpha = 1.0);
ad::Color fromQColor(const QColor& c);
QPainterPath toQPath(const ad::Path& path);

/// Ширина текста по реальным метрикам шрифта (шрифт 100px, затем масштаб — точные дробные размеры).
class QtTextMeasurer final : public ad::TextMeasurer {
public:
    [[nodiscard]] double width(std::string_view utf8, const ad::Font& font) const override;
};

/// Нарисовать кадр в текущей системе координат painter'а (мировые единицы).
void renderFrame(QPainter& p, const ad::Frame& frame);

}  // namespace app
