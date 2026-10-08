#pragma once
// Кадр как display list: набор примитивов в мировых координатах.
// Один и тот же кадр рисуется и на экране, и при экспорте (GIF/видео) —
// поэтому экспорт всегда совпадает с тем, что видно в редакторе.

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ad/color.hpp"
#include "ad/geometry.hpp"
#include "ad/model.hpp"

namespace ad {

struct Font {
    double size = 12;  // px в мировых единицах
    bool bold = false;
    friend bool operator==(const Font&, const Font&) = default;
};

/// Измерение ширины текста — реализуется рендерером (Qt) или приближённо (тесты).
class TextMeasurer {
public:
    virtual ~TextMeasurer() = default;
    [[nodiscard]] virtual double width(std::string_view utf8, const Font& font) const = 0;
};

/// Приближённая ширина: 0.6 × размер × число символов (без зависимостей от шрифтов).
class ApproxTextMeasurer final : public TextMeasurer {
public:
    [[nodiscard]] double width(std::string_view utf8, const Font& font) const override;
};

std::size_t utf8Length(std::string_view s);

struct Stroke {
    Color color;
    double width = 1;
    std::vector<double> dash;  // пусто — сплошная
    double dashOffset = 0;
    bool roundCap = false;
};

enum class Effect { None, Shadow, Glow };

struct Paint {
    std::optional<Color> fill;
    std::optional<Stroke> stroke;
    double opacity = 1;
    Effect effect = Effect::None;
};

/// p' = origin + R(rotateDeg)·S(scale)·(p − origin) + translate
struct Transform {
    Vec2 origin;
    double rotateDeg = 0;
    double scale = 1;
    Vec2 translate;
};

struct Clip {
    Rect rect;
    double radius = 0;
};

enum class HAlign { Left, Center, Right };
enum class VAlign { Baseline, Middle };

struct RectShape {
    Rect rect;
    double radius = 0;
};
struct EllipseShape {
    Vec2 center;
    double rx = 0;
    double ry = 0;
};
struct PathShape {
    Path path;
};
/// Дуга окружности: углы в градусах, 0 = +x, положительное направление — по часовой на экране.
struct ArcShape {
    Vec2 center;
    double radius = 0;
    double startDeg = 0;
    double sweepDeg = 360;
};
struct TextShape {
    Vec2 pos;
    std::string text;
    Font font;
    HAlign align = HAlign::Left;
    VAlign valign = VAlign::Baseline;
    std::optional<Stroke> halo;  // обводка под текстом (читаемость поверх линий)
};

using Shape = std::variant<RectShape, EllipseShape, PathShape, ArcShape, TextShape>;

struct Item {
    Shape shape;
    Paint paint;
    std::optional<Transform> transform;
    std::optional<Clip> clip;
};

struct Frame {
    std::vector<Item> items;  // в порядке отрисовки
};

struct SceneOptions {
    Selection selection;
    std::string connectFromNode;  // источник при создании связи (подсветка)
    bool connectMode = false;     // инструмент «Связь»: порты крупнее
    bool editorChrome = true;     // ручки точек изгиба и выделение
};

/// Палитра холста.
namespace palette {
inline constexpr Color kCanvasBg = Color::rgb(0x0a111f);
inline constexpr Color kPanelBg = Color::rgb(0x0b1220);
}  // namespace palette

Frame buildFrame(const Model& m, double t, const SceneOptions& opt, const TextMeasurer& tm);

/// Габариты содержимого (узлы, точки изгиба, заметки, бейджи) без отступов.
Rect contentBounds(const Model& m, const TextMeasurer& tm);

}  // namespace ad
