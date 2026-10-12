#include <QApplication>
#include <QSurfaceFormat>
#include <QDir>
#include <QTimer>

#include <csignal>
#include <cstdio>
#include <atomic>

#include "ui/main_window.h"
#include "controller.h"

#ifndef OUTDIR
#define OUTDIR "/home/cat/recordings/"
#endif

namespace {
std::atomic<bool> g_stopRequested{false};
void onTermSignal(int) { g_stopRequested.store(true); }
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("yunlu"));

    // 确保录制目录存在
    QDir().mkpath(QStringLiteral(OUTDIR));

    // libmali(G610) 主要支持 OpenGL ES，用 ES 3.0 上下文最可靠
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGLES);
    fmt.setVersion(3, 2);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    fmt.setDepthBufferSize(0);
    QSurfaceFormat::setDefaultFormat(fmt);

    yunlu::Controller ctrl;
    yunlu::MainWindow win(&ctrl);

    // SIGINT/SIGTERM：优雅停止采集与录制/推流（发送 RTMP unpublish 并写尾），
    // 避免被 kill 时服务器端推流会话残留、录制无法停止。
    std::signal(SIGINT,  onTermSignal);
    std::signal(SIGTERM, onTermSignal);

    QTimer termTimer;
    QObject::connect(&termTimer, &QTimer::timeout, [&]{
        if (g_stopRequested.exchange(false)) {
            fprintf(stderr, "[main] 收到退出信号，正在停止采集/录制/推流...\n");
            ctrl.stopVideo();
            QApplication::quit();
        }
    });
    termTimer.start(200);

    return app.exec();
}
