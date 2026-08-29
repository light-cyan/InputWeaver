#include "platform/windows/support/unique_handle.hpp"
#include "platform/windows/tui/tui_ipc.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

struct Pipe final {
    inputweaver::win32::UniqueHandle read;
    inputweaver::win32::UniqueHandle write;
};

[[nodiscard]] Pipe MakePipe()
{
    HANDLE read{};
    HANDLE write{};
    Require(CreatePipe(&read, &write, nullptr, 0U) != FALSE, "create pipe");
    return {
        inputweaver::win32::UniqueHandle{read},
        inputweaver::win32::UniqueHandle{write}};
}

void TestCodecs()
{
    const inputweaver::ui::tui::KeyEvent source{
        inputweaver::ui::tui::Key::Character,
        U'\u4e2d',
        true};
    const std::vector<std::uint8_t> keyBytes =
        inputweaver::win32::EncodeKeyEvent(source);
    inputweaver::ui::tui::KeyEvent decoded{};
    Require(
        inputweaver::win32::DecodeKeyEvent(keyBytes, decoded),
        "decode key event");
    Require(
        decoded.key == source.key
            && decoded.character == source.character
            && decoded.shift == source.shift,
        "key event round trip");
    const std::vector<std::uint8_t> surrogateKeyBytes =
        inputweaver::win32::EncodeKeyEvent({
            inputweaver::ui::tui::Key::Character,
            static_cast<char32_t>(0xd800U),
            false});
    Require(
        !inputweaver::win32::DecodeKeyEvent(surrogateKeyBytes, decoded),
        "reject surrogate key event");

    const std::vector<std::uint8_t> sizeBytes =
        inputweaver::win32::EncodeViewportSize(160U, 48U);
    std::size_t width{};
    std::size_t height{};
    Require(
        inputweaver::win32::DecodeViewportSize(
            sizeBytes,
            width,
            height),
        "decode viewport size");
    Require(width == 160U && height == 48U, "viewport size round trip");

    const inputweaver::ui::tui::TextStyle baseStyle{
        {1U, 2U, 3U},
        {4U, 5U, 6U},
        true,
        true};
    inputweaver::ui::tui::Canvas canvas(2U, 1U, baseStyle);
    canvas.Put(0U, 0U, U'\u4e2d', baseStyle);
    const std::vector<std::uint8_t> frameBytes =
        inputweaver::win32::EncodeTuiFrame(canvas);
    inputweaver::win32::TuiFrame frame;
    Require(
        inputweaver::win32::DecodeTuiFrame(frameBytes, frame),
        "decode TUI frame");
    Require(
        frame.width == 2U && frame.height == 1U
            && frame.cells.size() == 2U,
        "TUI frame dimensions");
    Require(
        frame.cells[0].codePoint == U'\u4e2d'
            && frame.cells[0].style.foreground.red == 1U
            && frame.cells[0].style.background.blue == 6U
            && frame.cells[0].style.hasBackground
            && frame.cells[0].style.underline
            && frame.cells[1].continuation,
        "TUI frame cell round trip");
    std::vector<std::uint8_t> truncatedFrame = frameBytes;
    truncatedFrame.pop_back();
    Require(
        !inputweaver::win32::DecodeTuiFrame(truncatedFrame, frame),
        "reject truncated TUI frame");

    const std::uintptr_t sourceWindow = 0x12345678U;
    const std::vector<std::uint8_t> windowBytes =
        inputweaver::win32::EncodeWindowHandle(sourceWindow);
    std::uintptr_t decodedWindow{};
    Require(
        inputweaver::win32::DecodeWindowHandle(
            windowBytes,
            decodedWindow),
        "decode window handle");
    Require(decodedWindow == sourceWindow, "window handle round trip");
}

void TestChannel()
{
    Pipe leftToRight = MakePipe();
    Pipe rightToLeft = MakePipe();
    inputweaver::win32::TuiIpcChannel left{
        std::move(rightToLeft.read),
        std::move(leftToRight.write)};
    inputweaver::win32::TuiIpcChannel right{
        std::move(leftToRight.read),
        std::move(rightToLeft.write)};

    const inputweaver::ui::tui::KeyEvent source{
        inputweaver::ui::tui::Key::Escape,
        0U,
        false};
    const std::vector<std::uint8_t> payload =
        inputweaver::win32::EncodeKeyEvent(source);
    std::string error;
    Require(
        left.Send(
            inputweaver::win32::TuiIpcMessageType::Key,
            payload,
            error),
        "send channel message");

    std::vector<inputweaver::win32::TuiIpcMessage> messages;
    bool disconnected{};
    Require(
        right.Poll(messages, disconnected, error),
        "poll channel message");
    Require(!disconnected, "channel remains connected");
    Require(messages.size() == 1U, "receive one channel message");
    Require(
        messages[0].type == inputweaver::win32::TuiIpcMessageType::Key,
        "preserve channel message type");
    inputweaver::ui::tui::KeyEvent decoded{};
    Require(
        inputweaver::win32::DecodeKeyEvent(messages[0].payload, decoded),
        "decode channel key event");
    Require(decoded.key == inputweaver::ui::tui::Key::Escape, "channel key value");

    const std::span<const std::uint8_t> empty;
    Require(
        right.Send(
            inputweaver::win32::TuiIpcMessageType::Close,
            empty,
            error),
        "send reverse channel message");
    Require(
        left.Poll(messages, disconnected, error),
        "poll reverse channel message");
    Require(
        messages.size() == 1U
            && messages[0].type
                == inputweaver::win32::TuiIpcMessageType::Close,
        "receive reverse channel message");

    Require(
        left.Send(
            inputweaver::win32::TuiIpcMessageType::Heartbeat,
            empty,
            error),
        "send heartbeat message");
    Require(
        right.Poll(messages, disconnected, error),
        "poll heartbeat message");
    Require(
        messages.size() == 1U
            && messages[0].type
                == inputweaver::win32::TuiIpcMessageType::Heartbeat
            && messages[0].payload.empty(),
        "receive empty heartbeat message");
}

} // namespace

int main()
{
    TestCodecs();
    TestChannel();
    std::cout << "Windows TUI IPC tests passed.\n";
    return 0;
}
