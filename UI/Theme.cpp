#include "Theme.hpp"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QWidget>

#include <utility>

#include "QtRender.hpp"

namespace ad::ui
{

namespace
{

QString Css(Color c) { return QString::fromStdString(c.Hex()); }

QPalette MakePalette(const UiPalette &u)
{
    const QColor disabled = ToQColor(u.faint);
    QPalette     p;
    p.setColor(QPalette::Window, ToQColor(u.window));
    p.setColor(QPalette::WindowText, ToQColor(u.text));
    p.setColor(QPalette::Base, ToQColor(u.base));
    p.setColor(QPalette::AlternateBase, ToQColor(u.panel));
    p.setColor(QPalette::ToolTipBase, ToQColor(u.hover));
    p.setColor(QPalette::ToolTipText, ToQColor(u.text));
    p.setColor(QPalette::PlaceholderText, ToQColor(u.muted));
    p.setColor(QPalette::Text, ToQColor(u.text));
    p.setColor(QPalette::Button, ToQColor(u.panel));
    p.setColor(QPalette::ButtonText, ToQColor(u.text));
    p.setColor(QPalette::BrightText, ToQColor(u.accent_text));
    p.setColor(QPalette::Highlight, ToQColor(u.accent));
    p.setColor(QPalette::HighlightedText, ToQColor(u.accent_text));
    p.setColor(QPalette::Link, ToQColor(u.accent));
    p.setColor(QPalette::LinkVisited, ToQColor(u.accent));
    p.setColor(QPalette::Light, ToQColor(u.hover));
    p.setColor(QPalette::Midlight, ToQColor(u.panel));
    p.setColor(QPalette::Mid, ToQColor(u.border));
    p.setColor(QPalette::Dark, ToQColor(u.base));
    p.setColor(QPalette::Shadow, ToQColor(u.light ? Color::Rgb(0x94a3b8) : Color::Rgb(0x000000)));
    p.setColor(QPalette::Disabled, QPalette::Text, disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    return p;
}

QString MakeStyleSheet(const UiPalette &u)
{
    QString                              css    = QStringLiteral(R"(
        QToolTip { color: {text}; background: {hover}; border: 1px solid {border}; padding: 4px; }
        QLabel#inspectorTitle { font-size: 15px; font-weight: 600; padding-bottom: 4px; }
        QLabel#inspectorSection, QLabel#panelTitle {
            color: {muted}; font-size: 11px; font-weight: 600; padding-top: 6px;
        }
        QLabel#fieldLabel { color: {muted}; font-size: 12px; }
        QLabel#hint { color: {muted}; font-size: 12px; }
        QLabel#errorText { color: {danger}; }
        QLabel#readout { font-family: monospace; padding: 2px 8px; background: {base};
                         border: 1px solid {border}; border-radius: 6px; }
        QWidget#jobCard { background: {panel}; border: 1px solid {border}; border-radius: 6px; }
        QPushButton { padding: 5px 10px; border: 1px solid {border}; border-radius: 6px; background: {panel}; color: {text}; }
        QPushButton:hover { background: {hover}; }
        QPushButton:checked { background: {selected}; border-color: {accent}; }
        QPushButton:disabled { color: {faint}; }
        QPushButton#dangerButton { border-color: {danger}; color: {danger}; }
        QPushButton#dangerButton:hover { background: {danger_bg}; }
        QPushButton#primaryButton { background: {accent}; border-color: {accent}; color: {accent_text}; }
        QPushButton#primaryButton:hover { background: {accent_hover}; }
        QToolButton { padding: 4px 8px; border: 1px solid transparent; border-radius: 6px; }
        QToolButton:hover { background: {hover}; border-color: {border}; }
        QToolButton:checked { background: {selected}; border-color: {accent}; }
        QCheckBox::indicator { width: 12px; height: 12px; border: 1px solid {muted}; border-radius: 3px; background: {base}; }
        QCheckBox::indicator:checked { background: {accent}; border-color: {accent}; }
        QCheckBox::indicator:disabled { border-color: {faint}; }
        QListWidget, QTreeWidget, QTableWidget { border: 1px solid {border}; border-radius: 6px; }
        QTabWidget::pane { border: 1px solid {border}; border-radius: 6px; }
        QTabBar::tab { padding: 6px 14px; background: {panel}; border: 1px solid {border}; color: {muted}; }
        QTabBar::tab:selected { background: {selected}; border-color: {accent}; color: {text}; }
        QTabBar::tab:hover { color: {text}; }
        QDockWidget::title { background: {panel}; padding: 4px 8px; }
        QMenuBar { background: transparent; border: none; }
        QMenuBar::item { background: transparent; padding: 5px 9px; border-radius: 5px; color: {text}; }
        QMenuBar::item:selected, QMenuBar::item:pressed { background: {hover}; }
        QMenu { background: {window}; border: 1px solid {border}; }
        QMenu::item:selected { background: {selected}; }
        QMenu::separator { height: 1px; background: {border}; margin: 4px 8px; }
        QProgressBar { border: 1px solid {border}; border-radius: 4px; background: {base}; text-align: center; }
        QProgressBar::chunk { background: {accent}; border-radius: 3px; }
        QSplitter::handle { background: {border}; }
        QStatusBar { color: {muted}; }
    )");
    const std::pair<const char *, Color> vars[] = {
        {"{text}", u.text},
        {"{muted}", u.muted},
        {"{faint}", u.faint},
        {"{base}", u.base},
        {"{window}", u.window},
        {"{panel}", u.panel},
        {"{hover}", u.hover},
        {"{border}", u.border},
        {"{selected}", u.selected},
        {"{accent_text}", u.accent_text},
        {"{accent_hover}", u.accent.Mix(u.accent_text, 0.12)},
        {"{accent}", u.accent},
        {"{danger_bg}", u.panel.Mix(u.danger, 0.18)},
        {"{danger}", u.danger},
    };
    for (const auto &[name, color] : vars)
    {
        css.replace(QLatin1String(name), Css(color));
    }
    return css;
}

} // namespace

Theme &Theme::Instance()
{
    static Theme theme;
    return theme;
}

void Theme::Apply(const DesignSystem *ds)
{
    UiPalette colors = DeriveUiPalette(ds);
    _design          = ds != nullptr ? std::optional<DesignSystem>(*ds) : std::nullopt;
    if (_applied && colors == _colors)
    {
        return;
    }
    _colors  = colors;
    _applied = true;
    QApplication::setPalette(MakePalette(_colors));
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance()); app != nullptr)
    {
        app->setStyleSheet(MakeStyleSheet(_colors)); // re-polishes every widget
    }
    Q_EMIT Changed();
    for (QWidget *w : QApplication::allWidgets())
    {
        w->update(); // custom-painted widgets read Ui() while painting
    }
}

void ApplyTheme()
{
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    Theme::Instance().Apply(nullptr);
}

} // namespace ad::ui
