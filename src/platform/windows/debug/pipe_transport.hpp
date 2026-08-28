#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "platform/windows/support/unique_handle.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace inputweaver::win32 {

enum class PipeIoResult : std::uint8_t {
    Succeeded,
    Cancelled,
    TimedOut,
    Failed,
};

template <typename Character>
[[nodiscard]] bool IsValidDebugToken(
    std::basic_string_view<Character> token) noexcept
{
    if (token.empty() || token.size() > 64U) {
        return false;
    }
    for (const Character character : token) {
        const bool digit = character >= Character{'0'}
            && character <= Character{'9'};
        const bool lower = character >= Character{'a'}
            && character <= Character{'z'};
        const bool upper = character >= Character{'A'}
            && character <= Character{'Z'};
        if (!digit && !lower && !upper
            && character != Character{'-'}
            && character != Character{'_'}
            && character != Character{'.'}) {
            return false;
        }
    }
    return true;
}

template <typename Character>
[[nodiscard]] std::wstring BuildDebugPipeName(
    std::uint32_t processId,
    std::basic_string_view<Character> token)
{
    std::wstring result = L"\\\\.\\pipe\\InputWeaver.Debug."
        + std::to_wstring(processId)
        + L".";
    result.reserve(result.size() + token.size());
    for (const Character character : token) {
        result.push_back(static_cast<wchar_t>(character));
    }
    return result;
}

namespace detail {

[[nodiscard]] inline PipeIoResult CompletePipeIo(
    HANDLE pipe,
    HANDLE cancelEvent,
    OVERLAPPED& overlapped,
    DWORD timeoutMilliseconds,
    DWORD& transferred) noexcept
{
    const HANDLE waits[] = {overlapped.hEvent, cancelEvent};
    const DWORD waitCount = cancelEvent == nullptr ? 1U : 2U;
    const DWORD wait = WaitForMultipleObjects(
        waitCount,
        waits,
        FALSE,
        timeoutMilliseconds);
    if (wait == WAIT_OBJECT_0) {
        return GetOverlappedResult(
            pipe,
            &overlapped,
            &transferred,
            FALSE) != FALSE
            ? PipeIoResult::Succeeded
            : PipeIoResult::Failed;
    }
    (void)CancelIoEx(pipe, &overlapped);
    (void)WaitForSingleObject(overlapped.hEvent, INFINITE);
    DWORD ignored{};
    (void)GetOverlappedResult(pipe, &overlapped, &ignored, FALSE);
    if (cancelEvent != nullptr && wait == WAIT_OBJECT_0 + 1U) {
        return PipeIoResult::Cancelled;
    }
    return wait == WAIT_TIMEOUT
        ? PipeIoResult::TimedOut
        : PipeIoResult::Failed;
}

template <typename Byte, typename StartOperation>
[[nodiscard]] PipeIoResult TransferPipeExact(
    HANDLE pipe,
    HANDLE cancelEvent,
    std::span<Byte> bytes,
    DWORD timeoutMilliseconds,
    StartOperation&& startOperation) noexcept
{
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (event == nullptr) {
            return PipeIoResult::Failed;
        }
        const UniqueHandle eventOwner(event);
        OVERLAPPED overlapped{};
        overlapped.hEvent = event;
        const DWORD requested = static_cast<DWORD>((std::min)(
            bytes.size() - offset,
            static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD transferred{};
        const BOOL started = std::forward<StartOperation>(startOperation)(
            bytes.data() + offset,
            requested,
            transferred,
            overlapped);
        PipeIoResult result = PipeIoResult::Succeeded;
        if (started == FALSE) {
            result = GetLastError() == ERROR_IO_PENDING
                ? CompletePipeIo(
                    pipe,
                    cancelEvent,
                    overlapped,
                    timeoutMilliseconds,
                    transferred)
                : PipeIoResult::Failed;
        }
        if (result != PipeIoResult::Succeeded || transferred == 0U) {
            return result == PipeIoResult::Succeeded
                ? PipeIoResult::Failed
                : result;
        }
        offset += transferred;
    }
    return PipeIoResult::Succeeded;
}

} // namespace detail

[[nodiscard]] inline PipeIoResult ReadPipeExact(
    HANDLE pipe,
    HANDLE cancelEvent,
    std::span<std::uint8_t> destination,
    DWORD timeoutMilliseconds = INFINITE) noexcept
{
    return detail::TransferPipeExact(
        pipe,
        cancelEvent,
        destination,
        timeoutMilliseconds,
        [pipe](void* data, DWORD size, DWORD& transferred, OVERLAPPED& operation) {
            return ReadFile(pipe, data, size, &transferred, &operation);
        });
}

[[nodiscard]] inline PipeIoResult WritePipeExact(
    HANDLE pipe,
    HANDLE cancelEvent,
    std::span<const std::uint8_t> source,
    DWORD timeoutMilliseconds = INFINITE) noexcept
{
    return detail::TransferPipeExact(
        pipe,
        cancelEvent,
        source,
        timeoutMilliseconds,
        [pipe](const void* data, DWORD size, DWORD& transferred, OVERLAPPED& operation) {
            return WriteFile(pipe, data, size, &transferred, &operation);
        });
}

} // namespace inputweaver::win32
