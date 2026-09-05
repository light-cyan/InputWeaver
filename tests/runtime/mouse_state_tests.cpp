#include "runtime/mouse_state.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
using namespace inputweaver;

int failures{};
void Check(bool condition, std::string_view name)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
}

bool Near(double left, double right) { return std::abs(left - right) < 1e-8; }
RuntimeValue Number(double number) { RuntimeValue result{}; result.type = ExpressionType::Number; result.numberValue = number; return result; }
RuntimeValue Milliseconds(std::int64_t ms) { RuntimeValue result{}; result.type = ExpressionType::Duration; result.durationValue.nanoseconds = ms * 1'000'000; return result; }

struct Periods {
    std::vector<RuntimeValue> values;
    std::vector<unsigned> reads;
    explicit Periods(std::initializer_list<RuntimeValue> initial) : values(initial), reads(initial.size()) {}
    static RuntimeEvaluationResult Evaluate(void* context, MeterId source) noexcept
    {
        auto& self = *static_cast<Periods*>(context);
        ++self.reads[source.value];
        return {self.values[source.value], RuntimeEvaluationFault::None, 0};
    }
    MousePeriodEvaluator Evaluator() { return {this, &Evaluate}; }
};

RuntimeInputEvent Move(double dx, double dy, InputCoordinate x, InputCoordinate y)
{
    RuntimeInputEvent event{};
    event.device = DeviceKind::Mouse;
    event.transition = Transition::Move;
    event.position = {x, y};
    event.delta = {dx, dy, 0, 0};
    return event;
}

void Input(RuntimeMouseState& mouse, Periods& periods, RuntimeInputEvent event, std::int64_t ms)
{
    mouse.Observe(event, ms * 1'000'000);
    Check(mouse.Accumulate(event, ms * 1'000'000, periods.Evaluator()), "input accumulates");
}

double Field(const RuntimeMouseState& mouse, MouseField field, std::uint32_t source = kInvalidProgramIndex,
    bool completed = false, std::span<const MouseCycle> selection = {})
{
    const auto result = mouse.Read({MeterId{source}, field, completed}, selection);
    Check(result.Succeeded(), "field evaluates");
    return result.value.type == ExpressionType::Duration ? static_cast<double>(result.value.durationValue.nanoseconds) / 1e6
        : result.value.type == ExpressionType::State ? result.value.stateValue : result.value.numberValue;
}

void TestDistanceAndSnapshots()
{
    const std::array configs{MouseMeterConfig{}, MouseMeterConfig{}};
    RuntimeMouseState mouse(configs, {80'000'000});
    Periods periods{Number(8), Number(24)};
    mouse.Initialize({100, 200}, 0);
    Check(Field(mouse, MouseField::Period, 0) == 0 && Field(mouse, MouseField::StartX, 0) == 100,
        "unopened cycle has zero period and observed origin");
    Input(mouse, periods, Move(27, 0, 127, 200), 1);
    const auto events = mouse.Occurrences();
    Check(events.size() == 4 && events[0].source.value == 0 && events[3].source.value == 1,
        "all source occurrences follow declaration and cycle order");
    Check(Field(mouse, MouseField::StartX, 1, true) == 100
        && Field(mouse, MouseField::X, 1, true) == 124
        && Field(mouse, MouseField::StartX, 1) == 124
        && Field(mouse, MouseField::X, 1) == 127
        && Field(mouse, MouseField::Distance, 1) == 3,
        "27 pixels yields a completed 24-pixel origin and a 3-pixel remainder");
    std::array<MouseCycle, 2> first{}, coarse{}, empty{};
    mouse.SelectCompleted(first, &events[0]);
    mouse.SelectCompleted(coarse, &events[3]);
    Check(first[0].sequence == events[0].cycle.sequence && first[1].sequence == events[3].cycle.sequence
        && coarse[0].sequence == events[2].cycle.sequence,
        "own snapshots select the trigger while other sources select the latest completion");
    Check(Field(mouse, MouseField::Valid, 0, true, empty) == 0, "empty selection remains invalid");
    mouse.Refresh({127, 200}, 200'000'000);
    Check(Field(mouse, MouseField::Distance, 1) == 3 && Field(mouse, MouseField::Moving, 1) == 0,
        "ordinary idle preserves distance progress");
    Input(mouse, periods, Move(-24, 0, 103, 200), 201);
    Check(Field(mouse, MouseField::Dx, 0, true, first) == 8
        && Field(mouse, MouseField::Dx, 0, true) == -8,
        "reserved completion selection remains stable after later inputs");
    mouse.Restart(MeterId{0});
    Check(Field(mouse, MouseField::Valid, 0, true) == 0 && Field(mouse, MouseField::Valid, 1, true) == 1
        && Field(mouse, MouseField::Dx, 0, true, first) == 8,
        "restart affects one source and preserves saved task selections");
}

void TestDynamicPeriodsAndTurns()
{
    const std::array configs{MouseMeterConfig{}};
    RuntimeMouseState mouse(configs, {80'000'000});
    Periods periods{Number(80)};
    mouse.Initialize({0, 0}, 0);
    Input(mouse, periods, Move(35, 0, 35, 0), 1);
    periods.values[0] = Number(20);
    Input(mouse, periods, Move(0, 45, 35, 45), 2);
    Check(Field(mouse, MouseField::Period, 0, true) == 80 && Field(mouse, MouseField::Period, 0) == 20
        && Field(mouse, MouseField::Dx, 0, true) == 35 && Field(mouse, MouseField::Dy, 0, true) == 45,
        "period latching preserves an active period and tracks turns");
    Check(periods.reads[0] == 2, "period expression evaluates only at openings");
    Input(mouse, periods, Move(3, 4, 38, 49), 3);
    Check(Near(Field(mouse, MouseField::Distance, 0), 5) && Field(mouse, MouseField::Remaining, 0) == 15,
        "path distance uses Euclidean segments");
}

void TestTimePhase()
{
    const std::array configs{MouseMeterConfig{EventTransition::Move, ExpressionType::Duration}};
    RuntimeMouseState mouse(configs, {80'000'000});
    Periods periods{Milliseconds(100)};
    mouse.Initialize({0, 0}, 0);
    Input(mouse, periods, Move(1, 0, 1, 0), 0);
    Input(mouse, periods, Move(50, 0, 51, 0), 50);
    Input(mouse, periods, Move(53, 0, 104, 0), 103);
    Check(mouse.Occurrences().size() == 1 && Field(mouse, MouseField::Progress, 0) == 3
        && Near(Field(mouse, MouseField::Distance, 0, true), 101)
        && Near(Field(mouse, MouseField::StartX, 0), 101),
        "late confirmation preserves the 100 ms logical boundary and splits displacement");
    Input(mouse, periods, Move(57, 0, 161, 0), 160);
    Input(mouse, periods, Move(43, 0, 204, 0), 203);
    Check(mouse.Occurrences().size() == 1 && Field(mouse, MouseField::Progress, 0) == 3,
        "next time boundary retains its phase");
    mouse.Refresh({204, 0}, 283'000'000);
    Check(Field(mouse, MouseField::Progress, 0) == 0 && Field(mouse, MouseField::Moving, 0) == 0
        && Field(mouse, MouseField::Valid, 0, true) == 1,
        "idle timeout clears unfinished time while retaining the latest completion");
    Input(mouse, periods, Move(1, 0, 205, 0), 283);
    Check(mouse.Occurrences().empty() && Field(mouse, MouseField::Progress, 0) == 0,
        "movement exactly at the timeout starts a fresh time span");

    periods.values[0] = Milliseconds(20);
    mouse.Restart(MeterId{0});
    Input(mouse, periods, Move(1, 0, 206, 0), 300);
    Input(mouse, periods, Move(65, 0, 271, 0), 365);
    Check(mouse.Occurrences().size() == 3 && Field(mouse, MouseField::Progress, 0) == 5,
        "one input confirms multiple time periods and retains the remainder");
}

void TestWheelAndLiveObservation()
{
    const std::array configs{MouseMeterConfig{EventTransition::Wheel}, MouseMeterConfig{EventTransition::HorizontalWheel}};
    RuntimeMouseState mouse(configs, {80'000'000});
    Periods periods{Number(1), Number(1)};
    mouse.Initialize({20, 30}, 0);
    mouse.Refresh({20, 30}, 10'000'000);
    Check(Field(mouse, MouseField::Moving) == 0 && Field(mouse, MouseField::IdleTime) == 10,
        "idle time starts at activation without claiming movement");
    Input(mouse, periods, Move(5, -2, 25, 28), 11);
    RuntimeInputEvent wheel{};
    wheel.device = DeviceKind::Mouse;
    wheel.transition = Transition::VerticalWheel;
    wheel.position = {25, 28};
    wheel.delta.wheelY = 0.25;
    Input(mouse, periods, wheel, 12);
    wheel.delta.wheelY = -0.1;
    Input(mouse, periods, wheel, 13);
    Check(Near(Field(mouse, MouseField::Progress, 0), 0.15)
        && Field(mouse, MouseField::Dx) == 0 && Field(mouse, MouseField::WheelX) == 0
        && Field(mouse, MouseField::WheelY) == -0.1,
        "wheel reversal cancels progress and numeric reports replace the whole delta tuple");
    wheel.delta.wheelY = 2.1;
    Input(mouse, periods, wheel, 14);
    Check(mouse.Occurrences().size() == 2 && Near(Field(mouse, MouseField::Progress, 0), 0.25),
        "fractional wheel overshoot conserves completed periods and remainder");
    wheel.transition = Transition::HorizontalWheel;
    wheel.delta = {0, 0, -1.5, 0};
    Input(mouse, periods, wheel, 15);
    Check(Field(mouse, MouseField::WheelX, 1, true) == -1 && Field(mouse, MouseField::Progress, 1) == -0.5
        && Field(mouse, MouseField::WheelY, 1, true) == 0, "horizontal wheel preserves its axis and sign");
    mouse.Refresh({200, 300}, 91'000'000);
    mouse.ResetMeters();
    Check(Field(mouse, MouseField::Moving) == 0 && Field(mouse, MouseField::IdleTime) == 80
        && Field(mouse, MouseField::WheelX) == -1.5 && Field(mouse, MouseField::X) == 200,
        "polling, idle, and source reset preserve the latest physical tuple");
    RuntimeInputEvent key{};
    mouse.Observe(key, 92'000'000);
    wheel.transition = Transition::Down;
    mouse.Observe(wheel, 93'000'000);
    Check(Field(mouse, MouseField::WheelX) == -1.5, "key and mouse buttons retain numeric observation");
}
void TestReportedMovementCoordinates()
{
    const std::array configs{MouseMeterConfig{}};
    RuntimeMouseState mouse(configs, {80'000'000});
    Periods periods{Number(8)};
    mouse.Initialize({0, 0}, 0);
    Input(mouse, periods, Move(5, 0, 5, 0), 1);
    mouse.Refresh({100, 0}, 2'000'000);
    Input(mouse, periods, Move(3, 0, 103, 0), 3);
    Check(Field(mouse, MouseField::Dx, 0, true) == 8 && Field(mouse, MouseField::Distance, 0, true) == 8
        && Field(mouse, MouseField::StartX, 0, true) == 0 && Field(mouse, MouseField::X, 0, true) == 103,
        "pointer relocation changes boundary coordinates without adding physical travel");
    mouse.Initialize({0, 0}, 0);
    Input(mouse, periods, Move(5, 0, 5, 0), 1);
    Input(mouse, periods, Move(5, 0, 5, 0), 2);
    Check(Field(mouse, MouseField::Dx, 0, true) == 8 && Field(mouse, MouseField::X, 0, true) == 3
        && Field(mouse, MouseField::Dx, 0) == 2 && Field(mouse, MouseField::X, 0) == 5,
        "consumed reports retain movement totals and interpolate within the actual report segment");
}
} // namespace

int main()
{
    TestDistanceAndSnapshots();
    TestDynamicPeriodsAndTurns();
    TestTimePhase();
    TestWheelAndLiveObservation();
    TestReportedMovementCoordinates();
    if (failures != 0) return 1;
    std::cout << "All mouse state tests passed.\n";
}
