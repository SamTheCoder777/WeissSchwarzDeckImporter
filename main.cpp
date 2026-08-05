// main.cpp — entry point for the TCG Deck Builder.
#include <QApplication>
#include <QQuickStyle>
#include "src/app/MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QQuickStyle::setStyle("Fusion");

    // Dark theme for the Widgets side so it matches the QML panels.
    app.setStyleSheet(R"(
        QMainWindow, QStackedWidget, QToolBar, QStatusBar { background: #1b1d21; color: #e8eaed; }

        QToolBar#SideBar {
            background: #16181c;
            border: none;
            border-right: 1px solid #2b2f35;
            padding: 8px 4px;
            spacing: 6px;
        }
        QToolBar#SideBar QToolButton {
            background: transparent; color: #9aa0a6;
            border: none; border-radius: 8px;
            padding: 10px 6px; min-width: 76px; font-size: 11px;
        }
        QToolBar#SideBar QToolButton:hover   { background: #23262b; color: #e8eaed; }
        QToolBar#SideBar QToolButton:checked { background: #24272c; color: #4aa3ff; }

        QPushButton {
            background: #2e3238; color: #e8eaed;
            border: none; border-radius: 7px; padding: 7px 14px;
        }
        QPushButton:hover   { background: #3a3f46; }
        QPushButton:pressed { background: #4aa3ff; color: white; }
        QPushButton:checked { background: #4aa3ff; color: white; }

        QLineEdit, QSpinBox {
            background: #24272c; color: #e8eaed;
            border: 1px solid #33373d; border-radius: 6px; padding: 6px;
            selection-background-color: #4aa3ff;
        }
        QLabel, QCheckBox { color: #e8eaed; background: transparent; }
        QSplitter::handle { background: #2b2f35; width: 3px; }
        QStatusBar { color: #9aa0a6; }

        QDialog, QMessageBox { background: #1b1d21; color: #e8eaed; }
        QMessageBox QLabel   { color: #e8eaed; }
        QDialog QLabel       { color: #e8eaed; }
        QMessageBox QPushButton, QDialog QPushButton { min-width: 78px; }

        QProgressBar {
            background-color: #24272c;
            border: 1px solid #33373d;
            border-radius: 6px;
            text-align: center;
            color: #e8eaed;
            font-weight: bold;
            height: 20px;
        }
        QProgressBar::chunk {
            background-color: #4aa3ff;
            border-radius: 5px;
        }
        QGroupBox {
            border: 1px solid palette(mid);
            border-radius: 6px;
            margin-top: 10px;
            padding-top: 10px;
            font-weight: 600;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 10px;
            padding: 0 4px;
            color: #e8eaed;
            font-weight: bold;
        }

        /* --- Index card list (ported from QML) --- */
        QFrame#listCard {
            background: white;
            border: 1px solid #e4e4e7;
            border-radius: 12px;
        }

        QWidget#indexCard {
            background: #f4f4f5;
            border-radius: 12px;
        }

        QLabel#cardTitle {
            font-size: 14px;
            font-weight: 600;
        }
        QLabel#cardDesc {
            font-size: 12px;
            color: #71717a;
        }

        QLabel#tagBadgeOk, QLabel#tagBadgeWarn {
            font-size: 10px;
            font-weight: 700;
            padding: 2px 8px;
            border-radius: 4px;
            letter-spacing: 0.02em;
        }
        QLabel#tagBadgeOk   { background: #dcfce7; color: #15803d; }
        QLabel#tagBadgeWarn { background: #fef3c7; color: #92400e; }

        QLabel#emptyState {
            color: #a1a1aa;
            font-size: 13px;
            padding: 40px;
        }
        QLabel#footerLabel {
            color: #a1a1aa;
            font-size: 11px;
        }

        /* --- Button tiers --- */
        QPushButton#actionPrimary {
            background: #18181b;
            color: white;
            border: none;
            font-weight: 600;
            padding: 6px 12px;
            border-radius: 8px;
        }
        QPushButton#actionPrimary:hover { background: #27272a; }
        QPushButton#actionPrimary:disabled { background: #d4d4d8; color: #71717a; }

        QPushButton#actionAccent {
            background: #4f46e5;
            color: white;
            border: none;
            font-weight: 600;
            padding: 6px 12px;
            border-radius: 8px;
        }
        QPushButton#actionAccent:hover { background: #4338ca; }

        QPushButton#actionWarn {
            background: #f59e0b;
            color: white;
            border: none;
            font-weight: 600;
            padding: 6px 12px;
            border-radius: 8px;
        }
        QPushButton#actionWarn:hover { background: #d97706; }

        QPushButton#actionGhost {
            background: transparent;
            border: 1px solid #d4d4d8;
            color: #52525b;
            padding: 6px 12px;
            border-radius: 8px;
        }
        QPushButton#actionGhost:hover { background: #f4f4f5; }

        QProgressBar {
            border: none;
            border-radius: 3px;
            background: #e4e4e7;
        }
        QProgressBar::chunk {
            background: #18181b;
            border-radius: 3px;
        }
    )");

    MainWindow w;
    w.show();
    return app.exec();
}