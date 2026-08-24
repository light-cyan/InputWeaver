#include "compiler/compiler.hpp"
#include "compiler/control_catalog.hpp"
#include "compiler/frontend.hpp"
#include "compiler/source.hpp"
#include "compiled_program_fixtures.hpp"

#include "program/compiled_program.hpp"
#include "program/program_dump.hpp"
#include "program/weavec_codec.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
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

[[nodiscard]] bool HasDiagnostic(
    const std::vector<inputweaver::compiler::CompileDiagnostic>& diagnostics,
    inputweaver::compiler::CompileDiagnosticCode code) noexcept
{
    return std::any_of(
        diagnostics.begin(),
        diagnostics.end(),
        [code](const auto& diagnostic) { return diagnostic.code == code; });
}

[[nodiscard]] inputweaver::compiler::CompileOutput CompileGood(
    std::string displayPath,
    std::string source,
    std::string_view name)
{
    inputweaver::compiler::CompileOutput output =
        inputweaver::compiler::CompileSource(
            std::move(displayPath),
            std::move(source));
    if (!output.diagnostics.empty()) {
        std::cerr << "Unexpected diagnostics for " << name << ":\n";
        for (const auto& diagnostic : output.diagnostics) {
            std::cerr << inputweaver::compiler::FormatCompileDiagnostic(diagnostic);
        }
    }
    Check(output.Succeeded(), std::string(name) + " compiles");
    return output;
}

[[nodiscard]] std::shared_ptr<const inputweaver::CompiledProgram> DecodeGood(
    const inputweaver::compiler::CompileOutput& output,
    std::string_view name)
{
    const inputweaver::DecodeWeavecResult decoded = inputweaver::DecodeWeavec(
        output.artifact);
    Check(!decoded.decodeError.has_value(), std::string(name) + " decodes");
    Check(decoded.validationErrors.empty(), std::string(name) + " validates");
    Check(decoded.program != nullptr, std::string(name) + " returns a program");
    return decoded.program;
}

[[nodiscard]] std::shared_ptr<const inputweaver::CompiledProgram> FinalizeFixture(
    inputweaver::CompiledProgramStorage storage,
    std::string_view name)
{
    inputweaver::FinalizeResult result = inputweaver::FinalizeCompiledProgram(
        std::move(storage));
    Check(result.errors.empty(), std::string(name) + " fixture validates");
    Check(result.program != nullptr, std::string(name) + " fixture finalizes");
    return result.program;
}

void TestSourceAndLexer()
{
    using namespace inputweaver;
    using namespace inputweaver::compiler;
    const std::string sourceBytes =
        "// \xe4\xb8\xad\xe6\x96\x87\r\n"
        "TARGET = GLOBAL;\r"
        "F1:down =>>; F1:up ~>>; F2:down =>; F2:up ~>;";
    DiagnosticSink diagnostics;
    const std::optional<SourceFile> source = MakeSourceFile(
        "lexer.weave",
        sourceBytes,
        {},
        diagnostics);
    Check(source.has_value(), "valid UTF-8 source loads");
    if (!source.has_value()) {
        return;
    }
    Check(source->lineStarts.size() == 3U, "CRLF and CR line starts are derived");
    diagnostics.SetSource(&*source);
    const std::vector<Token> tokens = LexSource(*source, {}, diagnostics);
    Check(!diagnostics.HasErrors(), "UTF-8 comments are accepted");
    Check(std::count_if(tokens.begin(), tokens.end(), [](const Token& token) {
        return token.kind == TokenKind::ConsumeContinue;
    }) == 1, "consume-continue arrow uses longest match");
    Check(std::count_if(tokens.begin(), tokens.end(), [](const Token& token) {
        return token.kind == TokenKind::ObserveContinue;
    }) == 1, "observe-continue arrow uses longest match");
    Check(std::count_if(tokens.begin(), tokens.end(), [](const Token& token) {
        return token.kind == TokenKind::ConsumeStop;
    }) == 1, "consume-stop arrow tokenizes");
    Check(std::count_if(tokens.begin(), tokens.end(), [](const Token& token) {
        return token.kind == TokenKind::ObserveStop;
    }) == 1, "observe-stop arrow tokenizes");

    std::string invalidUtf8 = "// ";
    invalidUtf8.push_back(static_cast<char>(0xc0U));
    invalidUtf8.push_back(static_cast<char>(0xafU));
    const CompileOutput invalid = CompileSource(
        "invalid-utf8.weave",
        invalidUtf8);
    Check(HasDiagnostic(invalid.diagnostics, CompileDiagnosticCode::InvalidUtf8),
        "invalid UTF-8 is rejected");

    const CompileOutput nonAsciiSyntax = CompileSource(
        "non-ascii.weave",
        "\xe7\x8a\xb6\xe6\x80\x81 = GLOBAL;");
    Check(HasDiagnostic(
        nonAsciiSyntax.diagnostics,
        CompileDiagnosticCode::NonAsciiSyntax),
        "non-ASCII syntax is rejected");

    const CompileOutput comments = CompileSource(
        "comments.weave",
        "/* outer /* nested */ outer */ TARGET = GLOBAL;");
    Check(HasDiagnostic(
        comments.diagnostics,
        CompileDiagnosticCode::NestedBlockComment),
        "nested block comments are rejected");
    const CompileOutput unterminatedComment = CompileSource(
        "comment.weave",
        "/* unterminated");
    Check(HasDiagnostic(
        unterminatedComment.diagnostics,
        CompileDiagnosticCode::UnterminatedBlockComment),
        "unterminated block comments are rejected");
    const CompileOutput unterminatedString = CompileSource(
        "string.weave",
        "TARGET = \"unterminated;");
    Check(HasDiagnostic(
        unterminatedString.diagnostics,
        CompileDiagnosticCode::UnterminatedString),
        "unterminated strings are rejected");

    DiagnosticSink escapedDiagnostics;
    const std::optional<SourceFile> escapedSource = MakeSourceFile(
        "escaped.weave",
        R"weave(TARGET = "a\\b\"c\nd\re\tf";)weave",
        {},
        escapedDiagnostics);
    Check(escapedSource.has_value(), "escaped string source loads");
    if (escapedSource.has_value()) {
        escapedDiagnostics.SetSource(&*escapedSource);
        const std::vector<Token> escapedTokens = LexSource(
            *escapedSource,
            {},
            escapedDiagnostics);
        const auto token = std::find_if(
            escapedTokens.begin(),
            escapedTokens.end(),
            [](const Token& candidate) { return candidate.kind == TokenKind::String; });
        Check(!escapedDiagnostics.HasErrors()
                && token != escapedTokens.end()
                && token->text == "a\\b\"c\nd\re\tf",
            "the complete cooked string escape set decodes exactly");
    }
    const CompileOutput invalidEscape = CompileSource(
        "escape.weave",
        "TARGET = \"bad\\q\";");
    Check(HasDiagnostic(
        invalidEscape.diagnostics,
        CompileDiagnosticCode::InvalidEscape),
        "unknown string escapes are rejected");

    const CompileOutput cooked = CompileGood(
        "cooked.weave",
        R"weave(TARGET = "C:\\Tools\\game.exe";
F1:down => exec("tool.exe\t--x\nnext\r\"q\"");
)weave",
        "cooked string artifact");
    const auto cookedProgram = DecodeGood(cooked, "cooked string artifact");
    if (cookedProgram != nullptr) {
        const StringId target = cookedProgram->Settings().target.text;
        Check(target.IsValid()
                && cookedProgram->Strings()[target.value] == "C:\\Tools\\game.exe",
            "cooked target bytes are preserved in the artifact");
        const auto exec = std::find_if(
            cookedProgram->ActionCode().begin(),
            cookedProgram->ActionCode().end(),
            [](const ActionInstruction& instruction) {
                return instruction.opcode == ActionOpcode::Exec;
            });
        Check(exec != cookedProgram->ActionCode().end()
                && cookedProgram->Strings()[exec->operand0]
                    == "tool.exe\t--x\nnext\r\"q\"",
            "cooked exec bytes are preserved in the artifact");
    }

    CompilerLimits tokenLimits;
    tokenLimits.maximumTokens = 4U;
    const CompileOutput limited = CompileSource(
        "tokens.weave",
        "TARGET = GLOBAL;",
        tokenLimits);
    Check(HasDiagnostic(limited.diagnostics, CompileDiagnosticCode::TokenLimit),
        "token growth is bounded");

    CompilerLimits nestingLimits;
    nestingLimits.maximumNestingDepth = 4U;
    const CompileOutput nested = CompileSource(
        "nesting.weave",
        "F1:down when 1+1+1+1+1 == 5 =>;",
        nestingLimits);
    Check(HasDiagnostic(
        nested.diagnostics,
        CompileDiagnosticCode::SyntaxNestingLimit),
        "expression tree depth is bounded");

    DiagnosticSink terminalDiagnostics;
    const std::optional<SourceFile> terminalBreak = MakeSourceFile(
        "terminal-break.weave",
        "TARGET = GLOBAL;\n",
        {},
        terminalDiagnostics);
    Check(terminalBreak.has_value(), "source ending in a line break loads");
    if (terminalBreak.has_value()) {
        Check(terminalBreak->lineStarts.size() == 2U
                && terminalBreak->lineStarts.back() == terminalBreak->bytes.size(),
            "terminal line break records the empty final line");
        const auto [line, column] = terminalBreak->LineColumn({
            static_cast<std::uint32_t>(terminalBreak->bytes.size()), 0U});
        Check(line == 2U && column == 1U,
            "end of file after a terminal line break uses the final line");
    }
}

void TestParserAndRecovery()
{
    using namespace inputweaver::compiler;
    const CompileOutput valid = CompileGood(
        "parser.weave",
        "TARGET = GLOBAL;\n"
        "state enabled = on;\n"
        "number count = -2.5;\n"
        "duration delay = 1.5s;\n"
        "A := B when enabled[on];\n"
        "pause Pause:down when LCtrl[held] ~> toggle;\n"
        "F1:down => | tap(A)tap(B) || gap()\n"
        "if enabled[on] then repeat count do tap(C) end else while F1[held] do | end end;\n",
        "complete parser grammar");
    Check(valid.Succeeded(), "adjacent and nested action syntax is accepted");

    const CompileOutput pauseContinue = CompileSource(
        "pause.weave",
        "pause F1:down =>> toggle;");
    Check(HasDiagnostic(
        pauseContinue.diagnostics,
        CompileDiagnosticCode::InvalidPauseRule),
        "pause continuing arrows are rejected");
    const CompileOutput pauseAction = CompileSource(
        "pause-action.weave",
        "pause F1:down => toggle(F2);");
    Check(!pauseAction.Succeeded(), "pause action flows are rejected");

    const CompileOutput recovered = CompileSource(
        "recovery.weave",
        "TARGET GLOBAL; F1 down =>; state value on;");
    Check(recovered.diagnostics.size() >= 3U,
        "parser recovers at top-level semicolons");

    const CompileOutput nestedRecovery = CompileSource(
        "nested-recovery.weave",
        "F1:down => if on then tap() "
        "repeat 2 do wait() end "
        "while on do bogus() end end; F2:down =>;");
    Check(nestedRecovery.diagnostics.size() == 3U,
        "parser recovers three independent nested action errors");
}

void TestBindingDiagnostics()
{
    using namespace inputweaver::compiler;
    struct Case final {
        std::string_view name;
        std::string source;
        CompileDiagnosticCode code;
    };
    const std::vector<Case> cases{
        {"duplicate setting", "TARGET=GLOBAL; TARGET=GLOBAL;",
            CompileDiagnosticCode::DuplicateSetting},
        {"duplicate variable", "state value=on; number value=1;",
            CompileDiagnosticCode::DuplicateSymbol},
        {"reserved variable", "state F1=on;",
            CompileDiagnosticCode::ReservedName},
        {"unknown value", "F1:down when missing[on] =>;",
            CompileDiagnosticCode::UnknownValue},
        {"unknown control", "Unknown.Key:down =>;",
            CompileDiagnosticCode::UnknownControl},
        {"condition type", "F1:down when 1 =>;",
            CompileDiagnosticCode::TypeMismatch},
        {"wait type", "F1:down => wait(1);",
            CompileDiagnosticCode::TypeMismatch},
        {"readonly pause", "F1:down => toggle(PAUSE);",
            CompileDiagnosticCode::ReadOnlyValue},
        {"readonly setting", "F1:down => set(TAP_DURATION, 1ms);",
            CompileDiagnosticCode::ReadOnlyValue},
        {"inexact duration", "duration value=0.0000001ms;",
            CompileDiagnosticCode::InvalidDuration},
        {"setting range", "ACTION_GAP=61s;",
            CompileDiagnosticCode::SettingOutOfRange},
        {"constant divide", "F1:down => wait(1s / 0);",
            CompileDiagnosticCode::ConstantEvaluation},
        {"reachable and fault", "F1:down when (1 == 1) and (1 / 0 > 0) =>;",
            CompileDiagnosticCode::ConstantEvaluation},
        {"reachable or fault", "F1:down when (1 == 2) or (1 / 0 > 0) =>;",
            CompileDiagnosticCode::ConstantEvaluation},
        {"dynamic short-circuit fault",
            "state enabled=on; F1:down when enabled[on] and (1 / 0 > 0) =>;",
            CompileDiagnosticCode::ConstantEvaluation},
        {"empty target", "TARGET=\"\";",
            CompileDiagnosticCode::EmptyString},
        {"empty exec", "F1:down => exec(\"\");",
            CompileDiagnosticCode::EmptyString},
        {"raw arity", "HID.Usage(7):down =>;",
            CompileDiagnosticCode::InvalidRawControl},
        {"raw negative", "Linux.Key(-1):down =>;",
            CompileDiagnosticCode::InvalidRawControl},
        {"raw prefix", "Windows.ScanCode(1, E2):down =>;",
            CompileDiagnosticCode::InvalidRawControl},
    };
    for (const Case& test : cases) {
        const CompileOutput output = CompileSource(
            std::string(test.name) + ".weave",
            test.source);
        Check(!output.Succeeded(), std::string(test.name) + " fails");
        Check(
            HasDiagnostic(output.diagnostics, test.code),
            std::string(test.name) + " has the expected diagnostic family");
        Check(output.artifact.empty(), std::string(test.name) + " emits no artifact");
    }

    std::string embedded = "F1:down => exec(\"a";
    embedded.push_back('\0');
    embedded += "b\");";
    const CompileOutput nul = CompileSource("nul.weave", embedded);
    Check(HasDiagnostic(nul.diagnostics, CompileDiagnosticCode::EmbeddedNul),
        "embedded NUL is rejected");

    const CompileOutput unreachableFaults = CompileGood(
        "unreachable-faults.weave",
        "F1:down when (1 == 2) and (1 / 0 > 0) =>;\n"
        "F2:down when (1 == 1) or (1 / 0 > 0) =>;",
        "unreachable short-circuit constant faults");
    Check(!HasDiagnostic(
            unreachableFaults.diagnostics,
            CompileDiagnosticCode::ConstantEvaluation),
        "constant faults on proven unreachable logical operands are accepted");

    const CompileOutput whitespaceStrings = CompileGood(
        "whitespace-strings.weave",
        "TARGET=\" \"; F1:down => exec(\" \" );",
        "non-empty whitespace strings");
    Check(whitespaceStrings.Succeeded(),
        "non-empty strings remain subject to downstream resolution");

    std::string manyErrors;
    for (std::size_t index = 0U; index < 100U; ++index) {
        manyErrors += "@;";
    }
    const CompileOutput bounded = CompileSource("bounded.weave", manyErrors);
    Check(bounded.diagnostics.size() == kMaximumCompileDiagnostics,
        "compile diagnostics stop at the fixed limit");
}

void TestControlCatalogAndV2()
{
    using namespace inputweaver;
    using namespace inputweaver::compiler;
    const auto shortName = ResolveNamedControl("A");
    const auto qualifiedName = ResolveNamedControl("Keyboard.A");
    Check(shortName.has_value() && qualifiedName.has_value(),
        "short and qualified keyboard controls resolve");
    Check(shortName == qualifiedName, "keyboard aliases share one identity");
    Check(ResolveNamedControl("Consumer.VolumeUp") == std::optional<ControlRef>{
        ControlRef{kControlNamespaceUsbHid, 0x0cU, 0x00e9U, 0U}},
        "consumer control uses its HID identity");
    Check(ResolveNamedControl("Windows.Keyboard.IMEOn").has_value(),
        "Windows catalog entry resolves");
    Check(ResolveNamedControl("Linux.Keyboard.Compose").has_value(),
        "Linux catalog entry resolves");
    Check(ResolveNamedControl("MacOS.Keyboard.Fn").has_value(),
        "macOS catalog entry resolves");
    Check(!ResolveNamedControl("F01").has_value(),
        "noncanonical numeric control spelling is rejected");

    for (char letter = 'A'; letter <= 'Z'; ++letter) {
        const std::string name(1U, letter);
        Check(ResolveNamedControl(name).has_value(), "every letter control resolves");
        Check(ResolveNamedControl("Keyboard." + name).has_value(),
            "every qualified letter control resolves");
    }
    for (unsigned int index = 1U; index <= 24U; ++index) {
        const std::string name = "F" + std::to_string(index);
        Check(ResolveNamedControl(name).has_value(), "every function control resolves");
        Check(ResolveNamedControl("Keyboard." + name).has_value(),
            "every qualified function control resolves");
    }
    for (unsigned int index = 0U; index <= 9U; ++index) {
        const std::string digit = "Digit" + std::to_string(index);
        const std::string numpad = "Numpad" + std::to_string(index);
        Check(ResolveNamedControl(digit).has_value(), "every digit control resolves");
        Check(ResolveNamedControl(numpad).has_value(), "every numpad digit resolves");
    }
    const std::vector<std::string_view> fixedNames{
        "Esc", "Enter", "Space", "Tab", "Backspace", "Delete", "Insert",
        "Home", "End", "PageUp", "PageDown", "ArrowLeft", "ArrowRight",
        "ArrowUp", "ArrowDown", "LCtrl", "RCtrl", "LShift", "RShift",
        "LAlt", "RAlt", "Pause", "CapsLock", "NumLock", "ScrollLock",
        "NumpadAdd", "NumpadSubtract", "NumpadMultiply", "NumpadDivide",
        "NumpadDecimal", "Mouse.Left", "Mouse.Right", "Mouse.Middle",
        "Mouse.X1", "Mouse.X2", "Consumer.PlayPause",
        "Consumer.ScanNextTrack", "Consumer.ScanPreviousTrack", "Consumer.Stop",
        "Consumer.Mute", "Consumer.VolumeUp", "Consumer.VolumeDown",
    };
    for (const std::string_view name : fixedNames) {
        Check(ResolveNamedControl(name).has_value(), "every fixed catalog control resolves");
    }

    const CompileOutput namespaceVariables = CompileGood(
        "namespace-values.weave",
        "state Keyboard=on; state Windows=off; F1:down when Keyboard[on] => toggle(Windows);",
        "namespace-prefix variable names");
    Check(namespaceVariables.Succeeded(),
        "control namespace prefixes remain available as user identifiers");

    const CompileOutput output = CompileGood(
        "controls.weave",
        "A:down => tap(Keyboard.A);\n"
        "HID.Usage(0x000C,0x00E9):down => tap(Consumer.VolumeUp);\n"
        "Windows.VirtualKey(0x41):down => tap(Windows.ScanCode(0x1E,E0));\n"
        "Linux.Key(30):down => tap(MacOS.KeyCode(0));\n",
        "v2 controls");
    const auto program = DecodeGood(output, "v2 controls");
    if (program == nullptr) {
        return;
    }
    Check(program->Controls().size() == 6U,
        "aliases and identical raw controls are canonicalized");
    const auto contains = [&program](ControlRef control) {
        return std::find(program->Controls().begin(), program->Controls().end(), control)
            != program->Controls().end();
    };
    Check(contains({kControlNamespaceWindows, kWindowsVirtualKeyFamily, 0x41U, 0U}),
        "raw Windows virtual key lowers");
    Check(contains({kControlNamespaceWindows, kWindowsScanCodeFamily, 0x1eU,
            kWindowsScanCodeQualifierE0}),
        "raw Windows scan code lowers");
    Check(contains({kControlNamespaceLinux, kLinuxEvKeyFamily, 30U, 0U}),
        "raw Linux key lowers");
    Check(contains({kControlNamespaceMacOs, kMacOsKeyCodeFamily, 0U, 0U}),
        "raw macOS key lowers");

    const CompileOutput boundaries = CompileGood(
        "raw-boundaries.weave",
        "HID.Usage(1,0):down =>;\n"
        "HID.Usage(0xFFFF,0xFFFF):down =>;\n"
        "Windows.VirtualKey(0):down =>;\n"
        "Windows.VirtualKey(0xFF):down =>;\n"
        "Windows.ScanCode(0):down =>;\n"
        "Windows.ScanCode(0xFF,E0):down =>;\n"
        "Windows.ScanCode(0xFF,E1):down =>;\n"
        "Linux.Key(0):down =>;\n"
        "Linux.Key(0x2FF):down =>;\n"
        "MacOS.KeyCode(0):down =>;\n"
        "MacOS.KeyCode(0xFFFF):down =>;",
        "raw control storage boundaries");
    Check(DecodeGood(boundaries, "raw control storage boundaries") != nullptr,
        "all frozen raw control boundary values compile and validate");

    constexpr std::array<std::string_view, 7U> invalidRawControls{
        "HID.Usage(0,0):down =>;",
        "HID.Usage(0x10000,0):down =>;",
        "HID.Usage(1,0x10000):down =>;",
        "Windows.VirtualKey(0x100):down =>;",
        "Windows.ScanCode(0x100):down =>;",
        "Linux.Key(0x300):down =>;",
        "MacOS.KeyCode(0x10000):down =>;",
    };
    for (const std::string_view invalidRawControl : invalidRawControls) {
        const CompileOutput invalidRaw = CompileSource(
            "invalid-raw-boundary.weave",
            std::string(invalidRawControl));
        Check(HasDiagnostic(
                invalidRaw.diagnostics,
                CompileDiagnosticCode::InvalidRawControl)
                && invalidRaw.artifact.empty(),
            "out-of-domain raw controls are rejected before artifact emission");
    }
}

void TestLoweringCoverage()
{
    using namespace inputweaver;
    const std::string source =
        "TARGET=GLOBAL; TAP_DURATION=0ms; ACTION_GAP=1ms;\n"
        "state s=on; state t=off; number n=4; duration d=2s;\n"
        "A := B when s[on];\n"
        "pause F12:down ~> toggle;\n"
        "F1:down when not t[on] and (n < 5 or n <= 5) =>>\n"
        "press(C) release(C) tap(D) wait(d) wait(1ms) | gap()\n"
        "set(n, +n + -n - n * n / n % n)\n"
        "set(d, d + d - d) set(d, d * n) set(d, n * d) set(d, d / n)\n"
        "set(s, on) toggle(t) exec(\"tool.exe\")\n"
        "if n > 0 and n >= 0 and n != 1 and d == d then tap(E) else tap(F) end\n"
        "repeat n do tap(G) end while A[idle] do tap(H) end;\n"
        "F1:repeat ~>>; F1:up ~>; F2:down =>;\n";
    const auto output = CompileGood("coverage.weave", source, "lowering coverage");
    const auto program = DecodeGood(output, "lowering coverage");
    if (program == nullptr) {
        return;
    }

    std::set<ExpressionOpcode> expressionOpcodes;
    std::set<UnaryOperator> unaryOperators;
    std::set<BinaryOperator> binaryOperators;
    for (const ExpressionInstruction instruction : program->ExpressionCode()) {
        expressionOpcodes.insert(instruction.opcode);
        if (instruction.opcode == ExpressionOpcode::Unary) {
            unaryOperators.insert(static_cast<UnaryOperator>(instruction.operand0));
        }
        if (instruction.opcode == ExpressionOpcode::Binary) {
            binaryOperators.insert(static_cast<BinaryOperator>(instruction.operand0));
        }
    }
    for (std::uint32_t value = 0U;
         value <= static_cast<std::uint32_t>(ExpressionOpcode::Return);
         ++value) {
        Check(expressionOpcodes.contains(static_cast<ExpressionOpcode>(value)),
            "every expression opcode is lowered");
    }
    for (std::uint32_t value = 0U;
         value <= static_cast<std::uint32_t>(UnaryOperator::BooleanNot);
         ++value) {
        Check(unaryOperators.contains(static_cast<UnaryOperator>(value)),
            "every unary operator is lowered");
    }
    for (std::uint32_t value = 0U;
         value <= static_cast<std::uint32_t>(BinaryOperator::NumberGreaterEqual);
         ++value) {
        Check(binaryOperators.contains(static_cast<BinaryOperator>(value)),
            "every binary operator is lowered");
    }

    std::set<ActionOpcode> actionOpcodes;
    for (const ActionInstruction instruction : program->ActionCode()) {
        actionOpcodes.insert(instruction.opcode);
    }
    for (std::uint32_t value = 0U;
         value <= static_cast<std::uint32_t>(ActionOpcode::End);
         ++value) {
        Check(actionOpcodes.contains(static_cast<ActionOpcode>(value)),
            "every action opcode is lowered");
    }
    for (std::size_t index = 0U; index < program->ActionCode().size(); ++index) {
        const ActionInstruction& instruction = program->ActionCode()[index];
        if (instruction.opcode == ActionOpcode::Jump
            && instruction.operand0 < index) {
            Check(index > 0U
                    && program->ActionCode()[index - 1U].opcode == ActionOpcode::Yield,
                "every backward action edge is preceded by Yield");
        }
    }
    Check(program->Mappings().size() == 1U, "complete mapping lowers separately");
    Check(program->PauseControlRules().size() == 1U,
        "pause rule lowers to the dedicated channel");
    Check(program->Requirements().requiresProcessLaunch,
        "exec derives the process-launch requirement");
    Check(program->Requirements().maximumRepeatFramesPerTask == 1U,
        "repeat frame requirement is derived");

    std::set<std::uint32_t> ordinals;
    for (const CompiledRule& rule : program->Rules()) {
        ordinals.insert(rule.sourceOrdinal);
    }
    for (const PauseControlRule& rule : program->PauseControlRules()) {
        ordinals.insert(rule.sourceOrdinal);
    }
    Check(ordinals.size() == program->Rules().size()
            + program->PauseControlRules().size(),
        "source ordinals are globally unique across rule channels");
}

void TestGapAndEmptyActionSemantics()
{
    using namespace inputweaver;
    const auto gaps = CompileGood(
        "gaps.weave",
        "F1:down => | tap(A) tap(B) gap() ||;",
        "gap semantics");
    const auto gapProgram = DecodeGood(gaps, "gap semantics");
    if (gapProgram != nullptr) {
        const std::size_t gapCount = static_cast<std::size_t>(std::count_if(
            gapProgram->ActionCode().begin(),
            gapProgram->ActionCode().end(),
            [](const ActionInstruction& instruction) {
                return instruction.opcode == ActionOpcode::Gap;
            }));
        Check(gapCount == 4U, "only authored gap actions are emitted");
    }

    const auto empty = CompileGood(
        "empty.weave",
        "F1:down =>; F1:repeat =>>; F1:up ~>; F2:down ~>>;",
        "empty action rules");
    const auto emptyProgram = DecodeGood(empty, "empty action rules");
    if (emptyProgram != nullptr) {
        Check(emptyProgram->ActionPrograms().empty(),
            "empty action flows allocate no action program");
        Check(std::all_of(
                emptyProgram->Rules().begin(),
                emptyProgram->Rules().end(),
                [](const CompiledRule& rule) { return !rule.action.IsValid(); }),
            "empty rules retain invalid action IDs");
    }
}

void TestGoldenFixtureSemantics()
{
    using namespace inputweaver;
    struct Fixture final {
        std::string_view name;
        std::string_view path;
        std::string source;
        CompiledProgramStorage storage;
    };
    std::vector<Fixture> fixtures;
    fixtures.push_back({
        "tap",
        "fixture.tap.weave",
        "TARGET = GLOBAL;\nF6:down => tap(F7);\n",
        test::MakeTapFixtureStorage()});
    fixtures.push_back({
        "mapping",
        "fixture.mapping.weave",
        "TARGET = GLOBAL;\nF6 := F7;\n",
        test::MakeMappingFixtureStorage()});
    fixtures.push_back({
        "conditional repeat",
        "fixture.conditional-repeat.weave",
        "TARGET = GLOBAL;\nstate enabled = on;\n"
        "F6:down when enabled[on] => repeat 2 do tap(F7) | end;\n",
        test::MakeConditionalRepeatFixtureStorage()});
    fixtures.push_back({
        "pause",
        "fixture.pause-control.weave",
        "TARGET = GLOBAL;\npause F6:down => toggle;\n",
        test::MakePauseControlFixtureStorage()});

    for (Fixture& fixture : fixtures) {
        const auto compiled = CompileGood(
            std::string(fixture.path),
            fixture.source,
            fixture.name);
        const auto expected = FinalizeFixture(std::move(fixture.storage), fixture.name);
        if (expected == nullptr) {
            continue;
        }
        Check(
            compiled.dump == DumpCompiledProgram(*expected),
            std::string(fixture.name)
                + " exactly matches the Phase 2 fixture dump");
        Check(
            compiled.artifact == EncodeWeavec(*expected),
            std::string(fixture.name)
                + " exactly matches the Phase 2 fixture artifact");
    }
}

void TestArtifactAndFileCommands()
{
    using namespace inputweaver;
    using namespace inputweaver::compiler;
    const std::string source = "TARGET=GLOBAL; F6:down => tap(F7);";
    const CompileOutput first = CompileGood(
        "deterministic.weave",
        source,
        "first deterministic compile");
    const CompileOutput second = CompileGood(
        "deterministic.weave",
        source,
        "second deterministic compile");
    Check(first.artifact == second.artifact, "artifact emission is deterministic");
    Check(first.dump == second.dump, "compiled dump is deterministic");
    const DecodeWeavecResult decoded = DecodeWeavec(first.artifact);
    Check(decoded.program != nullptr, "emitted artifact loads through shared codec");
    if (decoded.program != nullptr) {
        Check(DumpCompiledProgram(*decoded.program) == first.dump,
            "save-load dump equivalence holds");
    }

    std::vector<std::uint8_t> truncated = first.artifact;
    truncated.pop_back();
    Check(DecodeWeavec(truncated).decodeError.has_value(),
        "truncated compiler artifact is rejected");
    std::vector<std::uint8_t> trailing = first.artifact;
    trailing.push_back(0U);
    Check(DecodeWeavec(trailing).decodeError.has_value(),
        "compiler artifact with trailing data is rejected");

    std::filesystem::path directory;
    for (std::uint32_t index = 0U; index < 1024U; ++index) {
        const std::filesystem::path candidate = std::filesystem::path("bin")
            / ("compiler-test-" + std::to_string(index));
        std::error_code createError;
        if (std::filesystem::create_directory(candidate, createError)) {
            directory = candidate;
            break;
        }
        if (createError) {
            break;
        }
    }
    Check(!directory.empty(), "file-command scratch directory is created");
    if (directory.empty()) {
        return;
    }
    const std::filesystem::path sourcePath = directory / "input.weave";
    const std::filesystem::path artifactPath = directory / "input.weavec";
    {
        std::ofstream file(sourcePath, std::ios::binary);
        file << source;
    }
    const CompilerCommandResult validateResult = ValidateFile(sourcePath);
    Check(
        validateResult.succeeded
            && validateResult.dump.empty()
            && validateResult.artifactByteLength == 0U,
        "ValidateFile finalizes without encoding or formatting products");
    const CompilerCommandResult dumpResult = DumpFile(sourcePath);
    Check(
        dumpResult.succeeded
            && !dumpResult.dump.empty()
            && dumpResult.artifactByteLength == 0U,
        "DumpFile formats one dump without encoding an artifact");
    const CompilerCommandResult compileResult = CompileFile(
        sourcePath,
        artifactPath);
    Check(
        compileResult.succeeded
            && compileResult.dump.empty()
            && compileResult.artifactByteLength != 0U,
        "CompileFile encodes one artifact without formatting a dump");
    Check(std::filesystem::exists(artifactPath), "CompileFile publishes destination");
    std::vector<char> previousBytes;
    {
        std::ifstream file(artifactPath, std::ios::binary);
        previousBytes.assign(
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>());
    }
    {
        std::ofstream file(sourcePath, std::ios::binary | std::ios::trunc);
        file << "F1:down when 1 =>;";
    }
    const CompilerCommandResult failed = CompileFile(sourcePath, artifactPath);
    Check(!failed.succeeded, "CompileFile reports invalid source");
    std::vector<char> retainedBytes;
    {
        std::ifstream file(artifactPath, std::ios::binary);
        retainedBytes.assign(
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>());
    }
    Check(retainedBytes == previousBytes,
        "failed compilation preserves the previous destination");
    Check(ValidateFile(sourcePath).succeeded == false,
        "ValidateFile rejects invalid source without writing");
    Check(!DumpFile(directory / "missing.weave").succeeded,
        "missing source produces a file diagnostic");
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    Check(!cleanupError, "file-command scratch directory is removed");
}

void TestDiagnosticFormatting()
{
    using namespace inputweaver::compiler;
    const CompileOutput output = CompileSource(
        "format.weave",
        "TARGET = GLOBAL;\nF1:down when 1 =>;");
    Check(!output.diagnostics.empty(), "formatting test produces a diagnostic");
    if (!output.diagnostics.empty()) {
        const std::string formatted = FormatCompileDiagnostic(
            output.diagnostics.front());
        Check(formatted.find("format.weave:2:") != std::string::npos,
            "formatted diagnostic contains path, line, and column");
        Check(formatted.find("IW1307") != std::string::npos,
            "formatted diagnostic contains a stable code");
        Check(formatted.find('^') != std::string::npos,
            "formatted diagnostic contains a source marker");
    }
}

} // namespace

int main()
{
    TestSourceAndLexer();
    TestParserAndRecovery();
    TestBindingDiagnostics();
    TestControlCatalogAndV2();
    TestLoweringCoverage();
    TestGapAndEmptyActionSemantics();
    TestGoldenFixtureSemantics();
    TestArtifactAndFileCommands();
    TestDiagnosticFormatting();

    if (g_failureCount != 0) {
        std::cerr << g_failureCount << " compiler test(s) failed.\n";
        return 1;
    }
    std::cout << "All compiler tests passed.\n";
    return 0;
}
