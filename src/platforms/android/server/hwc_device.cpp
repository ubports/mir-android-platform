/*
 * Copyright © 2013 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by:
 *   Kevin DuBois <kevin.dubois@canonical.com>
 */

#include "swapping_gl_context.h"
#include "hwc_device.h"
#include "hwc_layerlist.h"
#include "hwc_wrapper.h"
#include "framebuffer_bundle.h"
#include "buffer.h"
#include "hwc_fallback_gl_renderer.h"
#include "mir/raii.h"
#include <limits>
#include <algorithm>
#include <chrono>
#include <thread>

namespace mg = mir::graphics;
namespace mga=mir::graphics::android;
namespace geom = mir::geometry;

namespace
{
bool plane_alpha_is_translucent(mg::Renderable const& renderable)
{
    float static const tolerance
    {
        1.0f/(2.0 * static_cast<float>(std::numeric_limits<decltype(hwc_layer_1_t::planeAlpha)>::max()))
    };
    return (renderable.alpha() < 1.0f - tolerance);
}
}

bool mga::HwcDevice20::compatible_renderlist(RenderableList const& list)
{
    return false;
}

bool mga::HwcDevice::compatible_renderlist(RenderableList const& list)
{
    if (list.empty())
        return false;

    for (auto const& renderable : list)
    {
        // TODO: enable planeAlpha for (hwc version >= 1.2), 90 deg rotation
        static glm::mat4 const identity(1, 0, 0, 0,  //
                                        0, 1, 0, 0,  //
                                        0, 0, 1, 0,  //
                                        0, 0, 0, 1);
        if (plane_alpha_is_translucent(*renderable) ||
            renderable->transformation() != identity)
        {
            return false;
        }
    }
    return true;
}

mga::HwcDevice::HwcDevice(std::shared_ptr<HwcWrapper> const& hwc_wrapper) :
    hwc_wrapper(hwc_wrapper)
{
}

bool mga::HwcDevice::buffer_is_onscreen(mg::Buffer const& buffer) const
{
    /* check the handles, as the buffer ptrs might change between sets */
    auto const handle = buffer.native_buffer_handle().get();
    auto it = std::find_if(
        onscreen_overlay_buffers.begin(), onscreen_overlay_buffers.end(),
        [&handle](std::shared_ptr<mg::Buffer> const& b)
        {
            return (handle == b->native_buffer_handle().get());
        });
    return it != onscreen_overlay_buffers.end();
}

void mga::HwcDevice::commit(std::list<DisplayContents> const& contents)
{
    std::vector<std::shared_ptr<mg::Buffer>> next_onscreen_overlay_buffers;

    hwc_wrapper->prepare(contents);

    bool purely_overlays = true;

    for (auto& content : contents)
    {
        if (content.list.needs_swapbuffers())
        {
            auto rejected_renderables = content.list.rejected_renderables();
            if (!rejected_renderables.empty())
            {
                auto current_context = mir::raii::paired_calls(
                    [&]{ content.context.make_current(); },
                    [&]{ content.context.release_current(); });
                content.compositor.render(std::move(rejected_renderables), content.list_offset, content.context);
            }
            content.list.setup_fb(content.context.last_rendered_buffer());
            content.list.swap_occurred();
            purely_overlays = false;
        }
    
        //setup overlays
        for (auto& layer : content.list)
        {
            auto buffer = layer.layer.buffer();
            if (layer.layer.is_overlay() && buffer)
            {
                if (!buffer_is_onscreen(*buffer))
                    layer.layer.set_acquirefence();
                next_onscreen_overlay_buffers.push_back(buffer);
            }
        }
    }

    hwc_wrapper->set(contents);
    onscreen_overlay_buffers = std::move(next_onscreen_overlay_buffers);

    for (auto& content : contents)
    {
        for (auto& it : content.list)
            it.layer.release_buffer();

        mir::Fd retire_fd(content.list.retirement_fence());
    }

    /*
     * Test results (how long can we sleep for without missing a frame?):
     *   arale:   10ms  (TODO: Find out why arale is so slow)
     *   mako:    15ms
     *   krillin: 11ms  (to be fair, the display is 67Hz)
     */
    using namespace std;
    recommend_sleep = purely_overlays ? 10ms : 0ms;
}

std::chrono::milliseconds mga::HwcDevice::recommended_sleep() const
{
    return recommend_sleep;
}

void mga::HwcDevice::content_cleared()
{
    onscreen_overlay_buffers.clear();
}

bool mga::HwcDevice::can_swap_buffers() const
{
    return true;
}
