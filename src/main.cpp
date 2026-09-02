#include <QApplication>
#include <QSurfaceFormat>
#include <QDir>

#include "ui/main_window.h"
#include "controller.h"

#ifndef OUTDIR
#define OUTDIR "/home/cat/recordings/"
#endif

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

    return app.exec();
}
