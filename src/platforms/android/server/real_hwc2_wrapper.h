/*
 * Copyright © 2014 Canonical Ltd.
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

#ifndef MIR_GRAPHICS_ANDROID_REAL_HWC2_WRAPPER_H_
#define MIR_GRAPHICS_ANDROID_REAL_HWC2_WRAPPER_H_

#include "hwc_wrapper.h"
#include "display_device.h"
#include <memory>
#include <hardware/hwcomposer.h>
#include <hybris/hwc2/hwc2_compatibility_layer.h>

#include <mutex>
#include <unordered_map>
#include <atomic>
#include <list>

namespace mir
{
namespace graphics
{
namespace android
{
class HwcReport;
class RealHwc2Wrapper;
struct Hwc2Callbacks
{
    HWC2EventListener listener;
    RealHwc2Wrapper* self;
    hwc2_compat_device_t* hwc2_device;
};

struct free_delete
{
    void operator()(void* x) { free(x); }
};

typedef std::unique_ptr<hwc2_compat_display_t, free_delete> hwc2_compat_display_ptr;
typedef std::unique_ptr<HWC2DisplayConfig, free_delete> HWC2DisplayConfig_ptr;

class RealHwc2Wrapper : public HwcWrapper
{
public:
    RealHwc2Wrapper(
        std::shared_ptr<HwcReport> const& report);
    ~RealHwc2Wrapper();

    void subscribe_to_events(
        void const* subscriber,
        std::function<void(DisplayName, graphics::Frame::Timestamp)> const& vsync_callback,
        std::function<void(DisplayName, bool)> const& hotplug_callback,
        std::function<void()> const& invalidate_callback) override;
    void unsubscribe_from_events(void const* subscriber) noexcept override;

    void prepare(std::list<DisplayContents> const& contents) const override;
    void set(std::list<DisplayContents> const& contents) const override;
    void vsync_signal_on(DisplayName) const override;
    void vsync_signal_off(DisplayName) const override;
    void display_on(DisplayName) const override;
    void display_off(DisplayName) const override;
    std::vector<ConfigId> display_configs(DisplayName) const override;
    int display_attributes(
        DisplayName, ConfigId, uint32_t const* attributes, int32_t* values) const override;
    void power_mode(DisplayName , PowerMode mode) const override;
    bool has_active_config(DisplayName name) const override;
    ConfigId active_config_for(DisplayName name) const override;
    void set_active_config(DisplayName name, ConfigId id) const override;

    void vsync(DisplayName, graphics::Frame::Timestamp) noexcept;
    void hotplug(hwc2_display_t, bool, bool) noexcept;
    void invalidate() noexcept;

    bool display_connected(DisplayName) const;

    static int composerSequenceId;
private:
    hwc2_compat_device_t* hwc2_device;
    std::unordered_map<int, hwc2_compat_display_ptr> hwc2_displays;
    std::shared_ptr<HwcReport> const report;
    std::mutex callback_map_lock;
    struct Callbacks
    {
        std::function<void(DisplayName, graphics::Frame::Timestamp)> vsync;
        std::function<void(DisplayName, bool)> hotplug;
        std::function<void()> invalidate;
    };
    std::unordered_map<void const*, Callbacks> callback_map;
    std::atomic<bool> is_plugged[HWC_NUM_DISPLAY_TYPES];
    std::unordered_map<int, std::vector<hwc2_compat_layer_t*>> display_contents;
    std::unordered_map<int, int> last_present_fence;
    std::unordered_map<int, bool> active_displays;
};

}
}
}
#endif /* MIR_GRAPHICS_ANDROID_REAL_HWC2_WRAPPER_H_ */
