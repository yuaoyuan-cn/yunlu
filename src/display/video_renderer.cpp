#include "video_renderer.h"

#include <QOpenGLExtraFunctions>

namespace {
const char* kVShader = R"(
#version 300 es
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aTex;
out vec2 vTex;
uniform int uMirror;
void main() {
    vec2 t = aTex;
    // V4L2 采集行0在图像顶部，OpenGL 纹理行0在底部 -> 垂直翻转
    t.y = 1.0 - t.y;
    if (uMirror == 1) t.x = 1.0 - t.x;
    vTex = t;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kFShader = R"(
#version 300 es
precision mediump float;
in vec2 vTex;
out vec4 FragColor;
uniform sampler2D uY;
uniform sampler2D uUV;
void main() {
    float y  = texture(uY,  vTex).r;
    vec2 uv  = texture(uUV, vTex).rg - 0.5;
    // NV12 UV 平面是 (Cb,Cr) 交错 -> uv.x=Cb, uv.y=Cr
    float r = y + 1.402 * uv.y;
    float g = y - 0.344136 * uv.x - 0.714136 * uv.y;
    float b = y + 1.772    * uv.x;
    FragColor = vec4(clamp(r,0.0,1.0), clamp(g,0.0,1.0), clamp(b,0.0,1.0), 1.0);
}
)";

// 一个全屏三角形即可覆盖整个视口，避免退化
const float kVertices[] = {
    -1.0f, -1.0f, 0.0f, 0.0f,
     3.0f, -1.0f, 2.0f, 0.0f,
    -1.0f,  3.0f, 0.0f, 2.0f,
};
} // namespace

VideoRenderer::VideoRenderer(QOpenGLFunctions* gl) : gl_(gl) {}
VideoRenderer::~VideoRenderer() {
    if (prog_) { prog_->removeAllShaders(); delete prog_; prog_ = nullptr; }
    if (gl_) {
        if (texY_)  gl_->glDeleteTextures(1, &texY_);
        if (texUV_) gl_->glDeleteTextures(1, &texUV_);
    }
    if (vbo_.isCreated()) vbo_.destroy();
}

void VideoRenderer::initialize() {
    prog_ = new QOpenGLShaderProgram();
    prog_->addShaderFromSourceCode(QOpenGLShader::Vertex, kVShader);
    prog_->addShaderFromSourceCode(QOpenGLShader::Fragment, kFShader);
    prog_->link();
    prog_->bind();

    if (!vbo_.isCreated()) vbo_.create();
    vbo_.bind();
    vbo_.allocate(kVertices, sizeof(kVertices));

    gl_->glGenTextures(1, &texY_);
    gl_->glGenTextures(1, &texUV_);
    init_ = true;
}

void VideoRenderer::render(const uint8_t* y, const uint8_t* uv,
                           int lsy, int lsuv, int width, int height, bool mirror) {
    if (!init_) return;

    prog_->bind();
    vbo_.bind();

    prog_->enableAttributeArray(0);
    prog_->enableAttributeArray(1);
    prog_->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));
    prog_->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));
    prog_->setUniformValue("uMirror", mirror ? 1 : 0);

    gl_->glActiveTexture(GL_TEXTURE0);
    gl_->glBindTexture(GL_TEXTURE_2D, texY_);
    gl_->glPixelStorei(GL_UNPACK_ROW_LENGTH, lsy);
    gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, y);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    prog_->setUniformValue("uY", 0);

    gl_->glActiveTexture(GL_TEXTURE1);
    gl_->glBindTexture(GL_TEXTURE_2D, texUV_);
    // GL_UNPACK_ROW_LENGTH 以 texel 计。UV 平面每行 = width 字节，GL_RG8 每 texel=2字节
    // -> 每行 width/2 个 texel。tightly packed 时可直接为 0。
    gl_->glPixelStorei(GL_UNPACK_ROW_LENGTH, lsuv / 2);
    gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, width / 2, height / 2, 0, GL_RG, GL_UNSIGNED_BYTE, uv);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    prog_->setUniformValue("uUV", 1);

    gl_->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gl_->glDrawArrays(GL_TRIANGLES, 0, 3);

    prog_->disableAttributeArray(0);
    prog_->disableAttributeArray(1);
    prog_->release();
}
