    [[nodiscard]] TopLevelSyntax ParseMeterDeclaration(SourceSpan begin)
    {
        TopLevelSyntax item{};
        item.kind = TopLevelSyntax::Kind::MeterDeclaration;
        item.name = ExpectWordToken("a meter name").text;
        Expect(LexemeKind::Equal, "'='");
        item.event = ParseEvent();
        ExpectWord("every");
        item.condition = ParseExpression();
        const auto close = Expect(LexemeKind::Semicolon, "';'");
        item.span = MergeSpans(begin, close.span);
        CountNode(item.span);
        return item;
    }

    [[nodiscard]] ActionSyntax ParseMouseAction(const ParserLexeme& name)
    {
        ActionSyntax action{};
        action.kind = name.text == "restart" ? ActionSyntax::Kind::RestartMeter : ActionSyntax::Kind::Pointer;
        action.name = name.text;
        Expect(LexemeKind::LeftParen, "'('");
        if (action.kind == ActionSyntax::Kind::RestartMeter) {
            action.secondaryName = ExpectWordToken("a meter name").text;
        } else {
            action.expression = ParseExpression();
            if (name.text == "move_by" || name.text == "move_to") {
                Expect(LexemeKind::Comma, "','");
                action.secondExpression = ParseExpression();
            }
        }
        const auto close = Expect(LexemeKind::RightParen, "')'");
        action.span = MergeSpans(name.span, close.span);
        CountNode(action.span);
        return action;
    }
