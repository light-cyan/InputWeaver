void TestMouseNumericDiagnostics()
{
    using namespace inputweaver;
    HookDiagnosticRecord hook{};
    hook.device = DeviceKind::Mouse;
    hook.transition = Transition::VerticalWheel;
    hook.position = {-73, 20};
    hook.delta.wheelY = 0.25;
    ApplyPrivacyRedaction(hook);
    const auto raw = FormatHookDiagnosticJson(hook);
    Check(raw.find("\"x\":-73,\"y\":20,\"dx\":0,\"dy\":0,\"wheel_x\":0,\"wheel_y\":0.25") != std::string::npos,
        "hook JSONL retains negative coordinates and fractional wheel values");
    InjectionDiagnosticRecord output{};
    output.outputKind = RuntimeOutputKind::Pointer;
    output.pointer = {PointerOperation::MoveBy, 0.5, -1};
    output.pointerPrepared = true;
    output.pointerOrigin = {-73, 20};
    output.pointerDestination = {-72, 19};
    const auto movement = FormatInjectionDiagnosticJson(output);
    Check(movement.find("\"operation\":\"move_by\",\"argument_x\":0.5,\"argument_y\":-1") != std::string::npos
        && movement.find("\"origin_x\":-73,\"origin_y\":20,\"destination_x\":-72,\"destination_y\":19") != std::string::npos,
        "pointer JSONL relates requested movement to the prepared origin and destination");
    output.pointer = {PointerOperation::ScrollHorizontal, -0.25, 0};
    output.preparedWheel = -0.25;
    const auto wheel = FormatInjectionDiagnosticJson(output);
    Check(wheel.find("\"operation\":\"scroll_horizontal\"") != std::string::npos
        && wheel.find("\"wheel_amount\":-0.25") != std::string::npos, "wheel JSONL retains operation and prepared detent amount");
}
