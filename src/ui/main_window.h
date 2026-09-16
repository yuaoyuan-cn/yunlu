#ifndef YUNLU_MAIN_WINDOW_H
#define YUNLU_MAIN_WINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QString>

#include "../controller.h"

class QPushButton;
class QLabel;

class VideoWidget;

namespace yunlu {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Controller* ctrl, QWidget* parent = nullptr);

protected:
    void resizeEvent(QResizeEvent* e) override;
    void closeEvent(QCloseEvent* e) override;

private slots:
    void toggleRecording();
    void toggleStreaming();
    void refreshStatus();

private:
    void buildUi();
    void loadConfig();

    Controller* ctrl_;
    VideoWidget* video_ = nullptr;

    QTimer* statusTimer_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    QPushButton* btnRec_  = nullptr;
    QPushButton* btnStream_ = nullptr;

    // 来自运行时配置文件
    QString rtmpUrl_;
    int bitrate_ = 0;

    bool recording_ = false;
    bool streaming_ = false;
};

} // namespace yunlu

#endif // YUNLU_MAIN_WINDOW_H
