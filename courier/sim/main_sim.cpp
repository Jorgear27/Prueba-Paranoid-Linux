/**
 * @file sim/main_sim.cpp
 * @brief Qt desktop simulation entry point.
 *
 * Starts the Qt application, wires the SOS and Delivered buttons to the same C
 * logic (mqtt_handler.c, courier_state.c, gps_task.c) that runs on the ESP32.
 *
 */

#include "ui_sim.h"

/* Pull in the C business logic */
extern "C"
{
#include "courier_state.h"
#include "gps_task.h"
#include "mqtt_handler.h"
}

#include <QApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/* ---------------------------------------------------------------------------
 * MqttThread — runs mqtt_handler_task() inside a Qt thread so the Qt event
 * loop is never blocked by the blocking mqtt_input() calls.
 * ------------------------------------------------------------------------- */
class MqttThread : public QThread
{
  public:
    void run() override
    {
        /* mqtt_handler_task is an infinite loop — runs for the app lifetime. */
        mqtt_handler_task(nullptr, nullptr, nullptr);
    }
};

/* ---------------------------------------------------------------------------
 * SimGpsThread — provides scripted GPS coordinates every 5 seconds.
 * Replaces the hardware gps_hal_read() weak symbol.
 * ------------------------------------------------------------------------- */

/* Override the weak gps_hal_read stub with simulated coordinates. */
extern "C" bool gps_hal_read(int32_t* lat_e6, int32_t* lon_e6)
{
    /* Córdoba, Argentina — moves slightly each call to simulate movement. */
    static int32_t lat = -31413030;
    static int32_t lon = -64183693;
    lat += 500; /* ~55 m north per tick */
    lon += 200;
    *lat_e6 = lat;
    *lon_e6 = lon;
    return true; /* always valid fix in simulation */
}

/* ---------------------------------------------------------------------------
 * Zephyr stubs — the simulation links against courier_state.c and
 * mqtt_handler.c which call a handful of Zephyr APIs. We stub them here
 * so the Qt build does not need the Zephyr SDK.
 *
 * These would later be replaced with pthreads/POSIX. For the moment
 * they are no-ops, sufficient for desktop demonstration.
 * ------------------------------------------------------------------------- */
extern "C"
{

/* k_mutex — map to a global Qt mutex table (simplification) */
#include <pthread.h>
    int k_mutex_init(struct k_mutex* m)
    {
        return pthread_mutex_init((pthread_mutex_t*)m, nullptr);
    }
    int k_mutex_lock(struct k_mutex* m, int)
    {
        return pthread_mutex_lock((pthread_mutex_t*)m);
    }
    int k_mutex_unlock(struct k_mutex* m)
    {
        return pthread_mutex_unlock((pthread_mutex_t*)m);
    }

/* k_sem — map to POSIX semaphore */
#include <semaphore.h>
    int k_sem_init(struct k_sem* s, int init, int max)
    {
        sem_init((sem_t*)s, 0, init);
        return 0;
    }
    int k_sem_take(struct k_sem* s, int timeout)
    {
        return sem_trywait((sem_t*)s);
    }
    int k_sem_give(struct k_sem* s)
    {
        sem_post((sem_t*)s);
        return 0;
    }

/* k_uptime — wall clock ms */
#include <time.h>
    int64_t k_uptime_get(void)
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    }
    uint32_t k_uptime_get_32(void)
    {
        return (uint32_t)k_uptime_get();
    }

    /* k_sleep — POSIX sleep */
    void k_sleep(int ms)
    {
        struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
        nanosleep(&ts, nullptr);
    }

    /* Logging stubs — print to stderr */
    void zephyr_log(const char* fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
        fputc('\n', stderr);
    }

} /* extern "C" */

/* ---------------------------------------------------------------------------
 * main()
 * ------------------------------------------------------------------------- */
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    /* Resolve runtime configuration from environment, falling back to
     * compile-time CMake definitions. */
    const char* employeeId = getenv("COURIER_EMPLOYEE_ID");
    const char* brokerAddr = getenv("COURIER_BROKER_ADDR");
    const char* brokerPortStr = getenv("COURIER_BROKER_PORT");

    if (!employeeId)
        employeeId = EMPLOYEE_ID;
    if (!brokerAddr)
        brokerAddr = MQTT_BROKER_ADDR;
    uint16_t brokerPort = MQTT_BROKER_PORT;
    if (brokerPortStr)
        brokerPort = (uint16_t)atoi(brokerPortStr);

    fprintf(stderr, "[sim] Employee: %s  Broker: %s:%d\n", employeeId, brokerAddr, (int)brokerPort);

    /* 1. Shared state */
    courier_state_init(employeeId);

    /* 2. Show Qt window */
    ui_init();

    QPushButton* btnSOS = ui_sim_get_sos_button();
    QPushButton* btnDelivered = ui_sim_get_delivered_button();

    /* 3. Connect to MQTT broker */
    mqtt_result_t rc = mqtt_handler_init(brokerAddr, brokerPort, employeeId);
    if (rc != MQTT_OK)
    {
        QMessageBox::warning(nullptr, "MQTT",
                             QString("Could not connect to broker at %1:%2.\n"
                                     "Route publishing will not work.")
                                 .arg(brokerAddr)
                                 .arg(brokerPort));
    }
    else
    {
        mqtt_handler_subscribe_routes();
    }

    /* 4. Start MQTT receive loop in a background thread */
    MqttThread* mqttThread = new MqttThread;
    mqttThread->start();

    /* 5. Wire SOS button → immediate publish */
    QObject::connect(btnSOS, &QPushButton::clicked, [&]() {
        gps_fix_t fix;
        courier_state_get_gps(&fix);
        mqtt_handler_publish_sos(fix.lat_e6, fix.lon_e6);
        ui_sos_feedback();
    });

    /* 6. Wire Delivered button → advance stop + publish */
    QObject::connect(btnDelivered, &QPushButton::clicked, [&]() {
        char stopName[MAX_STOP_NAME];
        courier_state_get_current_stop(stopName, sizeof(stopName));
        mqtt_handler_publish_delivered(stopName);

        bool more = courier_state_advance_stop();
        ui_notify_route_updated();

        char current[MAX_STOP_NAME], next[MAX_STOP_NAME];
        courier_state_get_current_stop(current, sizeof(current));
        courier_state_get_next_stop(next, sizeof(next));
        ui_set_current_stop(current);
        ui_set_next_stop(next);

        if (!more)
        {
            QMessageBox::information(nullptr, "Route complete", "All stops have been delivered!");
        }
    });

    /* 7. GPS simulation — QTimer every 5 s on main thread
     *    (on hardware this is k_work_delayable on the system workqueue) */
    QTimer* gpsTimer = new QTimer;
    gpsTimer->setInterval(5000);
    QObject::connect(gpsTimer, &QTimer::timeout, [&]() {
        int32_t lat_e6, lon_e6;
        bool valid = gps_hal_read(&lat_e6, &lon_e6);
        courier_state_update_gps(lat_e6, lon_e6, valid);
        mqtt_handler_publish_tracking(lat_e6, lon_e6, valid);
        ui_set_gps_warning(!valid);
    });
    gpsTimer->start();

    return app.exec();
}
