#include "Theme.hpp"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace ad::ui
{

void ApplyTheme(QApplication &app)
{
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    const QColor bg(0x0b, 0x12, 0x20);
    const QColor bg2(0x0f, 0x1a, 0x2e);
    const QColor panel(0x13, 0x1f, 0x38);
    const QColor panel2(0x18, 0x26, 0x42);
    const QColor text(0xdb, 0xe6, 0xfb);
    const QColor muted(0x85, 0x98, 0xb8);
    const QColor accent(0x4f, 0x8c, 0xff);

    QPalette p;
    p.setColor(QPalette::Window, bg2);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, bg);
    p.setColor(QPalette::AlternateBase, panel);
    p.setColor(QPalette::ToolTipBase, panel2);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, muted);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, panel);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::Mid, QColor(0x22, 0x31, 0x4f));
    p.setColor(QPalette::Disabled, QPalette::Text, muted.darker(130));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, muted.darker(130));
    p.setColor(QPalette::Disabled, QPalette::WindowText, muted.darker(130));
    QApplication::setPalette(p);

    app.setStyleSheet(QStringLiteral(R"(
        QToolTip { border: 1px solid #22314f; padding: 4px; }
        QLabel#inspectorTitle { font-size: 15px; font-weight: 600; padding-bottom: 4px; }
        QLabel#inspectorSection, QLabel#panelTitle {
            color: #8598b8; font-size: 11px; font-weight: 600; padding-top: 6px;
        }
        QLabel#fieldLabel { color: #8598b8; font-size: 12px; }
        QLabel#hint { color: #8598b8; font-size: 12px; }
        QLabel#readout { font-family: monospace; padding: 2px 8px; background: #0b1220;
                         border: 1px solid #22314f; border-radius: 6px; }
        QPushButton { padding: 5px 10px; border: 1px solid #22314f; border-radius: 6px; background: #131f38; }
        QPushButton:hover { background: #182642; }
        QPushButton:checked { background: #1d3a73; border-color: #4f8cff; }
        QPushButton:disabled { color: #56688a; }
        QPushButton#dangerButton { border-color: #7f1d1d; color: #fca5a5; }
        QPushButton#dangerButton:hover { background: #3b1219; }
        QPushButton#primaryButton { background: #2b5fd9; border-color: #4f8cff; color: white; }
        QPushButton#primaryButton:hover { background: #3a6ee6; }
        QToolButton { padding: 4px 8px; border: 1px solid transparent; border-radius: 6px; }
        QToolButton:hover { background: #182642; border-color: #22314f; }
        QToolButton:checked { background: #1d3a73; border-color: #4f8cff; }
        QListWidget, QTreeWidget, QTableWidget { border: 1px solid #22314f; border-radius: 6px; }
        QTabWidget::pane { border: 1px solid #22314f; border-radius: 6px; }
        QTabBar::tab { padding: 6px 14px; background: #131f38; border: 1px solid #22314f; }
        QTabBar::tab:selected { background: #1d3a73; border-color: #4f8cff; }
        QSplitter::handle { background: #22314f; }
        QStatusBar { color: #8598b8; }
    )"));
}

} // namespace ad::ui
