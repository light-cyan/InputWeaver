void TestMouseHighlighting()
{
    using namespace inputweaver::ui::tui;
    const auto colorAt = [](std::span<const SourceTokenSpan> spans, std::size_t byte) {
        for (const auto& span : spans) {
            if (span.beginByte <= byte && byte < span.endByte) return span.kind;
        }
        return SourceTokenKind::Plain;
    };
    SourceHighlightState state;
    (void)HighlightWeaveLine("number dx = 0; state valid = off; number[] values = [1];", state);
    const auto declaration = HighlightWeaveLine("meter path = Mouse:move every 24;", state);
    Check(state.meterNames.contains("path") && !state.scalarNames.contains("path")
        && colorAt(declaration, 0U) == SourceTokenKind::Type
        && colorAt(declaration, 6U) == SourceTokenKind::Variable
        && colorAt(declaration, 13U) == SourceTokenKind::Control,
        "meter symbols are distinct from scalars and Mouse event sources use control color");
    (void)HighlightWeaveLine("meter pulse = Mouse:move every 100ms;", state);
    (void)HighlightWeaveLine("meter vertical = Mouse:wheel every 1;", state);
    (void)HighlightWeaveLine("meter horizontal = Mouse:horizontalwheel every 0.25;", state);

    const std::vector<std::pair<std::string, std::vector<std::string_view>>> fields{
        {"Mouse", {"x", "y", "dx", "dy", "wheel_x", "wheel_y", "moving", "idle_time"}},
        {"path", {"start_x", "start_y", "x", "y", "dx", "dy", "distance", "moving",
            "period", "progress", "remaining"}},
        {"@path", {"start_x", "start_y", "x", "y", "dx", "dy", "distance", "period", "valid"}},
        {"pulse", {"period", "progress", "remaining", "moving"}},
        {"@pulse", {"period", "valid"}},
        {"vertical", {"x", "y", "wheel_x", "wheel_y", "period", "progress", "remaining"}},
        {"@vertical", {"x", "y", "wheel_x", "wheel_y", "period", "valid"}},
        {"horizontal", {"wheel_x", "wheel_y", "progress", "remaining"}},
        {"@horizontal", {"wheel_x", "wheel_y", "valid"}},
    };
    for (const auto& [owner, members] : fields) {
        for (const auto member : members) {
            const auto source = owner + "." + std::string(member);
            const auto spans = HighlightWeaveLine(source, state);
            bool correct = true;
            for (std::size_t byte = 0U; byte < source.size(); ++byte) {
                correct = correct && colorAt(spans, byte) == (source[byte] == '.'
                    ? SourceTokenKind::Plain : SourceTokenKind::Variable);
            }
            Check(correct, "all live and completed mouse fields use variable color except the dot: " + source);
        }
    }
    for (const auto source : {"unknown.dx", "dx.dx", "values.dx", "Mouse.valid",
            "path.idle_time", "path.unknown", "@missing.valid", "path.dx.dx"}) {
        const std::string_view text = source;
        const auto spans = HighlightWeaveLine(text, state);
        Check(colorAt(spans, text.rfind('.') + 1U) == SourceTokenKind::Plain,
            "member coloring does not leak from unrelated variables or owners");
    }
    constexpr std::string_view actions =
        "path:tick ~> restart(path) move_by(@path.dx, Mouse.dy) move_to(Mouse.x, Mouse.y) "
        "scroll(@vertical.wheel_y) scroll_horizontal(@horizontal.wheel_x);";
    const auto actionSpans = HighlightWeaveLine(actions, state);
    for (const auto action : {"restart", "move_by", "move_to", "scroll", "scroll_horizontal"}) {
        Check(colorAt(actionSpans, actions.find(action)) == SourceTokenKind::Action,
            "mouse actions retain action color");
    }
    Check(colorAt(actionSpans, actions.find("tick")) == SourceTokenKind::Constant
        && colorAt(actionSpans, actions.find('~')) == SourceTokenKind::Operator
        && colorAt(actionSpans, actions.find('@')) == SourceTokenKind::Variable
        && colorAt(actionSpans, actions.find('(')) == SourceTokenKind::Plain
        && colorAt(actionSpans, actions.find(':')) == SourceTokenKind::Plain,
        "ticks, arrows, snapshots, and delimiters retain distinct roles");
    for (const auto event : {"move", "wheel", "horizontalwheel"}) {
        const std::string source = "Mouse:" + std::string(event);
        const auto spans = HighlightWeaveLine(source, state);
        Check(colorAt(spans, 0U) == SourceTokenKind::Control
            && colorAt(spans, 5U) == SourceTokenKind::Plain
            && colorAt(spans, 6U) == SourceTokenKind::Constant,
            "all raw mouse transitions have the same palette as button transitions");
    }
    for (const auto button : {"Left", "Right", "Middle", "X1", "X2"}) {
        const std::string source = "Mouse." + std::string(button) + ":down";
        const auto spans = HighlightWeaveLine(source, state);
        Check(colorAt(spans, 0U) == SourceTokenKind::Control
            && colorAt(spans, 5U) == SourceTokenKind::Plain
            && colorAt(spans, 6U) == SourceTokenKind::Control,
            "mouse buttons remain controls");
    }

    SourceEditor editor;
    editor.Set("meter /* declaration */\npath = Mouse\n:move every 24;\n"
        "@ /* snapshot */ path /* member */\n.\nstart_x;\n"
        "Mouse\n/* button */ . Left:down;\n"
        "Mouse\n. /* position */ x;\n"
        "path\n:tick;\n@path.");
    SourceHighlightDocument document;
    Check(document.Update(editor), "multiline mouse references build highlights");
    Check(colorAt(document.Line(0U), 0U) == SourceTokenKind::Type
        && colorAt(document.Line(1U), 0U) == SourceTokenKind::Variable
        && colorAt(document.Line(1U), 7U) == SourceTokenKind::Control
        && colorAt(document.Line(2U), 1U) == SourceTokenKind::Constant
        && colorAt(document.Line(3U), 0U) == SourceTokenKind::Variable
        && colorAt(document.Line(3U), 2U) == SourceTokenKind::Comment
        && colorAt(document.Line(5U), 0U) == SourceTokenKind::Variable
        && colorAt(document.Line(6U), 0U) == SourceTokenKind::Control
        && colorAt(document.Line(7U), 15U) == SourceTokenKind::Control
        && colorAt(document.Line(8U), 0U) == SourceTokenKind::Variable
        && colorAt(document.Line(9U), 17U) == SourceTokenKind::Variable
        && colorAt(document.Line(10U), 0U) == SourceTokenKind::Variable
        && colorAt(document.Line(11U), 1U) == SourceTokenKind::Constant
        && colorAt(document.Line(12U), 0U) == SourceTokenKind::Variable
        && colorAt(document.Line(12U), 1U) == SourceTokenKind::Variable
        && colorAt(document.Line(12U), 5U) == SourceTokenKind::Plain,
        "comments, newlines, and incomplete snapshots preserve owner context");
    for (std::size_t line = 0U; line < editor.LineCount(); ++line) {
        std::size_t end = 0U;
        for (const auto& span : document.Line(line)) {
            Check(span.beginByte >= end && span.endByte <= editor.Line(line).size()
                && span.kind != SourceTokenKind::Plain,
                "deferred reference spans stay sorted, disjoint, and line-local");
            end = span.endByte;
        }
    }
    editor.Set("meter path = Mouse:move every 24;\n@path.dx");
    Check(document.Update(editor) && colorAt(document.Line(1U), 6U) == SourceTokenKind::Variable,
        "a declared meter colors subsequent fields");
    editor.Set("number path = 0;\n@path.dx");
    Check(document.Update(editor) && colorAt(document.Line(1U), 6U) == SourceTokenKind::Plain,
        "changing a meter to a scalar invalidates member highlighting");
    editor.Set("\n@path.dx");
    Check(document.Update(editor) && colorAt(document.Line(1U), 1U) == SourceTokenKind::Plain,
        "removing a meter declaration invalidates name highlighting");

    FakePlatform platform;
    platform.source = "meter path = Mouse:move every 24;\n"
        "path:tick ~> move_by(@path.dx, Mouse.dy);\n"
        "Mouse.Left:down ~> restart(path);";
    inputweaver::app::Application application(platform);
    Check(application.Initialize().succeeded, "mouse source rendering initializes");
    const auto colors = LoadColors();
    TuiController controller(application, colors);
    const auto canvas = controller.Render(150U, 30U);
    const auto hasColor = [&](std::string_view token, std::size_t offset, RgbColor color) {
        const auto position = FindAscii(canvas, token);
        return position + offset < canvas.Cells().size()
            && canvas.Cells()[position + offset].style.foreground == color;
    };
    Check(hasColor("@path.dx", 0U, colors.syntaxVariable)
        && hasColor("@path.dx", 1U, colors.syntaxVariable)
        && hasColor("@path.dx", 5U, colors.text)
        && hasColor("@path.dx", 6U, colors.syntaxVariable)
        && hasColor("Mouse.dy", 0U, colors.syntaxVariable)
        && hasColor("Mouse.dy", 6U, colors.syntaxVariable)
        && hasColor("Mouse:move", 0U, colors.syntaxControl)
        && hasColor("Mouse.Left", 0U, colors.syntaxControl)
        && hasColor("Mouse.Left", 6U, colors.syntaxControl)
        && hasColor("~>", 0U, colors.syntaxOperator)
        && hasColor("move_by", 0U, colors.syntaxAction),
        "the rendered SOURCE cells use the configured mouse and snapshot palette");
}
