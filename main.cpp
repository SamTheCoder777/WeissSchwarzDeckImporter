// main.cpp — entry point for the TCG Deck Builder.
#include <QApplication>
#include "MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

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
    )");

    MainWindow w;
    w.show();
    return app.exec();
}