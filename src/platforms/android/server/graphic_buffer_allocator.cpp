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
#include "mir/graphics/native_buffer.h"
#include "mir/raii.h"
#include "mir/graphics/display.h"
#include "mir/renderer/gl/context_source.h"
#include "mir/renderer/gl/context.h"
#include "mir/graphics/program_factory.h"
#include "mir/graphics/program.h"
#include "mir/executor.h"
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
#include <boost/exception/errinfo_errno.hpp>

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

std::unique_ptr<mir::renderer::gl::Context> context_for_output(mg::Display const& output)
{
    try
    {
        auto& context_source = dynamic_cast<mir::renderer::gl::ContextSource const&>(output);

        /*
         * We care about no part of this context's config; we will do no rendering with it.
         * All we care is that we can allocate texture IDs and bind a texture, which is
         * config independent.
         *
         * That's not *entirely* true; we also need it to be on the same device as we want
         * to do the rendering on, and that GL must support all the extensions we care about,
         * but since we don't yet support heterogeneous hybrid and implementing that will require
         * broader interface changes it's a safe enough requirement for now.
         */
        return context_source.create_gl_context();
    }
    catch (std::bad_cast const& err)
    {
        std::throw_with_nested(
            boost::enable_error_info(
                std::runtime_error{"Output platform cannot provide a GL context"})
                << boost::throw_function(__PRETTY_FUNCTION__)
                << boost::throw_line(__LINE__)
                << boost::throw_file(__FILE__));
    }
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

void mga::GraphicBufferAllocator::set_ctx(mg::Display const& output) {
  ctx = context_for_output(output);
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
GLuint get_tex_id()
{
    GLuint tex;
    glGenTextures(1, &tex);
    return tex;
}

geom::Size get_wl_buffer_size(wl_resource* buffer, mg::EGLExtensions::WaylandExtensions const& ext)
{
    EGLint width, height;

    auto dpy = eglGetCurrentDisplay();
    if (ext.eglQueryWaylandBufferWL(dpy, buffer, EGL_WIDTH, &width) == EGL_FALSE)
    {
        BOOST_THROW_EXCEPTION(mg::egl_error("Failed to query WaylandAllocator buffer width"));
    }
    if (ext.eglQueryWaylandBufferWL(dpy, buffer, EGL_HEIGHT, &height) == EGL_FALSE)
    {
        BOOST_THROW_EXCEPTION(mg::egl_error("Failed to query WaylandAllocator buffer height"));
    }

    return geom::Size{width, height};
}

mg::gl::Texture::Layout get_texture_layout(
    wl_resource* resource,
    mg::EGLExtensions::WaylandExtensions const& ext)
{
    EGLint inverted;
    auto dpy = eglGetCurrentDisplay();

    if (ext.eglQueryWaylandBufferWL(dpy, resource, EGL_WAYLAND_Y_INVERTED_WL, &inverted) == EGL_FALSE)
    {
        // EGL_WAYLAND_Y_INVERTED_WL is unsupported; the default is that the texture is in standard
        // GL texture layout
        return mg::gl::Texture::Layout::GL;
    }
    if (inverted)
    {
        // It has the standard y-decreases-with-row layout of GL textures
        return mg::gl::Texture::Layout::GL;
    }
    else
    {
        // It has y-increases-with-row layout.
        return mg::gl::Texture::Layout::TopRowFirst;
    }
}

EGLint get_wl_egl_format(wl_resource* resource, mg::EGLExtensions::WaylandExtensions const& ext)
{
    EGLint format;
    auto dpy = eglGetCurrentDisplay();

    if (ext.eglQueryWaylandBufferWL(dpy, resource, EGL_TEXTURE_FORMAT, &format) == EGL_FALSE)
    {
        BOOST_THROW_EXCEPTION(mg::egl_error("Failed to query Wayland buffer format"));
    }
    return format;
}

class WaylandTexBuffer :
    public mg::BufferBasic,
    public mg::NativeBufferBase,
    public mg::gl::Texture
{
public:
    // Note: Must be called with a current EGL context
    WaylandTexBuffer(
        std::shared_ptr<mir::renderer::gl::Context> ctx,
        wl_resource* buffer,
        mg::EGLExtensions const& extensions,
        std::function<void()>&& on_consumed,
        std::function<void()>&& on_release,
        std::shared_ptr<mir::Executor> wayland_executor)
        : ctx{std::move(ctx)},
          tex{get_tex_id()},
          on_consumed{std::move(on_consumed)},
          on_release{std::move(on_release)},
          size_{get_wl_buffer_size(buffer, *extensions.wayland)},
          layout_{get_texture_layout(buffer, *extensions.wayland)},
          egl_format{get_wl_egl_format(buffer, *extensions.wayland)},
          wayland_executor{std::move(wayland_executor)}
    {
        eglBindAPI(MIR_SERVER_EGL_OPENGL_API);

        const EGLint image_attrs[] =
            {
                EGL_WAYLAND_PLANE_WL, 0,
                EGL_NONE
            };

        auto egl_image = extensions.eglCreateImageKHR(
            eglGetCurrentDisplay(),
            EGL_NO_CONTEXT,
            EGL_WAYLAND_BUFFER_WL,
            buffer,
            image_attrs);

        if (egl_image == EGL_NO_IMAGE_KHR)
            BOOST_THROW_EXCEPTION(mg::egl_error("Failed to create EGLImage"));

        glBindTexture(GL_TEXTURE_2D, tex);
        extensions.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, egl_image);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // tex is now an EGLImage sibling, so we can free the EGLImage without
        // freeing the backing data.
        extensions.eglDestroyImageKHR(eglGetCurrentDisplay(), egl_image);
    }

    ~WaylandTexBuffer()
    {
        wayland_executor->spawn(
            [context = ctx, tex = tex]()
            {
                context->make_current();

                glDeleteTextures(1, &tex);

                context->release_current();
            });

        on_release();
    }

    std::shared_ptr<mir::graphics::NativeBuffer> native_buffer_handle() const override
    {
        return {nullptr};
    }

    mir::geometry::Size size() const override
    {
        return size_;
    }

    MirPixelFormat pixel_format() const override
    {
        /* TODO: These are lies, but the only piece of information external code uses
         * out of the MirPixelFormat is whether or not the buffer has an alpha channel.
         */
        switch(egl_format)
        {
            case EGL_TEXTURE_RGB:
                return mir_pixel_format_xrgb_8888;
            case EGL_TEXTURE_RGBA:
                return mir_pixel_format_argb_8888;
            case EGL_TEXTURE_EXTERNAL_WL:
                // Unspecified whether it has an alpha channel; say it does.
                return mir_pixel_format_argb_8888;
            case EGL_TEXTURE_Y_U_V_WL:
            case EGL_TEXTURE_Y_UV_WL:
                // These are just absolutely not RGB at all!
                // But they're defined to not have an alpha channel, so xrgb it is!
                return mir_pixel_format_xrgb_8888;
            case EGL_TEXTURE_Y_XUXV_WL:
                // This is a planar format, but *does* have alpha.
                return mir_pixel_format_argb_8888;
            default:
                // We've covered all possibilities above
                BOOST_THROW_EXCEPTION((std::logic_error{"Unexpected texture format!"}));
        }
    }

    NativeBufferBase* native_buffer_base() override
    {
        return this;
    }

    mir::graphics::gl::Program const& shader(mir::graphics::gl::ProgramFactory& cache) const override
    {
        static std::unique_ptr<mg::gl::Program> shader;
        if (!shader)
        {
            shader = cache.compile_fragment_shader(
                "",
                "uniform sampler2D tex;\n"
                "vec4 sample_to_rgba(in vec2 texcoord)\n"
                "{\n"
                "    return texture2D(tex, texcoord);\n"
                "}\n");
        }
        return *shader;
    }

    Layout layout() const override
    {
        return layout_;
    }

    void bind() override
    {
        glBindTexture(GL_TEXTURE_2D, tex);
        on_consumed();
        on_consumed = [](){};
    }

    void add_syncpoint() override
    {
    }
private:
    std::shared_ptr<mir::renderer::gl::Context> const ctx;
    GLuint const tex;

    std::function<void()> on_consumed;
    std::function<void()> const on_release;

    geom::Size const size_;
    Layout const layout_;
    EGLint const egl_format;

    std::shared_ptr<mir::Executor> const wayland_executor;
    std::mutex mutable content_lock;
};
}

void mga::GraphicBufferAllocator::bind_display(wl_display* display, std::shared_ptr<Executor> wayland_executor)
{
    // We need to set libhybris EGL platfrom to wayland here
    setenv("EGL_PLATFORM", "wayland", 1);

    auto context_guard = mir::raii::paired_calls(
      [this]() { ctx->make_current(); },
      [this]() { ctx->release_current(); });
    auto dpy = eglGetCurrentDisplay();

    if (dpy == EGL_NO_DISPLAY)
        BOOST_THROW_EXCEPTION((std::logic_error{"WaylandAllocator::bind_display called without an active EGL Display"}));

    if (!egl_extensions->wayland)
    {
        mir::log_warning("No EGL_WL_bind_wayland_display support");
        return;
    }

    if (egl_extensions->wayland->eglBindWaylandDisplayWL(dpy, display) == EGL_FALSE)
    {
        BOOST_THROW_EXCEPTION(mg::egl_error("Failed to bind Wayland EGL display"));
    }
    else
    {
        mir::log_info("Bound WaylandAllocator display");
    }

    this->wayland_executor = std::move(wayland_executor);
}

std::shared_ptr<mg::Buffer> mga::GraphicBufferAllocator::buffer_from_resource(
    wl_resource* buffer,
    std::function<void()>&& on_consumed,
    std::function<void()>&& on_release)
{
  // We also reset it here to make sure its always on wayland in the wayland
  // thread
  setenv("EGL_PLATFORM", "wayland", 1);

  auto context_guard = mir::raii::paired_calls(
      [this]() { ctx->make_current(); },
      [this]() { ctx->release_current(); });

  return std::make_shared<WaylandTexBuffer>(
      ctx,
      buffer,
      *egl_extensions,
      std::move(on_consumed),
      std::move(on_release),
      wayland_executor);
}
