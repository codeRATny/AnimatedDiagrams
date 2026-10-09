#include "PaletteWidget.hpp"

#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QScrollBar>
#include <QSettings>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <map>

#include "AppContext.hpp"
#include "Controller.hpp"
#include "QtRender.hpp"
#include "Theme.hpp"

namespace ad::ui
{

namespace
{

constexpr auto kCollapsedKey = "palette/collapsed";
constexpr int  kIdRole       = Qt::UserRole;     // element type id (element rows)
constexpr int  kGroupRole    = Qt::UserRole + 1; // group key (group rows)
const QString  kStarOn       = QStringLiteral("★");
const QString  kStarOff      = QStringLiteral("☆");

/// Tree with drag support: element rows carry their type id.
class PaletteTree : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;

protected:
    [[nodiscard]] QStringList mimeTypes() const override { return {QString::fromLatin1(kElementMime)}; }

    [[nodiscard]] QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override
    {
        if (items.isEmpty() || items.front()->data(0, kIdRole).toString().isEmpty())
        {
            return nullptr;
        }
        auto *mime = new QMimeData;
        mime->setData(QString::fromLatin1(kElementMime), items.front()->data(0, kIdRole).toString().toUtf8());
        return mime;
    }
};

QIcon Swatch(const std::string &hex)
{
    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(Qs(hex)));
    p.drawRoundedRect(QRectF(1, 1, 10, 10), 3, 3);
    return QIcon(pm);
}

} // namespace

PaletteWidget::PaletteWidget(AppContext &ctx, QWidget *parent) : QWidget(parent), _ctx(ctx)
{
    for (const QString &k : QSettings().value(kCollapsedKey).toStringList())
    {
        _collapsed.insert(k);
    }

    _search = new QLineEdit;
    _search->setPlaceholderText(tr("Search elements…"));
    _search->setClearButtonEnabled(true);
    connect(_search, &QLineEdit::textChanged, this, &PaletteWidget::_Rebuild);

    _tree = new PaletteTree;
    _tree->setColumnCount(2);
    _tree->setHeaderHidden(true);
    _tree->setRootIsDecorated(true);
    _tree->setIndentation(12);
    _tree->setDragEnabled(true);
    _tree->setDragDropMode(QAbstractItemView::DragOnly);
    _tree->setContextMenuPolicy(Qt::CustomContextMenu);
    _tree->setUniformRowHeights(true);
    _tree->header()->setStretchLastSection(false);
    _tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    _tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    _tree->header()->resizeSection(1, 26);
    connect(_tree, &QTreeWidget::itemClicked, this, &PaletteWidget::_OnClicked);
    connect(_tree, &QTreeWidget::customContextMenuRequested, this, &PaletteWidget::_ShowMenu);
    connect(_tree, &QTreeWidget::itemExpanded, this, &PaletteWidget::_SaveCollapsed);
    connect(_tree, &QTreeWidget::itemCollapsed, this, &PaletteWidget::_SaveCollapsed);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    layout->addWidget(_search);
    layout->addWidget(_tree, 1);

    connect(&_ctx, &AppContext::LibraryChanged, this, &PaletteWidget::_RequestRebuild);
    connect(&_ctx, &AppContext::FavoritesChanged, this, &PaletteWidget::_RequestRebuild);
    connect(&Theme::Instance(), &Theme::Changed, this, &PaletteWidget::_RequestRebuild); // item colors
    _Rebuild();
}

void PaletteWidget::SetDocument(Controller *ctl)
{
    if (_ctl != nullptr)
    {
        disconnect(_ctl, nullptr, this, nullptr);
    }
    _ctl = ctl;
    if (_ctl != nullptr)
    {
        connect(_ctl, &Controller::ModelChanged, this,
                [this](bool structural)
                {
                    // rebuild only when the document's own element types changed
                    if (structural && _ctl != nullptr && _ctl->GetModel().library.elements != _doc_cache)
                    {
                        _RequestRebuild();
                    }
                });
        connect(_ctl, &QObject::destroyed, this,
                [this]
                {
                    _ctl = nullptr;
                });
    }
    _RequestRebuild();
}

void PaletteWidget::SetCurrentType(const QString &id)
{
    _current = id;
    for (QTreeWidgetItemIterator it(_tree); *it != nullptr; ++it)
    {
        QFont f = (*it)->font(0);
        f.setBold((*it)->data(0, kIdRole).toString() == id && !id.isEmpty());
        (*it)->setFont(0, f);
    }
}

void PaletteWidget::_RequestRebuild()
{
    if (_pending)
    {
        return;
    }
    _pending = true;
    QTimer::singleShot(0, this,
                       [this]
                       {
                           _pending = false;
                           _Rebuild();
                       });
}

QTreeWidgetItem *PaletteWidget::_AddElement(QTreeWidgetItem *parent, const ElementType &e, const QString &source_hint)
{
    auto *item = new QTreeWidgetItem(parent);
    item->setText(0, (e.icon.empty() ? QString() : Qs(e.icon) + QLatin1Char(' ')) + Qs(e.label));
    const DesignSystem *ds = _ctl != nullptr ? _ctx.Reg().DesignOf(_ctl->GetModel()) : nullptr;
    item->setIcon(0, Swatch(ResolveColorToken(e.accent, ds)));
    const QString id = Qs(e.id);
    item->setData(0, kIdRole, id);
    QString tip = Qs(e.description.empty() ? e.label : e.description) + QStringLiteral("\n") + id;
    if (!source_hint.isEmpty())
    {
        tip += QStringLiteral(" · ") + source_hint;
    }
    item->setToolTip(0, tip);
    const bool fav = _ctx.IsFavorite(id);
    item->setText(1, fav ? kStarOn : kStarOff);
    item->setToolTip(1, fav ? tr("Remove from Favorites") : tr("Add to Favorites"));
    item->setTextAlignment(1, Qt::AlignCenter);
    item->setForeground(1, ToQColor(fav ? Ui().warning : Ui().faint));
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
    if (id == _current)
    {
        QFont f = item->font(0);
        f.setBold(true);
        item->setFont(0, f);
    }
    return item;
}

void PaletteWidget::_Rebuild()
{
    const LibrarySet *doc = _ctl != nullptr ? &_ctl->GetModel().library : nullptr;
    _doc_cache            = doc != nullptr ? doc->elements : std::vector<ElementType>{};
    const auto    entries = _ctx.Reg().Elements(doc);
    const QString filter  = _search->text().trimmed();
    auto          matches = [&filter](const ElementType &e)
    {
        return filter.isEmpty() || Qs(e.label).contains(filter, Qt::CaseInsensitive) || Qs(e.id).contains(filter, Qt::CaseInsensitive) ||
               Qs(e.category).contains(filter, Qt::CaseInsensitive);
    };

    const int scroll = _tree->verticalScrollBar() != nullptr ? _tree->verticalScrollBar()->value() : 0;
    _tree->clear();

    auto group = [this, &filter](const QString &key, const QString &title)
    {
        auto *g = new QTreeWidgetItem(_tree);
        g->setText(0, title);
        g->setData(0, kGroupRole, key);
        g->setFlags(Qt::ItemIsEnabled);
        g->setFirstColumnSpanned(true);
        QFont f = g->font(0);
        f.setBold(true);
        f.setPointSizeF(f.pointSizeF() * 0.92);
        g->setFont(0, f);
        g->setForeground(0, ToQColor(Ui().muted));
        g->setExpanded(!filter.isEmpty() || !_collapsed.contains(key));
        return g;
    };
    auto source_name = [this](const std::string &source)
    {
        if (source == kBuiltinSource)
        {
            return tr("Built-in");
        }
        if (source == kDocumentSource)
        {
            return tr("Document");
        }
        const PluginRecord *rec = _ctx.Plugins().Find(source);
        return rec != nullptr && !rec->plugin.info.name.empty() ? Qs(rec->plugin.info.name) : Qs(source);
    };

    // favorites (in the order they were added)
    QTreeWidgetItem *fav = group(QStringLiteral("fav"), QStringLiteral("★ ") + tr("Favorites"));
    for (const QString &id : _ctx.Favorites())
    {
        const auto it = std::ranges::find_if(entries,
                                             [&id](const auto &e)
                                             {
                                                 return Qs(e.def->id) == id;
                                             });
        if (it != entries.end() && matches(*it->def))
        {
            _AddElement(fav, *it->def, source_name(it->source));
        }
    }
    if (fav->childCount() == 0)
    {
        if (filter.isEmpty())
        {
            auto *hint = new QTreeWidgetItem(fav);
            hint->setText(0, tr("Click ☆ next to an element"));
            hint->setFlags(Qt::ItemIsEnabled);
            hint->setForeground(0, ToQColor(Ui().faint));
        }
        else
        {
            delete fav;
        }
    }

    // sources in registry order: built-in -> plugins -> document
    std::vector<std::string>                                             order;
    std::map<std::string, std::vector<const ElementType *>, std::less<>> by_source;
    for (const auto &e : entries)
    {
        if (!matches(*e.def))
        {
            continue;
        }
        if (!by_source.contains(e.source))
        {
            order.push_back(e.source);
        }
        by_source[e.source].push_back(e.def);
    }
    for (const auto &source : order)
    {
        const auto      &defs = by_source[source];
        const QString    key  = QStringLiteral("src:") + Qs(source);
        QTreeWidgetItem *g    = group(key, source_name(source));
        // category sub-groups when the source has several categories
        std::vector<std::string> categories;
        for (const ElementType *d : defs)
        {
            if (std::ranges::find(categories, d->category) == categories.end())
            {
                categories.push_back(d->category);
            }
        }
        for (const auto &cat : categories)
        {
            QTreeWidgetItem *parent = g;
            if (categories.size() > 1)
            {
                parent = new QTreeWidgetItem(g);
                parent->setText(0, Qs(cat));
                const QString ckey = key + QStringLiteral("/") + Qs(cat);
                parent->setData(0, kGroupRole, ckey);
                parent->setFlags(Qt::ItemIsEnabled);
                parent->setFirstColumnSpanned(true);
                parent->setForeground(0, ToQColor(Ui().muted));
                parent->setExpanded(!filter.isEmpty() || !_collapsed.contains(ckey));
            }
            for (const ElementType *d : defs)
            {
                if (d->category == cat)
                {
                    _AddElement(parent, *d, {});
                }
            }
        }
    }
    QTimer::singleShot(0, this,
                       [this, scroll]
                       {
                           _tree->verticalScrollBar()->setValue(scroll);
                       });
}

void PaletteWidget::_SaveCollapsed()
{
    if (!_search->text().trimmed().isEmpty())
    {
        return; // search expands everything temporarily
    }
    for (QTreeWidgetItemIterator it(_tree); *it != nullptr; ++it)
    {
        const QString key = (*it)->data(0, kGroupRole).toString();
        if (key.isEmpty())
        {
            continue;
        }
        if ((*it)->isExpanded())
        {
            _collapsed.remove(key);
        }
        else
        {
            _collapsed.insert(key);
        }
    }
    QSettings().setValue(kCollapsedKey, QStringList(_collapsed.begin(), _collapsed.end()));
}

void PaletteWidget::_OnClicked(QTreeWidgetItem *item, int column)
{
    const QString id = item->data(0, kIdRole).toString();
    if (id.isEmpty())
    {
        if (item->childCount() > 0 && column == 0)
        {
            item->setExpanded(!item->isExpanded());
        }
        return;
    }
    if (column == 1)
    {
        _ctx.SetFavorite(id, !_ctx.IsFavorite(id));
        return;
    }
    SetCurrentType(id);
    Q_EMIT TypeChosen(id);
}

void PaletteWidget::_ShowMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = _tree->itemAt(pos);
    const QString    id   = item != nullptr ? item->data(0, kIdRole).toString() : QString();
    if (id.isEmpty())
    {
        return;
    }
    QMenu      menu(this);
    const bool fav = _ctx.IsFavorite(id);
    menu.addAction(fav ? tr("Remove from Favorites") : tr("Add to Favorites"),
                   [this, id, fav]
                   {
                       _ctx.SetFavorite(id, !fav);
                   });
    menu.addAction(tr("Open in library…"),
                   [this, id]
                   {
                       Q_EMIT LibraryRequested(id);
                   });
    menu.exec(_tree->viewport()->mapToGlobal(pos));
}

} // namespace ad::ui
