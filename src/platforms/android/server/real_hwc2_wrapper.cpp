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

#include "mir/graphics/frame.h"
#include "real_hwc2_wrapper.h"
#include "hwc_layerlist.h"
#include "native_buffer.h"
#include "buffer.h"
#include "hwc_report.h"
#include "display_device_exceptions.h"
#include <boost/throw_exception.hpp>
#include <stdexcept>
#include <sstream>
#include <algorithm>
#include <sync/sync.h>

#define MIR_LOG_COMPONENT "android/server"
#include "mir/log.h"

namespace mg = mir::graphics;
namespace mga=mir::graphics::android;

namespace
{
static inline const char* getErrorName(hwc2_error_t error) {
    switch (error) {
        case HWC2_ERROR_NONE: return "None";
        case HWC2_ERROR_BAD_CONFIG: return "BadConfig";
        case HWC2_ERROR_BAD_DISPLAY: return "BadDisplay";
        case HWC2_ERROR_BAD_LAYER: return "BadLayer";
        case HWC2_ERROR_BAD_PARAMETER: return "BadParameter";
        case HWC2_ERROR_HAS_CHANGES: return "HasChanges";
        case HWC2_ERROR_NO_RESOURCES: return "NoResources";
        case HWC2_ERROR_NOT_VALIDATED: return "NotValidated";
        case HWC2_ERROR_UNSUPPORTED: return "Unsupported";
        default: return "Unknown";
    }
}

mga::DisplayName display_name(int raw_name)
{
    switch(raw_name)
    {
        default:
        case HWC_DISPLAY_PRIMARY:
            return mga::DisplayName::primary;
        case HWC_DISPLAY_EXTERNAL:
            return mga::DisplayName::external;
#ifdef ANDROID_CAF
        case HWC_DISPLAY_TERTIARY:
            return mga::DisplayName::tertiary;
#endif
        case HWC_DISPLAY_VIRTUAL:
            return mga::DisplayName::virt;
    }
}

//note: The destruction ordering of RealHwc2Wrapper should be enough to ensure that the
//callbacks are not called after the hwc module is closed. However, some badly synchronized
//drivers continue to call the hooks for a short period after we call close(). (LP: 1364637)
static std::mutex callback_lock;
static void refresh_hook(HWC2EventListener* listener, int32_t sequenceId, hwc2_display_t display)
{
    // mga::Hwc2Callbacks const* callbacks{nullptr};
    // std::unique_lock<std::mutex> lk(callback_lock);
    // if ((callbacks = reinterpret_cast<mga::Hwc2Callbacks const*>(listener)) && callbacks->self)
    //     callbacks->self->invalidate();
}

static void vsync_hook(HWC2EventListener* listener, int32_t sequenceId, hwc2_display_t display,
    int64_t timestamp)
{
    mga::Hwc2Callbacks const* callbacks{nullptr};
    std::unique_lock<std::mutex> lk(callback_lock);
    if ((callbacks = reinterpret_cast<mga::Hwc2Callbacks const*>(listener)) && callbacks->self)
    {
        // hwcomposer.h says the clock used is CLOCK_MONOTONIC, and testing
        // on various devices confirms this is the case...
        mg::Frame::Timestamp hwc_time{CLOCK_MONOTONIC,
                                      std::chrono::nanoseconds{timestamp}};
        callbacks->self->vsync(display_name(display), hwc_time);
    }
}

static void hotplug_hook(HWC2EventListener* listener, int32_t sequenceId,
    hwc2_display_t display, bool connected, bool primaryDisplay)
{
    mga::Hwc2Callbacks const* callbacks{nullptr};
    std::unique_lock<std::mutex> lk(callback_lock);

    mir::log_info("hotplug_hook(%d, %" PRIu64 ", %s, %s)",
           sequenceId, display,
           connected ? "connected" : "disconnected",
           primaryDisplay ? "primary" : "external");

    if ((callbacks = reinterpret_cast<mga::Hwc2Callbacks const*>(listener)) && callbacks->self)
        callbacks->self->hotplug(display, connected, primaryDisplay);
}
static mga::Hwc2Callbacks hwc_callbacks{{vsync_hook, hotplug_hook, refresh_hook}, nullptr, nullptr};

static mga::HWC2DisplayConfig_ptr get_active_config(
    hwc2_compat_device_t* hwc2_device,
    std::unordered_map<int, mga::hwc2_compat_display_ptr> const &hwc2_displays,
    mga::DisplayName display_name)
{
    auto &hwc2_display = hwc2_displays[as_hwc_display(display_name)];
    if (!hwc2_display) {
        std::stringstream ss;
        ss << "Attempted to get active configuration for unconnected display: " << as_hwc_display(display_name);
        BOOST_THROW_EXCEPTION(std::runtime_error(ss.str()));
    }

    return mga::HWC2DisplayConfig_ptr(
        hwc2_compat_display_get_active_config(hwc2_display.get()));
}

}

int mga::RealHwc2Wrapper::composerSequenceId = 0;

mga::RealHwc2Wrapper::RealHwc2Wrapper(
    std::shared_ptr<mga::HwcReport> const& report) :
    report(report)
{
    std::unique_lock<std::mutex> lk(callback_lock);

    hwc2_device = hwc2_compat_device_new(false);
    assert(hwc2_device);
    // hwc_device->registerProcs(hwc_device.get(), reinterpret_cast<hwc_procs_t*>(&hwc_callbacks));

    is_plugged[HWC_DISPLAY_PRIMARY].store(false);
    is_plugged[HWC_DISPLAY_EXTERNAL].store(false);
    is_plugged[HWC_DISPLAY_VIRTUAL].store(true);

    hwc_callbacks.self = this;
    hwc_callbacks.hwc2_device = hwc2_device;

    lk.unlock();
    hwc2_compat_device_register_callback(hwc2_device, reinterpret_cast<HWC2EventListener*>(&hwc_callbacks),
        mga::RealHwc2Wrapper::composerSequenceId++);
    lk.lock();
}

mga::RealHwc2Wrapper::~RealHwc2Wrapper()
{
    std::unique_lock<std::mutex> lk(callback_lock);
    hwc_callbacks.self = nullptr;
}

void mga::RealHwc2Wrapper::prepare(
    std::list<DisplayContents> const& contents) const
{
    //report->report_list_submitted_to_prepare(hwc1_displays);
    for (auto content : contents) {
        auto display_id = as_hwc_display(content.name);
        if (!active_displays[display_id])
            continue;

        if (display_id == HWC_DISPLAY_PRIMARY || display_id == HWC_DISPLAY_EXTERNAL) { // only primary and external for now
            auto it = display_contents.find(display_id);

            if (it == display_contents.end()) {
                it = display_contents.emplace(display_id, std::vector<hwc2_compat_layer_t*>{}).first;
            }

            auto hwc2_display = hwc2_displays[display_id].get();
            auto& layers = it->second;
            auto native_list = content.list.native_list();

            if (native_list->numHwLayers && layers.size() < 1) {
                auto layer = hwc2_compat_display_create_layer(hwc2_display);
                layers.push_back(layer);

                auto left = native_list->hwLayers[0].displayFrame.left;
                auto top = native_list->hwLayers[0].displayFrame.top;
                auto width = native_list->hwLayers[0].displayFrame.right;
                auto height = native_list->hwLayers[0].displayFrame.bottom;

                hwc2_compat_layer_set_composition_type(layer, HWC2_COMPOSITION_CLIENT);
                hwc2_compat_layer_set_blend_mode(layer, HWC2_BLEND_MODE_NONE);
                hwc2_compat_layer_set_source_crop(layer, left, top, width, height);
                hwc2_compat_layer_set_display_frame(layer, left, top, width, height);
                hwc2_compat_layer_set_visible_region(layer, left, top, width, height);

            }

            uint32_t num_requests = 0;
            uint32_t num_types = 0;

            auto error = hwc2_compat_display_validate(hwc2_display, &num_types, &num_requests);

            if (error != HWC2_ERROR_NONE && error != HWC2_ERROR_HAS_CHANGES) {
                std::stringstream ss;
                ss << "prepare: validate failed for display with id" << display_id << ": " << ::getErrorName(error);
                BOOST_THROW_EXCEPTION(std::runtime_error(ss.str()));
                return;
            }

            if (num_types || num_requests) {
                std::stringstream ss;
                ss << "prepare: validate required changes for display with id " << display_id << ": " << ::getErrorName(error);
                BOOST_THROW_EXCEPTION(std::runtime_error(ss.str()));
                return;
            }

            error = hwc2_compat_display_accept_changes(hwc2_display);
            if (error != HWC2_ERROR_NONE) {
                std::stringstream ss;
                ss << "prepare: acceptChanges failed: " << ::getErrorName(error);
                BOOST_THROW_EXCEPTION(std::runtime_error(ss.str()));
                return;
            }
        }
    }
   // report->report_prepare_done(hwc1_displays);
}

void mga::RealHwc2Wrapper::set(
    std::list<DisplayContents> const& contents) const
{
   // report->report_set_list(hwc1_displays);

    for (auto content : contents) {
        auto display_id = as_hwc_display(content.name);
        if (!active_displays[display_id])
            continue;

        if (display_id == HWC_DISPLAY_PRIMARY || display_id == HWC_DISPLAY_EXTERNAL) { // only primary and external for now
            auto hwc2_display = hwc2_displays[display_id].get();
            auto& fblayer = content.list.native_list()->hwLayers[1];
            bool sync_before_set = true;

            std::shared_ptr<mg::Buffer> buffer = nullptr;
            for (auto& it : content.list) {
                assert(buffer == nullptr); // There should be only a single layer with buffer
                buffer = it.layer.buffer();
            }

            if (buffer == nullptr) {
                mir::log_warning("Found no buffers for display %i", display_id, " continuing without");
                continue;
            }

            auto acquire_fence_fd = fblayer.acquireFenceFd;

            if (sync_before_set && acquire_fence_fd >= 0) {
                sync_wait(acquire_fence_fd, -1);
                close(acquire_fence_fd);
                acquire_fence_fd = -1;
            }

            auto native_buffer = mga::to_native_buffer_checked(buffer->native_buffer_handle());

            hwc2_compat_display_set_client_target(hwc2_display, /* slot */0, native_buffer->anwb(),
                                                acquire_fence_fd,
                                                HAL_DATASPACE_UNKNOWN);

            int presentFence = -1;

            // FIXME: find a propper fix
            // Validate here too as sometimes a display may not be validated at this point
            uint32_t num_requests = 0;
            uint32_t num_types = 0;
            hwc2_compat_display_validate(hwc2_display, &num_types, &num_requests);

            if (auto rc = hwc2_compat_display_present(hwc2_display, &presentFence)) {
                // Do not throw an exception in this case as it occasionaly fails on
                // some devices (found on lavender) but next try will likely succeed.
                mir::log_warning("set: error during hwc display present. rc = %s", getErrorName(rc));
            }
            fblayer.releaseFenceFd = presentFence;

            auto _last_present_fence = last_present_fence[display_id];
            if (_last_present_fence != -1) {
                sync_wait(_last_present_fence, -1);
                close(_last_present_fence);
            }

            last_present_fence[display_id] = presentFence != -1 ? dup(presentFence) : -1;
        }
    }

   // report->report_set_done(hwc1_displays);
}

void mga::RealHwc2Wrapper::vsync_signal_on(DisplayName display_name) const
{
    auto hwc2_display = hwc2_displays[as_hwc_display(display_name)].get();
    hwc2_compat_display_set_vsync_enabled(hwc2_display, HWC2_VSYNC_ENABLE);
    report->report_vsync_on();
}

void mga::RealHwc2Wrapper::vsync_signal_off(DisplayName display_name) const
{
    auto hwc2_display = hwc2_displays[as_hwc_display(display_name)].get();
    hwc2_compat_display_set_vsync_enabled(hwc2_display, HWC2_VSYNC_DISABLE);
    report->report_vsync_off();
}

void mga::RealHwc2Wrapper::display_on(DisplayName display_name) const
{
    BOOST_THROW_EXCEPTION(std::runtime_error("display_on() should not be used with HWC2 platform"));
}

void mga::RealHwc2Wrapper::display_off(DisplayName display_name) const
{
    BOOST_THROW_EXCEPTION(std::runtime_error("display_off() should not be used with HWC2 platform"));
    report->report_display_off();
}

void mga::RealHwc2Wrapper::subscribe_to_events(
        void const* subscriber,
        std::function<void(DisplayName, mg::Frame::Timestamp)> const& vsync,
        std::function<void(DisplayName, bool)> const& hotplug,
        std::function<void()> const& invalidate)
{
    std::unique_lock<std::mutex> lk(callback_map_lock);
    callback_map[subscriber] = {vsync, hotplug, invalidate};
}

void mga::RealHwc2Wrapper::unsubscribe_from_events(void const* subscriber) noexcept
{
    std::unique_lock<std::mutex> lk(callback_map_lock);
    auto it = callback_map.find(subscriber);
    if (it != callback_map.end())
        callback_map.erase(it);
}

void mga::RealHwc2Wrapper::vsync(DisplayName name, mg::Frame::Timestamp timestamp) noexcept
{
    std::unique_lock<std::mutex> lk(callback_map_lock);
    for(auto const& callbacks : callback_map)
    {
        try
        {
            callbacks.second.vsync(name, timestamp);
        }
        catch (...)
        {
        }
    }
}

void mga::RealHwc2Wrapper::hotplug(hwc2_display_t disp, bool connected, bool primaryDisplay) noexcept
{
    // Make sure to register the hotplug before triggering updates
    hwc2_compat_device_on_hotplug(hwc2_device, disp, connected);

    int display_id = primaryDisplay ? HWC_DISPLAY_PRIMARY : HWC_DISPLAY_EXTERNAL;

    if (auto new_display = hwc2_compat_device_get_display_by_id(hwc2_device, disp)) {

        if (connected) {
            mir::log_warning("hotplug: Adding display %" PRIu64 " with id %i", disp, display_id);

            // Check if this is the same display
            auto &oldDisplay = hwc2_displays[display_id];
            if (oldDisplay) {
                mir::log_warning("hotplug: We have an old display with this id, replacing this!");
            }

            hwc2_displays[display_id] = {hwc2_compat_display_ptr{new_display}};
            last_present_fence[display_id] = -1;
            active_displays[display_id] = true;
        } else {
            auto &oldDisplay = hwc2_displays[display_id];
            if (!oldDisplay) {
                mir::log_warning("hotplug: Could not find display to remove, ignoring");
            } else {
                mir::log_info("hotplug: Removing display %i", display_id);
                active_displays[display_id] = false;
            }

        }

        is_plugged[display_id].store(connected);

        auto name = display_name(disp);
        std::unique_lock<std::mutex> lk(callback_map_lock);
        for(auto const& callbacks : callback_map)
        {
            try
            {
                callbacks.second.hotplug(name, connected);
            }
            catch (...)
            {
            }
        }
    } else {
        mir::log_warning("hotplug: Something went wrong when trying to get display id!");
    }
}

void mga::RealHwc2Wrapper::invalidate() noexcept
{
    std::unique_lock<std::mutex> lk(callback_map_lock);
    for(auto const& callbacks : callback_map)
    {
        try
        {
            callbacks.second.invalidate();
        }
        catch (...)
        {
        }
    }
}

std::vector<mga::ConfigId> mga::RealHwc2Wrapper::display_configs(DisplayName display_name) const
{
    //Check first if display is unplugged, as some hw composers incorrectly report display configurations
    //when they have already triggered an unplug event.
    if (!is_plugged[mga::as_hwc_display(display_name)].load())
        return {};

    // //No way to get the number of display configs. SF uses 128 possible spots, but that seems excessive.
    // static size_t const max_configs = 16;
    // size_t num_configs = max_configs;
    // static uint32_t display_config[max_configs] = {};
    // if (hwc_device->getDisplayConfigs(hwc_device.get(), as_hwc_display(display_name), display_config, &num_configs))
    //     return {};

    // auto i = 0u;
    // std::vector<mga::ConfigId> config_ids{std::min(max_configs, num_configs)};
    // for(auto& id : config_ids)
    //     id = mga::ConfigId{display_config[i++]};
    // return config_ids;
    return {active_config_for(display_name)};
}

int mga::RealHwc2Wrapper::display_attributes(
    DisplayName display_name, ConfigId config_id, uint32_t const* attributes, int32_t* values) const
{
    auto config = get_active_config(hwc2_device, hwc2_displays, display_name);
    if (!config) {
        std::stringstream ss;
        ss << "No active configuration for display: " << as_hwc_display(display_name);
        BOOST_THROW_EXCEPTION(std::runtime_error(ss.str()));
    }

    for (int i = 0; attributes[i] != HWC_DISPLAY_NO_ATTRIBUTE; i++) {
        switch(attributes[i]) {
            case HWC_DISPLAY_WIDTH:
                values[i] = config->width;
                break;
            case HWC_DISPLAY_HEIGHT:
                values[i] = config->height;
                break;
            case HWC_DISPLAY_VSYNC_PERIOD:
                values[i] = config->vsyncPeriod;
                break;
            case HWC_DISPLAY_DPI_X:
                values[i] = config->dpiX;
                break;
            case HWC_DISPLAY_DPI_Y:
                values[i] = config->dpiY;
                break;
        }
    }
    return 0;
}

void mga::RealHwc2Wrapper::power_mode(DisplayName display_name, PowerMode mode) const
{
    if (display_name == mga::DisplayName::primary || display_name == mga::DisplayName::external) {
        auto hwc2_display = hwc2_displays[mga::as_hwc_display(display_name)].get();
        auto hwc2_mode = HWC2_POWER_MODE_ON;

        switch (mode) {
            case PowerMode::off:
                hwc2_mode = HWC2_POWER_MODE_OFF;
                break;
            case PowerMode::doze:
                hwc2_mode = HWC2_POWER_MODE_DOZE;
                break;
            case PowerMode::doze_suspend:
                hwc2_mode = HWC2_POWER_MODE_DOZE_SUSPEND;
                break;
        }

        if (auto error = hwc2_compat_display_set_power_mode(hwc2_display, hwc2_mode)) {
            mir::log_warning("error during hwc set(). rc = %d", static_cast<int>(error));
        }
    }
    report->report_power_mode(mode);
}

bool mga::RealHwc2Wrapper::has_active_config(DisplayName display_name) const
{
    auto config = get_active_config(hwc2_device, hwc2_displays, display_name);
    return config.get() != nullptr;
}

mga::ConfigId mga::RealHwc2Wrapper::active_config_for(DisplayName display_name) const
{
    auto config = get_active_config(hwc2_device, hwc2_displays, display_name);
    if (!config)
    {
        std::stringstream ss;
        ss << "No active configuration for display: " << as_hwc_display(display_name);
        BOOST_THROW_EXCEPTION(std::runtime_error(ss.str()));
    }
    return mga::ConfigId{static_cast<uint32_t>(config->id)};
}

void mga::RealHwc2Wrapper::set_active_config(DisplayName display_name, ConfigId id) const
{
    int rc = 0;
    //int rc = hwc_device->setActiveConfig(hwc_device.get(), as_hwc_display(display_name), id.as_value());
    if (rc < 0)
        BOOST_THROW_EXCEPTION(std::system_error(rc, std::system_category(), "unable to set active display config"));
}

bool mga::RealHwc2Wrapper::display_connected(DisplayName display_name) const
{
    size_t num_configs = 0;
    //return hwc_device->getDisplayConfigs(hwc_device.get(), as_hwc_display(display_name), nullptr, &num_configs) == 0;
    return true;
}
