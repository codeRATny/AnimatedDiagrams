#include "Fields.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

#include "Engine/Shapes.hpp"
#include "Model/Easing.hpp"
#include "QtRender.hpp"

namespace ad::ui::fields
{

namespace
{

QString TextColorFor(const QColor &bg) { return bg.lightness() > 140 ? QStringLiteral("#0b1220") : QStringLiteral("#ffffff"); }

void PaintColorButton(QPushButton *b, const QString &hex, bool dimmed)
{
    const QColor c(hex);
    b->setText(hex);
    b->setToolTip(dimmed ? QObject::tr("Наследуется (авто)") : QString());
    b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    b->setStyleSheet(QStringLiteral("QPushButton#colorButton { background: %1; color: %2; %3 }")
                         .arg(hex, TextColorFor(c), dimmed ? QStringLiteral("border-style: dashed; font-style: italic;") : QString()));
}

void MakeShrinkable(QComboBox *c)
{
    c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    c->setMinimumContentsLength(4);
}

} // namespace

Options FromCatalog(std::span<const OptionInfo> options)
{
    Options o;
    for (const auto &i : options)
    {
        o.emplace_back(Qs(i.id), Qs(i.label));
    }
    return o;
}

Options EasingOptions()
{
    Options o;
    for (const auto &e : Easings())
    {
        o.emplace_back(Qs(e.id), Qs(e.label));
    }
    return o;
}

Options EffectPropertyOptions()
{
    static const std::pair<EffectProperty, const char *> kLabels[] = {
        {EffectProperty::Opacity, QT_TRANSLATE_NOOP("fields", "Прозрачность (×)")},
        {EffectProperty::Scale, QT_TRANSLATE_NOOP("fields", "Масштаб (×)")},
        {EffectProperty::Rotate, QT_TRANSLATE_NOOP("fields", "Поворот (°)")},
        {EffectProperty::OffsetX, QT_TRANSLATE_NOOP("fields", "Сдвиг X (px)")},
        {EffectProperty::OffsetY, QT_TRANSLATE_NOOP("fields", "Сдвиг Y (px)")},
        {EffectProperty::Glow, QT_TRANSLATE_NOOP("fields", "Свечение (0..1)")},
        {EffectProperty::Tint, QT_TRANSLATE_NOOP("fields", "Тонирование (0..1)")},
    };
    Options o;
    for (const auto &[p, label] : kLabels)
    {
        o.emplace_back(Qs(ToString(p)), QString::fromUtf8(label));
    }
    return o;
}

QLabel *Section(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName(QStringLiteral("inspectorSection"));
    return l;
}

QWidget *Hint(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName(QStringLiteral("hint"));
    l->setWordWrap(true);
    return l;
}

QWidget *Row(QWidget *a, QWidget *b)
{
    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(8);
    l->addWidget(a, 1);
    l->addWidget(b, 1);
    return w;
}

QWidget *Row(QWidget *a, QWidget *b, QWidget *c)
{
    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(8);
    l->addWidget(a, 1);
    l->addWidget(b, 1);
    l->addWidget(c, 1);
    return w;
}

QWidget *Labeled(const QString &label, QWidget *field)
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(3);
    auto *cap = new QLabel(label);
    cap->setObjectName(QStringLiteral("fieldLabel"));
    l->addWidget(cap);
    l->addWidget(field);
    return w;
}

QWidget *LineEdit(const QString &value, std::function<void(const QString &)> on_change, const QString &placeholder)
{
    auto *e = new QLineEdit(value);
    e->setPlaceholderText(placeholder);
    QObject::connect(e, &QLineEdit::textEdited, e,
                     [f = std::move(on_change)](const QString &v)
                     {
                         f(v);
                     });
    return e;
}

QWidget *Spin(double value, double min, double max, double step, int decimals, std::function<void(double)> on_change)
{
    auto *s = new QDoubleSpinBox;
    s->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    s->setMinimumWidth(56);
    s->setRange(min, max);
    s->setDecimals(decimals);
    s->setSingleStep(step);
    s->setValue(value);
    s->setFocusPolicy(Qt::StrongFocus);
    s->setKeyboardTracking(false);
    QObject::connect(s, &QDoubleSpinBox::valueChanged, s,
                     [f = std::move(on_change)](double v)
                     {
                         f(v);
                     });
    return s;
}

QWidget *OptSpin(std::optional<double> value, double min, double max, double step, int decimals, const QString &fallback,
                 std::function<void(std::optional<double>)> on_change)
{
    auto        *s     = new QDoubleSpinBox;
    const double unset = min - step;
    s->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    s->setMinimumWidth(56);
    s->setRange(unset, max);
    s->setDecimals(decimals);
    s->setSingleStep(step);
    s->setSpecialValueText(fallback.isEmpty() ? QObject::tr("авто") : QObject::tr("авто (%1)").arg(fallback));
    s->setValue(value.value_or(unset));
    s->setFocusPolicy(Qt::StrongFocus);
    s->setKeyboardTracking(false);
    QObject::connect(s, &QDoubleSpinBox::valueChanged, s,
                     [unset, step, f = std::move(on_change)](double v)
                     {
                         f(v <= unset + (step / 2) ? std::nullopt : std::optional<double>(v));
                     });
    return s;
}

QWidget *Combo(const Options &options, const QString &current, std::function<void(const QString &)> on_change)
{
    auto *c = new QComboBox;
    MakeShrinkable(c);
    for (const auto &[value, label] : options)
    {
        c->addItem(label, value);
    }
    const int idx = c->findData(current);
    c->setCurrentIndex(idx >= 0 ? idx : 0);
    c->setFocusPolicy(Qt::StrongFocus);
    QObject::connect(c, &QComboBox::activated, c,
                     [c, f = std::move(on_change)](int i)
                     {
                         f(c->itemData(i).toString());
                     });
    return c;
}

QWidget *OptCombo(const Options &options, const std::optional<std::string> &current, const QString &fallback,
                  std::function<void(std::optional<std::string>)> on_change)
{
    auto *c = new QComboBox;
    MakeShrinkable(c);
    c->addItem(fallback.isEmpty() ? QObject::tr("авто") : QObject::tr("авто (%1)").arg(fallback), QVariant());
    for (const auto &[value, label] : options)
    {
        c->addItem(label, value);
    }
    const int idx = current.has_value() ? c->findData(Qs(*current)) : 0;
    c->setCurrentIndex(idx >= 0 ? idx : 0);
    c->setFocusPolicy(Qt::StrongFocus);
    QObject::connect(c, &QComboBox::activated, c,
                     [c, f = std::move(on_change)](int i)
                     {
                         const QVariant v = c->itemData(i);
                         f(v.isValid() ? std::optional<std::string>(Us(v.toString())) : std::nullopt);
                     });
    return c;
}

QWidget *ColorButton(const QString &hex, std::function<void(const QString &)> on_change)
{
    auto *b = new QPushButton;
    b->setObjectName(QStringLiteral("colorButton"));
    PaintColorButton(b, hex, false);
    QObject::connect(b, &QPushButton::clicked, b,
                     [b, hex, f = std::move(on_change)]
                     {
                         const QColor picked = QColorDialog::getColor(QColor(hex), b, QObject::tr("Цвет"));
                         if (picked.isValid())
                         {
                             f(picked.name());
                         }
                     });
    return b;
}

QWidget *OptColor(const std::optional<std::string> &value, const QString &fallback,
                  std::function<void(std::optional<std::string>)> on_change)
{
    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(4);
    auto         *b   = new QPushButton;
    const QString hex = value.has_value() ? Qs(*value) : fallback;
    b->setObjectName(QStringLiteral("colorButton"));
    PaintColorButton(b, hex.isEmpty() ? QStringLiteral("#000000") : hex, !value.has_value());
    auto *reset = new QPushButton(QStringLiteral("✕"));
    reset->setFixedWidth(26);
    reset->setToolTip(QObject::tr("Сбросить (наследовать)"));
    reset->setEnabled(value.has_value());
    l->addWidget(b, 1);
    l->addWidget(reset);
    auto f = std::make_shared<std::function<void(std::optional<std::string>)>>(std::move(on_change));
    QObject::connect(b, &QPushButton::clicked, b,
                     [b, hex, f]
                     {
                         const QColor picked = QColorDialog::getColor(QColor(hex), b, QObject::tr("Цвет"), QColorDialog::ShowAlphaChannel);
                         if (picked.isValid())
                         {
                             (*f)(Us(picked.alpha() == 255 ? picked.name() : picked.name(QColor::HexArgb)));
                         }
                     });
    QObject::connect(reset, &QPushButton::clicked, reset,
                     [f]
                     {
                         (*f)(std::nullopt);
                     });
    return w;
}

QWidget *Slider(double value, double min, double max, double step, std::function<void(double)> on_change)
{
    auto     *s     = new QSlider(Qt::Horizontal);
    const int steps = static_cast<int>(std::lround((max - min) / step));
    s->setRange(0, steps);
    s->setValue(static_cast<int>(std::lround((value - min) / step)));
    s->setFocusPolicy(Qt::StrongFocus);
    QObject::connect(s, &QSlider::valueChanged, s,
                     [min, step, f = std::move(on_change)](int v)
                     {
                         f(min + (v * step));
                     });
    return s;
}

QWidget *Check(const QString &label, bool value, std::function<void(bool)> on_change)
{
    auto *c = new QCheckBox(label);
    c->setChecked(value);
    QObject::connect(c, &QCheckBox::toggled, c,
                     [f = std::move(on_change)](bool v)
                     {
                         f(v);
                     });
    return c;
}

QWidget *OptCheck(const QString &label, std::optional<bool> value, bool fallback, std::function<void(std::optional<bool>)> on_change)
{
    auto *c = new QCheckBox(value.has_value() ? label
                                              : QObject::tr("%1 (авто: %2)").arg(label, fallback ? QObject::tr("да") : QObject::tr("нет")));
    c->setTristate(true);
    c->setCheckState(!value.has_value() ? Qt::PartiallyChecked : (*value ? Qt::Checked : Qt::Unchecked));
    c->setToolTip(QObject::tr("Промежуточное состояние — наследовать значение"));
    QObject::connect(c, &QCheckBox::stateChanged, c,
                     [f = std::move(on_change)](int state)
                     {
                         f(state == Qt::PartiallyChecked ? std::nullopt : std::optional<bool>(state == Qt::Checked));
                     });
    return c;
}

QWidget *Button(const QString &text, std::function<void()> on_click, bool danger)
{
    auto *b = new QPushButton(text);
    if (danger)
    {
        b->setObjectName(QStringLiteral("dangerButton"));
    }
    QObject::connect(b, &QPushButton::clicked, b,
                     [f = std::move(on_click)]
                     {
                         f();
                     });
    return b;
}

QDialogButtonBox *OkCancelButtons(QDialog *dlg)
{
    auto *box = new QDialogButtonBox;
    auto *ok  = box->addButton(QObject::tr("OK"), QDialogButtonBox::AcceptRole);
    box->addButton(QObject::tr("Отмена"), QDialogButtonBox::RejectRole);
    ok->setDefault(true);
    QObject::connect(box, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    return box;
}

void AddNodeStyleFields(QVBoxLayout *box, const NodeStyle &current, const NodeStyle &base, const StyleEdit &edit)
{
    auto str = [](const std::optional<std::string> &v, const char *def = "")
    {
        return Qs(v.value_or(def));
    };
    auto num = [](const std::optional<double> &v, double def)
    {
        return QString::number(v.value_or(def));
    };

    box->addWidget(Labeled(QObject::tr("Форма"), OptCombo(FromCatalog(Shapes()), current.shape, str(base.shape, "rounded"),
                                                          [edit](std::optional<std::string> v)
                                                          {
                                                              edit({},
                                                                   [&](NodeStyle &s)
                                                                   {
                                                                       s.shape = std::move(v);
                                                                   });
                                                          })));
    if (current.shape.value_or(base.shape.value_or("")) == "custom")
    {
        box->addWidget(Labeled(QObject::tr("Контур (SVG path в квадрате 0..1)"),
                               LineEdit(
                                   str(current.custom_path),
                                   [edit](const QString &v)
                                   {
                                       edit("style:path",
                                            [&](NodeStyle &s)
                                            {
                                                s.custom_path = v.isEmpty() ? std::nullopt : std::optional<std::string>(Us(v));
                                            });
                                   },
                                   str(base.custom_path, "M0.5 0 L1 0.5 L0.5 1 L0 0.5 Z"))));
    }
    box->addWidget(
        Row(Labeled(QObject::tr("Заливка"), OptColor(current.fill, base.fill.has_value() ? Qs(*base.fill) : Qs(NodeState("ok").fill.Hex()),
                                                     [edit](std::optional<std::string> v)
                                                     {
                                                         edit({},
                                                              [&](NodeStyle &s)
                                                              {
                                                                  s.fill = std::move(v);
                                                              });
                                                     })),
            Labeled(QObject::tr("Обводка"),
                    OptColor(current.stroke, base.stroke.has_value() ? Qs(*base.stroke) : Qs(NodeState("ok").ring.Hex()),
                             [edit](std::optional<std::string> v)
                             {
                                 edit({},
                                      [&](NodeStyle &s)
                                      {
                                          s.stroke = std::move(v);
                                      });
                             }))));
    box->addWidget(
        Row(Labeled(QObject::tr("Толщина обводки"), OptSpin(current.stroke_width, 0, 20, 0.5, 1, num(base.stroke_width, 2),
                                                            [edit](std::optional<double> v)
                                                            {
                                                                edit("style:stroke_width",
                                                                     [&](NodeStyle &s)
                                                                     {
                                                                         s.stroke_width = v;
                                                                     });
                                                            })),
            Labeled(QObject::tr("Линия"), OptCombo(FromCatalog(StrokeStyles()), current.stroke_style, str(base.stroke_style, "solid"),
                                                   [edit](std::optional<std::string> v)
                                                   {
                                                       edit({},
                                                            [&](NodeStyle &s)
                                                            {
                                                                s.stroke_style = std::move(v);
                                                            });
                                                   }))));
    box->addWidget(Row(Labeled(QObject::tr("Скругление"),
                               OptSpin(current.corner_radius, 0, 100, 1, 0,
                                       num(base.corner_radius, DefaultCornerRadius(current.shape.value_or(base.shape.value_or("rounded")))),
                                       [edit](std::optional<double> v)
                                       {
                                           edit("style:corner_radius",
                                                [&](NodeStyle &s)
                                                {
                                                    s.corner_radius = v;
                                                });
                                       })),
                       Labeled(QObject::tr("Непрозрачность"), OptSpin(current.opacity, 0, 1, 0.05, 2, num(base.opacity, 1),
                                                                      [edit](std::optional<double> v)
                                                                      {
                                                                          edit("style:opacity",
                                                                               [&](NodeStyle &s)
                                                                               {
                                                                                   s.opacity = v;
                                                                               });
                                                                      }))));
    box->addWidget(Row(Labeled(QObject::tr("Цвет текста"), OptColor(current.text_color, str(base.text_color, "#f2f6ff"),
                                                                    [edit](std::optional<std::string> v)
                                                                    {
                                                                        edit({},
                                                                             [&](NodeStyle &s)
                                                                             {
                                                                                 s.text_color = std::move(v);
                                                                             });
                                                                    })),
                       Labeled(QObject::tr("Размер шрифта"), OptSpin(current.font_size, 6, 72, 1, 0, num(base.font_size, 15),
                                                                     [edit](std::optional<double> v)
                                                                     {
                                                                         edit("style:font_size",
                                                                              [&](NodeStyle &s)
                                                                              {
                                                                                  s.font_size = v;
                                                                              });
                                                                     }))));
    box->addWidget(Row(OptCheck(QObject::tr("Тень"), current.shadow, base.shadow.value_or(true),
                                [edit](std::optional<bool> v)
                                {
                                    edit({},
                                         [&](NodeStyle &s)
                                         {
                                             s.shadow = v;
                                         });
                                }),
                       OptCheck(QObject::tr("Иконка"), current.show_icon, base.show_icon.value_or(true),
                                [edit](std::optional<bool> v)
                                {
                                    edit({},
                                         [&](NodeStyle &s)
                                         {
                                             s.show_icon = v;
                                         });
                                })));
}

} // namespace ad::ui::fields
