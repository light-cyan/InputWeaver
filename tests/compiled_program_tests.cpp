#include "compiled_program_fixtures.hpp"

#include "core/program_dump.hpp"
#include "core/program_validator.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

int g_failureCount = 0;

void Check(bool condition, std::string_view name)
{
    if (!condition) {
        ++g_failureCount;
        std::cerr << "FAIL: " << name << '\n';
    }
}

[[nodiscard]] bool HasError(
    const std::vector<inputweaver::ProgramValidationError>& errors,
    inputweaver::ProgramValidationErrorCode code)
{
    for (const auto& error : errors) {
        if (error.code == code) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::uint64_t Fnv1a64(std::string_view text) noexcept
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char rawByte : text) {
        const auto byte = static_cast<unsigned char>(rawByte);
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] std::shared_ptr<const inputweaver::CompiledProgram> FinalizeFixture(
    inputweaver::CompiledProgramStorage storage,
    std::string_view name)
{
    inputweaver::FinalizeResult result = inputweaver::FinalizeCompiledProgram(
        std::move(storage));
    if (!result.errors.empty()) {
        std::cerr << "Fixture failed: " << name << '\n';
        for (const auto& error : result.errors) {
            std::cerr << "  " << error.location << ": " << error.message << '\n';
        }
    }
    Check(result.errors.empty(), std::string(name) + " validates");
    Check(result.program != nullptr, std::string(name) + " finalizes");
    return result.program;
}

[[nodiscard]] inputweaver::CompiledProgramStorage MakeOpcodeCoverageStorage()
{
    using namespace inputweaver;
    CompiledProgramStorage storage = test::MakeTapFixtureStorage();
    const SourceSpan span{0U, storage.source.byteLength};
    storage.strings.push_back("state-value");
    storage.strings.push_back("number-value");
    storage.strings.push_back("duration-value");
    storage.strings.push_back("fixture-command");
    storage.userValues.initialStates = {1U};
    storage.userValues.initialNumbers = {3.0};
    storage.userValues.initialDurations = {{5'000'000}};
    storage.valueRefs = {
        {ValueDomain::UserState, ValueType::State, 0U},
        {ValueDomain::UserNumber, ValueType::Number, 0U},
        {ValueDomain::UserDuration, ValueType::Duration, 0U},
        {ValueDomain::BuiltinState, ValueType::State,
            static_cast<std::uint32_t>(BuiltinState::Pause)},
        {ValueDomain::BuiltinDuration, ValueType::Duration,
            static_cast<std::uint32_t>(BuiltinDuration::TapDuration)},
        {ValueDomain::BuiltinDuration, ValueType::Duration,
            static_cast<std::uint32_t>(BuiltinDuration::ActionGap)},
    };
    storage.debugInfo.variables = {
        {StringId{1U}, ValueRefId{0U}, {1U, 1U}},
        {StringId{2U}, ValueRefId{1U}, {2U, 1U}},
        {StringId{3U}, ValueRefId{2U}, {3U, 1U}},
    };
    storage.numberConstants = {2.0};
    storage.durationConstants = {{7'000'000}};
    storage.expressions.clear();
    storage.expressionCode.clear();

    const auto appendExpression = [&storage, span](
        ExpressionType resultType,
        std::uint32_t maximumStackDepth,
        std::initializer_list<ExpressionInstruction> code) {
        const std::uint32_t begin = static_cast<std::uint32_t>(
            storage.expressionCode.size());
        storage.expressionCode.insert(
            storage.expressionCode.end(),
            code.begin(),
            code.end());
        storage.expressions.push_back({
            {begin, static_cast<std::uint32_t>(code.size())},
            resultType,
            maximumStackDepth,
            span});
        return ExpressionId{
            static_cast<std::uint32_t>(storage.expressions.size() - 1U)};
    };
    const auto binary = [](BinaryOperator operation) {
        return static_cast<std::uint32_t>(operation);
    };
    const auto unary = [](UnaryOperator operation) {
        return static_cast<std::uint32_t>(operation);
    };

    const ExpressionId booleanBranch = appendExpression(
        ExpressionType::Boolean,
        1U,
        {
            {ExpressionOpcode::PushBoolean, ExpressionType::Boolean, 1U, 0U},
            {ExpressionOpcode::JumpIfFalse, ExpressionType::None, 4U, 0U},
            {ExpressionOpcode::PushBoolean, ExpressionType::Boolean, 1U, 0U},
            {ExpressionOpcode::Jump, ExpressionType::None, 5U, 0U},
            {ExpressionOpcode::PushBoolean, ExpressionType::Boolean, 0U, 0U},
            {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
        });
    static_cast<void>(appendExpression(
        ExpressionType::Boolean,
        1U,
        {
            {ExpressionOpcode::PushBoolean, ExpressionType::Boolean, 0U, 0U},
            {ExpressionOpcode::JumpIfTrue, ExpressionType::None, 4U, 0U},
            {ExpressionOpcode::PushBoolean, ExpressionType::Boolean, 0U, 0U},
            {ExpressionOpcode::Jump, ExpressionType::None, 5U, 0U},
            {ExpressionOpcode::PushBoolean, ExpressionType::Boolean, 1U, 0U},
            {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
        }));
    static_cast<void>(appendExpression(
        ExpressionType::Boolean,
        1U,
        {
            {ExpressionOpcode::ReadControlHeld, ExpressionType::Boolean, 1U, 0U},
            {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
        }));
    const ExpressionId stateValue = appendExpression(
        ExpressionType::State,
        1U,
        {
            {ExpressionOpcode::LoadValue, ExpressionType::State, 0U, 0U},
            {ExpressionOpcode::Return, ExpressionType::State, 0U, 0U},
        });
    const ExpressionId numberValue = appendExpression(
        ExpressionType::Number,
        2U,
        {
            {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
            {ExpressionOpcode::Unary, ExpressionType::Number,
                unary(UnaryOperator::NumberIdentity), 0U},
            {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
            {ExpressionOpcode::Binary, ExpressionType::Number,
                binary(BinaryOperator::NumberAdd), 0U},
            {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
            {ExpressionOpcode::Binary, ExpressionType::Number,
                binary(BinaryOperator::NumberSubtract), 0U},
            {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
            {ExpressionOpcode::Binary, ExpressionType::Number,
                binary(BinaryOperator::NumberMultiply), 0U},
            {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
            {ExpressionOpcode::Binary, ExpressionType::Number,
                binary(BinaryOperator::NumberDivide), 0U},
            {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
            {ExpressionOpcode::Binary, ExpressionType::Number,
                binary(BinaryOperator::NumberModulo), 0U},
            {ExpressionOpcode::Unary, ExpressionType::Number,
                unary(UnaryOperator::NumberNegate), 0U},
            {ExpressionOpcode::Return, ExpressionType::Number, 0U, 0U},
        });
    const ExpressionId durationValue = appendExpression(
        ExpressionType::Duration,
        2U,
        {
            {ExpressionOpcode::PushDuration, ExpressionType::Duration, 0U, 0U},
            {ExpressionOpcode::PushDuration, ExpressionType::Duration, 0U, 0U},
            {ExpressionOpcode::Binary, ExpressionType::Duration,
                binary(BinaryOperator::DurationAdd), 0U},
            {ExpressionOpcode::PushDuration, ExpressionType::Duration, 0U, 0U},
            {ExpressionOpcode::Binary, ExpressionType::Duration,
                binary(BinaryOperator::DurationSubtract), 0U},
            {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
            {ExpressionOpcode::Binary, ExpressionType::Duration,
                binary(BinaryOperator::DurationMultiplyNumber), 0U},
            {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
            {ExpressionOpcode::Binary, ExpressionType::Duration,
                binary(BinaryOperator::DurationDivideNumber), 0U},
            {ExpressionOpcode::Return, ExpressionType::Duration, 0U, 0U},
        });
    static_cast<void>(appendExpression(
        ExpressionType::Duration,
        2U,
        {
            {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
            {ExpressionOpcode::PushDuration, ExpressionType::Duration, 0U, 0U},
            {ExpressionOpcode::Binary, ExpressionType::Duration,
                binary(BinaryOperator::NumberMultiplyDuration), 0U},
            {ExpressionOpcode::Return, ExpressionType::Duration, 0U, 0U},
        }));
    static_cast<void>(appendExpression(
        ExpressionType::Boolean,
        2U,
        {
            {ExpressionOpcode::LoadValue, ExpressionType::State, 0U, 0U},
            {ExpressionOpcode::PushState, ExpressionType::State, 1U, 0U},
            {ExpressionOpcode::Binary, ExpressionType::Boolean,
                binary(BinaryOperator::Equal), 0U},
            {ExpressionOpcode::Unary, ExpressionType::Boolean,
                unary(UnaryOperator::BooleanNot), 0U},
            {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
        }));
    static_cast<void>(appendExpression(
        ExpressionType::Boolean,
        2U,
        {
            {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
            {ExpressionOpcode::LoadValue, ExpressionType::Number, 1U, 0U},
            {ExpressionOpcode::Binary, ExpressionType::Boolean,
                binary(BinaryOperator::NotEqual), 0U},
            {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
        }));
    static_cast<void>(appendExpression(
        ExpressionType::Boolean,
        2U,
        {
            {ExpressionOpcode::PushDuration, ExpressionType::Duration, 0U, 0U},
            {ExpressionOpcode::LoadValue, ExpressionType::Duration, 2U, 0U},
            {ExpressionOpcode::Binary, ExpressionType::Boolean,
                binary(BinaryOperator::Equal), 0U},
            {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
        }));
    for (const BinaryOperator operation : {
             BinaryOperator::NumberLess,
             BinaryOperator::NumberLessEqual,
             BinaryOperator::NumberGreater,
             BinaryOperator::NumberGreaterEqual}) {
        static_cast<void>(appendExpression(
            ExpressionType::Boolean,
            2U,
            {
                {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
                {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
                {ExpressionOpcode::Binary, ExpressionType::Boolean,
                    binary(operation), 0U},
                {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
            }));
    }
    storage.debugInfo.expressionInstructionSpans.assign(
        storage.expressionCode.size(),
        span);

    storage.actionPrograms = {{{0U, 18U}, 1U, 1U, span}};
    storage.actionCode = {
        {ActionOpcode::Press, 0U, 0U},
        {ActionOpcode::Release, 0U, 0U},
        {ActionOpcode::Tap, 0U, 0U},
        {ActionOpcode::Wait, durationValue.value, 0U},
        {ActionOpcode::Gap, 0U, 0U},
        {ActionOpcode::Set, 0U, stateValue.value},
        {ActionOpcode::Set, 1U, numberValue.value},
        {ActionOpcode::Set, 2U, durationValue.value},
        {ActionOpcode::Toggle, 0U, 0U},
        {ActionOpcode::Exec, 4U, 0U},
        {ActionOpcode::JumpIfFalse, booleanBranch.value, 12U},
        {ActionOpcode::Jump, 12U, 0U},
        {ActionOpcode::RepeatInit, 0U, numberValue.value},
        {ActionOpcode::RepeatCheck, 0U, 17U},
        {ActionOpcode::RepeatNext, 0U, 0U},
        {ActionOpcode::Yield, 0U, 0U},
        {ActionOpcode::Jump, 13U, 0U},
        {ActionOpcode::End, 0U, 0U},
    };
    storage.debugInfo.actionInstructionSpans.assign(storage.actionCode.size(), span);
    storage.rules[0].condition = booleanBranch;
    storage.rules[0].action = ActionProgramId{0U};
    storage.controlRequirements = {
        {ControlRefId{0U}, ToControlUseBits(ControlUse::OutputDownUp)},
        {ControlRefId{1U}, static_cast<std::uint8_t>(
            ToControlUseBits(ControlUse::EventSource)
            | ToControlUseBits(ControlUse::PhysicalState))},
    };
    storage.requirements = ComputeProgramRequirements(storage);
    return storage;
}

void TestRequiredFixtures()
{
    constexpr std::array<std::uint64_t, 4> expectedDumpHashes{
        16250531285494687580ULL,
        6279514400355555682ULL,
        16791086043516604065ULL,
        16678841533137656072ULL,
    };
    const std::array<inputweaver::CompiledProgramStorage, 4> storages{
        inputweaver::test::MakeTapFixtureStorage(),
        inputweaver::test::MakeMappingFixtureStorage(),
        inputweaver::test::MakeConditionalRepeatFixtureStorage(),
        inputweaver::test::MakePauseControlFixtureStorage(),
    };
    constexpr std::array<std::string_view, 4> names{
        "tap fixture",
        "mapping fixture",
        "conditional repeat fixture",
        "pause-control fixture",
    };
    for (std::size_t index = 0; index < storages.size(); ++index) {
        const auto program = FinalizeFixture(storages[index], names[index]);
        if (program == nullptr) {
            continue;
        }
        const std::string dump = inputweaver::DumpCompiledProgram(*program);
        const std::uint64_t hash = Fnv1a64(dump);
        if (hash != expectedDumpHashes[index]) {
            std::cerr << "Golden hash for " << names[index] << ": " << hash << '\n';
        }
        Check(hash == expectedDumpHashes[index], std::string(names[index]) + " golden dump");
    }
}

void TestCanonicalization()
{
    inputweaver::CompiledProgramStorage baselineStorage =
        inputweaver::test::MakeTapFixtureStorage();
    inputweaver::CompiledProgramStorage noisyStorage = baselineStorage;
    noisyStorage.strings.push_back(noisyStorage.strings.front());
    noisyStorage.controls.push_back(noisyStorage.controls.front());
    std::reverse(
        noisyStorage.controlRequirements.begin(),
        noisyStorage.controlRequirements.end());

    const auto baseline = FinalizeFixture(std::move(baselineStorage), "canonical baseline");
    const auto noisy = FinalizeFixture(std::move(noisyStorage), "canonical noisy input");
    Check(
        baseline != nullptr && noisy != nullptr
            && inputweaver::DumpCompiledProgram(*baseline)
                == inputweaver::DumpCompiledProgram(*noisy),
        "canonicalization removes pool and ordering history");
}

void TestBuilderAndImmutableAccess()
{
    inputweaver::CompiledProgramBuilder builder;
    builder.Storage() = inputweaver::test::MakeTapFixtureStorage();
    builder.DeriveRequirements();
    inputweaver::FinalizeResult result = std::move(builder).Finalize();
    Check(result.program != nullptr, "builder finalizes a valid program");
    if (result.program != nullptr) {
        Check(result.program->SchemaVersion() == 1U, "immutable program exposes schema");
        Check(result.program->Rules().size() == 1U, "immutable program exposes const rule span");
        Check(result.program->PauseControlRules().empty(),
            "immutable program exposes const pause-control span");
    }

    using RulesReturn = decltype(std::declval<const inputweaver::CompiledProgram&>().Rules());
    static_assert(std::is_same_v<RulesReturn, std::span<const inputweaver::CompiledRule>>);
    using PauseRulesReturn = decltype(
        std::declval<const inputweaver::CompiledProgram&>().PauseControlRules());
    static_assert(std::is_same_v<
        PauseRulesReturn,
        std::span<const inputweaver::PauseControlRule>>);
}

void TestPauseControlContract()
{
    using namespace inputweaver;
    for (const PauseEffect effect : {
             PauseEffect::On,
             PauseEffect::Off,
             PauseEffect::Toggle}) {
        auto storage = test::MakePauseControlFixtureStorage();
        storage.pauseControlRules[0].effect = effect;
        const auto program = FinalizeFixture(
            std::move(storage),
            "pause-control effect");
        Check(program != nullptr, "every pause-control effect validates");
        if (program != nullptr) {
            Check(program->Requirements().maximumPauseRulesPerEvent == 1U,
                "pause-control rule requirement is derived");
            Check(program->Requirements().maximumTasksPerEvent == 0U,
                "pause-control rules create no task requirement");
        }
    }
    {
        auto storage = test::MakePauseControlFixtureStorage();
        storage.pauseControlRules[0].effect = static_cast<PauseEffect>(99U);
        const auto result = FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, ProgramValidationErrorCode::Rule),
            "unknown pause-control effect is rejected");
    }
    {
        auto storage = test::MakePauseControlFixtureStorage();
        storage.pauseControlBuckets[0].rules = {1U, 1U};
        storage.requirements = ComputeProgramRequirements(storage);
        const auto result = FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, ProgramValidationErrorCode::Range),
            "invalid pause-control bucket range is rejected");
    }
    {
        auto storage = test::MakePauseControlFixtureStorage();
        storage.rules.push_back({
            ExpressionId{},
            ActionProgramId{},
            MappingId{},
            Delivery::Observe,
            MatchFlow::Stop,
            RuleKind::Event,
            0U,
            storage.pauseControlRules[0].source});
        storage.eventBuckets.push_back({
            storage.pauseControlBuckets[0].key,
            {0U, 1U}});
        storage.requirements = ComputeProgramRequirements(storage);
        const auto result = FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, ProgramValidationErrorCode::Rule),
            "source ordinal collision across pause and ordinary channels is rejected");
    }
    {
        auto storage = test::MakeTapFixtureStorage();
        storage.valueRefs.push_back({
            ValueDomain::BuiltinState,
            ValueType::State,
            static_cast<std::uint32_t>(BuiltinState::Pause)});
        storage.actionCode[0] = {ActionOpcode::Toggle, 0U, 0U};
        storage.actionPrograms[0].maximumOwnedControlCount = 0U;
        storage.controlRequirements = {
            {ControlRefId{1U}, ToControlUseBits(ControlUse::EventSource)},
        };
        storage.requirements = ComputeProgramRequirements(storage);
        const auto result = FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, ProgramValidationErrorCode::Action),
            "ordinary action program PAUSE write is rejected");
    }
}

void TestCompleteOpcodeCoverage()
{
    const auto program = FinalizeFixture(
        MakeOpcodeCoverageStorage(),
        "complete opcode coverage fixture");
    Check(program != nullptr, "all expression and action opcodes form a valid contract");
    if (program != nullptr) {
        Check(program->Requirements().requiresProcessLaunch,
            "Exec derives the process-launch requirement");
        Check(program->Requirements().maximumRepeatFramesPerTask == 1U,
            "repeat frame requirement is derived");
        Check(program->Requirements().maximumExpressionStackDepth == 2U,
            "expression stack requirement is derived");
    }
}

void TestCorruptIdentifier()
{
    auto storage = inputweaver::test::MakeTapFixtureStorage();
    storage.actionCode[0].operand0 = 99U;
    storage.requirements = inputweaver::ComputeProgramRequirements(storage);
    const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
    Check(result.program == nullptr, "corrupt control ID returns no program");
    Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::Action),
        "corrupt control ID is diagnosed");
}

void TestCorruptRange()
{
    auto storage = inputweaver::test::MakeTapFixtureStorage();
    storage.actionPrograms[0].code.count = 99U;
    storage.requirements = inputweaver::ComputeProgramRequirements(storage);
    const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
    Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::Range),
        "out-of-range action code is diagnosed");
}

void TestCorruptBackwardJump()
{
    auto storage = inputweaver::test::MakeConditionalRepeatFixtureStorage();
    storage.actionCode[6].operand0 = 6U;
    storage.requirements = inputweaver::ComputeProgramRequirements(storage);
    const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
    Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::Action),
        "non-cooperative backward jump is diagnosed");
}

void TestBypassedYield()
{
    auto storage = MakeOpcodeCoverageStorage();
    storage.actionCode[12].operand1 = 18U;
    storage.requirements = inputweaver::ComputeProgramRequirements(storage);
    const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
    Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::Action),
        "an explicit branch cannot enter a backward jump after its Yield");
}

void TestCorruptExpressionMerge()
{
    auto storage = inputweaver::test::MakeTapFixtureStorage();
    const inputweaver::SourceSpan span{0U, storage.source.byteLength};
    storage.numberConstants = {1.0};
    storage.expressions = {{{0U, 6U}, inputweaver::ExpressionType::Number, 1U, span}};
    storage.expressionCode = {
        {inputweaver::ExpressionOpcode::PushBoolean,
            inputweaver::ExpressionType::Boolean, 1U, 0U},
        {inputweaver::ExpressionOpcode::JumpIfFalse,
            inputweaver::ExpressionType::None, 4U, 0U},
        {inputweaver::ExpressionOpcode::PushNumber,
            inputweaver::ExpressionType::Number, 0U, 0U},
        {inputweaver::ExpressionOpcode::Jump,
            inputweaver::ExpressionType::None, 5U, 0U},
        {inputweaver::ExpressionOpcode::PushState,
            inputweaver::ExpressionType::State, 1U, 0U},
        {inputweaver::ExpressionOpcode::Return,
            inputweaver::ExpressionType::Number, 0U, 0U},
    };
    storage.debugInfo.expressionInstructionSpans.assign(6U, span);
    storage.requirements = inputweaver::ComputeProgramRequirements(storage);
    const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
    Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::Expression),
        "inconsistent expression stack merge is diagnosed");
}

void TestCorruptRuleBuckets()
{
    auto storage = inputweaver::test::MakeTapFixtureStorage();
    storage.eventBuckets.push_back(storage.eventBuckets.front());
    storage.requirements = inputweaver::ComputeProgramRequirements(storage);
    const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
    Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::Rule)
            || HasError(result.errors, inputweaver::ProgramValidationErrorCode::Range),
        "duplicate or overlapping event bucket is diagnosed");
}

void TestCorruptMappingLink()
{
    auto storage = inputweaver::test::MakeMappingFixtureStorage();
    storage.mappings[0].slot = inputweaver::MappingSlotId{99U};
    storage.requirements = inputweaver::ComputeProgramRequirements(storage);
    const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
    Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::Mapping),
        "invalid mapping slot link is diagnosed");
}

void TestCorruptRequirements()
{
    auto storage = inputweaver::test::MakeTapFixtureStorage();
    ++storage.requirements.maximumTasksPerEvent;
    const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
    Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::Requirements),
        "requirements mismatch is diagnosed");
}

void TestCorruptDebugSpan()
{
    auto storage = inputweaver::test::MakeTapFixtureStorage();
    storage.debugInfo.actionInstructionSpans[0] = {
        storage.source.byteLength,
        1U};
    const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
    Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::DebugInfo),
        "out-of-range debug span is diagnosed");
}

void TestCorruptControlRequirements()
{
    auto storage = inputweaver::test::MakeTapFixtureStorage();
    storage.controlRequirements.clear();
    const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
    Check(HasError(
            result.errors,
            inputweaver::ProgramValidationErrorCode::ControlRequirement),
        "missing control requirements are diagnosed");
}

void TestRemainingValidationFamilies()
{
    {
        auto storage = inputweaver::test::MakeTapFixtureStorage();
        storage.schemaVersion = 99U;
        const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::Schema),
            "unsupported schema is diagnosed");
    }
    {
        auto storage = inputweaver::test::MakeTapFixtureStorage();
        storage.source.displayPath = inputweaver::StringId{99U};
        const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::Identifier),
            "invalid source identity is diagnosed");
    }
    {
        auto storage = inputweaver::test::MakeTapFixtureStorage();
        storage.strings[0] = std::string(1U, static_cast<char>(0xc0));
        const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::String),
            "invalid UTF-8 is diagnosed");
    }
    {
        auto storage = inputweaver::test::MakeTapFixtureStorage();
        storage.lineStarts[0] = 1U;
        const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::Source),
            "invalid line-start table is diagnosed");
    }
    {
        auto storage = inputweaver::test::MakeTapFixtureStorage();
        storage.valueRefs.push_back({
            inputweaver::ValueDomain::BuiltinDuration,
            inputweaver::ValueType::Number,
            0U});
        const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::Value),
            "invalid value reference shape is diagnosed");
    }
    {
        auto storage = inputweaver::test::MakeTapFixtureStorage();
        storage.rules[0].kind = static_cast<inputweaver::RuleKind>(99U);
        const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::Rule),
            "unknown rule kind is diagnosed");
    }
}

void TestValidationErrorLimit()
{
    auto storage = inputweaver::test::MakeTapFixtureStorage();
    storage.debugInfo.actionInstructionSpans.assign(
        100U,
        {storage.source.byteLength, 1U});
    const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
    Check(result.errors.size() == inputweaver::kMaximumProgramValidationErrors,
        "validation errors stop at the fixed diagnostic limit");
}

} // namespace

int main()
{
    TestRequiredFixtures();
    TestPauseControlContract();
    TestCanonicalization();
    TestBuilderAndImmutableAccess();
    TestCompleteOpcodeCoverage();
    TestCorruptIdentifier();
    TestCorruptRange();
    TestCorruptBackwardJump();
    TestBypassedYield();
    TestCorruptExpressionMerge();
    TestCorruptRuleBuckets();
    TestCorruptMappingLink();
    TestCorruptRequirements();
    TestCorruptDebugSpan();
    TestCorruptControlRequirements();
    TestRemainingValidationFamilies();
    TestValidationErrorLimit();

    if (g_failureCount == 0) {
        std::cout << "All CompiledProgram contract tests passed.\n";
        return 0;
    }
    std::cerr << g_failureCount << " CompiledProgram contract test(s) failed.\n";
    return 1;
}
