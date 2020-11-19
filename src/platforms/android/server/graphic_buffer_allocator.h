/*
 * Copyright © 2017 The UBports project.
 * Copyright © 2012 Canonical Ltd.
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
 *   Marius Gripsgard <marius@ubports.com>
 */

#ifndef MIR_PLATFORM_ANDROID_GRAPHIC_BUFFER_ALLOCATOR_H_
#define MIR_PLATFORM_ANDROID_GRAPHIC_BUFFER_ALLOCATOR_H_

#include <cstddef>  // to fix missing #includes in graphics.h from hardware.h
#include <hardware/hardware.h>
#include "mir_toolkit/mir_native_buffer.h"

#include "mir/graphics/buffer_properties.h"
#include "mir/graphics/graphic_buffer_allocator.h"
#include "mir/graphics/wayland_allocator.h"

#include <EGL/egl.h>

namespace mir
{
namespace graphics
{

class EGLExtensions;

namespace android
{

class Gralloc;
class DeviceQuirks;
class CommandStreamSyncFactory;

class GraphicBufferAllocator:
  public graphics::GraphicBufferAllocator,
  public graphics::WaylandAllocator
{
public:
    GraphicBufferAllocator(
        std::shared_ptr<CommandStreamSyncFactory> const& cmdstream_sync_factory,
        std::shared_ptr<DeviceQuirks> const& quirks);

    std::shared_ptr<graphics::Buffer> alloc_buffer(
        graphics::BufferProperties const& buffer_properties) override;
    std::shared_ptr<graphics::Buffer> alloc_buffer(geometry::Size, uint32_t format, uint32_t flags) override;
    std::shared_ptr<graphics::Buffer> alloc_software_buffer(geometry::Size, MirPixelFormat) override;

    std::shared_ptr<graphics::Buffer> alloc_framebuffer(
        geometry::Size sz, MirPixelFormat pf);

    std::vector<MirPixelFormat> supported_pixel_formats() override;

    // WaylandAllocator
    void bind_display(wl_display* display) override;
    std::shared_ptr<Buffer> buffer_from_resource (wl_resource* buffer, std::function<void ()>&& on_consumed) override;
private:
    const hw_module_t    *hw_module;
    std::shared_ptr<Gralloc> alloc_device;
    std::shared_ptr<EGLExtensions> const egl_extensions;
    std::shared_ptr<CommandStreamSyncFactory> const cmdstream_sync_factory;
    std::shared_ptr<DeviceQuirks> const quirks;

    // WaylandAllocator
    EGLDisplay dpy;
};

}
}
}
#endif /* MIR_PLATFORM_ANDROID_GRAPHIC_BUFFER_ALLOCATOR_H_ */
