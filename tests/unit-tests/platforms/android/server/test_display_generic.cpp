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
 * Authored by: Alexandros Frantzis <alexandros.frantzis@canonical.com>
 */

#include "mir/graphics/display.h"
#include "mir/graphics/display_configuration.h"
#include "mir/graphics/platform.h"

#include "mir/test/doubles/mock_egl.h"
#include "mir/test/doubles/mock_gl.h"
#include "mir/test/doubles/stub_gl_config.h"
#include "mir/test/doubles/null_emergency_cleanup_registry.h"
#include "mir/graphics/default_display_configuration_policy.h"
#include "mir/test/doubles/mock_android_hw.h"
#include "mir/test/doubles/mock_display_device.h"
#include "mir/test/doubles/null_logger.h"
#include "mir/test/doubles/stub_display_report.h"
#include "mir/test/doubles/mock_option.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace mg = mir::graphics;
namespace mtd = mir::test::doubles;

class DisplayTestGeneric : public ::testing::Test
{
public:
    DisplayTestGeneric() :
        logger{std::make_shared<mtd::NullLogger>()},
        platform{
            create_host_platform(
                std::make_shared<mtd::MockOption>(),
                std::make_shared<mtd::NullEmergencyCleanupRegistry>(),
                std::make_shared<mtd::StubDisplayReport>(),
                logger)
        }

    {
        using namespace testing;

        ON_CALL(mock_egl, eglChooseConfig(_,_,_,1,_))
            .WillByDefault(DoAll(SetArgPointee<2>(mock_egl.fake_configs[0]),
                                 SetArgPointee<4>(1),
                                 Return(EGL_TRUE)));

        mock_egl.provide_egl_extensions();
        mock_gl.provide_gles_extensions();
    }

    std::shared_ptr<mg::Display> create_display()
    {
        return platform->create_display(
            std::make_shared<mg::CloneDisplayConfigurationPolicy>(),
            std::make_shared<mtd::StubGLConfig>());
    }

    ::testing::NiceMock<mtd::MockEGL> mock_egl;
    ::testing::NiceMock<mtd::MockGL> mock_gl;
    ::testing::NiceMock<mtd::HardwareAccessMock> hw_access_mock;
    std::shared_ptr<mir::logging::Logger> const logger;
    mir::UniqueModulePtr<mg::Platform> platform;
};

#include "../../test_display.h"
