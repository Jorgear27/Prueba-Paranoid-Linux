/**
 * @file sim/ui_sim.cpp
 * @brief Qt5 desktop simulation of the courier UI.
 *
 * Implements the same ui.h API as ui.c (LVGL) but using Qt widgets.
 *
 * Build:
 *   cd courier/sim
 *   cmake -B build -DCMAKE_BUILD_TYPE=Debug
 *   cmake --build build
 *   JWT_SECRET=... EMPLOYEE_ID=E001 ./build/courier_sim
 */

#include "ui_sim.h"

extern "C"
{
#include "courier_state.h"
}

#include <QApplication>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMetaObject>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <cstdio>

// ============================================================================
// Singleton main window — owns all widgets.
// ============================================================================

class CourierWindow : public QMainWindow
{
    Q_OBJECT

  public:
    static CourierWindow& instance()
    {
        static CourierWindow w;
        return w;
    }

    // -- Labels --
    QLabel* labelCurrent;
    QLabel* labelNext;
    QLabel* labelGpsWarn;
    QPushButton* btnSOS;
    QPushButton* btnDelivered;

  signals:
    void routeUpdated(); // emitted from any thread via QMetaObject::invokeMethod

  private:
    CourierWindow()
    {
        setWindowTitle("DHL Courier Rugged System — Simulation");
        setFixedSize(480, 320);

        QWidget* central = new QWidget(this);
        setCentralWidget(central);
        central->setStyleSheet("background-color: #1a1a2e;");

        QVBoxLayout* vbox = new QVBoxLayout(central);
        vbox->setContentsMargins(16, 16, 16, 16);
        vbox->setSpacing(12);

        // Title
        QLabel* title = new QLabel("DHL Delivery");
        QFont titleFont;
        titleFont.setPointSize(16);
        titleFont.setBold(true);
        title->setFont(titleFont);
        title->setStyleSheet("color: #e94560;");
        title->setAlignment(Qt::AlignCenter);
        vbox->addWidget(title);

        // Separator
        QFrame* sep = new QFrame;
        sep->setFrameShape(QFrame::HLine);
        sep->setStyleSheet("color: #444;");
        vbox->addWidget(sep);

        // Current stop
        labelCurrent = new QLabel("NOW:  ---");
        QFont stopFont;
        stopFont.setPointSize(14);
        stopFont.setBold(true);
        labelCurrent->setFont(stopFont);
        labelCurrent->setStyleSheet("color: #ffffff;");
        vbox->addWidget(labelCurrent);

        // Next stop
        labelNext = new QLabel("NEXT: ---");
        labelNext->setStyleSheet("color: #888888;");
        vbox->addWidget(labelNext);

        // GPS warning (hidden by default)
        labelGpsWarn = new QLabel("⚠ No GPS fix");
        labelGpsWarn->setStyleSheet("color: #FF8800;");
        labelGpsWarn->setVisible(false);
        vbox->addWidget(labelGpsWarn);

        vbox->addStretch();

        // Button row
        QHBoxLayout* hbox = new QHBoxLayout;
        hbox->setSpacing(16);

        btnSOS = new QPushButton("SOS");
        btnSOS->setFixedHeight(70);
        btnSOS->setStyleSheet("QPushButton { background:#CC0000; color:white; font-size:18px;"
                              "              font-weight:bold; border-radius:6px; }"
                              "QPushButton:pressed { background:#FF3333; }");

        btnDelivered = new QPushButton("DELIVERED");
        btnDelivered->setFixedHeight(70);
        btnDelivered->setStyleSheet("QPushButton { background:#007700; color:white; font-size:16px;"
                                    "              font-weight:bold; border-radius:6px; }"
                                    "QPushButton:pressed { background:#00BB00; }");

        hbox->addWidget(btnSOS);
        hbox->addWidget(btnDelivered);
        vbox->addLayout(hbox);

        // Wire routeUpdated signal to refresh labels on main thread
        connect(this, &CourierWindow::routeUpdated, this, &CourierWindow::refreshLabels, Qt::QueuedConnection);
    }

  private slots:
    void refreshLabels()
    {
        char current[MAX_STOP_NAME] = "---";
        char next[MAX_STOP_NAME] = "---";

        courier_state_get_current_stop(current, sizeof(current));
        courier_state_get_next_stop(next, sizeof(next));

        labelCurrent->setText(QString("NOW:  %1").arg(current));
        labelNext->setText(QString("NEXT: %1").arg(next));
    }
};

// ============================================================================
// C API — matches ui.h exactly so the same business logic links against either
// ui.c (LVGL/Zephyr) or this file (Qt/desktop).
// ============================================================================

extern "C"
{

    void ui_init(void)
    {
        // Qt widgets are created in CourierWindow constructor.
        // Called from main_sim.cpp after QApplication is constructed.
        CourierWindow::instance().show();
    }

    void ui_set_current_stop(const char* name)
    {
        QString text = QString("NOW:  %1").arg(name);
        QMetaObject::invokeMethod(
            &CourierWindow::instance(), [text]() { CourierWindow::instance().labelCurrent->setText(text); },
            Qt::QueuedConnection);
    }

    void ui_set_next_stop(const char* name)
    {
        QString text = QString("NEXT: %1").arg(name);
        QMetaObject::invokeMethod(
            &CourierWindow::instance(), [text]() { CourierWindow::instance().labelNext->setText(text); },
            Qt::QueuedConnection);
    }

    void ui_set_gps_warning(bool visible)
    {
        QMetaObject::invokeMethod(
            &CourierWindow::instance(), [visible]() { CourierWindow::instance().labelGpsWarn->setVisible(visible); },
            Qt::QueuedConnection);
    }

    void ui_sos_feedback(void)
    {
        QMetaObject::invokeMethod(
            &CourierWindow::instance(),
            []() {
                CourierWindow& w = CourierWindow::instance();
                w.btnSOS->setStyleSheet("QPushButton { background:#FFFFFF; color:black; font-size:18px;"
                                        "              font-weight:bold; border-radius:6px; }");
                QTimer::singleShot(200, &w, [&w]() {
                    w.btnSOS->setStyleSheet("QPushButton { background:#CC0000; color:white; font-size:18px;"
                                            "              font-weight:bold; border-radius:6px; }"
                                            "QPushButton:pressed { background:#FF3333; }");
                });
            },
            Qt::QueuedConnection);
    }

    void ui_notify_route_updated(void)
    {
        // Signals the main thread to refresh labels.
        QMetaObject::invokeMethod(&CourierWindow::instance(), "routeUpdated", Qt::QueuedConnection);
    }

    // ui_task() is not used in the Qt simulation — the Qt event loop takes
    // its place. Provided as a no-op so the linker is satisfied.
    void ui_task(void* p1, void* p2, void* p3)
    {
        (void)p1;
        (void)p2;
        (void)p3;
    }

    // Expose button widgets so main_sim.cpp can wire signals.
    QPushButton* ui_sim_get_sos_button(void)
    {
        return CourierWindow::instance().btnSOS;
    }

    QPushButton* ui_sim_get_delivered_button(void)
    {
        return CourierWindow::instance().btnDelivered;
    }

} // extern "C"

#include "ui_sim.moc"
