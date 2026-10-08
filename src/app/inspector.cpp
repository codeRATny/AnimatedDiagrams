#include "inspector.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>

#include "ad/catalog.hpp"
#include "ad/document.hpp"
#include "controller.hpp"
#include "qt_render.hpp"

namespace app {

using Kind = ad::Selection::Kind;

namespace {

QLabel* sectionLabel(const QString& text) {
    auto* l = new QLabel(text);
    l->setObjectName("inspectorSection");
    return l;
}

/// Упорядочить шаги и пересчитать длительность — после изменения времени шага.
void retime(ad::Model& m) {
    std::ranges::stable_sort(m.scenario.steps, {}, &ad::Step::start);
    m.scenario.duration = ad::autoDuration(m.scenario);
}

}  // namespace

Inspector::Inspector(Controller& ctl, QWidget* parent) : QScrollArea(parent), ctl_(ctl) {
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMinimumWidth(260);
    connect(&ctl_, &Controller::selectionChanged, this, [this] { requestRebuild(true); });
    connect(&ctl_, &Controller::modelChanged, this, [this](bool structural) { requestRebuild(structural); });
    rebuild();
}

void Inspector::requestRebuild(bool force) {
    if (!force) {
        // пока пользователь вводит значение в инспекторе — форму не пересобираем (иначе слетит фокус)
        const QWidget* f = QApplication::focusWidget();
        if (f && isAncestorOf(f)) return;
    }
    if (rebuildPending_) return;
    rebuildPending_ = true;
    QTimer::singleShot(0, this, [this] {
        rebuildPending_ = false;
        rebuild();
    });
}

void Inspector::rebuild() {
    const int scroll = verticalScrollBar()->value();
    auto* content = new QWidget;
    auto* box = new QVBoxLayout(content);
    box->setContentsMargins(12, 10, 12, 12);
    box->setSpacing(8);
    title_ = new QLabel;
    title_->setObjectName("inspectorTitle");
    box->addWidget(title_);

    const auto& sel = ctl_.selection();
    switch (sel.kind) {
        case Kind::Node: buildNode(box, sel.id); break;
        case Kind::Edge: buildEdge(box, sel.id); break;
        case Kind::Step: buildStep(box, sel.id); break;
        case Kind::None: buildEmpty(box); break;
    }
    box->addStretch(1);

    QWidget* old = takeWidget();
    setWidget(content);
    if (old) old->deleteLater();
    QTimer::singleShot(0, this, [this, scroll] { verticalScrollBar()->setValue(scroll); });
}

// ---- фабрики полей ----------------------------------------------------------

QWidget* Inspector::lineEdit(const QString& value, std::function<void(const QString&)> onChange, const QString& placeholder) {
    auto* e = new QLineEdit(value);
    e->setPlaceholderText(placeholder);
    connect(e, &QLineEdit::textEdited, this, [f = std::move(onChange)](const QString& v) { f(v); });
    return e;
}

QWidget* Inspector::spin(double value, double min, double max, double step, int decimals, std::function<void(double)> onChange) {
    auto* s = new QDoubleSpinBox;
    s->setRange(min, max);
    s->setDecimals(decimals);
    s->setSingleStep(step);
    s->setValue(value);
    s->setFocusPolicy(Qt::StrongFocus);
    connect(s, &QDoubleSpinBox::valueChanged, this, [f = std::move(onChange)](double v) { f(v); });
    return s;
}

QWidget* Inspector::combo(const Options& options, const QString& current, std::function<void(const QString&)> onChange) {
    auto* c = new QComboBox;
    for (const auto& [value, label] : options) c->addItem(label, value);
    const int idx = c->findData(current);
    c->setCurrentIndex(idx >= 0 ? idx : 0);
    connect(c, &QComboBox::activated, this, [c, f = std::move(onChange)](int i) { f(c->itemData(i).toString()); });
    return c;
}

QWidget* Inspector::colorButton(const QString& hex, std::function<void(const QString&)> onChange) {
    auto* b = new QPushButton(hex);
    b->setObjectName("colorButton");
    const QColor c(hex);
    b->setStyleSheet(QStringLiteral("QPushButton#colorButton { background: %1; color: %2; }")
                         .arg(hex, c.lightness() > 140 ? QStringLiteral("#0b1220") : QStringLiteral("#ffffff")));
    connect(b, &QPushButton::clicked, this, [this, hex, f = std::move(onChange)] {
        const QColor picked = QColorDialog::getColor(QColor(hex), this, tr("Цвет"));
        if (picked.isValid()) f(picked.name());
    });
    return b;
}

QWidget* Inspector::slider(double value, double min, double max, double step, std::function<void(double)> onChange) {
    auto* s = new QSlider(Qt::Horizontal);
    const int steps = static_cast<int>(std::lround((max - min) / step));
    s->setRange(0, steps);
    s->setValue(static_cast<int>(std::lround((value - min) / step)));
    connect(s, &QSlider::valueChanged, this, [min, step, f = std::move(onChange)](int v) { f(min + v * step); });
    return s;
}

QWidget* Inspector::check(const QString& label, bool value, std::function<void(bool)> onChange) {
    auto* c = new QCheckBox(label);
    c->setChecked(value);
    connect(c, &QCheckBox::toggled, this, [f = std::move(onChange)](bool v) { f(v); });
    return c;
}

QWidget* Inspector::button(const QString& text, std::function<void()> onClick, bool danger) {
    auto* b = new QPushButton(text);
    if (danger) b->setObjectName("dangerButton");
    connect(b, &QPushButton::clicked, this, [f = std::move(onClick)] { f(); });
    return b;
}

QWidget* Inspector::hint(const QString& text) {
    auto* l = new QLabel(text);
    l->setObjectName("hint");
    l->setWordWrap(true);
    return l;
}

QWidget* Inspector::row(QWidget* a, QWidget* b) {
    auto* w = new QWidget;
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(8);
    l->addWidget(a, 1);
    l->addWidget(b, 1);
    return w;
}

QWidget* Inspector::labeled(const QString& label, QWidget* field) {
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(3);
    auto* cap = new QLabel(label);
    cap->setObjectName("fieldLabel");
    l->addWidget(cap);
    l->addWidget(field);
    return w;
}

Inspector::Options Inspector::nodeOptions() const {
    Options o;
    for (const auto& n : ctl_.model().nodes) o.emplace_back(qs(n.id), qs(n.label));
    return o;
}

Inspector::Options Inspector::edgeOptions() const {
    const auto& m = ctl_.model();
    auto nm = [&](const std::string& id) { return m.node(id) ? qs(m.node(id)->label) : QStringLiteral("?"); };
    std::map<std::string, int> seen;
    Options o;
    for (const auto& e : m.edges) {
        const int n = ++seen[e.from + ">" + e.to];
        QString label = nm(e.from) + QStringLiteral(" → ") + nm(e.to);
        if (n > 1) label += QStringLiteral(" (%1)").arg(n);
        if (!e.label.empty()) label += QStringLiteral(" · ") + qs(e.label);
        o.emplace_back(qs(e.id), label);
    }
    return o;
}

Inspector::Options Inspector::portOptions(const std::string& nodeId) const {
    Options o{{QString(), tr("Авто")}};
    if (const ad::Node* n = ctl_.model().node(nodeId))
        for (std::size_t i = 0; i < n->ports.size(); ++i) o.emplace_back(qs(n->ports[i].id), tr("Точка %1").arg(i + 1));
    return o;
}

template <class Get>
void Inspector::labelControls(QVBoxLayout* box, const std::string& key, double defOffset, Get get) {
    const auto* obj = get(const_cast<ad::Model&>(ctl_.model()));
    if (!obj) return;
    box->addWidget(row(
        labeled(tr("Размер шрифта"), spin(obj->labelSize.value_or(12), 6, 72, 1, 0, [this, key, get](double v) {
                    ctl_.edit(key + ":labelSize", [&](ad::Model& m) { if (auto* o = get(m)) o->labelSize = v; });
                })),
        labeled(tr("Смещение"), spin(obj->labelOff.value_or(defOffset), -200, 200, 1, 0, [this, key, get](double v) {
                    ctl_.edit(key + ":labelOff", [&](ad::Model& m) { if (auto* o = get(m)) o->labelOff = v; });
                }))));
    box->addWidget(labeled(tr("Положение вдоль связи"),
                           slider(obj->labelPos.value_or(0.5), 0, 1, 0.02, [this, key, get](double v) {
                               ctl_.edit(key + ":labelPos", [&](ad::Model& m) { if (auto* o = get(m)) o->labelPos = v; });
                           })));
}

// ---- формы ------------------------------------------------------------------

void Inspector::buildEmpty(QVBoxLayout* box) {
    title_->setText(tr("Свойства"));
    box->addWidget(hint(tr("Ничего не выбрано.\nВыберите узел, связь или шаг сценария, чтобы редактировать свойства.")));
}

void Inspector::buildNode(QVBoxLayout* box, const std::string& id) {
    const ad::Node* n = ctl_.model().node(id);
    if (!n) return buildEmpty(box);
    title_->setText(tr("Узел"));
    const std::string key = "node:" + id;
    auto editNode = [this, id](const std::string& k, auto fn, bool structural = false) {
        ctl_.edit(k, [&](ad::Model& m) { if (ad::Node* node = m.node(id)) fn(*node); }, structural);
    };

    box->addWidget(labeled(tr("Название"), lineEdit(qs(n->label), [=](const QString& v) {
                               editNode(key + ":label", [&](ad::Node& x) { x.label = us(v); });
                           })));
    box->addWidget(labeled(tr("Подзаголовок"), lineEdit(qs(n->subtitle), [=](const QString& v) {
                               editNode(key + ":subtitle", [&](ad::Node& x) { x.subtitle = us(v); });
                           })));
    Options kinds;
    for (const auto& k : ad::nodeKinds()) kinds.emplace_back(qs(k.id), qs(k.label));
    box->addWidget(labeled(tr("Тип"), combo(kinds, qs(n->kind), [=](const QString& v) {
                               editNode({}, [&](ad::Node& x) {
                                   x.kind = us(v);
                                   x.shape = std::string(ad::nodeKind(x.kind).shape);
                               });
                           })));
    box->addWidget(labeled(tr("Цвет акцента"), colorButton(qs(n->color), [=](const QString& v) {
                               editNode({}, [&](ad::Node& x) { x.color = us(v); }, true);
                           })));
    box->addWidget(row(labeled(tr("Ширина"), spin(n->w, 40, 2000, 10, 0, [=](double v) {
                                   editNode(key + ":w", [&](ad::Node& x) { x.w = v; });
                               })),
                       labeled(tr("Высота"), spin(n->h, 30, 2000, 10, 0, [=](double v) {
                                   editNode(key + ":h", [&](ad::Node& x) { x.h = v; });
                               }))));

    box->addWidget(sectionLabel(tr("Точки соединения")));
    for (std::size_t i = 0; i < n->ports.size(); ++i) {
        const std::string portId = n->ports[i].id;
        auto* lbl = new QLabel(tr("Точка %1").arg(i + 1));
        auto* rm = qobject_cast<QPushButton*>(button(QStringLiteral("✕"), [this, id, portId] {
            ctl_.document().removePort(id, portId);
            ctl_.changed(true);
        }, true));
        rm->setFixedWidth(36);
        auto* w = new QWidget;
        auto* l = new QHBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(lbl, 1);
        l->addWidget(rm);
        box->addWidget(w);
    }
    box->addWidget(button(tr("＋ Добавить точку соединения"), [this, id] {
        ctl_.document().addPort(id);
        ctl_.changed(true);
    }));
    box->addWidget(hint(tr("Точки можно перетаскивать. В режиме «Связь» кликните по точке, чтобы привязать к ней связь.")));
    box->addWidget(button(tr("Удалить узел"), [this] { ctl_.deleteSelection(); }, true));
}

void Inspector::buildEdge(QVBoxLayout* box, const std::string& id) {
    const auto& m = ctl_.model();
    const ad::Edge* e = m.edge(id);
    if (!e) return buildEmpty(box);
    title_->setText(tr("Связь"));
    const std::string key = "edge:" + id;
    auto editEdge = [this, id](const std::string& k, auto fn, bool structural = false) {
        ctl_.edit(k, [&](ad::Model& mm) { if (ad::Edge* edge = mm.edge(id)) fn(*edge); }, structural);
    };
    auto nm = [&](const std::string& nid) { return m.node(nid) ? qs(m.node(nid)->label) : QStringLiteral("?"); };
    box->addWidget(hint(nm(e->from) + QStringLiteral(" → ") + nm(e->to)));

    box->addWidget(labeled(tr("Подпись"), lineEdit(qs(e->label), [=](const QString& v) {
                               editEdge(key + ":label", [&](ad::Edge& x) { x.label = us(v); });
                           })));
    labelControls(box, key, 10, [id](ad::Model& mm) { return mm.edge(id); });

    box->addWidget(labeled(tr("Стиль"), combo({{"solid", tr("Сплошная")}, {"dashed", tr("Пунктир")}}, qs(e->style),
                                              [=](const QString& v) { editEdge({}, [&](ad::Edge& x) { x.style = us(v); }); })));
    box->addWidget(row(labeled(tr("Вход (от)"), combo(portOptions(e->from), qs(e->fromPort), [=](const QString& v) {
                                   editEdge({}, [&](ad::Edge& x) { x.fromPort = us(v); });
                               })),
                       labeled(tr("Выход (к)"), combo(portOptions(e->to), qs(e->toPort), [=](const QString& v) {
                                   editEdge({}, [&](ad::Edge& x) { x.toPort = us(v); });
                               }))));

    const bool hasWp = !e->waypoints.empty();
    QWidget* curve = slider(e->curve, -0.5, 0.5, 0.05, [=](double v) {
        editEdge(key + ":curve", [&](ad::Edge& x) { x.curve = std::round(v * 100) / 100; });
    });
    curve->setEnabled(!hasWp);
    box->addWidget(labeled(hasWp ? tr("Изгиб (задан точками)") : tr("Изгиб"), curve));
    box->addWidget(check(tr("Двунаправленная (стрелки с обеих сторон)"), e->bidirectional, [=](bool v) {
        editEdge({}, [&](ad::Edge& x) { x.bidirectional = v; });
    }));

    auto* wpLbl = new QLabel(tr("Точек изгиба: %1").arg(e->waypoints.size()));
    auto* clear = button(tr("Очистить"), [=] { editEdge({}, [](ad::Edge& x) { x.waypoints.clear(); }, true); });
    clear->setEnabled(hasWp);
    box->addWidget(row(wpLbl, clear));
    box->addWidget(hint(tr("Двойной клик по связи — добавить точку изгиба; по точке — удалить. Точки перетаскиваются.")));
    box->addWidget(button(tr("⇄ Развернуть направление"), [this, id] {
        ctl_.document().reverseEdge(id);
        ctl_.changed(true);
    }));
    box->addWidget(button(tr("Удалить связь"), [this] { ctl_.deleteSelection(); }, true));
}

void Inspector::buildStep(QVBoxLayout* box, const std::string& id) {
    const auto& m = ctl_.model();
    const ad::Step* s = m.step(id);
    if (!s) return buildEmpty(box);
    title_->setText(tr("Шаг: %1").arg(qs(ad::stepTypeLabel(s->type))));
    const std::string key = "step:" + id;
    auto editStep = [this, id](const std::string& k, auto fn, bool structural = false) {
        ctl_.edit(k, [&](ad::Model& mm) { if (ad::Step* st = mm.step(id)) fn(*st); }, structural);
    };

    Options types;
    for (const auto& t : ad::stepTypes()) types.emplace_back(qs(t.id), qs(t.label));
    box->addWidget(labeled(tr("Тип шага"), combo(types, qs(ad::toString(s->type)), [=, this](const QString& v) {
                               editStep({}, [&](ad::Step& st) {
                                   st.type = ad::stepTypeFromString(us(v)).value_or(st.type);
                                   ctl_.document().normalizeStep(st);
                               }, true);
                           })));

    auto nodeCombo = [&](const QString& label) {
        box->addWidget(labeled(label, combo(nodeOptions(), qs(s->nodeId), [=](const QString& v) {
                                   editStep({}, [&](ad::Step& st) { st.nodeId = us(v); });
                               })));
    };
    auto colorField = [&](const QString& fallback) {
        return labeled(tr("Цвет"), colorButton(s->color.empty() ? fallback : qs(s->color), [=](const QString& v) {
                           editStep({}, [&](ad::Step& st) { st.color = us(v); }, true);
                       }));
    };

    switch (s->type) {
        case ad::StepType::Message: {
            Options edges{{QString(), tr("— по узлам (без связи) —")}};
            for (auto& o : edgeOptions()) edges.push_back(std::move(o));
            box->addWidget(labeled(tr("Связь"), combo(edges, qs(s->edgeId), [=, this](const QString& v) {
                                       editStep({}, [&](ad::Step& st) {
                                           st.edgeId = us(v);
                                           if (const ad::Edge* e = ctl_.model().edge(st.edgeId)) {
                                               st.from = e->from;
                                               st.to = e->to;
                                           }
                                       }, true);
                                   })));
            if (m.edge(s->edgeId)) {
                auto nm = [&](const std::string& nid) { return m.node(nid) ? qs(m.node(nid)->label) : QStringLiteral("?"); };
                box->addWidget(button(tr("Направление: %1 → %2  ⇄").arg(nm(s->from), nm(s->to)), [=] {
                    editStep({}, [](ad::Step& st) { std::swap(st.from, st.to); }, true);
                }));
            } else {
                box->addWidget(labeled(tr("От"), combo(nodeOptions(), qs(s->from), [=](const QString& v) {
                                           editStep({}, [&](ad::Step& st) { st.from = us(v); });
                                       })));
                box->addWidget(labeled(tr("К"), combo(nodeOptions(), qs(s->to), [=](const QString& v) {
                                           editStep({}, [&](ad::Step& st) { st.to = us(v); });
                                       })));
            }
            Options variants;
            for (const auto& v : ad::msgVariants()) variants.emplace_back(qs(v.id), qs(v.label));
            box->addWidget(labeled(tr("Вариант"), combo(variants, qs(s->variant), [=](const QString& v) {
                                       editStep({}, [&](ad::Step& st) { st.variant = us(v); });
                                   })));
            box->addWidget(labeled(tr("Подпись"), lineEdit(qs(s->label), [=](const QString& v) {
                                       editStep(key + ":label", [&](ad::Step& st) { st.label = us(v); });
                                   })));
            break;
        }
        case ad::StepType::Timer: {
            nodeCombo(tr("Узел"));
            Options units;
            for (const auto& u : ad::timeUnits()) units.emplace_back(qs(u.id), qs(u.label));
            box->addWidget(row(labeled(tr("Отсчёт от"), spin(s->seconds, 0, 100000, 1, 0, [=](double v) {
                                           editStep(key + ":seconds", [&](ad::Step& st) { st.seconds = v; });
                                       })),
                               labeled(tr("Единица"), combo(units, qs(s->unit), [=](const QString& v) {
                                           editStep({}, [&](ad::Step& st) { st.unit = us(v); });
                                       }))));
            box->addWidget(labeled(tr("Подпись"), lineEdit(qs(s->label), [=](const QString& v) {
                                       editStep(key + ":label", [&](ad::Step& st) { st.label = us(v); });
                                   })));
            break;
        }
        case ad::StepType::State: {
            const auto& preset = ad::nodeState(s->state);
            nodeCombo(tr("Узел"));
            Options states;
            for (const auto& st : ad::nodeStates()) states.emplace_back(qs(st.id), qs(st.label));
            box->addWidget(labeled(tr("Состояние (пресет)"), combo(states, qs(s->state), [=](const QString& v) {
                                       editStep({}, [&](ad::Step& st) { st.state = us(v); }, true);
                                   })));
            box->addWidget(labeled(tr("Подпись"), lineEdit(qs(s->label), [=](const QString& v) {
                                       editStep(key + ":label", [&](ad::Step& st) { st.label = us(v); });
                                   }, qs(preset.label))));
            auto* reset = button(tr("Сбросить цвет"), [=] { editStep({}, [](ad::Step& st) { st.color.clear(); }, true); });
            reset->setEnabled(!s->color.empty());
            box->addWidget(row(colorField(qs(preset.ring.hex())),
                               labeled(tr("Размер шрифта"), spin(s->labelSize.value_or(11), 6, 48, 1, 0, [=](double v) {
                                           editStep(key + ":labelSize", [&](ad::Step& st) { st.labelSize = v; });
                                       }))));
            box->addWidget(reset);
            box->addWidget(hint(tr("Подпись и цвет переопределяют пресет; пусто — берётся из пресета.")));
            break;
        }
        case ad::StepType::Note:
            box->addWidget(labeled(tr("Текст"), lineEdit(qs(s->text), [=](const QString& v) {
                                       editStep(key + ":text", [&](ad::Step& st) { st.text = us(v); });
                                   })));
            box->addWidget(row(labeled(QStringLiteral("X"), spin(s->x, -100000, 100000, 10, 0, [=](double v) {
                                           editStep(key + ":x", [&](ad::Step& st) { st.x = v; });
                                       })),
                               labeled(QStringLiteral("Y"), spin(s->y, -100000, 100000, 10, 0, [=](double v) {
                                           editStep(key + ":y", [&](ad::Step& st) { st.y = v; });
                                       }))));
            box->addWidget(colorField(QStringLiteral("#fbbf24")));
            break;
        case ad::StepType::Pulse:
            nodeCombo(tr("Узел"));
            box->addWidget(colorField(QStringLiteral("#22d3ee")));
            break;
        case ad::StepType::Action:
            nodeCombo(tr("Узел (сервис)"));
            box->addWidget(labeled(tr("Действие"), lineEdit(qs(s->text), [=](const QString& v) {
                                       editStep(key + ":text", [&](ad::Step& st) { st.text = us(v); });
                                   })));
            box->addWidget(colorField(QStringLiteral("#38bdf8")));
            break;
        case ad::StepType::Link: {
            box->addWidget(labeled(tr("Связь"), combo(edgeOptions(), qs(s->edgeId), [=](const QString& v) {
                                       editStep({}, [&](ad::Step& st) { st.edgeId = us(v); });
                                   })));
            box->addWidget(labeled(tr("Текст"), lineEdit(qs(s->text), [=](const QString& v) {
                                       editStep(key + ":text", [&](ad::Step& st) { st.text = us(v); });
                                   })));
            Options anims;
            for (const auto& a : ad::linkAnims()) anims.emplace_back(qs(a.id), qs(a.label));
            box->addWidget(row(colorField(QStringLiteral("#38bdf8")),
                               labeled(tr("Анимация"), combo(anims, qs(s->anim), [=](const QString& v) {
                                           editStep({}, [&](ad::Step& st) { st.anim = us(v); });
                                       }))));
            labelControls(box, key, 22, [id](ad::Model& mm) { return mm.step(id); });
            break;
        }
    }

    box->addWidget(sectionLabel(tr("Время")));
    box->addWidget(row(labeled(tr("Начало, мс"), spin(s->start, 0, 3'600'000, 50, 0, [=, this](double v) {
                                   ctl_.edit(key + ":start", [&](ad::Model& mm) {
                                       if (ad::Step* st = mm.step(id)) st->start = v;
                                       retime(mm);
                                   });
                               })),
                       labeled(tr("Длит., мс"), spin(s->duration, ad::kMinStepDuration, 3'600'000, 50, 0, [=, this](double v) {
                                   ctl_.edit(key + ":duration", [&](ad::Model& mm) {
                                       if (ad::Step* st = mm.step(id)) st->duration = v;
                                       retime(mm);
                                   });
                               }))));
    box->addWidget(button(tr("⧉ Дублировать шаг"), [this, id] {
        if (const ad::Step* copy = ctl_.document().duplicateStep(id)) {
            const std::string newId = copy->id;
            ctl_.changed(true);
            ctl_.select(Kind::Step, newId);
        }
    }));
    box->addWidget(button(tr("Удалить шаг"), [this] { ctl_.deleteSelection(); }, true));
}

}  // namespace app
