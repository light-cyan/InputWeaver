#include "compiled_program_fixtures.hpp"

#include "program/program_dump.hpp"
#include "program/program_requirements.hpp"
#include "program/weavec_codec.hpp"
#include "support/little_endian.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
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

[[nodiscard]] bool HasDecodeError(
    const inputweaver::DecodeWeavecResult& result,
    inputweaver::WeavecDecodeErrorCode code) noexcept
{
    return result.decodeError.has_value() && result.decodeError->code == code;
}

[[nodiscard]] std::uint64_t ReadLittleEndianU64(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept
{
    std::uint64_t value{};
    (void)inputweaver::support::ReadLittleEndian(bytes, offset, value);
    return value;
}

void WriteLittleEndianU64(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint64_t value) noexcept
{
    inputweaver::support::StoreLittleEndian<std::uint64_t>(
        bytes,
        offset,
        value);
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
    storage.strings.push_back("state-array");
    storage.strings.push_back("number-array");
    storage.settings.target.kind = TargetSelectorKind::Executable;
    storage.settings.target.text = StringId{4U};
    storage.settings.randomSeed = (std::numeric_limits<std::uint64_t>::max)();
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
        {ValueDomain::BuiltinNumber, ValueType::Number,
            static_cast<std::uint32_t>(BuiltinNumber::Rand01)},
    };
    storage.debugInfo.variables = {
        {StringId{1U}, ValueRefId{0U}, {1U, 1U}},
        {StringId{2U}, ValueRefId{1U}, {2U, 1U}},
        {StringId{3U}, ValueRefId{2U}, {3U, 1U}},
    };
    storage.arrays = {
        {ArrayElementType::State, {0U, 2U}},
        {ArrayElementType::Number, {0U, 2U}},
    };
    storage.initialArrayStates = {0U, 1U};
    storage.initialArrayNumbers = {1.0, 2.5};
    storage.debugInfo.arrays = {
        {StringId{5U}, ArrayId{0U}, {4U, 1U}},
        {StringId{6U}, ArrayId{1U}, {5U, 1U}},
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
        ExpressionType::ControlState,
        1U,
        {
            {ExpressionOpcode::ReadControlState, ExpressionType::ControlState,
                1U, 0U},
            {ExpressionOpcode::Return, ExpressionType::ControlState, 0U, 0U},
        }));
    static_cast<void>(appendExpression(
        ExpressionType::Number,
        1U,
        {
            {ExpressionOpcode::LoadValue, ExpressionType::Number, 6U, 0U},
            {ExpressionOpcode::Return, ExpressionType::Number, 0U, 0U},
        }));
    static_cast<void>(appendExpression(
        ExpressionType::ControlState,
        1U,
        {
            {ExpressionOpcode::PushControlState, ExpressionType::ControlState,
                1U, 0U},
            {ExpressionOpcode::Return, ExpressionType::ControlState, 0U, 0U},
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
        ExpressionType::Number,
        1U,
        {
            {ExpressionOpcode::LoadArrayLength, ExpressionType::Number, 1U, 0U},
            {ExpressionOpcode::Return, ExpressionType::Number, 0U, 0U},
        }));
    static_cast<void>(appendExpression(
        ExpressionType::State,
        1U,
        {
            {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
            {ExpressionOpcode::LoadArrayElement, ExpressionType::State, 0U, 0U},
            {ExpressionOpcode::Return, ExpressionType::State, 0U, 0U},
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

    storage.actionPrograms = {{{0U, 23U}, 1U, 1U, span}};
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
        {ActionOpcode::SetArrayElement, 0U, numberValue.value, stateValue.value},
        {ActionOpcode::ToggleArrayElement, 0U, numberValue.value, 0U},
        {ActionOpcode::AppendArrayElement, 1U, numberValue.value, 0U},
        {ActionOpcode::PopArrayElement, 0U, 0U, 0U},
        {ActionOpcode::ClearArray, 1U, 0U, 0U},
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
        {ControlRefId{6U}, ToControlUseBits(ControlUse::EventSource)},
    };
    storage.requirements = ComputeProgramRequirements(storage);
    return storage;
}

void TestRequiredFixtures()
{
    constexpr std::array<std::uint64_t, 4> expectedDumpHashes{
        3026090136955141491ULL,
        16667569394712554932ULL,
        715892691562162642ULL,
        8701880781902487440ULL,
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

void TestControlIdentityContract()
{
    using namespace inputweaver;
    const ControlRef f6{
        kControlNamespaceUsbHid,
        0x07U,
        0x3fU,
        kControlQualifierNone};
    const ControlRef f7{
        kControlNamespaceUsbHid,
        0x07U,
        0x40U,
        kControlQualifierNone};

    const auto tap = FinalizeFixture(test::MakeTapFixtureStorage(), "control identity tap");
    Check(
        tap != nullptr && tap->Controls().size() == 7U
            && tap->Controls()[0] == f6 && tap->Controls()[1] == f7,
        "control pool is canonicalized by the complete four-field identity");
    Check(
        tap != nullptr && tap->EventBuckets()[0].key.control == ControlRefId{0U}
            && tap->ActionCode()[0].operand0 == 1U,
        "event and output operands share dense canonical ControlRefIds");

    const auto mapping = FinalizeFixture(
        test::MakeMappingFixtureStorage(),
        "control identity mapping");
    Check(
        mapping != nullptr
            && mapping->MappingSlots()[0].source == ControlRefId{0U}
            && mapping->Mappings()[0].target == ControlRefId{1U},
        "mapping sources and targets use the shared control pool");

    constexpr std::array supportedIdentityShapes{
        ControlRef{kControlNamespaceUsbHid, 1U, 0U, 0U},
        ControlRef{kControlNamespaceUsbHid, kMaximumHidUsagePage,
            kMaximumHidUsageId, 0U},
        ControlRef{kControlNamespaceWindows, kWindowsVirtualKeyFamily, 0U, 0U},
        ControlRef{kControlNamespaceWindows, kWindowsVirtualKeyFamily,
            kMaximumWindowsNativeCode, 0U},
        ControlRef{kControlNamespaceWindows, kWindowsScanCodeFamily, 0U,
            kControlQualifierNone},
        ControlRef{kControlNamespaceWindows, kWindowsScanCodeFamily, 0x1dU,
            kWindowsScanCodeQualifierE0},
        ControlRef{kControlNamespaceWindows, kWindowsScanCodeFamily,
            kMaximumWindowsNativeCode, kWindowsScanCodeQualifierE1},
        ControlRef{kControlNamespaceLinux, kLinuxEvKeyFamily, 0U, 0U},
        ControlRef{kControlNamespaceLinux, kLinuxEvKeyFamily,
            kMaximumLinuxEvKeyCode, 0U},
        ControlRef{kControlNamespaceMacOs, kMacOsKeyCodeFamily, 0U, 0U},
        ControlRef{kControlNamespaceMacOs, kMacOsKeyCodeFamily,
            kMaximumMacOsKeyCode, 0U},
    };
    for (const ControlRef control : supportedIdentityShapes) {
        auto storage = test::MakePauseControlFixtureStorage();
        storage.controls[0] = control;
        const auto result = FinalizeCompiledProgram(std::move(storage));
        Check(result.program != nullptr && result.errors.empty(),
            "published control namespace shape validates structurally");
    }

    constexpr std::array invalidIdentityShapes{
        ControlRef{0U, 1U, 1U, 0U},
        ControlRef{3U, 1U, 1U, 0U},
        ControlRef{kControlNamespaceWeave, 1U, 1U, 0U},
        ControlRef{kControlNamespaceUsbHid, 0U, 0U, 0U},
        ControlRef{kControlNamespaceUsbHid, kMaximumHidUsagePage + 1U, 0U, 0U},
        ControlRef{kControlNamespaceUsbHid, 1U, kMaximumHidUsageId + 1U, 0U},
        ControlRef{kControlNamespaceWindows, 3U, 1U, 0U},
        ControlRef{kControlNamespaceWindows, kWindowsVirtualKeyFamily, 1U, 1U},
        ControlRef{kControlNamespaceWindows, kWindowsVirtualKeyFamily,
            kMaximumWindowsNativeCode + 1U, 0U},
        ControlRef{kControlNamespaceWindows, kWindowsScanCodeFamily, 1U, 3U},
        ControlRef{kControlNamespaceWindows, kWindowsScanCodeFamily,
            kMaximumWindowsNativeCode + 1U, 0U},
        ControlRef{kControlNamespaceLinux, 2U, 1U, 0U},
        ControlRef{kControlNamespaceLinux, kLinuxEvKeyFamily,
            kMaximumLinuxEvKeyCode + 1U, 0U},
        ControlRef{kControlNamespaceMacOs, kMacOsKeyCodeFamily,
            kMaximumMacOsKeyCode + 1U, 0U},
        ControlRef{kControlNamespaceMacOs, kMacOsKeyCodeFamily,
            kInvalidProgramIndex, 0U},
    };
    for (const ControlRef control : invalidIdentityShapes) {
        auto storage = test::MakePauseControlFixtureStorage();
        storage.controls[0] = control;
        const auto result = FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, ProgramValidationErrorCode::Value),
            "invalid control namespace, family, numeric domain, qualifier, or field is rejected");
    }
}

void TestWeavecRoundTrips()
{
    using namespace inputweaver;
    const std::array<CompiledProgramStorage, 5U> storages{
        test::MakeTapFixtureStorage(),
        test::MakeMappingFixtureStorage(),
        test::MakeConditionalRepeatFixtureStorage(),
        test::MakePauseControlFixtureStorage(),
        MakeOpcodeCoverageStorage(),
    };
    constexpr std::array<std::uint8_t, 8U> magic{
        0x57U, 0x45U, 0x41U, 0x56U, 0x45U, 0x43U, 0x00U, 0x04U};

    for (std::size_t index = 0; index < storages.size(); ++index) {
        const auto original = FinalizeFixture(storages[index], "weavec source fixture");
        if (original == nullptr) {
            continue;
        }
        const std::vector<std::uint8_t> bytes = EncodeWeavec(*original);
        Check(bytes.size() >= kWeavecHeaderSize,
            "encoded artifact contains the complete header");
        Check(
            bytes.size() >= magic.size()
                && std::equal(magic.begin(), magic.end(), bytes.begin()),
            "encoded artifact uses the WEAVEC magic bytes");
        Check(
            ReadLittleEndianU64(bytes, 8U)
                == static_cast<std::uint64_t>(bytes.size() - kWeavecHeaderSize),
            "encoded header contains the exact payload length");

        const DecodeWeavecResult decoded = DecodeWeavec(bytes);
        Check(!decoded.decodeError.has_value(),
            "encoded artifact passes binary decoding");
        Check(decoded.validationErrors.empty(),
            "decoded artifact passes structural validation");
        Check(decoded.program != nullptr,
            "decoded artifact produces an immutable program");
        if (decoded.program != nullptr) {
            Check(
                DumpCompiledProgram(*decoded.program) == DumpCompiledProgram(*original),
                "weavec round trip preserves the deterministic program dump");
            Check(EncodeWeavec(*decoded.program) == bytes,
                "weavec round trip reproduces identical bytes");
        }
    }
}

void TestWeavecRejection()
{
    using namespace inputweaver;
    const auto program = FinalizeFixture(test::MakeTapFixtureStorage(), "weavec rejection");
    if (program == nullptr) {
        return;
    }
    const std::vector<std::uint8_t> valid = EncodeWeavec(*program);

    {
        const std::vector<std::uint8_t> shortHeader(15U, 0U);
        const auto result = DecodeWeavec(shortHeader);
        Check(HasDecodeError(result, WeavecDecodeErrorCode::InvalidHeader),
            "short weavec header is rejected");
    }
    {
        auto bytes = valid;
        bytes[0] ^= 0xffU;
        const auto result = DecodeWeavec(bytes);
        Check(HasDecodeError(result, WeavecDecodeErrorCode::InvalidHeader),
            "invalid weavec magic is rejected");
    }
    {
        auto bytes = valid;
        bytes[7] = 0x03U;
        const auto result = DecodeWeavec(bytes);
        Check(HasDecodeError(result, WeavecDecodeErrorCode::InvalidHeader),
            "obsolete V3 artifacts are rejected");
    }
    {
        auto bytes = valid;
        WriteLittleEndianU64(
            bytes,
            8U,
            ReadLittleEndianU64(bytes, 8U) + 1U);
        const auto result = DecodeWeavec(bytes);
        Check(HasDecodeError(result, WeavecDecodeErrorCode::LengthMismatch),
            "mismatched weavec payload length is rejected");
    }
    {
        auto bytes = valid;
        bytes.pop_back();
        WriteLittleEndianU64(
            bytes,
            8U,
            static_cast<std::uint64_t>(bytes.size() - kWeavecHeaderSize));
        const auto result = DecodeWeavec(bytes);
        Check(HasDecodeError(result, WeavecDecodeErrorCode::Truncated),
            "internally truncated weavec payload is rejected");
    }
    {
        auto bytes = valid;
        bytes.push_back(0U);
        WriteLittleEndianU64(
            bytes,
            8U,
            static_cast<std::uint64_t>(bytes.size() - kWeavecHeaderSize));
        const auto result = DecodeWeavec(bytes);
        Check(HasDecodeError(result, WeavecDecodeErrorCode::TrailingData),
            "trailing weavec payload field is rejected");
    }
    {
        auto bytes = valid;
        constexpr std::size_t targetKindOffset = kWeavecHeaderSize + 16U;
        bytes[targetKindOffset] = 0xffU;
        const auto result = DecodeWeavec(bytes);
        Check(HasDecodeError(result, WeavecDecodeErrorCode::InvalidScalar),
            "unknown encoded enumeration is rejected");
    }
    {
        auto bytes = valid;
        constexpr std::size_t requirementsBooleanOffset =
            kWeavecHeaderSize + 16U + 37U + 68U;
        bytes[requirementsBooleanOffset] = 2U;
        const auto result = DecodeWeavec(bytes);
        Check(HasDecodeError(result, WeavecDecodeErrorCode::InvalidScalar),
            "noncanonical encoded Boolean is rejected");
    }
    {
        WeavecDecodeLimits limits{};
        limits.maximumPayloadBytes = 0U;
        const auto result = DecodeWeavec(valid, limits);
        Check(HasDecodeError(result, WeavecDecodeErrorCode::LimitExceeded),
            "configured weavec payload limit is enforced");
    }
    {
        WeavecDecodeLimits limits{};
        limits.maximumCollectionElements = 0U;
        const auto result = DecodeWeavec(valid, limits);
        Check(HasDecodeError(result, WeavecDecodeErrorCode::LimitExceeded),
            "configured weavec collection limit is enforced before allocation");
    }
    {
        auto bytes = valid;
        constexpr std::size_t maximumRulesOffset =
            kWeavecHeaderSize + 16U + 37U + 36U;
        bytes[maximumRulesOffset] = 0U;
        const auto result = DecodeWeavec(bytes);
        Check(!result.decodeError.has_value() && result.program == nullptr
                && HasError(
                    result.validationErrors,
                    ProgramValidationErrorCode::Requirements),
            "serialized synchronous requirement cannot understate hook-path work");
    }
    {
        auto bytes = valid;
        constexpr std::size_t maximumTasksOffset =
            kWeavecHeaderSize + 16U + 37U + 44U;
        bytes[maximumTasksOffset] = 0U;
        const auto result = DecodeWeavec(bytes);
        Check(!result.decodeError.has_value() && result.program == nullptr
                && HasError(
                    result.validationErrors,
                    ProgramValidationErrorCode::Requirements),
            "structurally inconsistent decoded program is rejected by finalization");
    }
    {
        auto bytes = valid;
        constexpr std::size_t firstStringByteOffset =
            kWeavecHeaderSize + 16U + 37U + 69U + 4U + 4U;
        bytes[firstStringByteOffset] = 0xc0U;
        const auto result = DecodeWeavec(bytes);
        Check(!result.decodeError.has_value() && result.program == nullptr
                && HasError(
                    result.validationErrors,
                    ProgramValidationErrorCode::String),
            "decoded UTF-8 violation reaches structural validation");
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
    Check(
        baseline != nullptr && noisy != nullptr
            && inputweaver::EncodeWeavec(*baseline)
                == inputweaver::EncodeWeavec(*noisy),
        "canonicalization produces deterministic weavec bytes");
}

void TestBuilderAndImmutableAccess()
{
    inputweaver::CompiledProgramBuilder builder;
    builder.Storage() = inputweaver::test::MakeTapFixtureStorage();
    builder.DeriveRequirements();
    inputweaver::FinalizeResult result = std::move(builder).Finalize();
    Check(result.program != nullptr, "builder finalizes a valid program");
    if (result.program != nullptr) {
        Check(result.program->Rules().size() == 1U, "immutable program exposes const rule span");
        Check(result.program->PauseControlRules().empty(),
            "immutable program exposes const pause-control span");
        Check(result.program->ExitControlRules().size() == 1U,
            "immutable program exposes the compiled exit-control span");
    }

    using RulesReturn = decltype(std::declval<const inputweaver::CompiledProgram&>().Rules());
    static_assert(std::is_same_v<RulesReturn, std::span<const inputweaver::CompiledRule>>);
    using PauseRulesReturn = decltype(
        std::declval<const inputweaver::CompiledProgram&>().PauseControlRules());
    static_assert(std::is_same_v<
        PauseRulesReturn,
        std::span<const inputweaver::PauseControlRule>>);
    using ExitRulesReturn = decltype(
        std::declval<const inputweaver::CompiledProgram&>().ExitControlRules());
    static_assert(std::is_same_v<
        ExitRulesReturn,
        std::span<const inputweaver::ExitControlRule>>);
}

void TestExitControlContract()
{
    using namespace inputweaver;
    {
        const auto program = FinalizeFixture(
            test::MakeTapFixtureStorage(),
            "exit-control fixture");
        Check(
            program != nullptr
                && program->ExitControlRules().size() == 1U
                && program->Requirements().maximumExitRulesPerEvent == 1U,
            "exit-control rules and capacity requirements are preserved");
    }
    {
        auto storage = test::MakeTapFixtureStorage();
        storage.exitControlBuckets.clear();
        storage.exitControlRules.clear();
        storage.requirements = ComputeProgramRequirements(storage);
        const auto result = FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, ProgramValidationErrorCode::Rule),
            "compiled programs without an exit control are rejected");
    }
    {
        auto storage = test::MakeConditionalRepeatFixtureStorage();
        storage.exitControlRules[0].condition = ExpressionId{1U};
        const auto result = FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, ProgramValidationErrorCode::Rule),
            "exit-control conditions must be Boolean");
    }
    {
        auto storage = test::MakeTapFixtureStorage();
        storage.exitControlBuckets[0].rules = {1U, 1U};
        storage.requirements = ComputeProgramRequirements(storage);
        const auto result = FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, ProgramValidationErrorCode::Range),
            "invalid exit-control bucket ranges are rejected");
    }
    {
        auto storage = test::MakeTapFixtureStorage();
        storage.exitControlRules[0].sourceOrdinal = 0U;
        const auto result = FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, ProgramValidationErrorCode::Rule),
            "exit and ordinary rules cannot share a source ordinal");
    }
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
        Check(program->Requirements().arrayCount == 2U
                && program->Requirements().initialArrayElementBytes
                    == 2U + 2U * sizeof(double)
                && program->Arrays().size() == 2U,
            "array requirements and descriptors are retained");
    }
}

void TestCorruptArrays()
{
    using namespace inputweaver;
    const auto expectError = [](
        auto mutate,
        ProgramValidationErrorCode code,
        std::string_view name) {
        CompiledProgramStorage storage = MakeOpcodeCoverageStorage();
        mutate(storage);
        const FinalizeResult result = FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, code), name);
    };
    expectError([](CompiledProgramStorage& storage) {
        storage.initialArrayStates[0] = 2U;
    }, ProgramValidationErrorCode::Value,
        "invalid initial array state is diagnosed");
    expectError([](CompiledProgramStorage& storage) {
        storage.initialArrayNumbers[0] =
            (std::numeric_limits<double>::quiet_NaN)();
    }, ProgramValidationErrorCode::Value,
        "non-finite initial array number is diagnosed");
    expectError([](CompiledProgramStorage& storage) {
        storage.arrays[1].initialValues.count = 99U;
    }, ProgramValidationErrorCode::Range,
        "out-of-range array initializer span is diagnosed");
    expectError([](CompiledProgramStorage& storage) {
        storage.arrays[1].initialValues.count = 1U;
    }, ProgramValidationErrorCode::Range,
        "incomplete array initializer pool coverage is diagnosed");
    expectError([](CompiledProgramStorage& storage) {
        storage.arrays[0].elementType = static_cast<ArrayElementType>(99U);
    }, ProgramValidationErrorCode::Value,
        "unknown array element type is diagnosed");
    expectError([](CompiledProgramStorage& storage) {
        storage.debugInfo.arrays[1].array = ArrayId{0U};
    }, ProgramValidationErrorCode::DebugInfo,
        "duplicate array debug identity is diagnosed");
    expectError([](CompiledProgramStorage& storage) {
        const auto instruction = std::find_if(
            storage.actionCode.begin(),
            storage.actionCode.end(),
            [](const ActionInstruction& value) {
                return value.opcode == ActionOpcode::ToggleArrayElement;
            });
        instruction->operand0 = 99U;
    }, ProgramValidationErrorCode::Action,
        "invalid array action identity is diagnosed");
    expectError([](CompiledProgramStorage& storage) {
        const auto instruction = std::find_if(
            storage.expressionCode.begin(),
            storage.expressionCode.end(),
            [](const ExpressionInstruction& value) {
                return value.opcode == ExpressionOpcode::LoadArrayLength;
            });
        instruction->operand1 = 1U;
    }, ProgramValidationErrorCode::Expression,
        "array expression unused operands are diagnosed");
    expectError([](CompiledProgramStorage& storage) {
        const auto instruction = std::find_if(
            storage.actionCode.begin(),
            storage.actionCode.end(),
            [](const ActionInstruction& value) {
                return value.opcode == ActionOpcode::AppendArrayElement;
            });
        instruction->operand2 = 1U;
    }, ProgramValidationErrorCode::Action,
        "array action unused operands are diagnosed");
}

void TestDeepExpressionStack()
{
    using namespace inputweaver;
    constexpr std::uint32_t depth = 32U * 1024U;
    CompiledProgramStorage storage = test::MakeTapFixtureStorage();
    storage.numberConstants = {1.0};
    storage.expressionCode.clear();
    storage.expressionCode.reserve(2U * depth + 2U);
    for (std::uint32_t index = 0U; index < depth; ++index) {
        storage.expressionCode.push_back({
            ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U});
    }
    for (std::uint32_t index = 1U; index < depth; ++index) {
        storage.expressionCode.push_back({ExpressionOpcode::Binary,
            ExpressionType::Number,
            static_cast<std::uint32_t>(BinaryOperator::NumberAdd), 0U});
    }
    storage.expressionCode.insert(storage.expressionCode.end(), {
        {ExpressionOpcode::PushNumber, ExpressionType::Number, 0U, 0U},
        {ExpressionOpcode::Binary, ExpressionType::Boolean,
            static_cast<std::uint32_t>(BinaryOperator::Equal), 0U},
        {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
    });
    storage.expressions = {{{0U, static_cast<std::uint32_t>(
        storage.expressionCode.size())}, ExpressionType::Boolean, depth, {}}};
    storage.debugInfo.expressionInstructionSpans.assign(
        storage.expressionCode.size(), {});
    storage.controlRequirements.erase(
        std::remove_if(
            storage.controlRequirements.begin(),
            storage.controlRequirements.end(),
            [](const ControlRequirement& requirement) {
                return requirement.uses
                    == ToControlUseBits(ControlUse::PhysicalState);
            }),
        storage.controlRequirements.end());
    storage.requirements = ComputeProgramRequirements(storage);

    const FinalizeResult result = FinalizeCompiledProgram(std::move(storage));
    Check(result.errors.empty() && result.program != nullptr,
        "deep expression stacks validate with bounded state storage");
    if (result.program != nullptr) {
        Check(result.program->Requirements().maximumExpressionStackDepth == depth,
            "deep expression stack depth remains exact");
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
    storage.actionCode[10].operand1 = 16U;
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
    {
        auto storage = inputweaver::test::MakeTapFixtureStorage();
        ++storage.requirements.maximumTasksPerEvent;
        const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::Requirements),
            "task requirement mismatch is diagnosed");
    }
    {
        auto storage = inputweaver::test::MakePauseControlFixtureStorage();
        --storage.requirements.maximumPauseRulesPerEvent;
        const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::Requirements),
            "pause-rule requirement mismatch is diagnosed");
    }
    {
        auto storage = inputweaver::test::MakeTapFixtureStorage();
        --storage.requirements.maximumRulesPerEvent;
        const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::Requirements),
            "ordinary-rule requirement mismatch is diagnosed");
    }
    {
        auto storage = inputweaver::test::MakeConditionalRepeatFixtureStorage();
        --storage.requirements.maximumPredicateStepsPerEvent;
        const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::Requirements),
            "predicate-step requirement mismatch is diagnosed");
    }
    {
        auto storage = inputweaver::test::MakeMappingFixtureStorage();
        --storage.requirements.maximumMappingOperationsPerEvent;
        const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::Requirements),
            "mapping-operation requirement mismatch is diagnosed");
    }
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
        auto storage = MakeOpcodeCoverageStorage();
        storage.strings[4] = std::string("tool\0argument", 13U);
        const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::Action)
                && HasError(
                    result.errors,
                    inputweaver::ProgramValidationErrorCode::Identifier),
            "embedded NUL in executable text is diagnosed");
    }
    {
        auto storage = MakeOpcodeCoverageStorage();
        storage.strings[4].clear();
        const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::Action)
                && HasError(
                    result.errors,
                    inputweaver::ProgramValidationErrorCode::Identifier),
            "empty executable target and Exec command are diagnosed");
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
        storage.valueRefs.push_back({
            inputweaver::ValueDomain::BuiltinNumber,
            inputweaver::ValueType::Number,
            static_cast<std::uint32_t>(inputweaver::BuiltinNumber::Rand01) + 1U});
        const auto result = inputweaver::FinalizeCompiledProgram(std::move(storage));
        Check(HasError(result.errors, inputweaver::ProgramValidationErrorCode::Value),
            "unknown builtin number is diagnosed");
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
    TestControlIdentityContract();
    TestWeavecRoundTrips();
    TestWeavecRejection();
    TestExitControlContract();
    TestPauseControlContract();
    TestCanonicalization();
    TestBuilderAndImmutableAccess();
    TestCompleteOpcodeCoverage();
    TestCorruptArrays();
    TestDeepExpressionStack();
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
