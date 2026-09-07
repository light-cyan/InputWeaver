    [[nodiscard]] MeterId ResolveMeter(std::string_view name, SourceSpan span)
    {
        const auto found = symbols_.find(name);
        if (found != symbols_.end() && found->second.meter.IsValid()) {
            return found->second.meter;
        }
        diagnostics_.Add(CompileDiagnosticCode::UnknownValue, span,
            "unknown meter '" + std::string(name) + "'");
        return {};
    }

    [[nodiscard]] std::optional<EventTransition> BindMouseTransition(const EventSyntax& event)
    {
        if (event.control.name == "Mouse" && !event.control.raw) {
            if (event.transition == "move") return EventTransition::Move;
            if (event.transition == "wheel") return EventTransition::Wheel;
            if (event.transition == "horizontalwheel") return EventTransition::HorizontalWheel;
        }
        ReportType(event.span, "mouse events use Mouse:move, Mouse:wheel, or Mouse:horizontalwheel");
        return std::nullopt;
    }

    void BindMeterDeclaration(const TopLevelSyntax& item)
    {
        if (!CheckDeclarationName(item)) return;
        const auto transition = BindMouseTransition(item.event);
        bindingPeriod_ = true;
        auto period = BindExpression(*item.condition);
        bindingPeriod_ = false;
        if (!transition.has_value()) return;
        if (period->type != ExpressionType::Number
            && !(*transition == EventTransition::Move && period->type == ExpressionType::Duration)) {
            ReportType(item.condition->span, "move periods require Number or Duration; wheel periods require Number");
        }
        const auto* number = std::get_if<double>(&period->constant);
        const auto* duration = std::get_if<DurationValue>(&period->constant);
        if ((number != nullptr && *number <= 0.0) || (duration != nullptr && duration->nanoseconds <= 0)) {
            ReportType(item.condition->span, "meter period must be positive");
        }
        const MeterId id{static_cast<std::uint32_t>(program_.meters.size())};
        program_.meters.push_back({item.name, *transition, std::move(period), item.span});
        Symbol symbol{};
        symbol.meter = id;
        symbol.declaration = item.span;
        symbols_.emplace(item.name, symbol);
    }

    [[nodiscard]] bool IsFieldReference(std::string_view name) const
    {
        const auto dot = name.find('.');
        if (dot == std::string_view::npos) return false;
        const auto subject = name.substr(0, dot);
        if (subject == "Mouse") return !ResolveNamedControl(name).has_value();
        const auto found = symbols_.find(subject);
        return found != symbols_.end() && found->second.meter.IsValid();
    }

    [[nodiscard]] std::unique_ptr<BoundExpression> BindField(const ExpressionSyntax& syntax)
    {
        if (bindingPeriod_) {
            ReportType(syntax.span, "mouse and meter fields require a rule evaluation context");
            return MakeErrorExpression(syntax.span);
        }
        const std::string_view name = syntax.reference.name;
        const auto dot = name.find('.');
        const auto subject = name.substr(0, dot);
        const auto field = dot == std::string_view::npos ? std::string_view{} : name.substr(dot + 1);
        const auto* found = language::FindMouseFieldWord(field);
        if (found == nullptr) {
            ReportType(syntax.span, "unknown mouse or meter field '" + std::string(field) + "'");
            return MakeErrorExpression(syntax.span);
        }
        // Source vocabulary and artifact field identifiers have separate owners.
        static constexpr std::array fields{
            MouseField::X, MouseField::Y, MouseField::Dx, MouseField::Dy,
            MouseField::WheelX, MouseField::WheelY, MouseField::Moving, MouseField::IdleTime,
            MouseField::StartX, MouseField::StartY, MouseField::Distance,
            MouseField::Period, MouseField::Progress, MouseField::Remaining, MouseField::Valid,
        };
        static_assert(fields.size() == language::kMouseFieldWords.size());
        const auto fieldIndex = static_cast<std::size_t>(found - language::kMouseFieldWords.data());
        MouseFieldReference reference{{}, fields[fieldIndex], syntax.completed};
        auto transition = EventTransition::Move;
        auto periodType = ExpressionType::Number;
        if (subject != "Mouse") {
            reference.source = ResolveMeter(subject, syntax.span);
            if (!reference.source.IsValid()) return MakeErrorExpression(syntax.span);
            const auto& source = program_.meters[reference.source.value];
            transition = source.transition;
            periodType = source.period->type;
        }
        const auto type = MouseFieldType(reference, transition, periodType);
        if (type == ExpressionType::None) {
            ReportType(syntax.span, "field is not available on this source view");
            return MakeErrorExpression(syntax.span);
        }
        auto expression = std::make_unique<BoundExpression>();
        expression->kind = BoundExpression::Kind::LoadField;
        expression->type = type;
        expression->span = syntax.span;
        expression->field = reference;
        return expression;
    }

    void BindRuleEvent(const TopLevelSyntax& item, BoundRule& rule)
    {
        const auto& event = item.event;
        if (item.kind == TopLevelSyntax::Kind::EventRule
            && (event.control.name == "Mouse" || event.transition == "tick")) {
            if (event.control.name == "Mouse") {
                if (const auto transition = BindMouseTransition(event)) rule.transition = *transition;
            } else {
                rule.meter = ResolveMeter(event.control.name, event.span);
                rule.transition = EventTransition::Tick;
                if (item.arrow == RuleArrowSyntax::ConsumeStop || item.arrow == RuleArrowSyntax::ConsumeContinue) {
                    ReportType(item.span, "tick subscriptions require an observation arrow");
                }
            }
        } else {
            if (const auto source = BindControl(event.control)) rule.source = *source;
            if (const auto transition = BindTransition(event.transition, event.span)) rule.transition = *transition;
        }
    }

    void BindPointerAction(const ActionSyntax& item, BoundAction& action)
    {
        action.kind = BoundAction::Kind::Pointer;
        if (item.name == "move_by") action.pointerOperation = PointerOperation::MoveBy;
        if (item.name == "move_to") action.pointerOperation = PointerOperation::MoveTo;
        if (item.name == "scroll") action.pointerOperation = PointerOperation::Scroll;
        if (item.name == "scroll_horizontal") action.pointerOperation = PointerOperation::ScrollHorizontal;
        action.expression = BindExpression(*item.expression);
        RequireType(*action.expression, ExpressionType::Number, item.span, "pointer arguments require Number");
        if (item.secondExpression) {
            action.secondExpression = BindExpression(*item.secondExpression);
            RequireType(*action.secondExpression, ExpressionType::Number, item.span, "pointer arguments require Number");
        }
    }
