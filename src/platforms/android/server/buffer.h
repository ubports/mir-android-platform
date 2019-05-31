/*
 * Copyright © 2012,2013 Canonical Ltd.
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

#ifndef MIR_GRAPHICS_ANDROID_BUFFER_H_
#define MIR_GRAPHICS_ANDROID_BUFFER_H_

#include "mir/graphics/buffer_basic.h"
#include "mir/renderer/gl/texture_source.h"
#include "mir/renderer/gl/texture_target.h"
#include "mir/renderer/sw/pixel_source.h"
#include "mir/graphics/texture.h"

#include <hardware/gralloc.h>

#include <mutex>
#include <condition_variable>
#include <map>

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

namespace mir
{
namespace graphics
{
struct EGLExtensions;
namespace android
{
/*
 * renderer::gl::TextureSource and graphics::gl::Texture both have
 * a bind() method. They need to do different things.
 *
 * Because we can't just override them based on their signature,
 * do the intermediate-base-class trick of having two proxy bases
 * which do nothing but rename bind() to something unique.
 */

class BindResolverTex : public gl::Texture
{
public:
    BindResolverTex() = default;

    void bind() override final;

protected:
    virtual void tex_bind() = 0;
};

class BindResolverTexTarget : public renderer::gl::TextureSource
{
public:
    BindResolverTexTarget() = default;

    void bind() override final;

protected:
    virtual void upload_to_texture() = 0;
};

class NativeBuffer;
class Buffer: public BufferBasic, public NativeBufferBase,
              public BindResolverTexTarget,
              public renderer::gl::TextureTarget,
              public BindResolverTex,
              public renderer::software::PixelSource
{
public:
    Buffer(gralloc_module_t const* hw_module,
           std::shared_ptr<android::NativeBuffer> const& buffer_handle,
           std::shared_ptr<EGLExtensions> const& extensions);
    ~Buffer();

    geometry::Size size() const override;
    geometry::Stride stride() const override;
    MirPixelFormat pixel_format() const override;
    void gl_bind_to_texture() override;
    void upload_to_texture() override;
    void secure_for_render() override;

    void bind_for_write() override;
    void commit() override;

    //note, you will get the native representation of an android buffer, including
    //the fences associated with the buffer. You must close these fences
    std::shared_ptr<graphics::NativeBuffer> native_buffer_handle() const override;

    void write(unsigned char const* pixels, size_t size) override;
    void read(std::function<void(unsigned char const*)> const&) override;

    NativeBufferBase* native_buffer_base() override;

    gl::Program const& shader(gl::ProgramFactory& cache) const override;
    Layout layout() const override;
    void add_syncpoint() override;

protected:
    void tex_bind() override;

private:
    void bind(std::unique_lock<std::mutex> const&);
    void secure_for_render(std::unique_lock<std::mutex> const&);
    gralloc_module_t const* hw_module;

    typedef std::pair<EGLDisplay, EGLContext> DispContextPair;
    std::map<DispContextPair,EGLImageKHR> egl_image_map;

    std::mutex mutable content_lock;
    std::shared_ptr<android::NativeBuffer> native_buffer;
    std::shared_ptr<EGLExtensions> egl_extensions;
    GLuint tex_id{0};
};

}
}
}

#endif /* MIR_GRAPHICS_ANDROID_BUFFER_H_ */
