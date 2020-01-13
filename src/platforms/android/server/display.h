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
 * Authored by: Kevin DuBois <kevin.dubois@canonical.com>
 */

#ifndef MIR_GRAPHICS_ANDROID_DISPLAY_H_
#define MIR_GRAPHICS_ANDROID_DISPLAY_H_

#include "mir/graphics/display.h"
#include "mir/graphics/frame.h"
#include "mir/graphics/atomic_frame.h"
#include "mir/renderer/gl/context_source.h"
#include "gl_context.h"
#include "display_group.h"
#include "hwc_configuration.h"
#include "display_configuration.h"
#include "overlay_optimization.h"

#include <memory>
#include <mutex>
#include <array>
#include <unordered_map>

namespace mir
{
namespace gl { class ProgramFactory; }
namespace graphics
{

class DisplayReport;
class GLConfig;

namespace android
{
class DisplayComponentFactory;
class DisplaySupportProvider;
class ConfigurableDisplayBuffer;
class DisplayChangePipe;
class DisplayDevice;
class NativeWindowReport;

class Display : public graphics::Display,
                public graphics::NativeDisplay,
                public renderer::gl::ContextSource
{
public:
    explicit Display(
        std::shared_ptr<DisplayComponentFactory> const& display_buffer_builder,
        std::shared_ptr<gl::ProgramFactory> const& gl_program_factory,
        std::shared_ptr<GLConfig> const& gl_config,
        std::shared_ptr<DisplayReport> const& display_report,
        std::shared_ptr<NativeWindowReport> const& native_window_report,
        OverlayOptimization overlay_option);
    ~Display() noexcept;

    void for_each_display_sync_group(std::function<void(graphics::DisplaySyncGroup&)> const& f) override;

    std::unique_ptr<graphics::DisplayConfiguration> configuration() const override;
    bool apply_if_configuration_preserves_display_buffers(graphics::DisplayConfiguration const& conf) override;
    void configure(graphics::DisplayConfiguration const&) override;

    void register_configuration_change_handler(
        EventHandlerRegister& handlers,
        DisplayConfigurationChangeHandler const& conf_change_handler) override;

    void register_pause_resume_handlers(
        EventHandlerRegister& handlers,
        DisplayPauseHandler const& pause_handler,
        DisplayResumeHandler const& resume_handler) override;

    void pause() override;
    void resume() override;

    std::shared_ptr<Cursor> create_hardware_cursor() override;
    std::unique_ptr<VirtualOutput> create_virtual_output(int width, int height) override;

    NativeDisplay* native_display() override;

    std::unique_ptr<renderer::gl::Context> create_gl_context() const override;

    Frame last_frame_on(unsigned output_id) const override;

private:
    void on_hotplug();
    void on_vsync(DisplayName, graphics::Frame::Timestamp);

    std::shared_ptr<DisplayReport> const display_report;
    std::shared_ptr<NativeWindowReport> const native_window_report;
    std::shared_ptr<DisplayComponentFactory> const display_buffer_builder;
    std::mutex mutable configuration_mutex;
    bool mutable configuration_dirty{false};
    std::unique_ptr<HwcConfiguration> const hwc_config;
    ConfigChangeSubscription const hotplug_subscription;
    DisplayConfiguration mutable config;
    DisplayOutputConnections old_outputs;
    PbufferGLContext gl_context;
    std::shared_ptr<DisplayDevice> display_device;
    std::unique_ptr<DisplayChangePipe> display_change_pipe;
    std::shared_ptr<gl::ProgramFactory> const gl_program_factory;
    DisplayGroup mutable displays;
    OverlayOptimization const overlay_option;

    void update_configuration(std::lock_guard<decltype(configuration_mutex)> const&) const;
    void configure_locked(
        graphics::DisplayConfiguration const& new_configuration,
        std::lock_guard<decltype(configuration_mutex)> const&);

    std::mutex mutable vsync_mutex;
    std::unordered_map<unsigned,AtomicFrame> last_frame;
};

}
}
}
#endif /* MIR_GRAPHICS_ANDROID_DISPLAY_H_ */
