#ifndef _UI_INSPECTOR_HPP_
#define _UI_INSPECTOR_HPP_

#include <QScrollArea>

#include <string>

#include "Fields.hpp"

class QLabel;
class QVBoxLayout;

/// @file Inspector.hpp
/// @brief Property form of the selected node, edge or scenario step; scene settings
///        when nothing is selected.

namespace ad::ui
{

class Controller;

class Inspector : public QScrollArea
{
    Q_OBJECT

public:
    explicit Inspector(Controller &ctl, QWidget *parent = nullptr);

Q_SIGNALS:
    /// "Open the library" request (e.g. to edit the effect used by a step).
    void LibraryRequested(const QString &item_id);

private:
    void _RequestRebuild(bool force);
    void _Rebuild();
    void _BuildScene(QVBoxLayout *box);
    void _BuildNode(QVBoxLayout *box, const std::string &id);
    void _BuildEdge(QVBoxLayout *box, const std::string &id);
    void _BuildStep(QVBoxLayout *box, const std::string &id);
    void _SaveNodeAsType(const std::string &node_id);

    [[nodiscard]] fields::Options _NodeOptions() const;
    [[nodiscard]] fields::Options _EdgeOptions() const;
    [[nodiscard]] fields::Options _PortOptions(const std::string &node_id) const;
    [[nodiscard]] fields::Options _ElementOptions() const;
    [[nodiscard]] fields::Options _EffectOptions() const;

    /// Font size / offset / position of a label (edge or link step); get(Model&) -> Edge* | Step*.
    template <class Get>
    void _LabelControls(QVBoxLayout *box, const std::string &key, double def_offset, Get get);

    Controller &_ctl;
    QLabel     *_title           = nullptr;
    bool        _rebuild_pending = false;
};

} // namespace ad::ui

#endif // _UI_INSPECTOR_HPP_
