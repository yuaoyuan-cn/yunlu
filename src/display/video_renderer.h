#ifndef YUNLU_VIDEO_RENDERER_H
#define YUNLU_VIDEO_RENDERER_H

#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLTexture>

// 将 NV12 帧用 GPU 转为 RGB 并绘制（YUV->RGB shader），可选镜像/缩放
class VideoRenderer {
public:
    VideoRenderer(QOpenGLFunctions* gl);
    ~VideoRenderer();

    void initialize();
    void render(const uint8_t* y, const uint8_t* uv,
                int linesizeY, int linesizeUV, int width, int height,
                bool mirror);

private:
    QOpenGLFunctions* gl_ = nullptr;
    QOpenGLShaderProgram* prog_ = nullptr;
    QOpenGLBuffer vbo_;   // 全屏四边形
    GLuint texY_ = 0, texUV_ = 0;
    bool init_ = false;
    int texW_ = 0, texH_ = 0;
};

#endif // YUNLU_VIDEO_RENDERER_H
