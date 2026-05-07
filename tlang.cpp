#include <algorithm>
#include <cctype>
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
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

struct CompileError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

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
    "CALL", "NUMBER", "TEXT", "BOOL", "VOID", "TRUE", "FALSE", "ADD", "SUBTRACT",
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
    return word == "NEGATIVE" || kSmallNumbers.contains(word) || kTens.contains(word);
}

static bool is_identifier_word(const std::string& word, bool allowMain = false) {
    if (word.empty()) return false;
    if (allowMain && word == "MAIN") return true;
    if (kKeywords.contains(word)) return false;
    return std::all_of(word.begin(), word.end(), [](char ch) {
        return ch >= 'A' && ch <= 'Z';
    });
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
        if (ch >= 'a' && ch <= 'z') {
            Token token{std::string(1, ch), line, col};
            throw CompileError(location(file, token) + ": lowercase letters are not allowed in normative TLang");
        }
        if (ch < 'A' || ch > 'Z') {
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
    Type returnType = Type::Unknown;
};

class Parser {
public:
    Parser(std::vector<Token> tokens, std::string file)
        : tokens_(std::move(tokens)), file_(std::move(file)) {
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
            if (signatures_.contains(name)) {
                fail(start, "duplicate function '" + name + "'");
            }
            signatures_.insert({name, Signature{start, name, arity, returnType}});
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
        return ops.contains(word);
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
            if (!at_end() && kScales.contains(peek().text)) {
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
        if (kSmallNumbers.contains(peek().text)) {
            value = kSmallNumbers.at(peek().text);
            consumed = true;
            ++pos_;
        } else if (kTens.contains(peek().text)) {
            value = kTens.at(peek().text);
            consumed = true;
            ++pos_;
            if (!at_end() && kSmallNumbers.contains(peek().text) &&
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
            if (functions_.contains(fn.name)) fail(fn.token, "duplicate function '" + fn.name + "'");
            functions_[fn.name] = FunctionInfo{&fn};
        }
    }

    void register_globals() {
        for (const Declaration& decl : module_.globals) {
            if (globals_.contains(decl.name)) fail(decl.token, "duplicate global '" + decl.name + "'");
            if (functions_.contains(decl.name)) fail(decl.token, "global '" + decl.name + "' conflicts with function name");
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
            if (env.contains(fn.params[i])) fail(fn.token, "parameter '" + fn.params[i] + "' conflicts with a global");
            env[fn.params[i]] = Symbol{fn.paramTypes[i], false, true, static_cast<int>(i), fn.token};
        }
        analyze_block(fn, fn.body, env, changed, finalPass);
    }

    void analyze_block(Function& fn, const std::vector<std::unique_ptr<Stmt>>& body,
                       std::unordered_map<std::string, Symbol> env, bool& changed, bool finalPass) {
        for (const auto& stmt : body) {
            switch (stmt->kind) {
                case StmtKind::Decl: {
                    if (env.contains(stmt->name)) fail(stmt->token, "duplicate declaration '" + stmt->name + "'");
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
                if (found == functions_.end()) fail(expr.token, "unknown function '" + expr.name + "'");
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

class CppEmitter {
public:
    explicit CppEmitter(const Module& module) : module_(module) {}

    std::string emit() {
        out_ << "#include <cstdint>\n";
        out_ << "#include <iostream>\n";
        out_ << "#include <sstream>\n";
        out_ << "#include <stdexcept>\n";
        out_ << "#include <string>\n";
        out_ << "#include <unordered_map>\n";
        out_ << "#include <vector>\n\n";
        emit_runtime();
        emit_prototypes();
        for (const auto& decl : module_.globals) emit_declaration(decl, 0, true);
        if (!module_.globals.empty()) out_ << "\n";
        for (const auto& fn : module_.functions) emit_function(fn);
        return out_.str();
    }

private:
    const Module& module_;
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
        out_ << "        if (small.contains(words[i])) { group = small.at(words[i]); ++i; }\n";
        out_ << "        else if (tens.contains(words[i])) { group = tens.at(words[i]); ++i; if (i < words.size() && small.contains(words[i]) && small.at(words[i]) > 0 && small.at(words[i]) < 10) { group += small.at(words[i]); ++i; } }\n";
        out_ << "        else throw std::runtime_error(\"invalid number input\");\n";
        out_ << "        any = true;\n";
        out_ << "        if (i < words.size() && words[i] == \"HUNDRED\") { group *= 100; ++i; }\n";
        out_ << "        if (i < words.size() && scales.contains(words[i])) { total += group * scales.at(words[i]); ++i; continue; }\n";
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
        out_ << "}\n\n";
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
                std::string code = cpp_name(expr.name, true) + "(";
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

struct CliOptions {
    fs::path input;
    fs::path output = "a.exe";
    fs::path cppOutput;
    bool cppOnly = false;
};

static void print_usage() {
    std::cerr << "usage: tlang compile INPUT [--emit-cpp FILE] [--cpp-only] [-o OUTPUT]\n";
}

static CliOptions parse_cli(int argc, char** argv) {
    if (argc < 3 || std::string(argv[1]) != "compile") {
        print_usage();
        throw CompileError("invalid command");
    }
    CliOptions options;
    options.input = argv[2];
    options.cppOutput = options.input;
    options.cppOutput.replace_extension(".cpp");
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
        } else if (arg == "--cpp-only") {
            options.cppOnly = true;
        } else {
            throw CompileError("unknown option: " + arg);
        }
    }
    return options;
}

int main(int argc, char** argv) {
    try {
        CliOptions options = parse_cli(argc, argv);
        const std::string source = read_file(options.input);
        auto tokens = lex_source(source, options.input.string());
        Parser parser(std::move(tokens), options.input.string());
        Module module = parser.parse_module();
        SemanticAnalyzer analyzer(module, options.input.string());
        analyzer.analyze();
        CppEmitter emitter(module);
        std::string cpp = emitter.emit();
        write_file(options.cppOutput, cpp);
        if (!options.cppOnly) {
            std::string command = "g++ -std=c++20 -O2 " + quote_arg(options.cppOutput) + " -o " + quote_arg(options.output);
            int code = run_command(command);
            if (code != 0) throw CompileError("C++ compiler failed");
        }
        std::cout << "OK\n";
        std::cout << "C++: " << options.cppOutput.string() << "\n";
        if (!options.cppOnly) std::cout << "Executable: " << options.output.string() << "\n";
        return 0;
    } catch (const CompileError& err) {
        std::cerr << "error: " << err.what() << "\n";
        return 1;
    } catch (const std::exception& err) {
        std::cerr << "error: " << err.what() << "\n";
        return 1;
    }
}
