#include "main_window.h"

#include <cstdio>
#include <vector>

#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QResizeEvent>
#include <QApplication>
#include <QDateTime>
#include <QSettings>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>
#include <QKeyEvent>
#include <QList>
#include <QVector>

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
    ctrl_->startCapture(settings::kCaptureDevice,
                        settings::kCaptureWidth, settings::kCaptureHeight,
                        settings::kCaptureFps, bitrate_);
}

void MainWindow::loadConfig() {
    const QString path = QString::fromUtf8(settings::kConfigFile);
    QSettings cfg(path, QSettings::IniFormat);

    if (!QFile::exists(path)) {
        cfg.setValue("stream/url", QString::fromUtf8(settings::kDefaultRtmpUrl));
        cfg.setValue("stream/bitrate", settings::kBitrate);
        cfg.setValue("record/format", QString::fromUtf8(settings::kContainer));
        cfg.setValue("record/max_file_mb", settings::kMaxFileMB);
        cfg.setValue("record/min_free_mb", settings::kMinFreeMB);
        cfg.sync();
        fprintf(stderr, "[config] 已生成默认配置文件: %s\n", path.toUtf8().constData());
    }

    rtmpUrl_ = cfg.value("stream/url", QString::fromUtf8(settings::kDefaultRtmpUrl)).toString();
    bitrate_ = cfg.value("stream/bitrate", settings::kBitrate).toInt();

    // 录制容器后缀：mkv(默认，抗中断) / mp4
    QString ext = cfg.value("record/format", QString::fromUtf8(settings::kContainer)).toString();
    ext = ext.trimmed().toLower();
    ext.replace(QStringLiteral("."), QString());
    if (ext != QStringLiteral("mkv") && ext != QStringLiteral("mp4")) ext = QStringLiteral("mkv");
    recordExt_ = QStringLiteral(".") + ext;

    int maxMb = cfg.value("record/max_file_mb", settings::kMaxFileMB).toInt();
    if (maxMb <= 0) maxMb = settings::kMaxFileMB;
    maxRecordBytes_ = static_cast<int64_t>(maxMb) * 1024 * 1024;

    int minMb = cfg.value("record/min_free_mb", settings::kMinFreeMB).toInt();
    if (minMb < 0) minMb = settings::kMinFreeMB;
    minFreeBytes_ = static_cast<int64_t>(minMb) * 1024 * 1024;

    fprintf(stderr, "[config] 文件: %s\n", path.toUtf8().constData());
    fprintf(stderr, "[config] 推流地址: %s\n", rtmpUrl_.toUtf8().constData());
    fprintf(stderr, "[config] 码率: %d bps\n", bitrate_);
    fprintf(stderr, "[config] 容器: %s, 分段: %d MB, 磁盘保护: %d MB\n",
            recordExt_.toUtf8().constData(), maxMb, minMb);

    // 启动自检：历史上 MP4 录制被中断会残留 .part 文件（MKV 无此问题）
    QDir recd(QString::fromUtf8(OUTDIR));
    QStringList parts = recd.entryList(QStringList() << QStringLiteral("*.mp4.part"),
                                       QDir::Files, QDir::Name);
    if (!parts.isEmpty())
        fprintf(stderr, "[config] 发现 %d 个旧的未完成 .part 文件，可用 python3 ~/mp4fix.py 抢救\n",
                parts.size());
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

    // 拷贝进度条（默认隐藏）
    copyBar_ = new QProgressBar(this);
    copyBar_->setFixedSize(360, 18);
    copyBar_->setRange(0, 100);
    copyBar_->setValue(0);
    copyBar_->setTextVisible(true);
    copyBar_->setStyleSheet(
        "QProgressBar{background:rgba(0,0,0,150);border:1px solid #555;border-radius:4px;"
        "color:#fff;text-align:center;font-size:12px;}"
        "QProgressBar::chunk{background:#1c6fd8;border-radius:3px;}");
    copyBar_->hide();

    // 底部按钮条
    btnRec_ = new QPushButton(QStringLiteral("● 开始录制"), this);
    btnRec_->setFixedSize(200, 60);
    btnRec_->setStyleSheet(
        "QPushButton{background:#2c8f2c;color:#fff;font-size:20px;border-radius:6px;}"
        "QPushButton:pressed{background:#1a6f1a;}");
    connect(btnRec_, &QPushButton::clicked, this, &MainWindow::toggleRecording);

    btnUsbRec_ = new QPushButton(QStringLiteral("⏏ U盘录制"), this);
    btnUsbRec_->setFixedSize(200, 60);
    btnUsbRec_->setStyleSheet(
        "QPushButton{background:#2b6b6b;color:#fff;font-size:20px;border-radius:6px;}"
        "QPushButton:pressed{background:#1d4a4a;}");
    connect(btnUsbRec_, &QPushButton::clicked, this, &MainWindow::toggleUsbRecording);

    btnStream_ = new QPushButton(QStringLiteral("▶ 开始推流"), this);
    btnStream_->setFixedSize(200, 60);
    btnStream_->setStyleSheet(
        "QPushButton{background:#1c6fd8;color:#fff;font-size:20px;border-radius:6px;}"
        "QPushButton:pressed{background:#145aa8;}");
    connect(btnStream_, &QPushButton::clicked, this, &MainWindow::toggleStreaming);

    btnUsb_ = new QPushButton(QStringLiteral("⏏ 拷贝到U盘"), this);
    btnUsb_->setFixedSize(200, 60);
    btnUsb_->setStyleSheet(
        "QPushButton{background:#8a6d1f;color:#fff;font-size:20px;border-radius:6px;}"
        "QPushButton:pressed{background:#6b5315;}");
    connect(btnUsb_, &QPushButton::clicked, this, &MainWindow::copyToUsb);

    btnQuit_ = new QPushButton(QStringLiteral("退出"), this);
    btnQuit_->setFixedSize(120, 60);
    btnQuit_->setStyleSheet(
        "QPushButton{background:#b03030;color:#fff;font-size:18px;border-radius:6px;}"
        "QPushButton:pressed{background:#901010;}");
    connect(btnQuit_, &QPushButton::clicked, this, [this]{
        if (ctrl_) ctrl_->stopVideo();
        qApp->quit();
    });

    // 布局：视频铺满，悬浮控件在顶
    auto* container = new QWidget(this);
    auto* root = new QVBoxLayout(container);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(video_);

    setCentralWidget(container);

    // 悬浮覆盖：用手动定位在顶层子控件
    statusLabel_->setParent(video_);
    copyBar_->setParent(video_);
    btnRec_->setParent(video_);
    btnUsbRec_->setParent(video_);
    btnStream_->setParent(video_);
    btnUsb_->setParent(video_);
    btnQuit_->setParent(video_);
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
    if (!video_) return;
    statusLabel_->move(20, 20);
    copyBar_->move(20, 76);

    int by = video_->height() - 80;
    btnRec_->move(20, by);
    btnUsbRec_->move(20 + 200 + 12, by);
    btnStream_->move(20 + 200 + 12 + 200 + 12, by);
    btnUsb_->move(20 + 200 + 12 + 200 + 12 + 200 + 12, by);
    btnQuit_->move(video_->width() - 140, by);
}

void MainWindow::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        if (ctrl_) ctrl_->stopVideo();
        qApp->quit();
    } else {
        QMainWindow::keyPressEvent(e);
    }
}

void MainWindow::setRecordStyle(bool recording) {
    // 两个录制按钮共享一个状态
    const QList<QPushButton*> btns { btnRec_, btnUsbRec_ };
    for (QPushButton* b : btns) {
        if (!b) continue;
        if (recording) {
            b->setText(QStringLiteral("■ 停止录制"));
            b->setStyleSheet("QPushButton{background:#c04000;color:#fff;"
                             "font-size:20px;border-radius:6px;}");
        } else {
            const bool isUsb = (b == btnUsbRec_);
            b->setText(isUsb ? QStringLiteral("⏏ U盘录制") : QStringLiteral("● 开始录制"));
            b->setStyleSheet(isUsb
                ? "QPushButton{background:#2b6b6b;color:#fff;font-size:20px;border-radius:6px;}"
                : "QPushButton{background:#2c8f2c;color:#fff;font-size:20px;border-radius:6px;}");
        }
    }
}

QString MainWindow::findUsbMount() {
    for (const QStorageInfo& s : QStorageInfo::mountedVolumes()) {
        if (!s.isValid() || !s.isReady() || s.isRoot()) continue;
        const QString root = s.rootPath();
        if (root.startsWith(QStringLiteral("/media/")) ||
            root.startsWith(QStringLiteral("/run/media/")) ||
            root.startsWith(QStringLiteral("/mnt/"))) {
            return root;
        }
    }
    return QString();
}

QString MainWindow::usbRecordDir(QString* err) {
    const QString usb = findUsbMount();
    if (usb.isEmpty()) {
        if (err) *err = QStringLiteral("未检测到U盘");
        return QString();
    }
    const QString dir = usb + QStringLiteral("/yunlu");
    if (!QDir().mkpath(dir)) {
        if (err) *err = QStringLiteral("U盘目录创建失败");
        return QString();
    }
    QStorageInfo si(dir);
    if (si.isValid() && si.isReady() && si.bytesAvailable() < minFreeBytes_) {
        if (err) *err = QStringLiteral("U盘空间不足");
        return QString();
    }
    return dir;
}

QString MainWindow::currentRecordDir() const {
    if (recording_) return recordDir_;
    return QString::fromUtf8(OUTDIR);
}

void MainWindow::toggleRecording() {
    if (recording_) { stopRecordingUi(); return; }
    startRecordingUi(false);
}

void MainWindow::toggleUsbRecording() {
    if (recording_) { stopRecordingUi(); return; }
    startRecordingUi(true);
}

bool MainWindow::startRecordingUi(bool toUsb) {
    QString dir = QString::fromUtf8(OUTDIR);
    if (toUsb) {
        QString err;
        dir = usbRecordDir(&err);
        if (dir.isEmpty()) {
            statusLabel_->setText(QStringLiteral(" ") + err);
            statusLabel_->adjustSize();
            return false;
        }
    }
    QStorageInfo si(dir);
    if (si.isValid() && si.isReady() && si.bytesAvailable() < minFreeBytes_) {
        statusLabel_->setText(QStringLiteral(" 存储空间不足，无法开始录制"));
        statusLabel_->adjustSize();
        return false;
    }

    QString fname = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + recordExt_;
    QString full  = dir + QStringLiteral("/") + fname;
    if (!ctrl_->startRecording(full.toStdString())) {
        statusLabel_->setText(QStringLiteral(" 启动录制失败"));
        statusLabel_->adjustSize();
        return false;
    }
    recording_    = true;
    recordToUsb_  = toUsb;
    recordDir_    = dir;
    recordFile_   = full;
    setRecordStyle(true);
    fprintf(stderr, "[ui] 开始录制 -> %s\n", full.toUtf8().constData());
    return true;
}

void MainWindow::stopRecordingUi() {
    ctrl_->stopRecording();
    recording_ = false;
    recordToUsb_ = false;
    recordDir_.clear();
    recordFile_.clear();
    setRecordStyle(false);
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

bool MainWindow::copyFileWithProgress(const QString& src, const QString& dst,
                                      const std::function<void(qint64)>& onChunk) {
    QFile in(src);
    if (!in.open(QIODevice::ReadOnly)) return false;
    QFile out(dst);
    if (!out.open(QIODevice::WriteOnly)) { in.close(); return false; }

    const qint64 kChunk = 1 << 20;   // 1MB
    std::vector<char> buf(kChunk);
    while (!in.atEnd()) {
        const qint64 n = in.read(buf.data(), kChunk);
        if (n <= 0) break;
        if (out.write(buf.data(), n) != n) { in.close(); out.close(); return false; }
        out.flush();
        if (onChunk) onChunk(n);
        QApplication::processEvents();   // 保持界面响应并刷新进度
    }
    in.close();
    out.close();
    return true;
}

void MainWindow::copyToUsb() {
    const QString usb = findUsbMount();
    if (usb.isEmpty()) {
        statusLabel_->setText(QStringLiteral(" 未检测到U盘，请插入后重试"));
        statusLabel_->adjustSize();
        return;
    }

    QDir srcDir(QString::fromUtf8(OUTDIR));
    QString dstDirPath = usb + QStringLiteral("/yunlu");
    if (!QDir().mkpath(dstDirPath)) {
        statusLabel_->setText(QStringLiteral(" U盘目录创建失败: ") + dstDirPath);
        statusLabel_->adjustSize();
        return;
    }

    // 收集待拷文件（跳过已存在且大小一致的）
    QStringList files = srcDir.entryList(
        QStringList() << QStringLiteral("*.mkv") << QStringLiteral("*.mp4")
                      << QStringLiteral("*.mp4.part"),
        QDir::Files, QDir::Time);

    struct Job { QString src, dst; qint64 size; };
    std::vector<Job> jobs;
    qint64 totalBytes = 0;
    int skipped = 0;
    for (const QString& f : files) {
        const QString srcPath = srcDir.absoluteFilePath(f);
        const QString dstPath = dstDirPath + QStringLiteral("/") + f;
        QFileInfo si(srcPath);
        if (!si.isFile()) continue;
        QFileInfo di(dstPath);
        if (di.exists() && di.size() == si.size()) { ++skipped; continue; }
        jobs.push_back({srcPath, dstPath, si.size()});
        totalBytes += si.size();
    }

    if (jobs.isEmpty()) {
        copyBar_->hide();
        statusLabel_->setText(QStringLiteral(" 无新文件需要拷贝(跳过 %1 个)").arg(skipped));
        statusLabel_->adjustSize();
        return;
    }

    copyBar_->setRange(0, 100);
    copyBar_->setValue(0);
    copyBar_->show();

    int copied = 0, failed = 0;
    qint64 doneBytes = 0;
    for (const Job& j : jobs) {
        statusLabel_->setText(QStringLiteral(" 拷贝中 %1 (%2/%3)")
                              .arg(QFileInfo(j.src).fileName())
                              .arg(copied + 1).arg(jobs.size()));
        statusLabel_->adjustSize();
        copyBar_->setFormat(QStringLiteral("%p%"));

        if (QFile::exists(j.dst)) QFile::remove(j.dst);
        bool ok = copyFileWithProgress(j.src, j.dst, [&](qint64 n){
            doneBytes += n;
            if (totalBytes > 0) {
                const int pct = static_cast<int>(doneBytes * 100 / totalBytes);
                copyBar_->setValue(pct);
                copyBar_->setFormat(QStringLiteral("%1 / %2 MB  %3%")
                    .arg(doneBytes / (1024 * 1024)).arg(totalBytes / (1024 * 1024)).arg(pct));
                statusLabel_->setText(QStringLiteral(" 拷貝中 %1 %2%")
                    .arg(QFileInfo(j.src).fileName()).arg(pct));
            }
        });
        if (ok) ++copied; else ++failed;
    }

    copyBar_->hide();
    copyBar_->reset();
    statusLabel_->setText(QStringLiteral(" U盘拷贝完成: 新增 %1, 跳过 %2, 失败 %3 -> %4")
                          .arg(copied).arg(skipped).arg(failed).arg(dstDirPath));
    statusLabel_->adjustSize();
}

void MainWindow::refreshStatus() {
    if (!ctrl_) return;

    // 录制状态回查：写失败时 MediaOutput 会静默关闭录制 sink，按钮需同步复位
    if (recording_ && !ctrl_->isRecording()) {
        recording_ = false;
        recordToUsb_ = false;
        recordDir_.clear();
        recordFile_.clear();
        setRecordStyle(false);
        statusLabel_->setText(QStringLiteral(" 录制已中断(存储写满或已拔出)"));
        statusLabel_->adjustSize();
        return;
    }

    // 单文件过大自动分段；存储余量不足时干净停止（保留写尾能力）
    if (recording_) {
        QStorageInfo st(currentRecordDir());
        const bool lowDisk = st.isValid() && st.isReady() && st.bytesAvailable() < minFreeBytes_;
        if (lowDisk) {
            stopRecordingUi();
            statusLabel_->setText(QStringLiteral(" 存储空间不足，已停止录制，请备份后清理"));
            statusLabel_->adjustSize();
            return;
        }

        if (ctrl_->recorderBytes() >= maxRecordBytes_) {
            QString dir = recordDir_;
            if (recordToUsb_) {
                QString err;
                QString d = usbRecordDir(&err);
                if (d.isEmpty()) {
                    stopRecordingUi();
                    statusLabel_->setText(QStringLiteral(" ") + err + QStringLiteral("，已停止录制"));
                    statusLabel_->adjustSize();
                    return;
                }
                dir = d;
            }
            QString fname = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + recordExt_;
            if (ctrl_->rotateRecording((dir + QStringLiteral("/") + fname).toStdString())) {
                recordFile_ = dir + QStringLiteral("/") + fname;
                statusLabel_->setText(QStringLiteral(" 录制分段: ") + fname);
                statusLabel_->adjustSize();
                return;
            }
        }
    }

    if (!ctrl_->hasSignal()) {
        statusLabel_->setText(QStringLiteral(" 等待 HDMI 信号..."));
        statusLabel_->adjustSize();
        return;
    }
    if (copyBar_->isVisible()) return;   // 拷贝中不覆盖拷贝状态

    QString s = QStringLiteral(" 采集:开");
    s += QStringLiteral(" 录制:") + (ctrl_->isRecording() ? QStringLiteral("●") : QStringLiteral("○"));
    s += QStringLiteral(" 推流:") + (ctrl_->isStreaming() ? QStringLiteral("▶") : QStringLiteral("□"));
    s += QStringLiteral(" 丢帧:") + QString::number(ctrl_->droppedFrames());

    // 当前文件大小
    if (ctrl_->isRecording())
        s += QStringLiteral(" ") + QString::number(ctrl_->recorderBytes() / (1024 * 1024))
             + QStringLiteral("MB");
    // 目标存储剩余空间
    QStorageInfo st(currentRecordDir());
    if (st.isValid() && st.isReady()) {
        const double gb = st.bytesAvailable() / (1024.0 * 1024.0 * 1024.0);
        s += QStringLiteral(" 剩:%1GB").arg(gb, 0, 'f', 1);
        if (recordToUsb_) s += QStringLiteral("(U盘)");
    }
    s += QStringLiteral("   ") + QString::number(ctrl_->captureWidth()) + QStringLiteral("x")
         + QString::number(ctrl_->captureHeight());
    s += QStringLiteral("  ") + QString::number(ctrl_->captureFps(), 'f', 1) + QStringLiteral("fps");
    if (recording_) s += recordToUsb_ ? QStringLiteral(" [U盘录制]") : QStringLiteral(" [本地录制]");

    statusLabel_->setText(s);
    statusLabel_->adjustSize();
}

} // namespace yunlu
