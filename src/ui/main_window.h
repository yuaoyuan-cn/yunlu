#ifndef YUNLU_MAIN_WINDOW_H
#define YUNLU_MAIN_WINDOW_H

#include <QMainWindow>
#include <QTimer>

#include "../controller.h"

class QPushButton;
class QLabel;
class QLineEdit;
class QButtonGroup;

class VideoWidget;

namespace yunlu {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Controller* ctrl, QWidget* parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private slots:
    void toggleRecording();
    void toggleStreaming();
    void toggleOsc();
    void refreshStatus();

private:
    void buildUi();

    Controller* ctrl_;
    VideoWidget* video_ = nullptr;

    QTimer* statusTimer_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* stateLabel_  = nullptr;
    QLineEdit* urlEdit_  = nullptr;

    QPushButton* btnRec_  = nullptr;
    QPushButton* btnStream_ = nullptr;
    QPushButton* btnOsc_  = nullptr;
    QPushButton* btnQuit_ = nullptr;

    bool recording_ = false;
    bool streaming_ = false;
    int64_t startedUs_ = 0;
};

} // namespace yunlu

#endif // YUNLU_MAIN_WINDOW_H
