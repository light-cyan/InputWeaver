#pragma once

#include "debug/debug_client.hpp"

#include <memory>

namespace inputweaver::win32 {

class WindowsDebugClient final : public debug::DebugClient {
public:
    explicit WindowsDebugClient(debug::DebugClientCapacities capacities = {});
    ~WindowsDebugClient() override;

    WindowsDebugClient(const WindowsDebugClient&) = delete;
    WindowsDebugClient& operator=(const WindowsDebugClient&) = delete;

    [[nodiscard]] debug::DebugClientResult Connect(
        debug::ProcessIdentity process,
        std::string_view debugToken) override;
    [[nodiscard]] debug::DebugClientResult StartCapture() override;
    [[nodiscard]] debug::DebugClientResult StopCapture() override;
    [[nodiscard]] debug::DebugClientResult RequestExecutorStop() override;
    [[nodiscard]] std::shared_ptr<const debug::DebugClientState> ReadState()
        const override;
    void Disconnect() noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace inputweaver::win32
