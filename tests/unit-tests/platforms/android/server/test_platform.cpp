/*
 * Copyright © 2012 Canonical Ltd.
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
 * Authored by: Kevin DuBois <kevin.dubois@canonical.com>
 */

#include "mir/graphics/platform_ipc_operations.h"
#include "mir/options/program_option.h"
#include "mir/shared_library.h"
#include "mir/test/doubles/fd_matcher.h"
#include "mir/test/doubles/mock_android_hw.h"
#include "mir/test/doubles/mock_android_native_buffer.h"
#include "mir/test/doubles/mock_buffer.h"
#include "mir/test/doubles/mock_buffer_ipc_message.h"
#include "mir/test/doubles/mock_display_report.h"
#include "mir/test/doubles/mock_egl.h"
#include "mir/test/doubles/null_console_services.h"
#include "mir/test/doubles/null_gl_context.h"
#include "mir/test/doubles/stub_buffer_allocator.h"
#include "mir/test/doubles/stub_display_builder.h"
#include "mir/test/doubles/stub_display_report.h"
#include "mir/test/fake_shared.h"
#include "mir_test_framework/executable_path.h"
#include "mir_toolkit/mir_native_buffer.h"
#include "native_window_report.h"
#include "src/platforms/android/server/platform.h"

#include <boost/filesystem.hpp>

#include <dlfcn.h>
#include <gtest/gtest.h>
#include <libgen.h>
#include <system/window.h>

namespace mg = mir::graphics;
namespace mo = mir::options;
namespace mga = mir::graphics::android;
namespace mt = mir::test;
namespace mtd = mir::test::doubles;
namespace geom = mir::geometry;
namespace mtf = mir_test_framework;

static const char probe_platform[] = "probe_graphics_platform";

namespace
{
std::string executable_path()
{
    char buf[1024];
    auto tmp = readlink("/proc/self/exe", buf, sizeof buf);
    if (tmp < 0)
        BOOST_THROW_EXCEPTION(boost::enable_error_info(std::runtime_error("Failed to find our executable path"))
                              << boost::errinfo_errno(errno));
    if (tmp > static_cast<ssize_t>(sizeof(buf) - 1))
        BOOST_THROW_EXCEPTION(std::runtime_error("Path to executable is too long!"));
    buf[tmp] = '\0';
    return dirname(buf);
}

std::string server_platform(std::string const& name)
{
    std::string libname{name};

    if (libname.find(".so") == std::string::npos)
        libname += ".so." MIR_SERVER_GRAPHICS_PLATFORM_ABI_STRING;

    for (auto const& option :
         {executable_path() + "/../lib/server-modules/", executable_path() + "/../lib/server-platform/",
          std::string(MIR_SERVER_PLATFORM_PATH) + '/'})
    {
        std::cout << option << std::endl;
        auto path_to_test = option + libname;
        if (boost::filesystem::exists(path_to_test))
            return path_to_test;
    }

    BOOST_THROW_EXCEPTION(std::runtime_error("Failed to find server platform in standard search locations"));
}
}  // namespace

class PlatformBufferIPCPackaging : public ::testing::Test
{
protected:
    virtual void SetUp()
    {
        using namespace testing;

        stub_display_builder = std::make_shared<mtd::StubDisplayBuilder>();
        stub_display_report = std::make_shared<mtd::StubDisplayReport>();

        num_ints = 43;
        num_fds = 55;
        auto handle_size = sizeof(native_handle_t) + (sizeof(int) * (num_ints + num_fds));
        auto native_buffer_raw = (native_handle_t*)::operator new(handle_size);
        native_buffer_handle = std::shared_ptr<native_handle_t>(native_buffer_raw);

        native_buffer_handle->numInts = num_ints;
        native_buffer_handle->numFds = num_fds;
        for (auto i = 0u; i < (num_ints + num_fds); i++)
        {
            native_buffer_handle->data[i] = i;
        }

        native_buffer = std::make_shared<NiceMock<mtd::MockAndroidNativeBuffer>>();
        mock_buffer = std::make_shared<NiceMock<mtd::MockBuffer>>();

        anwb.stride = pixel_stride.as_int();
        ON_CALL(*native_buffer, handle()).WillByDefault(Return(native_buffer_handle.get()));
        ON_CALL(*native_buffer, anwb()).WillByDefault(Return(&anwb));
        ON_CALL(*mock_buffer, native_buffer_handle()).WillByDefault(Return(native_buffer));
        ON_CALL(*mock_buffer, stride()).WillByDefault(Return(byte_stride));
        ON_CALL(*mock_buffer, pixel_format()).WillByDefault(Return(format));

        quirks = std::make_shared<mga::DeviceQuirks>(mga::PropertiesOps{}, context);
    }

    ANativeWindowBuffer anwb;
    std::shared_ptr<mtd::MockAndroidNativeBuffer> native_buffer;
    std::shared_ptr<mtd::StubBufferAllocator> stub_buffer_allocator;
    std::shared_ptr<mtd::StubDisplayBuilder> stub_display_builder;
    std::shared_ptr<mtd::MockBuffer> mock_buffer;
    std::shared_ptr<native_handle_t> native_buffer_handle;
    std::shared_ptr<mg::DisplayReport> stub_display_report;
    std::shared_ptr<mga::NativeWindowReport> anw_report = std::make_shared<mga::NullNativeWindowReport>();
    testing::NiceMock<mtd::MockEGL> mock_egl;
    mtd::NullGLContext context;
    std::shared_ptr<mga::DeviceQuirks> quirks;
    MirPixelFormat const format = mir_pixel_format_abgr_8888;
    geom::Stride const pixel_stride{300};
    geom::Stride const byte_stride{300 * MIR_BYTES_PER_PIXEL(format)};
    unsigned int num_ints, num_fds;
};

/* ipc packaging tests */
TEST_F(PlatformBufferIPCPackaging, test_ipc_data_packed_correctly_for_full_ipc_with_fence)
{
    using namespace ::testing;
    int fake_fence{333};
    EXPECT_CALL(*native_buffer, wait_for_unlock_by_gpu());
    EXPECT_CALL(*native_buffer, copy_fence()).WillOnce(Return(fake_fence));

    mga::GrallocPlatform platform(stub_buffer_allocator);

    mtd::MockBufferIpcMessage mock_ipc_msg;
    int offset = 0;
    EXPECT_CALL(mock_ipc_msg, pack_flags(mir_buffer_flag_fenced));
    EXPECT_CALL(mock_ipc_msg, pack_fd(mtd::RawFdMatcher(fake_fence)));
    for (auto i = 0u; i < num_fds; i++)
        EXPECT_CALL(mock_ipc_msg, pack_fd(mtd::RawFdMatcher(native_buffer_handle->data[offset++])));
    for (auto i = 0u; i < num_ints; i++)
        EXPECT_CALL(mock_ipc_msg, pack_data(native_buffer_handle->data[offset++]));

#ifndef __clang__
    // FIXME: Why can't clang compile this on yakkety (with the
    //        gcc6 headers)? (LP: #1609612)
    EXPECT_CALL(mock_ipc_msg, pack_stride(byte_stride)).Times(1);
#endif

    EXPECT_CALL(*mock_buffer, size()).WillOnce(Return(mir::geometry::Size{123, 456}));
    EXPECT_CALL(mock_ipc_msg, pack_size(_)).Times(1);

    auto ipc_ops = platform.make_ipc_operations();
    ipc_ops->pack_buffer(mock_ipc_msg, *mock_buffer, mg::BufferIpcMsgType::full_msg);
}

TEST_F(PlatformBufferIPCPackaging, test_ipc_data_packed_correctly_for_full_ipc_without_fence)
{
    using namespace ::testing;
    EXPECT_CALL(*native_buffer, wait_for_unlock_by_gpu());
    EXPECT_CALL(*native_buffer, copy_fence()).WillOnce(Return(-1));

    mga::GrallocPlatform platform(stub_buffer_allocator);

    mtd::MockBufferIpcMessage mock_ipc_msg;
    int offset = 0;
    EXPECT_CALL(mock_ipc_msg, pack_flags(0));
    EXPECT_CALL(mock_ipc_msg, pack_fd(mtd::RawFdMatcher(-1))).Times(0);

    for (auto i = 0u; i < num_fds; i++)
    {
        EXPECT_CALL(mock_ipc_msg, pack_fd(mtd::RawFdMatcher(native_buffer_handle->data[offset++]))).Times(1);
    }
    for (auto i = 0u; i < num_ints; i++)
    {
        EXPECT_CALL(mock_ipc_msg, pack_data(native_buffer_handle->data[offset++])).Times(1);
    }

#ifndef __clang__
    // FIXME: Why can't clang compile this on yakkety (with the
    //        gcc6 headers)? (LP: #1609612)
    EXPECT_CALL(mock_ipc_msg, pack_stride(byte_stride)).Times(1);
#endif

    EXPECT_CALL(*mock_buffer, size()).WillOnce(Return(mir::geometry::Size{123, 456}));
    EXPECT_CALL(mock_ipc_msg, pack_size(_)).Times(1);

    auto ipc_ops = platform.make_ipc_operations();
    ipc_ops->pack_buffer(mock_ipc_msg, *mock_buffer, mg::BufferIpcMsgType::full_msg);
}

TEST_F(PlatformBufferIPCPackaging, test_ipc_data_packed_correctly_for_nested)
{
    using namespace ::testing;
    EXPECT_CALL(*native_buffer, wait_for_unlock_by_gpu());
    EXPECT_CALL(*native_buffer, copy_fence()).WillOnce(Return(-1));

    mga::GrallocPlatform platform(stub_buffer_allocator);

    mtd::MockBufferIpcMessage mock_ipc_msg;
    int offset = 0;
    for (auto i = 0u; i < num_fds; i++)
    {
        EXPECT_CALL(mock_ipc_msg, pack_fd(mtd::RawFdMatcher(native_buffer_handle->data[offset++]))).Times(1);
    }
    EXPECT_CALL(mock_ipc_msg, pack_flags(0));
    for (auto i = 0u; i < num_ints; i++)
    {
        EXPECT_CALL(mock_ipc_msg, pack_data(native_buffer_handle->data[offset++])).Times(1);
    }

#ifndef __clang__
    // FIXME: Why can't clang compile this on yakkety (with the
    //        gcc6 headers)? (LP: #1609612)
    EXPECT_CALL(mock_ipc_msg, pack_stride(byte_stride)).Times(1);
#endif

    EXPECT_CALL(*mock_buffer, size()).WillOnce(Return(mir::geometry::Size{123, 456}));
    EXPECT_CALL(mock_ipc_msg, pack_size(_)).Times(1);

    auto ipc_ops = platform.make_ipc_operations();
    ipc_ops->pack_buffer(mock_ipc_msg, *mock_buffer, mg::BufferIpcMsgType::full_msg);
}

TEST_F(PlatformBufferIPCPackaging, test_ipc_data_packed_correctly_for_partial_ipc)
{
    using namespace ::testing;

    int fake_fence{33};
    mga::GrallocPlatform platform(stub_buffer_allocator);
    auto ipc_ops = platform.make_ipc_operations();

    mtd::MockBufferIpcMessage mock_ipc_msg;

    Sequence seq;
    EXPECT_CALL(mock_ipc_msg, pack_flags(mir_buffer_flag_fenced)).InSequence(seq);
    EXPECT_CALL(mock_ipc_msg, pack_fd(mtd::RawFdMatcher(fake_fence))).InSequence(seq);
    EXPECT_CALL(mock_ipc_msg, pack_flags(0)).InSequence(seq);

    EXPECT_CALL(*native_buffer, copy_fence()).Times(2).WillOnce(Return(fake_fence)).WillOnce(Return(-1));

    ipc_ops->pack_buffer(mock_ipc_msg, *mock_buffer, mg::BufferIpcMsgType::update_msg);
    ipc_ops->pack_buffer(mock_ipc_msg, *mock_buffer, mg::BufferIpcMsgType::update_msg);
}

TEST(AndroidGraphicsPlatform, egl_native_display_is_egl_default_display)
{
    testing::NiceMock<mtd::MockEGL> mock_egl;
    mtd::NullGLContext context;
    mga::GrallocPlatform platform(std::make_shared<mtd::StubBufferAllocator>());
    EXPECT_EQ(EGL_DEFAULT_DISPLAY, platform.egl_native_display());
}

TEST(AndroidGraphicsPlatform, probe_returns_unsupported_when_no_hwaccess)
{
    using namespace testing;
    NiceMock<mtd::HardwareAccessMock> hwaccess;
    mo::ProgramOption options;

    ON_CALL(hwaccess, hw_get_module(_, _)).WillByDefault(Return(-1));

    mir::SharedLibrary platform_lib{server_platform("graphics-android")};
    auto probe = platform_lib.load_function<mg::PlatformProbe>(probe_platform);
    EXPECT_EQ(mg::PlatformPriority::unsupported, probe(std::make_shared<mtd::NullConsoleServices>(), options));
}

TEST(AndroidGraphicsPlatform, probe_returns_best_when_hwaccess_succeeds)
{
    testing::NiceMock<mtd::HardwareAccessMock> hwaccess;
    mir::options::ProgramOption options;

    mir::SharedLibrary platform_lib{server_platform("graphics-android")};
    auto probe = platform_lib.load_function<mg::PlatformProbe>(probe_platform);
    EXPECT_EQ(mg::PlatformPriority::best, probe(std::make_shared<mtd::NullConsoleServices>(), options));
}
