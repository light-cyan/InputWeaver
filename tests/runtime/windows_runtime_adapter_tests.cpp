#include "platform/windows/runtime/compiled_target_resolver.hpp"
#include "platform/windows/runtime/input_injector.hpp"
#include "platform/windows/runtime/process_context.hpp"
#include "platform/windows/runtime/runtime_control_catalog.hpp"
#include "platform/windows/runtime/runtime_process_launcher.hpp"
#include "platform/windows/runtime/runtime_route_adapter.hpp"
#include "program/compiled_program.hpp"
#include "program/program_validator.hpp"
#include "runtime/action_queue.hpp"
#include "runtime/program_runtime.hpp"
#include "../program/compiled_program_fixtures.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
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

[[nodiscard]] std::wstring ModulePath()
{
    std::vector<wchar_t> buffer(512U);
    while (true) {
        const DWORD length = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (length == 0U) {
            return {};
        }
        if (length < buffer.size() - 1U) {
            return {buffer.data(), length};
        }
        buffer.resize(buffer.size() * 2U);
    }
}

[[nodiscard]] std::string Utf8(std::wstring_view value)
{
    if (value.empty()) {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    const int converted = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        required,
        nullptr,
        nullptr);
    return converted == required ? result : std::string{};
}

[[nodiscard]] std::string Quote(std::wstring_view value)
{
    return "\"" + Utf8(value) + "\"";
}

struct FakeCreateProcessState final {
    bool succeed{true};
    std::wstring application;
    std::wstring command;
    std::wstring currentDirectory;
    bool processAttributesNull{};
    bool threadAttributesNull{};
    bool inheritHandles{};
    DWORD creationFlags{};
    bool environmentNull{};
    std::size_t calls{};
};

FakeCreateProcessState g_createProcess;

struct CancellationProbeState final {
    std::size_t calls{};
};

[[nodiscard]] bool CancelOnSecondProbe(void* context) noexcept
{
    auto& state = *static_cast<CancellationProbeState*>(context);
    ++state.calls;
    return state.calls >= 2U;
}

BOOL WINAPI FakeCreateProcessW(
    LPCWSTR applicationName,
    LPWSTR commandLine,
    LPSECURITY_ATTRIBUTES processAttributes,
    LPSECURITY_ATTRIBUTES threadAttributes,
    BOOL inheritHandles,
    DWORD creationFlags,
    LPVOID environment,
    LPCWSTR currentDirectory,
    LPSTARTUPINFOW startup,
    LPPROCESS_INFORMATION process)
{
    ++g_createProcess.calls;
    g_createProcess.application = applicationName == nullptr ? L"" : applicationName;
    g_createProcess.command = commandLine == nullptr ? L"" : commandLine;
    g_createProcess.currentDirectory = currentDirectory == nullptr
        ? L""
        : currentDirectory;
    g_createProcess.processAttributesNull = processAttributes == nullptr;
    g_createProcess.threadAttributesNull = threadAttributes == nullptr;
    g_createProcess.inheritHandles = inheritHandles != FALSE;
    g_createProcess.creationFlags = creationFlags;
    g_createProcess.environmentNull = environment == nullptr;
    if (!g_createProcess.succeed || startup == nullptr || process == nullptr) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    process->hProcess = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    process->hThread = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    process->dwProcessId = 1U;
    process->dwThreadId = 2U;
    return process->hProcess != nullptr && process->hThread != nullptr
        ? TRUE
        : FALSE;
}

class FakeClock final : public inputweaver::RuntimeClock {
public:
    [[nodiscard]] std::int64_t NowNanoseconds() const noexcept override
    {
        return now_;
    }

    void Advance(std::int64_t duration) noexcept
    {
        now_ += duration;
    }

private:
    std::int64_t now_{};
};

class NoLaunch final : public inputweaver::RuntimeProcessLauncher {
public:
    [[nodiscard]] bool Permitted() const noexcept override
    {
        return true;
    }

    [[nodiscard]] inputweaver::RuntimeLaunchResult Launch(
        std::string_view command,
        inputweaver::RuntimeCancellationProbe cancellation) noexcept override
    {
        (void)command;
        if (cancellation.Cancelled()) {
            return inputweaver::RuntimeLaunchResult::Cancelled;
        }
        return inputweaver::RuntimeLaunchResult::Launched;
    }
};

[[nodiscard]] std::shared_ptr<const inputweaver::CompiledProgram> Finalize(
    inputweaver::CompiledProgramStorage storage)
{
    inputweaver::FinalizeResult result = inputweaver::FinalizeCompiledProgram(
        std::move(storage));
    Check(result.program != nullptr, "Windows adapter fixture finalizes");
    return std::move(result.program);
}

void CheckSingleCatalogBinding(
    const inputweaver::ControlRef& control,
    std::uint8_t uses,
    std::string_view name)
{
    inputweaver::win32::WindowsControlCatalog catalog;
    catalog.BeginActivation();
    inputweaver::ActivatedControl activated{};
    const auto result = catalog.BindControl(
        inputweaver::ControlRefId{0U},
        control,
        uses,
        activated);
    Check(
        result == inputweaver::RuntimeControlBindResult::Bound,
        name);
    catalog.CommitActivation();
    Check(
        catalog.BindingCount() == 1U
            && catalog.Binding(activated.backendToken) != nullptr,
        "committed catalog token resolves to one recipe");
    const inputweaver::win32::WindowsControlBinding* const binding =
        catalog.Binding(activated.backendToken);
    if (binding == nullptr) {
        return;
    }
    if (activated.device == inputweaver::DeviceKind::Keyboard) {
        Check(
            activated.initialStateQueryable && binding->initialStateQueryable,
            "keyboard binding exposes initial-state query support");
    }
    inputweaver::InputEvent native{};
    native.device = activated.device;
    native.origin = inputweaver::InputOrigin::PhysicalCandidate;
    native.transition = inputweaver::Transition::Down;
    native.code = binding->virtualKey;
    native.scanCode = binding->scanCode;
    native.flags = binding->scanQualifier
            == inputweaver::kWindowsScanCodeQualifierE0
        ? LLKHF_EXTENDED
        : 0U;
    const std::optional<inputweaver::ControlRefId> normalized =
        catalog.Normalize(native);
    Check(
        normalized.has_value() && normalized->value == 0U,
        "catalog input recipe normalizes to its strong ID");

    inputweaver::ActionBatch output{};
    output.actions[0] = {
        activated.device,
        inputweaver::Transition::Down,
        binding->virtualKey,
        0,
        0};
    output.actions[1] = output.actions[0];
    output.actions[1].transition = inputweaver::Transition::Up;
    output.actionCount = 2U;
    const inputweaver::InputInjector injector(0x49575254U);
    const inputweaver::PreparedInputBatch prepared = injector.Prepare(output);
    Check(
        prepared.Succeeded() && prepared.count == 2U,
        "catalog output recipe converts to native down and up inputs");
}

void TestWindowsControlCatalogCoverage()
{
    using namespace inputweaver;
    constexpr std::uint8_t allUses =
        ToControlUseBits(ControlUse::EventSource)
        | ToControlUseBits(ControlUse::PhysicalState)
        | ToControlUseBits(ControlUse::OutputDownUp)
        | ToControlUseBits(ControlUse::OutputRepeat);
    for (std::uint32_t usage = 0x04U; usage <= 0x1dU; ++usage) {
        CheckSingleCatalogBinding(
            {kControlNamespaceUsbHid, 0x07U, usage, 0U},
            allUses,
            "portable letter has complete Windows capabilities");
    }
    for (std::uint32_t usage = 0x1eU; usage <= 0x27U; ++usage) {
        CheckSingleCatalogBinding(
            {kControlNamespaceUsbHid, 0x07U, usage, 0U},
            allUses,
            "portable digit has complete Windows capabilities");
    }
    constexpr std::array<std::uint32_t, 62U> keyboardUsages = {
        0x28U, 0x29U, 0x2aU, 0x2bU, 0x2cU, 0x39U,
        0x3aU, 0x3bU, 0x3cU, 0x3dU, 0x3eU, 0x3fU,
        0x40U, 0x41U, 0x42U, 0x43U, 0x44U, 0x45U,
        0x47U, 0x48U, 0x49U, 0x4aU, 0x4bU, 0x4cU,
        0x4dU, 0x4eU, 0x4fU, 0x50U, 0x51U, 0x52U,
        0x53U, 0x54U, 0x55U, 0x56U, 0x57U,
        0x59U, 0x5aU, 0x5bU, 0x5cU, 0x5dU, 0x5eU,
        0x5fU, 0x60U, 0x61U, 0x62U, 0x63U,
        0x68U, 0x69U, 0x6aU, 0x6bU, 0x6cU, 0x6dU,
        0x6eU, 0x6fU, 0x70U, 0x71U, 0x72U, 0x73U,
        0xe0U, 0xe1U, 0xe2U, 0xe4U,
    };
    for (const std::uint32_t usage : keyboardUsages) {
        CheckSingleCatalogBinding(
            {kControlNamespaceUsbHid, 0x07U, usage, 0U},
            allUses,
            "published portable keyboard usage binds on Windows");
    }
    CheckSingleCatalogBinding(
        {kControlNamespaceUsbHid, 0x07U, 0xe5U, 0U},
        allUses,
        "right shift binds on Windows");
    CheckSingleCatalogBinding(
        {kControlNamespaceUsbHid, 0x07U, 0xe6U, 0U},
        allUses,
        "right alt binds on Windows");
    for (std::uint32_t usage = 1U; usage <= 5U; ++usage) {
        CheckSingleCatalogBinding(
            {kControlNamespaceUsbHid, 0x09U, usage, 0U},
            allUses,
            "portable mouse button has complete Windows capabilities");
    }
    constexpr std::array<std::uint32_t, 7U> consumerUsages = {
        0x00cdU, 0x00b5U, 0x00b6U, 0x00b7U,
        0x00e2U, 0x00e9U, 0x00eaU,
    };
    for (const std::uint32_t usage : consumerUsages) {
        CheckSingleCatalogBinding(
            {kControlNamespaceUsbHid, 0x0cU, usage, 0U},
            allUses,
            "portable consumer control binds on Windows");
    }
    CheckSingleCatalogBinding(
        {kControlNamespaceWindows, kWindowsVirtualKeyFamily, VK_IME_ON, 0U},
        allUses,
        "Windows IMEOn virtual key binds");
    CheckSingleCatalogBinding(
        {kControlNamespaceWindows, kWindowsVirtualKeyFamily, VK_OEM_1, 0U},
        allUses,
        "layout-sensitive Windows virtual key binds");
    CheckSingleCatalogBinding(
        {kControlNamespaceWindows, kWindowsScanCodeFamily, 0x1eU, 0U},
        allUses,
        "normal Windows scan code binds");
    CheckSingleCatalogBinding(
        {kControlNamespaceWindows, kWindowsScanCodeFamily, 0x1dU,
            kWindowsScanCodeQualifierE0},
        allUses,
        "E0 Windows scan code binds");
    CheckSingleCatalogBinding(
        {kControlNamespaceWindows, kWindowsScanCodeFamily, 0x45U,
            kWindowsScanCodeQualifierE1},
        allUses,
        "E1 exceptional Windows scan code binds");

    win32::WindowsControlCatalog unsupportedCatalog;
    unsupportedCatalog.BeginActivation();
    ActivatedControl unsupported{};
    Check(unsupportedCatalog.BindControl(
            ControlRefId{0U},
            {kControlNamespaceWindows, kWindowsVirtualKeyFamily, 0U, 0U},
            allUses,
            unsupported) == RuntimeControlBindResult::UnsupportedIdentity,
        "Windows activation rejects virtual-key zero after structural validation");
    Check(unsupportedCatalog.BindControl(
            ControlRefId{1U},
            {kControlNamespaceWindows, kWindowsScanCodeFamily, 0U, 0U},
            allUses,
            unsupported) == RuntimeControlBindResult::UnsupportedIdentity,
        "Windows activation rejects scan-code zero after structural validation");
    unsupportedCatalog.AbortActivation();

    win32::WindowsControlCatalog recipeCatalog;
    recipeCatalog.BeginActivation();
    ActivatedControl normal{};
    ActivatedControl extended{};
    ActivatedControl exceptional{};
    ActivatedControl layout{};
    Check(
        recipeCatalog.BindControl(
            ControlRefId{0U},
            {kControlNamespaceWindows, kWindowsScanCodeFamily, 0x1eU, 0U},
            allUses,
            normal) == RuntimeControlBindResult::Bound,
        "normal recipe stages");
    Check(
        recipeCatalog.BindControl(
            ControlRefId{1U},
            {kControlNamespaceWindows, kWindowsScanCodeFamily, 0x1dU,
                kWindowsScanCodeQualifierE0},
            allUses,
            extended) == RuntimeControlBindResult::Bound,
        "E0 recipe stages");
    Check(
        recipeCatalog.BindControl(
            ControlRefId{2U},
            {kControlNamespaceWindows, kWindowsScanCodeFamily, 0x45U,
                kWindowsScanCodeQualifierE1},
            allUses,
            exceptional) == RuntimeControlBindResult::Bound,
        "E1 recipe stages");
    Check(
        recipeCatalog.BindControl(
            ControlRefId{3U},
            {kControlNamespaceWindows, kWindowsVirtualKeyFamily, VK_OEM_1, 0U},
            allUses,
            layout) == RuntimeControlBindResult::Bound,
        "layout-sensitive recipe stages");
    recipeCatalog.CommitActivation();
    Check(
        recipeCatalog.Binding(normal.backendToken)->scanQualifier == 0U,
        "normal recipe retains no prefix");
    Check(
        recipeCatalog.Binding(extended.backendToken)->scanQualifier
            == kWindowsScanCodeQualifierE0,
        "E0 recipe retains its prefix");
    Check(
        recipeCatalog.Binding(exceptional.backendToken)->exceptionalSequence,
        "E1 recipe is marked exceptional");
    Check(
        recipeCatalog.Binding(layout.backendToken)->layoutSensitive,
        "OEM virtual key records layout sensitivity");
}

void TestCatalogAmbiguityAndNormalization()
{
    using namespace inputweaver;
    win32::WindowsControlCatalog catalog;
    catalog.BeginActivation();
    ActivatedControl portable{};
    ActivatedControl native{};
    constexpr std::uint8_t inputUse =
        ToControlUseBits(ControlUse::EventSource);
    Check(
        catalog.BindControl(
            ControlRefId{0U},
            {kControlNamespaceUsbHid, 0x07U, 0x04U, 0U},
            inputUse,
            portable) == RuntimeControlBindResult::Bound,
        "portable A input binding stages");
    Check(
        catalog.BindControl(
            ControlRefId{1U},
            {kControlNamespaceWindows, kWindowsVirtualKeyFamily, 'A', 0U},
            inputUse,
            native) == RuntimeControlBindResult::MissingCapability,
        "ambiguous native input identities reject activation");
    catalog.AbortActivation();

    catalog.BeginActivation();
    Check(
        catalog.BindControl(
            ControlRefId{7U},
            {kControlNamespaceUsbHid, 0x07U, 0x3fU, 0U},
            inputUse,
            portable) == RuntimeControlBindResult::Bound,
        "portable F6 binding stages");
    catalog.CommitActivation();
    InputEvent input{};
    input.device = DeviceKind::Keyboard;
    input.origin = InputOrigin::PhysicalCandidate;
    input.transition = Transition::Down;
    input.code = VK_F6;
    const std::optional<ControlRefId> normalized = catalog.Normalize(input);
    Check(
        normalized.has_value() && normalized->value == 7U,
        "native F6 normalizes to its activated strong ID");
}

void TestKeyboardInitialStateCapabilities()
{
    using namespace inputweaver;
    constexpr std::uint8_t inputAndState = static_cast<std::uint8_t>(
        ToControlUseBits(ControlUse::EventSource)
        | ToControlUseBits(ControlUse::PhysicalState));
    win32::WindowsControlCatalog modifierCatalog;
    modifierCatalog.BeginActivation();
    ActivatedControl left{};
    ActivatedControl right{};
    Check(
        modifierCatalog.BindControl(
            ControlRefId{0U},
            {kControlNamespaceUsbHid, 0x07U, 0xe0U, 0U},
            inputAndState,
            left) == RuntimeControlBindResult::Bound
            && modifierCatalog.BindControl(
                ControlRefId{1U},
                {kControlNamespaceUsbHid, 0x07U, 0xe4U, 0U},
                inputAndState,
                right) == RuntimeControlBindResult::Bound,
        "left and right modifiers stage as independent queryable controls");
    modifierCatalog.CommitActivation();
    const win32::WindowsControlBinding* const leftBinding =
        modifierCatalog.Binding(left.backendToken);
    const win32::WindowsControlBinding* const rightBinding =
        modifierCatalog.Binding(right.backendToken);
    Check(
        leftBinding != nullptr
            && rightBinding != nullptr
            && leftBinding->virtualKey == VK_LCONTROL
            && rightBinding->virtualKey == VK_RCONTROL
            && leftBinding->initialStateQueryable
            && rightBinding->initialStateQueryable,
        "modifier initial-state recipes retain left-right identity");

    std::uint32_t unqueryableScanCode = 0U;
    for (std::uint32_t scanCode = 1U; scanCode <= 0xffU; ++scanCode) {
        if (MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX) == 0U) {
            unqueryableScanCode = scanCode;
            break;
        }
    }
    Check(unqueryableScanCode != 0U, "Windows exposes an unqueryable scan-code identity");
    if (unqueryableScanCode == 0U) {
        return;
    }
    const ControlRef rawScan{
        kControlNamespaceWindows,
        kWindowsScanCodeFamily,
        unqueryableScanCode,
        kControlQualifierNone};
    win32::WindowsControlCatalog eventCatalog;
    eventCatalog.BeginActivation();
    ActivatedControl eventOnly{};
    Check(
        eventCatalog.BindControl(
            ControlRefId{0U},
            rawScan,
            ToControlUseBits(ControlUse::EventSource),
            eventOnly) == RuntimeControlBindResult::Bound
            && !eventOnly.initialStateQueryable,
        "unqueryable raw scan code remains available as an unsynchronized event source");
    eventCatalog.AbortActivation();
    win32::WindowsControlCatalog stateCatalog;
    stateCatalog.BeginActivation();
    ActivatedControl physicalState{};
    Check(
        stateCatalog.BindControl(
            ControlRefId{0U},
            rawScan,
            inputAndState,
            physicalState) == RuntimeControlBindResult::MissingCapability,
        "unqueryable raw scan code rejects physical-state requirements");
    stateCatalog.AbortActivation();
}

void TestModifierStateSeeding()
{
    using namespace inputweaver;
    CompiledProgramStorage storage = test::MakeTapFixtureStorage();
    const SourceSpan source{0U, storage.source.byteLength};
    storage.controls = {
        {kControlNamespaceUsbHid, 0x07U, 0xe0U, 0U},
        {kControlNamespaceUsbHid, 0x07U, 0xe4U, 0U},
    };
    storage.actionPrograms.clear();
    storage.actionCode.clear();
    storage.rules.clear();
    storage.eventBuckets.clear();
    storage.expressions = {
        {{0U, 2U}, ExpressionType::Boolean, 1U, source},
        {{2U, 2U}, ExpressionType::Boolean, 1U, source},
    };
    storage.expressionCode = {
        {ExpressionOpcode::ReadControlHeld, ExpressionType::Boolean, 0U, 0U},
        {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
        {ExpressionOpcode::ReadControlHeld, ExpressionType::Boolean, 1U, 0U},
        {ExpressionOpcode::Return, ExpressionType::Boolean, 0U, 0U},
    };
    storage.controlRequirements = {
        {ControlRefId{0U}, ToControlUseBits(ControlUse::PhysicalState)},
        {ControlRefId{1U}, ToControlUseBits(ControlUse::PhysicalState)},
    };
    storage.debugInfo.actionInstructionSpans.clear();
    storage.debugInfo.expressionInstructionSpans.assign(4U, source);
    storage.requirements = ComputeProgramRequirements(storage);
    const auto program = Finalize(std::move(storage));
    win32::WindowsControlCatalog catalog;
    ActionQueue queue;
    win32::WindowsRuntimeOutputPort output(
        catalog,
        0U,
        &queue,
        &win32::PublishRuntimeBatchToActionQueue);
    win32::WindowsRuntimeRoutePort route(nullptr);
    FakeClock clock;
    NoLaunch launcher;
    ProgramRuntime runtime({}, catalog, output, route, launcher, clock);
    Check(
        runtime.Activate(program).activated
            && runtime.SeedPhysicalState(ControlRefId{0U}, true)
            && runtime.SeedPhysicalState(ControlRefId{1U}, false),
        "left and right modifier states seed before the first event");
    const RuntimeEvaluationResult leftHeld = runtime.EvaluateExpression(
        ExpressionId{0U});
    const RuntimeEvaluationResult rightHeld = runtime.EvaluateExpression(
        ExpressionId{1U});
    Check(
        leftHeld.Succeeded()
            && leftHeld.value.booleanValue
            && rightHeld.Succeeded()
            && !rightHeld.value.booleanValue,
        "seeded modifier held and idle predicates remain independent");
}

void TestCompiledTargetResolution()
{
    using namespace inputweaver;
    TargetSelectorKind kind = TargetSelectorKind::Unspecified;
    std::wstring selector;
    std::wstring errorMessage;
    const auto globalProgram = Finalize(test::MakeTapFixtureStorage());
    if (globalProgram == nullptr) {
        return;
    }
    Check(
        win32::ResolveCompiledTarget(
            *globalProgram, false, {}, kind, selector, errorMessage)
            && kind == TargetSelectorKind::Global
            && selector.empty(),
        "compiled GLOBAL target is used when no command-line override exists");
    Check(
        win32::ResolveCompiledTarget(
            *globalProgram, false, L"notepad.exe", kind, selector, errorMessage)
            && kind == TargetSelectorKind::Executable
            && selector == L"notepad.exe",
        "command-line executable target overrides the compiled target");
    Check(
        win32::ResolveCompiledTarget(
            *globalProgram, true, {}, kind, selector, errorMessage)
            && kind == TargetSelectorKind::Global
            && selector.empty(),
        "--target-global overrides the compiled target");
    Check(
        win32::ResolveCompiledTarget(
            *globalProgram, false, L"GLOBAL", kind, selector, errorMessage)
            && kind == TargetSelectorKind::Executable
            && selector == L"GLOBAL",
        "--target values remain executable selectors without reserved words");
    errorMessage.clear();
    Check(
        !win32::ResolveCompiledTarget(
            *globalProgram, true, L"notepad.exe", kind, selector, errorMessage)
            && !errorMessage.empty(),
        "executable and global target overrides are mutually exclusive");

    CompiledProgramStorage executableStorage = test::MakeTapFixtureStorage();
    executableStorage.strings.push_back("notepad.exe");
    executableStorage.settings.target.kind = TargetSelectorKind::Executable;
    executableStorage.settings.target.text = StringId{1U};
    const auto executableProgram = Finalize(std::move(executableStorage));
    if (executableProgram != nullptr) {
        Check(
            win32::ResolveCompiledTarget(
                *executableProgram, false, {}, kind, selector, errorMessage)
                && kind == TargetSelectorKind::Executable
                && selector == L"notepad.exe",
            "compiled executable target is the default without an override");
    }

    CompiledProgramStorage unspecifiedStorage = test::MakeTapFixtureStorage();
    unspecifiedStorage.settings.target = {};
    const auto unspecifiedProgram = Finalize(std::move(unspecifiedStorage));
    if (unspecifiedProgram != nullptr) {
        errorMessage.clear();
        Check(
            !win32::ResolveCompiledTarget(
                *unspecifiedProgram, false, {}, kind, selector, errorMessage)
                && !errorMessage.empty(),
            "missing compiled and command-line targets are rejected");
    }
}

void TestRuntimeAdaptersAndForceStop()
{
    using namespace inputweaver;
    win32::WindowsControlCatalog catalog;
    ActionQueue queue;
    win32::WindowsRuntimeOutputPort output(
        catalog,
        0U,
        &queue,
        &win32::PublishRuntimeBatchToActionQueue);
    win32::WindowsRuntimeRoutePort route(nullptr);
    FakeClock clock;
    NoLaunch launcher;
    ProgramRuntime runtime({}, catalog, output, route, launcher, clock);
    const auto program = Finalize(test::MakeTapFixtureStorage());
    Check(runtime.Activate(program).activated, "Windows runtime adapters activate tap fixture");
    win32::WindowsRuntimeInputAdapter inputAdapter(catalog);
    InputEvent f6{};
    f6.device = DeviceKind::Keyboard;
    f6.origin = InputOrigin::PhysicalCandidate;
    f6.transition = Transition::Down;
    f6.code = VK_F6;
    Check(
        runtime.HandleInput(inputAdapter.Normalize(f6)) == InputDecision::Suppress,
        "Windows native F6 reaches the compiled event bucket");
    (void)runtime.Pump();
    ActionBatch batch{};
    Check(
        queue.TryPop(batch)
            && batch.actionCount == 1U
            && batch.actions[0].transition == Transition::Down
            && batch.actions[0].code == VK_F7,
        "runtime output enters the existing bounded ActionBatch queue");

    InputEvent control{};
    control.device = DeviceKind::Keyboard;
    control.origin = InputOrigin::PhysicalCandidate;
    control.transition = Transition::Down;
    control.code = VK_LCONTROL;
    InputEvent shift = control;
    shift.code = VK_RSHIFT;
    InputEvent f12 = control;
    f12.code = VK_F12;
    win32::WindowsRuntimeInputAdapter startupSeedAdapter(catalog);
    (void)startupSeedAdapter.Normalize(control);
    (void)startupSeedAdapter.Normalize(shift);
    const RuntimeInputEvent seededForceStop = startupSeedAdapter.Normalize(f12);
    Check(
        seededForceStop.forceStopRequested
            && runtime.HandleInput(seededForceStop) == InputDecision::Suppress,
        "startup-seeded Ctrl-Shift state preserves physical F12 force stop");
    Check(
        runtime.HandleInput(inputAdapter.Normalize(f6)) == InputDecision::Forward,
        "force stop disables subsequent runtime transactions");
}

void TestExecutableResolutionAndCreateContract()
{
    using namespace inputweaver::win32;
    const ExecutableResolutionResult bare = ResolveExecutableCommand(
        "cmd.exe /d /c echo ready");
    Check(
        bare.Succeeded()
            && bare.executableToken == L"cmd.exe"
            && !bare.executablePath.empty()
            && !bare.workingDirectory.empty(),
        "bare executable resolves through native CreateProcess search order");

    const std::wstring module = ModulePath();
    const std::filesystem::path spaceDirectory =
        std::filesystem::path(module).parent_path() / L"runtime launcher space";
    const std::filesystem::path copiedExecutable = spaceDirectory / L"adapter-child.exe";
    std::error_code fileError;
    std::filesystem::create_directories(spaceDirectory, fileError);
    fileError.clear();
    std::filesystem::copy_file(
        module,
        copiedExecutable,
        std::filesystem::copy_options::overwrite_existing,
        fileError);
    Check(!fileError, "test creates an executable path containing spaces");
    const std::string quotedCommand = Quote(copiedExecutable.wstring()) + " --probe";
    const ExecutableResolutionResult quoted = ResolveExecutableCommand(quotedCommand);
    Check(
        quoted.Succeeded()
            && quoted.executablePath == copiedExecutable.wstring()
            && quoted.workingDirectory == spaceDirectory.wstring(),
        "quoted absolute executable preserves spaces and derives its directory");
    const ExecutableResolutionResult unterminated = ResolveExecutableCommand(
        "\"C:\\missing.exe --arg");
    Check(
        unterminated.error == ExecutableResolutionError::UnterminatedQuote,
        "unterminated executable quote is rejected deterministically");
    const ExecutableResolutionResult missing = ResolveExecutableCommand(
        "InputWeaver.NoSuchExecutable.exe --arg");
    Check(
        missing.error == ExecutableResolutionError::ExecutableNotFound,
        "resolution failure is reported before process creation");

    g_createProcess = {};
    WindowsProcessLauncher deniedLauncher(false, &FakeCreateProcessW);
    Check(
        !deniedLauncher.Permitted()
            && deniedLauncher.Launch("cmd.exe /d /c echo denied", {})
                == inputweaver::RuntimeLaunchResult::CreationFailed
            && deniedLauncher.LastWin32Error() == ERROR_ACCESS_DISABLED_BY_POLICY
            && g_createProcess.calls == 0U,
        "denied process launcher never reaches native process creation");
    WindowsProcessLauncher fakeLauncher(true, &FakeCreateProcessW);
    Check(
        fakeLauncher.Launch("cmd.exe /d /c echo ready", {})
            == inputweaver::RuntimeLaunchResult::Launched,
        "explicit cmd invocation launches without an implicit shell");
    Check(
        g_createProcess.calls == 1U
            && !g_createProcess.application.empty()
            && g_createProcess.command == L"cmd.exe /d /c echo ready"
            && g_createProcess.currentDirectory
                == std::filesystem::path(g_createProcess.application).parent_path().wstring(),
        "CreateProcess receives resolved application, authored command, and executable directory");
    Check(
        g_createProcess.processAttributesNull
            && g_createProcess.threadAttributesNull
            && !g_createProcess.inheritHandles
            && g_createProcess.creationFlags == 0U
            && g_createProcess.environmentNull,
        "CreateProcess inherits environment without inheriting handles or adding flags");
    const std::size_t successfulCreateCalls = g_createProcess.calls;
    CancellationProbeState cancellation{};
    Check(
        fakeLauncher.Launch(
            "cmd.exe /d /c echo cancelled",
            {&cancellation, &CancelOnSecondProbe})
                == inputweaver::RuntimeLaunchResult::Cancelled
            && cancellation.calls == 2U
            && g_createProcess.calls == successfulCreateCalls,
        "cancellation after resolution prevents native process creation");
    g_createProcess = {};
    g_createProcess.succeed = false;
    Check(
        fakeLauncher.Launch("cmd.exe /d /c exit 0", {})
                == inputweaver::RuntimeLaunchResult::CreationFailed
            && fakeLauncher.LastWin32Error() == ERROR_ACCESS_DENIED,
        "native creation failure is returned to the current task");

    fileError.clear();
    std::filesystem::remove_all(spaceDirectory, fileError);
}

void TestChildWorkingDirectoryAndImmediateReturn()
{
    using namespace inputweaver::win32;
    const std::wstring module = ModulePath();
    const std::filesystem::path marker =
        std::filesystem::path(module).parent_path() / L"runtime-child-cwd.txt";
    std::error_code fileError;
    (void)std::filesystem::remove(marker, fileError);
    WindowsProcessLauncher launcher;
    const std::string command = Quote(module)
        + " --write-cwd "
        + Quote(marker.wstring());
    Check(
        launcher.Launch(command, {}) == inputweaver::RuntimeLaunchResult::Launched,
        "working-directory observation child launches");
    for (std::size_t attempt = 0U;
         attempt < 200U && !std::filesystem::exists(marker);
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::ifstream input(marker);
    std::string observed;
    std::getline(input, observed);
    Check(
        std::filesystem::path(observed)
            == std::filesystem::path(module).parent_path(),
        "child observes the resolved executable directory as its working directory");
    fileError.clear();
    (void)std::filesystem::remove(marker, fileError);

    const ULONGLONG start = GetTickCount64();
    Check(
        launcher.Launch(Quote(module) + " --hold", {})
            == inputweaver::RuntimeLaunchResult::Launched,
        "long-running child launches");
    const ULONGLONG elapsed = GetTickCount64() - start;
    Check(elapsed < 500U, "successful launch returns without waiting for child exit");
}

int RunChildMode(int argc, char** argv)
{
    if (argc == 3 && std::string_view(argv[1]) == "--write-cwd") {
        std::ofstream output(argv[2], std::ios::trunc);
        output << std::filesystem::current_path().string() << '\n';
        return output ? 0 : 2;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--hold") {
        std::this_thread::sleep_for(std::chrono::milliseconds(750));
        return 0;
    }
    return -1;
}

} // namespace

int main(int argc, char** argv)
{
    const int childResult = RunChildMode(argc, argv);
    if (childResult >= 0) {
        return childResult;
    }

    TestWindowsControlCatalogCoverage();
    TestCatalogAmbiguityAndNormalization();
    TestKeyboardInitialStateCapabilities();
    TestModifierStateSeeding();
    TestCompiledTargetResolution();
    TestRuntimeAdaptersAndForceStop();
    TestExecutableResolutionAndCreateContract();
    TestChildWorkingDirectoryAndImmediateReturn();

    if (g_failureCount != 0) {
        std::cerr << g_failureCount << " Windows runtime adapter test(s) failed.\n";
        return 1;
    }
    std::cout << "All Windows runtime adapter tests passed.\n";
    return 0;
}
