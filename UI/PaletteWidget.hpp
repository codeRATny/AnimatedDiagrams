#ifndef _UI_PALETTE_WIDGET_HPP_
#define _UI_PALETTE_WIDGET_HPP_

#include <QSet>
#include <QWidget>

#include <vector>

#include "Model/Library.hpp"

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

/// @file PaletteWidget.hpp
/// @brief Element palette: favorites first, then groups per source (built-in, every
///        plugin, the document) with category sub-groups; search; drag to the canvas.

namespace ad::ui
{

class AppContext;
class Controller;

/// MIME type of an element type id dragged from the palette.
inline constexpr char kElementMime[] = "application/x-animated-diagrams-element";

class PaletteWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PaletteWidget(AppContext &ctx, QWidget *parent = nullptr);

    /// Document whose own element types are listed (the active tab).
    void SetDocument(Controller *ctl);
    /// Highlight the type placed by the "node" tool.
    void SetCurrentType(const QString &id);

Q_SIGNALS:
    void TypeChosen(const QString &id);
    void LibraryRequested(const QString &id);

private:
    void             _RequestRebuild();
    void             _Rebuild();
    void             _ShowMenu(const QPoint &pos);
    void             _OnClicked(QTreeWidgetItem *item, int column);
    void             _SaveCollapsed();
    QTreeWidgetItem *_AddElement(QTreeWidgetItem *parent, const ElementType &e, const QString &source_hint);

    AppContext              &_ctx;
    Controller              *_ctl    = nullptr;
    QLineEdit               *_search = nullptr;
    QTreeWidget             *_tree   = nullptr;
    QString                  _current;
    QSet<QString>            _collapsed;
    std::vector<ElementType> _doc_cache;
    bool                     _pending = false;
};

} // namespace ad::ui

#endif // _UI_PALETTE_WIDGET_HPP_
