// VialFX interface shell on Visage (C++20). Talks to the plugin only through gui_bridge.h.
//
// GPL-3.0-or-later, like the rest of VialFX.

#include "gui_bridge.h"

#include <visage/app.h>

#include "embedded/vialfx_fonts.h"

#include <algorithm>
#include <cmath>
#include <map>

using namespace visage::dimension;

namespace vialfx {
  namespace {
    constexpr float kPi = 3.14159265f;
    constexpr int kDesignWidth = 960;
    constexpr int kDesignHeight = 620;
    constexpr float kHeaderHeight = 56.0f;
    constexpr float kTabHeight = 34.0f;

    constexpr unsigned kBackground = 0xff1d1f24;
    constexpr unsigned kPanel = 0xff2a2d34;
    constexpr unsigned kAccent = 0xff4cc9f0;
    constexpr unsigned kText = 0xffe6e8ee;
    constexpr unsigned kDim = 0xff8d92a0;

    std::string titleCase(const std::string& s) {
      std::string out;
      bool up = true;
      for (char c : s) {
        if (c == '_') { out += ' '; up = true; continue; }
        out += up ? (char)std::toupper((unsigned char)c) : c;
        up = false;
      }
      return out;
    }

    class Knob : public visage::Frame {
      public:
        Knob(GuiHost* host, const GuiParam& param) : host_(host), param_(param) { }

        void refresh() {
          float v = host_->value(param_.id);
          if (v != value_) {
            value_ = v;
            redraw();
          }
        }

        float normalized() const {
          float range = param_.max - param_.min;
          return range > 0.0f ? std::clamp((value_ - param_.min) / range, 0.0f, 1.0f) : 0.0f;
        }

        void apply(float norm) {
          norm = std::clamp(norm, 0.0f, 1.0f);
          float v = param_.min + norm * (param_.max - param_.min);
          if (param_.stepped)
            v = std::round(v);
          value_ = v;
          host_->setValue(param_.id, v);
          redraw();
        }

        void draw(visage::Canvas& canvas) override {
          float w = width();
          float size = std::min(w, height() - 34.0f * dpiScale());
          float x = (w - size) * 0.5f;
          float y = 6.0f * dpiScale();
          float thick = std::max(3.0f, size * 0.1f);
          float half = 0.75f * kPi;

          canvas.setColor(kPanel);
          canvas.arc(x, y, size, thick, 0.0f, half, true);
          canvas.setColor(hovering_ ? 0xff7fdcf7 : kAccent);
          float n = normalized();
          float start = -half;
          float end = start + 2.0f * half * n;
          if (n > 0.001f)
            canvas.arc(x, y, size, thick, 0.5f * (start + end), 0.5f * (end - start), true);

          visage::Font label_font(11.0f * dpiScale(), resources::fonts::Lato_Regular_ttf);
          canvas.setColor(kText);
          canvas.text(host_->valueText(param_.id, value_), label_font, visage::Font::kCenter, 0, y + size,
                      w, 14.0f * dpiScale());
          canvas.setColor(kDim);
          canvas.text(param_.display_name, label_font, visage::Font::kCenter, 0, y + size + 14.0f * dpiScale(),
                      w, 14.0f * dpiScale());
        }

        void mouseEnter(const visage::MouseEvent&) override { hovering_ = true; redraw(); }
        void mouseExit(const visage::MouseEvent&) override { hovering_ = false; redraw(); }

        void mouseDown(const visage::MouseEvent& e) override {
          if (e.repeatClickCount() >= 2) {
            host_->beginGesture(param_.id);
            value_ = param_.default_value;
            host_->setValue(param_.id, value_);
            host_->endGesture(param_.id);
            redraw();
            return;
          }
          host_->beginGesture(param_.id);
          dragging_ = true;
          drag_start_y_ = e.position.y;
          drag_start_norm_ = normalized();
        }

        void mouseDrag(const visage::MouseEvent& e) override {
          if (!dragging_)
            return;
          float sensitivity = e.isShiftDown() ? 800.0f : 200.0f;
          apply(drag_start_norm_ + (drag_start_y_ - e.position.y) / sensitivity);
        }

        void mouseUp(const visage::MouseEvent&) override {
          if (dragging_)
            host_->endGesture(param_.id);
          dragging_ = false;
        }

        bool mouseWheel(const visage::MouseEvent& e) override {
          host_->beginGesture(param_.id);
          float step = param_.stepped ? 1.0f / std::max(1.0f, param_.max - param_.min) : 0.02f;
          apply(normalized() + (e.wheel_delta_y > 0 ? step : -step));
          host_->endGesture(param_.id);
          return true;
        }

      private:
        GuiHost* host_;
        GuiParam param_;
        float value_ = -1.0e30f;
        bool hovering_ = false;
        bool dragging_ = false;
        float drag_start_y_ = 0.0f;
        float drag_start_norm_ = 0.0f;
    };

    // A page of knobs for one module of the engine.
    class ModulePage : public visage::Frame {
      public:
        ModulePage(GuiHost* host, const std::vector<GuiParam>& params) {
          for (const GuiParam& p : params) {
            knobs_.push_back(std::make_unique<Knob>(host, p));
            addChild(knobs_.back().get());
          }
        }

        void resized() override {
          if (knobs_.empty())
            return;
          float cell = 92.0f * dpiScale();
          int columns = std::max(1, (int)(width() / cell));
          float cell_w = width() / columns;
          for (size_t i = 0; i < knobs_.size(); ++i) {
            int col = (int)i % columns, row = (int)i / columns;
            knobs_[i]->setBounds(col * cell_w, row * (cell + 10.0f * dpiScale()), cell_w, cell + 6.0f * dpiScale());
          }
        }

        void refresh() { for (auto& k : knobs_) k->refresh(); }

      private:
        std::vector<std::unique_ptr<Knob>> knobs_;
    };

    class TabBar : public visage::Frame {
      public:
        std::vector<std::string> tabs;
        int selected = 0;
        std::function<void(int)> on_select;

        void draw(visage::Canvas& canvas) override {
          visage::Font font(12.0f * dpiScale(), resources::fonts::Lato_Regular_ttf);
          float x = 0.0f;
          for (size_t i = 0; i < tabs.size(); ++i) {
            float tab_w = tabWidth(i);
            canvas.setColor((int)i == selected ? kPanel : kBackground);
            canvas.roundedRectangle(x, 0, tab_w - 3.0f * dpiScale(), height(), 6.0f * dpiScale());
            canvas.setColor((int)i == selected ? kAccent : kDim);
            canvas.text(tabs[i], font, visage::Font::kCenter, x, 0, tab_w - 3.0f * dpiScale(), height());
            x += tab_w;
          }
        }

        void mouseDown(const visage::MouseEvent& e) override {
          float x = 0.0f;
          for (size_t i = 0; i < tabs.size(); ++i) {
            x += tabWidth(i);
            if (e.position.x < x) {
              selected = (int)i;
              if (on_select) on_select(selected);
              redraw();
              return;
            }
          }
        }

      private:
        float tabWidth(size_t i) const { return (14.0f + 7.0f * tabs[i].size()) * dpiScale(); }
    };

    class VialfxGui : public GuiWindow {
      public:
        explicit VialfxGui(GuiHost* host) : host_(host) {
          std::map<std::string, std::vector<GuiParam>> by_module;
          for (const GuiParam& p : host->params())
            if (p.name != "bypass")
              by_module[p.module].push_back(p);

          app_ = std::make_unique<visage::ApplicationWindow>();
          app_->setWindowDimensions(kDesignWidth, kDesignHeight);
          app_->setFixedAspectRatio(true);
          app_->onDraw() = [this](visage::Canvas& canvas) { drawBackdrop(canvas); };
          app_->onWindowContentsResized() = [this] { if (on_resized) on_resized(); };

          for (auto& entry : by_module) {
            pages_.push_back(std::make_unique<ModulePage>(host, entry.second));
            tabs_.tabs.push_back(titleCase(entry.first));
            app_->addChild(pages_.back().get(), false);
          }
          tabs_.on_select = [this](int index) { showPage(index); };
          app_->addChild(&tabs_);
          app_->onResize() += [this] { layout(); };
          layout();
          if (!pages_.empty())
            showPage(0);
          timer_.onTimerCallback() = [this] {
            for (auto& p : pages_)
              if (p->isVisible()) p->refresh();
          };
          timer_.startTimer(50);
        }

        ~VialfxGui() override { close(); }

        bool attach(void* parent) override {
          app_->show(parent);
          return true;
        }
        void close() override {
          timer_.stopTimer();
          if (app_) app_->close();
        }
        uint32_t width() const override { return (uint32_t)std::lround(app_->nativeWidth()); }
        uint32_t height() const override { return (uint32_t)std::lround(app_->nativeHeight()); }
        void setSizeFromHost(uint32_t w, uint32_t h) override { app_->setNativeWindowDimensionsFromHost((int)w, (int)h); }
        void adjustSize(uint32_t* w, uint32_t* h) override { app_->adjustWindowDimensions(w, h, true, true); }
        bool fixedAspectRatio() const override { return app_->isFixedAspectRatio(); }
        float aspectRatio() const override { return app_->aspectRatio(); }
        int posixFd() const override { return app_ && app_->window() ? app_->window()->posixFd() : -1; }
        void processPosixFd() override {
#ifdef __linux__
          if (app_ && app_->window()) app_->window()->processPluginFdEvents();
#endif
        }

      private:
        void drawBackdrop(visage::Canvas& canvas) {
          canvas.setColor(kBackground);
          canvas.fill(0, 0, app_->width(), app_->height());
          float s = app_->dpiScale();
          canvas.setColor(kPanel);
          canvas.fill(0, 0, app_->width(), kHeaderHeight * s);
          visage::Font title(24.0f * s, resources::fonts::Lato_Regular_ttf);
          visage::Font small(11.0f * s, resources::fonts::Lato_Regular_ttf);
          canvas.setColor(kAccent);
          canvas.text("VialFX", title, visage::Font::kLeft, 20.0f * s, 0, 200.0f * s, kHeaderHeight * s);
          canvas.setColor(kDim);
          std::string right = "Visage interface (test)  |  " + host_->version();
          canvas.text(right, small, visage::Font::kRight, app_->width() - 420.0f * s - 16.0f * s, 0,
                      420.0f * s, kHeaderHeight * s);
        }

        void layout() {
          float s = app_->dpiScale();
          float pad = 16.0f * s;
          tabs_.setBounds(pad, kHeaderHeight * s + 10.0f * s, app_->width() - 2 * pad, kTabHeight * s);
          float top = (kHeaderHeight + kTabHeight + 24.0f) * s;
          for (auto& p : pages_)
            p->setBounds(pad, top, app_->width() - 2 * pad, app_->height() - top - pad);
        }

        void showPage(int index) {
          for (size_t i = 0; i < pages_.size(); ++i)
            pages_[i]->setVisible((int)i == index);
          if (index >= 0 && index < (int)pages_.size())
            pages_[index]->refresh();
        }

        GuiHost* host_;
        std::unique_ptr<visage::ApplicationWindow> app_;
        std::vector<std::unique_ptr<ModulePage>> pages_;
        TabBar tabs_;
        visage::EventTimer timer_;
    };
  }

  std::unique_ptr<GuiWindow> createGuiWindow(GuiHost* host) {
    return std::make_unique<VialfxGui>(host);
  }
}
