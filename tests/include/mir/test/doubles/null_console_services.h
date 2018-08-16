/*
 * Copyright © 2018 UBports Community
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 2 or 3,
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
 * Authored by: Andreas Pokorny <andreas.pokorny@gmail.com>
 */

#ifndef MIR_TEST_DOUBLES_NULL_CONSOLE_SERVICES_H_
#define MIR_TEST_DOUBLES_NULL_CONSOLE_SERVICES_H_

#include "mir/console_services.h"

namespace mir
{
namespace test
{
namespace doubles
{
struct NullConsoleServices : ConsoleServices
{
public:
    void register_switch_handlers(graphics::EventHandlerRegister& /*handlers*/,
                                  std::function<bool()> const& /*switch_away*/,
                                  std::function<bool()> const& /*switch_back*/) override
    {
    }
    void restore() override
    {
    }
    std::future<std::unique_ptr<Device>> acquire_device(int /*major*/,
                                                        int /*minor*/,
                                                        std::unique_ptr<Device::Observer> /*observer*/) override
    {
        return {}; //std::future<std::unique_ptr<Device>>
    }

    NullConsoleServices() = default;
};

}  // namespace doubles
}  // namespace test
}  // namespace mir

#endif
