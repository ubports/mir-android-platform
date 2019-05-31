/*
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
 */

#include "mir/graphics/egl_extensions.h"
#include "mir/graphics/egl_error.h"
#include "mir/graphics/program.h"
#include "mir/graphics/program_factory.h"
#include "native_buffer.h"
#include "sync_fence.h"
#include "android_format_conversion-inl.h"
#include "buffer.h"

#include <system/window.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <boost/throw_exception.hpp>
#include <stdexcept>

namespace mg=mir::graphics;
namespace mga=mir::graphics::android;
namespace geom=mir::geometry;

void mga::BindResolverTex::bind()
{
    tex_bind();
}

void mga::BindResolverTexTarget::bind()
{
    upload_to_texture();
}

mga::Buffer::Buffer(gralloc_module_t const* hw_module,
    std::shared_ptr<NativeBuffer> const& buffer_handle,
    std::shared_ptr<mg::EGLExtensions> const& extensions)
    : hw_module(hw_module),
      native_buffer(buffer_handle),
      egl_extensions(extensions)
{
}

mga::Buffer::~Buffer()
{
    for(auto& it : egl_image_map)
    {
        EGLDisplay disp = it.first.first;
        egl_extensions->eglDestroyImageKHR(disp, it.second);
    }
}

geom::Size mga::Buffer::size() const
{
    ANativeWindowBuffer *anwb = native_buffer->anwb();
    return {anwb->width, anwb->height};
}

geom::Stride mga::Buffer::stride() const
{
    ANativeWindowBuffer *anwb = native_buffer->anwb();
    return geom::Stride{anwb->stride *
                        MIR_BYTES_PER_PIXEL(pixel_format())};
}

MirPixelFormat mga::Buffer::pixel_format() const
{
    ANativeWindowBuffer *anwb = native_buffer->anwb();
    return mga::to_mir_format(anwb->format);
}

void mga::Buffer::gl_bind_to_texture()
{
    std::unique_lock<std::mutex> lk(content_lock);
    bind(lk);
    secure_for_render(lk);
}

void mga::Buffer::upload_to_texture()
{
    std::unique_lock<std::mutex> lk(content_lock);
    bind(lk);
}

void mga::Buffer::bind_for_write()
{
    upload_to_texture();
}

void mga::Buffer::bind(std::unique_lock<std::mutex> const&)
{
    native_buffer->ensure_available_for(mga::BufferAccess::read);

    DispContextPair current
    {
        eglGetCurrentDisplay(),
        eglGetCurrentContext()
    };

    if (current.first == EGL_NO_DISPLAY)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("cannot bind buffer to texture without EGL context"));
    }

    static const EGLint image_attrs[] =
    {
        EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
        EGL_NONE
    };

    EGLImageKHR image;
    auto it = egl_image_map.find(current);
    if (it == egl_image_map.end())
    {
        image = egl_extensions->eglCreateImageKHR(
                    current.first, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID,
                    native_buffer->anwb(), image_attrs);

        if (image == EGL_NO_IMAGE_KHR)
        {
            BOOST_THROW_EXCEPTION(mg::egl_error("error binding buffer to texture"));
        }
        egl_image_map[current] = image;
    }
    else /* already had it in map */
    {
        image = it->second;
    }

    egl_extensions->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
}

std::shared_ptr<mg::NativeBuffer> mga::Buffer::native_buffer_handle() const
{
    std::unique_lock<std::mutex> lk(content_lock);

    auto native_resource = std::shared_ptr<mg::NativeBuffer>(
        native_buffer.get(),
        [this](NativeBuffer*)
        {
            content_lock.unlock();
        });

    //lock remains in effect until the native handle is released
    lk.release();
    return native_resource;
}

void mga::Buffer::write(unsigned char const* data, size_t data_size)
{
    std::unique_lock<std::mutex> lk(content_lock);

    native_buffer->ensure_available_for(mga::BufferAccess::write);

    auto bpp = MIR_BYTES_PER_PIXEL(pixel_format());
    size_t buffer_size_bytes = size().height.as_int() * size().width.as_int() * bpp;
    if (buffer_size_bytes != data_size)
        BOOST_THROW_EXCEPTION(std::logic_error("Size of pixels is not equal to size of buffer"));

    char* vaddr{nullptr};
    int usage = GRALLOC_USAGE_SW_WRITE_OFTEN;
    int width = size().width.as_uint32_t();
    int height = size().height.as_uint32_t();
    int top = 0;
    int left = 0;
    if (hw_module->lock(
            hw_module, native_buffer->handle(), usage, top, left, width, height, reinterpret_cast<void**>(&vaddr)) ||
        !vaddr)
        BOOST_THROW_EXCEPTION(std::runtime_error("error securing buffer for client cpu use"));

    // Copy line by line in case of stride != width*bpp
    for (int i = 0; i < height; i++)
    {
        int line_offset_in_buffer = stride().as_uint32_t()*i;
        int line_offset_in_source = bpp*width*i;
        memcpy(vaddr + line_offset_in_buffer, data + line_offset_in_source, width * bpp);
    }

    hw_module->unlock(hw_module, native_buffer->handle());
}

void mga::Buffer::read(std::function<void(unsigned char const*)> const& do_with_data)
{
    std::unique_lock<std::mutex> lk(content_lock);

    native_buffer->ensure_available_for(mga::BufferAccess::read);
    auto buffer_size = size();

    unsigned char* vaddr{nullptr};
    int usage = GRALLOC_USAGE_SW_READ_OFTEN;
    int width = buffer_size.width.as_uint32_t();
    int height = buffer_size.height.as_uint32_t();

    int top = 0;
    int left = 0;
    if ((hw_module->lock(
        hw_module, native_buffer->handle(), usage, top, left, width, height, reinterpret_cast<void**>(&vaddr)) ) ||
        !vaddr)
        BOOST_THROW_EXCEPTION(std::runtime_error("error securing buffer for client cpu use"));

    do_with_data(vaddr);

    hw_module->unlock(hw_module, native_buffer->handle());
}

mg::NativeBufferBase* mga::Buffer::native_buffer_base()
{
    return this;
}

void mga::Buffer::secure_for_render()
{
    std::unique_lock<std::mutex> lk(content_lock);
    secure_for_render(lk);
}

void mga::Buffer::secure_for_render(std::unique_lock<std::mutex> const&)
{
    native_buffer->lock_for_gpu();
}

void mga::Buffer::commit()
{
    // post rendering step - only necessary when buffer is backed by user memory (c.f. to ShmBuffer)
}

mg::gl::Program const& mga::Buffer::shader(
    mg::gl::ProgramFactory& cache) const
{
    static auto const program = cache.compile_fragment_shader(
        "",
        "uniform sampler2D tex;\n"
        "vec4 sample_to_rgba(in vec2 texcoord)\n"
        "{\n"
        "    return texture2D(tex, texcoord);\n"
        "}\n");

    return *program;
}

mg::gl::Texture::Layout mga::Buffer::layout() const
{
    return Layout::GL;
}

void mga::Buffer::add_syncpoint()
{

}

void mga::Buffer::tex_bind()
{
    bool const needs_initialisation = tex_id == 0;
    if (needs_initialisation)
    {
        glGenTextures(1, &tex_id);
    }
    glBindTexture(GL_TEXTURE_2D, tex_id);
    if (needs_initialisation)
    {
        // The ShmBuffer *should* be immutable, so we can just upload once.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl_bind_to_texture();
    }
  }
