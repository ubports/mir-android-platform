
/*
 * Copyright © 2015 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Thomas Guest <thomas.guest@canonical.com>
 *              Kevin DuBois <kevin.dubois@canonical.com>
 */

#include "mir/graphics/platform.h"
#include "mir/graphics/graphic_buffer_allocator.h"
#include "mir/graphics/buffer_properties.h"
#include "mir/graphics/platform_ipc_operations.h"
#include "mir/test/doubles/mock_egl.h"
#include "mir/test/doubles/mock_gl.h"
#include "mir/test/doubles/null_emergency_cleanup_registry.h"
#include "mir/test/doubles/stub_display_report.h"
#include "mir/test/doubles/null_logger.h"
#include "mir/test/doubles/null_console_services.h"
#include "mir/options/program_option.h"
#include "mir/test/doubles/mock_android_hw.h"

#include <gtest/gtest.h>

namespace mg = mir::graphics;
namespace geom = mir::geometry;
namespace mtd = mir::test::doubles;
namespace mo = mir::options;
namespace ml = mir::logging;

class GraphicsPlatform : public ::testing::Test
{
public:
    GraphicsPlatform() : logger(std::make_shared<mtd::NullLogger>())
    {
    }

    std::shared_ptr<mg::Platform> create_platform()
    {
      return create_host_platform(
          std::make_shared<mir::options::ProgramOption>(),
          std::make_shared<mtd::NullEmergencyCleanupRegistry>(),
          std::make_shared<mtd::NullConsoleServices>(),
          std::make_shared<mtd::StubDisplayReport>(),
          logger);
    }

    std::shared_ptr<ml::Logger> logger;

    ::testing::NiceMock<mtd::MockEGL> mock_egl;
    ::testing::NiceMock<mtd::MockGL> mock_gl;
    ::testing::NiceMock<mtd::HardwareAccessMock> hw_access_mock;
};

#include "../../test_graphics_platform.h"
