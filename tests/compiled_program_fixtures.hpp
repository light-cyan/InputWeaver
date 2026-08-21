#pragma once

#include "core/compiled_program.hpp"

namespace inputweaver::test {

[[nodiscard]] CompiledProgramStorage MakeTapFixtureStorage();
[[nodiscard]] CompiledProgramStorage MakeMappingFixtureStorage();
[[nodiscard]] CompiledProgramStorage MakeConditionalRepeatFixtureStorage();
[[nodiscard]] CompiledProgramStorage MakePauseControlFixtureStorage();

} // namespace inputweaver::test
