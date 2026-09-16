#include "main_window.h"

#include <cstdio>

#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QResizeEvent>
#include <QCloseEvent>
#include <QApplication>
#include <QDateTime>
#include <QSettings>
#include <QFile>

#include "../display/video_widget.h"
#include "../../config/settings.h"

#ifndef OUTDIR
#define OUTDIR "/home/cat/recordings/"
#endif

namespace yunlu {

MainWindow::MainWindow(Controller* ctrl, QWidget* parent)
    : QMainWindow(parent), ctrl_(ctrl) {
    loadConfig();
    buildUi();
    ctrl_->setPreview(video_);

    statusTimer_ = new QTimer(this);
    connect(statusTimer_, &QTimer::timeout, this, &MainWindow::refreshStatus);
    statusTimer_->start(500);

    // 以约 60fps 刷新视频预览
    auto* paintTimer = new QTimer(this);
    connect(paintTimer, &QTimer::timeout, this, [this]{ video_->update(); });
    paintTimer->start(16);

    // 进入全屏
    showFullScreen();

    // 自动启动采集（分辨率/帧率取 settings.h，码率取运行时配置文件）
    // 采集支持 HDMI 热插拔自动恢复：无信号也能正常启动并显示界面，
    // 接入信号后自动开始采集（格式由驱动/信号决定，统一转 NV12）。
    ctrl_->startCapture(settings::kCaptureDevice,
                        settings::kCaptureWidth, settings::kCaptureHeight,
                        settings::kCaptureFps, bitrate_);
}

void MainWindow::loadConfig() {
    const QString path = QString::fromUtf8(settings::kConfigFile);
    QSettings cfg(path, QSettings::IniFormat);

    // 首次运行：生成默认配置文件，方便在设备上直接编辑
    if (!QFile::exists(path)) {
        cfg.setValue("stream/url", QString::fromUtf8(settings::kDefaultRtmpUrl));
        cfg.setValue("stream/bitrate", settings::kBitrate);
        cfg.sync();
        fprintf(stderr, "[config] 已生成默认配置文件: %s\n", path.toUtf8().constData());
    }

    rtmpUrl_ = cfg.value("stream/url", QString::fromUtf8(settings::kDefaultRtmpUrl)).toString();
    bitrate_ = cfg.value("stream/bitrate", settings::kBitrate).toInt();

    fprintf(stderr, "[config] 文件: %s\n", path.toUtf8().constData());
    fprintf(stderr, "[config] 推流地址: %s\n", rtmpUrl_.toUtf8().constData());
    fprintf(stderr, "[config] 码率: %d bps\n", bitrate_);
}

void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("云录 - HDMI 采集录制"));

    video_ = new VideoWidget(this);

    // 状态栏（左上角悬浮信息，实时更新）
    statusLabel_ = new QLabel(this);
    statusLabel_->setStyleSheet(
        "background: rgba(0,0,0,160); color: #0f0; padding: 6px 10px;"
        "border-radius: 4px; font-size: 14px;");
    statusLabel_->setText(QStringLiteral("等待 HDMI 信号..."));

    // 底部按钮条
    btnRec_ = new QPushButton(QStringLiteral("● 开始录制"), this);
    btnRec_->setFixedSize(220, 60);
    btnRec_->setStyleSheet(
        "QPushButton{background:#2c8f2c;color:#fff;font-size:20px;border-radius:6px;}"
        "QPushButton:pressed{background:#1a6f1a;}");
    connect(btnRec_, &QPushButton::clicked, this, &MainWindow::toggleRecording);

    btnStream_ = new QPushButton(QStringLiteral("▶ 开始推流"), this);
    btnStream_->setFixedSize(220, 60);
    btnStream_->setStyleSheet(
        "QPushButton{background:#1c6fd8;color:#fff;font-size:20px;border-radius:6px;}"
        "QPushButton:pressed{background:#145aa8;}");
    connect(btnStream_, &QPushButton::clicked, this, &MainWindow::toggleStreaming);

    // 布局：视频铺满，悬浮控件在顶
    auto* container = new QWidget(this);
    auto* root = new QVBoxLayout(container);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(video_);

    setCentralWidget(container);

    // 悬浮覆盖：用手动定位在顶层子控件
    statusLabel_->setParent(video_);
    btnRec_->setParent(video_);
    btnStream_->setParent(video_);
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
    if (!video_) return;
    statusLabel_->move(20, 20);

    int by = video_->height() - 80;
    btnRec_->move(20, by);
    btnStream_->move(20 + 220 + 15, by);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    // 不支持退出：忽略所有关闭请求（Esc/Alt+F4/窗口关闭）
    e->ignore();
}

void MainWindow::toggleRecording() {
    if (!recording_) {
        QDateTime now = QDateTime::currentDateTime();
        QString fname = QStringLiteral(OUTDIR) + now.toString("yyyyMMdd_HHmmss") + QStringLiteral(".mp4");
        if (ctrl_->startRecording(fname.toStdString())) {
            recording_ = true;
            btnRec_->setText(QStringLiteral("■ 停止录制"));
            btnRec_->setStyleSheet("QPushButton{background:#c04000;color:#fff;"
                                   "font-size:20px;border-radius:6px;}");
        }
    } else {
        ctrl_->stopRecording();
        recording_ = false;
        btnRec_->setText(QStringLiteral("● 开始录制"));
        btnRec_->setStyleSheet("QPushButton{background:#2c8f2c;color:#fff;"
                               "font-size:20px;border-radius:6px;}");
    }
}

void MainWindow::toggleStreaming() {
    if (!streaming_) {
        if (rtmpUrl_.trimmed().isEmpty()) return;
        if (ctrl_->startStreaming(rtmpUrl_.toStdString())) {
            streaming_ = true;
            btnStream_->setText(QStringLiteral("■ 停止推流"));
            btnStream_->setStyleSheet("QPushButton{background:#c04000;color:#fff;"
                                      "font-size:20px;border-radius:6px;}");
        }
    } else {
        ctrl_->stopStreaming();
        streaming_ = false;
        btnStream_->setText(QStringLiteral("▶ 开始推流"));
        btnStream_->setStyleSheet("QPushButton{background:#1c6fd8;color:#fff;"
                                  "font-size:20px;border-radius:6px;}");
    }
}

void MainWindow::refreshStatus() {
    if (!ctrl_) return;
    QString s;
    if (ctrl_->hasSignal()) {
        s += QStringLiteral(" 采集:开");
        s += QStringLiteral(" 录制:") + (ctrl_->isRecording() ? QStringLiteral("●") : QStringLiteral("○"));
        s += QStringLiteral(" 推流:") + (ctrl_->isStreaming() ? QStringLiteral("▶") : QStringLiteral("□"));
        s += QStringLiteral(" 丢帧:") + QString::number(ctrl_->droppedFrames());
        s += QStringLiteral("   ") + QString::number(ctrl_->captureWidth()) + QStringLiteral("x")
             + QString::number(ctrl_->captureHeight());
        s += QStringLiteral("  ") + QString::number(ctrl_->captureFps(), 'f', 1) + QStringLiteral("fps");
    } else {
        s += QStringLiteral(" 等待 HDMI 信号...");
    }
    statusLabel_->setText(s);
    statusLabel_->adjustSize();
}

} // namespace yunlu
