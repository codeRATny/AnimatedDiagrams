#include "LibraryDialog.hpp"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#include "Controller.hpp"
#include "Engine/Templates.hpp"
#include "Fields.hpp"
#include "Plugins/Plugin.hpp"
#include "PreviewWidget.hpp"
#include "QtRender.hpp"
#include "Timeline/TimelineLayout.hpp"
#include "Utils/File.hpp"
#include "Utils/Text.hpp"

namespace ad::ui
{

using namespace fields;

namespace
{

constexpr int kIdRole     = Qt::UserRole;
constexpr int kSourceRole = Qt::UserRole + 1;

QString SourceName(const std::string &source)
{
    if (source == kBuiltinSource)
    {
        return QObject::tr("встроенный");
    }
    if (source == kDocumentSource)
    {
        return QObject::tr("документ");
    }
    return QObject::tr("плагин %1").arg(Qs(source));
}

template <class T>
QString ItemText(const T &def)
{
    return Qs(def.category) + QStringLiteral(" · ") + Qs(def.label);
}

template <class T>
T *FindById(std::vector<T> &v, const std::string &id)
{
    const auto it = std::ranges::find(v, id, &T::id);
    return it != v.end() ? &*it : nullptr;
}

/// Value range of an effect property in the keyframe editor.
struct PropertyRange
{
    double min;
    double max;
    double step;
    int    decimals;
};

PropertyRange RangeOf(EffectProperty p)
{
    switch (p)
    {
    case EffectProperty::Rotate:
        return {-1440, 1440, 5, 0};
    case EffectProperty::OffsetX:
    case EffectProperty::OffsetY:
        return {-1000, 1000, 1, 0};
    case EffectProperty::Scale:
        return {0, 10, 0.05, 2};
    case EffectProperty::Opacity:
    case EffectProperty::Glow:
    case EffectProperty::Tint:
        return {0, 1, 0.05, 2};
    }
    return {-1000, 1000, 0.05, 2};
}

QWidget *RowOf(std::initializer_list<std::pair<QWidget *, int>> items)
{
    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(6);
    for (const auto &[widget, stretch] : items)
    {
        l->addWidget(widget, stretch);
    }
    return w;
}

QPushButton *SmallButton(const QString &text, const QString &tip, std::function<void()> fn, bool danger = false)
{
    auto *b = qobject_cast<QPushButton *>(Button(text, std::move(fn), danger));
    b->setToolTip(tip);
    b->setFixedWidth(34);
    return b;
}

} // namespace

LibraryDialog::LibraryDialog(Controller &ctl, QWidget *parent) : QDialog(parent), _ctl(ctl)
{
    setWindowTitle(tr("Библиотека"));
    resize(1080, 700);

    _tabs                  = new QTabWidget;
    const QString titles[] = {tr("Элементы"), tr("Эффекты"), tr("Анимации")};
    for (int i = 0; i < 3; ++i)
    {
        _lists[i] = new QListWidget;
        _lists[i]->setSelectionMode(QAbstractItemView::SingleSelection);
        connect(_lists[i], &QListWidget::currentItemChanged, this, &LibraryDialog::_OnSelectionChanged);
        _tabs->addTab(_lists[i], titles[i]);
    }
    connect(_tabs, &QTabWidget::currentChanged, this,
            [this]
            {
                _OnSelectionChanged();
            });

    _new_button   = new QPushButton(tr("＋ Создать"));
    _dup_button   = new QPushButton(tr("⧉ Дублировать"));
    _del_button   = new QPushButton(tr("Удалить"));
    _apply_button = new QPushButton(tr("▶ Применить к диаграмме…"));
    _del_button->setObjectName(QStringLiteral("dangerButton"));
    connect(_new_button, &QPushButton::clicked, this, &LibraryDialog::_New);
    connect(_dup_button, &QPushButton::clicked, this, &LibraryDialog::_Duplicate);
    connect(_del_button, &QPushButton::clicked, this, &LibraryDialog::_Delete);
    connect(_apply_button, &QPushButton::clicked, this,
            [this]
            {
                Q_EMIT ApplyAnimationRequested(Qs(_current.id));
            });

    auto *left   = new QWidget;
    auto *left_l = new QVBoxLayout(left);
    left_l->setContentsMargins(0, 0, 0, 0);
    left_l->addWidget(_tabs, 1);
    left_l->addWidget(RowOf({{_new_button, 1}, {_dup_button, 1}}));
    left_l->addWidget(RowOf({{_del_button, 1}, {_apply_button, 2}}));

    _origin = new QLabel;
    _origin->setObjectName(QStringLiteral("hint"));
    _origin->setWordWrap(true);
    _editor = new QScrollArea;
    _editor->setWidgetResizable(true);
    _editor->setFrameShape(QFrame::NoFrame);
    _editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *center   = new QWidget;
    auto *center_l = new QVBoxLayout(center);
    center_l->setContentsMargins(0, 0, 0, 0);
    center_l->addWidget(_origin);
    center_l->addWidget(_editor, 1);

    _preview         = new PreviewWidget;
    auto *right      = new QWidget;
    auto *right_l    = new QVBoxLayout(right);
    auto *prev_title = new QLabel(tr("ПРЕДПРОСМОТР"));
    prev_title->setObjectName(QStringLiteral("panelTitle"));
    right_l->setContentsMargins(0, 0, 0, 0);
    right_l->addWidget(prev_title);
    right_l->addWidget(_preview, 1);

    auto *split = new QSplitter;
    split->addWidget(left);
    split->addWidget(center);
    split->addWidget(right);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    split->setStretchFactor(2, 3);
    split->setSizes({260, 420, 380});

    auto *export_button = new QPushButton(tr("Экспорт в плагин…"));
    connect(export_button, &QPushButton::clicked, this, &LibraryDialog::_ExportPlugin);
    auto *buttons = new QDialogButtonBox;
    buttons->addButton(new QPushButton(tr("Закрыть")), QDialogButtonBox::RejectRole);
    buttons->addButton(export_button, QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(split, 1);
    layout->addWidget(buttons);

    connect(&_ctl, &Controller::LibraryChanged, this,
            [this]
            {
                _RefreshLists();
                _RequestEditorRebuild();
            });
    connect(&_ctl, &Controller::ModelChanged, this,
            [this](bool structural)
            {
                if (!_applying && structural)
                {
                    _RefreshLists(); // undo / redo / MCP edits of the document library
                    _RequestEditorRebuild();
                }
            });
    _RefreshLists();
}

// ---------------------------------------------------------------------------
// Lists and selection
// ---------------------------------------------------------------------------

LibraryDialog::Tab LibraryDialog::_CurrentTab() const { return static_cast<Tab>(_tabs->currentIndex()); }

QListWidget *LibraryDialog::_List(Tab tab) const { return _lists[static_cast<int>(tab)]; }

bool LibraryDialog::_Editable() const { return !_current.id.empty() && _current.source == kDocumentSource; }

std::string LibraryDialog::_UniqueId(const std::string &base) const
{
    const auto &lib   = _ctl.GetModel().library;
    const auto  taken = [&](const std::string &id)
    {
        return _ctl.Reg().FindElement(id, &lib) != nullptr || _ctl.Reg().FindEffect(id, &lib) != nullptr ||
               _ctl.Reg().FindAnimation(id, &lib) != nullptr;
    };
    if (!taken(base))
    {
        return base;
    }
    for (int i = 2;; ++i)
    {
        std::string id = base + "-" + std::to_string(i);
        if (!taken(id))
        {
            return id;
        }
    }
}

void LibraryDialog::_RefreshLists()
{
    const auto &lib  = _ctl.GetModel().library;
    const auto &reg  = _ctl.Reg();
    auto        fill = [&](QListWidget *list, const auto &entries)
    {
        const QString        selected = list->currentItem() != nullptr ? list->currentItem()->data(kIdRole).toString() : QString();
        const QSignalBlocker block(list);
        list->clear();
        for (const auto &e : entries)
        {
            auto *item = new QListWidgetItem(ItemText(*e.def), list);
            item->setData(kIdRole, Qs(e.def->id));
            item->setData(kSourceRole, Qs(e.source));
            item->setToolTip(QStringLiteral("%1\n%2").arg(Qs(e.def->id), SourceName(e.source)));
            if (e.source == kDocumentSource)
            {
                QFont f = item->font();
                f.setBold(true);
                item->setFont(f);
            }
            if (item->data(kIdRole).toString() == selected)
            {
                list->setCurrentItem(item);
            }
        }
        if (list->currentItem() == nullptr && list->count() > 0)
        {
            list->setCurrentRow(0);
        }
    };
    fill(_lists[0], reg.Elements(&lib));
    fill(_lists[1], reg.Effects(&lib));
    fill(_lists[2], reg.Animations(&lib));
    _OnSelectionChanged();
}

void LibraryDialog::SelectItem(const QString &id)
{
    _RefreshLists();
    for (int i = 0; i < 3; ++i)
    {
        for (int row = 0; row < _lists[i]->count(); ++row)
        {
            if (_lists[i]->item(row)->data(kIdRole).toString() == id)
            {
                _tabs->setCurrentIndex(i);
                _lists[i]->setCurrentRow(row);
                return;
            }
        }
    }
}

void LibraryDialog::_OnSelectionChanged()
{
    const QListWidgetItem *item = _List(_CurrentTab())->currentItem();
    Current                next;
    if (item != nullptr)
    {
        next = {Us(item->data(kIdRole).toString()), Us(item->data(kSourceRole).toString())};
    }
    if (next.id == _current.id && next.source == _current.source && _editor->widget() != nullptr)
    {
        _UpdateButtons();
        return;
    }
    _current = next;
    _RebuildEditor();
}

void LibraryDialog::_UpdateButtons()
{
    _dup_button->setEnabled(!_current.id.empty());
    _del_button->setEnabled(_Editable());
    _apply_button->setVisible(_CurrentTab() == Tab::Animations);
    _apply_button->setEnabled(!_current.id.empty() && !_ctl.GetModel().nodes.empty());
}

void LibraryDialog::_RequestEditorRebuild()
{
    if (_rebuild_pending)
    {
        return;
    }
    _rebuild_pending = true;
    QTimer::singleShot(0, this,
                       [this]
                       {
                           _rebuild_pending = false;
                           _RebuildEditor();
                       });
}

// ---------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------

void LibraryDialog::_RebuildEditor()
{
    const int scroll  = _editor->verticalScrollBar()->value();
    auto     *content = new QWidget;
    auto     *outer   = new QVBoxLayout(content);
    outer->setContentsMargins(12, 6, 12, 12);
    outer->setSpacing(8);

    // read-only definitions are shown in a disabled form
    auto *form = new QWidget;
    auto *box  = new QVBoxLayout(form);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(8);
    outer->addWidget(form);
    outer->addStretch(1);

    const auto &lib   = _ctl.GetModel().library;
    const auto &reg   = _ctl.Reg();
    bool        found = false;
    switch (_CurrentTab())
    {
    case Tab::Elements:
        if (const ElementType *e = reg.FindElement(_current.id, &lib); e != nullptr)
        {
            _BuildElementEditor(box, ElementType(*e));
            found = true;
        }
        break;
    case Tab::Effects:
        if (const EffectDef *e = reg.FindEffect(_current.id, &lib); e != nullptr)
        {
            _BuildEffectEditor(box, EffectDef(*e));
            found = true;
        }
        break;
    case Tab::Animations:
        if (const AnimationTemplate *a = reg.FindAnimation(_current.id, &lib); a != nullptr)
        {
            _BuildAnimationEditor(box, AnimationTemplate(*a));
            found = true;
        }
        break;
    }
    if (!found)
    {
        box->addWidget(Hint(tr("Выберите определение слева или создайте новое.")));
        _origin->clear();
    }
    else if (_Editable())
    {
        _origin->setText(tr("Источник: документ — изменения сохраняются вместе с диаграммой и отменяются через Ctrl+Z."));
    }
    else
    {
        form->setEnabled(false);
        _origin->setText(tr("Источник: %1 — только чтение. Нажмите «Дублировать», чтобы создать редактируемую копию в документе.")
                             .arg(SourceName(_current.source)));
    }

    QWidget *old = _editor->takeWidget();
    _editor->setWidget(content);
    if (old != nullptr)
    {
        old->deleteLater();
    }
    QTimer::singleShot(0, this,
                       [this, scroll]
                       {
                           _editor->verticalScrollBar()->setValue(scroll);
                       });
    _UpdatePreview();
    _UpdateButtons();
}

void LibraryDialog::_UpdatePreview()
{
    const auto &lib = _ctl.GetModel().library;
    const auto &reg = _ctl.Reg();
    switch (_CurrentTab())
    {
    case Tab::Elements:
        if (const ElementType *e = reg.FindElement(_current.id, &lib); e != nullptr)
        {
            _preview->ShowElement(*e, reg);
            return;
        }
        break;
    case Tab::Effects:
        if (const EffectDef *e = reg.FindEffect(_current.id, &lib); e != nullptr)
        {
            _preview->ShowEffect(*e, reg);
            return;
        }
        break;
    case Tab::Animations:
        if (const AnimationTemplate *a = reg.FindAnimation(_current.id, &lib); a != nullptr)
        {
            _preview->ShowAnimation(*a, reg);
            return;
        }
        break;
    }
    _preview->Clear();
}

void LibraryDialog::_AfterEdit(bool rebuild)
{
    // refresh the list caption in place (the list is not rebuilt to keep the focus)
    if (QListWidgetItem *item = _List(_CurrentTab())->currentItem(); item != nullptr)
    {
        const auto &lib = _ctl.GetModel().library;
        if (const auto *e = lib.Element(_current.id); e != nullptr && _CurrentTab() == Tab::Elements)
        {
            item->setText(ItemText(*e));
        }
        else if (const auto *f = lib.Effect(_current.id); f != nullptr && _CurrentTab() == Tab::Effects)
        {
            item->setText(ItemText(*f));
        }
        else if (const auto *a = lib.Animation(_current.id); a != nullptr && _CurrentTab() == Tab::Animations)
        {
            item->setText(ItemText(*a));
        }
    }
    _UpdatePreview();
    if (rebuild)
    {
        _RequestEditorRebuild();
    }
}

void LibraryDialog::_EditElement(const std::string &key, const std::function<void(ElementType &)> &fn, bool rebuild)
{
    const std::string id = _current.id;
    _applying            = true;
    _ctl.Edit(
        key.empty() ? std::string() : "lib:" + id + ":" + key,
        [&](Model &m)
        {
            if (ElementType *e = FindById(m.library.elements, id); e != nullptr)
            {
                fn(*e);
            }
        },
        true);
    _applying = false;
    _AfterEdit(rebuild);
}

void LibraryDialog::_EditEffect(const std::string &key, const std::function<void(EffectDef &)> &fn, bool rebuild)
{
    const std::string id = _current.id;
    _applying            = true;
    _ctl.Edit(
        key.empty() ? std::string() : "lib:" + id + ":" + key,
        [&](Model &m)
        {
            if (EffectDef *e = FindById(m.library.effects, id); e != nullptr)
            {
                fn(*e);
            }
        },
        true);
    _applying = false;
    _AfterEdit(rebuild);
}

void LibraryDialog::_EditAnimation(const std::string &key, const std::function<void(AnimationTemplate &)> &fn, bool rebuild)
{
    const std::string id = _current.id;
    _applying            = true;
    _ctl.Edit(
        key.empty() ? std::string() : "lib:" + id + ":" + key,
        [&](Model &m)
        {
            if (AnimationTemplate *a = FindById(m.library.animations, id); a != nullptr)
            {
                fn(*a);
            }
        },
        true);
    _applying = false;
    _AfterEdit(rebuild);
}

// ---------------------------------------------------------------------------
// Element editor
// ---------------------------------------------------------------------------

void LibraryDialog::_BuildElementEditor(QVBoxLayout *box, const ElementType &e)
{
    auto *title = new QLabel(tr("Элемент «%1»").arg(Qs(e.label)));
    title->setObjectName(QStringLiteral("inspectorTitle"));
    box->addWidget(title);
    box->addWidget(Hint(tr("Идентификатор: %1").arg(Qs(e.id))));

    auto text = [this](const std::string &key, std::string ElementType::*field)
    {
        return [this, key, field](const QString &v)
        {
            _EditElement(key,
                         [&](ElementType &t)
                         {
                             t.*field = Us(v);
                         });
        };
    };
    box->addWidget(Labeled(tr("Название"), LineEdit(Qs(e.label), text("label", &ElementType::label))));
    box->addWidget(Row(Labeled(tr("Иконка (символ)"), LineEdit(Qs(e.icon), text("icon", &ElementType::icon), QStringLiteral("⚙"))),
                       Labeled(tr("Категория"), LineEdit(Qs(e.category), text("category", &ElementType::category)))));
    box->addWidget(Labeled(tr("Описание"), LineEdit(Qs(e.description), text("description", &ElementType::description))));
    box->addWidget(Row(Labeled(tr("Акцент"), ColorButton(Qs(e.accent),
                                                         [this](const QString &v)
                                                         {
                                                             _EditElement(
                                                                 {},
                                                                 [&](ElementType &t)
                                                                 {
                                                                     t.accent = Us(v);
                                                                 },
                                                                 true);
                                                         })),
                       Row(Labeled(tr("Ширина"), Spin(e.width, 20, 2000, 10, 0,
                                                      [this](double v)
                                                      {
                                                          _EditElement("width",
                                                                       [&](ElementType &t)
                                                                       {
                                                                           t.width = v;
                                                                       });
                                                      })),
                           Labeled(tr("Высота"), Spin(e.height, 20, 2000, 10, 0,
                                                      [this](double v)
                                                      {
                                                          _EditElement("height",
                                                                       [&](ElementType &t)
                                                                       {
                                                                           t.height = v;
                                                                       });
                                                      })))));
    box->addWidget(Section(tr("Стиль по умолчанию")));
    AddNodeStyleFields(box, e.style, {},
                       [this](const std::string &merge_key, const std::function<void(NodeStyle &)> &fn)
                       {
                           _EditElement(
                               merge_key,
                               [&](ElementType &t)
                               {
                                   fn(t.style);
                               },
                               merge_key.empty());
                       });
    box->addWidget(Hint(tr("Стиль узла на холсте может переопределить любое из этих полей. "
                           "Форма «custom» использует SVG-контур в квадрате 0..1 (например «M0 0 L1 0 L0.5 1 Z»).")));
}

// ---------------------------------------------------------------------------
// Effect editor
// ---------------------------------------------------------------------------

void LibraryDialog::_BuildEffectEditor(QVBoxLayout *box, const EffectDef &e)
{
    auto *title = new QLabel(tr("Эффект «%1»").arg(Qs(e.label)));
    title->setObjectName(QStringLiteral("inspectorTitle"));
    box->addWidget(title);
    box->addWidget(Hint(tr("Идентификатор: %1").arg(Qs(e.id))));

    auto text = [this](const std::string &key, std::string EffectDef::*field)
    {
        return [this, key, field](const QString &v)
        {
            _EditEffect(key,
                        [&](EffectDef &t)
                        {
                            t.*field = Us(v);
                        });
        };
    };
    box->addWidget(Labeled(tr("Название"), LineEdit(Qs(e.label), text("label", &EffectDef::label))));
    box->addWidget(Row(Labeled(tr("Категория"), LineEdit(Qs(e.category), text("category", &EffectDef::category))),
                       Labeled(tr("Цвет"), ColorButton(Qs(e.color),
                                                       [this](const QString &v)
                                                       {
                                                           _EditEffect(
                                                               {},
                                                               [&](EffectDef &t)
                                                               {
                                                                   t.color = Us(v);
                                                               },
                                                               true);
                                                       }))));
    box->addWidget(Labeled(tr("Описание"), LineEdit(Qs(e.description), text("description", &EffectDef::description))));
    box->addWidget(Labeled(tr("Повторов за длительность шага"), Spin(e.repeat, 1, 100, 1, 0,
                                                                     [this](double v)
                                                                     {
                                                                         _EditEffect("repeat",
                                                                                     [&](EffectDef &t)
                                                                                     {
                                                                                         t.repeat = static_cast<int>(v);
                                                                                     });
                                                                     })));

    box->addWidget(Section(tr("Дорожки ключевых кадров")));
    box->addWidget(Hint(tr("Каждая дорожка анимирует одно свойство узла. t — доля одного повтора (0..1), "
                           "кривая задаёт переход от предыдущего ключа к этому.")));
    for (size_t ti = 0; ti < e.tracks.size(); ++ti)
    {
        const EffectTrack  &track = e.tracks[ti];
        const PropertyRange range = RangeOf(track.property);
        box->addWidget(RowOf({{Combo(EffectPropertyOptions(), Qs(ToString(track.property)),
                                     [this, ti](const QString &v)
                                     {
                                         _EditEffect(
                                             {},
                                             [&](EffectDef &t)
                                             {
                                                 if (ti < t.tracks.size())
                                                 {
                                                     t.tracks[ti].property =
                                                         EffectPropertyFromString(Us(v)).value_or(t.tracks[ti].property);
                                                 }
                                             },
                                             true);
                                     }),
                               1},
                              {SmallButton(
                                   QStringLiteral("✕"), tr("Удалить дорожку"),
                                   [this, ti]
                                   {
                                       _EditEffect(
                                           {},
                                           [&](EffectDef &t)
                                           {
                                               if (ti < t.tracks.size())
                                               {
                                                   t.tracks.erase(t.tracks.begin() + static_cast<std::ptrdiff_t>(ti));
                                               }
                                           },
                                           true);
                                   },
                                   true),
                               0}}));
        for (size_t ki = 0; ki < track.keys.size(); ++ki)
        {
            const Keyframe &k   = track.keys[ki];
            auto            key = [ti, ki](EffectDef &t) -> Keyframe *
            {
                if (ti < t.tracks.size() && ki < t.tracks[ti].keys.size())
                {
                    return &t.tracks[ti].keys[ki];
                }
                return nullptr;
            };
            const std::string merge = "track" + std::to_string(ti) + "key" + std::to_string(ki);
            box->addWidget(RowOf({{new QLabel(QStringLiteral("   t")), 0},
                                  {Spin(k.t, 0, 1, 0.05, 2,
                                        [this, key](double v)
                                        {
                                            _EditEffect(
                                                {},
                                                [&](EffectDef &t)
                                                {
                                                    if (Keyframe *kf = key(t); kf != nullptr)
                                                    {
                                                        kf->t = v;
                                                    }
                                                    for (auto &tr : t.tracks)
                                                    {
                                                        std::ranges::stable_sort(tr.keys, {}, &Keyframe::t);
                                                    }
                                                },
                                                true);
                                        }),
                                   2},
                                  {Spin(k.value, range.min, range.max, range.step, range.decimals,
                                        [this, key, merge](double v)
                                        {
                                            _EditEffect(merge + "value",
                                                        [&](EffectDef &t)
                                                        {
                                                            if (Keyframe *kf = key(t); kf != nullptr)
                                                            {
                                                                kf->value = v;
                                                            }
                                                        });
                                        }),
                                   2},
                                  {Combo(EasingOptions(), Qs(ToString(k.easing)),
                                         [this, key](const QString &v)
                                         {
                                             _EditEffect({},
                                                         [&](EffectDef &t)
                                                         {
                                                             if (Keyframe *kf = key(t); kf != nullptr)
                                                             {
                                                                 kf->easing = EasingFromString(Us(v)).value_or(kf->easing);
                                                             }
                                                         });
                                         }),
                                   3},
                                  {SmallButton(QStringLiteral("−"), tr("Удалить ключ"),
                                               [this, ti, ki]
                                               {
                                                   _EditEffect(
                                                       {},
                                                       [&](EffectDef &t)
                                                       {
                                                           if (ti < t.tracks.size() && ki < t.tracks[ti].keys.size())
                                                           {
                                                               auto &keys = t.tracks[ti].keys;
                                                               keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(ki));
                                                           }
                                                       },
                                                       true);
                                               }),
                                   0}}));
        }
        box->addWidget(Button(tr("＋ Ключ"),
                              [this, ti]
                              {
                                  _EditEffect(
                                      {},
                                      [&](EffectDef &t)
                                      {
                                          if (ti >= t.tracks.size())
                                          {
                                              return;
                                          }
                                          auto  &keys = t.tracks[ti].keys;
                                          double at   = 1;
                                          double v    = DefaultValue(t.tracks[ti].property);
                                          if (!keys.empty())
                                          {
                                              // halfway between the last two keys or at the end
                                              at = keys.size() >= 2 ? (keys[keys.size() - 2].t + keys.back().t) / 2 : 1;
                                              v  = keys.back().value;
                                          }
                                          keys.push_back({at, v, Easing::EaseInOut});
                                          std::ranges::stable_sort(keys, {}, &Keyframe::t);
                                      },
                                      true);
                              }));
    }
    box->addWidget(Button(tr("＋ Дорожка"),
                          [this]
                          {
                              _EditEffect(
                                  {},
                                  [](EffectDef &t)
                                  {
                                      EffectProperty prop = EffectProperty::Scale;
                                      for (const EffectProperty p : EffectProperties())
                                      {
                                          if (std::ranges::none_of(t.tracks,
                                                                   [p](const EffectTrack &tr)
                                                                   {
                                                                       return tr.property == p;
                                                                   }))
                                          {
                                              prop = p;
                                              break;
                                          }
                                      }
                                      const double d = DefaultValue(prop);
                                      t.tracks.push_back(
                                          {prop, {{0, d, Easing::Linear}, {0.5, d, Easing::EaseOut}, {1, d, Easing::EaseIn}}});
                                  },
                                  true);
                          }));
}

// ---------------------------------------------------------------------------
// Animation editor
// ---------------------------------------------------------------------------

void LibraryDialog::_BuildAnimationEditor(QVBoxLayout *box, const AnimationTemplate &a)
{
    auto *title = new QLabel(tr("Анимация «%1»").arg(Qs(a.label)));
    title->setObjectName(QStringLiteral("inspectorTitle"));
    box->addWidget(title);
    box->addWidget(Hint(tr("Идентификатор: %1").arg(Qs(a.id))));

    auto text = [this](const std::string &key, std::string AnimationTemplate::*field)
    {
        return [this, key, field](const QString &v)
        {
            _EditAnimation(key,
                           [&](AnimationTemplate &t)
                           {
                               t.*field = Us(v);
                           });
        };
    };
    box->addWidget(Labeled(tr("Название"), LineEdit(Qs(a.label), text("label", &AnimationTemplate::label))));
    box->addWidget(Labeled(tr("Категория"), LineEdit(Qs(a.category), text("category", &AnimationTemplate::category))));
    box->addWidget(Labeled(tr("Описание"), LineEdit(Qs(a.description), text("description", &AnimationTemplate::description))));

    box->addWidget(Section(tr("Роли")));
    box->addWidget(Hint(tr("Роли — места для узлов. При применении каждой роли назначается узел диаграммы.")));
    for (size_t i = 0; i < a.roles.size(); ++i)
    {
        auto *id_label = new QLabel(Qs(a.roles[i].id));
        id_label->setMinimumWidth(80);
        box->addWidget(RowOf({{id_label, 0},
                              {LineEdit(Qs(a.roles[i].label),
                                        [this, i](const QString &v)
                                        {
                                            _EditAnimation("role" + std::to_string(i),
                                                           [&](AnimationTemplate &t)
                                                           {
                                                               if (i < t.roles.size())
                                                               {
                                                                   t.roles[i].label = Us(v);
                                                               }
                                                           });
                                        }),
                               1}}));
    }

    box->addWidget(Section(tr("Шаги (время от начала, мс)")));
    for (size_t i = 0; i < a.steps.size(); ++i)
    {
        const Step &s = a.steps[i];
        QString     what;
        switch (s.type)
        {
        case StepType::Message:
            what = QStringLiteral("%1 → %2").arg(Qs(s.from), Qs(s.to));
            break;
        case StepType::Link:
            what = Qs(s.edge_id);
            break;
        case StepType::Note:
            what = Qs(s.text);
            break;
        default:
            what = Qs(s.node_id);
            break;
        }
        if (s.type == StepType::Effect)
        {
            what += QStringLiteral(" · ") + Qs(s.effect);
        }
        else if (!s.label.empty())
        {
            what += QStringLiteral(" · ") + Qs(s.label);
        }
        auto *caption = new QLabel(QStringLiteral("%1: %2").arg(Qs(StepTypeLabel(s.type)), what));
        caption->setToolTip(caption->text());
        caption->setMinimumWidth(120);
        box->addWidget(RowOf({{caption, 3},
                              {Spin(s.start, 0, 3'600'000, 50, 0,
                                    [this, i](double v)
                                    {
                                        _EditAnimation("start" + std::to_string(i),
                                                       [&](AnimationTemplate &t)
                                                       {
                                                           if (i < t.steps.size())
                                                           {
                                                               t.steps[i].start = v;
                                                           }
                                                       });
                                    }),
                               2},
                              {Spin(s.duration, kMinStepDuration, 3'600'000, 50, 0,
                                    [this, i](double v)
                                    {
                                        _EditAnimation("duration" + std::to_string(i),
                                                       [&](AnimationTemplate &t)
                                                       {
                                                           if (i < t.steps.size())
                                                           {
                                                               t.steps[i].duration = v;
                                                           }
                                                       });
                                    }),
                               2},
                              {SmallButton(
                                   QStringLiteral("✕"), tr("Удалить шаг"),
                                   [this, i]
                                   {
                                       _EditAnimation(
                                           {},
                                           [&](AnimationTemplate &t)
                                           {
                                               if (i < t.steps.size())
                                               {
                                                   t.steps.erase(t.steps.begin() + static_cast<std::ptrdiff_t>(i));
                                               }
                                           },
                                           true);
                                   },
                                   true),
                               0}}));
    }
    box->addWidget(Hint(tr("Новую анимацию проще всего создать из готового фрагмента сценария: кнопка «Создать» на этой вкладке "
                           "берёт шаги из диапазона времени таймлайна.")));
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void LibraryDialog::_New()
{
    std::string id;
    switch (_CurrentTab())
    {
    case Tab::Elements:
    {
        ElementType e = _ctl.Reg().Element(kDefaultType);
        id            = _UniqueId("element");
        e.id          = id;
        e.label       = Us(tr("Новый элемент"));
        e.category    = "Мои";
        e.description.clear();
        _ctl.Doc().UpsertElement(e);
        break;
    }
    case Tab::Effects:
    {
        EffectDef e;
        id         = _UniqueId("effect");
        e.id       = id;
        e.label    = Us(tr("Новый эффект"));
        e.category = "Мои";
        e.tracks.push_back({EffectProperty::Scale, {{0, 1, Easing::Linear}, {0.5, 1.15, Easing::EaseOut}, {1, 1, Easing::EaseIn}}});
        _ctl.Doc().UpsertEffect(e);
        break;
    }
    case Tab::Animations:
        _NewAnimation();
        return;
    }
    _applying = true;
    _ctl.Changed(true);
    _applying = false;
    SelectItem(Qs(id));
}

void LibraryDialog::_NewAnimation()
{
    const auto &m = _ctl.GetModel();
    if (m.scenario.steps.empty())
    {
        QMessageBox::information(this, tr("Новая анимация"),
                                 tr("Анимация создаётся из шагов сценария. Добавьте шаги на таймлайн и выберите диапазон времени."));
        return;
    }
    double from = 0;
    double to   = m.scenario.duration;
    if (m.FindStep(_ctl.GetSelection().id) != nullptr && _ctl.GetSelection().kind == Selection::Kind::Step)
    {
        from = m.FindStep(_ctl.GetSelection().id)->start;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Новая анимация из сценария"));
    auto *name    = new QLineEdit(tr("Моя анимация"));
    auto *from_sb = new QDoubleSpinBox;
    auto *to_sb   = new QDoubleSpinBox;
    for (auto *sb : {from_sb, to_sb})
    {
        sb->setRange(0, 3'600'000);
        sb->setDecimals(0);
        sb->setSingleStep(100);
        sb->setSuffix(tr(" мс"));
    }
    from_sb->setValue(from);
    to_sb->setValue(to);
    auto *count   = new QLabel;
    auto  recount = [&]
    {
        count->setText(tr("Шагов в диапазоне: %1").arg(StepsInRange(m, from_sb->value(), to_sb->value()).size()));
    };
    connect(from_sb, &QDoubleSpinBox::valueChanged, &dlg, recount);
    connect(to_sb, &QDoubleSpinBox::valueChanged, &dlg, recount);
    recount();
    auto *form = new QFormLayout(&dlg);
    form->addRow(tr("Название"), name);
    form->addRow(tr("Начало шагов от"), from_sb);
    form->addRow(tr("до"), to_sb);
    form->addRow(count);
    form->addRow(new QLabel(tr("Узлы, участвующие в шагах, станут ролями шаблона.")));
    auto *buttons = fields::OkCancelButtons(&dlg);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted)
    {
        return;
    }
    const auto ids = StepsInRange(m, from_sb->value(), to_sb->value());
    if (ids.empty())
    {
        QMessageBox::warning(this, tr("Новая анимация"), tr("В выбранном диапазоне нет шагов."));
        return;
    }
    const std::string label = Us(name->text().trimmed().isEmpty() ? tr("Моя анимация") : name->text().trimmed());
    const std::string id    = _UniqueId(Slugify(label, "animation"));
    AnimationTemplate tpl   = MakeTemplate(m, ids, id, label);
    tpl.category            = "Мои";
    _ctl.Doc().UpsertAnimation(tpl);
    _applying = true;
    _ctl.Changed(true);
    _applying = false;
    SelectItem(Qs(id));
}

void LibraryDialog::_Duplicate()
{
    if (_current.id.empty())
    {
        return;
    }
    const auto       &lib    = _ctl.GetModel().library;
    const auto       &reg    = _ctl.Reg();
    const std::string id     = _UniqueId(_current.id + "-copy");
    const std::string suffix = Us(tr(" (копия)"));
    switch (_CurrentTab())
    {
    case Tab::Elements:
        if (const ElementType *src = reg.FindElement(_current.id, &lib); src != nullptr)
        {
            ElementType e = *src;
            e.id          = id;
            e.label += suffix;
            _ctl.Doc().UpsertElement(e);
        }
        break;
    case Tab::Effects:
        if (const EffectDef *src = reg.FindEffect(_current.id, &lib); src != nullptr)
        {
            EffectDef e = *src;
            e.id        = id;
            e.label += suffix;
            _ctl.Doc().UpsertEffect(e);
        }
        break;
    case Tab::Animations:
        if (const AnimationTemplate *src = reg.FindAnimation(_current.id, &lib); src != nullptr)
        {
            AnimationTemplate a = *src;
            a.id                = id;
            a.label += suffix;
            _ctl.Doc().UpsertAnimation(a);
        }
        break;
    }
    _applying = true;
    _ctl.Changed(true);
    _applying = false;
    SelectItem(Qs(id));
}

void LibraryDialog::_Delete()
{
    if (!_Editable())
    {
        return;
    }
    const auto answer = QMessageBox::question(this, tr("Удалить"),
                                              tr("Удалить «%1» из библиотеки документа?\n"
                                                 "Узлы и шаги, которые его используют, перейдут на определение из плагина или встроенное.")
                                                  .arg(Qs(_current.id)));
    if (answer != QMessageBox::Yes)
    {
        return;
    }
    if (_ctl.Doc().RemoveLibraryItem(_current.id))
    {
        _current = {};
        _ctl.Changed(true);
    }
}

void LibraryDialog::_ExportPlugin()
{
    // everything visible in the registry; later sources override earlier ones
    const auto &lib = _ctl.GetModel().library;
    const auto &reg = _ctl.Reg();
    LibrarySet  all;
    QDialog     dlg(this);
    dlg.setWindowTitle(tr("Экспорт в плагин"));
    dlg.resize(560, 620);
    auto *items = new QListWidget;
    auto  add   = [&](const QString &kind, const auto &entries)
    {
        for (const auto &e : entries)
        {
            all.Upsert(*e.def);
            auto *item = new QListWidgetItem(
                QStringLiteral("%1: %2 (%3) — %4").arg(kind, Qs(e.def->label), Qs(e.def->id), SourceName(e.source)), items);
            item->setData(kIdRole, Qs(e.def->id));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(e.source == kDocumentSource ? Qt::Checked : Qt::Unchecked);
        }
    };
    add(tr("Элемент"), reg.Elements(&lib));
    add(tr("Эффект"), reg.Effects(&lib));
    add(tr("Анимация"), reg.Animations(&lib));

    const std::string slug        = Slugify(_ctl.GetModel().meta.name, "library");
    auto             *id_edit     = new QLineEdit(QStringLiteral("user.%1").arg(Qs(slug)));
    auto             *name_edit   = new QLineEdit(Qs(_ctl.GetModel().meta.name));
    auto             *ver_edit    = new QLineEdit(QStringLiteral("1.0.0"));
    auto             *author_edit = new QLineEdit;
    auto             *desc_edit   = new QLineEdit;
    auto             *form        = new QFormLayout;
    form->addRow(tr("Идентификатор"), id_edit);
    form->addRow(tr("Название"), name_edit);
    form->addRow(tr("Версия"), ver_edit);
    form->addRow(tr("Автор"), author_edit);
    form->addRow(tr("Описание"), desc_edit);

    auto *save_btn    = new QPushButton(tr("Сохранить в файл…"));
    auto *install_btn = new QPushButton(tr("Установить"));
    install_btn->setObjectName(QStringLiteral("primaryButton"));
    auto *buttons = new QDialogButtonBox;
    buttons->addButton(tr("Отмена"), QDialogButtonBox::RejectRole);
    buttons->addButton(save_btn, QDialogButtonBox::ActionRole);
    buttons->addButton(install_btn, QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(new QLabel(tr("Состав плагина:")));
    layout->addWidget(items, 1);
    layout->addWidget(Hint(tr("Плагин — JSON-файл с элементами, эффектами и анимациями. Его можно передать коллегам "
                              "или установить через «Плагины → Установить из файла».")));
    layout->addWidget(buttons);

    auto build = [&]() -> std::optional<Plugin>
    {
        PluginInfo info;
        info.id          = Us(id_edit->text().trimmed());
        info.name        = Us(name_edit->text().trimmed());
        info.version     = Us(ver_edit->text().trimmed());
        info.author      = Us(author_edit->text().trimmed());
        info.description = Us(desc_edit->text().trimmed());
        if (!IsValidPluginId(info.id))
        {
            QMessageBox::warning(&dlg, tr("Экспорт"),
                                 tr("Идентификатор: строчные латинские буквы, цифры, «.», «_», «-» (например user.my-set)."));
            return std::nullopt;
        }
        std::vector<std::string> ids;
        for (int i = 0; i < items->count(); ++i)
        {
            if (items->item(i)->checkState() == Qt::Checked)
            {
                ids.push_back(Us(items->item(i)->data(kIdRole).toString()));
            }
        }
        if (ids.empty())
        {
            QMessageBox::warning(&dlg, tr("Экспорт"), tr("Отметьте хотя бы одно определение."));
            return std::nullopt;
        }
        if (info.name.empty())
        {
            info.name = info.id;
        }
        return MakePlugin(info, all, ids);
    };

    connect(save_btn, &QPushButton::clicked, &dlg,
            [&]
            {
                const auto plugin = build();
                if (!plugin.has_value())
                {
                    return;
                }
                const QString path = QFileDialog::getSaveFileName(&dlg, tr("Сохранить плагин"),
                                                                  Qs(plugin->info.id) + QStringLiteral(".json"), tr("Плагин (*.json)"));
                if (path.isEmpty())
                {
                    return;
                }
                try
                {
                    WriteFile(PathFromUtf8(Us(path)), SerializePlugin(*plugin));
                    dlg.accept();
                }
                catch (const std::exception &ex)
                {
                    QMessageBox::warning(&dlg, tr("Экспорт"), QString::fromUtf8(ex.what()));
                }
            });
    connect(install_btn, &QPushButton::clicked, &dlg,
            [&]
            {
                const auto plugin = build();
                if (!plugin.has_value())
                {
                    return;
                }
                const auto installed = _ctl.Plugins().Install(*plugin);
                if (!installed.has_value())
                {
                    QMessageBox::warning(&dlg, tr("Установка"), Qs(installed.error()));
                    return;
                }
                _ctl.ReloadPlugins();
                QMessageBox::information(&dlg, tr("Установка"), tr("Плагин установлен:\n%1").arg(Qs(PathToUtf8(*installed))));
                dlg.accept();
            });
    dlg.exec();
}

} // namespace ad::ui
