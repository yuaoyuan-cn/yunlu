#ifndef YUNLU_MAIN_WINDOW_H
#define YUNLU_MAIN_WINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QString>
#include <functional>

#include "../controller.h"

class QPushButton;
class QLabel;
class QProgressBar;
class QKeyEvent;

class VideoWidget;

namespace yunlu {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Controller* ctrl, QWidget* parent = nullptr);

protected:
    void resizeEvent(QResizeEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private slots:
    void toggleRecording();
    void toggleUsbRecording();
    void toggleStreaming();
    void copyToUsb();
    void refreshStatus();

private:
    void buildUi();
    void loadConfig();
    void setRecordStyle(bool recording);
    QString findUsbMount();
    QString usbRecordDir(QString* err);
    QString currentRecordDir() const;
    bool startRecordingUi(bool toUsb);
    void stopRecordingUi();
    bool copyFileWithProgress(const QString& src, const QString& dst,
                              const std::function<void(qint64)>& onChunk);

    Controller* ctrl_;
    VideoWidget* video_ = nullptr;

    QTimer* statusTimer_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QProgressBar* copyBar_ = nullptr;

    QPushButton* btnRec_  = nullptr;
    QPushButton* btnStream_ = nullptr;
    QPushButton* btnUsbRec_ = nullptr;
    QPushButton* btnUsb_  = nullptr;
    QPushButton* btnQuit_ = nullptr;

    // 来自运行时配置文件
    QString rtmpUrl_;
    int bitrate_ = 0;
    int64_t maxRecordBytes_ = 2000LL * 1024 * 1024;
    int64_t minFreeBytes_ = 500LL * 1024 * 1024;
    QString recordExt_ = QStringLiteral(".mkv");

    bool recording_ = false;
    bool streaming_ = false;
    bool recordToUsb_ = false;
    QString recordDir_;   // 当前录制目录（本地或U盘）
    QString recordFile_;

    // U 盘接触不良自愈：录制被中断后等待 U 盘恢复并自动续录
    bool pendingResumeUsb_ = false;
    int64_t nextResumeTryUs_ = 0;
    int resumeTimes_ = 0;
};

} // namespace yunlu

#endif // YUNLU_MAIN_WINDOW_H
