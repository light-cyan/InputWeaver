#include "language/lexer.hpp"
#include "language/word_catalog.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int gFailureCount = 0;

void Check(bool condition, std::string_view name)
{
    if (!condition) {
        ++gFailureCount;
        std::cerr << "FAIL: " << name << '\n';
    }
}

[[nodiscard]] bool HasIssue(
    const inputweaver::language::ScanResult& result,
    inputweaver::language::LexicalIssueCode code) noexcept
{
    return std::any_of(
        result.issues.begin(),
        result.issues.end(),
        [code](const auto& issue) { return issue.code == code; });
}

[[nodiscard]] std::size_t CountKind(
    const inputweaver::language::ScanResult& result,
    inputweaver::language::LexemeKind kind) noexcept
{
    return static_cast<std::size_t>(std::count_if(
        result.lexemes.begin(),
        result.lexemes.end(),
        [kind](const auto& lexeme) { return lexeme.kind == kind; }));
}

void TestLexemeKindsAndSpans()
{
    using namespace inputweaver::language;
    constexpr std::string_view source =
        "state[] values = [1, 2.5]; A:down => tap(B) | wait(20ms); "
        "x == 0x2A != 3 <= 4 >= 2 + 1 - 1 * 2 / 1 % 2 -> ~> =>> ~>>";
    const ScanResult result = ScanWeave(source);
    Check(result.issues.empty(), "valid source has no lexical issues");
    Check(CountKind(result, LexemeKind::Word) >= 8U, "words are emitted");
    Check(CountKind(result, LexemeKind::Number) >= 8U, "numbers are emitted");
    Check(CountKind(result, LexemeKind::Duration) == 1U, "duration is emitted");
    Check(CountKind(result, LexemeKind::HexInteger) == 1U, "hexadecimal integer is emitted");
    Check(CountKind(result, LexemeKind::ConsumeStop) == 1U, "consume-stop arrow is emitted");
    Check(CountKind(result, LexemeKind::ConsumeContinue) == 1U, "consume-continue arrow is emitted");
    Check(CountKind(result, LexemeKind::ObserveStop) == 1U, "observe-stop arrow is emitted");
    Check(CountKind(result, LexemeKind::ObserveContinue) == 1U, "observe-continue arrow is emitted");
    Check(CountKind(result, LexemeKind::MappingArrow) == 1U, "mapping arrow is emitted");
    for (const Lexeme& lexeme : result.lexemes) {
        if (lexeme.kind == LexemeKind::EndOfInput) {
            continue;
        }
        Check(lexeme.span.EndByte() <= source.size() && source.substr(lexeme.span.beginByte, lexeme.span.byteLength) == lexeme.text, "every lexeme retains its exact source span");
    }
}

void TestCommentsAndIncrementalState()
{
    using namespace inputweaver::language;
    constexpr std::string_view source = "state x = off; /* comment\ncontinues */ x == on; // tail\n";
    const ScanResult whole = ScanWeave(source);
    Check(whole.issues.empty(), "complete comments scan without issues");

    std::vector<Lexeme> incremental;
    std::vector<LexicalIssue> incrementalIssues;
    LexerState state{};
    std::size_t begin = 0U;
    while (begin < source.size()) {
        const std::size_t newline = source.find('\n', begin);
        const std::size_t end = newline == std::string_view::npos ? source.size() : newline + 1U;
        ScanOptions options{};
        options.baseByteOffset = begin;
        options.initialState = state;
        options.finalInput = end == source.size();
        ScanResult line = ScanWeave(source.substr(begin, end - begin), options);
        state = line.finalState;
        line.lexemes.pop_back();
        incremental.insert(incremental.end(), line.lexemes.begin(), line.lexemes.end());
        incrementalIssues.insert(incrementalIssues.end(), line.issues.begin(), line.issues.end());
        begin = end;
    }
    Check(state.blockCommentDepth == 0U, "incremental block-comment state closes");
    Check(incrementalIssues.empty(), "incremental comments produce the same clean issue state");
    const auto significant = [](const std::vector<Lexeme>& lexemes) {
        std::vector<Lexeme> result;
        std::copy_if(lexemes.begin(), lexemes.end(), std::back_inserter(result), [](const Lexeme& lexeme) { return !IsTrivia(lexeme.kind) && lexeme.kind != LexemeKind::EndOfInput; });
        return result;
    };
    const std::vector<Lexeme> wholeSignificant = significant(whole.lexemes);
    const std::vector<Lexeme> incrementalSignificant = significant(incremental);
    Check(incrementalSignificant.size() == wholeSignificant.size(), "whole and incremental scans emit the same significant lexeme count");
    if (incrementalSignificant.size() == wholeSignificant.size()) {
        for (std::size_t index = 0U; index < incrementalSignificant.size(); ++index) {
            Check(incrementalSignificant[index].kind == wholeSignificant[index].kind && incrementalSignificant[index].span.beginByte == wholeSignificant[index].span.beginByte && incrementalSignificant[index].span.byteLength == wholeSignificant[index].span.byteLength, "whole and incremental significant lexeme boundaries match");
        }
    }

    const ScanResult nested = ScanWeave("/* outer /* nested */ outer */");
    Check(HasIssue(nested, LexicalIssueCode::NestedBlockComment), "nested block comments are diagnosed");
    const ScanResult unterminated = ScanWeave("/* open");
    Check(HasIssue(unterminated, LexicalIssueCode::UnterminatedBlockComment), "unterminated block comments are diagnosed at final input");
}

void TestStringsAndInvalidInput()
{
    using namespace inputweaver::language;
    constexpr std::string_view literal = R"weave("a\\b\"c\nd\re\tf")weave";
    const StringDecodeResult decoded = DecodeStringLiteral(literal, 7U);
    Check(decoded.terminated && decoded.issues.empty() && decoded.value == "a\\b\"c\nd\re\tf", "the complete string escape set decodes exactly");

    const ScanResult invalidEscape = ScanWeave(R"weave("bad\q")weave");
    Check(HasIssue(invalidEscape, LexicalIssueCode::InvalidEscape), "invalid string escapes are diagnosed");
    const ScanResult unterminated = ScanWeave("\"open");
    Check(HasIssue(unterminated, LexicalIssueCode::UnterminatedString) && CountKind(unterminated, LexemeKind::IncompleteString) == 1U, "unterminated strings remain visible as incomplete lexemes");
    const ScanResult nonAscii = ScanWeave("\xe4\xb8\xad");
    Check(HasIssue(nonAscii, LexicalIssueCode::NonAsciiSyntax), "non-ASCII syntax is diagnosed");
    const std::string embeddedNul{"// comment\0tail", 15U};
    const ScanResult nul = ScanWeave(embeddedNul);
    Check(HasIssue(nul, LexicalIssueCode::EmbeddedNul), "embedded NUL is diagnosed inside comments");

    const StringDecodeResult bounded = DecodeStringLiteral(
        R"weave("\q\q")weave", 0U, 1U);
    Check(bounded.terminated && bounded.issues.size() == 1U
        && bounded.issueLimitReached,
        "string decoding bounds issues without using a separate validation path");
}

void TestNumberBoundaries()
{
    using namespace inputweaver::language;
    struct NumberCase final {
        std::string_view text;
        LexemeKind kind;
    };
    constexpr NumberCase valid[]{
        {"0", LexemeKind::Number}, {"12", LexemeKind::Number},
        {"3.5", LexemeKind::Number}, {"30ms", LexemeKind::Duration},
        {"1.5s", LexemeKind::Duration}, {"2min", LexemeKind::Duration},
        {"0x2A", LexemeKind::HexInteger}};
    for (const NumberCase& value : valid) {
        const ScanResult result = ScanWeave(value.text);
        Check(result.issues.empty() && result.lexemes.size() == 2U
            && result.lexemes.front().kind == value.kind
            && result.lexemes.front().span.beginByte == 0U
            && result.lexemes.front().span.byteLength == value.text.size()
            && result.lexemes.front().text == value.text,
            "valid numbers retain one exact lexeme boundary");
    }

    constexpr std::string_view invalid[]{
        "0x", "1.", ".5", "1e2", "1e-2", "0x2G", "1foo",
        "1do", "1msfoo", "1.2.3"};
    for (const std::string_view value : invalid) {
        const ScanResult result = ScanWeave(value);
        Check(HasIssue(result, LexicalIssueCode::InvalidNumber)
            && result.lexemes.size() == 2U
            && result.lexemes.front().kind == LexemeKind::Invalid
            && result.lexemes.front().span.beginByte == 0U
            && result.lexemes.front().span.byteLength == value.size()
            && result.lexemes.front().text == value,
            "invalid numbers are consumed as one exact invalid lexeme");
    }
}

void TestLimitsAndCatalog()
{
    using namespace inputweaver::language;
    ScanOptions options{};
    options.maximumSignificantLexemes = 2U;
    const ScanResult limited = ScanWeave("a + b", options);
    Check(limited.significantLexemeLimitReached && CountKind(limited, LexemeKind::Word) == 1U && CountKind(limited, LexemeKind::Plus) == 1U, "the significant lexeme limit stops before the next token");

    options = {};
    options.maximumStoredLexemes = 2U;
    const ScanResult triviaLimited = ScanWeave("/**//**//**/", options);
    Check(triviaLimited.storedLexemeLimitReached
        && CountKind(triviaLimited, LexemeKind::BlockComment) == 2U
        && triviaLimited.lexemes.back().span.beginByte == 8U,
        "the stored lexeme budget bounds lossless trivia output");

    options = {};
    options.maximumIssues = 2U;
    const ScanResult issueLimited = ScanWeave(
        "/* /* /* /* */ */ */ */", options);
    Check(issueLimited.issueLimitReached && issueLimited.issues.size() == 2U
        && CountKind(issueLimited, LexemeKind::BlockComment) == 1U
        && issueLimited.finalState.blockCommentDepth == 0U,
        "the issue budget bounds one issue-dense lexeme without stopping progress");

    options = {};
    options.retainTrivia = false;
    const ScanResult significantOnly = ScanWeave(" /**/ value // tail", options);
    Check(significantOnly.lexemes.size() == 2U
        && significantOnly.lexemes.front().kind == LexemeKind::Word
        && significantOnly.lexemes.front().text == "value",
        "callers can discard trivia without a second scanner");
    Check(LookupWordRole("state") == WordRole::Type && LookupWordRole("held") == WordRole::Constant && LookupWordRole("append") == WordRole::Action && LookupWordRole("length") == WordRole::Property, "the word catalog exposes V3 lexical roles");
    Check(IsReservedLanguageWord("PAUSE") && !IsReservedLanguageWord("userValue")
        && LookupWordRole("Windows.VirtualKey") == WordRole::RawControl
        && LookupWordRole("and") == WordRole::None
        && LookupWordRole("or") == WordRole::None
        && LookupWordRole("not") == WordRole::None
        && IsReservedLanguageWord("and") && IsReservedLanguageWord("or")
        && IsReservedLanguageWord("not"),
        "the word catalog owns reserved and dotted raw-control names");
}

} // namespace

int main()
{
    TestLexemeKindsAndSpans();
    TestCommentsAndIncrementalState();
    TestStringsAndInvalidInput();
    TestNumberBoundaries();
    TestLimitsAndCatalog();
    if (gFailureCount != 0) {
        std::cerr << gFailureCount << " language test(s) failed.\n";
        return 1;
    }
    std::cout << "All language tests passed.\n";
    return 0;
}
