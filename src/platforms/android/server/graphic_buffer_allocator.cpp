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

#include "mir/graphics/platform.h"
#include "mir/graphics/egl_extensions.h"
#include "mir/graphics/egl_error.h"
#include "mir/graphics/buffer_properties.h"
#include "mir/graphics/buffer_ipc_message.h"
#include "cmdstream_sync_factory.h"
#include "sync_fence.h"
#include "android_native_buffer.h"
#include "graphic_buffer_allocator.h"
#include "gralloc_module.h"
#include "buffer.h"
#include "device_quirks.h"
#include "egl_sync_fence.h"
#include "android_format_conversion-inl.h"

#include <boost/throw_exception.hpp>

#include <wayland-server.h>

#include <stdexcept>

#define MIR_LOG_COMPONENT "android-buffer-allocator"
#include <mir/log.h>

namespace mg  = mir::graphics;
namespace mga = mir::graphics::android;
namespace geom = mir::geometry;

namespace
{

void alloc_dev_deleter(alloc_device_t* t)
{
    /* android takes care of delete for us */
    t->common.close((hw_device_t*)t);
}

void null_alloc_dev_deleter(alloc_device_t*)
{
}

}

mga::GraphicBufferAllocator::GraphicBufferAllocator(
    std::shared_ptr<CommandStreamSyncFactory> const& cmdstream_sync_factory,
    std::shared_ptr<DeviceQuirks> const& quirks)
    : egl_extensions(std::make_shared<mg::EGLExtensions>()),
    cmdstream_sync_factory(cmdstream_sync_factory),
    quirks(quirks)
{
    int err;

    err = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &hw_module);
    if (err < 0)
        BOOST_THROW_EXCEPTION(std::runtime_error("Could not open hardware module"));

    struct alloc_device_t* alloc_dev;
    err = hw_module->methods->open(hw_module, GRALLOC_HARDWARE_GPU0, (struct hw_device_t**) &alloc_dev);
    if (err < 0)
        BOOST_THROW_EXCEPTION(std::runtime_error("Could not open hardware module"));

    /* note for future use: at this point, the hardware module should be filled with vendor information
       that we can determine different courses of action based upon */

    std::shared_ptr<struct alloc_device_t> alloc_dev_ptr(
        alloc_dev,
        quirks->gralloc_cannot_be_closed_safely() ? null_alloc_dev_deleter : alloc_dev_deleter);
    alloc_device = std::make_shared<mga::GrallocModule>(
        alloc_dev_ptr, cmdstream_sync_factory, quirks);
}

std::shared_ptr<mg::Buffer> mga::GraphicBufferAllocator::alloc_buffer(
    mg::BufferProperties const& properties)
{
    return std::make_shared<Buffer>(
        reinterpret_cast<gralloc_module_t const*>(hw_module),
        alloc_device->alloc_buffer(
            properties.size,
            mga::to_android_format(properties.format),
            mga::convert_to_android_usage(properties.usage)),
        egl_extensions);
}

std::shared_ptr<mg::Buffer> mga::GraphicBufferAllocator::alloc_framebuffer(
    geometry::Size size, MirPixelFormat pf)
{
    return std::make_shared<Buffer>(
        reinterpret_cast<gralloc_module_t const*>(hw_module),
        alloc_device->alloc_buffer(
            size,
            mga::to_android_format(pf),
            quirks->fb_gralloc_bits()),
        egl_extensions);
}

std::vector<MirPixelFormat> mga::GraphicBufferAllocator::supported_pixel_formats()
{
    static std::vector<MirPixelFormat> const pixel_formats{
        mir_pixel_format_abgr_8888,
        mir_pixel_format_xbgr_8888,
        mir_pixel_format_rgb_888,
        mir_pixel_format_rgb_565
    };

    return pixel_formats;
}

std::shared_ptr<mg::Buffer> mga::GraphicBufferAllocator::alloc_software_buffer(
    geometry::Size size, MirPixelFormat format)
{
    return std::make_shared<Buffer>(
        reinterpret_cast<gralloc_module_t const*>(hw_module),
        alloc_device->alloc_buffer(
            size,
            mga::to_android_format(format),
            mga::convert_to_android_usage(mg::BufferUsage::software)),
        egl_extensions);
}

std::shared_ptr<mg::Buffer> mga::GraphicBufferAllocator::alloc_buffer(
    geometry::Size size, uint32_t native_format, uint32_t native_flags)
{
    return std::make_shared<Buffer>(
        reinterpret_cast<gralloc_module_t const*>(hw_module),
        alloc_device->alloc_buffer(size, native_format, native_flags),
        egl_extensions);
}

namespace
{
class WaylandBuffer :
    public mir::graphics::BufferBasic,
    public mir::graphics::NativeBufferBase,
    public mir::renderer::gl::TextureSource
{
public:
    static std::shared_ptr<mg::Buffer> mir_buffer_from_wl_buffer(
        EGLDisplay dpy,
        wl_resource* buffer,
        std::shared_ptr<mg::EGLExtensions> const& extensions,
        std::function<void()>&& on_consumed)
    {
        std::shared_ptr<WaylandBuffer> mir_buffer;
        DestructionShim* shim;

        if (auto notifier = wl_resource_get_destroy_listener(buffer, &on_buffer_destroyed))
        {
            // We've already constructed a shim for this buffer, update it.
            shim = wl_container_of(notifier, shim, destruction_listener);

            if (!(mir_buffer = shim->associated_buffer.lock()))
            {
                /*
                 * We've seen this wl_buffer before, but all the WaylandBuffers associated with it
                 * have been destroyed.
                 *
                 * Recreate a new WaylandBuffer to track the new compositor lifetime.
                 */
                mir_buffer = std::shared_ptr<WaylandBuffer>{
                    new WaylandBuffer{
                        dpy,
                        buffer,
                        extensions,
                        std::move(on_consumed)}};
                shim->associated_buffer = mir_buffer;
            }
        }
        else
        {
            mir_buffer = std::shared_ptr<WaylandBuffer>{
                new WaylandBuffer{
                    dpy,
                    buffer,
                    extensions,
                    std::move(on_consumed)}};
            shim = new DestructionShim;
            shim->destruction_listener.notify = &on_buffer_destroyed;
            shim->associated_buffer = mir_buffer;

            wl_resource_add_destroy_listener(buffer, &shim->destruction_listener);
        }

        mir_buffer->buffer_mutex = shim->mutex;
        return mir_buffer;
    }

    ~WaylandBuffer()
    {
        if (egl_image != EGL_NO_IMAGE_KHR)
            extensions->eglDestroyImageKHR(dpy, egl_image);

        std::lock_guard<std::mutex> lock{*buffer_mutex};
        if (buffer)
        {
            wl_resource_queue_event(buffer, WL_BUFFER_RELEASE);
        }
    }

    void gl_bind_to_texture() override
    {
        std::unique_lock<std::mutex> lock{*buffer_mutex};
        if (buffer == nullptr)
        {
            mir::log_warning("WaylandBuffer::gl_bind_to_texture() called on a destroyed wl_buffer", this);
            return;
        }
        if (egl_image == EGL_NO_IMAGE_KHR)
        {
            eglBindAPI(MIR_SERVER_EGL_OPENGL_API);

            const EGLint image_attrs[] =
                {
                    EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
                    EGL_NONE
                };

            egl_image = extensions->eglCreateImageKHR(
                dpy,
                EGL_NO_CONTEXT,
                EGL_WAYLAND_BUFFER_WL,
                buffer,
                image_attrs);

            if (egl_image == EGL_NO_IMAGE_KHR)
                BOOST_THROW_EXCEPTION(mg::egl_error("Failed to create EGLImage"));

            on_consumed();
        }
        lock.unlock();

        extensions->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, egl_image);
    }

    void bind() override
    {
        gl_bind_to_texture();
    }

    void secure_for_render() override
    {
    }

    std::shared_ptr<mir::graphics::NativeBuffer> native_buffer_handle() const override
    {
        return nullptr;
    }

    mir::geometry::Size size() const override
    {
        return mir::geometry::Size{width, height};
    }

    MirPixelFormat pixel_format() const override
    {
        return format;
    }

    mir::graphics::NativeBufferBase *native_buffer_base() override
    {
        return this;
    }

private:
    WaylandBuffer(
        EGLDisplay dpy,
        wl_resource* buffer,
        std::shared_ptr<mg::EGLExtensions> const& extensions,
        std::function<void()>&& on_consumed)
        : buffer{buffer},
        dpy{dpy},
        egl_image{EGL_NO_IMAGE_KHR},
        extensions{extensions},
        on_consumed{std::move(on_consumed)}
    {
        if (extensions->wayland->eglQueryWaylandBufferWL(dpy, buffer, EGL_WIDTH, &width) == EGL_FALSE)
        {
            BOOST_THROW_EXCEPTION(mg::egl_error("Failed to query WaylandAllocator buffer width"));
        }
        if (extensions->wayland->eglQueryWaylandBufferWL(dpy, buffer, EGL_HEIGHT, &height) == EGL_FALSE)
        {
            BOOST_THROW_EXCEPTION(mg::egl_error("Failed to query WaylandAllocator buffer height"));
        }

        EGLint texture_format;
        if (!extensions->wayland->eglQueryWaylandBufferWL(dpy, buffer, EGL_TEXTURE_FORMAT, &texture_format))
        {
            BOOST_THROW_EXCEPTION(mg::egl_error("Failed to query WL buffer format"));
        }

        if (texture_format == EGL_TEXTURE_RGB)
        {
            format = mir_pixel_format_xrgb_8888;
        }
        else if (texture_format == EGL_TEXTURE_RGBA)
        {
            format = mir_pixel_format_argb_8888;
        }
        else
        {
            BOOST_THROW_EXCEPTION((std::invalid_argument{"YUV buffers are unimplemented"}));
        }
    }

    static void on_buffer_destroyed(wl_listener* listener, void*)
    {
        static_assert(
                std::is_standard_layout<DestructionShim>::value,
                "DestructionShim must be Standard Layout for wl_container_of to be defined behaviour");

        DestructionShim* shim;
        shim = wl_container_of(listener, shim, destruction_listener);

        {
            std::lock_guard<std::mutex> lock{*shim->mutex};
            if (auto mir_buffer = shim->associated_buffer.lock())
            {
                mir_buffer->buffer = nullptr;
            }
        }

        delete shim;
    }

    struct DestructionShim
    {
        std::shared_ptr<std::mutex> const mutex = std::make_shared<std::mutex>();
        std::weak_ptr<WaylandBuffer> associated_buffer;
        wl_listener destruction_listener;
    };

    std::shared_ptr<std::mutex> buffer_mutex;
    wl_resource* buffer;

    EGLDisplay dpy;
    EGLImageKHR egl_image;

    EGLint width, height;
    MirPixelFormat format;

    std::shared_ptr<mg::EGLExtensions> const extensions;

    std::function<void()> on_consumed;
};
}

void mga::GraphicBufferAllocator::bind_display(wl_display* display)
{
    dpy = eglGetCurrentDisplay();

    if (dpy == EGL_NO_DISPLAY)
        BOOST_THROW_EXCEPTION((std::logic_error{"WaylandAllocator::bind_display called without an active EGL Display"}));

    if (!egl_extensions->wayland)
    {
        mir::log_warning("No EGL_WL_bind_wayland_display support");
        return;
    }

    if (egl_extensions->wayland->eglBindWaylandDisplayWL(dpy, display) == EGL_FALSE)
    {
        BOOST_THROW_EXCEPTION(mg::egl_error("Failed to bind Wayland display"));
    }
    else
    {
        mir::log_info("Bound WaylandAllocator display");
    }
}

std::shared_ptr<mg::Buffer> mga::GraphicBufferAllocator::buffer_from_resource (wl_resource* buffer, std::function<void ()>&& on_consumed)
{
    if (egl_extensions->wayland)
        return WaylandBuffer::mir_buffer_from_wl_buffer(
            dpy,
            buffer,
            egl_extensions,
            std::move(on_consumed));
    return nullptr;
}
