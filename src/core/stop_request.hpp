#pragma once

namespace ukr {

struct StopRequest final {
    void* context{};
    void (*invoke)(void*) noexcept{};

    void Request() const noexcept {
        if (invoke != nullptr) {
            invoke(context);
        }
    }
};

}  // namespace ukr
