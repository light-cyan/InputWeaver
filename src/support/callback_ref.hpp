#pragma once

#include <type_traits>
#include <utility>

namespace inputweaver::support {

template <typename Signature>
struct CallbackRef;

template <typename Result, typename... Arguments>
struct CallbackRef<Result(Arguments...) noexcept> final {
    static_assert(
        std::is_void_v<Result>
        || std::is_nothrow_default_constructible_v<Result>);

    void* context{};
    Result (*invoke)(void*, Arguments...) noexcept{};

    Result Invoke(Arguments... arguments) const noexcept
    {
        if (invoke != nullptr) {
            if constexpr (std::is_void_v<Result>) {
                invoke(context, std::forward<Arguments>(arguments)...);
                return;
            } else {
                return invoke(
                    context,
                    std::forward<Arguments>(arguments)...);
            }
        }
        if constexpr (!std::is_void_v<Result>) {
            return Result{};
        }
    }
};

} // namespace inputweaver::support
