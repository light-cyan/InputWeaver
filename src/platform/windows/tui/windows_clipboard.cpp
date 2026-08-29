#include "windows_clipboard.hpp"

#include "platform/windows/support/text_encoding.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstring>
#include <system_error>

namespace inputweaver::win32 {

bool CopyTextToClipboard(
    std::string_view text,
    std::string& error) noexcept
{
    try {
        std::wstring wide;
        if (!Utf8ToWide(text, wide)) {
            error = "Cannot encode the selected text for the clipboard.";
            return false;
        }
        if (OpenClipboard(nullptr) == FALSE) {
            error = std::system_category().message(
                static_cast<int>(GetLastError()));
            return false;
        }
        if (EmptyClipboard() == FALSE) {
            error = std::system_category().message(
                static_cast<int>(GetLastError()));
            CloseClipboard();
            return false;
        }
        const SIZE_T bytes = (wide.size() + 1U) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        void* destination = memory == nullptr ? nullptr : GlobalLock(memory);
        if (destination == nullptr) {
            error = std::system_category().message(
                static_cast<int>(GetLastError()));
            if (memory != nullptr) {
                GlobalFree(memory);
            }
            CloseClipboard();
            return false;
        }
        std::memcpy(destination, wide.c_str(), bytes);
        GlobalUnlock(memory);
        if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
            error = std::system_category().message(
                static_cast<int>(GetLastError()));
            GlobalFree(memory);
            CloseClipboard();
            return false;
        }
        CloseClipboard();
        return true;
    } catch (...) {
        error = "Cannot copy the selected text to the clipboard.";
        return false;
    }
}

} // namespace inputweaver::win32
