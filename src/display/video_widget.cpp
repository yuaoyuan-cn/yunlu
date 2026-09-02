#include "video_widget.h"

#include <cstring>
#include <cstdio>

VideoWidget::VideoWidget(QWidget* parent)
    : QOpenGLWidget(parent) {
    // 只在必要更新时重绘，交由外部 QTimer 驱动
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
}

VideoWidget::~VideoWidget() {
    delete[] bufY_; delete[] bufUV_;
}

void VideoWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
    renderer_ = new VideoRenderer(this->context()->functions());
    renderer_->initialize();
}

void VideoWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void VideoWidget::submitFrame(const yunlu::VideoFrame& f) {
    QMutexLocker l(&mutex_);
    if (f.width != w_ || f.height != h_) {
        delete[] bufY_; delete[] bufUV_;
        w_ = f.width; h_ = f.height;
        lsy_ = f.linesize_y; lsuv_ = f.linesize_uv;
        bufY_  = new uint8_t[lsy_ * h_];
        bufUV_ = new uint8_t[lsuv_ * (h_ / 2)];
    }
    memcpy(bufY_, f.y, (size_t)lsy_ * h_);
    memcpy(bufUV_, f.uv, (size_t)lsuv_ * (h_ / 2));
    hasFrame_ = true;
}

void VideoWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);
    if (!renderer_ || !hasFrame_) return;

    uint8_t *y = nullptr, *uv = nullptr;
    int lsy = 0, lsuv = 0, w = 0, h = 0;
    {
        QMutexLocker l(&mutex_);
        if (!hasFrame_) return;
        y = bufY_; uv = bufUV_; lsy = lsy_; lsuv = lsuv_; w = w_; h = h_;
    }
    if (w <= 0) return;
    renderer_->render(y, uv, lsy, lsuv, w, h, mirror_);
}
