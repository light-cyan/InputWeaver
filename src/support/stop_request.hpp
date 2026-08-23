#pragma once

namespace inputweaver {

struct StopRequest final {
    void* context{};
    void (*invoke)(void*) noexcept{};

    void Request() const noexcept {
        if (invoke != nullptr) {
            invoke(context);
        }
    }
};

}  // namespace inputweaver
