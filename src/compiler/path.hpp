#pragma once

#include <filesystem>
#include <string>

namespace inputweaver::compiler {

[[nodiscard]] inline std::string PathToUtf8(
    const std::filesystem::path& path)
{
    const std::u8string value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

} // namespace inputweaver::compiler
