/* Copyright 2013-2019 Matt Tytel
 *
 * vital is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vital is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vital.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "border_bounds_constrainer.h"
#include "full_interface.h"
#include "load_save.h"
#include "synth_gui_interface.h"

void BorderBoundsConstrainer::checkBounds(Rectangle<int>& bounds, const Rectangle<int>& previous,
                                          const Rectangle<int>& limits,
                                          bool stretching_top, bool stretching_left,
                                          bool stretching_bottom, bool stretching_right) {
  border_.subtractFrom(bounds);
  double aspect_ratio = getFixedAspectRatio();

  ComponentBoundsConstrainer::checkBounds(bounds, previous, limits,
                                          stretching_top, stretching_left,
                                          stretching_bottom, stretching_right);

  Rectangle<int> display_area = Desktop::getInstance().getDisplays().getTotalBounds(true);
  if (gui_) {
    ComponentPeer* peer = gui_->getPeer();
    if (peer)
      peer->getFrameSize().subtractFrom(display_area);
  }

  if (display_area.getWidth() < bounds.getWidth()) {
    bounds.setWidth(display_area.getWidth());
    if (aspect_ratio > 0.0)
      bounds.setHeight(std::round(bounds.getWidth() / aspect_ratio));
  }
  if (display_area.getHeight() < bounds.getHeight()) {
    bounds.setHeight(display_area.getHeight());
    if (aspect_ratio > 0.0)
      bounds.setWidth(std::round(bounds.getHeight() * aspect_ratio));
  }

  border_.addTo(bounds);
}

void BorderBoundsConstrainer::resizeStart() {
  if (gui_)
    gui_->enableRedoBackground(false);
}

void BorderBoundsConstrainer::resizeEnd() {
  if (gui_) {
    float width_scale = gui_->getWidth() / (1.0f * vital::kDefaultWindowWidth);
    float height_scale = gui_->getHeight() / (1.0f * vital::kDefaultWindowHeight);
    LoadSave::saveWindowSize(width_scale);
    if (width_scale > 0.0f)
      LoadSave::saveWindowAspect(height_scale / width_scale);
    gui_->enableRedoBackground(true);
  }
}
