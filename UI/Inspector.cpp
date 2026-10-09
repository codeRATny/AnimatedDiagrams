#include "Inspector.hpp"

#include <QApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>

#include "Controller.hpp"
#include "Engine/Design.hpp"
#include "Engine/Engine.hpp"
#include "Model/Catalog.hpp"
#include "Model/Document.hpp"
#include "QtRender.hpp"
#include "Utils/Text.hpp"

namespace ad::ui
{

using Kind = Selection::Kind;
using namespace fields;

namespace
{

/// Order the steps and recompute the duration after a step time change.
void Retime(Model &m)
{
    std::ranges::stable_sort(m.scenario.steps, {}, &Step::start);
    m.scenario.duration = AutoDuration(m.scenario);
}

QString SourceSuffix(const std::string &source)
{
    if (source == kBuiltinSource)
    {
        return {};
    }
    if (source == kDocumentSource)
    {
        return QObject::tr("  [документ]");
    }
    return QStringLiteral("  [%1]").arg(Qs(source));
}

} // namespace

Inspector::Inspector(Controller &ctl, QWidget *parent) : QScrollArea(parent), _ctl(ctl)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMinimumWidth(300);
    connect(&_ctl, &Controller::SelectionChanged, this,
            [this]
            {
                _RequestRebuild(true);
            });
    connect(&_ctl, &Controller::ModelChanged, this,
            [this](bool structural)
            {
                _RequestRebuild(structural);
            });
    connect(&_ctl, &Controller::LibraryChanged, this,
            [this]
            {
                _RequestRebuild(true);
            });
    _Rebuild();
}

void Inspector::_RequestRebuild(bool force)
{
    if (!force)
    {
        // do not rebuild while the user types into the inspector (focus would be lost)
        const QWidget *f = QApplication::focusWidget();
        if (f != nullptr && isAncestorOf(f))
        {
            return;
        }
    }
    if (_rebuild_pending)
    {
        return;
    }
    _rebuild_pending = true;
    QTimer::singleShot(0, this,
                       [this]
                       {
                           _rebuild_pending = false;
                           _Rebuild();
                       });
}

void Inspector::_Rebuild()
{
    const int scroll  = verticalScrollBar()->value();
    auto     *content = new QWidget;
    auto     *box     = new QVBoxLayout(content);
    box->setContentsMargins(12, 10, 12, 12);
    box->setSpacing(8);
    _title = new QLabel;
    _title->setObjectName(QStringLiteral("inspectorTitle"));
    box->addWidget(_title);

    const auto &sel = _ctl.GetSelection();
    switch (sel.kind)
    {
    case Kind::Node:
        _BuildNode(box, sel.id);
        break;
    case Kind::Edge:
        _BuildEdge(box, sel.id);
        break;
    case Kind::Step:
        _BuildStep(box, sel.id);
        break;
    case Kind::None:
        _BuildScene(box);
        break;
    }
    box->addStretch(1);

    QWidget *old = takeWidget();
    setWidget(content);
    if (old != nullptr)
    {
        old->deleteLater();
    }
    QTimer::singleShot(0, this,
                       [this, scroll]
                       {
                           verticalScrollBar()->setValue(scroll);
                       });
}

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------

Options Inspector::_NodeOptions() const
{
    Options o;
    for (const auto &n : _ctl.GetModel().nodes)
    {
        o.emplace_back(Qs(n.id), Qs(n.label));
    }
    return o;
}

Options Inspector::_EdgeOptions() const
{
    const auto &m  = _ctl.GetModel();
    auto        nm = [&](const std::string &id)
    {
        const Node *n = m.FindNode(id);
        return n != nullptr ? Qs(n->label) : QStringLiteral("?");
    };
    std::map<std::string, int> seen;
    Options                    o;
    for (const auto &e : m.edges)
    {
        const int n     = ++seen[e.from + ">" + e.to];
        QString   label = nm(e.from) + QStringLiteral(" → ") + nm(e.to);
        if (n > 1)
        {
            label += QStringLiteral(" (%1)").arg(n);
        }
        if (!e.label.empty())
        {
            label += QStringLiteral(" · ") + Qs(e.label);
        }
        o.emplace_back(Qs(e.id), label);
    }
    return o;
}

Options Inspector::_PortOptions(const std::string &node_id) const
{
    Options o{{QString(), tr("Авто")}};
    if (const Node *n = _ctl.GetModel().FindNode(node_id); n != nullptr)
    {
        for (size_t i = 0; i < n->ports.size(); ++i)
        {
            o.emplace_back(Qs(n->ports[i].id), tr("Точка %1").arg(i + 1));
        }
    }
    return o;
}

Options Inspector::_ElementOptions() const
{
    Options o;
    for (const auto &e : _ctl.Reg().Elements(&_ctl.GetModel().library))
    {
        o.emplace_back(Qs(e.def->id), Qs(e.def->category) + QStringLiteral(" · ") + Qs(e.def->label) + SourceSuffix(e.source));
    }
    return o;
}

Options Inspector::_EffectOptions() const
{
    Options o;
    for (const auto &e : _ctl.Reg().Effects(&_ctl.GetModel().library))
    {
        o.emplace_back(Qs(e.def->id), Qs(e.def->label) + SourceSuffix(e.source));
    }
    return o;
}

template <class Get>
void Inspector::_LabelControls(QVBoxLayout *box, const std::string &key, double def_offset, Get get)
{
    const auto *obj = get(_ctl.GetModel());
    if (obj == nullptr)
    {
        return;
    }
    box->addWidget(Row(Labeled(tr("Размер шрифта"), Spin(obj->label_size.value_or(12), 6, 72, 1, 0,
                                                         [this, key, get](double v)
                                                         {
                                                             _ctl.Edit(key + ":label_size",
                                                                       [&](Model &m)
                                                                       {
                                                                           if (auto *o = get(m); o != nullptr)
                                                                           {
                                                                               o->label_size = v;
                                                                           }
                                                                       });
                                                         })),
                       Labeled(tr("Смещение"), Spin(obj->label_off.value_or(def_offset), -200, 200, 1, 0,
                                                    [this, key, get](double v)
                                                    {
                                                        _ctl.Edit(key + ":label_off",
                                                                  [&](Model &m)
                                                                  {
                                                                      if (auto *o = get(m); o != nullptr)
                                                                      {
                                                                          o->label_off = v;
                                                                      }
                                                                  });
                                                    }))));
    box->addWidget(Labeled(tr("Положение вдоль связи"), Slider(obj->label_pos.value_or(0.5), 0, 1, 0.02,
                                                               [this, key, get](double v)
                                                               {
                                                                   _ctl.Edit(key + ":label_pos",
                                                                             [&](Model &m)
                                                                             {
                                                                                 if (auto *o = get(m); o != nullptr)
                                                                                 {
                                                                                     o->label_pos = v;
                                                                                 }
                                                                             });
                                                               })));
}

// ---------------------------------------------------------------------------
// Scene (nothing selected)
// ---------------------------------------------------------------------------

void Inspector::_BuildScene(QVBoxLayout *box)
{
    const auto &m = _ctl.GetModel();
    _title->setText(tr("Сцена"));
    auto edit_scene = [this](const std::string &key, const std::function<void(Model &)> &fn)
    {
        _ctl.Edit(key, fn, true);
    };

    box->addWidget(Labeled(tr("Название"), LineEdit(Qs(m.meta.name),
                                                    [this](const QString &v)
                                                    {
                                                        _ctl.Rename(v);
                                                    })));
    box->addWidget(Labeled(tr("Описание"), LineEdit(Qs(m.meta.description),
                                                    [edit_scene](const QString &v)
                                                    {
                                                        edit_scene("meta:description",
                                                                   [&](Model &mm)
                                                                   {
                                                                       mm.meta.description = Us(v);
                                                                   });
                                                    })));

    box->addWidget(Section(tr("Дизайн-система")));
    Options designs{{QString(), tr("— не задана —")}};
    for (const auto &d : _ctl.Reg().DesignSystems(&m.library))
    {
        designs.emplace_back(Qs(d.def->id), Qs(d.def->label));
    }
    box->addWidget(Combo(designs, Qs(m.design_system),
                         [this](const QString &v)
                         {
                             const DesignSystem *ds = v.isEmpty() ? nullptr : _ctl.Reg().FindDesignSystem(Us(v), &_ctl.GetModel().library);
                             const DesignSystem  copy = ds != nullptr ? *ds : DesignSystem{};
                             _ctl.Edit(
                                 {},
                                 [&](Model &mm)
                                 {
                                     if (ds != nullptr)
                                     {
                                         ApplyDesignSystem(mm, copy);
                                     }
                                     else
                                     {
                                         ClearDesignSystem(mm);
                                     }
                                 },
                                 true);
                         }));
    auto *edit_ds = Button(tr("Редактировать…"),
                           [this]
                           {
                               Q_EMIT LibraryRequested(Qs(_ctl.GetModel().design_system));
                           });
    edit_ds->setEnabled(!m.design_system.empty());
    box->addWidget(Row(edit_ds, Button(tr("Сохранить вид как…"),
                                       [this]
                                       {
                                           bool          ok   = false;
                                           const QString name = QInputDialog::getText(this, tr("Новая дизайн-система"), tr("Название:"),
                                                                                      QLineEdit::Normal, tr("Моя тема"), &ok);
                                           if (!ok || name.trimmed().isEmpty())
                                           {
                                               return;
                                           }
                                           const auto &mm = _ctl.GetModel();
                                           std::string id = Slugify(Us(name), "design");
                                           for (int i = 2; _ctl.Reg().FindDesignSystem(id, &mm.library) != nullptr; ++i)
                                           {
                                               id = Slugify(Us(name), "design") + "-" + std::to_string(i);
                                           }
                                           const DesignSystem ds = DesignFromScene(mm, _ctl.Reg().DesignOf(mm), id, Us(name.trimmed()));
                                           _ctl.Doc().UpsertDesignSystem(ds); // checkpoints; applying joins the same undo step
                                           ApplyDesignSystem(_ctl.Doc().Mutable(), ds);
                                           _ctl.Changed(true);
                                           Q_EMIT LibraryRequested(Qs(id));
                                       })));
    box->addWidget(Hint(tr("Дизайн-система задаёт цвета холста, состояний и сообщений, шрифт и стили по умолчанию. "
                           "Цвета холста ниже копируются из неё и остаются редактируемыми.")));

    box->addWidget(Section(tr("Холст")));
    box->addWidget(Row(Labeled(tr("Фон"), ColorButton(Qs(m.scene.background),
                                                      [edit_scene](const QString &v)
                                                      {
                                                          edit_scene({},
                                                                     [&](Model &mm)
                                                                     {
                                                                         mm.scene.background = Us(v);
                                                                     });
                                                      })),
                       Labeled(tr("Цвет текста"), ColorButton(Qs(m.scene.text_color),
                                                              [edit_scene](const QString &v)
                                                              {
                                                                  edit_scene({},
                                                                             [&](Model &mm)
                                                                             {
                                                                                 mm.scene.text_color = Us(v);
                                                                             });
                                                              }))));
    box->addWidget(Row(Labeled(tr("Цвет связей"), ColorButton(Qs(m.scene.edge_color),
                                                              [edit_scene](const QString &v)
                                                              {
                                                                  edit_scene({},
                                                                             [&](Model &mm)
                                                                             {
                                                                                 mm.scene.edge_color = Us(v);
                                                                             });
                                                              })),
                       Labeled(tr("Шаг сетки"), Spin(m.scene.grid_size, 4, 200, 1, 0,
                                                     [edit_scene](double v)
                                                     {
                                                         edit_scene("scene:grid_size",
                                                                    [&](Model &mm)
                                                                    {
                                                                        mm.scene.grid_size = v;
                                                                    });
                                                     }))));
    box->addWidget(Check(tr("Показывать сетку"), m.scene.grid,
                         [edit_scene](bool v)
                         {
                             edit_scene({},
                                        [&](Model &mm)
                                        {
                                            mm.scene.grid = v;
                                        });
                         }));

    box->addWidget(Section(tr("Сценарий")));
    box->addWidget(Labeled(tr("Длительность сцены, мс"), Spin(m.scenario.duration, 500, 3'600'000, 100, 0,
                                                              [this](double v)
                                                              {
                                                                  _ctl.Doc().SetDuration(v);
                                                                  _ctl.Changed(false);
                                                              })));
    box->addWidget(Check(tr("Подбирать длительность автоматически"), !m.scenario.user_duration,
                         [edit_scene](bool v)
                         {
                             edit_scene({},
                                        [&](Model &mm)
                                        {
                                            mm.scenario.user_duration = !v;
                                            if (v)
                                            {
                                                mm.scenario.duration = AutoDuration(mm.scenario);
                                            }
                                        });
                         }));
    box->addWidget(Hint(tr("Ничего не выбрано. Выберите узел, связь или шаг сценария, чтобы редактировать их свойства.")));
}

// ---------------------------------------------------------------------------
// Node
// ---------------------------------------------------------------------------

void Inspector::_BuildNode(QVBoxLayout *box, const std::string &id)
{
    const auto &m = _ctl.GetModel();
    const Node *n = m.FindNode(id);
    if (n == nullptr)
    {
        _BuildScene(box);
        return;
    }
    const ElementType &type = _ctl.Reg().Element(n->type, &m.library);
    _title->setText(tr("Узел: %1").arg(Qs(type.label)));
    const std::string key       = "node:" + id;
    auto              edit_node = [this, id](const std::string &k, const std::function<void(Node &)> &fn, bool structural = false)
    {
        _ctl.Edit(
            k,
            [&](Model &mm)
            {
                if (Node *node = mm.FindNode(id); node != nullptr)
                {
                    fn(*node);
                }
            },
            structural);
    };

    box->addWidget(Labeled(tr("Название"), LineEdit(Qs(n->label),
                                                    [=](const QString &v)
                                                    {
                                                        edit_node(key + ":label",
                                                                  [&](Node &x)
                                                                  {
                                                                      x.label = Us(v);
                                                                  });
                                                    })));
    box->addWidget(Labeled(tr("Подзаголовок"), LineEdit(Qs(n->subtitle),
                                                        [=](const QString &v)
                                                        {
                                                            edit_node(key + ":subtitle",
                                                                      [&](Node &x)
                                                                      {
                                                                          x.subtitle = Us(v);
                                                                      });
                                                        })));
    box->addWidget(Labeled(tr("Тип элемента"), Combo(_ElementOptions(), Qs(n->type),
                                                     [this, edit_node](const QString &v)
                                                     {
                                                         const auto        &mm      = _ctl.GetModel();
                                                         const std::string  type_id = Us(v);
                                                         const ElementType &next    = _ctl.Reg().Element(type_id, &mm.library);
                                                         edit_node(
                                                             {},
                                                             [&](Node &x)
                                                             {
                                                                 const ElementType &prev = _ctl.Reg().Element(x.type, &mm.library);
                                                                 // keep a custom size, follow the type size otherwise
                                                                 if (x.w == prev.width && x.h == prev.height)
                                                                 {
                                                                     x.w = next.width;
                                                                     x.h = next.height;
                                                                 }
                                                                 x.type = type_id;
                                                             },
                                                             true);
                                                     })));
    box->addWidget(Row(
        Labeled(tr("Акцент"), OptColor(n->accent.empty() ? std::nullopt : std::optional<std::string>(n->accent),
                                       Qs(ResolveColorToken(type.accent, _ctl.Reg().DesignOf(m))),
                                       [=](std::optional<std::string> v)
                                       {
                                           edit_node(
                                               {},
                                               [&](Node &x)
                                               {
                                                   x.accent = v.value_or("");
                                               },
                                               true);
                                       })),
        Row(Labeled(tr("Ширина"), Spin(n->w, 20, 4000, 10, 0,
                                       [=](double v)
                                       {
                                           edit_node(key + ":w",
                                                     [&](Node &x)
                                                     {
                                                         x.w = v;
                                                     });
                                       })),
            Labeled(
                tr("Высота"),
                Spin(
                    n->h, 20,
                    4000, 10, 0,
                    [=](double v)
                    {
                        edit_node(key + ":h",
                                  [&](Node &x)
                                  {
                                      x.h = v;
                                  });
                    })))));

    box->addWidget(Section(tr("Стиль узла")));
    AddNodeStyleFields(box, n->style, type.style,
                       [=](const std::string &merge_key, const std::function<void(NodeStyle &)> &fn)
                       {
                           edit_node(merge_key.empty() ? std::string() : key + ":" + merge_key,
                                     [&](Node &x)
                                     {
                                         fn(x.style);
                                     });
                       });
    auto *reset = Button(tr("Сбросить стиль к типу"),
                         [=]
                         {
                             edit_node(
                                 {},
                                 [](Node &x)
                                 {
                                     x.style = {};
                                 },
                                 true);
                         });
    reset->setEnabled(!n->style.Empty());
    box->addWidget(Row(reset, Button(tr("Сохранить как тип…"),
                                     [this, id]
                                     {
                                         _SaveNodeAsType(id);
                                     })));
    box->addWidget(Hint(tr("Поля «авто» наследуются от типа элемента. «Сохранить как тип» создаёт элемент в библиотеке документа.")));

    box->addWidget(Section(tr("Точки соединения")));
    for (size_t i = 0; i < n->ports.size(); ++i)
    {
        const std::string port_id = n->ports[i].id;
        auto             *rm      = qobject_cast<QPushButton *>(Button(
            QStringLiteral("✕"),
            [this, id, port_id]
            {
                _ctl.Doc().RemovePort(id, port_id);
                _ctl.Changed(true);
            },
            true));
        rm->setFixedWidth(36);
        auto *w = new QWidget;
        auto *l = new QHBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(new QLabel(tr("Точка %1").arg(i + 1)), 1);
        l->addWidget(rm);
        box->addWidget(w);
    }
    box->addWidget(Button(tr("＋ Добавить точку соединения"),
                          [this, id]
                          {
                              _ctl.Doc().AddPort(id);
                              _ctl.Changed(true);
                          }));
    box->addWidget(Hint(tr("Точки можно перетаскивать. В режиме «Связь» кликните по точке, чтобы привязать к ней связь.")));
    box->addWidget(Button(
        tr("Удалить узел"),
        [this]
        {
            _ctl.DeleteSelection();
        },
        true));
}

void Inspector::_SaveNodeAsType(const std::string &node_id)
{
    const auto &m = _ctl.GetModel();
    const Node *n = m.FindNode(node_id);
    if (n == nullptr)
    {
        return;
    }
    bool          ok   = false;
    const QString name = QInputDialog::getText(this, tr("Новый тип элемента"), tr("Название типа:"), QLineEdit::Normal, Qs(n->label), &ok);
    if (!ok || name.trimmed().isEmpty())
    {
        return;
    }
    const ElementType &base = _ctl.Reg().Element(n->type, &m.library);
    ElementType        t    = base;
    std::string        id   = Slugify(Us(name), "element");
    for (int i = 2; _ctl.Reg().FindElement(id, &m.library) != nullptr; ++i)
    {
        id = Slugify(Us(name), "element") + "-" + std::to_string(i);
    }
    t.id       = id;
    t.label    = Us(name.trimmed());
    t.category = "Мои";
    t.accent   = n->accent.empty() ? base.accent : n->accent;
    t.width    = n->w;
    t.height   = n->h;
    t.style    = base.style.Merged(n->style);

    _ctl.Doc().UpsertElement(t); // checkpoints; the node update below joins the same undo step
    if (Node *nn = _ctl.Doc().Mutable().FindNode(node_id); nn != nullptr)
    {
        nn->type  = id;
        nn->style = {};
        nn->accent.clear();
    }
    _ctl.Changed(true);
    Q_EMIT LibraryRequested(Qs(id));
}

// ---------------------------------------------------------------------------
// Edge
// ---------------------------------------------------------------------------

void Inspector::_BuildEdge(QVBoxLayout *box, const std::string &id)
{
    const auto &m = _ctl.GetModel();
    const Edge *e = m.FindEdge(id);
    if (e == nullptr)
    {
        _BuildScene(box);
        return;
    }
    _title->setText(tr("Связь"));
    const std::string key       = "edge:" + id;
    auto              edit_edge = [this, id](const std::string &k, const std::function<void(Edge &)> &fn, bool structural = false)
    {
        _ctl.Edit(
            k,
            [&](Model &mm)
            {
                if (Edge *edge = mm.FindEdge(id); edge != nullptr)
                {
                    fn(*edge);
                }
            },
            structural);
    };
    auto nm = [&](const std::string &nid)
    {
        const Node *n = m.FindNode(nid);
        return n != nullptr ? Qs(n->label) : QStringLiteral("?");
    };
    box->addWidget(Hint(nm(e->from) + QStringLiteral(" → ") + nm(e->to)));

    box->addWidget(Labeled(tr("Подпись"), LineEdit(Qs(e->label),
                                                   [=](const QString &v)
                                                   {
                                                       edit_edge(key + ":label",
                                                                 [&](Edge &x)
                                                                 {
                                                                     x.label = Us(v);
                                                                 });
                                                   })));
    _LabelControls(box, key, 10,
                   [id](auto &mm)
                   {
                       return mm.FindEdge(id);
                   });

    box->addWidget(Section(tr("Стиль линии")));
    box->addWidget(Row(Labeled(tr("Цвет"), OptColor(e->style.color, Qs(m.scene.edge_color),
                                                    [=](std::optional<std::string> v)
                                                    {
                                                        edit_edge(
                                                            {},
                                                            [&](Edge &x)
                                                            {
                                                                x.style.color = std::move(v);
                                                            },
                                                            true);
                                                    })),
                       Labeled(tr("Цвет подписи"), OptColor(e->style.label_color, QStringLiteral("#c7d4ee"),
                                                            [=](std::optional<std::string> v)
                                                            {
                                                                edit_edge(
                                                                    {},
                                                                    [&](Edge &x)
                                                                    {
                                                                        x.style.label_color = std::move(v);
                                                                    },
                                                                    true);
                                                            }))));
    box->addWidget(Row(Labeled(tr("Толщина"), OptSpin(e->style.width, 0.5, 20, 0.5, 1, QStringLiteral("2"),
                                                      [=](std::optional<double> v)
                                                      {
                                                          edit_edge(key + ":width",
                                                                    [&](Edge &x)
                                                                    {
                                                                        x.style.width = v;
                                                                    });
                                                      })),
                       Labeled(tr("Линия"), OptCombo(FromCatalog(StrokeStyles()), e->style.stroke_style, QStringLiteral("solid"),
                                                     [=](std::optional<std::string> v)
                                                     {
                                                         edit_edge({},
                                                                   [&](Edge &x)
                                                                   {
                                                                       x.style.stroke_style = std::move(v);
                                                                   });
                                                     }))));
    box->addWidget(Row(Labeled(tr("Стрелка в начале"), OptCombo(FromCatalog(ArrowHeads()), e->style.arrow_start, QStringLiteral("none"),
                                                                [=](std::optional<std::string> v)
                                                                {
                                                                    edit_edge({},
                                                                              [&](Edge &x)
                                                                              {
                                                                                  x.style.arrow_start = std::move(v);
                                                                              });
                                                                })),
                       Labeled(tr("Стрелка в конце"), OptCombo(FromCatalog(ArrowHeads()), e->style.arrow_end, QStringLiteral("triangle"),
                                                               [=](std::optional<std::string> v)
                                                               {
                                                                   edit_edge({},
                                                                             [&](Edge &x)
                                                                             {
                                                                                 x.style.arrow_end = std::move(v);
                                                                             });
                                                               }))));
    box->addWidget(Labeled(tr("Маршрут"), OptCombo(FromCatalog(Routings()), e->style.routing, QStringLiteral("curved"),
                                                   [=](std::optional<std::string> v)
                                                   {
                                                       edit_edge({},
                                                                 [&](Edge &x)
                                                                 {
                                                                     x.style.routing = std::move(v);
                                                                 });
                                                   })));

    box->addWidget(Section(tr("Геометрия")));
    box->addWidget(Row(Labeled(tr("Вход (от)"), Combo(_PortOptions(e->from), Qs(e->from_port),
                                                      [=](const QString &v)
                                                      {
                                                          edit_edge({},
                                                                    [&](Edge &x)
                                                                    {
                                                                        x.from_port = Us(v);
                                                                    });
                                                      })),
                       Labeled(tr("Выход (к)"), Combo(_PortOptions(e->to), Qs(e->to_port),
                                                      [=](const QString &v)
                                                      {
                                                          edit_edge({},
                                                                    [&](Edge &x)
                                                                    {
                                                                        x.to_port = Us(v);
                                                                    });
                                                      }))));

    const bool has_wp = !e->waypoints.empty();
    QWidget   *curve  = Slider(e->curve, -0.5, 0.5, 0.05,
                               [=](double v)
                               {
                                edit_edge(key + ":curve",
                                             [&](Edge &x)
                                             {
                                              x.curve = std::round(v * 100) / 100;
                                          });
                            });
    curve->setEnabled(!has_wp && e->style.routing.value_or("curved") == "curved");
    box->addWidget(Labeled(has_wp ? tr("Изгиб (задан точками)") : tr("Изгиб"), curve));

    auto *clear = Button(tr("Очистить"),
                         [=]
                         {
                             edit_edge(
                                 {},
                                 [](Edge &x)
                                 {
                                     x.waypoints.clear();
                                 },
                                 true);
                         });
    clear->setEnabled(has_wp);
    box->addWidget(Row(new QLabel(tr("Точек изгиба: %1").arg(e->waypoints.size())), clear));
    box->addWidget(Hint(tr("Двойной клик по связи — добавить точку изгиба; по точке — удалить. Точки перетаскиваются.")));
    box->addWidget(Button(tr("⇄ Развернуть направление"),
                          [this, id]
                          {
                              _ctl.Doc().ReverseEdge(id);
                              _ctl.Changed(true);
                          }));
    box->addWidget(Button(
        tr("Удалить связь"),
        [this]
        {
            _ctl.DeleteSelection();
        },
        true));
}

// ---------------------------------------------------------------------------
// Step
// ---------------------------------------------------------------------------

void Inspector::_BuildStep(QVBoxLayout *box, const std::string &id)
{
    const auto &m = _ctl.GetModel();
    const Step *s = m.FindStep(id);
    if (s == nullptr)
    {
        _BuildScene(box);
        return;
    }
    _title->setText(tr("Шаг: %1").arg(Qs(StepTypeLabel(s->type))));
    const std::string key       = "step:" + id;
    auto              edit_step = [this, id](const std::string &k, const std::function<void(Step &)> &fn, bool structural = false)
    {
        _ctl.Edit(
            k,
            [&](Model &mm)
            {
                if (Step *st = mm.FindStep(id); st != nullptr)
                {
                    fn(*st);
                }
            },
            structural);
    };

    Options types;
    for (const auto &t : StepTypes())
    {
        types.emplace_back(Qs(t.id), Qs(t.label));
    }
    box->addWidget(Labeled(tr("Тип шага"), Combo(types, Qs(ToString(s->type)),
                                                 [this, edit_step](const QString &v)
                                                 {
                                                     edit_step(
                                                         {},
                                                         [&](Step &st)
                                                         {
                                                             st.type = StepTypeFromString(Us(v)).value_or(st.type);
                                                             _ctl.Doc().NormalizeStep(st);
                                                         },
                                                         true);
                                                 })));

    auto node_combo = [&](const QString &label)
    {
        box->addWidget(Labeled(label, Combo(_NodeOptions(), Qs(s->node_id),
                                            [=](const QString &v)
                                            {
                                                edit_step({},
                                                          [&](Step &st)
                                                          {
                                                              st.node_id = Us(v);
                                                          });
                                            })));
    };
    auto color_field = [&](const QString &fallback)
    {
        return Labeled(tr("Цвет"), OptColor(s->color.empty() ? std::nullopt : std::optional<std::string>(s->color), fallback,
                                            [=](std::optional<std::string> v)
                                            {
                                                edit_step(
                                                    {},
                                                    [&](Step &st)
                                                    {
                                                        st.color = v.value_or("");
                                                    },
                                                    true);
                                            }));
    };
    auto text_field = [&](const QString &label, const std::string &field_key, std::string Step::*field, const QString &placeholder = {})
    {
        box->addWidget(Labeled(label, LineEdit(
                                          Qs(s->*field),
                                          [=](const QString &v)
                                          {
                                              edit_step(key + ":" + field_key,
                                                        [&](Step &st)
                                                        {
                                                            st.*field = Us(v);
                                                        });
                                          },
                                          placeholder)));
    };

    switch (s->type)
    {
    case StepType::Message:
    {
        Options edges{{QString(), tr("— по узлам (без связи) —")}};
        for (auto &o : _EdgeOptions())
        {
            edges.push_back(std::move(o));
        }
        box->addWidget(Labeled(tr("Связь"), Combo(edges, Qs(s->edge_id),
                                                  [this, edit_step](const QString &v)
                                                  {
                                                      edit_step(
                                                          {},
                                                          [&](Step &st)
                                                          {
                                                              st.edge_id = Us(v);
                                                              if (const Edge *e = _ctl.GetModel().FindEdge(st.edge_id); e != nullptr)
                                                              {
                                                                  st.from = e->from;
                                                                  st.to   = e->to;
                                                              }
                                                          },
                                                          true);
                                                  })));
        if (m.FindEdge(s->edge_id) != nullptr)
        {
            auto nm = [&](const std::string &nid)
            {
                const Node *n = m.FindNode(nid);
                return n != nullptr ? Qs(n->label) : QStringLiteral("?");
            };
            box->addWidget(Button(tr("Направление: %1 → %2  ⇄").arg(nm(s->from), nm(s->to)),
                                  [=]
                                  {
                                      edit_step(
                                          {},
                                          [](Step &st)
                                          {
                                              std::swap(st.from, st.to);
                                          },
                                          true);
                                  }));
        }
        else
        {
            box->addWidget(Row(Labeled(tr("От"), Combo(_NodeOptions(), Qs(s->from),
                                                       [=](const QString &v)
                                                       {
                                                           edit_step({},
                                                                     [&](Step &st)
                                                                     {
                                                                         st.from = Us(v);
                                                                     });
                                                       })),
                               Labeled(tr("К"), Combo(_NodeOptions(), Qs(s->to),
                                                      [=](const QString &v)
                                                      {
                                                          edit_step({},
                                                                    [&](Step &st)
                                                                    {
                                                                        st.to = Us(v);
                                                                    });
                                                      }))));
        }
        Options variants;
        for (const auto &v : MsgVariants())
        {
            variants.emplace_back(Qs(v.id), Qs(v.label));
        }
        box->addWidget(Row(Labeled(tr("Вариант"), Combo(variants, Qs(s->variant),
                                                        [=](const QString &v)
                                                        {
                                                            edit_step(
                                                                {},
                                                                [&](Step &st)
                                                                {
                                                                    st.variant = Us(v);
                                                                },
                                                                true);
                                                        })),
                           color_field(Qs(MsgVariant(s->variant).color.Hex()))));
        text_field(tr("Подпись"), "label", &Step::label);

        box->addWidget(Section(tr("Пакет")));
        box->addWidget(Row(Labeled(tr("Форма"), Combo(FromCatalog(PacketShapes()), Qs(s->packet),
                                                      [=](const QString &v)
                                                      {
                                                          edit_step({},
                                                                    [&](Step &st)
                                                                    {
                                                                        st.packet = Us(v);
                                                                    });
                                                      })),
                           Labeled(tr("Движение"), Combo(EasingOptions(), Qs(ToString(s->easing)),
                                                         [=](const QString &v)
                                                         {
                                                             edit_step({},
                                                                       [&](Step &st)
                                                                       {
                                                                           st.easing = EasingFromString(Us(v)).value_or(st.easing);
                                                                       });
                                                         }))));
        box->addWidget(Row(Labeled(tr("Размер"), Spin(s->packet_size, 0.3, 5, 0.1, 1,
                                                      [=](double v)
                                                      {
                                                          edit_step(key + ":packet_size",
                                                                    [&](Step &st)
                                                                    {
                                                                        st.packet_size = v;
                                                                    });
                                                      })),
                           Labeled(tr("Количество"), Spin(s->packet_count, 1, 20, 1, 0,
                                                          [=](double v)
                                                          {
                                                              edit_step(key + ":packet_count",
                                                                        [&](Step &st)
                                                                        {
                                                                            st.packet_count = static_cast<int>(v);
                                                                        });
                                                          }))));
        box->addWidget(Check(tr("Подсвечивать пройденный путь"), s->trail,
                             [=](bool v)
                             {
                                 edit_step({},
                                           [&](Step &st)
                                           {
                                               st.trail = v;
                                           });
                             }));
        break;
    }
    case StepType::Timer:
    {
        node_combo(tr("Узел"));
        Options units;
        for (const auto &u : TimeUnits())
        {
            units.emplace_back(Qs(u.id), Qs(u.label));
        }
        box->addWidget(Row(Labeled(tr("Отсчёт от"), Spin(s->seconds, 0, 100000, 1, 0,
                                                         [=](double v)
                                                         {
                                                             edit_step(key + ":seconds",
                                                                       [&](Step &st)
                                                                       {
                                                                           st.seconds = v;
                                                                       });
                                                         })),
                           Labeled(tr("Единица"), Combo(units, Qs(s->unit),
                                                        [=](const QString &v)
                                                        {
                                                            edit_step({},
                                                                      [&](Step &st)
                                                                      {
                                                                          st.unit = Us(v);
                                                                      });
                                                        }))));
        text_field(tr("Подпись"), "label", &Step::label);
        break;
    }
    case StepType::State:
    {
        const auto &preset = NodeState(s->state);
        node_combo(tr("Узел"));
        Options states;
        for (const auto &st : NodeStates())
        {
            states.emplace_back(Qs(st.id), Qs(st.label));
        }
        box->addWidget(Labeled(tr("Состояние (пресет)"), Combo(states, Qs(s->state),
                                                               [=](const QString &v)
                                                               {
                                                                   edit_step(
                                                                       {},
                                                                       [&](Step &st)
                                                                       {
                                                                           st.state = Us(v);
                                                                       },
                                                                       true);
                                                               })));
        text_field(tr("Подпись"), "label", &Step::label, Qs(preset.label));
        box->addWidget(Row(color_field(Qs(preset.ring.Hex())), Labeled(tr("Размер шрифта"), Spin(s->label_size.value_or(11), 6, 48, 1, 0,
                                                                                                 [=](double v)
                                                                                                 {
                                                                                                     edit_step(key + ":label_size",
                                                                                                               [&](Step &st)
                                                                                                               {
                                                                                                                   st.label_size = v;
                                                                                                               });
                                                                                                 }))));
        box->addWidget(Hint(tr("Подпись и цвет переопределяют пресет; пусто — берётся из пресета.")));
        break;
    }
    case StepType::Note:
        text_field(tr("Текст"), "text", &Step::text);
        box->addWidget(Row(Labeled(QStringLiteral("X"), Spin(s->x, -100000, 100000, 10, 0,
                                                             [=](double v)
                                                             {
                                                                 edit_step(key + ":x",
                                                                           [&](Step &st)
                                                                           {
                                                                               st.x = v;
                                                                           });
                                                             })),
                           Labeled(QStringLiteral("Y"), Spin(s->y, -100000, 100000, 10, 0,
                                                             [=](double v)
                                                             {
                                                                 edit_step(key + ":y",
                                                                           [&](Step &st)
                                                                           {
                                                                               st.y = v;
                                                                           });
                                                             }))));
        box->addWidget(color_field(QStringLiteral("#fbbf24")));
        break;
    case StepType::Effect:
    {
        node_combo(tr("Узел"));
        const EffectDef *fx = _ctl.Reg().FindEffect(s->effect, &m.library);
        box->addWidget(Labeled(tr("Эффект"), Combo(_EffectOptions(), Qs(s->effect),
                                                   [=](const QString &v)
                                                   {
                                                       edit_step(
                                                           {},
                                                           [&](Step &st)
                                                           {
                                                               st.effect = Us(v);
                                                           },
                                                           true);
                                                   })));
        if (fx != nullptr && !fx->description.empty())
        {
            box->addWidget(Hint(Qs(fx->description)));
        }
        box->addWidget(color_field(fx != nullptr ? Qs(fx->color) : QStringLiteral("#22d3ee")));
        box->addWidget(Row(Labeled(tr("Интенсивность"), Spin(s->intensity, 0, 5, 0.1, 2,
                                                             [=](double v)
                                                             {
                                                                 edit_step(key + ":intensity",
                                                                           [&](Step &st)
                                                                           {
                                                                               st.intensity = v;
                                                                           });
                                                             })),
                           Labeled(tr("Повторы"), OptSpin(s->repeat > 0 ? std::optional<double>(s->repeat) : std::nullopt, 1, 100, 1, 0,
                                                          fx != nullptr ? QString::number(fx->repeat) : QString(),
                                                          [=](std::optional<double> v)
                                                          {
                                                              edit_step(key + ":repeat",
                                                                        [&](Step &st)
                                                                        {
                                                                            st.repeat = v.has_value() ? static_cast<int>(*v) : 0;
                                                                        });
                                                          }))));
        const QString effect_id = Qs(s->effect);
        box->addWidget(Button(tr("Редактировать эффект в библиотеке…"),
                              [this, effect_id]
                              {
                                  Q_EMIT LibraryRequested(effect_id);
                              }));
        break;
    }
    case StepType::Action:
        node_combo(tr("Узел (сервис)"));
        text_field(tr("Действие"), "text", &Step::text);
        box->addWidget(color_field(QStringLiteral("#38bdf8")));
        break;
    case StepType::Link:
    {
        box->addWidget(Labeled(tr("Связь"), Combo(_EdgeOptions(), Qs(s->edge_id),
                                                  [=](const QString &v)
                                                  {
                                                      edit_step({},
                                                                [&](Step &st)
                                                                {
                                                                    st.edge_id = Us(v);
                                                                });
                                                  })));
        text_field(tr("Текст"), "text", &Step::text);
        Options anims;
        for (const auto &a : LinkAnims())
        {
            anims.emplace_back(Qs(a.id), Qs(a.label));
        }
        box->addWidget(Row(color_field(QStringLiteral("#38bdf8")), Labeled(tr("Анимация"), Combo(anims, Qs(s->anim),
                                                                                                 [=](const QString &v)
                                                                                                 {
                                                                                                     edit_step({},
                                                                                                               [&](Step &st)
                                                                                                               {
                                                                                                                   st.anim = Us(v);
                                                                                                               });
                                                                                                 }))));
        _LabelControls(box, key, 22,
                       [id](auto &mm)
                       {
                           return mm.FindStep(id);
                       });
        break;
    }
    }

    box->addWidget(Section(tr("Время")));
    box->addWidget(Row(Labeled(tr("Начало, мс"), Spin(s->start, 0, 3'600'000, 50, 0,
                                                      [this, key, id](double v)
                                                      {
                                                          _ctl.Edit(key + ":start",
                                                                    [&](Model &mm)
                                                                    {
                                                                        if (Step *st = mm.FindStep(id); st != nullptr)
                                                                        {
                                                                            st->start = v;
                                                                        }
                                                                        Retime(mm);
                                                                    });
                                                      })),
                       Labeled(tr("Длит., мс"), Spin(s->duration, kMinStepDuration, 3'600'000, 50, 0,
                                                     [this, key, id](double v)
                                                     {
                                                         _ctl.Edit(key + ":duration",
                                                                   [&](Model &mm)
                                                                   {
                                                                       if (Step *st = mm.FindStep(id); st != nullptr)
                                                                       {
                                                                           st->duration = v;
                                                                       }
                                                                       Retime(mm);
                                                                   });
                                                     }))));
    box->addWidget(Button(tr("⧉ Дублировать шаг"),
                          [this, id]
                          {
                              if (const Step *copy = _ctl.Doc().DuplicateStep(id); copy != nullptr)
                              {
                                  const std::string new_id = copy->id;
                                  _ctl.Changed(true);
                                  _ctl.Select(Kind::Step, new_id);
                              }
                          }));
    box->addWidget(Button(
        tr("Удалить шаг"),
        [this]
        {
            _ctl.DeleteSelection();
        },
        true));
}

} // namespace ad::ui
