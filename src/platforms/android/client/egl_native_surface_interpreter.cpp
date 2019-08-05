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
 * Authored by: Kevin DuBois <kevin.dubois@canonical.com>
 */

#include "egl_native_surface_interpreter.h"
#include "mir/client/client_buffer.h"
#include "mir/mir_render_surface.h"
#include "sync_fence.h"
#include "android_format_conversion-inl.h"
#include <boost/throw_exception.hpp>
#include <hardware/gralloc.h>
#include <sstream>
#include <stdexcept>
#include <system/graphics.h>
#include <system/window.h>

namespace mcla = mir::client::android;
namespace mcl = mir::client;
namespace mga = mir::graphics::android;

mcla::EGLNativeSurfaceInterpreter::EGLNativeSurfaceInterpreter(EGLNativeSurface* surface)
    : surface(surface),
      driver_pixel_format(-1),
      sync_ops(std::make_shared<mga::RealSyncFileOps>()),
      hardware_bits(GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_HW_RENDER),
      software_bits(GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_HW_COMPOSER |
                    GRALLOC_USAGE_HW_TEXTURE),
      last_buffer_age(0)
{
}

mga::NativeBuffer* mcla::EGLNativeSurfaceInterpreter::driver_requests_buffer()
{
    acquire_surface();
    auto buffer = surface->get_current_buffer();
    last_buffer_age = buffer->age();
    auto buffer_to_driver = mga::to_native_buffer_checked(buffer->native_buffer_handle());

    ANativeWindowBuffer* anwb = buffer_to_driver->anwb();
    anwb->format = driver_pixel_format;
    return buffer_to_driver.get();
}

void mcla::EGLNativeSurfaceInterpreter::driver_returns_buffer(ANativeWindowBuffer*, int fence_fd)
{
    // TODO: pass fence to server instead of waiting here
    mga::SyncFence sync_fence(sync_ops, mir::Fd(fence_fd));
    sync_fence.wait();

    surface->swap_buffers_sync();
}

void mcla::EGLNativeSurfaceInterpreter::dispatch_driver_request_format(int format)
{
    /*
     * Here, we use the hack to "lock" the format to the first one set by
     * Android's libEGL at the EGL surface's creation time, which is the one
     * chosen at the Mir window creation time, the one Mir server always
     * acknowledge and acted upon. Some Android EGL implementation change this
     * later, resulting in incompatibility between Mir client and server. By
     * locking the format this way, the client will still render in the old
     * format (rendering code hornor the setting here).
     * TODO: find a way to communicate the format change back to the server. I
     * believe there must be a good reason to change the rendering format
     * (maybe for performance reason?).
     */
    if (driver_pixel_format == -1 || driver_pixel_format == 0 || format == 0)
        driver_pixel_format = format;
}

int mcla::EGLNativeSurfaceInterpreter::driver_requests_info(int key) const
{
    switch (key)
    {
    case NATIVE_WINDOW_WIDTH:
    case NATIVE_WINDOW_DEFAULT_WIDTH:
        if (!surface && requested_size.is_set())
        {
            return requested_size.value().width.as_int();
        }
        else
        {
            acquire_surface();
            return surface->get_parameters().width;
        }
    case NATIVE_WINDOW_HEIGHT:
    case NATIVE_WINDOW_DEFAULT_HEIGHT:
        if (!surface && requested_size.is_set())
        {
            return requested_size.value().height.as_int();
        }
        else
        {
            acquire_surface();
            return surface->get_parameters().height;
        }
    case NATIVE_WINDOW_FORMAT:
        return driver_pixel_format;
    case NATIVE_WINDOW_TRANSFORM_HINT:
        return 0;
    case NATIVE_WINDOW_MIN_UNDEQUEUED_BUFFERS:
        return 2;
    case NATIVE_WINDOW_CONCRETE_TYPE:
        return NATIVE_WINDOW_SURFACE;
    case NATIVE_WINDOW_CONSUMER_USAGE_BITS:
        if (!surface || surface->get_parameters().buffer_usage == mir_buffer_usage_hardware)
            return hardware_bits;
        else
            return software_bits;
    case NATIVE_WINDOW_DEFAULT_DATASPACE:
        return HAL_DATASPACE_UNKNOWN;
    case NATIVE_WINDOW_BUFFER_AGE:
        return last_buffer_age;
    case NATIVE_WINDOW_IS_VALID:
        // true
        return 1;
    default:
        std::stringstream sstream;
        sstream << "driver requested unsupported query. key: " << key;
        throw std::runtime_error(sstream.str());
    }
}

void mcla::EGLNativeSurfaceInterpreter::sync_to_display(bool should_sync)
{
    if (surface)
        surface->request_and_wait_for_configure(mir_window_attrib_swapinterval, should_sync);
}

void mcla::EGLNativeSurfaceInterpreter::dispatch_driver_request_buffer_count(unsigned int count)
{
    if (surface)
        surface->set_buffer_cache_size(count);
    else
        cache_count = count;
}

void mcla::EGLNativeSurfaceInterpreter::dispatch_driver_request_buffer_size(geometry::Size size)
{
    if (surface)
    {
        auto params = surface->get_parameters();
        if (geometry::Size{params.width, params.height} == size)
            return;
        surface->set_size(size);
    }
    else
    {
        requested_size = size;
    }
}

void mcla::EGLNativeSurfaceInterpreter::set_surface(EGLNativeSurface* s)
{
    surface = s;
    if (surface)
    {
        if (requested_size.is_set())
            surface->set_size(requested_size.value());
        if (cache_count.is_set())
            surface->set_buffer_cache_size(cache_count.value());
    }
}

void mcla::EGLNativeSurfaceInterpreter::acquire_surface() const
{
    if (surface)
        return;

    if (!native_key)
        throw std::runtime_error("no id to access MirRenderSurface");

    auto rs = mcl::render_surface_lookup(native_key);
    if (!rs)
        throw std::runtime_error("no MirRenderSurface found");
    auto size = rs->size();
    // kludge - the side effect of this function will pass the MirNativeSurface into
    rs->get_buffer_stream(size.width.as_int(), size.height.as_int(), mga::to_mir_format(driver_pixel_format), mir_buffer_usage_hardware);

    if (!surface)
        throw std::runtime_error("no EGLNativeSurface received from mirclient library");
}
