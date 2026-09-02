#ifndef YUNLU_VIDEO_WIDGET_H
#define YUNLU_VIDEO_WIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QMutex>
#include <QImage>

#include "../common.h"
#include "video_renderer.h"

// 全屏视频预览控件：GPU 纹理绘制 NV12
class VideoWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit VideoWidget(QWidget* parent = nullptr);
    ~VideoWidget() override;

    // 从采集线程调用：拷贝帧到内部缓冲供渲染
    void submitFrame(const yunlu::VideoFrame& f);

public slots:
    void setMirror(bool m) { mirror_ = m; }

signals:
    void frameRendered();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

private:
    VideoRenderer* renderer_ = nullptr;
    QMutex mutex_;
    uint8_t* bufY_  = nullptr;
    uint8_t* bufUV_ = nullptr;
    int lsy_ = 0, lsuv_ = 0, w_ = 0, h_ = 0;
    bool mirror_ = false;
    bool hasFrame_ = false;
};

#endif // YUNLU_VIDEO_WIDGET_H
