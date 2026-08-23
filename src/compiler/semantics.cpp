#include "semantics.hpp"

#include "control_catalog.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace inputweaver::compiler {
namespace {

constexpr std::int64_t kMaximumSettingDuration = 60'000'000'000LL;

struct Symbol final {
    ValueRef value{};
    SourceSpan declaration{};
};

[[nodiscard]] ExpressionType ToExpressionType(ValueType type) noexcept
{
    switch (type) {
    case ValueType::State: return ExpressionType::State;
    case ValueType::Number: return ExpressionType::Number;
    case ValueType::Duration: return ExpressionType::Duration;
    }
    return ExpressionType::None;
}

[[nodiscard]] bool IsWritable(ValueRef value) noexcept
{
    return value.domain == ValueDomain::UserState
        || value.domain == ValueDomain::UserNumber
        || value.domain == ValueDomain::UserDuration;
}

[[nodiscard]] bool IsReservedName(std::string_view name) noexcept
{
    constexpr std::array<std::string_view, 36U> reserved{{
        "TARGET", "TAP_DURATION", "ACTION_GAP", "PAUSE", "GLOBAL",
        "state", "number", "duration", "pause", "when", "on", "off",
        "toggle", "down", "repeat", "up", "and", "or", "not", "press",
        "release", "tap", "wait", "gap", "set", "exec", "if", "then",
        "else", "end", "do", "while", "held", "idle", "E0", "E1",
    }};
    return std::find(reserved.begin(), reserved.end(), name) != reserved.end()
        || IsReservedControlIdentifier(name);
}

[[nodiscard]] std::optional<double> ParseNumber(std::string_view text) noexcept
{
    double value = 0.0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(
        begin,
        end,
        value,
        std::chars_format::fixed);
    if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value == 0.0 ? 0.0 : value;
}

[[nodiscard]] std::optional<std::uint64_t> ParseUnsigned(
    std::string_view text) noexcept
{
    if (text.empty() || text.front() == '-') {
        return std::nullopt;
    }
    int base = 10;
    if (text.starts_with("0x")) {
        text.remove_prefix(2U);
        base = 16;
    }
    if (text.empty() || text.find('.') != std::string_view::npos) {
        return std::nullopt;
    }
    std::uint64_t value = 0U;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value, base);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<DurationValue> ParseDuration(
    std::string_view text) noexcept
{
    std::int64_t factor = 0;
    std::string_view number = text;
    if (text.ends_with("min")) {
        factor = 60'000'000'000LL;
        number.remove_suffix(3U);
    } else if (text.ends_with("ms")) {
        factor = 1'000'000LL;
        number.remove_suffix(2U);
    } else if (text.ends_with("s")) {
        factor = 1'000'000'000LL;
        number.remove_suffix(1U);
    } else {
        return std::nullopt;
    }

    const std::size_t dot = number.find('.');
    const std::string_view wholeText = dot == std::string_view::npos
        ? number
        : number.substr(0U, dot);
    std::string_view fractionText = dot == std::string_view::npos
        ? std::string_view{}
        : number.substr(dot + 1U);
    const auto whole = ParseUnsigned(wholeText);
    if (!whole.has_value()
        || *whole > static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max() / factor)) {
        return std::nullopt;
    }
    std::uint64_t total = *whole * static_cast<std::uint64_t>(factor);
    while (!fractionText.empty() && fractionText.back() == '0') {
        fractionText.remove_suffix(1U);
    }
    if (fractionText.empty()) {
        return DurationValue{static_cast<std::int64_t>(total)};
    }
    if (fractionText.size() > 10U) {
        return std::nullopt;
    }
    const auto fraction = ParseUnsigned(fractionText);
    if (!fraction.has_value()) {
        return std::nullopt;
    }
    std::uint64_t denominator = 1U;
    for (std::size_t index = 0U; index < fractionText.size(); ++index) {
        denominator *= 10U;
    }
    const std::uint64_t unsignedFactor = static_cast<std::uint64_t>(factor);
    const std::uint64_t divisor = std::gcd(denominator, unsignedFactor);
    const std::uint64_t reducedDenominator = denominator / divisor;
    if (*fraction % reducedDenominator != 0U) {
        return std::nullopt;
    }
    const std::uint64_t fractionNanoseconds = (*fraction / reducedDenominator)
        * (unsignedFactor / divisor);
    if (fractionNanoseconds
        > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
            - total) {
        return std::nullopt;
    }
    total += fractionNanoseconds;
    return DurationValue{static_cast<std::int64_t>(total)};
}

[[nodiscard]] std::unique_ptr<BoundExpression> MakeErrorExpression(
    SourceSpan span)
{
    auto expression = std::make_unique<BoundExpression>();
    expression->span = span;
    expression->type = ExpressionType::None;
    return expression;
}

class Binder final {
public:
    Binder(
        const SourceFile& source,
        const SyntaxTree& syntax,
        DiagnosticSink& diagnostics)
        : source_(source), syntax_(syntax), diagnostics_(diagnostics)
    {
        program_.displayPath = source.displayPath;
        program_.sourceByteLength = static_cast<std::uint32_t>(source.bytes.size());
        program_.lineStarts = source.lineStarts;
    }

    [[nodiscard]] BoundProgram Run()
    {
        for (const TopLevelSyntax& item : syntax_.items) {
            switch (item.kind) {
            case TopLevelSyntax::Kind::TargetSetting:
                BindTarget(item);
                break;
            case TopLevelSyntax::Kind::TapDurationSetting:
                BindDurationSetting(item, true);
                break;
            case TopLevelSyntax::Kind::ActionGapSetting:
                BindDurationSetting(item, false);
                break;
            case TopLevelSyntax::Kind::StateDeclaration:
            case TopLevelSyntax::Kind::NumberDeclaration:
            case TopLevelSyntax::Kind::DurationDeclaration:
                BindDeclaration(item);
                break;
            case TopLevelSyntax::Kind::Mapping:
            case TopLevelSyntax::Kind::PauseRule:
            case TopLevelSyntax::Kind::EventRule:
                BindRule(item);
                break;
            }
        }
        return std::move(program_);
    }

private:
    void BindTarget(const TopLevelSyntax& item)
    {
        if (targetSetting_.has_value()) {
            diagnostics_.Add(
                CompileDiagnosticCode::DuplicateSetting,
                item.span,
                "TARGET may be assigned only once",
                {{*targetSetting_, "first assignment is here"}});
            return;
        }
        targetSetting_ = item.span;
        program_.targetKind = item.targetGlobal
            ? TargetSelectorKind::Global
            : TargetSelectorKind::Executable;
        program_.targetText = item.literal;
        program_.targetSource = item.valueSpan;
        if (!item.targetGlobal && item.literal.empty()) {
            diagnostics_.Add(
                CompileDiagnosticCode::EmptyString,
                item.valueSpan,
                "target selector must not be empty");
        } else if (!item.targetGlobal
                   && item.literal.find('\0') != std::string::npos) {
            diagnostics_.Add(
                CompileDiagnosticCode::EmbeddedNul,
                item.valueSpan,
                "target selector contains an embedded NUL");
        }
    }

    void BindDurationSetting(const TopLevelSyntax& item, bool tap)
    {
        std::optional<SourceSpan>& previous = tap
            ? tapDurationSetting_
            : actionGapSetting_;
        const std::string_view name = tap ? "TAP_DURATION" : "ACTION_GAP";
        if (previous.has_value()) {
            diagnostics_.Add(
                CompileDiagnosticCode::DuplicateSetting,
                item.span,
                std::string(name) + " may be assigned only once",
                {{*previous, "first assignment is here"}});
            return;
        }
        previous = item.span;
        const auto value = ParseDuration(item.literal);
        if (!value.has_value()) {
            diagnostics_.Add(
                CompileDiagnosticCode::InvalidDuration,
                item.valueSpan,
                "duration literal is out of range or not exactly representable in nanoseconds");
            return;
        }
        if (value->nanoseconds > kMaximumSettingDuration) {
            diagnostics_.Add(
                CompileDiagnosticCode::SettingOutOfRange,
                item.valueSpan,
                std::string(name) + " must be between 0ms and 1min");
            return;
        }
        if (tap) {
            program_.tapDuration = *value;
        } else {
            program_.actionGap = *value;
        }
    }

    void BindDeclaration(const TopLevelSyntax& item)
    {
        if (IsReservedName(item.name)) {
            diagnostics_.Add(
                CompileDiagnosticCode::ReservedName,
                item.span,
                "'" + item.name + "' is reserved and cannot name a variable");
            return;
        }
        const auto existing = symbols_.find(item.name);
        if (existing != symbols_.end()) {
            diagnostics_.Add(
                CompileDiagnosticCode::DuplicateSymbol,
                item.span,
                "variable '" + item.name + "' is already declared",
                {{existing->second.declaration, "first declaration is here"}});
            return;
        }

        ValueRef value{};
        if (item.kind == TopLevelSyntax::Kind::StateDeclaration) {
            value = {
                ValueDomain::UserState,
                ValueType::State,
                static_cast<std::uint32_t>(program_.userValues.initialStates.size())};
            program_.userValues.initialStates.push_back(item.stateValue ? 1U : 0U);
        } else if (item.kind == TopLevelSyntax::Kind::NumberDeclaration) {
            value = {
                ValueDomain::UserNumber,
                ValueType::Number,
                static_cast<std::uint32_t>(program_.userValues.initialNumbers.size())};
            const auto initial = ParseNumber(item.literal);
            if (!initial.has_value()) {
                diagnostics_.Add(
                    CompileDiagnosticCode::InvalidNumber,
                    item.valueSpan,
                    "number literal must be a finite binary64 value");
            }
            program_.userValues.initialNumbers.push_back(initial.value_or(0.0));
        } else {
            value = {
                ValueDomain::UserDuration,
                ValueType::Duration,
                static_cast<std::uint32_t>(program_.userValues.initialDurations.size())};
            const auto initial = ParseDuration(item.literal);
            if (!initial.has_value()) {
                diagnostics_.Add(
                    CompileDiagnosticCode::InvalidDuration,
                    item.valueSpan,
                    "duration literal is out of range or not exactly representable in nanoseconds");
            }
            program_.userValues.initialDurations.push_back(
                initial.value_or(DurationValue{}));
        }
        symbols_.emplace(item.name, Symbol{value, item.span});
        program_.variables.push_back({item.name, value, item.span});
    }

    [[nodiscard]] std::optional<ValueRef> ResolveValue(
        std::string_view name,
        SourceSpan span)
    {
        if (name == "PAUSE") {
            return ValueRef{
                ValueDomain::BuiltinState,
                ValueType::State,
                static_cast<std::uint32_t>(BuiltinState::Pause)};
        }
        if (name == "TAP_DURATION") {
            return ValueRef{
                ValueDomain::BuiltinDuration,
                ValueType::Duration,
                static_cast<std::uint32_t>(BuiltinDuration::TapDuration)};
        }
        if (name == "ACTION_GAP") {
            return ValueRef{
                ValueDomain::BuiltinDuration,
                ValueType::Duration,
                static_cast<std::uint32_t>(BuiltinDuration::ActionGap)};
        }
        const auto found = symbols_.find(std::string(name));
        if (found == symbols_.end()) {
            diagnostics_.Add(
                CompileDiagnosticCode::UnknownValue,
                span,
                "unknown value '" + std::string(name) + "'");
            return std::nullopt;
        }
        return found->second.value;
    }

    [[nodiscard]] std::optional<ControlRef> BindControl(
        const ControlSyntax& syntax)
    {
        if (!syntax.raw) {
            const auto control = ResolveNamedControl(syntax.name);
            if (!control.has_value()) {
                diagnostics_.Add(
                    CompileDiagnosticCode::UnknownControl,
                    syntax.span,
                    "unknown control '" + syntax.name + "'");
            }
            return control;
        }

        const auto reject = [this, &syntax](std::string message) {
            diagnostics_.Add(
                CompileDiagnosticCode::InvalidRawControl,
                syntax.span,
                std::move(message));
            return std::optional<ControlRef>{};
        };
        if (syntax.name == "HID.Usage") {
            if (syntax.arguments.size() != 2U) {
                return reject("HID.Usage requires exactly two numeric arguments");
            }
            const auto page = ParseUnsigned(syntax.arguments[0].text);
            const auto usage = ParseUnsigned(syntax.arguments[1].text);
            if (!page.has_value() || !usage.has_value()
                || *page == 0U || *page > kMaximumHidUsagePage
                || *usage > kMaximumHidUsageId) {
                return reject("HID usage page must be 1..0xFFFF and usage must be 0..0xFFFF");
            }
            return ControlRef{
                kControlNamespaceUsbHid,
                static_cast<std::uint32_t>(*page),
                static_cast<std::uint32_t>(*usage),
                kControlQualifierNone};
        }
        if (syntax.name == "Windows.VirtualKey") {
            if (syntax.arguments.size() != 1U) {
                return reject("Windows.VirtualKey requires exactly one numeric argument");
            }
            const auto code = ParseUnsigned(syntax.arguments[0].text);
            if (!code.has_value() || *code > kMaximumWindowsNativeCode) {
                return reject("Windows virtual-key code must be in 0..0xFF");
            }
            return ControlRef{
                kControlNamespaceWindows,
                kWindowsVirtualKeyFamily,
                static_cast<std::uint32_t>(*code),
                kControlQualifierNone};
        }
        if (syntax.name == "Windows.ScanCode") {
            if (syntax.arguments.empty() || syntax.arguments.size() > 2U) {
                return reject("Windows.ScanCode requires a code and optional E0 or E1 prefix");
            }
            const auto code = ParseUnsigned(syntax.arguments[0].text);
            if (!code.has_value() || *code > kMaximumWindowsNativeCode) {
                return reject("Windows scan code must be in 0..0xFF");
            }
            std::uint32_t qualifier = kControlQualifierNone;
            if (syntax.arguments.size() == 2U) {
                if (syntax.arguments[1].text == "E0") {
                    qualifier = kWindowsScanCodeQualifierE0;
                } else if (syntax.arguments[1].text == "E1") {
                    qualifier = kWindowsScanCodeQualifierE1;
                } else {
                    return reject("Windows scan-code prefix must be E0 or E1");
                }
            }
            return ControlRef{
                kControlNamespaceWindows,
                kWindowsScanCodeFamily,
                static_cast<std::uint32_t>(*code),
                qualifier};
        }
        if (syntax.name == "Linux.Key") {
            if (syntax.arguments.size() != 1U) {
                return reject("Linux.Key requires exactly one numeric argument");
            }
            const auto code = ParseUnsigned(syntax.arguments[0].text);
            if (!code.has_value() || *code > kMaximumLinuxEvKeyCode) {
                return reject("Linux key code must be in 0..0x2FF");
            }
            return ControlRef{
                kControlNamespaceLinux,
                kLinuxEvKeyFamily,
                static_cast<std::uint32_t>(*code),
                kControlQualifierNone};
        }
        if (syntax.name == "MacOS.KeyCode") {
            if (syntax.arguments.size() != 1U) {
                return reject("MacOS.KeyCode requires exactly one numeric argument");
            }
            const auto code = ParseUnsigned(syntax.arguments[0].text);
            if (!code.has_value() || *code > kMaximumMacOsKeyCode) {
                return reject("macOS key code must be in 0..0xFFFF");
            }
            return ControlRef{
                kControlNamespaceMacOs,
                kMacOsKeyCodeFamily,
                static_cast<std::uint32_t>(*code),
                kControlQualifierNone};
        }
        return reject("unknown raw control constructor");
    }

    [[nodiscard]] std::unique_ptr<BoundExpression> BindExpression(
        const ExpressionSyntax& syntax)
    {
        switch (syntax.kind) {
        case ExpressionSyntax::Kind::StateLiteral: {
            auto expression = std::make_unique<BoundExpression>();
            expression->kind = BoundExpression::Kind::StateConstant;
            expression->type = ExpressionType::State;
            expression->span = syntax.span;
            expression->stateValue = syntax.stateValue ? 1U : 0U;
            expression->constant = expression->stateValue;
            return expression;
        }
        case ExpressionSyntax::Kind::NumberLiteral: {
            const auto value = ParseNumber(syntax.text);
            if (!value.has_value()) {
                diagnostics_.Add(
                    CompileDiagnosticCode::InvalidNumber,
                    syntax.span,
                    "number literal must be a finite binary64 value");
                return MakeErrorExpression(syntax.span);
            }
            auto expression = std::make_unique<BoundExpression>();
            expression->kind = BoundExpression::Kind::NumberConstant;
            expression->type = ExpressionType::Number;
            expression->span = syntax.span;
            expression->numberValue = *value;
            expression->constant = *value;
            return expression;
        }
        case ExpressionSyntax::Kind::DurationLiteral: {
            const auto value = ParseDuration(syntax.text);
            if (!value.has_value()) {
                diagnostics_.Add(
                    CompileDiagnosticCode::InvalidDuration,
                    syntax.span,
                    "duration literal is out of range or not exactly representable in nanoseconds");
                return MakeErrorExpression(syntax.span);
            }
            auto expression = std::make_unique<BoundExpression>();
            expression->kind = BoundExpression::Kind::DurationConstant;
            expression->type = ExpressionType::Duration;
            expression->span = syntax.span;
            expression->durationValue = *value;
            expression->constant = *value;
            return expression;
        }
        case ExpressionSyntax::Kind::ValueReference: {
            const auto value = ResolveValue(syntax.text, syntax.span);
            if (!value.has_value()) {
                return MakeErrorExpression(syntax.span);
            }
            auto expression = std::make_unique<BoundExpression>();
            expression->kind = BoundExpression::Kind::LoadValue;
            expression->type = ToExpressionType(value->type);
            expression->span = syntax.span;
            expression->value = *value;
            return expression;
        }
        case ExpressionSyntax::Kind::StateQuery:
            return BindStateQuery(syntax);
        case ExpressionSyntax::Kind::Unary:
            return BindUnary(syntax);
        case ExpressionSyntax::Kind::Binary:
            return BindBinary(syntax);
        }
        return MakeErrorExpression(syntax.span);
    }

    [[nodiscard]] std::unique_ptr<BoundExpression> BindStateQuery(
        const ExpressionSyntax& syntax)
    {
        if (syntax.text == "held" || syntax.text == "idle") {
            const auto control = BindControl(syntax.querySubject);
            if (!control.has_value()) {
                return MakeErrorExpression(syntax.span);
            }
            auto held = std::make_unique<BoundExpression>();
            held->kind = BoundExpression::Kind::ReadControlHeld;
            held->type = ExpressionType::Boolean;
            held->span = syntax.span;
            held->control = *control;
            if (syntax.text == "held") {
                return held;
            }
            auto idle = std::make_unique<BoundExpression>();
            idle->kind = BoundExpression::Kind::Unary;
            idle->type = ExpressionType::Boolean;
            idle->span = syntax.span;
            idle->unary = UnaryOperator::BooleanNot;
            idle->left = std::move(held);
            return idle;
        }
        if (syntax.text == "on" || syntax.text == "off") {
            if (syntax.querySubject.raw
                || syntax.querySubject.name.find('.') != std::string::npos) {
                diagnostics_.Add(
                    CompileDiagnosticCode::TypeMismatch,
                    syntax.span,
                    "on/off queries require a state value");
                return MakeErrorExpression(syntax.span);
            }
            const auto value = ResolveValue(
                syntax.querySubject.name,
                syntax.querySubject.span);
            if (!value.has_value()) {
                return MakeErrorExpression(syntax.span);
            }
            if (value->type != ValueType::State) {
                diagnostics_.Add(
                    CompileDiagnosticCode::TypeMismatch,
                    syntax.span,
                    "on/off queries require a state value");
                return MakeErrorExpression(syntax.span);
            }
            auto load = std::make_unique<BoundExpression>();
            load->kind = BoundExpression::Kind::LoadValue;
            load->type = ExpressionType::State;
            load->span = syntax.querySubject.span;
            load->value = *value;
            auto expected = std::make_unique<BoundExpression>();
            expected->kind = BoundExpression::Kind::StateConstant;
            expected->type = ExpressionType::State;
            expected->span = syntax.span;
            expected->stateValue = syntax.text == "on" ? 1U : 0U;
            expected->constant = expected->stateValue;
            auto equality = std::make_unique<BoundExpression>();
            equality->kind = BoundExpression::Kind::Binary;
            equality->type = ExpressionType::Boolean;
            equality->span = syntax.span;
            equality->binary = BinaryOperator::Equal;
            equality->left = std::move(load);
            equality->right = std::move(expected);
            return equality;
        }
        diagnostics_.Add(
            CompileDiagnosticCode::TypeMismatch,
            syntax.span,
            "state predicate must be held, idle, on, or off");
        return MakeErrorExpression(syntax.span);
    }

    [[nodiscard]] std::unique_ptr<BoundExpression> BindUnary(
        const ExpressionSyntax& syntax)
    {
        auto operand = BindExpression(*syntax.left);
        auto expression = std::make_unique<BoundExpression>();
        expression->kind = BoundExpression::Kind::Unary;
        expression->span = syntax.span;
        expression->left = std::move(operand);
        if (syntax.text == "not") {
            if (expression->left->type != ExpressionType::Boolean) {
                ReportType(syntax.span, "not requires a Boolean operand");
                expression->type = ExpressionType::None;
                return expression;
            }
            expression->type = ExpressionType::Boolean;
            expression->unary = UnaryOperator::BooleanNot;
            if (const bool* value = std::get_if<bool>(&expression->left->constant)) {
                expression->constant = !*value;
            }
            return expression;
        }
        if (expression->left->type != ExpressionType::Number) {
            ReportType(syntax.span, "unary plus and minus require a Number operand");
            expression->type = ExpressionType::None;
            return expression;
        }
        expression->type = ExpressionType::Number;
        expression->unary = syntax.text == "+"
            ? UnaryOperator::NumberIdentity
            : UnaryOperator::NumberNegate;
        if (const double* value = std::get_if<double>(&expression->left->constant)) {
            const double result = syntax.text == "+" ? *value : -*value;
            if (!std::isfinite(result)) {
                ReportConstantFault(syntax.span);
            } else {
                expression->constant = result == 0.0 ? 0.0 : result;
            }
        }
        return expression;
    }

    [[nodiscard]] std::unique_ptr<BoundExpression> BindBinary(
        const ExpressionSyntax& syntax)
    {
        auto left = BindExpression(*syntax.left);
        const bool logical = syntax.text == "and" || syntax.text == "or";
        const bool* leftValue = logical
            ? std::get_if<bool>(&left->constant)
            : nullptr;
        const bool rightUnreachable = leftValue != nullptr
            && ((syntax.text == "and" && !*leftValue)
                || (syntax.text == "or" && *leftValue));
        std::unique_ptr<BoundExpression> right;
        if (rightUnreachable) {
            ++suppressedConstantFaultDepth_;
            right = BindExpression(*syntax.right);
            --suppressedConstantFaultDepth_;
        } else {
            right = BindExpression(*syntax.right);
        }
        auto expression = std::make_unique<BoundExpression>();
        expression->span = syntax.span;
        expression->left = std::move(left);
        expression->right = std::move(right);
        const ExpressionType leftType = expression->left->type;
        const ExpressionType rightType = expression->right->type;
        if (leftType == ExpressionType::None || rightType == ExpressionType::None) {
            expression->type = ExpressionType::None;
            return expression;
        }

        if (syntax.text == "and" || syntax.text == "or") {
            expression->kind = syntax.text == "and"
                ? BoundExpression::Kind::LogicalAnd
                : BoundExpression::Kind::LogicalOr;
            if (leftType != ExpressionType::Boolean
                || rightType != ExpressionType::Boolean) {
                ReportType(syntax.span, "and/or require Boolean operands");
                expression->type = ExpressionType::None;
                return expression;
            }
            expression->type = ExpressionType::Boolean;
            leftValue = std::get_if<bool>(&expression->left->constant);
            const bool* rightValue = std::get_if<bool>(&expression->right->constant);
            if (rightUnreachable) {
                expression->constant = *leftValue;
            } else if (leftValue != nullptr && rightValue != nullptr) {
                expression->constant = syntax.text == "and"
                    ? (*leftValue && *rightValue)
                    : (*leftValue || *rightValue);
            }
            return expression;
        }

        expression->kind = BoundExpression::Kind::Binary;
        if (!SelectBinaryOperator(
                syntax.text,
                leftType,
                rightType,
                expression->binary,
                expression->type)) {
            ReportType(
                syntax.span,
                "operator '" + syntax.text + "' does not accept these operand types");
            expression->type = ExpressionType::None;
            return expression;
        }
        EvaluateConstantBinary(*expression);
        return expression;
    }

    [[nodiscard]] bool SelectBinaryOperator(
        std::string_view operation,
        ExpressionType left,
        ExpressionType right,
        BinaryOperator& result,
        ExpressionType& output) const noexcept
    {
        output = ExpressionType::None;
        if (operation == "==" || operation == "!=") {
            if (left != right
                || (left != ExpressionType::State
                    && left != ExpressionType::Number
                    && left != ExpressionType::Duration)) {
                return false;
            }
            result = operation == "=="
                ? BinaryOperator::Equal
                : BinaryOperator::NotEqual;
            output = ExpressionType::Boolean;
            return true;
        }
        if (operation == "<" || operation == "<="
            || operation == ">" || operation == ">=") {
            if (left != ExpressionType::Number || right != ExpressionType::Number) {
                return false;
            }
            if (operation == "<") result = BinaryOperator::NumberLess;
            if (operation == "<=") result = BinaryOperator::NumberLessEqual;
            if (operation == ">") result = BinaryOperator::NumberGreater;
            if (operation == ">=") result = BinaryOperator::NumberGreaterEqual;
            output = ExpressionType::Boolean;
            return true;
        }
        if (operation == "+" || operation == "-") {
            if (left == ExpressionType::Number && right == ExpressionType::Number) {
                result = operation == "+"
                    ? BinaryOperator::NumberAdd
                    : BinaryOperator::NumberSubtract;
                output = ExpressionType::Number;
                return true;
            }
            if (left == ExpressionType::Duration && right == ExpressionType::Duration) {
                result = operation == "+"
                    ? BinaryOperator::DurationAdd
                    : BinaryOperator::DurationSubtract;
                output = ExpressionType::Duration;
                return true;
            }
            return false;
        }
        if (operation == "*") {
            if (left == ExpressionType::Number && right == ExpressionType::Number) {
                result = BinaryOperator::NumberMultiply;
                output = ExpressionType::Number;
                return true;
            }
            if (left == ExpressionType::Duration && right == ExpressionType::Number) {
                result = BinaryOperator::DurationMultiplyNumber;
                output = ExpressionType::Duration;
                return true;
            }
            if (left == ExpressionType::Number && right == ExpressionType::Duration) {
                result = BinaryOperator::NumberMultiplyDuration;
                output = ExpressionType::Duration;
                return true;
            }
            return false;
        }
        if (operation == "/") {
            if (left == ExpressionType::Number && right == ExpressionType::Number) {
                result = BinaryOperator::NumberDivide;
                output = ExpressionType::Number;
                return true;
            }
            if (left == ExpressionType::Duration && right == ExpressionType::Number) {
                result = BinaryOperator::DurationDivideNumber;
                output = ExpressionType::Duration;
                return true;
            }
            return false;
        }
        if (operation == "%"
            && left == ExpressionType::Number
            && right == ExpressionType::Number) {
            result = BinaryOperator::NumberModulo;
            output = ExpressionType::Number;
            return true;
        }
        return false;
    }

    void EvaluateConstantBinary(BoundExpression& expression)
    {
        const double* leftNumber = std::get_if<double>(&expression.left->constant);
        const double* rightNumber = std::get_if<double>(&expression.right->constant);
        const DurationValue* leftDuration = std::get_if<DurationValue>(
            &expression.left->constant);
        const DurationValue* rightDuration = std::get_if<DurationValue>(
            &expression.right->constant);
        const std::uint8_t* leftState = std::get_if<std::uint8_t>(
            &expression.left->constant);
        const std::uint8_t* rightState = std::get_if<std::uint8_t>(
            &expression.right->constant);

        switch (expression.binary) {
        case BinaryOperator::NumberAdd:
        case BinaryOperator::NumberSubtract:
        case BinaryOperator::NumberMultiply:
        case BinaryOperator::NumberDivide:
        case BinaryOperator::NumberModulo:
            if (leftNumber != nullptr && rightNumber != nullptr) {
                if ((expression.binary == BinaryOperator::NumberDivide
                     || expression.binary == BinaryOperator::NumberModulo)
                    && *rightNumber == 0.0) {
                    ReportConstantFault(expression.span);
                    return;
                }
                double value = 0.0;
                if (expression.binary == BinaryOperator::NumberAdd) {
                    value = *leftNumber + *rightNumber;
                } else if (expression.binary == BinaryOperator::NumberSubtract) {
                    value = *leftNumber - *rightNumber;
                } else if (expression.binary == BinaryOperator::NumberMultiply) {
                    value = *leftNumber * *rightNumber;
                } else if (expression.binary == BinaryOperator::NumberDivide) {
                    value = *leftNumber / *rightNumber;
                } else {
                    value = std::fmod(*leftNumber, *rightNumber);
                }
                if (!std::isfinite(value)) {
                    ReportConstantFault(expression.span);
                } else {
                    expression.constant = value == 0.0 ? 0.0 : value;
                }
            }
            return;
        case BinaryOperator::DurationAdd:
        case BinaryOperator::DurationSubtract:
            if (leftDuration != nullptr && rightDuration != nullptr) {
                if (expression.binary == BinaryOperator::DurationAdd) {
                    if (rightDuration->nanoseconds
                        > std::numeric_limits<std::int64_t>::max()
                            - leftDuration->nanoseconds) {
                        ReportConstantFault(expression.span);
                        return;
                    }
                    expression.constant = DurationValue{
                        leftDuration->nanoseconds + rightDuration->nanoseconds};
                } else {
                    expression.constant = DurationValue{(std::max)(
                        std::int64_t{0},
                        leftDuration->nanoseconds - rightDuration->nanoseconds)};
                }
            }
            return;
        case BinaryOperator::DurationMultiplyNumber:
        case BinaryOperator::DurationDivideNumber:
            if (leftDuration != nullptr && rightNumber != nullptr) {
                EvaluateDurationScale(
                    expression,
                    leftDuration->nanoseconds,
                    *rightNumber,
                    expression.binary == BinaryOperator::DurationDivideNumber);
            }
            return;
        case BinaryOperator::NumberMultiplyDuration:
            if (leftNumber != nullptr && rightDuration != nullptr) {
                EvaluateDurationScale(
                    expression,
                    rightDuration->nanoseconds,
                    *leftNumber,
                    false);
            }
            return;
        case BinaryOperator::Equal:
        case BinaryOperator::NotEqual: {
            std::optional<bool> equal;
            if (leftNumber != nullptr && rightNumber != nullptr) {
                equal = *leftNumber == *rightNumber;
            } else if (leftDuration != nullptr && rightDuration != nullptr) {
                equal = *leftDuration == *rightDuration;
            } else if (leftState != nullptr && rightState != nullptr) {
                equal = *leftState == *rightState;
            }
            if (equal.has_value()) {
                expression.constant = expression.binary == BinaryOperator::Equal
                    ? *equal
                    : !*equal;
            }
            return;
        }
        case BinaryOperator::NumberLess:
        case BinaryOperator::NumberLessEqual:
        case BinaryOperator::NumberGreater:
        case BinaryOperator::NumberGreaterEqual:
            if (leftNumber != nullptr && rightNumber != nullptr) {
                bool value = false;
                if (expression.binary == BinaryOperator::NumberLess) {
                    value = *leftNumber < *rightNumber;
                } else if (expression.binary == BinaryOperator::NumberLessEqual) {
                    value = *leftNumber <= *rightNumber;
                } else if (expression.binary == BinaryOperator::NumberGreater) {
                    value = *leftNumber > *rightNumber;
                } else {
                    value = *leftNumber >= *rightNumber;
                }
                expression.constant = value;
            }
            return;
        }
    }

    void EvaluateDurationScale(
        BoundExpression& expression,
        std::int64_t duration,
        double number,
        bool divide)
    {
        if (divide && number == 0.0) {
            ReportConstantFault(expression.span);
            return;
        }
        const long double scaled = divide
            ? static_cast<long double>(duration) / static_cast<long double>(number)
            : static_cast<long double>(duration) * static_cast<long double>(number);
        if (!std::isfinite(scaled)
            || scaled > static_cast<long double>(
                std::numeric_limits<std::int64_t>::max())) {
            ReportConstantFault(expression.span);
            return;
        }
        const long double clamped = (std::max)(0.0L, scaled);
        expression.constant = DurationValue{
            static_cast<std::int64_t>(std::trunc(clamped))};
    }

    void ReportType(SourceSpan span, std::string message)
    {
        diagnostics_.Add(
            CompileDiagnosticCode::TypeMismatch,
            span,
            std::move(message));
    }

    void ReportConstantFault(SourceSpan span)
    {
        if (suppressedConstantFaultDepth_ != 0U) {
            return;
        }
        diagnostics_.Add(
            CompileDiagnosticCode::ConstantEvaluation,
            span,
            "constant expression would fault at runtime");
    }

    [[nodiscard]] std::optional<EventTransition> BindTransition(
        std::string_view transition,
        SourceSpan span)
    {
        if (transition == "down") return EventTransition::Down;
        if (transition == "repeat") return EventTransition::Repeat;
        if (transition == "up") return EventTransition::Up;
        diagnostics_.Add(
            CompileDiagnosticCode::TypeMismatch,
            span,
            "event transition must be down, repeat, or up");
        return std::nullopt;
    }

    [[nodiscard]] std::vector<BoundAction> BindActions(
        const std::vector<ActionSyntax>& syntax)
    {
        std::vector<BoundAction> actions;
        actions.reserve(syntax.size());
        for (const ActionSyntax& item : syntax) {
            BoundAction action{};
            action.span = item.span;
            switch (item.kind) {
            case ActionSyntax::Kind::Input: {
                const auto control = BindControl(item.control);
                if (control.has_value()) {
                    action.control = *control;
                }
                if (item.name == "press") action.kind = BoundAction::Kind::Press;
                if (item.name == "release") action.kind = BoundAction::Kind::Release;
                if (item.name == "tap") action.kind = BoundAction::Kind::Tap;
                break;
            }
            case ActionSyntax::Kind::Wait:
                action.kind = BoundAction::Kind::Wait;
                action.expression = BindExpression(*item.expression);
                RequireType(*action.expression, ExpressionType::Duration, item.span,
                    "wait requires a Duration expression");
                break;
            case ActionSyntax::Kind::Gap:
                action.kind = BoundAction::Kind::Gap;
                break;
            case ActionSyntax::Kind::Set: {
                action.kind = BoundAction::Kind::Set;
                const auto value = ResolveValue(item.name, item.span);
                if (value.has_value()) {
                    action.value = *value;
                    if (!IsWritable(*value)) {
                        diagnostics_.Add(
                            CompileDiagnosticCode::ReadOnlyValue,
                            item.span,
                            "set target must be a user variable");
                    }
                }
                action.expression = BindExpression(*item.expression);
                if (value.has_value()) {
                    RequireType(
                        *action.expression,
                        ToExpressionType(value->type),
                        item.span,
                        "set expression type must match its target");
                }
                break;
            }
            case ActionSyntax::Kind::Toggle: {
                action.kind = BoundAction::Kind::Toggle;
                const auto value = ResolveValue(item.name, item.span);
                if (value.has_value()) {
                    action.value = *value;
                    if (!IsWritable(*value)) {
                        diagnostics_.Add(
                            CompileDiagnosticCode::ReadOnlyValue,
                            item.span,
                            "toggle target must be a user state variable");
                    } else if (value->type != ValueType::State) {
                        ReportType(item.span, "toggle target must have type State");
                    }
                }
                break;
            }
            case ActionSyntax::Kind::Exec:
                action.kind = BoundAction::Kind::Exec;
                action.command = item.stringValue;
                if (action.command.empty()) {
                    diagnostics_.Add(
                        CompileDiagnosticCode::EmptyString,
                        item.span,
                        "exec command must not be empty");
                } else if (action.command.find('\0') != std::string::npos) {
                    diagnostics_.Add(
                        CompileDiagnosticCode::EmbeddedNul,
                        item.span,
                        "exec command contains an embedded NUL");
                }
                break;
            case ActionSyntax::Kind::If:
                action.kind = BoundAction::Kind::If;
                action.expression = BindExpression(*item.expression);
                RequireType(*action.expression, ExpressionType::Boolean, item.span,
                    "if condition must be Boolean");
                action.body = BindActions(item.body);
                action.alternative = BindActions(item.alternative);
                break;
            case ActionSyntax::Kind::Repeat:
                action.kind = BoundAction::Kind::Repeat;
                action.expression = BindExpression(*item.expression);
                RequireType(*action.expression, ExpressionType::Number, item.span,
                    "repeat limit must have type Number");
                action.body = BindActions(item.body);
                break;
            case ActionSyntax::Kind::While:
                action.kind = BoundAction::Kind::While;
                action.expression = BindExpression(*item.expression);
                RequireType(*action.expression, ExpressionType::Boolean, item.span,
                    "while condition must be Boolean");
                action.body = BindActions(item.body);
                break;
            }
            actions.push_back(std::move(action));
        }
        return actions;
    }

    void RequireType(
        const BoundExpression& expression,
        ExpressionType expected,
        SourceSpan span,
        std::string message)
    {
        if (expression.type != ExpressionType::None
            && expression.type != expected) {
            ReportType(span, std::move(message));
        }
    }

    void BindRule(const TopLevelSyntax& item)
    {
        const std::uint32_t ordinal = nextSourceOrdinal_++;
        BoundRule rule{};
        rule.sourceOrdinal = ordinal;
        rule.sourceSpan = item.span;
        rule.actionFlowSpan = item.actionFlowSpan;
        if (item.kind == TopLevelSyntax::Kind::Mapping) {
            rule.kind = BoundRule::Kind::Mapping;
            const auto source = BindControl(item.sourceControl);
            const auto target = BindControl(item.targetControl);
            if (source.has_value()) rule.source = *source;
            if (target.has_value()) rule.target = *target;
            rule.transition = EventTransition::Down;
            if (item.condition != nullptr) {
                rule.condition = BindExpression(*item.condition);
                RequireType(*rule.condition, ExpressionType::Boolean, item.condition->span,
                    "mapping condition must be Boolean");
            }
        } else {
            const auto source = BindControl(item.event.control);
            const auto transition = BindTransition(item.event.transition, item.event.span);
            if (source.has_value()) rule.source = *source;
            if (transition.has_value()) rule.transition = *transition;
            if (item.condition != nullptr) {
                rule.condition = BindExpression(*item.condition);
                RequireType(*rule.condition, ExpressionType::Boolean, item.condition->span,
                    "rule condition must be Boolean");
            }
            rule.delivery = item.arrow == TokenKind::ConsumeStop
                    || item.arrow == TokenKind::ConsumeContinue
                ? Delivery::Consume
                : Delivery::Observe;
            rule.flow = item.arrow == TokenKind::ConsumeContinue
                    || item.arrow == TokenKind::ObserveContinue
                ? MatchFlow::Continue
                : MatchFlow::Stop;
            if (item.kind == TopLevelSyntax::Kind::PauseRule) {
                rule.kind = BoundRule::Kind::Pause;
                if (item.pauseEffect == "on") rule.pauseEffect = PauseEffect::On;
                if (item.pauseEffect == "off") rule.pauseEffect = PauseEffect::Off;
                if (item.pauseEffect == "toggle") rule.pauseEffect = PauseEffect::Toggle;
            } else {
                rule.kind = BoundRule::Kind::Event;
                rule.actions = BindActions(item.actions);
            }
        }
        program_.rules.push_back(std::move(rule));
    }

    const SourceFile& source_;
    const SyntaxTree& syntax_;
    DiagnosticSink& diagnostics_;
    BoundProgram program_;
    std::map<std::string, Symbol, std::less<>> symbols_;
    std::optional<SourceSpan> targetSetting_;
    std::optional<SourceSpan> tapDurationSetting_;
    std::optional<SourceSpan> actionGapSetting_;
    std::uint32_t nextSourceOrdinal_{};
    std::uint32_t suppressedConstantFaultDepth_{};
};

} // namespace

std::optional<BoundProgram> BindProgram(
    const SourceFile& source,
    const SyntaxTree& syntax,
    DiagnosticSink& diagnostics)
{
    return Binder(source, syntax, diagnostics).Run();
}

} // namespace inputweaver::compiler
