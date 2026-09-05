void TestMouseInputNormalization()
{
    using namespace inputweaver;
    win32::WindowsControlCatalog catalog;
    win32::WindowsRuntimeInputAdapter adapter(catalog);
    adapter.SeedPointerPosition({-100, 200});
    WindowsNativeInputEvent input{};
    input.device = DeviceKind::Mouse;
    input.origin = InputOrigin::PhysicalCandidate;
    input.transition = Transition::Move;
    input.position = {-95, 198};
    auto normalized = adapter.Normalize(input);
    Check(!normalized.control.IsValid() && normalized.delta.dx == 5 && normalized.delta.dy == -2,
        "mouse movement uses physical pixel deltas without a control binding");
    adapter.CompleteInput(input, InputDecision::Suppress);
    normalized = adapter.Normalize(input);
    Check(normalized.delta.dx == 5 && normalized.delta.dy == -2,
        "consumed movement retains the delivered pointer baseline");
    adapter.CompleteInput(input, InputDecision::Forward);
    input.position = {-92, 202};
    normalized = adapter.Normalize(input);
    Check(normalized.delta.dx == 3 && normalized.delta.dy == 4,
        "forwarded movement advances the pointer baseline");
    adapter.CompleteInput(input, InputDecision::Forward);

    input.origin = InputOrigin::CurrentInstanceInjected;
    input.position = {500, 300};
    normalized = adapter.Normalize(input);
    Check(normalized.delta.dx == 0 && normalized.delta.dy == 0,
        "injected movement contributes no physical delta");
    adapter.CompleteInput(input, InputDecision::Forward);
    input.origin = InputOrigin::PhysicalCandidate;
    input.position = {502, 301};
    normalized = adapter.Normalize(input);
    Check(normalized.delta.dx == 2 && normalized.delta.dy == 1,
        "physical movement after injection starts from the delivered injected position");

    input.transition = Transition::VerticalWheel;
    input.mouseData = 30U << 16U;
    normalized = adapter.Normalize(input);
    Check(normalized.delta.dx == 0 && normalized.delta.dy == 0
        && normalized.delta.wheelX == 0 && normalized.delta.wheelY == 0.25,
        "quarter-detent vertical input preserves fractions and clears inactive axes");
    input.transition = Transition::HorizontalWheel;
    input.mouseData = static_cast<std::uint32_t>(static_cast<std::uint16_t>(-60)) << 16U;
    normalized = adapter.Normalize(input);
    Check(normalized.delta.wheelX == -0.5 && normalized.delta.wheelY == 0,
        "horizontal input decodes the signed wheel high word");
}
