#pragma once

#include "JuceHeader.h"
#include <vector>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

// Full-screen glow (bloom) applied after the interface is drawn. Every pixel that is close to one of the
// skin's glowing colours spreads a soft halo of itself around it, so glow works on anything that is drawn
// in that colour: text, lines, knobs, waveforms, meters.
class GlowPass {
  public:
    static constexpr int kMaxKeys = 12;

    struct Key {
      float red, green, blue, glow;
    };

    void setKeys(const std::vector<Key>& keys) { keys_ = keys; }
    bool active() const { return !keys_.empty() && !failed_; }

    void render(OpenGLContext& context, int width, int height, float scale) {
      if (!active() || width <= 0 || height <= 0)
        return;

      if (program_ == nullptr && !build(context))
        return;

      auto& ext = context.extensions;
      glViewport(0, 0, width, height);
      glDisable(GL_SCISSOR_TEST);
      glDisable(GL_DEPTH_TEST);

      if (texture_ == 0)
        glGenTextures(1, &texture_);

      ext.glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, texture_);
      if (width != texture_width_ || height != texture_height_) {
        texture_width_ = width;
        texture_height_ = height;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      }
      glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);

      float strongest = 0.0f;
      float data[kMaxKeys * 4] = { };
      int count = std::min(kMaxKeys, static_cast<int>(keys_.size()));
      for (int i = 0; i < count; ++i) {
        data[4 * i] = keys_[i].red;
        data[4 * i + 1] = keys_[i].green;
        data[4 * i + 2] = keys_[i].blue;
        data[4 * i + 3] = keys_[i].glow;
        strongest = std::max(strongest, keys_[i].glow);
      }

      program_->use();
      program_->setUniform("tex", 0);
      program_->setUniform("texel", 1.0f / width, 1.0f / height);
      program_->setUniform("radius", (8.0f + 28.0f * strongest) * scale);
      program_->setUniform("count", count);
      program_->setUniform("keys", data, kMaxKeys * 4);

      glEnable(GL_BLEND);
      glBlendFunc(GL_ONE, GL_ONE);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glDisable(GL_BLEND);
      glBindTexture(GL_TEXTURE_2D, 0);
    }

    void destroy() {
      if (texture_ != 0) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
      }
      texture_width_ = texture_height_ = 0;
      program_ = nullptr;
      failed_ = false;
    }

  private:
    bool build(OpenGLContext& context) {
      static const char* kVertex =
          "varying vec2 uv;\n"
          "void main() {\n"
          "    vec2 p = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));\n"
          "    uv = p;\n"
          "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
          "}\n";

      static const char* kFragment =
          "uniform sampler2D tex;\n"
          "uniform vec2 texel;\n"
          "uniform float radius;\n"
          "uniform int count;\n"
          "uniform float keys[48];\n"
          "varying vec2 uv;\n"
          "void main() {\n"
          "    vec3 total = vec3(0.0);\n"
          "    const int kTaps = 32;\n"
          "    for (int i = 0; i < kTaps; ++i) {\n"
          "        float fi = float(i) + 0.5;\n"
          "        float r = sqrt(fi / float(kTaps));\n"
          "        float a = fi * 2.39996323;\n"
          "        vec2 offset = vec2(cos(a), sin(a)) * r * radius;\n"
          "        vec3 c = texture2D(tex, uv + offset * texel).rgb;\n"
          "        float w = 0.0;\n"
          "        for (int k = 0; k < 12; ++k) {\n"
          "            if (k >= count)\n"
          "                break;\n"
          "            vec4 key = vec4(keys[4 * k], keys[4 * k + 1], keys[4 * k + 2], keys[4 * k + 3]);\n"
          "            float d = distance(c, key.rgb);\n"
          "            w = max(w, key.a * (1.0 - smoothstep(0.0, 0.35, d)));\n"
          "        }\n"
          "        total += c * w * (1.0 - 0.7 * r);\n"
          "    }\n"
          "    gl_FragColor = vec4(total * (4.0 / float(kTaps)), 1.0);\n"
          "}\n";

      auto program = std::make_unique<OpenGLShaderProgram>(context);
      if (program->addVertexShader(OpenGLHelpers::translateVertexShaderToV3(kVertex)) &&
          program->addFragmentShader(OpenGLHelpers::translateFragmentShaderToV3(kFragment)) &&
          program->link()) {
        program_ = std::move(program);
        return true;
      }

      failed_ = true;  // Glow is cosmetic: skip it when the driver can't build the shader.
      return false;
    }

    std::vector<Key> keys_;
    std::unique_ptr<OpenGLShaderProgram> program_;
    GLuint texture_ = 0;
    int texture_width_ = 0, texture_height_ = 0;
    bool failed_ = false;
};
