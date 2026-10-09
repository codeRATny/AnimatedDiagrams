#include "LibraryDialog.hpp"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#include "AppContext.hpp"
#include "Controller.hpp"
#include "Engine/Design.hpp"
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
        return QObject::tr("built-in");
    }
    if (source == kDocumentSource)
    {
        return QObject::tr("document");
    }
    return QObject::tr("plugin %1").arg(Qs(source));
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

LibraryDialog::LibraryDialog(AppContext &ctx, Controller *ctl, QWidget *parent) : QDialog(parent), _ctx(ctx), _ctl(ctl)
{
    setWindowTitle(tr("Library"));
    resize(1200, 720);

    _tabs                  = new QTabWidget;
    const QString titles[] = {tr("Elements"), tr("Effects"), tr("Animations"), tr("Design")};
    for (int i = 0; i < kTabCount; ++i)
    {
        _lists[i] = new QListWidget;
        _lists[i]->setSelectionMode(QAbstractItemView::SingleSelection);
        connect(_lists[i], &QListWidget::currentItemChanged, this, &LibraryDialog::_OnSelectionChanged);
        _tabs->addTab(_lists[i], titles[i]);
    }
    _tabs->tabBar()->setExpanding(false);
    _tabs->setStyleSheet(QStringLiteral("QTabBar::tab { padding: 6px 9px; }"));
    _tabs->tabBar()->setUsesScrollButtons(true);
    _tabs->setTabToolTip(3, tr("Design systems: colors, font and styles for the whole document"));
    connect(_tabs, &QTabWidget::currentChanged, this,
            [this]
            {
                _OnSelectionChanged();
            });

    _new_button   = new QPushButton(tr("＋ New"));
    _dup_button   = new QPushButton(tr("⧉ Duplicate"));
    _del_button   = new QPushButton(tr("Delete"));
    _apply_button = new QPushButton(tr("▶ Apply to diagram…"));
    _del_button->setObjectName(QStringLiteral("dangerButton"));
    connect(_new_button, &QPushButton::clicked, this, &LibraryDialog::_New);
    connect(_dup_button, &QPushButton::clicked, this, &LibraryDialog::_Duplicate);
    connect(_del_button, &QPushButton::clicked, this, &LibraryDialog::_Delete);
    connect(_apply_button, &QPushButton::clicked, this,
            [this]
            {
                if (_CurrentTab() != Tab::Designs)
                {
                    Q_EMIT ApplyAnimationRequested(Qs(_current.id));
                    return;
                }
                const DesignSystem *d = _ctl->Reg().FindDesignSystem(_current.id, &_ctl->GetModel().library);
                if (d == nullptr)
                {
                    return;
                }
                const DesignSystem copy = *d;
                _ctl->Edit(
                    {},
                    [&](Model &m)
                    {
                        ApplyDesignSystem(m, copy);
                    },
                    true);
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
    auto *prev_title = new QLabel(tr("PREVIEW"));
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
    split->setSizes({360, 450, 390});

    auto *export_button = new QPushButton(tr("Export to plugin…"));
    connect(export_button, &QPushButton::clicked, this, &LibraryDialog::_ExportPlugin);
    auto *buttons = new QDialogButtonBox;
    buttons->addButton(new QPushButton(tr("Close")), QDialogButtonBox::RejectRole);
    buttons->addButton(export_button, QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(split, 1);
    layout->addWidget(buttons);

    connect(&_ctx, &AppContext::LibraryChanged, this,
            [this]
            {
                _RefreshLists();
                _RequestEditorRebuild();
            });
    SetController(ctl);
}

void LibraryDialog::SetController(Controller *ctl)
{
    if (_ctl != nullptr)
    {
        disconnect(_ctl, nullptr, this, nullptr);
    }
    _ctl = ctl;
    if (_ctl == nullptr)
    {
        return;
    }
    connect(_ctl, &Controller::ModelChanged, this,
            [this](bool structural)
            {
                if (!_applying && structural)
                {
                    _RefreshLists(); // undo / redo / MCP edits of the document library
                    _RequestEditorRebuild();
                }
            });
    connect(_ctl, &Controller::DocumentStateChanged, this,
            [this]
            {
                setWindowTitle(tr("Library — %1").arg(Qs(_ctl->GetModel().meta.name)));
            });
    setWindowTitle(tr("Library — %1").arg(Qs(_ctl->GetModel().meta.name)));
    _current = {};
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
    const auto &lib   = _ctl->GetModel().library;
    const auto  taken = [&](const std::string &id)
    {
        return _ctl->Reg().FindElement(id, &lib) != nullptr || _ctl->Reg().FindEffect(id, &lib) != nullptr ||
               _ctl->Reg().FindAnimation(id, &lib) != nullptr;
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
    const auto &lib  = _ctl->GetModel().library;
    const auto &reg  = _ctl->Reg();
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
    fill(_lists[3], reg.DesignSystems(&lib));
    _OnSelectionChanged();
}

void LibraryDialog::SelectItem(const QString &id)
{
    _RefreshLists();
    for (int i = 0; i < kTabCount; ++i)
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
    _apply_button->setVisible(_CurrentTab() == Tab::Animations || _CurrentTab() == Tab::Designs);
    _apply_button->setText(_CurrentTab() == Tab::Designs ? tr("✓ Apply to document") : tr("▶ Apply to diagram…"));
    _apply_button->setEnabled(!_current.id.empty() && (_CurrentTab() == Tab::Designs || !_ctl->GetModel().nodes.empty()));
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

    const auto &lib   = _ctl->GetModel().library;
    const auto &reg   = _ctl->Reg();
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
    case Tab::Designs:
        if (const DesignSystem *d = reg.FindDesignSystem(_current.id, &lib); d != nullptr)
        {
            _BuildDesignEditor(box, DesignSystem(*d));
            found = true;
        }
        break;
    }
    if (!found)
    {
        box->addWidget(Hint(tr("Select a definition on the left or create a new one.")));
        _origin->clear();
    }
    else if (_Editable())
    {
        _origin->setText(tr("Source: document — changes are saved with the diagram and can be undone with Ctrl+Z."));
    }
    else
    {
        form->setEnabled(false);
        _origin->setText(
            tr("Source: %1 — read-only. Click “Duplicate” to create an editable copy in the document.").arg(SourceName(_current.source)));
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
    const auto &lib = _ctl->GetModel().library;
    const auto &reg = _ctl->Reg();
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
    case Tab::Designs:
        if (const DesignSystem *d = reg.FindDesignSystem(_current.id, &lib); d != nullptr)
        {
            _preview->ShowDesignSystem(*d, reg);
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
        const auto &lib = _ctl->GetModel().library;
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
        else if (const auto *d = lib.Design(_current.id); d != nullptr && _CurrentTab() == Tab::Designs)
        {
            item->setText(ItemText(*d));
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
    _ctl->Edit(
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
    _ctl->Edit(
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
    _ctl->Edit(
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

void LibraryDialog::_EditDesign(const std::string &key, const std::function<void(DesignSystem &)> &fn, bool rebuild)
{
    const std::string id = _current.id;
    _applying            = true;
    _ctl->Edit(
        key.empty() ? std::string() : "lib:" + id + ":" + key,
        [&](Model &m)
        {
            if (DesignSystem *d = FindById(m.library.design_systems, id); d != nullptr)
            {
                fn(*d);
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
    auto *title = new QLabel(tr("Element “%1”").arg(Qs(e.label)));
    title->setObjectName(QStringLiteral("inspectorTitle"));
    box->addWidget(title);
    box->addWidget(Hint(tr("ID: %1").arg(Qs(e.id))));

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
    box->addWidget(Labeled(tr("Name"), LineEdit(Qs(e.label), text("label", &ElementType::label))));
    box->addWidget(Row(Labeled(tr("Icon (character)"), LineEdit(Qs(e.icon), text("icon", &ElementType::icon), QStringLiteral("⚙"))),
                       Labeled(tr("Category"), LineEdit(Qs(e.category), text("category", &ElementType::category)))));
    box->addWidget(Labeled(tr("Description"), LineEdit(Qs(e.description), text("description", &ElementType::description))));
    box->addWidget(Row(Labeled(tr("Accent"), ColorButton(Qs(e.accent),
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
                       Row(Labeled(tr("Width"), Spin(e.width, 20, 2000, 10, 0,
                                                     [this](double v)
                                                     {
                                                         _EditElement("width",
                                                                      [&](ElementType &t)
                                                                      {
                                                                          t.width = v;
                                                                      });
                                                     })),
                           Labeled(tr("Height"), Spin(e.height, 20, 2000, 10, 0,
                                                      [this](double v)
                                                      {
                                                          _EditElement("height",
                                                                       [&](ElementType &t)
                                                                       {
                                                                           t.height = v;
                                                                       });
                                                      })))));
    box->addWidget(Section(tr("Default style")));
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
    box->addWidget(Hint(tr("A node's style on the canvas can override any of these fields. "
                           "The “custom” shape uses an SVG path in the 0..1 square (e.g. “M0 0 L1 0 L0.5 1 Z”).")));
}

// ---------------------------------------------------------------------------
// Effect editor
// ---------------------------------------------------------------------------

void LibraryDialog::_BuildEffectEditor(QVBoxLayout *box, const EffectDef &e)
{
    auto *title = new QLabel(tr("Effect “%1”").arg(Qs(e.label)));
    title->setObjectName(QStringLiteral("inspectorTitle"));
    box->addWidget(title);
    box->addWidget(Hint(tr("ID: %1").arg(Qs(e.id))));

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
    box->addWidget(Labeled(tr("Name"), LineEdit(Qs(e.label), text("label", &EffectDef::label))));
    box->addWidget(Row(Labeled(tr("Category"), LineEdit(Qs(e.category), text("category", &EffectDef::category))),
                       Labeled(tr("Color"), ColorButton(Qs(e.color),
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
    box->addWidget(Labeled(tr("Description"), LineEdit(Qs(e.description), text("description", &EffectDef::description))));
    box->addWidget(Labeled(tr("Repeats per step duration"), Spin(e.repeat, 1, 100, 1, 0,
                                                                 [this](double v)
                                                                 {
                                                                     _EditEffect("repeat",
                                                                                 [&](EffectDef &t)
                                                                                 {
                                                                                     t.repeat = static_cast<int>(v);
                                                                                 });
                                                                 })));

    box->addWidget(Section(tr("Keyframe tracks")));
    box->addWidget(Hint(tr("Each track animates one node property. t is the fraction of one repeat (0..1); "
                           "the curve defines the transition from the previous key to this one.")));
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
                                   QStringLiteral("✕"), tr("Delete track"),
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
                                  {SmallButton(QStringLiteral("−"), tr("Delete key"),
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
        box->addWidget(Button(tr("＋ Key"),
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
    box->addWidget(Button(tr("＋ Track"),
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
    auto *title = new QLabel(tr("Animation “%1”").arg(Qs(a.label)));
    title->setObjectName(QStringLiteral("inspectorTitle"));
    box->addWidget(title);
    box->addWidget(Hint(tr("ID: %1").arg(Qs(a.id))));

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
    box->addWidget(Labeled(tr("Name"), LineEdit(Qs(a.label), text("label", &AnimationTemplate::label))));
    box->addWidget(Labeled(tr("Category"), LineEdit(Qs(a.category), text("category", &AnimationTemplate::category))));
    box->addWidget(Labeled(tr("Description"), LineEdit(Qs(a.description), text("description", &AnimationTemplate::description))));

    box->addWidget(Section(tr("Roles")));
    box->addWidget(Hint(tr("Roles are slots for nodes. When the animation is applied, each role is assigned a diagram node.")));
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

    box->addWidget(Section(tr("Steps (time from start, ms)")));
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
                                   QStringLiteral("✕"), tr("Delete step"),
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
    box->addWidget(
        Hint(tr("The easiest way to create a new animation is from an existing part of the scenario: the “New” button on this tab "
                "takes the steps from a time range of the timeline.")));
}

// ---------------------------------------------------------------------------
// Design system editor
// ---------------------------------------------------------------------------

void LibraryDialog::_BuildDesignEditor(QVBoxLayout *box, const DesignSystem &d)
{
    auto *title = new QLabel(tr("Design system “%1”").arg(Qs(d.label)));
    title->setObjectName(QStringLiteral("inspectorTitle"));
    box->addWidget(title);
    const bool active = _ctl->GetModel().design_system == d.id;
    box->addWidget(Hint(tr("ID: %1%2").arg(Qs(d.id), active ? tr(" · used by the document") : QString())));

    auto text = [this](const std::string &key, std::string DesignSystem::*field)
    {
        return [this, key, field](const QString &v)
        {
            _EditDesign(key,
                        [&](DesignSystem &t)
                        {
                            t.*field = Us(v);
                        });
        };
    };
    auto opt_color = [this](std::optional<std::string> DesignSystem::*field)
    {
        return [this, field](std::optional<std::string> v)
        {
            _EditDesign(
                {},
                [&](DesignSystem &t)
                {
                    t.*field = std::move(v);
                },
                true);
        };
    };
    box->addWidget(Labeled(tr("Name"), LineEdit(Qs(d.label), text("label", &DesignSystem::label))));
    box->addWidget(Row(Labeled(tr("Category"), LineEdit(Qs(d.category), text("category", &DesignSystem::category))),
                       Labeled(tr("Description"), LineEdit(Qs(d.description), text("description", &DesignSystem::description)))));

    // ---- canvas and typography ---------------------------------------------
    box->addWidget(Section(tr("Canvas and font")));
    box->addWidget(Row(Labeled(tr("Background"), OptColor(d.background, QStringLiteral("#0a111f"), opt_color(&DesignSystem::background))),
                       Labeled(tr("Edges"), OptColor(d.edge_color, QStringLiteral("#5f7196"), opt_color(&DesignSystem::edge_color)))));
    box->addWidget(
        Row(Labeled(tr("Text"), OptColor(d.text_color, QStringLiteral("#f2f6ff"), opt_color(&DesignSystem::text_color))),
            Labeled(tr("Subtitle"), OptColor(d.subtitle_color, QStringLiteral("#b7c6e4"), opt_color(&DesignSystem::subtitle_color)))));
    box->addWidget(Row(OptCheck(tr("Grid"), d.grid, true,
                                [this](std::optional<bool> v)
                                {
                                    _EditDesign({},
                                                [&](DesignSystem &t)
                                                {
                                                    t.grid = v;
                                                });
                                }),
                       Labeled(tr("Grid spacing"), OptSpin(d.grid_size, 4, 200, 1, 0, QStringLiteral("26"),
                                                           [this](std::optional<double> v)
                                                           {
                                                               _EditDesign("grid_size",
                                                                           [&](DesignSystem &t)
                                                                           {
                                                                               t.grid_size = v;
                                                                           });
                                                           }))));
    Options fonts{{QString(), tr("Application font")}};
    for (const QString &family : QFontDatabase::families())
    {
        fonts.emplace_back(family, family);
    }
    box->addWidget(Labeled(tr("Font"), Combo(fonts, Qs(d.font_family),
                                             [this](const QString &v)
                                             {
                                                 _EditDesign({},
                                                             [&](DesignSystem &t)
                                                             {
                                                                 t.font_family = Us(v);
                                                             });
                                             })));

    // ---- color tokens --------------------------------------------------------
    box->addWidget(Section(tr("Color tokens")));
    box->addWidget(Hint(tr("Any element or step color can reference a token: “$primary”, “$surface”… "
                           "The color then changes along with the design system.")));
    std::vector<std::string> names;
    for (const auto &[k, v] : DefaultTokens())
    {
        names.push_back(k);
    }
    for (const auto &[k, v] : d.colors)
    {
        if (!DefaultTokens().contains(k))
        {
            names.push_back(k);
        }
    }
    for (const auto &name : names)
    {
        const auto        it       = d.colors.find(name);
        const bool        standard = DefaultTokens().contains(name);
        const std::string fallback = standard ? DefaultTokens().at(name) : std::string("#888888");
        auto             *label    = new QLabel(QStringLiteral("$") + Qs(name));
        label->setMinimumWidth(110);
        QWidget *color = OptColor(it != d.colors.end() ? std::optional<std::string>(it->second) : std::nullopt, Qs(fallback),
                                  [this, name](std::optional<std::string> v)
                                  {
                                      _EditDesign(
                                          {},
                                          [&](DesignSystem &t)
                                          {
                                              if (v.has_value())
                                              {
                                                  t.colors[name] = *v;
                                              }
                                              else
                                              {
                                                  t.colors.erase(name);
                                              }
                                          },
                                          true);
                                  });
        box->addWidget(RowOf({{label, 0}, {color, 1}}));
    }
    box->addWidget(Button(tr("＋ Custom token…"),
                          [this]
                          {
                              bool              ok   = false;
                              const QString     name = QInputDialog::getText(this, tr("New token"), tr("Name (Latin letters, no $):"),
                                                                             QLineEdit::Normal, QStringLiteral("brand"), &ok);
                              const std::string id   = Slugify(Us(name.trimmed()), "");
                              if (!ok || id.empty())
                              {
                                  return;
                              }
                              _EditDesign(
                                  {},
                                  [&](DesignSystem &t)
                                  {
                                      t.colors.try_emplace(id, "#4f8cff");
                                  },
                                  true);
                          }));

    // ---- node states and message variants -------------------------------------
    box->addWidget(Section(tr("Node states (fill / stroke)")));
    for (const auto &st : NodeStates())
    {
        const std::string sid = std::string(st.id);
        const auto        it  = d.states.find(sid);
        const StateColors cur = it != d.states.end() ? it->second : StateColors{};
        auto              set = [this, sid](bool ring)
        {
            return [this, sid, ring](std::optional<std::string> v)
            {
                _EditDesign(
                    {},
                    [&](DesignSystem &t)
                    {
                        StateColors &sc            = t.states[sid];
                        (ring ? sc.ring : sc.fill) = std::move(v);
                        if (!sc.fill.has_value() && !sc.ring.has_value())
                        {
                            t.states.erase(sid);
                        }
                    },
                    true);
            };
        };
        auto *label = new QLabel(Qs(st.label));
        label->setMinimumWidth(110);
        box->addWidget(RowOf(
            {{label, 0}, {OptColor(cur.fill, Qs(st.fill.Hex()), set(false)), 1}, {OptColor(cur.ring, Qs(st.ring.Hex()), set(true)), 1}}));
    }
    box->addWidget(Section(tr("Message variants")));
    for (const auto &v : MsgVariants())
    {
        const std::string vid   = std::string(v.id);
        const auto        it    = d.variants.find(vid);
        auto             *label = new QLabel(Qs(v.label));
        label->setMinimumWidth(110);
        box->addWidget(RowOf({{label, 0},
                              {OptColor(it != d.variants.end() ? std::optional<std::string>(it->second) : std::nullopt, Qs(v.color.Hex()),
                                        [this, vid](std::optional<std::string> c)
                                        {
                                            _EditDesign(
                                                {},
                                                [&](DesignSystem &t)
                                                {
                                                    if (c.has_value())
                                                    {
                                                        t.variants[vid] = *c;
                                                    }
                                                    else
                                                    {
                                                        t.variants.erase(vid);
                                                    }
                                                },
                                                true);
                                        }),
                               1}}));
    }

    // ---- default styles ------------------------------------------------------
    box->addWidget(Section(tr("Node defaults")));
    AddNodeStyleFields(box, d.node, {},
                       [this](const std::string &merge_key, const std::function<void(NodeStyle &)> &fn)
                       {
                           _EditDesign(
                               merge_key.empty() ? std::string() : "node:" + merge_key,
                               [&](DesignSystem &t)
                               {
                                   fn(t.node);
                               },
                               merge_key.empty());
                       });
    box->addWidget(Section(tr("Edge defaults")));
    auto edge = [this](const std::function<void(EdgeStyle &)> &fn, const std::string &key = {})
    {
        _EditDesign(
            key,
            [&](DesignSystem &t)
            {
                fn(t.edge);
            },
            key.empty());
    };
    box->addWidget(Row(Labeled(tr("Color"), OptColor(d.edge.color, tr("edge color"),
                                                     [edge](std::optional<std::string> v)
                                                     {
                                                         edge(
                                                             [&](EdgeStyle &e)
                                                             {
                                                                 e.color = std::move(v);
                                                             });
                                                     })),
                       Labeled(tr("Label color"), OptColor(d.edge.label_color, QStringLiteral("#9fb3d6"),
                                                           [edge](std::optional<std::string> v)
                                                           {
                                                               edge(
                                                                   [&](EdgeStyle &e)
                                                                   {
                                                                       e.label_color = std::move(v);
                                                                   });
                                                           }))));
    box->addWidget(Row(Labeled(tr("Thickness"), OptSpin(d.edge.width, 0.5, 20, 0.5, 1, QStringLiteral("2.2"),
                                                        [edge](std::optional<double> v)
                                                        {
                                                            edge(
                                                                [&](EdgeStyle &e)
                                                                {
                                                                    e.width = v;
                                                                },
                                                                "edge:width");
                                                        })),
                       Labeled(tr("Line"), OptCombo(FromCatalog(StrokeStyles()), d.edge.stroke_style, QStringLiteral("solid"),
                                                    [edge](std::optional<std::string> v)
                                                    {
                                                        edge(
                                                            [&](EdgeStyle &e)
                                                            {
                                                                e.stroke_style = std::move(v);
                                                            });
                                                    }))));
    box->addWidget(Row(Labeled(tr("Routing"), OptCombo(FromCatalog(Routings()), d.edge.routing, QStringLiteral("curved"),
                                                       [edge](std::optional<std::string> v)
                                                       {
                                                           edge(
                                                               [&](EdgeStyle &e)
                                                               {
                                                                   e.routing = std::move(v);
                                                               });
                                                       })),
                       Labeled(tr("Arrow"), OptCombo(FromCatalog(ArrowHeads()), d.edge.arrow_end, QStringLiteral("triangle"),
                                                     [edge](std::optional<std::string> v)
                                                     {
                                                         edge(
                                                             [&](EdgeStyle &e)
                                                             {
                                                                 e.arrow_end = std::move(v);
                                                             });
                                                     }))));

    // ---- per element type overrides ------------------------------------------------
    box->addWidget(Section(tr("Element type overrides")));
    Options types;
    for (const auto &e : _ctl->Reg().Elements(&_ctl->GetModel().library))
    {
        const bool has = d.elements.contains(e.def->id);
        types.emplace_back(Qs(e.def->id), (has ? QStringLiteral("● ") : QString()) + Qs(e.def->label));
    }
    if (_override_type.isEmpty() && !d.elements.empty())
    {
        _override_type = Qs(d.elements.begin()->first);
    }
    if (_override_type.isEmpty() && !types.empty())
    {
        _override_type = types.front().first;
    }
    box->addWidget(Labeled(tr("Element type (● = overridden)"), Combo(types, _override_type,
                                                                      [this](const QString &v)
                                                                      {
                                                                          _override_type = v;
                                                                          _RequestEditorRebuild();
                                                                      })));
    const std::string     type_id       = Us(_override_type);
    const auto            ov_it         = d.elements.find(type_id);
    const ElementOverride ov            = ov_it != d.elements.end() ? ov_it->second : ElementOverride{};
    const ElementType    &type          = _ctl->Reg().Element(type_id, &_ctl->GetModel().library);
    auto                  edit_override = [this, type_id](const std::string &key, const std::function<void(ElementOverride &)> &fn)
    {
        _EditDesign(
            key,
            [&](DesignSystem &t)
            {
                ElementOverride &o = t.elements[type_id];
                fn(o);
                if (!o.accent.has_value() && o.style.Empty())
                {
                    t.elements.erase(type_id);
                }
            },
            key.empty());
    };
    box->addWidget(Labeled(tr("Accent"), OptColor(ov.accent, Qs(type.accent),
                                                  [edit_override](std::optional<std::string> v)
                                                  {
                                                      edit_override({},
                                                                    [&](ElementOverride &o)
                                                                    {
                                                                        o.accent = std::move(v);
                                                                    });
                                                  })));
    AddNodeStyleFields(box, ov.style, type.style,
                       [edit_override](const std::string &merge_key, const std::function<void(NodeStyle &)> &fn)
                       {
                           edit_override(merge_key.empty() ? std::string() : "elem:" + merge_key,
                                         [&](ElementOverride &o)
                                         {
                                             fn(o.style);
                                         });
                       });
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
        ElementType e = _ctl->Reg().Element(kDefaultType);
        id            = _UniqueId("element");
        e.id          = id;
        e.label       = Us(tr("New element"));
        e.category    = Us(tr("Mine"));
        e.description.clear();
        _ctl->Doc().UpsertElement(e);
        break;
    }
    case Tab::Effects:
    {
        EffectDef e;
        id         = _UniqueId("effect");
        e.id       = id;
        e.label    = Us(tr("New effect"));
        e.category = Us(tr("Mine"));
        e.tracks.push_back({EffectProperty::Scale, {{0, 1, Easing::Linear}, {0.5, 1.15, Easing::EaseOut}, {1, 1, Easing::EaseIn}}});
        _ctl->Doc().UpsertEffect(e);
        break;
    }
    case Tab::Animations:
        _NewAnimation();
        return;
    case Tab::Designs:
    {
        // start from the current look of the document
        const auto &m    = _ctl->GetModel();
        const auto *base = _ctl->Reg().DesignOf(m);
        id               = _UniqueId("design");
        DesignSystem d = DesignFromScene(m, base != nullptr ? base : _ctl->Reg().FindDesignSystem("dark"), id, Us(tr("New design system")));
        d.description.clear();
        _ctl->Doc().UpsertDesignSystem(d);
        break;
    }
    }
    _applying = true;
    _ctl->Changed(true);
    _applying = false;
    SelectItem(Qs(id));
}

void LibraryDialog::_NewAnimation()
{
    const auto &m = _ctl->GetModel();
    if (m.scenario.steps.empty())
    {
        QMessageBox::information(this, tr("New animation"),
                                 tr("An animation is created from scenario steps. Add steps to the timeline and select a time range."));
        return;
    }
    double from = 0;
    double to   = m.scenario.duration;
    if (m.FindStep(_ctl->GetSelection().id) != nullptr && _ctl->GetSelection().kind == Selection::Kind::Step)
    {
        from = m.FindStep(_ctl->GetSelection().id)->start;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("New animation from scenario"));
    auto *name    = new QLineEdit(tr("My animation"));
    auto *from_sb = new QDoubleSpinBox;
    auto *to_sb   = new QDoubleSpinBox;
    for (auto *sb : {from_sb, to_sb})
    {
        sb->setRange(0, 3'600'000);
        sb->setDecimals(0);
        sb->setSingleStep(100);
        sb->setSuffix(tr(" ms"));
    }
    from_sb->setValue(from);
    to_sb->setValue(to);
    auto *count   = new QLabel;
    auto  recount = [&]
    {
        count->setText(tr("Steps in range: %1").arg(StepsInRange(m, from_sb->value(), to_sb->value()).size()));
    };
    connect(from_sb, &QDoubleSpinBox::valueChanged, &dlg, recount);
    connect(to_sb, &QDoubleSpinBox::valueChanged, &dlg, recount);
    recount();
    auto *form = new QFormLayout(&dlg);
    form->addRow(tr("Name"), name);
    form->addRow(tr("Steps starting from"), from_sb);
    form->addRow(tr("to"), to_sb);
    form->addRow(count);
    form->addRow(new QLabel(tr("Nodes involved in these steps will become the template's roles.")));
    auto *buttons = fields::OkCancelButtons(&dlg);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted)
    {
        return;
    }
    const auto ids = StepsInRange(m, from_sb->value(), to_sb->value());
    if (ids.empty())
    {
        QMessageBox::warning(this, tr("New animation"), tr("There are no steps in the selected range."));
        return;
    }
    const std::string label = Us(name->text().trimmed().isEmpty() ? tr("My animation") : name->text().trimmed());
    const std::string id    = _UniqueId(Slugify(label, "animation"));
    AnimationTemplate tpl   = MakeTemplate(m, ids, id, label);
    tpl.category            = Us(tr("Mine"));
    _ctl->Doc().UpsertAnimation(tpl);
    _applying = true;
    _ctl->Changed(true);
    _applying = false;
    SelectItem(Qs(id));
}

void LibraryDialog::_Duplicate()
{
    if (_current.id.empty())
    {
        return;
    }
    const auto       &lib    = _ctl->GetModel().library;
    const auto       &reg    = _ctl->Reg();
    const std::string id     = _UniqueId(_current.id + "-copy");
    const std::string suffix = Us(tr(" (copy)"));
    switch (_CurrentTab())
    {
    case Tab::Elements:
        if (const ElementType *src = reg.FindElement(_current.id, &lib); src != nullptr)
        {
            ElementType e = *src;
            e.id          = id;
            e.label += suffix;
            _ctl->Doc().UpsertElement(e);
        }
        break;
    case Tab::Effects:
        if (const EffectDef *src = reg.FindEffect(_current.id, &lib); src != nullptr)
        {
            EffectDef e = *src;
            e.id        = id;
            e.label += suffix;
            _ctl->Doc().UpsertEffect(e);
        }
        break;
    case Tab::Animations:
        if (const AnimationTemplate *src = reg.FindAnimation(_current.id, &lib); src != nullptr)
        {
            AnimationTemplate a = *src;
            a.id                = id;
            a.label += suffix;
            _ctl->Doc().UpsertAnimation(a);
        }
        break;
    case Tab::Designs:
        if (const DesignSystem *src = reg.FindDesignSystem(_current.id, &lib); src != nullptr)
        {
            DesignSystem d = *src;
            d.id           = id;
            d.label += suffix;
            d.category = Us(tr("Mine"));
            _ctl->Doc().UpsertDesignSystem(d);
        }
        break;
    }
    _applying = true;
    _ctl->Changed(true);
    _applying = false;
    SelectItem(Qs(id));
}

void LibraryDialog::_Delete()
{
    if (!_Editable())
    {
        return;
    }
    const auto answer = QMessageBox::question(this, tr("Delete"),
                                              tr("Delete “%1” from the document library?\n"
                                                 "Nodes and steps that use it will fall back to the plugin or built-in definition.")
                                                  .arg(Qs(_current.id)));
    if (answer != QMessageBox::Yes)
    {
        return;
    }
    if (_ctl->Doc().RemoveLibraryItem(_current.id))
    {
        _current = {};
        _ctl->Changed(true);
    }
}

void LibraryDialog::_ExportPlugin()
{
    // everything visible in the registry; later sources override earlier ones
    const auto &lib = _ctl->GetModel().library;
    const auto &reg = _ctl->Reg();
    LibrarySet  all;
    QDialog     dlg(this);
    dlg.setWindowTitle(tr("Export to plugin"));
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
    add(tr("Element"), reg.Elements(&lib));
    add(tr("Effect"), reg.Effects(&lib));
    add(tr("Animation"), reg.Animations(&lib));
    add(tr("Design system"), reg.DesignSystems(&lib));

    const std::string slug        = Slugify(_ctl->GetModel().meta.name, "library");
    auto             *id_edit     = new QLineEdit(QStringLiteral("user.%1").arg(Qs(slug)));
    auto             *name_edit   = new QLineEdit(Qs(_ctl->GetModel().meta.name));
    auto             *ver_edit    = new QLineEdit(QStringLiteral("1.0.0"));
    auto             *author_edit = new QLineEdit;
    auto             *desc_edit   = new QLineEdit;
    auto             *form        = new QFormLayout;
    form->addRow(tr("ID"), id_edit);
    form->addRow(tr("Name"), name_edit);
    form->addRow(tr("Version"), ver_edit);
    form->addRow(tr("Author"), author_edit);
    form->addRow(tr("Description"), desc_edit);

    auto *save_btn    = new QPushButton(tr("Save to file…"));
    auto *install_btn = new QPushButton(tr("Install"));
    install_btn->setObjectName(QStringLiteral("primaryButton"));
    auto *buttons = new QDialogButtonBox;
    buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    buttons->addButton(save_btn, QDialogButtonBox::ActionRole);
    buttons->addButton(install_btn, QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(new QLabel(tr("Plugin contents:")));
    layout->addWidget(items, 1);
    layout->addWidget(Hint(tr("A plugin is a JSON file with elements, effects and animations. You can share it with colleagues "
                              "or install it via “Plugins → Install from file”.")));
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
            QMessageBox::warning(&dlg, tr("Export"), tr("ID: lowercase Latin letters, digits, “.”, “_”, “-” (e.g. user.my-set)."));
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
            QMessageBox::warning(&dlg, tr("Export"), tr("Check at least one definition."));
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
                const QString path = QFileDialog::getSaveFileName(&dlg, tr("Save plugin"), Qs(plugin->info.id) + QStringLiteral(".json"),
                                                                  tr("Plugin (*.json)"));
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
                    QMessageBox::warning(&dlg, tr("Export"), QString::fromUtf8(ex.what()));
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
                const auto installed = _ctx.Plugins().Install(*plugin);
                if (!installed.has_value())
                {
                    QMessageBox::warning(&dlg, tr("Installation"), Qs(installed.error()));
                    return;
                }
                _ctx.ReloadPlugins();
                QMessageBox::information(&dlg, tr("Installation"), tr("Plugin installed:\n%1").arg(Qs(PathToUtf8(*installed))));
                dlg.accept();
            });
    dlg.exec();
}

} // namespace ad::ui
