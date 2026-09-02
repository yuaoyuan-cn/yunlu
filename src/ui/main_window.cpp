#include "main_window.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QApplication>
#include <QStyle>
#include <QDateTime>

#include "../display/video_widget.h"
#include "../../config/settings.h"

#ifndef OUTDIR
#define OUTDIR "/home/cat/recordings/"
#endif

namespace yunlu {

MainWindow::MainWindow(Controller* ctrl, QWidget* parent)
    : QMainWindow(parent), ctrl_(ctrl) {
    buildUi();
    ctrl_->setPreview(video_);

    statusTimer_ = new QTimer(this);
    connect(statusTimer_, &QTimer::timeout, this, &MainWindow::refreshStatus);
    statusTimer_->start(500);

    // 以约 30fps 刷新视频预览
    auto* paintTimer = new QTimer(this);
    connect(paintTimer, &QTimer::timeout, this, [this]{ video_->update(); });
    paintTimer->start(33);

    // 进入全屏
    showFullScreen();

    // 自动启动采集（使用 settings.h 中的配置）
    // 采集支持 HDMI 热插拔自动恢复：无信号也能正常启动并显示界面，
    // 接入信号后自动开始采集（格式由驱动/信号决定，统一转 NV12）。
    ctrl_->startCapture(settings::kCaptureDevice,
                        settings::kCaptureWidth, settings::kCaptureHeight,
                        settings::kCaptureFps);
}

void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("云录 - HDMI 采集录制"));

    video_ = new VideoWidget(this);

    // 状态栏（顶部悬浮信息）
    stateLabel_ = new QLabel(this);
    stateLabel_->setStyleSheet(
        "background: rgba(0,0,0,160); color: #fff; padding: 6px 10px;"
        "border-radius: 4px; font-size: 16px;");
    stateLabel_->setText(QStringLiteral("等待 HDMI 信号..."));

    statusLabel_ = new QLabel(this);
    statusLabel_->setStyleSheet(
        "background: rgba(0,0,0,160); color: #0f0; padding: 6px 10px;"
        "border-radius: 4px; font-size: 14px;");

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

    urlEdit_ = new QLineEdit(this);
    urlEdit_->setPlaceholderText(QStringLiteral("RTMP 地址 rtmp://..."));
    urlEdit_->setFixedWidth(360);
    urlEdit_->setStyleSheet("QLineEdit{background:rgba(0,0,0,150);color:#fff;"
                            "border:1px solid #555;padding:6px;font-size:15px;}");
    urlEdit_->setText(QString::fromUtf8(settings::kDefaultRtmpUrl));
    btnOsc_ = new QPushButton(QStringLiteral("动作"), this);
    btnOsc_->setFixedSize(120, 60);
    btnOsc_->setStyleSheet(
        "QPushButton{background:#555;color:#fff;font-size:18px;border-radius:6px;}"
        "QPushButton:pressed{background:#333;}");
    connect(btnOsc_, &QPushButton::clicked, this, &MainWindow::toggleOsc);

    btnQuit_ = new QPushButton(QStringLiteral("退出"), this);
    btnQuit_->setFixedSize(120, 60);
    btnQuit_->setStyleSheet(
        "QPushButton{background:#b03030;color:#fff;font-size:18px;border-radius:6px;}"
        "QPushButton:pressed{background:#901010;}");
    connect(btnQuit_, &QPushButton::clicked, qApp, &QApplication::quit);

    // 布局：视频铺满，悬浮控件在顶
    auto* container = new QWidget(this);
    auto* root = new QVBoxLayout(container);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(video_);

    setCentralWidget(container);

    // 悬浮覆盖：用手动定位在顶层子控件
    stateLabel_->setParent(video_);
    statusLabel_->setParent(video_);
    btnRec_->setParent(video_);
    btnStream_->setParent(video_);
    urlEdit_->setParent(video_);
    btnOsc_->setParent(video_);
    btnQuit_->setParent(video_);
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
    if (!video_) return;
    int w = video_->width(), h = video_->height();
    stateLabel_->move(20, 20);
    statusLabel_->move(20, 70);

    int by = h - 80;
    btnRec_->move(20, by);
    btnStream_->move(20 + 220 + 15, by);
    urlEdit_->move(20 + 220 + 15 + 220 + 20, by + 6);
    btnOsc_->move(w - 140 - 20 - 135, by);
    btnQuit_->move(w - 140, by);
}

void MainWindow::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape)
        qApp->quit();
    else
        QMainWindow::keyPressEvent(e);
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
        QString url = urlEdit_->text().trimmed();
        if (url.isEmpty()) return;
        if (ctrl_->startStreaming(url.toStdString())) {
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

void MainWindow::toggleOsc() {
    // 占位：后续可接入动作控制
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
    } else {
        s += QStringLiteral(" 等待 HDMI 信号...");
    }
    statusLabel_->setText(s);
    statusLabel_->adjustSize();
}

} // namespace yunlu
