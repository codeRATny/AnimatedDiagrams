#ifndef _UI_FIELDS_HPP_
#define _UI_FIELDS_HPP_

#include <QString>

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Model/Catalog.hpp"
#include "Model/Library.hpp"

class QDialog;
class QDialogButtonBox;
class QLabel;
class QVBoxLayout;
class QWidget;

/// @file Fields.hpp
/// @brief Small form-field factories shared by the inspector and the library editor.
///
/// Every factory takes a change callback invoked with the new value. "Opt" variants
/// edit std::optional values: an empty value means "inherit" (from the element type,
/// the effect or the editor defaults).

namespace ad::ui::fields
{

using Options = std::vector<std::pair<QString, QString>>; // (value, label)

Options FromCatalog(std::span<const OptionInfo> options);
Options EasingOptions();
Options EffectPropertyOptions();

QLabel  *Section(const QString &text);
QWidget *Hint(const QString &text);
QWidget *Row(QWidget *a, QWidget *b);
QWidget *Row(QWidget *a, QWidget *b, QWidget *c);
QWidget *Labeled(const QString &label, QWidget *field);

QWidget *LineEdit(const QString &value, std::function<void(const QString &)> on_change, const QString &placeholder = {});
QWidget *Spin(double value, double min, double max, double step, int decimals, std::function<void(double)> on_change);
/// Spin box with an extra "auto" value below `min`; `fallback` is shown in the "auto" caption.
QWidget *OptSpin(std::optional<double> value, double min, double max, double step, int decimals, const QString &fallback,
                 std::function<void(std::optional<double>)> on_change);
QWidget *Combo(const Options &options, const QString &current, std::function<void(const QString &)> on_change);
QWidget *OptCombo(const Options &options, const std::optional<std::string> &current, const QString &fallback,
                  std::function<void(std::optional<std::string>)> on_change);
QWidget *ColorButton(const QString &hex, std::function<void(const QString &)> on_change);
/// Color button + reset button; an unset color shows `fallback` dimmed.
QWidget *OptColor(const std::optional<std::string> &value, const QString &fallback,
                  std::function<void(std::optional<std::string>)> on_change);
QWidget *Slider(double value, double min, double max, double step, std::function<void(double)> on_change);
QWidget *Check(const QString &label, bool value, std::function<void(bool)> on_change);
/// Tri-state check box: partially checked = inherit.
QWidget *OptCheck(const QString &label, std::optional<bool> value, bool fallback, std::function<void(std::optional<bool>)> on_change);
QWidget *Button(const QString &text, std::function<void()> on_click, bool danger = false);

/// "OK" / "Отмена" buttons wired to the dialog (independent of installed Qt translations).
QDialogButtonBox *OkCancelButtons(QDialog *dlg);

/// Edit callback: apply `fn` to the style being edited; `merge_key` groups continuous edits.
using StyleEdit = std::function<void(const std::string &merge_key, const std::function<void(NodeStyle &)> &fn)>;

/// Form for every NodeStyle field. `current` -- the style being edited, `base` -- the
/// effective inherited style (shown for unset fields).
void AddNodeStyleFields(QVBoxLayout *box, const NodeStyle &current, const NodeStyle &base, const StyleEdit &edit);

} // namespace ad::ui::fields

#endif // _UI_FIELDS_HPP_
