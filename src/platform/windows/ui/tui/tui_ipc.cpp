#include "tui_ipc.hpp"

#include <algorithm>
#include <limits>
#include <system_error>
#include <utility>

namespace inputweaver::win32 {
namespace {

inline constexpr std::uint32_t kWireMagic = 0x31545749U;
inline constexpr std::size_t kHeaderBytes = 12U;
inline constexpr std::size_t kMaximumPayloadBytes = 16U * 1024U * 1024U;
inline constexpr std::size_t kFrameHeaderBytes = 8U;
inline constexpr std::size_t kFrameCellBytes = 11U;

void AppendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
}

void AppendU64(std::vector<std::uint8_t>& bytes, std::uint64_t value)
{
    AppendU32(bytes, static_cast<std::uint32_t>(value & 0xffffffffULL));
    AppendU32(bytes, static_cast<std::uint32_t>(value >> 32U));
}

[[nodiscard]] std::uint32_t ReadU32(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept
{
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] bool IsMessageType(std::uint32_t value) noexcept
{
    return value >= static_cast<std::uint32_t>(TuiIpcMessageType::Frame)
        && value <= static_cast<std::uint32_t>(
            TuiIpcMessageType::Heartbeat);
}

[[nodiscard]] bool IsDisconnectedError(DWORD error) noexcept
{
    return error == ERROR_BROKEN_PIPE
        || error == ERROR_NO_DATA
        || error == ERROR_PIPE_NOT_CONNECTED;
}

[[nodiscard]] std::string WindowsError(DWORD error)
{
    return std::system_category().message(static_cast<int>(error));
}

} // namespace

TuiIpcChannel::TuiIpcChannel(
    UniqueHandle readHandle,
    UniqueHandle writeHandle) noexcept
    : readHandle_(std::move(readHandle)),
      writeHandle_(std::move(writeHandle))
{
}

void TuiIpcChannel::Reset(
    UniqueHandle readHandle,
    UniqueHandle writeHandle) noexcept
{
    readHandle_ = std::move(readHandle);
    writeHandle_ = std::move(writeHandle);
    receiveBuffer_.clear();
}

bool TuiIpcChannel::Connected() const noexcept
{
    return static_cast<bool>(readHandle_)
        && static_cast<bool>(writeHandle_);
}

bool TuiIpcChannel::Send(
    TuiIpcMessageType type,
    std::span<const std::uint8_t> payload,
    std::string& error) noexcept
{
    try {
        if (!Connected() || payload.size() > kMaximumPayloadBytes) {
            error = "The TUI IPC message is not valid for this connection.";
            return false;
        }
        std::vector<std::uint8_t> bytes;
        bytes.reserve(kHeaderBytes + payload.size());
        AppendU32(bytes, kWireMagic);
        AppendU32(bytes, static_cast<std::uint32_t>(type));
        AppendU32(bytes, static_cast<std::uint32_t>(payload.size()));
        bytes.insert(bytes.end(), payload.begin(), payload.end());

        std::size_t offset{};
        while (offset < bytes.size()) {
            const std::size_t remaining = bytes.size() - offset;
            const DWORD requested = static_cast<DWORD>((std::min)(
                remaining,
                static_cast<std::size_t>(
                    (std::numeric_limits<DWORD>::max)())));
            DWORD written{};
            if (WriteFile(
                    writeHandle_.Get(),
                    bytes.data() + offset,
                    requested,
                    &written,
                    nullptr) == FALSE) {
                const DWORD nativeError = GetLastError();
                error = WindowsError(nativeError);
                return false;
            }
            if (written == 0U) {
                error = "The TUI IPC pipe cannot accept more data.";
                return false;
            }
            offset += written;
        }
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool TuiIpcChannel::Poll(
    std::vector<TuiIpcMessage>& messages,
    bool& disconnected,
    std::string& error) noexcept
{
    messages.clear();
    disconnected = false;
    try {
        if (!Connected()) {
            disconnected = true;
            return true;
        }
        for (;;) {
            DWORD available{};
            if (PeekNamedPipe(
                    readHandle_.Get(),
                    nullptr,
                    0U,
                    nullptr,
                    &available,
                    nullptr) == FALSE) {
                const DWORD nativeError = GetLastError();
                if (IsDisconnectedError(nativeError)) {
                    disconnected = true;
                    return true;
                }
                error = WindowsError(nativeError);
                return false;
            }
            if (available == 0U) {
                break;
            }
            const std::size_t oldSize = receiveBuffer_.size();
            receiveBuffer_.resize(oldSize + available);
            DWORD read{};
            if (ReadFile(
                    readHandle_.Get(),
                    receiveBuffer_.data() + oldSize,
                    available,
                    &read,
                    nullptr) == FALSE) {
                const DWORD nativeError = GetLastError();
                receiveBuffer_.resize(oldSize);
                if (IsDisconnectedError(nativeError)) {
                    disconnected = true;
                    return true;
                }
                error = WindowsError(nativeError);
                return false;
            }
            receiveBuffer_.resize(oldSize + read);
            if (read == 0U) {
                disconnected = true;
                return true;
            }
        }

        std::size_t offset{};
        while (receiveBuffer_.size() - offset >= kHeaderBytes) {
            const std::span<const std::uint8_t> remaining{
                receiveBuffer_.data() + offset,
                receiveBuffer_.size() - offset};
            const std::uint32_t magic = ReadU32(remaining, 0U);
            const std::uint32_t type = ReadU32(remaining, 4U);
            const std::size_t payloadBytes = ReadU32(remaining, 8U);
            if (magic != kWireMagic || !IsMessageType(type)
                || payloadBytes > kMaximumPayloadBytes) {
                error = "The TUI IPC stream contains an invalid message.";
                return false;
            }
            if (remaining.size() < kHeaderBytes + payloadBytes) {
                break;
            }
            TuiIpcMessage message{};
            message.type = static_cast<TuiIpcMessageType>(type);
            const auto payloadBegin = remaining.begin()
                + static_cast<std::ptrdiff_t>(kHeaderBytes);
            message.payload.assign(
                payloadBegin,
                payloadBegin + static_cast<std::ptrdiff_t>(payloadBytes));
            messages.push_back(std::move(message));
            offset += kHeaderBytes + payloadBytes;
        }
        if (offset != 0U) {
            receiveBuffer_.erase(
                receiveBuffer_.begin(),
                receiveBuffer_.begin()
                    + static_cast<std::ptrdiff_t>(offset));
        }
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

std::vector<std::uint8_t> EncodeKeyEvent(const ui::tui::KeyEvent& event)
{
    std::vector<std::uint8_t> payload;
    payload.reserve(16U);
    AppendU32(payload, static_cast<std::uint32_t>(event.key));
    AppendU32(payload, static_cast<std::uint32_t>(event.character));
    AppendU32(payload, event.shift ? 1U : 0U);
    AppendU32(payload, static_cast<std::uint32_t>(event.source));
    return payload;
}

bool DecodeKeyEvent(
    std::span<const std::uint8_t> payload,
    ui::tui::KeyEvent& event) noexcept
{
    if (payload.size() != 16U) {
        return false;
    }
    const std::uint32_t key = ReadU32(payload, 0U);
    const std::uint32_t character = ReadU32(payload, 4U);
    const std::uint32_t shift = ReadU32(payload, 8U);
    const std::uint32_t source = ReadU32(payload, 12U);
    if (key > static_cast<std::uint32_t>(ui::tui::Key::Redo)
        || character > 0x10ffffU
        || (character >= 0xd800U && character <= 0xdfffU)
        || shift > 1U
        || source > static_cast<std::uint32_t>(ui::tui::KeyEventSource::Drop)) {
        return false;
    }
    event.key = static_cast<ui::tui::Key>(key);
    event.character = static_cast<char32_t>(character);
    event.shift = shift != 0U;
    event.source = static_cast<ui::tui::KeyEventSource>(source);
    return true;
}

std::vector<std::uint8_t> EncodeViewportSize(
    std::size_t width,
    std::size_t height)
{
    std::vector<std::uint8_t> payload;
    payload.reserve(8U);
    AppendU32(payload, static_cast<std::uint32_t>(width));
    AppendU32(payload, static_cast<std::uint32_t>(height));
    return payload;
}

bool DecodeViewportSize(
    std::span<const std::uint8_t> payload,
    std::size_t& width,
    std::size_t& height) noexcept
{
    if (payload.size() != 8U) {
        return false;
    }
    const std::size_t decodedWidth = ReadU32(payload, 0U);
    const std::size_t decodedHeight = ReadU32(payload, 4U);
    if (decodedWidth == 0U || decodedHeight == 0U
        || decodedWidth > kMaximumTuiColumns
        || decodedHeight > kMaximumTuiRows) {
        return false;
    }
    width = decodedWidth;
    height = decodedHeight;
    return true;
}

std::vector<std::uint8_t> EncodeTuiFrame(const ui::tui::Canvas& canvas)
{
    const std::span<const ui::tui::Cell> cells = canvas.Cells();
    std::vector<std::uint8_t> payload;
    payload.reserve(kFrameHeaderBytes + cells.size() * kFrameCellBytes);
    AppendU32(payload, static_cast<std::uint32_t>(canvas.Width()));
    AppendU32(payload, static_cast<std::uint32_t>(canvas.Height()));
    for (const ui::tui::Cell& cell : cells) {
        AppendU32(payload, static_cast<std::uint32_t>(cell.codePoint));
        payload.push_back(cell.style.foreground.red);
        payload.push_back(cell.style.foreground.green);
        payload.push_back(cell.style.foreground.blue);
        payload.push_back(cell.style.background.red);
        payload.push_back(cell.style.background.green);
        payload.push_back(cell.style.background.blue);
        payload.push_back(static_cast<std::uint8_t>(
            (cell.style.hasBackground ? 1U : 0U)
            | (cell.style.underline ? 2U : 0U)
            | (cell.continuation ? 4U : 0U)));
    }
    return payload;
}

bool DecodeTuiFrame(
    std::span<const std::uint8_t> payload,
    TuiFrame& frame) noexcept
{
    try {
        if (payload.size() < kFrameHeaderBytes) {
            return false;
        }
        const std::size_t width = ReadU32(payload, 0U);
        const std::size_t height = ReadU32(payload, 4U);
        if (width == 0U || height == 0U
            || width > kMaximumTuiColumns
            || height > kMaximumTuiRows
            || width > (std::numeric_limits<std::size_t>::max)() / height) {
            return false;
        }
        const std::size_t cellCount = width * height;
        if (cellCount > ((std::numeric_limits<std::size_t>::max)()
                - kFrameHeaderBytes) / kFrameCellBytes
            || payload.size()
                != kFrameHeaderBytes + cellCount * kFrameCellBytes) {
            return false;
        }
        std::vector<ui::tui::Cell> cells;
        cells.reserve(cellCount);
        std::size_t offset = kFrameHeaderBytes;
        for (std::size_t index = 0U; index < cellCount; ++index) {
            const std::uint32_t codePoint = ReadU32(payload, offset);
            const std::uint8_t flags = payload[offset + 10U];
            if (codePoint > 0x10ffffU
                || (codePoint >= 0xd800U && codePoint <= 0xdfffU)
                || flags > 7U) {
                return false;
            }
            cells.push_back({
                static_cast<char32_t>(codePoint),
                {{payload[offset + 4U],
                  payload[offset + 5U],
                  payload[offset + 6U]},
                 {payload[offset + 7U],
                  payload[offset + 8U],
                  payload[offset + 9U]},
                 (flags & 1U) != 0U,
                 (flags & 2U) != 0U},
                (flags & 4U) != 0U});
            offset += kFrameCellBytes;
        }
        frame.width = width;
        frame.height = height;
        frame.cells = std::move(cells);
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<std::uint8_t> EncodeWindowHandle(std::uintptr_t window)
{
    std::vector<std::uint8_t> payload;
    payload.reserve(8U);
    AppendU64(payload, static_cast<std::uint64_t>(window));
    return payload;
}

bool DecodeWindowHandle(
    std::span<const std::uint8_t> payload,
    std::uintptr_t& window) noexcept
{
    if (payload.size() != 8U) {
        return false;
    }
    const std::uint64_t decoded = ReadU32(payload, 0U)
        | (static_cast<std::uint64_t>(ReadU32(payload, 4U)) << 32U);
    if (decoded == 0U
        || decoded > (std::numeric_limits<std::uintptr_t>::max)()) {
        return false;
    }
    window = static_cast<std::uintptr_t>(decoded);
    return true;
}

} // namespace inputweaver::win32
