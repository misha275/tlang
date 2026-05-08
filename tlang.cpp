#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <regex>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

struct CompileError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

enum class DiagnosticKind { Info, Success, Warning, Error };

struct TerminalStyle {
    bool useColor = false;
};

static bool is_color_allowed_by_env() {
    const char* noColor = std::getenv("NO_COLOR");
    if (noColor && *noColor) return false;
    const char* term = std::getenv("TERM");
    if (term && std::string(term) == "dumb") return false;
    return true;
}

#ifdef _WIN32
static bool enable_virtual_terminal(HANDLE handle) {
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr) return false;
    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode)) return false;
    if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0) return true;
    return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}
#endif

static TerminalStyle make_terminal_style() {
    TerminalStyle style;
    if (!is_color_allowed_by_env()) return style;
#ifdef _WIN32
    const bool stdoutOk = enable_virtual_terminal(GetStdHandle(STD_OUTPUT_HANDLE));
    const bool stderrOk = enable_virtual_terminal(GetStdHandle(STD_ERROR_HANDLE));
    style.useColor = stdoutOk || stderrOk;
#else
    style.useColor = true;
#endif
    return style;
}

static const char* color_code(const TerminalStyle& style, DiagnosticKind kind) {
    if (!style.useColor) return "";
    switch (kind) {
        case DiagnosticKind::Info: return "\x1b[36m";
        case DiagnosticKind::Success: return "\x1b[32m";
        case DiagnosticKind::Warning: return "\x1b[33m";
        case DiagnosticKind::Error: return "\x1b[31m";
    }
    return "";
}

static const char* reset_code(const TerminalStyle& style) {
    return style.useColor ? "\x1b[0m" : "";
}

static std::string diagnostic_label(DiagnosticKind kind) {
    switch (kind) {
        case DiagnosticKind::Info: return "info";
        case DiagnosticKind::Success: return "success";
        case DiagnosticKind::Warning: return "warning";
        case DiagnosticKind::Error: return "error";
    }
    return "info";
}

static void print_diagnostic(std::ostream& out, const TerminalStyle& style, DiagnosticKind kind, const std::string& message) {
    out << color_code(style, kind) << diagnostic_label(kind) << ": " << reset_code(style) << message << "\n";
}

struct ParsedDiagnostic {
    std::string file;
    int line = 0;
    int col = 0;
    std::string details;
};

static std::optional<ParsedDiagnostic> parse_location_message(const std::string& message) {
    static const std::regex pattern(R"(^(.*):([0-9]+):([0-9]+):\s*(.+)$)");
    std::smatch match;
    if (!std::regex_match(message, match, pattern) || match.size() != 5) return std::nullopt;
    ParsedDiagnostic parsed;
    parsed.file = match[1].str();
    parsed.line = std::stoi(match[2].str());
    parsed.col = std::stoi(match[3].str());
    parsed.details = match[4].str();
    return parsed;
}

static std::optional<std::string> read_source_line(const std::string& file, int lineNumber) {
    if (lineNumber <= 0) return std::nullopt;
    std::ifstream in{fs::path(file), std::ios::binary};
    if (!in) return std::nullopt;
    std::string sourceLine;
    for (int lineIndex = 1; lineIndex <= lineNumber; ++lineIndex) {
        if (!std::getline(in, sourceLine)) return std::nullopt;
    }
    if (!sourceLine.empty() && sourceLine.back() == '\r') sourceLine.pop_back();
    return sourceLine;
}

static std::string make_caret_line(const std::string& sourceLine, int col) {
    int safeCol = std::max(1, col);
    std::string caret;
    caret.reserve(static_cast<std::size_t>(safeCol + 1));
    for (int i = 1; i < safeCol; ++i) {
        if (i - 1 < static_cast<int>(sourceLine.size()) && sourceLine[static_cast<std::size_t>(i - 1)] == '\t') caret += '\t';
        else caret += ' ';
    }
    caret += '^';
    return caret;
}

static std::optional<std::string> diagnostic_hint_for(const std::string& message) {
    if (message.find("invalid command") != std::string::npos || message.find("unknown option") != std::string::npos ||
        message.find("requires a value") != std::string::npos) {
        return "use: tlang compile INPUT [--emit-cpp FILE] [--cpp-only] [--windows-gui] [-o OUTPUT]";
    }
    if (message.find("cannot open input file") != std::string::npos) {
        return "check path to input file and current working directory";
    }
    if (message.find("cannot find library") != std::string::npos) {
        return "place .tlib in ./libraries near project or next to input file";
    }
    if (message.find("C++ compiler failed") != std::string::npos) {
        return "install g++ (MinGW/LLVM), ensure it is in PATH, and rerun with --emit-cpp to inspect generated C++";
    }
    if (message.find("forbidden character") != std::string::npos) {
        return "remove unsupported symbols; use letters, digits, spaces and supported punctuation";
    }
    if (message.find("expected END LINE") != std::string::npos) {
        return "finish each statement with: END LINE";
    }
    if (message.find("expected statement") != std::string::npos || message.find("expected expression") != std::string::npos) {
        return "check previous line for missing keywords (SET/RETURN/IF/WHILE/CALL) or missing END LINE";
    }
    if (message.find("undeclared identifier") != std::string::npos) {
        return "declare variable before usage: LET name AS TYPE VALUE ... END LINE";
    }
    if (message.find("unknown function") != std::string::npos) {
        return "check function name spelling and ensure FUNCTION START ... FUNCTION END exists";
    }
    if (message.find("type mismatch") != std::string::npos) {
        return "make both sides the same type or convert expression to the declared type";
    }
    if (message.find("duplicate") != std::string::npos) {
        return "rename one of the duplicated declarations or function parameters";
    }
    if (message.find("missing MAIN function") != std::string::npos) {
        return "add entry point: FUNCTION START MAIN RETURNS NUMBER END LINE ... FUNCTION END MAIN END LINE";
    }
    if (message.find("unclosed text literal") != std::string::npos) {
        return "close text literal with: TEXT END";
    }
    return std::nullopt;
}

static void print_compile_error(const TerminalStyle& style, const std::string& message) {
    if (std::optional<ParsedDiagnostic> parsed = parse_location_message(message); parsed.has_value()) {
        const ParsedDiagnostic& d = *parsed;
        print_diagnostic(std::cerr, style, DiagnosticKind::Error, d.details);
        print_diagnostic(std::cerr, style, DiagnosticKind::Info,
                         "at " + d.file + ":" + std::to_string(d.line) + ":" + std::to_string(d.col));
        if (std::optional<std::string> sourceLine = read_source_line(d.file, d.line); sourceLine.has_value()) {
            print_diagnostic(std::cerr, style, DiagnosticKind::Info,
                             std::to_string(d.line) + " | " + *sourceLine);
            print_diagnostic(std::cerr, style, DiagnosticKind::Info,
                             "    | " + make_caret_line(*sourceLine, d.col));
        }
    } else {
        print_diagnostic(std::cerr, style, DiagnosticKind::Error, message);
    }
    if (std::optional<std::string> hint = diagnostic_hint_for(message); hint.has_value()) {
        print_diagnostic(std::cerr, style, DiagnosticKind::Warning, "hint: " + *hint);
    }
}

struct Token {
    std::string text;
    int line = 1;
    int col = 1;
};

static std::string location(const std::string& file, const Token& token) {
    return file + ":" + std::to_string(token.line) + ":" + std::to_string(token.col);
}

static const std::set<std::string> kKeywords = {
    "LET", "FIXED", "SET", "AS", "VALUE", "FUNCTION", "START", "END", "RETURNS",
    "RETURN", "PARAMETER", "IF", "THEN", "ELSE", "WHILE", "DO", "INPUT", "OUTPUT",
    "CALL", "USE", "LIBRARY", "NUMBER", "TEXT", "BOOL", "VOID", "TRUE", "FALSE", "ADD", "SUBTRACT",
    "MULTIPLY", "DIVIDE", "MODULO", "JOIN", "EQUAL", "LESS", "GREATER", "AND",
    "OR", "NOT", "NEGATIVE", "POINT", "ZERO", "ONE", "TWO", "THREE", "FOUR",
    "FIVE", "SIX", "SEVEN", "EIGHT", "NINE", "TEN", "ELEVEN", "TWELVE",
    "THIRTEEN", "FOURTEEN", "FIFTEEN", "SIXTEEN", "SEVENTEEN", "EIGHTEEN",
    "NINETEEN", "TWENTY", "THIRTY", "FORTY", "FIFTY", "SIXTY", "SEVENTY",
    "EIGHTY", "NINETY", "HUNDRED", "THOUSAND", "MILLION", "BILLION", "TRILLION",
    "MAIN"
};

static const std::unordered_map<std::string, int> kSmallNumbers = {
    {"ZERO", 0}, {"ONE", 1}, {"TWO", 2}, {"THREE", 3}, {"FOUR", 4}, {"FIVE", 5},
    {"SIX", 6}, {"SEVEN", 7}, {"EIGHT", 8}, {"NINE", 9}, {"TEN", 10},
    {"ELEVEN", 11}, {"TWELVE", 12}, {"THIRTEEN", 13}, {"FOURTEEN", 14},
    {"FIFTEEN", 15}, {"SIXTEEN", 16}, {"SEVENTEEN", 17}, {"EIGHTEEN", 18},
    {"NINETEEN", 19}
};

static const std::unordered_map<std::string, int> kTens = {
    {"TWENTY", 20}, {"THIRTY", 30}, {"FORTY", 40}, {"FIFTY", 50},
    {"SIXTY", 60}, {"SEVENTY", 70}, {"EIGHTY", 80}, {"NINETY", 90}
};

static const std::unordered_map<std::string, std::int64_t> kScales = {
    {"THOUSAND", 1000LL}, {"MILLION", 1000000LL}, {"BILLION", 1000000000LL},
    {"TRILLION", 1000000000000LL}
};

static bool is_type_word(const std::string& word) {
    return word == "NUMBER" || word == "TEXT" || word == "BOOL" || word == "VOID";
}

static bool is_number_start(const std::string& word) {
    return word == "NEGATIVE" || kSmallNumbers.find(word) != kSmallNumbers.end() || kTens.find(word) != kTens.end();
}

static bool is_identifier_word(const std::string& word, bool allowMain = false) {
    if (word.empty()) return false;
    if (allowMain && word == "MAIN") return true;
    if (kKeywords.find(word) != kKeywords.end()) return false;
    return std::all_of(word.begin(), word.end(), [](char ch) {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
    });
}

static bool is_symbol_char(char ch) {
    static const std::string symbols = "#!?.,:;+-*/=_()[]<>@%";
    return symbols.find(ch) != std::string::npos;
}

static std::vector<Token> lex_source(const std::string& source, const std::string& file) {
    std::vector<Token> tokens;
    int line = 1;
    int col = 1;
    std::string current;
    int startLine = 1;
    int startCol = 1;

    auto flush = [&]() {
        if (!current.empty()) {
            tokens.push_back(Token{current, startLine, startCol});
            current.clear();
        }
    };

    for (unsigned char raw : source) {
        char ch = static_cast<char>(raw);
        if (ch == '\r') continue;
        if (ch == '\n') {
            flush();
            ++line;
            col = 1;
            continue;
        }
        if (std::isspace(raw)) {
            flush();
            ++col;
            continue;
        }
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || is_symbol_char(ch))) {
            Token token{std::string(1, ch), line, col};
            throw CompileError(location(file, token) + ": forbidden character '" + std::string(1, ch) + "'");
        }
        if (current.empty()) {
            startLine = line;
            startCol = col;
        }
        current.push_back(ch);
        ++col;
    }
    flush();
    return tokens;
}

enum class Type { Unknown, Number, Text, Bool, Void };

static std::string type_name(Type type) {
    switch (type) {
        case Type::Number: return "NUMBER";
        case Type::Text: return "TEXT";
        case Type::Bool: return "BOOL";
        case Type::Void: return "VOID";
        case Type::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

static Type parse_type_word(const std::string& word) {
    if (word == "NUMBER") return Type::Number;
    if (word == "TEXT") return Type::Text;
    if (word == "BOOL") return Type::Bool;
    if (word == "VOID") return Type::Void;
    return Type::Unknown;
}

enum class ExprKind { Number, Text, Bool, Identifier, Call, Unary, Binary };

struct Expr {
    ExprKind kind;
    Token token;
    Type resolved = Type::Unknown;
    std::int64_t number = 0;
    std::string text;
    bool boolean = false;
    std::string name;
    std::string op;
    std::vector<std::unique_ptr<Expr>> args;
};

enum class StmtKind { Decl, Assign, Return, Input, Output, If, While, ExprStmt };

struct Stmt {
    StmtKind kind;
    Token token;
    bool fixed = false;
    std::string name;
    Type declaredType = Type::Unknown;
    std::unique_ptr<Expr> expr;
    std::vector<std::unique_ptr<Stmt>> thenBody;
    std::vector<std::unique_ptr<Stmt>> elseBody;
};

struct Declaration {
    Token token;
    bool fixed = false;
    std::string name;
    Type type = Type::Unknown;
    std::unique_ptr<Expr> value;
};

struct Function {
    Token token;
    std::string name;
    std::vector<std::string> params;
    std::vector<Type> paramTypes;
    Type returnType = Type::Unknown;
    std::vector<std::unique_ptr<Stmt>> body;
};

struct Module {
    std::vector<Declaration> globals;
    std::vector<Function> functions;
};

struct Signature {
    Token token;
    std::string name;
    std::size_t arity = 0;
    std::vector<Type> paramTypes;
    Type returnType = Type::Unknown;
    bool builtin = false;
};

struct BuiltinFunction {
    std::string name;
    std::string cppName;
    std::vector<Type> paramTypes;
    Type returnType = Type::Unknown;
    bool usesWindowsApi = false;
};

static const std::unordered_map<std::string, BuiltinFunction>& builtin_functions() {
    static const std::unordered_map<std::string, BuiltinFunction> builtins = {
        {"WINMESSAGE", {"WINMESSAGE", "win_message", {Type::Text, Type::Text}, Type::Number, true}},
        {"WINWINDOW", {"WINWINDOW", "win_window", {Type::Text, Type::Number, Type::Number}, Type::Number, true}},
        {"WINBUTTON", {"WINBUTTON", "win_button", {Type::Number, Type::Text, Type::Number, Type::Number, Type::Number, Type::Number, Type::Number}, Type::Number, true}},
        {"WINLABEL", {"WINLABEL", "win_label", {Type::Number, Type::Text, Type::Number, Type::Number, Type::Number, Type::Number}, Type::Number, true}},
        {"WINEDIT", {"WINEDIT", "win_edit", {Type::Number, Type::Text, Type::Number, Type::Number, Type::Number, Type::Number, Type::Number}, Type::Number, true}},
        {"WINTEXT", {"WINTEXT", "win_text", {Type::Number}, Type::Text, true}},
        {"WINSETTEXT", {"WINSETTEXT", "win_set_text", {Type::Number, Type::Text}, Type::Void, true}},
        {"WINSHOW", {"WINSHOW", "win_show", {Type::Number}, Type::Void, true}},
        {"WINWAIT", {"WINWAIT", "win_wait", {}, Type::Bool, true}},
        {"WINRUN", {"WINRUN", "win_run", {}, Type::Number, true}},
        {"WINCOMMAND", {"WINCOMMAND", "win_command", {}, Type::Number, true}},
        {"WINQUIT", {"WINQUIT", "win_quit", {}, Type::Void, true}},
        {"PIXELWINDOW", {"PIXELWINDOW", "pixel_window", {Type::Text, Type::Number, Type::Number}, Type::Number, true}},
        {"PIXELCOLOR", {"PIXELCOLOR", "pixel_color", {Type::Number, Type::Number, Type::Number}, Type::Number, true}},
        {"PIXELHEX", {"PIXELHEX", "pixel_hex", {Type::Text}, Type::Number, true}},
        {"PIXELSET", {"PIXELSET", "pixel_set", {Type::Number, Type::Number, Type::Number, Type::Number}, Type::Void, true}},
        {"PIXELGET", {"PIXELGET", "pixel_get", {Type::Number, Type::Number, Type::Number}, Type::Number, true}},
        {"PIXELCLEAR", {"PIXELCLEAR", "pixel_clear", {Type::Number}, Type::Void, true}},
        {"PIXELPRESENT", {"PIXELPRESENT", "pixel_present", {Type::Number}, Type::Void, true}},
        {"PIXELLAYER", {"PIXELLAYER", "pixel_layer", {Type::Number, Type::Number}, Type::Number, true}},
        {"PIXELRECT", {"PIXELRECT", "pixel_rect", {Type::Number, Type::Number, Type::Number, Type::Number, Type::Number, Type::Number}, Type::Void, true}},
        {"PIXELFRAME", {"PIXELFRAME", "pixel_frame", {Type::Number, Type::Number, Type::Number, Type::Number, Type::Number, Type::Number}, Type::Void, true}},
        {"PIXELLINE", {"PIXELLINE", "pixel_line", {Type::Number, Type::Number, Type::Number, Type::Number, Type::Number, Type::Number}, Type::Void, true}},
        {"PIXELCIRCLE", {"PIXELCIRCLE", "pixel_circle", {Type::Number, Type::Number, Type::Number, Type::Number, Type::Number}, Type::Void, true}},
        {"PIXELCIRCLEFRAME", {"PIXELCIRCLEFRAME", "pixel_circle_frame", {Type::Number, Type::Number, Type::Number, Type::Number, Type::Number}, Type::Void, true}},
        {"PIXELELLIPSE", {"PIXELELLIPSE", "pixel_ellipse", {Type::Number, Type::Number, Type::Number, Type::Number, Type::Number, Type::Number}, Type::Void, true}},
        {"PIXELTRIANGLE", {"PIXELTRIANGLE", "pixel_triangle", {Type::Number, Type::Number, Type::Number, Type::Number, Type::Number, Type::Number, Type::Number, Type::Number}, Type::Void, true}},
        {"PIXELDIAMOND", {"PIXELDIAMOND", "pixel_diamond", {Type::Number, Type::Number, Type::Number, Type::Number, Type::Number}, Type::Void, true}},
        {"PIXELSTAR", {"PIXELSTAR", "pixel_star", {Type::Number, Type::Number, Type::Number, Type::Number, Type::Number}, Type::Void, true}},
        {"PIXELTEXT", {"PIXELTEXT", "pixel_text", {Type::Number, Type::Number, Type::Number, Type::Number, Type::Number, Type::Text}, Type::Void, true}},
        {"PIXELNUMBER", {"PIXELNUMBER", "pixel_number", {Type::Number, Type::Number, Type::Number, Type::Number, Type::Number, Type::Number}, Type::Void, true}},
        {"PIXELBUTTON", {"PIXELBUTTON", "pixel_button", {Type::Number, Type::Number, Type::Number, Type::Number, Type::Number, Type::Number, Type::Text, Type::Number, Type::Number, Type::Number}, Type::Number, true}},
        {"PIXELINPUT", {"PIXELINPUT", "pixel_input", {Type::Number, Type::Number, Type::Number, Type::Number, Type::Number, Type::Number, Type::Text, Type::Number, Type::Number, Type::Number}, Type::Number, true}},
        {"PIXELOBJECTTEXT", {"PIXELOBJECTTEXT", "pixel_object_text", {Type::Number}, Type::Text, true}},
        {"PIXELSETTEXT", {"PIXELSETTEXT", "pixel_set_object_text", {Type::Number, Type::Text}, Type::Void, true}},
    };
    return builtins;
}

static const BuiltinFunction* find_builtin_function(const std::string& name) {
    auto found = builtin_functions().find(name);
    return found == builtin_functions().end() ? nullptr : &found->second;
}

class Parser {
public:
    Parser(std::vector<Token> tokens, std::string file)
        : tokens_(std::move(tokens)), file_(std::move(file)) {
        register_builtin_signatures();
        collect_signatures();
    }

    Module parse_module() {
        Module module;
        while (!at_end()) {
            if (match("LET") || match("FIXED")) {
                module.globals.push_back(parse_declaration(previous()));
                expect_end_line();
            } else if (match("FUNCTION")) {
                module.functions.push_back(parse_function(previous()));
            } else {
                fail(peek(), "expected top-level declaration or function definition");
            }
        }
        return module;
    }

private:
    std::vector<Token> tokens_;
    std::string file_;
    std::size_t pos_ = 0;
    std::unordered_map<std::string, Signature> signatures_;

    bool at_end() const { return pos_ >= tokens_.size(); }
    const Token& peek() const {
        static Token eof{"<EOF>", 1, 1};
        return at_end() ? eof : tokens_[pos_];
    }
    const Token& previous() const { return tokens_[pos_ - 1]; }
    bool check(const std::string& text) const { return !at_end() && peek().text == text; }
    bool check_next(const std::string& text) const { return pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].text == text; }
    bool match(const std::string& text) {
        if (!check(text)) return false;
        ++pos_;
        return true;
    }
    Token consume(const std::string& text, const std::string& message) {
        if (match(text)) return previous();
        fail(peek(), message);
    }
    [[noreturn]] void fail(const Token& token, const std::string& message) const {
        throw CompileError(location(file_, token) + ": " + message);
    }

    void register_builtin_signatures() {
        for (const auto& [name, builtin] : builtin_functions()) {
            Token token{name, 1, 1};
            signatures_.insert({name, Signature{token, name, builtin.paramTypes.size(), builtin.paramTypes, builtin.returnType, true}});
        }
    }

    void collect_signatures() {
        for (std::size_t i = 0; i < tokens_.size();) {
            if (tokens_[i].text != "FUNCTION" || i + 1 >= tokens_.size() || tokens_[i + 1].text != "START") {
                ++i;
                continue;
            }
            Token start = tokens_[i++];
            ++i;
            if (i >= tokens_.size() || !is_identifier_word(tokens_[i].text, true)) {
                fail(i < tokens_.size() ? tokens_[i] : start, "expected function name");
            }
            std::string name = tokens_[i++].text;
            std::size_t arity = 0;
            std::set<std::string> seenParams;
            while (i < tokens_.size() && tokens_[i].text == "PARAMETER") {
                ++i;
                if (i >= tokens_.size() || !is_identifier_word(tokens_[i].text)) {
                    fail(i < tokens_.size() ? tokens_[i] : start, "expected parameter name");
                }
                if (!seenParams.insert(tokens_[i].text).second) {
                    fail(tokens_[i], "duplicate parameter '" + tokens_[i].text + "'");
                }
                ++arity;
                ++i;
            }
            if (i >= tokens_.size() || tokens_[i].text != "RETURNS") {
                fail(i < tokens_.size() ? tokens_[i] : start, "expected RETURNS in function signature");
            }
            ++i;
            if (i >= tokens_.size() || !is_type_word(tokens_[i].text)) {
                fail(i < tokens_.size() ? tokens_[i] : start, "expected return type");
            }
            Type returnType = parse_type_word(tokens_[i++].text);
            auto existing = signatures_.find(name);
            if (existing != signatures_.end()) {
                if (existing->second.builtin) fail(start, "function '" + name + "' conflicts with a built-in function");
                fail(start, "duplicate function '" + name + "'");
            }
            signatures_.insert({name, Signature{start, name, arity, {}, returnType, false}});
        }
    }

    void expect_end_line() {
        consume("END", "expected END LINE");
        consume("LINE", "expected END LINE");
    }

    bool is_end_line() const {
        return check("END") && check_next("LINE");
    }

    bool is_block_stop() const {
        return (check("FUNCTION") && check_next("END")) ||
               (check("END") && (check_next("IF") || check_next("WHILE"))) ||
               check("ELSE");
    }

    std::string consume_identifier(bool allowMain, const std::string& what) {
        if (at_end() || !is_identifier_word(peek().text, allowMain)) {
            fail(peek(), "expected " + what);
        }
        std::string name = peek().text;
        ++pos_;
        return name;
    }

    Declaration parse_declaration(Token start) {
        bool fixed = start.text == "FIXED";
        std::string name = consume_identifier(false, "identifier");
        consume("AS", "expected AS in declaration");
        Type type = parse_type_word(peek().text);
        if (type == Type::Unknown) fail(peek(), "expected type in declaration");
        ++pos_;
        if (type == Type::Void) fail(previous(), "variables cannot have VOID type");
        consume("VALUE", "expected VALUE in declaration");
        return Declaration{start, fixed, name, type, parse_expression(false)};
    }

    std::unique_ptr<Stmt> parse_declaration_statement(Token start) {
        Declaration decl = parse_declaration(start);
        expect_end_line();
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = StmtKind::Decl;
        stmt->token = decl.token;
        stmt->fixed = decl.fixed;
        stmt->name = std::move(decl.name);
        stmt->declaredType = decl.type;
        stmt->expr = std::move(decl.value);
        return stmt;
    }

    Function parse_function(Token functionToken) {
        consume("START", "expected START after FUNCTION");
        std::string name = consume_identifier(true, "function name");
        Function fn;
        fn.token = functionToken;
        fn.name = name;
        std::set<std::string> seenParams;
        while (match("PARAMETER")) {
            std::string param = consume_identifier(false, "parameter name");
            if (!seenParams.insert(param).second) fail(previous(), "duplicate parameter '" + param + "'");
            fn.params.push_back(param);
            fn.paramTypes.push_back(Type::Unknown);
        }
        consume("RETURNS", "expected RETURNS in function signature");
        Type returnType = parse_type_word(peek().text);
        if (returnType == Type::Unknown) fail(peek(), "expected return type");
        ++pos_;
        fn.returnType = returnType;
        expect_end_line();
        fn.body = parse_statement_block();
        consume("FUNCTION", "expected FUNCTION END");
        consume("END", "expected FUNCTION END");
        std::string endName = consume_identifier(true, "function end name");
        if (endName != name) fail(previous(), "function end name '" + endName + "' does not match '" + name + "'");
        expect_end_line();
        return fn;
    }

    std::vector<std::unique_ptr<Stmt>> parse_statement_block() {
        std::vector<std::unique_ptr<Stmt>> statements;
        while (!at_end() && !is_block_stop()) {
            statements.push_back(parse_statement());
        }
        if (at_end()) fail(peek(), "unclosed block");
        return statements;
    }

    std::unique_ptr<Stmt> parse_statement() {
        if (match("LET") || match("FIXED")) return parse_declaration_statement(previous());
        if (match("SET")) {
            Token start = previous();
            auto stmt = std::make_unique<Stmt>();
            stmt->kind = StmtKind::Assign;
            stmt->token = start;
            stmt->name = consume_identifier(false, "assignment target");
            consume("VALUE", "expected VALUE in assignment");
            stmt->expr = parse_expression(false);
            expect_end_line();
            return stmt;
        }
        if (match("RETURN")) {
            auto stmt = std::make_unique<Stmt>();
            stmt->kind = StmtKind::Return;
            stmt->token = previous();
            if (!is_end_line()) stmt->expr = parse_expression(false);
            expect_end_line();
            return stmt;
        }
        if (match("INPUT")) {
            auto stmt = std::make_unique<Stmt>();
            stmt->kind = StmtKind::Input;
            stmt->token = previous();
            stmt->name = consume_identifier(false, "input target");
            expect_end_line();
            return stmt;
        }
        if (match("OUTPUT")) {
            auto stmt = std::make_unique<Stmt>();
            stmt->kind = StmtKind::Output;
            stmt->token = previous();
            stmt->expr = parse_expression(false);
            expect_end_line();
            return stmt;
        }
        if (match("IF")) return parse_if(previous());
        if (match("WHILE")) return parse_while(previous());
        if (check("CALL")) {
            auto stmt = std::make_unique<Stmt>();
            stmt->kind = StmtKind::ExprStmt;
            stmt->token = peek();
            stmt->expr = parse_expression(false);
            expect_end_line();
            return stmt;
        }
        fail(peek(), "expected statement");
    }

    std::unique_ptr<Stmt> parse_if(Token start) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = StmtKind::If;
        stmt->token = start;
        stmt->expr = parse_expression(false);
        consume("THEN", "expected THEN in IF statement");
        expect_end_line();
        stmt->thenBody = parse_statement_block();
        if (match("ELSE")) {
            expect_end_line();
            stmt->elseBody = parse_statement_block();
        }
        consume("END", "expected END IF");
        consume("IF", "expected END IF");
        expect_end_line();
        return stmt;
    }

    std::unique_ptr<Stmt> parse_while(Token start) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = StmtKind::While;
        stmt->token = start;
        stmt->expr = parse_expression(false);
        consume("DO", "expected DO in WHILE statement");
        expect_end_line();
        stmt->thenBody = parse_statement_block();
        consume("END", "expected END WHILE");
        consume("WHILE", "expected END WHILE");
        expect_end_line();
        return stmt;
    }

    std::unique_ptr<Expr> parse_expression(bool preferShortNumber) {
        if (at_end()) fail(peek(), "expected expression");
        Token start = peek();
        if (match("TEXT")) {
            consume("START", "expected TEXT START");
            std::vector<std::string> words;
            while (!at_end() && !(check("TEXT") && check_next("END"))) {
                words.push_back(peek().text);
                ++pos_;
            }
            if (at_end()) fail(start, "unclosed text literal");
            consume("TEXT", "expected TEXT END");
            consume("END", "expected TEXT END");
            auto expr = std::make_unique<Expr>();
            expr->kind = ExprKind::Text;
            expr->token = start;
            for (std::size_t i = 0; i < words.size(); ++i) {
                if (i) expr->text += " ";
                expr->text += words[i];
            }
            return expr;
        }
        if (match("TRUE") || match("FALSE")) {
            auto expr = std::make_unique<Expr>();
            expr->kind = ExprKind::Bool;
            expr->token = previous();
            expr->boolean = expr->token.text == "TRUE";
            return expr;
        }
        if (match("CALL")) {
            auto expr = std::make_unique<Expr>();
            expr->kind = ExprKind::Call;
            expr->token = previous();
            expr->name = consume_identifier(true, "function name after CALL");
            auto found = signatures_.find(expr->name);
            if (found == signatures_.end()) fail(previous(), "unknown function '" + expr->name + "'");
            for (std::size_t i = 0; i < found->second.arity; ++i) {
                expr->args.push_back(parse_expression(i + 1 < found->second.arity));
            }
            return expr;
        }
        if (is_binary_operator(peek().text)) {
            std::string op = peek().text;
            ++pos_;
            auto expr = std::make_unique<Expr>();
            expr->kind = ExprKind::Binary;
            expr->token = start;
            expr->op = op;
            expr->args.push_back(parse_expression(true));
            expr->args.push_back(parse_expression(false));
            return expr;
        }
        if (match("NOT")) {
            auto expr = std::make_unique<Expr>();
            expr->kind = ExprKind::Unary;
            expr->token = previous();
            expr->op = "NOT";
            expr->args.push_back(parse_expression(false));
            return expr;
        }
        if (is_number_start(peek().text)) {
            return parse_number_literal(preferShortNumber);
        }
        if (is_identifier_word(peek().text, false)) {
            auto expr = std::make_unique<Expr>();
            expr->kind = ExprKind::Identifier;
            expr->token = peek();
            expr->name = peek().text;
            ++pos_;
            return expr;
        }
        fail(peek(), "expected expression");
    }

    bool is_binary_operator(const std::string& word) const {
        static const std::set<std::string> ops = {
            "ADD", "SUBTRACT", "MULTIPLY", "DIVIDE", "MODULO", "JOIN",
            "EQUAL", "LESS", "GREATER", "AND", "OR"
        };
        return ops.find(word) != ops.end();
    }

    std::unique_ptr<Expr> parse_number_literal(bool preferShort) {
        Token start = peek();
        bool negative = false;
        if (match("NEGATIVE")) negative = true;
        std::int64_t total = 0;
        bool consumedAny = false;
        while (!at_end()) {
            auto group = parse_number_group(preferShort);
            if (!group.has_value()) break;
            consumedAny = true;
            std::int64_t value = *group;
            if (!at_end() && kScales.find(peek().text) != kScales.end()) {
                std::int64_t scale = kScales.at(peek().text);
                total += value * scale;
                ++pos_;
                if (preferShort) break;
                continue;
            }
            total += value;
            break;
        }
        if (!consumedAny) fail(start, "invalid number phrase");
        auto expr = std::make_unique<Expr>();
        expr->kind = ExprKind::Number;
        expr->token = start;
        expr->number = negative ? -total : total;
        return expr;
    }

    std::optional<std::int64_t> parse_number_group(bool preferShort) {
        if (at_end()) return std::nullopt;
        std::int64_t value = 0;
        bool consumed = false;
        if (kSmallNumbers.find(peek().text) != kSmallNumbers.end()) {
            value = kSmallNumbers.at(peek().text);
            consumed = true;
            ++pos_;
        } else if (kTens.find(peek().text) != kTens.end()) {
            value = kTens.at(peek().text);
            consumed = true;
            ++pos_;
            if (!at_end() && kSmallNumbers.find(peek().text) != kSmallNumbers.end() &&
                kSmallNumbers.at(peek().text) > 0 && kSmallNumbers.at(peek().text) < 10) {
                value += kSmallNumbers.at(peek().text);
                ++pos_;
            }
        } else {
            return std::nullopt;
        }
        if (!at_end() && check("HUNDRED")) {
            value *= 100;
            ++pos_;
            if (!preferShort) {
                auto tail = parse_number_group(true);
                if (tail.has_value()) value += *tail;
            }
        }
        return consumed ? std::optional<std::int64_t>(value) : std::nullopt;
    }
};

struct Symbol {
    Type type = Type::Unknown;
    bool mutableValue = false;
    bool isParam = false;
    int paramIndex = -1;
    Token token;
};

struct FunctionInfo {
    Function* fn = nullptr;
};

class SemanticAnalyzer {
public:
    SemanticAnalyzer(Module& module, std::string file)
        : module_(module), file_(std::move(file)) {}

    void analyze() {
        register_functions();
        register_globals();
        bool changed = false;
        for (int pass = 0; pass < 20; ++pass) {
            changed = false;
            analyze_globals(changed);
            for (Function& fn : module_.functions) analyze_function(fn, changed, false);
            if (!changed) break;
        }
        analyze_globals(changed);
        for (Function& fn : module_.functions) analyze_function(fn, changed, true);
        validate_main();
        validate_parameter_types();
        validate_returns();
    }

private:
    Module& module_;
    std::string file_;
    std::unordered_map<std::string, FunctionInfo> functions_;
    std::unordered_map<std::string, Symbol> globals_;

    [[noreturn]] void fail(const Token& token, const std::string& message) const {
        throw CompileError(location(file_, token) + ": " + message);
    }

    void register_functions() {
        for (Function& fn : module_.functions) {
            if (functions_.find(fn.name) != functions_.end()) fail(fn.token, "duplicate function '" + fn.name + "'");
            if (find_builtin_function(fn.name)) fail(fn.token, "function '" + fn.name + "' conflicts with a built-in function");
            functions_[fn.name] = FunctionInfo{&fn};
        }
    }

    void register_globals() {
        for (const Declaration& decl : module_.globals) {
            if (globals_.find(decl.name) != globals_.end()) fail(decl.token, "duplicate global '" + decl.name + "'");
            if (functions_.find(decl.name) != functions_.end()) fail(decl.token, "global '" + decl.name + "' conflicts with function name");
            globals_[decl.name] = Symbol{decl.type, !decl.fixed, false, -1, decl.token};
        }
    }

    void validate_main() {
        auto found = functions_.find("MAIN");
        if (found == functions_.end()) {
            Token token{"<EOF>", 1, 1};
            fail(token, "missing MAIN function");
        }
        Function& main = *found->second.fn;
        if (!main.params.empty()) fail(main.token, "MAIN must not have parameters");
        if (main.returnType != Type::Number && main.returnType != Type::Void) {
            fail(main.token, "MAIN must return NUMBER or VOID");
        }
    }

    void validate_parameter_types() {
        for (const Function& fn : module_.functions) {
            for (std::size_t i = 0; i < fn.paramTypes.size(); ++i) {
                if (fn.paramTypes[i] == Type::Unknown) {
                    fail(fn.token, "cannot infer type of parameter '" + fn.params[i] + "' in function '" + fn.name + "'");
                }
                if (fn.paramTypes[i] == Type::Void) {
                    fail(fn.token, "parameter '" + fn.params[i] + "' cannot have VOID type");
                }
            }
        }
    }

    void validate_returns() {
        for (const Function& fn : module_.functions) {
            if (fn.returnType != Type::Void && !block_returns(fn.body)) {
                fail(fn.token, "missing RETURN in function '" + fn.name + "'");
            }
        }
    }

    bool block_returns(const std::vector<std::unique_ptr<Stmt>>& body) const {
        for (const auto& stmt : body) {
            if (stmt->kind == StmtKind::Return) return true;
            if (stmt->kind == StmtKind::If && !stmt->elseBody.empty() &&
                block_returns(stmt->thenBody) && block_returns(stmt->elseBody)) {
                return true;
            }
        }
        return false;
    }

    void analyze_globals(bool& changed) {
        std::unordered_map<std::string, Symbol> env = globals_;
        for (const Declaration& decl : module_.globals) {
            Type valueType = analyze_expr(*decl.value, nullptr, env, decl.type, changed, true);
            require_assignable(decl.value->token, decl.type, valueType, "global '" + decl.name + "'");
        }
    }

    void analyze_function(Function& fn, bool& changed, bool finalPass) {
        std::unordered_map<std::string, Symbol> env = globals_;
        for (std::size_t i = 0; i < fn.params.size(); ++i) {
            if (env.find(fn.params[i]) != env.end()) fail(fn.token, "parameter '" + fn.params[i] + "' conflicts with a global");
            env[fn.params[i]] = Symbol{fn.paramTypes[i], false, true, static_cast<int>(i), fn.token};
        }
        analyze_block(fn, fn.body, env, changed, finalPass);
    }

    void analyze_block(Function& fn, const std::vector<std::unique_ptr<Stmt>>& body,
                       std::unordered_map<std::string, Symbol> env, bool& changed, bool finalPass) {
        for (const auto& stmt : body) {
            switch (stmt->kind) {
                case StmtKind::Decl: {
                    if (env.find(stmt->name) != env.end()) fail(stmt->token, "duplicate declaration '" + stmt->name + "'");
                    Type valueType = analyze_expr(*stmt->expr, &fn, env, stmt->declaredType, changed, finalPass);
                    require_assignable(stmt->expr->token, stmt->declaredType, valueType, "declaration '" + stmt->name + "'");
                    env[stmt->name] = Symbol{stmt->declaredType, !stmt->fixed, false, -1, stmt->token};
                    break;
                }
                case StmtKind::Assign: {
                    auto found = env.find(stmt->name);
                    if (found == env.end()) fail(stmt->token, "undeclared identifier '" + stmt->name + "'");
                    if (!found->second.mutableValue) fail(stmt->token, "cannot assign immutable identifier '" + stmt->name + "'");
                    Type valueType = analyze_expr(*stmt->expr, &fn, env, found->second.type, changed, finalPass);
                    require_assignable(stmt->expr->token, found->second.type, valueType, "assignment to '" + stmt->name + "'");
                    break;
                }
                case StmtKind::Return: {
                    if (fn.returnType == Type::Void) {
                        if (stmt->expr) fail(stmt->token, "VOID function cannot return a value");
                    } else {
                        if (!stmt->expr) fail(stmt->token, "function must return " + type_name(fn.returnType));
                        Type valueType = analyze_expr(*stmt->expr, &fn, env, fn.returnType, changed, finalPass);
                        require_assignable(stmt->expr->token, fn.returnType, valueType, "return from '" + fn.name + "'");
                    }
                    break;
                }
                case StmtKind::Input: {
                    auto found = env.find(stmt->name);
                    if (found == env.end()) fail(stmt->token, "undeclared identifier '" + stmt->name + "'");
                    if (!found->second.mutableValue) fail(stmt->token, "cannot input into immutable identifier '" + stmt->name + "'");
                    if (found->second.type == Type::Void || found->second.type == Type::Unknown) {
                        fail(stmt->token, "invalid input target '" + stmt->name + "'");
                    }
                    stmt->declaredType = found->second.type;
                    break;
                }
                case StmtKind::Output: {
                    Type valueType = analyze_expr(*stmt->expr, &fn, env, Type::Unknown, changed, finalPass);
                    if (valueType == Type::Void) fail(stmt->expr->token, "OUTPUT cannot print VOID");
                    if (finalPass && valueType == Type::Unknown) fail(stmt->expr->token, "cannot infer OUTPUT expression type");
                    break;
                }
                case StmtKind::If: {
                    Type condition = analyze_expr(*stmt->expr, &fn, env, Type::Bool, changed, finalPass);
                    require_assignable(stmt->expr->token, Type::Bool, condition, "IF condition");
                    analyze_block(fn, stmt->thenBody, env, changed, finalPass);
                    if (!stmt->elseBody.empty()) analyze_block(fn, stmt->elseBody, env, changed, finalPass);
                    break;
                }
                case StmtKind::While: {
                    Type condition = analyze_expr(*stmt->expr, &fn, env, Type::Bool, changed, finalPass);
                    require_assignable(stmt->expr->token, Type::Bool, condition, "WHILE condition");
                    analyze_block(fn, stmt->thenBody, env, changed, finalPass);
                    break;
                }
                case StmtKind::ExprStmt: {
                    if (stmt->expr->kind != ExprKind::Call) fail(stmt->token, "only CALL expressions may be used as statements");
                    analyze_expr(*stmt->expr, &fn, env, Type::Unknown, changed, finalPass);
                    break;
                }
            }
        }
    }

    void require_assignable(const Token& token, Type expected, Type actual, const std::string& context) const {
        if (actual == Type::Unknown) return;
        if (expected != actual) {
            fail(token, "type mismatch in " + context + ": expected " + type_name(expected) + ", got " + type_name(actual));
        }
    }

    Type analyze_expr(Expr& expr, Function* current, std::unordered_map<std::string, Symbol>& env,
                      Type expected, bool& changed, bool finalPass) {
        switch (expr.kind) {
            case ExprKind::Number:
                expr.resolved = Type::Number;
                break;
            case ExprKind::Text:
                expr.resolved = Type::Text;
                break;
            case ExprKind::Bool:
                expr.resolved = Type::Bool;
                break;
            case ExprKind::Identifier: {
                auto found = env.find(expr.name);
                if (found == env.end()) fail(expr.token, "undeclared identifier '" + expr.name + "'");
                if (found->second.type == Type::Unknown && expected != Type::Unknown && found->second.isParam && current) {
                    current->paramTypes[found->second.paramIndex] = expected;
                    found->second.type = expected;
                    changed = true;
                }
                expr.resolved = found->second.type;
                break;
            }
            case ExprKind::Call: {
                auto found = functions_.find(expr.name);
                if (found == functions_.end()) {
                    const BuiltinFunction* builtin = find_builtin_function(expr.name);
                    if (!builtin) fail(expr.token, "unknown function '" + expr.name + "'");
                    if (builtin->paramTypes.size() != expr.args.size()) fail(expr.token, "wrong function arity for '" + expr.name + "'");
                    for (std::size_t i = 0; i < expr.args.size(); ++i) {
                        Type argType = analyze_expr(*expr.args[i], current, env, builtin->paramTypes[i], changed, finalPass);
                        require_assignable(expr.args[i]->token, builtin->paramTypes[i], argType,
                                           "argument " + std::to_string(i + 1) + " of '" + expr.name + "'");
                    }
                    expr.resolved = builtin->returnType;
                    break;
                }
                Function& callee = *found->second.fn;
                if (callee.params.size() != expr.args.size()) fail(expr.token, "wrong function arity for '" + expr.name + "'");
                for (std::size_t i = 0; i < expr.args.size(); ++i) {
                    Type expectedParam = callee.paramTypes[i];
                    Type argType = analyze_expr(*expr.args[i], current, env, expectedParam, changed, finalPass);
                    if (expectedParam == Type::Unknown && argType != Type::Unknown) {
                        callee.paramTypes[i] = argType;
                        changed = true;
                    } else if (expectedParam != Type::Unknown) {
                        require_assignable(expr.args[i]->token, expectedParam, argType,
                                           "argument " + std::to_string(i + 1) + " of '" + expr.name + "'");
                    }
                }
                expr.resolved = callee.returnType;
                break;
            }
            case ExprKind::Unary: {
                Type inner = analyze_expr(*expr.args[0], current, env, Type::Bool, changed, finalPass);
                require_assignable(expr.args[0]->token, Type::Bool, inner, "NOT operand");
                expr.resolved = Type::Bool;
                break;
            }
            case ExprKind::Binary:
                expr.resolved = analyze_binary(expr, current, env, changed, finalPass);
                break;
        }
        if (expected != Type::Unknown) require_assignable(expr.token, expected, expr.resolved, "expression");
        if (finalPass && expr.resolved == Type::Unknown) fail(expr.token, "cannot infer expression type");
        return expr.resolved;
    }

    Type analyze_binary(Expr& expr, Function* current, std::unordered_map<std::string, Symbol>& env,
                        bool& changed, bool finalPass) {
        auto requireBoth = [&](Type operandType, Type resultType) {
            Type left = analyze_expr(*expr.args[0], current, env, operandType, changed, finalPass);
            Type right = analyze_expr(*expr.args[1], current, env, operandType, changed, finalPass);
            require_assignable(expr.args[0]->token, operandType, left, expr.op + " left operand");
            require_assignable(expr.args[1]->token, operandType, right, expr.op + " right operand");
            return resultType;
        };
        if (expr.op == "ADD" || expr.op == "SUBTRACT" || expr.op == "MULTIPLY" ||
            expr.op == "DIVIDE" || expr.op == "MODULO") {
            return requireBoth(Type::Number, Type::Number);
        }
        if (expr.op == "JOIN") return requireBoth(Type::Text, Type::Text);
        if (expr.op == "LESS" || expr.op == "GREATER") return requireBoth(Type::Number, Type::Bool);
        if (expr.op == "AND" || expr.op == "OR") return requireBoth(Type::Bool, Type::Bool);
        if (expr.op == "EQUAL") {
            Type left = analyze_expr(*expr.args[0], current, env, Type::Unknown, changed, finalPass);
            Type right = analyze_expr(*expr.args[1], current, env, left, changed, finalPass);
            if (left == Type::Unknown && right != Type::Unknown) {
                left = analyze_expr(*expr.args[0], current, env, right, changed, finalPass);
            }
            if (left != Type::Unknown && right != Type::Unknown && left != right) {
                fail(expr.token, "EQUAL operands must have the same type");
            }
            if (left == Type::Void || right == Type::Void) fail(expr.token, "EQUAL cannot compare VOID");
            return Type::Bool;
        }
        fail(expr.token, "unknown operator '" + expr.op + "'");
    }
};

static bool expr_uses_windows_api(const Expr& expr) {
    if (expr.kind == ExprKind::Call) {
        if (const BuiltinFunction* builtin = find_builtin_function(expr.name); builtin && builtin->usesWindowsApi) {
            return true;
        }
    }
    for (const auto& arg : expr.args) {
        if (expr_uses_windows_api(*arg)) return true;
    }
    return false;
}

static bool stmt_uses_windows_api(const Stmt& stmt) {
    if (stmt.expr && expr_uses_windows_api(*stmt.expr)) return true;
    for (const auto& child : stmt.thenBody) {
        if (stmt_uses_windows_api(*child)) return true;
    }
    for (const auto& child : stmt.elseBody) {
        if (stmt_uses_windows_api(*child)) return true;
    }
    return false;
}

static bool module_uses_windows_api(const Module& module) {
    for (const auto& decl : module.globals) {
        if (decl.value && expr_uses_windows_api(*decl.value)) return true;
    }
    for (const auto& fn : module.functions) {
        for (const auto& stmt : fn.body) {
            if (stmt_uses_windows_api(*stmt)) return true;
        }
    }
    return false;
}

class CppEmitter {
public:
    explicit CppEmitter(const Module& module)
        : module_(module), usesWindowsApi_(module_uses_windows_api(module)) {}

    std::string emit() {
        if (usesWindowsApi_) out_ << "#include <algorithm>\n";
        if (usesWindowsApi_) out_ << "#include <cstdlib>\n";
        out_ << "#include <cstdint>\n";
        if (usesWindowsApi_) out_ << "#include <deque>\n";
        out_ << "#include <iostream>\n";
        out_ << "#include <sstream>\n";
        out_ << "#include <stdexcept>\n";
        out_ << "#include <string>\n";
        out_ << "#include <unordered_map>\n";
        out_ << "#include <vector>\n\n";
        if (usesWindowsApi_) {
            out_ << "#ifdef _WIN32\n";
            out_ << "#define WIN32_LEAN_AND_MEAN\n";
            out_ << "#include <windows.h>\n";
            out_ << "#include <windowsx.h>\n";
            out_ << "#endif\n\n";
        }
        emit_runtime();
        emit_prototypes();
        for (const auto& decl : module_.globals) emit_declaration(decl, 0, true);
        if (!module_.globals.empty()) out_ << "\n";
        for (const auto& fn : module_.functions) emit_function(fn);
        return out_.str();
    }

private:
    const Module& module_;
    bool usesWindowsApi_ = false;
    std::ostringstream out_;

    static std::string cpp_type(Type type) {
        switch (type) {
            case Type::Number: return "std::int64_t";
            case Type::Text: return "std::string";
            case Type::Bool: return "bool";
            case Type::Void: return "void";
            case Type::Unknown: return "void";
        }
        return "void";
    }

    static std::string cpp_name(const std::string& name, bool function = false) {
        if (function && name == "MAIN") return "main";
        return (function ? "fn_" : "tl_") + name;
    }

    static std::string indent(int level) { return std::string(static_cast<std::size_t>(level * 4), ' '); }

    static std::string escape_cpp_string(const std::string& value) {
        std::string result;
        for (char ch : value) {
            if (ch == '\\') result += "\\\\";
            else if (ch == '"') result += "\\\"";
            else if (ch == '\n') result += "\\n";
            else result.push_back(ch);
        }
        return result;
    }

    void emit_runtime() {
        out_ << "namespace tlang_runtime {\n";
        out_ << "std::vector<std::string> split_words(const std::string& line) {\n";
        out_ << "    std::istringstream input(line);\n";
        out_ << "    std::vector<std::string> words;\n";
        out_ << "    for (std::string word; input >> word;) words.push_back(word);\n";
        out_ << "    return words;\n";
        out_ << "}\n";
        out_ << "std::int64_t parse_number_words(const std::string& line) {\n";
        out_ << "    static const std::unordered_map<std::string, std::int64_t> small = {{\"ZERO\",0},{\"ONE\",1},{\"TWO\",2},{\"THREE\",3},{\"FOUR\",4},{\"FIVE\",5},{\"SIX\",6},{\"SEVEN\",7},{\"EIGHT\",8},{\"NINE\",9},{\"TEN\",10},{\"ELEVEN\",11},{\"TWELVE\",12},{\"THIRTEEN\",13},{\"FOURTEEN\",14},{\"FIFTEEN\",15},{\"SIXTEEN\",16},{\"SEVENTEEN\",17},{\"EIGHTEEN\",18},{\"NINETEEN\",19}};\n";
        out_ << "    static const std::unordered_map<std::string, std::int64_t> tens = {{\"TWENTY\",20},{\"THIRTY\",30},{\"FORTY\",40},{\"FIFTY\",50},{\"SIXTY\",60},{\"SEVENTY\",70},{\"EIGHTY\",80},{\"NINETY\",90}};\n";
        out_ << "    static const std::unordered_map<std::string, std::int64_t> scales = {{\"THOUSAND\",1000},{\"MILLION\",1000000},{\"BILLION\",1000000000},{\"TRILLION\",1000000000000LL}};\n";
        out_ << "    auto words = split_words(line);\n";
        out_ << "    if (words.empty()) throw std::runtime_error(\"empty number input\");\n";
        out_ << "    std::size_t i = 0; bool neg = false; if (words[i] == \"NEGATIVE\") { neg = true; ++i; }\n";
        out_ << "    std::int64_t total = 0; bool any = false;\n";
        out_ << "    while (i < words.size()) {\n";
        out_ << "        std::int64_t group = 0;\n";
        out_ << "        if (small.find(words[i]) != small.end()) { group = small.at(words[i]); ++i; }\n";
        out_ << "        else if (tens.find(words[i]) != tens.end()) { group = tens.at(words[i]); ++i; if (i < words.size() && small.find(words[i]) != small.end() && small.at(words[i]) > 0 && small.at(words[i]) < 10) { group += small.at(words[i]); ++i; } }\n";
        out_ << "        else throw std::runtime_error(\"invalid number input\");\n";
        out_ << "        any = true;\n";
        out_ << "        if (i < words.size() && words[i] == \"HUNDRED\") { group *= 100; ++i; }\n";
        out_ << "        if (i < words.size() && scales.find(words[i]) != scales.end()) { total += group * scales.at(words[i]); ++i; continue; }\n";
        out_ << "        total += group;\n";
        out_ << "    }\n";
        out_ << "    if (!any) throw std::runtime_error(\"invalid number input\");\n";
        out_ << "    return neg ? -total : total;\n";
        out_ << "}\n";
        out_ << "bool parse_bool_word(const std::string& line) {\n";
        out_ << "    if (line == \"TRUE\") return true;\n";
        out_ << "    if (line == \"FALSE\") return false;\n";
        out_ << "    throw std::runtime_error(\"expected TRUE or FALSE\");\n";
        out_ << "}\n";
        out_ << "void print_bool(bool value) { std::cout << (value ? \"TRUE\" : \"FALSE\") << '\\n'; }\n";
        if (usesWindowsApi_) emit_windows_runtime();
        out_ << "}\n\n";
    }

    void emit_windows_runtime() {
        out_ << "#ifdef _WIN32\n";
        out_ << "namespace winapi {\n";
        out_ << "constexpr const char* window_class_name = \"TLANG_WINDOW\";\n";
        out_ << "std::deque<std::int64_t> command_queue;\n";
        out_ << "std::unordered_map<std::int64_t, std::unordered_map<std::int64_t, COLORREF>> pixel_dictionaries;\n";
        out_ << "struct PixelLayer { std::int64_t window = 0; std::int64_t z = 0; std::unordered_map<std::int64_t, COLORREF> pixels; };\n";
        out_ << "struct PixelObject { std::int64_t window = 0; std::int64_t target = 0; std::int64_t x = 0; std::int64_t y = 0; std::int64_t width = 0; std::int64_t height = 0; std::int64_t id = 0; std::string text; COLORREF back = 0; COLORREF fore = 0; COLORREF border = 0; bool input = false; int animation = 0; };\n";
        out_ << "std::unordered_map<std::int64_t, PixelLayer> pixel_layers;\n";
        out_ << "std::unordered_map<std::int64_t, std::vector<std::int64_t>> window_layers;\n";
        out_ << "std::unordered_map<std::int64_t, PixelObject> pixel_objects;\n";
        out_ << "std::unordered_map<std::int64_t, std::vector<std::int64_t>> window_objects;\n";
        out_ << "std::unordered_map<HWND, WNDPROC> edit_previous_procs;\n";
        out_ << "std::unordered_map<HWND, std::int64_t> edit_command_ids;\n";
        out_ << "std::int64_t focused_input = 0;\n";
        out_ << "std::int64_t next_pixel_id = -1;\n";
        out_ << "bool registered = false;\n";
        out_ << "std::int64_t to_number(HWND handle) { return static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(handle)); }\n";
        out_ << "HWND to_window(std::int64_t handle) { return reinterpret_cast<HWND>(static_cast<std::intptr_t>(handle)); }\n";
        out_ << "HMENU to_menu(std::int64_t id) { return reinterpret_cast<HMENU>(static_cast<std::intptr_t>(id)); }\n";
        out_ << "std::int64_t pixel_key(std::int64_t x, std::int64_t y) { return (x << 32) ^ (y & 0xffffffffLL); }\n";
        out_ << "std::int64_t pixel_x(std::int64_t key) { return key >> 32; }\n";
        out_ << "std::int64_t pixel_y(std::int64_t key) { return key & 0xffffffffLL; }\n";
        out_ << "bool is_layer(std::int64_t handle) { return pixel_layers.find(handle) != pixel_layers.end(); }\n";
        out_ << "std::int64_t target_window(std::int64_t target) { return is_layer(target) ? pixel_layers.at(target).window : target; }\n";
        out_ << "std::unordered_map<std::int64_t, COLORREF>& target_pixels(std::int64_t target) { return is_layer(target) ? pixel_layers[target].pixels : pixel_dictionaries[target]; }\n";
        out_ << "int clamp_color(std::int64_t value) {\n";
        out_ << "    if (value < 0) return 0;\n";
        out_ << "    if (value > 255) return 255;\n";
        out_ << "    return static_cast<int>(value);\n";
        out_ << "}\n";
        out_ << "COLORREF mix_color(COLORREF left, COLORREF right, int amount) {\n";
        out_ << "    if (amount < 0) amount = 0;\n";
        out_ << "    if (amount > 255) amount = 255;\n";
        out_ << "    int inverse = 255 - amount;\n";
        out_ << "    return RGB((GetRValue(left) * inverse + GetRValue(right) * amount) / 255,\n";
        out_ << "               (GetGValue(left) * inverse + GetGValue(right) * amount) / 255,\n";
        out_ << "               (GetBValue(left) * inverse + GetBValue(right) * amount) / 255);\n";
        out_ << "}\n";
        out_ << "void fail_last_error(const char* action) {\n";
        out_ << "    std::ostringstream message;\n";
        out_ << "    message << action << \" failed with Windows error \" << GetLastError();\n";
        out_ << "    throw std::runtime_error(message.str());\n";
        out_ << "}\n";
        out_ << "void set_pixel(std::int64_t target, std::int64_t x, std::int64_t y, COLORREF color) { target_pixels(target)[pixel_key(x, y)] = color; }\n";
        out_ << "void line(std::int64_t target, std::int64_t x1, std::int64_t y1, std::int64_t x2, std::int64_t y2, COLORREF color) {\n";
        out_ << "    std::int64_t dx = std::llabs(x2 - x1), sx = x1 < x2 ? 1 : -1;\n";
        out_ << "    std::int64_t dy = -std::llabs(y2 - y1), sy = y1 < y2 ? 1 : -1;\n";
        out_ << "    std::int64_t err = dx + dy;\n";
        out_ << "    while (true) {\n";
        out_ << "        set_pixel(target, x1, y1, color);\n";
        out_ << "        if (x1 == x2 && y1 == y2) break;\n";
        out_ << "        std::int64_t e2 = 2 * err;\n";
        out_ << "        if (e2 >= dy) { err += dy; x1 += sx; }\n";
        out_ << "        if (e2 <= dx) { err += dx; y1 += sy; }\n";
        out_ << "    }\n";
        out_ << "}\n";
        out_ << "void rect(std::int64_t target, std::int64_t x, std::int64_t y, std::int64_t width, std::int64_t height, COLORREF color) {\n";
        out_ << "    for (std::int64_t yy = 0; yy < height; ++yy) for (std::int64_t xx = 0; xx < width; ++xx) set_pixel(target, x + xx, y + yy, color);\n";
        out_ << "}\n";
        out_ << "void frame(std::int64_t target, std::int64_t x, std::int64_t y, std::int64_t width, std::int64_t height, COLORREF color) {\n";
        out_ << "    line(target, x, y, x + width - 1, y, color);\n";
        out_ << "    line(target, x, y + height - 1, x + width - 1, y + height - 1, color);\n";
        out_ << "    line(target, x, y, x, y + height - 1, color);\n";
        out_ << "    line(target, x + width - 1, y, x + width - 1, y + height - 1, color);\n";
        out_ << "}\n";
        out_ << "void circle(std::int64_t target, std::int64_t cx, std::int64_t cy, std::int64_t radius, COLORREF color, bool filled) {\n";
        out_ << "    std::int64_t r2 = radius * radius;\n";
        out_ << "    std::int64_t inner = (radius > 1 ? (radius - 1) * (radius - 1) : 0);\n";
        out_ << "    for (std::int64_t y = -radius; y <= radius; ++y) for (std::int64_t x = -radius; x <= radius; ++x) {\n";
        out_ << "        std::int64_t d = x * x + y * y;\n";
        out_ << "        if ((filled && d <= r2) || (!filled && d <= r2 && d >= inner)) set_pixel(target, cx + x, cy + y, color);\n";
        out_ << "    }\n";
        out_ << "}\n";
        out_ << "void ellipse(std::int64_t target, std::int64_t cx, std::int64_t cy, std::int64_t rx, std::int64_t ry, COLORREF color) {\n";
        out_ << "    if (rx <= 0 || ry <= 0) return;\n";
        out_ << "    std::int64_t rx2 = rx * rx, ry2 = ry * ry, limit = rx2 * ry2;\n";
        out_ << "    for (std::int64_t y = -ry; y <= ry; ++y) for (std::int64_t x = -rx; x <= rx; ++x) if (x * x * ry2 + y * y * rx2 <= limit) set_pixel(target, cx + x, cy + y, color);\n";
        out_ << "}\n";
        out_ << "std::int64_t edge(std::int64_t ax, std::int64_t ay, std::int64_t bx, std::int64_t by, std::int64_t px, std::int64_t py) { return (px - ax) * (by - ay) - (py - ay) * (bx - ax); }\n";
        out_ << "void triangle(std::int64_t target, std::int64_t x1, std::int64_t y1, std::int64_t x2, std::int64_t y2, std::int64_t x3, std::int64_t y3, COLORREF color) {\n";
        out_ << "    std::int64_t minx = std::min({x1, x2, x3}), maxx = std::max({x1, x2, x3});\n";
        out_ << "    std::int64_t miny = std::min({y1, y2, y3}), maxy = std::max({y1, y2, y3});\n";
        out_ << "    for (std::int64_t y = miny; y <= maxy; ++y) for (std::int64_t x = minx; x <= maxx; ++x) {\n";
        out_ << "        std::int64_t a = edge(x1, y1, x2, y2, x, y), b = edge(x2, y2, x3, y3, x, y), c = edge(x3, y3, x1, y1, x, y);\n";
        out_ << "        if ((a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0)) set_pixel(target, x, y, color);\n";
        out_ << "    }\n";
        out_ << "}\n";
        out_ << "void diamond(std::int64_t target, std::int64_t cx, std::int64_t cy, std::int64_t radius, COLORREF color) {\n";
        out_ << "    for (std::int64_t y = -radius; y <= radius; ++y) for (std::int64_t x = -radius; x <= radius; ++x) if (std::llabs(x) + std::llabs(y) <= radius) set_pixel(target, cx + x, cy + y, color);\n";
        out_ << "}\n";
        out_ << "void star(std::int64_t target, std::int64_t cx, std::int64_t cy, std::int64_t radius, COLORREF color) {\n";
        out_ << "    line(target, cx, cy - radius, cx, cy + radius, color);\n";
        out_ << "    line(target, cx - radius, cy, cx + radius, cy, color);\n";
        out_ << "    line(target, cx - radius, cy - radius, cx + radius, cy + radius, color);\n";
        out_ << "    line(target, cx - radius, cy + radius, cx + radius, cy - radius, color);\n";
        out_ << "    circle(target, cx, cy, radius / 4 + 1, color, true);\n";
        out_ << "}\n";
        out_ << "const std::vector<std::string>& glyph(char ch) {\n";
        out_ << "    static const std::vector<std::string> blank = {\"00000\",\"00000\",\"00000\",\"00000\",\"00000\",\"00000\",\"00000\"};\n";
        out_ << "    static const std::unordered_map<char, std::vector<std::string>> font = {\n";
        out_ << "        {'A', {\"01110\",\"10001\",\"10001\",\"11111\",\"10001\",\"10001\",\"10001\"}}, {'B', {\"11110\",\"10001\",\"10001\",\"11110\",\"10001\",\"10001\",\"11110\"}}, {'C', {\"01111\",\"10000\",\"10000\",\"10000\",\"10000\",\"10000\",\"01111\"}},\n";
        out_ << "        {'D', {\"11110\",\"10001\",\"10001\",\"10001\",\"10001\",\"10001\",\"11110\"}}, {'E', {\"11111\",\"10000\",\"10000\",\"11110\",\"10000\",\"10000\",\"11111\"}}, {'F', {\"11111\",\"10000\",\"10000\",\"11110\",\"10000\",\"10000\",\"10000\"}},\n";
        out_ << "        {'G', {\"01111\",\"10000\",\"10000\",\"10011\",\"10001\",\"10001\",\"01111\"}}, {'H', {\"10001\",\"10001\",\"10001\",\"11111\",\"10001\",\"10001\",\"10001\"}}, {'I', {\"11111\",\"00100\",\"00100\",\"00100\",\"00100\",\"00100\",\"11111\"}},\n";
        out_ << "        {'J', {\"00111\",\"00010\",\"00010\",\"00010\",\"10010\",\"10010\",\"01100\"}}, {'K', {\"10001\",\"10010\",\"10100\",\"11000\",\"10100\",\"10010\",\"10001\"}}, {'L', {\"10000\",\"10000\",\"10000\",\"10000\",\"10000\",\"10000\",\"11111\"}},\n";
        out_ << "        {'M', {\"10001\",\"11011\",\"10101\",\"10101\",\"10001\",\"10001\",\"10001\"}}, {'N', {\"10001\",\"11001\",\"10101\",\"10011\",\"10001\",\"10001\",\"10001\"}}, {'O', {\"01110\",\"10001\",\"10001\",\"10001\",\"10001\",\"10001\",\"01110\"}},\n";
        out_ << "        {'P', {\"11110\",\"10001\",\"10001\",\"11110\",\"10000\",\"10000\",\"10000\"}}, {'Q', {\"01110\",\"10001\",\"10001\",\"10001\",\"10101\",\"10010\",\"01101\"}}, {'R', {\"11110\",\"10001\",\"10001\",\"11110\",\"10100\",\"10010\",\"10001\"}},\n";
        out_ << "        {'S', {\"01111\",\"10000\",\"10000\",\"01110\",\"00001\",\"00001\",\"11110\"}}, {'T', {\"11111\",\"00100\",\"00100\",\"00100\",\"00100\",\"00100\",\"00100\"}}, {'U', {\"10001\",\"10001\",\"10001\",\"10001\",\"10001\",\"10001\",\"01110\"}},\n";
        out_ << "        {'V', {\"10001\",\"10001\",\"10001\",\"10001\",\"10001\",\"01010\",\"00100\"}}, {'W', {\"10001\",\"10001\",\"10001\",\"10101\",\"10101\",\"10101\",\"01010\"}}, {'X', {\"10001\",\"10001\",\"01010\",\"00100\",\"01010\",\"10001\",\"10001\"}},\n";
        out_ << "        {'Y', {\"10001\",\"10001\",\"01010\",\"00100\",\"00100\",\"00100\",\"00100\"}}, {'Z', {\"11111\",\"00001\",\"00010\",\"00100\",\"01000\",\"10000\",\"11111\"}},\n";
        out_ << "        {'0', {\"01110\",\"10001\",\"10011\",\"10101\",\"11001\",\"10001\",\"01110\"}}, {'1', {\"00100\",\"01100\",\"00100\",\"00100\",\"00100\",\"00100\",\"01110\"}}, {'2', {\"01110\",\"10001\",\"00001\",\"00010\",\"00100\",\"01000\",\"11111\"}},\n";
        out_ << "        {'3', {\"11110\",\"00001\",\"00001\",\"01110\",\"00001\",\"00001\",\"11110\"}}, {'4', {\"00010\",\"00110\",\"01010\",\"10010\",\"11111\",\"00010\",\"00010\"}}, {'5', {\"11111\",\"10000\",\"10000\",\"11110\",\"00001\",\"00001\",\"11110\"}},\n";
        out_ << "        {'6', {\"01110\",\"10000\",\"10000\",\"11110\",\"10001\",\"10001\",\"01110\"}}, {'7', {\"11111\",\"00001\",\"00010\",\"00100\",\"01000\",\"01000\",\"01000\"}}, {'8', {\"01110\",\"10001\",\"10001\",\"01110\",\"10001\",\"10001\",\"01110\"}},\n";
        out_ << "        {'9', {\"01110\",\"10001\",\"10001\",\"01111\",\"00001\",\"00001\",\"01110\"}}, {'-', {\"00000\",\"00000\",\"00000\",\"11111\",\"00000\",\"00000\",\"00000\"}}, {' ', {\"00000\",\"00000\",\"00000\",\"00000\",\"00000\",\"00000\",\"00000\"}}\n";
        out_ << "    };\n";
        out_ << "    auto found = font.find(ch);\n";
        out_ << "    return found == font.end() ? blank : found->second;\n";
        out_ << "}\n";
        out_ << "void text(std::int64_t target, std::int64_t x, std::int64_t y, std::int64_t scale, COLORREF color, const std::string& value) {\n";
        out_ << "    if (scale < 1) scale = 1;\n";
        out_ << "    std::int64_t cursor = x;\n";
        out_ << "    for (char raw : value) {\n";
        out_ << "        char ch = raw >= 'a' && raw <= 'z' ? static_cast<char>(raw - 'a' + 'A') : raw;\n";
        out_ << "        const auto& rows = glyph(ch);\n";
        out_ << "        for (std::int64_t gy = 0; gy < static_cast<std::int64_t>(rows.size()); ++gy) for (std::int64_t gx = 0; gx < static_cast<std::int64_t>(rows[gy].size()); ++gx) if (rows[gy][gx] == '1') rect(target, cursor + gx * scale, y + gy * scale, scale, scale, color);\n";
        out_ << "        cursor += 6 * scale;\n";
        out_ << "    }\n";
        out_ << "}\n";
        out_ << "void draw_object(const PixelObject& object) {\n";
        out_ << "    rect(object.target, object.x, object.y, object.width, object.height, object.back);\n";
        out_ << "    frame(object.target, object.x, object.y, object.width, object.height, object.border);\n";
        out_ << "    std::int64_t scale = std::max<std::int64_t>(1, (object.height - 8) / 9);\n";
        out_ << "    text(object.target, object.x + 5, object.y + std::max<std::int64_t>(2, (object.height - 7 * scale) / 2), scale, object.fore, object.text);\n";
        out_ << "}\n";
        out_ << "void redraw_object(std::int64_t objectHandle) { auto found = pixel_objects.find(objectHandle); if (found != pixel_objects.end()) draw_object(found->second); }\n";
        out_ << "void present_target(std::int64_t target) { HWND window = to_window(target_window(target)); InvalidateRect(window, nullptr, TRUE); UpdateWindow(window); }\n";
        out_ << "LRESULT CALLBACK edit_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {\n";
        out_ << "    if (message == WM_KEYDOWN && wparam == VK_RETURN) {\n";
        out_ << "        HWND parent = GetParent(hwnd);\n";
        out_ << "        auto found = edit_command_ids.find(hwnd);\n";
        out_ << "        if (parent != nullptr && found != edit_command_ids.end()) {\n";
        out_ << "            SendMessageA(parent, WM_COMMAND, MAKEWPARAM(static_cast<UINT>(found->second), BN_CLICKED), reinterpret_cast<LPARAM>(hwnd));\n";
        out_ << "        }\n";
        out_ << "        return 0;\n";
        out_ << "    }\n";
        out_ << "    auto previous = edit_previous_procs.find(hwnd);\n";
        out_ << "    if (previous != edit_previous_procs.end()) return CallWindowProcA(previous->second, hwnd, message, wparam, lparam);\n";
        out_ << "    return DefWindowProcA(hwnd, message, wparam, lparam);\n";
        out_ << "}\n";
        out_ << "LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {\n";
        out_ << "    switch (message) {\n";
        out_ << "        case WM_COMMAND:\n";
        out_ << "            command_queue.push_back(static_cast<std::int64_t>(LOWORD(wparam)));\n";
        out_ << "            return 0;\n";
        out_ << "        case WM_LBUTTONDOWN: {\n";
        out_ << "            std::int64_t handle = to_number(window);\n";
        out_ << "            std::int64_t mx = GET_X_LPARAM(lparam), my = GET_Y_LPARAM(lparam);\n";
        out_ << "            focused_input = 0;\n";
        out_ << "            auto objects = window_objects.find(handle);\n";
        out_ << "            if (objects != window_objects.end()) {\n";
        out_ << "                for (auto it = objects->second.rbegin(); it != objects->second.rend(); ++it) {\n";
        out_ << "                    auto found = pixel_objects.find(*it);\n";
        out_ << "                    if (found == pixel_objects.end()) continue;\n";
        out_ << "                    PixelObject& object = found->second;\n";
        out_ << "                    if (mx >= object.x && my >= object.y && mx < object.x + object.width && my < object.y + object.height) {\n";
        out_ << "                        if (object.input) { focused_input = *it; SetFocus(window); }\n";
        out_ << "                        else command_queue.push_back(object.id);\n";
        out_ << "                        break;\n";
        out_ << "                    }\n";
        out_ << "                }\n";
        out_ << "            }\n";
        out_ << "            return 0;\n";
        out_ << "        }\n";
        out_ << "        case WM_CHAR:\n";
        out_ << "            if (focused_input && pixel_objects.find(focused_input) != pixel_objects.end()) {\n";
        out_ << "                PixelObject& object = pixel_objects[focused_input];\n";
        out_ << "                if (wparam == 8) { if (!object.text.empty()) object.text.pop_back(); }\n";
        out_ << "                else if (wparam >= 32 && wparam <= 126) object.text.push_back(static_cast<char>(wparam));\n";
        out_ << "                draw_object(object);\n";
        out_ << "                present_target(object.target);\n";
        out_ << "            }\n";
        out_ << "            return 0;\n";
        out_ << "        case WM_DESTROY:\n";
        out_ << "            pixel_dictionaries.erase(to_number(window));\n";
        out_ << "            window_layers.erase(to_number(window));\n";
        out_ << "            window_objects.erase(to_number(window));\n";
        out_ << "            PostQuitMessage(0);\n";
        out_ << "            return 0;\n";
        out_ << "        case WM_PAINT: {\n";
        out_ << "            PAINTSTRUCT paint{};\n";
        out_ << "            HDC dc = BeginPaint(window, &paint);\n";
        out_ << "            auto found = pixel_dictionaries.find(to_number(window));\n";
        out_ << "            if (found != pixel_dictionaries.end()) {\n";
        out_ << "                for (const auto& [key, color] : found->second) {\n";
        out_ << "                    SetPixel(dc, static_cast<int>(pixel_x(key)), static_cast<int>(pixel_y(key)), color);\n";
        out_ << "                }\n";
        out_ << "            }\n";
        out_ << "            auto layers = window_layers.find(to_number(window));\n";
        out_ << "            if (layers != window_layers.end()) {\n";
        out_ << "                std::vector<std::int64_t> ordered = layers->second;\n";
        out_ << "                std::sort(ordered.begin(), ordered.end(), [](std::int64_t left, std::int64_t right) { return pixel_layers[left].z < pixel_layers[right].z; });\n";
        out_ << "                for (std::int64_t layerHandle : ordered) for (const auto& [key, color] : pixel_layers[layerHandle].pixels) SetPixel(dc, static_cast<int>(pixel_x(key)), static_cast<int>(pixel_y(key)), color);\n";
        out_ << "            }\n";
        out_ << "            EndPaint(window, &paint);\n";
        out_ << "            return 0;\n";
        out_ << "        }\n";
        out_ << "    }\n";
        out_ << "    return DefWindowProcA(window, message, wparam, lparam);\n";
        out_ << "}\n";
        out_ << "void ensure_window_class() {\n";
        out_ << "    if (registered) return;\n";
        out_ << "    WNDCLASSA wc{};\n";
        out_ << "    wc.lpfnWndProc = window_proc;\n";
        out_ << "    wc.hInstance = GetModuleHandleA(nullptr);\n";
        out_ << "    wc.lpszClassName = window_class_name;\n";
        out_ << "    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);\n";
        out_ << "    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);\n";
        out_ << "    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) fail_last_error(\"RegisterClassA\");\n";
        out_ << "    registered = true;\n";
        out_ << "}\n";
        out_ << "}\n";
        out_ << "std::int64_t win_message(const std::string& title, const std::string& message) {\n";
        out_ << "    return MessageBoxA(nullptr, message.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);\n";
        out_ << "}\n";
        out_ << "std::int64_t win_window(const std::string& title, std::int64_t width, std::int64_t height) {\n";
        out_ << "    winapi::ensure_window_class();\n";
        out_ << "    HWND window = CreateWindowExA(0, winapi::window_class_name, title.c_str(), WS_OVERLAPPEDWINDOW | WS_VISIBLE,\n";
        out_ << "                                   CW_USEDEFAULT, CW_USEDEFAULT, static_cast<int>(width), static_cast<int>(height),\n";
        out_ << "                                   nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);\n";
        out_ << "    if (!window) winapi::fail_last_error(\"CreateWindowExA\");\n";
        out_ << "    return winapi::to_number(window);\n";
        out_ << "}\n";
        out_ << "std::int64_t win_button(std::int64_t parent, const std::string& text, std::int64_t x, std::int64_t y,\n";
        out_ << "                        std::int64_t width, std::int64_t height, std::int64_t id) {\n";
        out_ << "    HWND control = CreateWindowExA(0, \"BUTTON\", text.c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,\n";
        out_ << "                                   static_cast<int>(x), static_cast<int>(y), static_cast<int>(width), static_cast<int>(height),\n";
        out_ << "                                   winapi::to_window(parent), winapi::to_menu(id), GetModuleHandleA(nullptr), nullptr);\n";
        out_ << "    if (!control) winapi::fail_last_error(\"CreateWindowExA BUTTON\");\n";
        out_ << "    return winapi::to_number(control);\n";
        out_ << "}\n";
        out_ << "std::int64_t win_label(std::int64_t parent, const std::string& text, std::int64_t x, std::int64_t y,\n";
        out_ << "                       std::int64_t width, std::int64_t height) {\n";
        out_ << "    HWND control = CreateWindowExA(0, \"STATIC\", text.c_str(), WS_CHILD | WS_VISIBLE,\n";
        out_ << "                                   static_cast<int>(x), static_cast<int>(y), static_cast<int>(width), static_cast<int>(height),\n";
        out_ << "                                   winapi::to_window(parent), nullptr, GetModuleHandleA(nullptr), nullptr);\n";
        out_ << "    if (!control) winapi::fail_last_error(\"CreateWindowExA STATIC\");\n";
        out_ << "    return winapi::to_number(control);\n";
        out_ << "}\n";
        out_ << "std::int64_t win_edit(std::int64_t parent, const std::string& text, std::int64_t x, std::int64_t y,\n";
        out_ << "                      std::int64_t width, std::int64_t height, std::int64_t id) {\n";
        out_ << "    HWND control = CreateWindowExA(WS_EX_CLIENTEDGE, \"EDIT\", text.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,\n";
        out_ << "                                   static_cast<int>(x), static_cast<int>(y), static_cast<int>(width), static_cast<int>(height),\n";
        out_ << "                                   winapi::to_window(parent), winapi::to_menu(id), GetModuleHandleA(nullptr), nullptr);\n";
        out_ << "    if (!control) winapi::fail_last_error(\"CreateWindowExA EDIT\");\n";
        out_ << "    winapi::edit_command_ids[control] = id;\n";
        out_ << "    winapi::edit_previous_procs[control] = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(control, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(winapi::edit_proc)));\n";
        out_ << "    return winapi::to_number(control);\n";
        out_ << "}\n";
        out_ << "std::string win_text(std::int64_t handle) {\n";
        out_ << "    HWND window = winapi::to_window(handle);\n";
        out_ << "    int length = GetWindowTextLengthA(window);\n";
        out_ << "    if (length < 0) winapi::fail_last_error(\"GetWindowTextLengthA\");\n";
        out_ << "    std::vector<char> buffer(static_cast<std::size_t>(length) + 1);\n";
        out_ << "    if (GetWindowTextA(window, buffer.data(), length + 1) == 0 && length > 0) winapi::fail_last_error(\"GetWindowTextA\");\n";
        out_ << "    return std::string(buffer.data());\n";
        out_ << "}\n";
        out_ << "void win_set_text(std::int64_t handle, const std::string& text) {\n";
        out_ << "    if (!SetWindowTextA(winapi::to_window(handle), text.c_str())) winapi::fail_last_error(\"SetWindowTextA\");\n";
        out_ << "}\n";
        out_ << "void win_show(std::int64_t handle) {\n";
        out_ << "    HWND window = winapi::to_window(handle);\n";
        out_ << "    ShowWindow(window, SW_SHOW);\n";
        out_ << "    UpdateWindow(window);\n";
        out_ << "}\n";
        out_ << "bool win_wait() {\n";
        out_ << "    MSG message{};\n";
        out_ << "    BOOL result = GetMessageA(&message, nullptr, 0, 0);\n";
        out_ << "    if (result == -1) winapi::fail_last_error(\"GetMessageA\");\n";
        out_ << "    if (result == 0) return false;\n";
        out_ << "    TranslateMessage(&message);\n";
        out_ << "    DispatchMessageA(&message);\n";
        out_ << "    return true;\n";
        out_ << "}\n";
        out_ << "std::int64_t win_run() {\n";
        out_ << "    MSG message{};\n";
        out_ << "    while (true) {\n";
        out_ << "        BOOL result = GetMessageA(&message, nullptr, 0, 0);\n";
        out_ << "        if (result == -1) winapi::fail_last_error(\"GetMessageA\");\n";
        out_ << "        if (result == 0) return static_cast<std::int64_t>(message.wParam);\n";
        out_ << "        TranslateMessage(&message);\n";
        out_ << "        DispatchMessageA(&message);\n";
        out_ << "    }\n";
        out_ << "}\n";
        out_ << "std::int64_t win_command() {\n";
        out_ << "    if (winapi::command_queue.empty()) return 0;\n";
        out_ << "    std::int64_t id = winapi::command_queue.front();\n";
        out_ << "    winapi::command_queue.pop_front();\n";
        out_ << "    return id;\n";
        out_ << "}\n";
        out_ << "void win_quit() { PostQuitMessage(0); }\n";
        out_ << "std::int64_t pixel_window(const std::string& title, std::int64_t width, std::int64_t height) {\n";
        out_ << "    winapi::ensure_window_class();\n";
        out_ << "    RECT rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};\n";
        out_ << "    DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;\n";
        out_ << "    if (!AdjustWindowRect(&rect, style, FALSE)) winapi::fail_last_error(\"AdjustWindowRect\");\n";
        out_ << "    HWND window = CreateWindowExA(0, winapi::window_class_name, title.c_str(), style,\n";
        out_ << "                                   CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,\n";
        out_ << "                                   nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);\n";
        out_ << "    if (!window) winapi::fail_last_error(\"CreateWindowExA PIXELWINDOW\");\n";
        out_ << "    std::int64_t handle = winapi::to_number(window);\n";
        out_ << "    winapi::pixel_dictionaries[handle];\n";
        out_ << "    return handle;\n";
        out_ << "}\n";
        out_ << "std::int64_t pixel_color(std::int64_t red, std::int64_t green, std::int64_t blue) {\n";
        out_ << "    return static_cast<std::int64_t>(RGB(winapi::clamp_color(red), winapi::clamp_color(green), winapi::clamp_color(blue)));\n";
        out_ << "}\n";
        out_ << "int pixel_hex_digit(char ch) {\n";
        out_ << "    if (ch >= '0' && ch <= '9') return ch - '0';\n";
        out_ << "    if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');\n";
        out_ << "    if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');\n";
        out_ << "    throw std::runtime_error(\"invalid HEX color digit\");\n";
        out_ << "}\n";
        out_ << "std::int64_t pixel_hex(const std::string& value) {\n";
        out_ << "    std::string hex;\n";
        out_ << "    for (char ch : value) {\n";
        out_ << "        if (ch == ' ' || ch == '#') continue;\n";
        out_ << "        hex.push_back(ch);\n";
        out_ << "    }\n";
        out_ << "    if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'X' || hex[1] == 'x')) hex = hex.substr(2);\n";
        out_ << "    if (hex.size() == 3) {\n";
        out_ << "        std::string expanded;\n";
        out_ << "        for (char ch : hex) { expanded.push_back(ch); expanded.push_back(ch); }\n";
        out_ << "        hex = expanded;\n";
        out_ << "    }\n";
        out_ << "    if (hex.size() != 6) throw std::runtime_error(\"HEX color must have 3 or 6 digits\");\n";
        out_ << "    std::int64_t red = pixel_hex_digit(hex[0]) * 16 + pixel_hex_digit(hex[1]);\n";
        out_ << "    std::int64_t green = pixel_hex_digit(hex[2]) * 16 + pixel_hex_digit(hex[3]);\n";
        out_ << "    std::int64_t blue = pixel_hex_digit(hex[4]) * 16 + pixel_hex_digit(hex[5]);\n";
        out_ << "    return pixel_color(red, green, blue);\n";
        out_ << "}\n";
        out_ << "void pixel_set(std::int64_t handle, std::int64_t x, std::int64_t y, std::int64_t color) {\n";
        out_ << "    winapi::pixel_dictionaries[handle][winapi::pixel_key(x, y)] = static_cast<COLORREF>(color);\n";
        out_ << "}\n";
        out_ << "std::int64_t pixel_get(std::int64_t handle, std::int64_t x, std::int64_t y) {\n";
        out_ << "    auto canvas = winapi::pixel_dictionaries.find(handle);\n";
        out_ << "    if (canvas == winapi::pixel_dictionaries.end()) return 0;\n";
        out_ << "    auto pixel = canvas->second.find(winapi::pixel_key(x, y));\n";
        out_ << "    return pixel == canvas->second.end() ? 0 : static_cast<std::int64_t>(pixel->second);\n";
        out_ << "}\n";
        out_ << "void pixel_clear(std::int64_t handle) {\n";
        out_ << "    winapi::pixel_dictionaries[handle].clear();\n";
        out_ << "    InvalidateRect(winapi::to_window(handle), nullptr, TRUE);\n";
        out_ << "}\n";
        out_ << "void pixel_present(std::int64_t handle) {\n";
        out_ << "    HWND window = winapi::to_window(handle);\n";
        out_ << "    InvalidateRect(window, nullptr, TRUE);\n";
        out_ << "    UpdateWindow(window);\n";
        out_ << "}\n";
        out_ << "#else\n";
        out_ << "[[noreturn]] void require_windows_api() { throw std::runtime_error(\"WIN built-ins require Windows API support\"); }\n";
        out_ << "std::int64_t win_message(const std::string&, const std::string&) { require_windows_api(); }\n";
        out_ << "std::int64_t win_window(const std::string&, std::int64_t, std::int64_t) { require_windows_api(); }\n";
        out_ << "std::int64_t win_button(std::int64_t, const std::string&, std::int64_t, std::int64_t, std::int64_t, std::int64_t, std::int64_t) { require_windows_api(); }\n";
        out_ << "std::int64_t win_label(std::int64_t, const std::string&, std::int64_t, std::int64_t, std::int64_t, std::int64_t) { require_windows_api(); }\n";
        out_ << "std::int64_t win_edit(std::int64_t, const std::string&, std::int64_t, std::int64_t, std::int64_t, std::int64_t, std::int64_t) { require_windows_api(); }\n";
        out_ << "std::string win_text(std::int64_t) { require_windows_api(); }\n";
        out_ << "void win_set_text(std::int64_t, const std::string&) { require_windows_api(); }\n";
        out_ << "void win_show(std::int64_t) { require_windows_api(); }\n";
        out_ << "bool win_wait() { require_windows_api(); }\n";
        out_ << "std::int64_t win_run() { require_windows_api(); }\n";
        out_ << "std::int64_t win_command() { require_windows_api(); }\n";
        out_ << "void win_quit() { require_windows_api(); }\n";
        out_ << "std::int64_t pixel_window(const std::string&, std::int64_t, std::int64_t) { require_windows_api(); }\n";
        out_ << "std::int64_t pixel_color(std::int64_t red, std::int64_t green, std::int64_t blue) { return ((red & 255) | ((green & 255) << 8) | ((blue & 255) << 16)); }\n";
        out_ << "std::int64_t pixel_hex(const std::string&) { require_windows_api(); }\n";
        out_ << "void pixel_set(std::int64_t, std::int64_t, std::int64_t, std::int64_t) { require_windows_api(); }\n";
        out_ << "std::int64_t pixel_get(std::int64_t, std::int64_t, std::int64_t) { require_windows_api(); }\n";
        out_ << "void pixel_clear(std::int64_t) { require_windows_api(); }\n";
        out_ << "void pixel_present(std::int64_t) { require_windows_api(); }\n";
        out_ << "#endif\n";
    }

    void emit_prototypes() {
        for (const auto& fn : module_.functions) {
            if (fn.name == "MAIN") continue;
            out_ << cpp_type(fn.returnType) << " " << cpp_name(fn.name, true) << "(";
            for (std::size_t i = 0; i < fn.params.size(); ++i) {
                if (i) out_ << ", ";
                out_ << cpp_type(fn.paramTypes[i]) << " " << cpp_name(fn.params[i]);
            }
            out_ << ");\n";
        }
        if (!module_.functions.empty()) out_ << "\n";
    }

    void emit_function(const Function& fn) {
        std::string returnType = fn.name == "MAIN" ? "int" : cpp_type(fn.returnType);
        out_ << returnType << " " << cpp_name(fn.name, true) << "(";
        for (std::size_t i = 0; i < fn.params.size(); ++i) {
            if (i) out_ << ", ";
            out_ << cpp_type(fn.paramTypes[i]) << " " << cpp_name(fn.params[i]);
        }
        out_ << ") {\n";
        for (const auto& stmt : fn.body) emit_statement(*stmt, 1, fn);
        if (fn.name == "MAIN" && fn.returnType == Type::Void) out_ << "    return 0;\n";
        out_ << "}\n\n";
    }

    void emit_declaration(const Declaration& decl, int level, bool global) {
        out_ << indent(level);
        if (decl.fixed) out_ << "const ";
        out_ << cpp_type(decl.type) << " " << cpp_name(decl.name) << " = " << emit_expr(*decl.value) << ";\n";
        (void)global;
    }

    void emit_statement(const Stmt& stmt, int level, const Function& fn) {
        switch (stmt.kind) {
            case StmtKind::Decl:
                out_ << indent(level);
                if (stmt.fixed) out_ << "const ";
                out_ << cpp_type(stmt.declaredType) << " " << cpp_name(stmt.name) << " = " << emit_expr(*stmt.expr) << ";\n";
                break;
            case StmtKind::Assign:
                out_ << indent(level) << cpp_name(stmt.name) << " = " << emit_expr(*stmt.expr) << ";\n";
                break;
            case StmtKind::Return:
                if (fn.name == "MAIN") {
                    if (stmt.expr) out_ << indent(level) << "return static_cast<int>(" << emit_expr(*stmt.expr) << ");\n";
                    else out_ << indent(level) << "return 0;\n";
                } else {
                    out_ << indent(level) << "return";
                    if (stmt.expr) out_ << " " << emit_expr(*stmt.expr);
                    out_ << ";\n";
                }
                break;
            case StmtKind::Input:
                out_ << indent(level) << "{ std::string line; std::getline(std::cin, line); ";
                out_ << cpp_name(stmt.name) << " = ";
                if (stmt.declaredType == Type::Number) out_ << "tlang_runtime::parse_number_words(line)";
                else if (stmt.declaredType == Type::Bool) out_ << "tlang_runtime::parse_bool_word(line)";
                else out_ << "line";
                out_ << "; }\n";
                break;
            case StmtKind::Output:
                if (stmt.expr->resolved == Type::Bool) {
                    out_ << indent(level) << "tlang_runtime::print_bool(" << emit_expr(*stmt.expr) << ");\n";
                } else {
                    out_ << indent(level) << "std::cout << " << emit_expr(*stmt.expr) << " << '\\n';\n";
                }
                break;
            case StmtKind::If:
                out_ << indent(level) << "if (" << emit_expr(*stmt.expr) << ") {\n";
                for (const auto& child : stmt.thenBody) emit_statement(*child, level + 1, fn);
                if (!stmt.elseBody.empty()) {
                    out_ << indent(level) << "} else {\n";
                    for (const auto& child : stmt.elseBody) emit_statement(*child, level + 1, fn);
                }
                out_ << indent(level) << "}\n";
                break;
            case StmtKind::While:
                out_ << indent(level) << "while (" << emit_expr(*stmt.expr) << ") {\n";
                for (const auto& child : stmt.thenBody) emit_statement(*child, level + 1, fn);
                out_ << indent(level) << "}\n";
                break;
            case StmtKind::ExprStmt:
                out_ << indent(level) << emit_expr(*stmt.expr) << ";\n";
                break;
        }
    }

    std::string emit_expr(const Expr& expr) const {
        switch (expr.kind) {
            case ExprKind::Number:
                return std::to_string(expr.number) + "LL";
            case ExprKind::Text:
                return "std::string(\"" + escape_cpp_string(expr.text) + "\")";
            case ExprKind::Bool:
                return expr.boolean ? "true" : "false";
            case ExprKind::Identifier:
                return cpp_name(expr.name);
            case ExprKind::Call: {
                const BuiltinFunction* builtin = find_builtin_function(expr.name);
                std::string code = builtin ? ("tlang_runtime::" + builtin->cppName + "(") : (cpp_name(expr.name, true) + "(");
                for (std::size_t i = 0; i < expr.args.size(); ++i) {
                    if (i) code += ", ";
                    code += emit_expr(*expr.args[i]);
                }
                code += ")";
                return code;
            }
            case ExprKind::Unary:
                return "(!" + emit_expr(*expr.args[0]) + ")";
            case ExprKind::Binary:
                return emit_binary(expr);
        }
        return "";
    }

    std::string emit_binary(const Expr& expr) const {
        const std::string left = emit_expr(*expr.args[0]);
        const std::string right = emit_expr(*expr.args[1]);
        if (expr.op == "ADD") return "(" + left + " + " + right + ")";
        if (expr.op == "SUBTRACT") return "(" + left + " - " + right + ")";
        if (expr.op == "MULTIPLY") return "(" + left + " * " + right + ")";
        if (expr.op == "DIVIDE") return "(" + left + " / " + right + ")";
        if (expr.op == "MODULO") return "(" + left + " % " + right + ")";
        if (expr.op == "JOIN") return "(" + left + " + " + right + ")";
        if (expr.op == "EQUAL") return "(" + left + " == " + right + ")";
        if (expr.op == "LESS") return "(" + left + " < " + right + ")";
        if (expr.op == "GREATER") return "(" + left + " > " + right + ")";
        if (expr.op == "AND") return "(" + left + " && " + right + ")";
        if (expr.op == "OR") return "(" + left + " || " + right + ")";
        return "";
    }
};

static std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw CompileError("cannot open input file: " + path.string());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

static std::vector<std::string> split_source_words(const std::string& line) {
    std::istringstream input(line);
    std::vector<std::string> words;
    for (std::string word; input >> word;) words.push_back(word);
    return words;
}

static std::string lower_ascii(std::string value) {
    for (char& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

static bool is_library_directive(const std::vector<std::string>& words) {
    return words.size() == 5 && words[0] == "USE" && words[1] == "LIBRARY" &&
           is_identifier_word(words[2], false) && words[3] == "END" && words[4] == "LINE";
}

static fs::path resolve_library_path(const fs::path& includingFile, const std::string& name) {
    const std::string lowerName = lower_ascii(name) + ".tlib";
    const std::string exactName = name + ".tlib";
    const fs::path parent = includingFile.parent_path();
    std::vector<fs::path> candidates = {
        parent / "libraries" / lowerName,
        parent / "libraries" / exactName,
        parent / ".." / "libraries" / lowerName,
        parent / ".." / "libraries" / exactName,
        fs::current_path() / "libraries" / lowerName,
        fs::current_path() / "libraries" / exactName,
    };
    for (const fs::path& candidate : candidates) {
        std::error_code ec;
        if (fs::exists(candidate, ec)) return fs::weakly_canonical(candidate, ec);
    }
    std::ostringstream message;
    message << "cannot find library '" << name << "'. Tried:";
    for (const fs::path& candidate : candidates) message << " " << candidate.string();
    throw CompileError(message.str());
}

static std::string load_source_with_libraries(const fs::path& path, std::set<fs::path>& loadedLibraries) {
    const std::string source = read_file(path);
    std::istringstream lines(source);
    std::ostringstream mainSource;
    std::ostringstream librarySource;
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> words = split_source_words(line);
        if (is_library_directive(words)) {
            fs::path libraryPath = resolve_library_path(path, words[2]);
            if (loadedLibraries.insert(libraryPath).second) {
                librarySource << "\n" << load_source_with_libraries(libraryPath, loadedLibraries);
            }
            mainSource << "\n";
            continue;
        }
        mainSource << line << "\n";
    }
    return mainSource.str() + librarySource.str();
}

static std::string load_source_with_libraries(const fs::path& path) {
    std::set<fs::path> loadedLibraries;
    return load_source_with_libraries(path, loadedLibraries);
}

static void write_file(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw CompileError("cannot write file: " + path.string());
    out << content;
}

static int run_command(const std::string& command) {
    return std::system(command.c_str());
}

static std::string quote_arg(const fs::path& path) {
    std::string value = path.string();
    std::string escaped;
    for (char ch : value) {
        if (ch == '"') escaped += "\\\"";
        else escaped.push_back(ch);
    }
    return "\"" + escaped + "\"";
}

static std::string sanitize_file_stem(std::string value) {
    std::string result;
    for (char ch : value) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_') {
            result.push_back(ch);
        }
    }
    return result.empty() ? "program" : result;
}

static fs::path make_temporary_cpp_path(const fs::path& input) {
    std::error_code ec;
    fs::path directory = fs::temp_directory_path(ec);
    if (ec) directory = fs::current_path();

    const std::string stem = sanitize_file_stem(input.stem().string());
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 100; ++attempt) {
        fs::path candidate = directory / ("tlang-" + stem + "-" + std::to_string(stamp) + "-" + std::to_string(attempt) + ".cpp");
        if (!fs::exists(candidate, ec)) return candidate;
    }
    throw CompileError("cannot create a temporary C++ output path");
}

struct TemporaryFileCleanup {
    fs::path path;
    bool enabled = false;

    ~TemporaryFileCleanup() {
        if (!enabled || path.empty()) return;
        std::error_code ec;
        fs::remove(path, ec);
    }
};

struct CliOptions {
    fs::path input;
    fs::path output = "a.exe";
    fs::path cppOutput;
    bool cppOnly = false;
    bool emitCpp = false;
    bool windowsGui = false;
};

static void print_usage() {
    std::cerr << "usage: tlang compile INPUT [--emit-cpp FILE] [--cpp-only] [--windows-gui] [-o OUTPUT]\n";
}

static CliOptions parse_cli(int argc, char** argv) {
    if (argc < 3 || std::string(argv[1]) != "compile") {
        print_usage();
        throw CompileError("invalid command");
    }
    CliOptions options;
    options.input = argv[2];
    options.output = options.input;
#ifdef _WIN32
    options.output.replace_extension(".exe");
#else
    options.output.replace_extension("");
#endif
    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o") {
            if (++i >= argc) throw CompileError("-o requires a value");
            options.output = argv[i];
        } else if (arg == "--emit-cpp") {
            if (++i >= argc) throw CompileError("--emit-cpp requires a value");
            options.cppOutput = argv[i];
            options.emitCpp = true;
        } else if (arg == "--cpp-only") {
            options.cppOnly = true;
        } else if (arg == "--windows-gui") {
            options.windowsGui = true;
        } else {
            throw CompileError("unknown option: " + arg);
        }
    }
    if (options.cppOnly && options.cppOutput.empty()) {
        options.cppOutput = options.input;
        options.cppOutput.replace_extension(".cpp");
        options.emitCpp = true;
    }
    return options;
}

int main(int argc, char** argv) {
    const TerminalStyle style = make_terminal_style();
    try {
        CliOptions options = parse_cli(argc, argv);
        const std::string source = load_source_with_libraries(options.input);
        auto tokens = lex_source(source, options.input.string());
        Parser parser(std::move(tokens), options.input.string());
        Module module = parser.parse_module();
        SemanticAnalyzer analyzer(module, options.input.string());
        analyzer.analyze();
        CppEmitter emitter(module);
        std::string cpp = emitter.emit();
        TemporaryFileCleanup temporaryCpp;
        if (options.cppOutput.empty()) {
            options.cppOutput = make_temporary_cpp_path(options.input);
            temporaryCpp.path = options.cppOutput;
            temporaryCpp.enabled = true;
        }
        write_file(options.cppOutput, cpp);
        if (!options.cppOnly) {
            std::string command = "g++ -std=c++20 -O2 " + quote_arg(options.cppOutput) + " -o " + quote_arg(options.output);
            if (module_uses_windows_api(module)) command += " -luser32 -lgdi32";
            if (options.windowsGui) command += " -mwindows";
            int code = run_command(command);
            if (code != 0) throw CompileError("C++ compiler failed with exit code " + std::to_string(code));
        }
        print_diagnostic(std::cout, style, DiagnosticKind::Success, "OK");
        if (options.emitCpp) print_diagnostic(std::cout, style, DiagnosticKind::Info, "C++: " + options.cppOutput.string());
        if (!options.cppOnly) print_diagnostic(std::cout, style, DiagnosticKind::Info, "Executable: " + options.output.string());
        return 0;
    } catch (const CompileError& err) {
        print_compile_error(style, err.what());
        return 1;
    } catch (const std::exception& err) {
        print_diagnostic(std::cerr, style, DiagnosticKind::Error, err.what());
        return 1;
    }
}
