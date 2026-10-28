// Language: C++20; file: qt_main.cpp; target: Windows 10/11; runtime: Qt 6 Quick/QML.
#include "app_controller.hpp"

#include <windows.h>
#include <dwmapi.h>

#include <QGuiApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QWindow>

namespace {
QString log_path;
QMutex log_mutex;

void write_qt_log(QtMsgType type, const QMessageLogContext&, const QString& message) {
    const auto type_name = type == QtDebugMsg ? QStringLiteral("DEBUG")
        : type == QtInfoMsg ? QStringLiteral("INFO")
        : type == QtWarningMsg ? QStringLiteral("WARNING")
        : type == QtCriticalMsg ? QStringLiteral("CRITICAL")
        : QStringLiteral("FATAL");
    const auto line = QStringLiteral("%1 [%2] %3\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), type_name, message);
    OutputDebugStringW(reinterpret_cast<const wchar_t*>(line.utf16()));
    QMutexLocker lock(&log_mutex);
    QFile file(log_path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream(&file) << line;
    }
    if (type == QtFatalMsg) std::abort();
}
}

int main(int argc, char* argv[]) {
    QCoreApplication::setOrganizationName(QStringLiteral("OpenDriver"));
    QCoreApplication::setApplicationName(QStringLiteral("Open Driver"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication application(argc, argv);
    const auto data_path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(data_path);
    log_path = data_path + QStringLiteral("/open-driver-ui.log");
    QFile::remove(log_path);
    qInstallMessageHandler(write_qt_log);

    AppController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("appController"), &controller);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &application, [] { QCoreApplication::exit(EXIT_FAILURE); },
                     Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("OpenDriver"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) return EXIT_FAILURE;

    if (auto* window = qobject_cast<QWindow*>(engine.rootObjects().front())) {
        const BOOL dark = TRUE;
        const auto handle = reinterpret_cast<HWND>(window->winId());
        DwmSetWindowAttribute(handle, static_cast<DWMWINDOWATTRIBUTE>(20), &dark, sizeof(dark));
        const COLORREF caption = RGB(18, 19, 22);
        DwmSetWindowAttribute(handle, static_cast<DWMWINDOWATTRIBUTE>(35),
                              &caption, sizeof(caption));

        int requested_page = -1;
        for (const auto& argument : application.arguments()) {
            if (argument.startsWith(QStringLiteral("--page="))) {
                bool valid = false;
                const auto page = argument.sliced(7).toInt(&valid);
                if (valid && page >= 0 && page <= 4) requested_page = page;
            }
        }
        const auto screenshot_index = application.arguments().indexOf(QStringLiteral("--render-test"));
        if (screenshot_index >= 0 && screenshot_index + 1 < application.arguments().size()) {
            const auto output = application.arguments().at(screenshot_index + 1);
            QTimer::singleShot(150, window, [window, output, requested_page] {
                if (requested_page >= 0) window->setProperty("currentPage", requested_page);
                QTimer::singleShot(750, window, [window, output] {
                    auto* quick_window = qobject_cast<QQuickWindow*>(window);
                    const auto saved = quick_window && quick_window->grabWindow().save(output);
                    QCoreApplication::exit(saved ? EXIT_SUCCESS : EXIT_FAILURE);
                });
            });
        } else if (requested_page >= 0) {
            window->setProperty("currentPage", requested_page);
        }
    }
    if (application.arguments().contains(QStringLiteral("--cancel-smoke-test"))) {
        QObject::connect(&controller, &AppController::stateChanged, &application, [&controller] {
            if (controller.state() == QStringLiteral("cancelled")) {
                QCoreApplication::exit(EXIT_SUCCESS);
            } else if (controller.state() == QStringLiteral("error")) {
                QCoreApplication::exit(2);
            }
        });
        QTimer::singleShot(100, &controller, &AppController::startScan);
        QTimer::singleShot(500, &controller, &AppController::cancelOperation);
        QTimer::singleShot(15000, &application, [] { QCoreApplication::exit(3); });
    }
    return application.exec();
}
