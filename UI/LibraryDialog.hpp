#ifndef _UI_LIBRARY_DIALOG_HPP_
#define _UI_LIBRARY_DIALOG_HPP_

#include <QDialog>

#include <functional>
#include <string>

#include "Model/Library.hpp"

class QLabel;
class QListWidget;
class QPushButton;
class QScrollArea;
class QTabWidget;
class QVBoxLayout;

/// @file LibraryDialog.hpp
/// @brief Library editor: browse element types, effects and animation templates from
///        all sources; create, duplicate, edit and delete document definitions;
///        export a selection as a plugin.

namespace ad::ui
{

class AppContext;
class Controller;
class PreviewWidget;

class LibraryDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Tab
    {
        Elements,
        Effects,
        Animations,
        Designs
    };
    static constexpr int kTabCount = 4;

    LibraryDialog(AppContext &ctx, Controller *ctl, QWidget *parent = nullptr);

    /// Work with another document (the active tab changed).
    void SetController(Controller *ctl);

    /// Switch to the tab holding `id` and select it.
    void SelectItem(const QString &id);

Q_SIGNALS:
    /// "Apply" was pressed for an animation template.
    void ApplyAnimationRequested(const QString &template_id);

private:
    struct Current
    {
        std::string id;
        std::string source;
    };

    [[nodiscard]] Tab          _CurrentTab() const;
    [[nodiscard]] QListWidget *_List(Tab tab) const;
    [[nodiscard]] bool         _Editable() const;
    [[nodiscard]] std::string  _UniqueId(const std::string &base) const;

    void _RefreshLists();
    void _OnSelectionChanged();
    void _RequestEditorRebuild();
    void _RebuildEditor();
    void _UpdatePreview();
    void _UpdateButtons();

    void _BuildElementEditor(QVBoxLayout *box, const ElementType &e);
    void _BuildEffectEditor(QVBoxLayout *box, const EffectDef &e);
    void _BuildAnimationEditor(QVBoxLayout *box, const AnimationTemplate &a);
    void _BuildDesignEditor(QVBoxLayout *box, const DesignSystem &d);

    /// Modify a document definition; an empty key starts a new undo step.
    void _EditElement(const std::string &key, const std::function<void(ElementType &)> &fn, bool rebuild = false);
    void _EditEffect(const std::string &key, const std::function<void(EffectDef &)> &fn, bool rebuild = false);
    void _EditAnimation(const std::string &key, const std::function<void(AnimationTemplate &)> &fn, bool rebuild = false);
    void _EditDesign(const std::string &key, const std::function<void(DesignSystem &)> &fn, bool rebuild = false);
    void _AfterEdit(bool rebuild);

    void _New();
    void _NewAnimation();
    void _Duplicate();
    void _Delete();
    void _ExportPlugin();

    AppContext    &_ctx;
    Controller    *_ctl              = nullptr;
    QTabWidget    *_tabs             = nullptr;
    QListWidget   *_lists[kTabCount] = {};
    QScrollArea   *_editor           = nullptr;
    PreviewWidget *_preview          = nullptr;
    QLabel        *_origin           = nullptr;
    QPushButton   *_new_button       = nullptr;
    QPushButton   *_dup_button       = nullptr;
    QPushButton   *_del_button       = nullptr;
    QPushButton   *_apply_button     = nullptr;

    Current _current;
    bool    _applying        = false; // our own edit is being applied (ignore model notifications)
    bool    _rebuild_pending = false;
    QString _override_type; // element type whose design system override is edited
};

} // namespace ad::ui

#endif // _UI_LIBRARY_DIALOG_HPP_
