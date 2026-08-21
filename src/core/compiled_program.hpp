#pragma once

#include "input_event.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace inputweaver {

inline constexpr std::uint32_t kInvalidProgramIndex = 0xffffffffU;
inline constexpr std::uint32_t kCompiledProgramSchemaVersion = 1U;
inline constexpr std::size_t kMaximumProgramValidationErrors = 64U;

template <typename Tag>
struct ProgramId final {
    std::uint32_t value{kInvalidProgramIndex};

    constexpr ProgramId() noexcept = default;
    constexpr explicit ProgramId(std::uint32_t index) noexcept : value(index) {}

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return value != kInvalidProgramIndex;
    }

    auto operator<=>(const ProgramId&) const = default;
};

struct StringIdTag;
struct ControlRefIdTag;
struct ValueRefIdTag;
struct ExpressionIdTag;
struct ActionProgramIdTag;
struct MappingIdTag;
struct MappingSlotIdTag;

using StringId = ProgramId<StringIdTag>;
using ControlRefId = ProgramId<ControlRefIdTag>;
using ValueRefId = ProgramId<ValueRefIdTag>;
using ExpressionId = ProgramId<ExpressionIdTag>;
using ActionProgramId = ProgramId<ActionProgramIdTag>;
using MappingId = ProgramId<MappingIdTag>;
using MappingSlotId = ProgramId<MappingSlotIdTag>;

struct TableRange final {
    std::uint32_t begin{};
    std::uint32_t count{};

    auto operator<=>(const TableRange&) const = default;
};

struct SourceSpan final {
    std::uint32_t beginByte{};
    std::uint32_t byteLength{};

    auto operator<=>(const SourceSpan&) const = default;
};

struct DurationValue final {
    std::int64_t nanoseconds{};

    auto operator<=>(const DurationValue&) const = default;
};

struct ControlRef final {
    DeviceKind device{};
    ControlCode code{};

    auto operator<=>(const ControlRef&) const = default;
};

enum class EventTransition : std::uint8_t {
    Down,
    Repeat,
    Up,
};

struct EventKey final {
    ControlRef control{};
    EventTransition transition{};

    auto operator<=>(const EventKey&) const = default;
};

struct ProgramSource final {
    StringId displayPath{};
    std::uint32_t byteLength{};
    TableRange lineStarts{};
};

enum class TargetSelectorKind : std::uint8_t {
    Unspecified,
    Global,
    ExecutableName,
    AbsolutePath,
};

struct TargetSelector final {
    TargetSelectorKind kind{};
    StringId text{};
    SourceSpan source{};
};

struct ProgramSettings final {
    TargetSelector target{};
    DurationValue tapDuration{};
    DurationValue actionGap{};
};

enum class ValueType : std::uint8_t {
    State,
    Number,
    Duration,
};

enum class ExpressionType : std::uint8_t {
    None,
    Boolean,
    State,
    Number,
    Duration,
};

enum class ValueDomain : std::uint8_t {
    UserState,
    UserNumber,
    UserDuration,
    BuiltinState,
    BuiltinDuration,
};

enum class BuiltinState : std::uint8_t {
    Pause,
};

enum class BuiltinDuration : std::uint8_t {
    TapDuration,
    ActionGap,
};

struct ValueRef final {
    ValueDomain domain{};
    ValueType type{};
    std::uint32_t index{};

    auto operator<=>(const ValueRef&) const = default;
};

struct UserValueLayout final {
    std::vector<std::uint8_t> initialStates;
    std::vector<double> initialNumbers;
    std::vector<DurationValue> initialDurations;
};

struct VariableDebugRecord final {
    StringId name{};
    ValueRefId value{};
    SourceSpan declaration{};
};

enum class ExpressionOpcode : std::uint8_t {
    PushBoolean,
    PushState,
    PushNumber,
    PushDuration,
    LoadValue,
    ReadControlHeld,
    Unary,
    Binary,
    Jump,
    JumpIfFalse,
    JumpIfTrue,
    Return,
};

struct ExpressionInstruction final {
    ExpressionOpcode opcode{};
    ExpressionType type{};
    std::uint32_t operand0{};
    std::uint32_t operand1{};
};

struct ExpressionDescriptor final {
    TableRange code{};
    ExpressionType resultType{};
    std::uint32_t maximumStackDepth{};
    SourceSpan source{};
};

enum class UnaryOperator : std::uint8_t {
    NumberIdentity,
    NumberNegate,
    BooleanNot,
};

enum class BinaryOperator : std::uint8_t {
    NumberAdd,
    NumberSubtract,
    NumberMultiply,
    NumberDivide,
    NumberModulo,
    DurationAdd,
    DurationSubtract,
    DurationMultiplyNumber,
    NumberMultiplyDuration,
    DurationDivideNumber,
    Equal,
    NotEqual,
    NumberLess,
    NumberLessEqual,
    NumberGreater,
    NumberGreaterEqual,
};

enum class ActionOpcode : std::uint8_t {
    Press,
    Release,
    Tap,
    Wait,
    Gap,
    Set,
    Toggle,
    Exec,
    Jump,
    JumpIfFalse,
    RepeatInit,
    RepeatCheck,
    RepeatNext,
    Yield,
    End,
};

struct ActionInstruction final {
    ActionOpcode opcode{};
    std::uint32_t operand0{};
    std::uint32_t operand1{};
};

struct ActionProgramDescriptor final {
    TableRange code{};
    std::uint32_t repeatFrameCount{};
    std::uint32_t maximumOwnedControlCount{};
    SourceSpan source{};
};

enum class Delivery : std::uint8_t {
    Observe,
    Consume,
};

enum class MatchFlow : std::uint8_t {
    Stop,
    Continue,
};

enum class RuleKind : std::uint8_t {
    Event,
    MappingDown,
};

struct CompiledRule final {
    ExpressionId condition{};
    ActionProgramId action{};
    MappingId mapping{};
    Delivery delivery{};
    MatchFlow flow{};
    RuleKind kind{};
    std::uint32_t sourceOrdinal{};
    SourceSpan source{};
};

struct EventBucket final {
    EventKey key{};
    TableRange rules{};
};

struct MappingSlotDescriptor final {
    ControlRef source{};
};

struct MappingDescriptor final {
    MappingSlotId slot{};
    ControlRef target{};
    SourceSpan source{};
};

enum class ControlUse : std::uint8_t {
    EventSource = 1U << 0U,
    PhysicalState = 1U << 1U,
    OutputDownUp = 1U << 2U,
    OutputRepeat = 1U << 3U,
};

[[nodiscard]] constexpr std::uint8_t ToControlUseBits(ControlUse use) noexcept {
    return static_cast<std::uint8_t>(use);
}

struct ControlRequirement final {
    ControlRefId control{};
    std::uint8_t uses{};
};

struct ProgramRequirements final {
    std::uint32_t stateSlotCount{};
    std::uint32_t numberSlotCount{};
    std::uint32_t durationSlotCount{};
    std::uint32_t mappingSlotCount{};
    std::uint32_t maximumRulesPerEvent{};
    std::uint32_t maximumPredicateStepsPerEvent{};
    std::uint32_t maximumTasksPerEvent{};
    std::uint32_t maximumMappingOperationsPerEvent{};
    std::uint32_t maximumTransactionItemsPerEvent{};
    std::uint32_t maximumExpressionStackDepth{};
    std::uint32_t maximumRepeatFramesPerTask{};
    std::uint32_t maximumOwnedControlsPerTask{};
    bool requiresProcessLaunch{};

    auto operator<=>(const ProgramRequirements&) const = default;
};

struct ProgramDebugInfo final {
    std::vector<VariableDebugRecord> variables;
    std::vector<SourceSpan> expressionInstructionSpans;
    std::vector<SourceSpan> actionInstructionSpans;
};

struct CompiledProgramStorage final {
    std::uint32_t schemaVersion{kCompiledProgramSchemaVersion};
    ProgramSource source{};
    ProgramSettings settings{};
    ProgramRequirements requirements{};

    std::vector<std::string> strings;
    std::vector<std::uint32_t> lineStarts;
    std::vector<ControlRef> controls;
    std::vector<ControlRequirement> controlRequirements;
    std::vector<ValueRef> valueRefs;
    UserValueLayout userValues;

    std::vector<double> numberConstants;
    std::vector<DurationValue> durationConstants;
    std::vector<ExpressionDescriptor> expressions;
    std::vector<ExpressionInstruction> expressionCode;

    std::vector<ActionProgramDescriptor> actionPrograms;
    std::vector<ActionInstruction> actionCode;

    std::vector<MappingSlotDescriptor> mappingSlots;
    std::vector<MappingDescriptor> mappings;
    std::vector<EventBucket> eventBuckets;
    std::vector<CompiledRule> rules;

    ProgramDebugInfo debugInfo;
};

enum class ProgramValidationErrorCode : std::uint8_t {
    Schema,
    TableSize,
    Identifier,
    Range,
    String,
    Source,
    Value,
    Expression,
    Action,
    Rule,
    Mapping,
    ControlRequirement,
    DebugInfo,
    Requirements,
};

struct ProgramValidationError final {
    ProgramValidationErrorCode code{};
    std::string location;
    std::string message;
};

class CompiledProgram final {
public:
    [[nodiscard]] std::uint32_t SchemaVersion() const noexcept;
    [[nodiscard]] const ProgramSource& Source() const noexcept;
    [[nodiscard]] const ProgramSettings& Settings() const noexcept;
    [[nodiscard]] const ProgramRequirements& Requirements() const noexcept;
    [[nodiscard]] std::span<const std::string> Strings() const noexcept;
    [[nodiscard]] std::span<const std::uint32_t> LineStarts() const noexcept;
    [[nodiscard]] std::span<const ControlRef> Controls() const noexcept;
    [[nodiscard]] std::span<const ControlRequirement> ControlRequirements() const noexcept;
    [[nodiscard]] std::span<const ValueRef> ValueRefs() const noexcept;
    [[nodiscard]] const UserValueLayout& UserValues() const noexcept;
    [[nodiscard]] std::span<const double> NumberConstants() const noexcept;
    [[nodiscard]] std::span<const DurationValue> DurationConstants() const noexcept;
    [[nodiscard]] std::span<const ExpressionDescriptor> Expressions() const noexcept;
    [[nodiscard]] std::span<const ExpressionInstruction> ExpressionCode() const noexcept;
    [[nodiscard]] std::span<const ActionProgramDescriptor> ActionPrograms() const noexcept;
    [[nodiscard]] std::span<const ActionInstruction> ActionCode() const noexcept;
    [[nodiscard]] std::span<const MappingSlotDescriptor> MappingSlots() const noexcept;
    [[nodiscard]] std::span<const MappingDescriptor> Mappings() const noexcept;
    [[nodiscard]] std::span<const EventBucket> EventBuckets() const noexcept;
    [[nodiscard]] std::span<const CompiledRule> Rules() const noexcept;
    [[nodiscard]] const ProgramDebugInfo& DebugInfo() const noexcept;

private:
    explicit CompiledProgram(CompiledProgramStorage storage) noexcept;

    CompiledProgramStorage storage_;

    friend struct FinalizeResult;
    friend class CompiledProgramBuilder;
    friend struct CompiledProgramFinalizerAccess;
};

struct FinalizeResult final {
    std::shared_ptr<const CompiledProgram> program;
    std::vector<ProgramValidationError> errors;
};

[[nodiscard]] FinalizeResult FinalizeCompiledProgram(CompiledProgramStorage storage);

class CompiledProgramBuilder final {
public:
    [[nodiscard]] CompiledProgramStorage& Storage() noexcept;
    [[nodiscard]] const CompiledProgramStorage& Storage() const noexcept;
    void DeriveRequirements() noexcept;
    [[nodiscard]] CompiledProgramStorage TakeStorage() && noexcept;
    [[nodiscard]] FinalizeResult Finalize() &&;

private:
    CompiledProgramStorage storage_;
};

} // namespace inputweaver
