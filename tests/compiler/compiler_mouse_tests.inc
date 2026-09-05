void TestMouseCompilation()
{
    using namespace inputweaver;
    const auto output = CompileGood("mouse.weave", R"(
TARGET = GLOBAL;
MOUSE_IDLE_TIMEOUT = 90ms;
number stride = 24;
number x = 0;
number y = 0;
duration interval = 100ms;
state movingNow = off;
number[] steps = [8, 24];
event travel = Mouse:move every stride;
event pulse = Mouse:move every interval;
event vertical = Mouse:wheel every 1;
event horizontal = Mouse:horizontalwheel every 0.25;
event fine = Mouse:move every steps[0] * 2;
travel:tick when @travel.dx > 0 ~>> set(x, @travel.start_x);
travel:tick ~> wait(100ms) move_by(@travel.dx, @travel.dy);
pulse:tick when @travel.valid == on ~> move_to(@travel.x, @travel.y);
vertical:tick ~> scroll(@vertical.wheel_y);
horizontal:tick ~> scroll_horizontal(@horizontal.wheel_x);
Mouse:move when Mouse.dx > 0 => set(x, Mouse.x);
Mouse:wheel when Mouse.wheel_y > 0 =>> scroll(0.25);
Mouse:horizontalwheel when Mouse.wheel_x < 0 ~>> scroll_horizontal(-1);
F1:down when @travel.valid == on ~> wait(1ms) move_to(@travel.start_x, @travel.start_y);
F2:down => restart(travel) set(stride, 40) move_by(0.5, -0.5);
A -> B when @travel.valid == on;
pause Pause:down when Mouse.moving == off ~> toggle;
exit F12:down when @travel.valid == on;
)", "mouse syntax");
    if (!output.Succeeded()) return;
    const auto program = DecodeGood(output, "mouse artifact");
    if (!program) return;
    Check(program->Settings().mouseIdleTimeout.nanoseconds == 90'000'000,
        "mouse idle setting is persisted");
    Check(program->EventSources().size() == 5, "each declaration has a distinct source identity");
    Check(program->Requirements().eventSourceCount == 5
        && program->Requirements().requiresMouseObservation
        && program->Requirements().requiresPointerOutput,
        "mouse program requirements are derived");
    const auto& travel = program->EventSources()[0];
    const auto& pulse = program->EventSources()[1];
    Check(program->Strings()[travel.name.value] == "travel"
        && program->Expressions()[travel.period.value].resultType == ExpressionType::Number
        && program->Expressions()[pulse.period.value].resultType == ExpressionType::Duration,
        "source names and distance/time period types survive the codec");
    Check(std::count_if(program->EventBuckets().begin(), program->EventBuckets().end(),
        [](const auto& bucket) { return bucket.key.source == EventSourceId{0}; }) == 1,
        "shared subscriptions use one named event bucket");
    Check(EncodeWeavec(*program) == output.artifact, "mouse artifact round trip is deterministic");
    const auto dump = DumpCompiledProgram(*program);
    Check(dump.find("load-field") != std::string::npos && dump.find("restart-event") != std::string::npos
        && dump.find("event-sources 5") != std::string::npos && dump.find("Mouse:move") != std::string::npos,
        "dump includes mouse fields, sources, actions, and raw keys");

    const std::string declarations =
        "number n = 0; duration d = 0ms; state s = off; "
        "event path = Mouse:move every 24; event pulse = Mouse:move every 100ms; "
        "event wheelSource = Mouse:wheel every 1; ";
    const std::vector<std::pair<std::string, std::string>> fieldGroups{
        {"n", "Mouse.x Mouse.y Mouse.dx Mouse.dy Mouse.wheel_x Mouse.wheel_y "
            "path.start_x path.start_y path.x path.y path.dx path.dy path.distance "
            "path.period path.progress path.remaining wheelSource.x wheelSource.y "
            "wheelSource.wheel_x wheelSource.wheel_y wheelSource.period "
            "wheelSource.progress wheelSource.remaining "
            "@path.start_x @path.start_y @path.x @path.y @path.dx @path.dy @path.distance @path.period "
            "@wheelSource.x @wheelSource.y @wheelSource.wheel_x @wheelSource.wheel_y @wheelSource.period"},
        {"d", "Mouse.idle_time MOUSE_IDLE_TIMEOUT pulse.period pulse.progress pulse.remaining @pulse.period"},
        {"s", "Mouse.moving path.moving @path.valid @pulse.valid @wheelSource.valid"},
    };
    std::string fields = declarations + "F1:down ~> ";
    for (const auto& [target, group] : fieldGroups) {
        std::size_t begin = 0;
        while (begin < group.size()) {
            const auto end = group.find(' ', begin);
            fields += "set(" + target + ", " + group.substr(begin, end - begin) + ") ";
            if (end == std::string::npos) break;
            begin = end + 1;
        }
    }
    fields += ';';
    const auto fieldOutput = CompileGood("mouse-fields.weave", fields, "typed mouse and source fields");
    if (fieldOutput.Succeeded()) {
        const auto fieldProgram = DecodeGood(fieldOutput, "field artifact");
        if (fieldProgram) Check(fieldProgram->Settings().mouseIdleTimeout.nanoseconds == 80'000'000,
            "mouse idle timeout defaults to 80 ms");
    }
}
