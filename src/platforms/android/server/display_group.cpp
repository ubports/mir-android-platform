/*
 * Copyright © 2015 Canonical Ltd.
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

#include "display_group.h"
#include "configurable_display_buffer.h"
#include "display_device_exceptions.h"
#include <boost/throw_exception.hpp>
#include <stdexcept>

#define MIR_LOG_COMPONENT "android/server"
#include "mir/log.h"

#define MAX_CONSECUTIVE_COMMIT_FAILURE 3

namespace mg = mir::graphics;
namespace mga = mir::graphics::android;
namespace geom = mir::geometry;

mga::DisplayGroup::DisplayGroup(
    std::shared_ptr<mga::DisplayDevice> const& device,
    std::unique_ptr<mga::ConfigurableDisplayBuffer> primary_buffer,
    ExceptionHandler const& exception_handler) :
    device(device),
    exception_handler(exception_handler),
    commit_failure_count(0)
{
    dbs.emplace(std::make_pair(mga::DisplayName::primary, std::move(primary_buffer)));
}

mga::DisplayGroup::DisplayGroup(
    std::shared_ptr<mga::DisplayDevice> const& device,
    std::unique_ptr<mga::ConfigurableDisplayBuffer> primary_buffer)
    : DisplayGroup(device, std::move(primary_buffer), []{})
{
}

void mga::DisplayGroup::for_each_display_buffer(std::function<void(mg::DisplayBuffer&)> const& f)
{
    std::unique_lock<decltype(guard)> lk(guard);
    for(auto const& db : dbs)
        if (db.second->power_mode() != mir_power_mode_off)
            f(*db.second);
}

void mga::DisplayGroup::add(DisplayName name, std::unique_ptr<ConfigurableDisplayBuffer> buffer)
{
    std::unique_lock<decltype(guard)> lk(guard);
    dbs.emplace(std::make_pair(name, std::move(buffer)));
}

void mga::DisplayGroup::remove(DisplayName name)
{
    if (name == mga::DisplayName::primary)
        BOOST_THROW_EXCEPTION(std::logic_error("cannot remove primary display"));

    std::unique_lock<decltype(guard)> lk(guard);
    auto it = dbs.find(name);
    if (it != dbs.end())
        dbs.erase(it);
}

bool mga::DisplayGroup::display_present(DisplayName name) const
{
    std::unique_lock<decltype(guard)> lk(guard);
    return (dbs.end() != dbs.find(name));
}

void mga::DisplayGroup::configure(
    DisplayName name, MirPowerMode mode, glm::mat2 const& transform, geom::Rectangle const& view_area)
{
    std::unique_lock<decltype(guard)> lk(guard);
    auto it = dbs.find(name);
    if (it != dbs.end())
        it->second->configure(mode, transform, view_area);
}

void mga::DisplayGroup::post()
{
    std::list<DisplayContents> contents;
    {
        std::unique_lock<decltype(guard)> lk(guard);
        for(auto const& db : dbs)
            contents.emplace_back(db.second->contents());
    }

    try
    {
        device->commit(contents);
        commit_failure_count = 0;
    }
    catch (mga::DisplayDisconnectedException const&)
    {
        //Ignore disconnect errors as they are not fatal
        commit_failure_count = 0;
    }
    catch (std::runtime_error const& e)
    {
        // Falure to commit() can be transient. We allow commit() to fail
        // 3 times consecutively before declaring it fatal.
        commit_failure_count++;
        if (commit_failure_count > MAX_CONSECUTIVE_COMMIT_FAILURE) {
            mir::log_error("Commiting has failed %d times consecutively.", commit_failure_count);
            BOOST_THROW_EXCEPTION(e);
        } else {
            mir::log_warning("Commiting has failed %d time(s) consecutively.", commit_failure_count);
            mir::log_warning("The lastest error is: %s", e.what());

            // We allow Display to inject an error handler (which can then attempt to recover
            // from this error) to try to prevent the error from happening in the future.
            exception_handler();
        }
    }

}

std::chrono::milliseconds mga::DisplayGroup::recommended_sleep() const
{
    return device->recommended_sleep();
}
