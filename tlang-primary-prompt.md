# TLang Specification v1

## 1. Overview

TLang is a compiled, word-only programming language designed as a strict, symbolic-free surface language that targets C++ as its backend.

The language is intentionally constrained:

- source code uses only letters and whitespace;
- no digits are allowed in source code literals or identifiers;
- no punctuation or operator symbols are allowed in source code;
- every reserved keyword is uppercase;
- function boundaries and statement boundaries are expressed with uppercase word markers.

TLang is not interpreted. A TLang compiler must translate source code into C++ or another equivalent compiled representation and then produce a native executable through a standard C++ toolchain.

## 2. Design Goals

The language is defined around five priorities:

1. Maximum textual regularity.
2. No symbol-based syntax.
3. Deterministic parsing with minimal ambiguity.
4. A small but useful imperative core.
5. A clean path to C++ code generation.

The grammar is intentionally word-driven so that the lexer and parser can be implemented without symbol scanning, operator precedence tables, or punctuation-heavy token classes.

## 3. Source Rules

### 3.1 Allowed characters

Source files may contain only:

- uppercase Latin letters;
- lowercase Latin letters if the compiler is configured to accept them for identifiers;
- whitespace characters used as separators.

For the normative v1 profile, all keywords and all user-defined identifiers must be written in uppercase. Any lowercase keyword or identifier is a compile-time lexical error.

### 3.2 Forbidden characters

The following are forbidden in TLang source code:

- digits;
- punctuation;
- arithmetic symbols;
- brackets;
- quotation marks;
- commas;
- colons;
- semicolons;
- arrows;
- underscores;
- any other non-letter character except whitespace.

### 3.3 Whitespace

Whitespace separates tokens. Multiple spaces are equivalent to one space. Newlines are allowed and treated as whitespace, but statement structure is not inferred from line breaks alone.

### 3.4 Case sensitivity

TLang is case-sensitive.

- Reserved words must appear in uppercase.
- Function names must appear in uppercase.
- Variable names must appear in uppercase.
- Type names must appear in uppercase.

This rule keeps the scanner unambiguous and preserves the all-caps visual style required by the language.

## 4. Lexical Categories

The lexer recognizes the following token classes:

- keywords;
- identifiers;
- type names;
- number words;
- text literal markers;
- end-line markers.

### 4.1 Keywords

The reserved words for v1 are:

- LET
- FIXED
- SET
- AS
- VALUE
- FUNCTION
- START
- END
- RETURNS
- RETURN
- PARAMETER
- IF
- THEN
- ELSE
- WHILE
- DO
- INPUT
- OUTPUT
- CALL
- NUMBER
- TEXT
- BOOL
- VOID
- TRUE
- FALSE
- ADD
- SUBTRACT
- MULTIPLY
- DIVIDE
- MODULO
- JOIN
- EQUAL
- LESS
- GREATER
- AND
- OR
- NOT
- NEGATIVE
- POINT
- ZERO
- ONE
- TWO
- THREE
- FOUR
- FIVE
- SIX
- SEVEN
- EIGHT
- NINE
- TEN
- ELEVEN
- TWELVE
- THIRTEEN
- FOURTEEN
- FIFTEEN
- SIXTEEN
- SEVENTEEN
- EIGHTEEN
- NINETEEN
- TWENTY
- THIRTY
- FORTY
- FIFTY
- SIXTY
- SEVENTY
- EIGHTY
- NINETY
- HUNDRED
- THOUSAND
- MILLION
- BILLION
- TRILLION
- MAIN

### 4.2 Identifiers

Identifiers are uppercase alphabetic words that are not reserved words.

Examples:

- COUNTER
- TOTAL
- PROCESS
- MAINLOOP
- MESSAGE

Identifiers must contain at least one letter and may not contain digits or punctuation.

### 4.3 End-line marker

Every statement ends with the two-word marker END LINE.

This marker is part of the language grammar, not a comment or formatting hint.

### 4.4 Text literal markers

Text literals are written with the markers TEXT START and TEXT END.

Inside a text literal, all words are treated as raw text content until the next TEXT END marker is encountered.

Example:

TEXT START HELLO WORLD TEXT END

## 5. Types

TLang has four built-in types.

### 5.1 NUMBER

NUMBER is the numeric type. The canonical backend mapping is signed 64-bit integer.

### 5.2 TEXT

TEXT is the string type. The canonical backend mapping is std::string or an equivalent string representation in the generated C++ code.

### 5.3 BOOL

BOOL is the boolean type. The canonical backend mapping is bool.

### 5.4 VOID

VOID is the absence-of-value type. It is used for procedures that do not return a value.

## 6. Numbers Written as Words

Numbers are written entirely as words. No digit characters are allowed in numeric literals.

### 6.1 Integer literals

Integer literals are formed from the following components:

- an optional NEGATIVE prefix;
- one or more cardinal words;
- optional scale words.

Accepted cardinal and scale words are:

- ZERO
- ONE
- TWO
- THREE
- FOUR
- FIVE
- SIX
- SEVEN
- EIGHT
- NINE
- TEN
- ELEVEN
- TWELVE
- THIRTEEN
- FOURTEEN
- FIFTEEN
- SIXTEEN
- SEVENTEEN
- EIGHTEEN
- NINETEEN
- TWENTY
- THIRTY
- FORTY
- FIFTY
- SIXTY
- SEVENTY
- EIGHTY
- NINETY
- HUNDRED
- THOUSAND
- MILLION
- BILLION
- TRILLION

### 6.2 Integer parsing rule

The compiler must parse number words into a canonical signed integer value.

Recommended evaluation algorithm:

1. Split the number phrase into tokens.
2. Detect NEGATIVE and remember the sign.
3. Read words left to right.
4. Convert units, teens, and tens into values.
5. HUNDRED multiplies the current group by one hundred.
6. THOUSAND, MILLION, BILLION, and TRILLION finalize the current group and multiply it by the corresponding scale.
7. Sum all finalized groups.
8. Apply the sign.

Examples:

- ZERO = 0
- ONE = 1
- TEN = 10
- TWENTY ONE = 21
- ONE HUNDRED = 100
- ONE HUNDRED TWENTY THREE = 123
- ONE THOUSAND = 1000
- ONE MILLION = 1000000
- NEGATIVE SEVEN = -7

### 6.3 Decimal literals

Decimal numbers are optional in v1 but recommended for later compatibility.

If implemented, the syntax is:

- integer part
- POINT
- digit words for the fractional part

Example:

- ONE POINT FIVE
- TWELVE POINT ZERO SEVEN

If decimals are not implemented by a compiler version, the compiler must reject them with a clear diagnostic.

### 6.4 Canonicalization

The compiler must normalize all number literals to a canonical numeric value during semantic analysis. The runtime does not parse number words; the compiler does.

## 7. Expressions

TLang uses prefix expressions. Prefix form avoids punctuation and removes the need for precedence rules.

### 7.1 Expression forms

An expression may be one of the following:

- a number literal;
- a text literal;
- TRUE or FALSE;
- an identifier;
- a function call;
- a prefix operator expression.

### 7.2 Prefix operators

The core prefix operators are:

- ADD expression expression
- SUBTRACT expression expression
- MULTIPLY expression expression
- DIVIDE expression expression
- MODULO expression expression
- JOIN expression expression
- EQUAL expression expression
- LESS expression expression
- GREATER expression expression
- AND expression expression
- OR expression expression
- NOT expression

### 7.3 Type rules for expressions

- ADD, SUBTRACT, MULTIPLY, DIVIDE, and MODULO operate on NUMBER and return NUMBER.
- JOIN operates on TEXT and returns TEXT.
- EQUAL returns BOOL and accepts operands of the same type.
- LESS and GREATER operate on NUMBER and return BOOL.
- AND and OR operate on BOOL and return BOOL.
- NOT operates on BOOL and returns BOOL.

### 7.4 Function calls as expressions

Function calls use the CALL keyword followed by the function name and its arguments.

The arity is determined by the function signature.

Example:

- CALL SUM ONE TWO

The parser consumes exactly the number of arguments declared for SUM.

## 8. Statements

Every statement ends with END LINE unless the statement is a block header or a block terminator that already includes its own required marker.

### 8.1 Variable declaration

Mutable variables are declared with LET.

Syntax:

LET NAME AS TYPE VALUE EXPRESSION END LINE

Example:

LET TOTAL AS NUMBER VALUE ONE HUNDRED END LINE

### 8.2 Constant declaration

Immutable values are declared with FIXED.

Syntax:

FIXED NAME AS TYPE VALUE EXPRESSION END LINE

Example:

FIXED PI AS NUMBER VALUE THREE END LINE

### 8.3 Assignment

Existing mutable variables are updated with SET.

Syntax:

SET NAME VALUE EXPRESSION END LINE

Example:

SET TOTAL VALUE ADD TOTAL ONE END LINE

### 8.4 Return statement

Functions return with RETURN.

Syntax for value-returning functions:

RETURN EXPRESSION END LINE

Syntax for VOID functions:

RETURN END LINE

### 8.5 Input statement

INPUT reads one value from standard input into an existing variable.

Syntax:

INPUT NAME END LINE

Semantics:

- if NAME is NUMBER, the runtime reads a number phrase and converts it using the same number-word grammar as the compiler;
- if NAME is TEXT, the runtime reads a full text line;
- if NAME is BOOL, the runtime accepts TRUE or FALSE.

### 8.6 Output statement

OUTPUT writes a value to standard output.

Syntax:

OUTPUT EXPRESSION END LINE

Semantics:

- NUMBER values are printed as decimal digits in the generated program;
- TEXT values are printed verbatim;
- BOOL values are printed as TRUE or FALSE.

### 8.7 If statement

Syntax:

IF EXPRESSION THEN END LINE
	statements
ELSE END LINE
	statements
END IF END LINE

The ELSE branch is optional.

If the condition is omitted or does not resolve to BOOL, compilation fails.

### 8.8 While statement

Syntax:

WHILE EXPRESSION DO END LINE
	statements
END WHILE END LINE

The loop condition must resolve to BOOL.

### 8.9 Expression statements

A CALL expression may appear as a standalone statement if its return value is intentionally ignored.

Example:

CALL LOG MESSAGE END LINE

## 9. Functions

Functions are the main unit of executable structure in TLang.

### 9.1 Function declaration

Syntax:

FUNCTION START NAME [PARAMETER NAME ...] RETURNS TYPE END LINE
	body statements
FUNCTION END NAME END LINE

Rules:

- NAME is an uppercase identifier.
- The same NAME must appear after FUNCTION END.
- Parameters are listed before RETURNS.
- Each PARAMETER introduces exactly one parameter name.
- Parameter names are uppercase identifiers.
- The function body may contain declarations, assignments, control flow, input, output, calls, and return statements.

Example:

FUNCTION START SUM PARAMETER LEFT PARAMETER RIGHT RETURNS NUMBER END LINE
RETURN ADD LEFT RIGHT END LINE
FUNCTION END SUM END LINE

### 9.2 Main function

Every program must define exactly one MAIN function.

Recommended signature:

FUNCTION START MAIN RETURNS NUMBER END LINE

The compiler maps MAIN to the entry point of the generated executable.

### 9.3 Parameters

Function parameters are local variables initialized by the call site.

Parameters are immutable inside the function body unless the compiler explicitly lowers them into mutable locals, which is allowed only if semantics remain equivalent.

### 9.4 Return type checking

The compiler must verify that every control-flow path in a non-VOID function returns a value of the declared return type.

## 10. Top-Level Structure

A TLang source file is a module.

Allowed top-level items:

- FIXED declarations;
- LET declarations;
- function definitions.

Executable statements are not allowed at top level.

Top-level declarations are compiled as module-scope C++ declarations or constants.

## 11. Compilation Model

TLang is compiled in phases.

### 11.1 Lexical analysis

The lexer must:

- reject forbidden characters;
- split input into word tokens;
- recognize keywords, identifiers, and markers;
- detect malformed text literal boundaries;
- report the exact location of lexical errors.

### 11.2 Parsing

The parser builds an abstract syntax tree with at least the following node types:

- module;
- function definition;
- variable declaration;
- constant declaration;
- assignment;
- return statement;
- input statement;
- output statement;
- if statement;
- while statement;
- call expression;
- operator expression;
- literal expression;
- identifier expression.

### 11.3 Semantic analysis

The semantic phase must verify:

- identifiers are declared before use;
- identifiers are not redeclared in the same scope;
- function names are unique;
- function call arity matches the declaration;
- expressions have compatible types;
- constants are not reassigned;
- return statements match the enclosing function type;
- MAIN exists exactly once.

### 11.4 Intermediate representation

The compiler may lower the AST into a simple typed IR before C++ emission. The IR is optional, but recommended if the compiler needs optimization, constant folding, or clearer diagnostics.

### 11.5 C++ backend

The backend should emit a C++20 translation unit or equivalent modern C++ source.

Recommended backend mappings:

- NUMBER -> std::int64_t
- TEXT -> std::string
- BOOL -> bool
- VOID -> void

The generated C++ may use a small runtime helper library for:

- number-word parsing for INPUT;
- text concatenation support;
- output formatting;
- safe string handling.

### 11.6 Build pipeline

Recommended build pipeline:

1. Parse TLang source.
2. Type-check the program.
3. Emit C++ source.
4. Invoke a standard C++ compiler.
5. Produce the native executable.

The language remains compiled even if the backend first emits C++ as an intermediate artifact.

## 12. Error Model

The compiler must produce actionable diagnostics.

Required error classes:

- lexical error;
- unknown keyword error;
- invalid identifier error;
- invalid number phrase error;
- undeclared identifier error;
- duplicate declaration error;
- type mismatch error;
- wrong function arity error;
- missing MAIN error;
- missing END LINE error;
- unclosed block error;
- missing RETURN error;
- immutable assignment error.

Each diagnostic should include:

- file name;
- line number;
- token position if available;
- a short human-readable explanation;
- the affected identifier or keyword if relevant.

## 13. Formal Grammar

The following grammar describes the v1 surface syntax.

```ebnf
module            ::= { top_level_item }

top_level_item    ::= declaration END_LINE
										| function_definition

declaration       ::= let_declaration
										| fixed_declaration

let_declaration   ::= LET IDENTIFIER AS type VALUE expression
fixed_declaration ::= FIXED IDENTIFIER AS type VALUE expression

function_definition ::= FUNCTION START IDENTIFIER { PARAMETER IDENTIFIER } RETURNS type END_LINE
												{ statement }
												FUNCTION END IDENTIFIER END_LINE

statement         ::= declaration END_LINE
										| assignment END_LINE
										| return_statement END_LINE
										| input_statement END_LINE
										| output_statement END_LINE
										| if_statement
										| while_statement
										| expression_statement END_LINE

assignment        ::= SET IDENTIFIER VALUE expression
return_statement  ::= RETURN [ expression ]
input_statement   ::= INPUT IDENTIFIER
output_statement  ::= OUTPUT expression
expression_statement ::= expression

if_statement      ::= IF expression THEN END_LINE
											{ statement }
											[ ELSE END_LINE { statement } ]
											END IF END_LINE

while_statement   ::= WHILE expression DO END_LINE
											{ statement }
											END WHILE END_LINE

expression        ::= literal
										| IDENTIFIER
										| call_expression
										| unary_expression
										| binary_expression

call_expression   ::= CALL IDENTIFIER { expression }

unary_expression  ::= NOT expression

binary_expression ::= ADD expression expression
										| SUBTRACT expression expression
										| MULTIPLY expression expression
										| DIVIDE expression expression
										| MODULO expression expression
										| JOIN expression expression
										| EQUAL expression expression
										| LESS expression expression
										| GREATER expression expression
										| AND expression expression
										| OR expression expression

literal           ::= number_literal
										| text_literal
										| TRUE
										| FALSE

number_literal    ::= [ NEGATIVE ] number_word_sequence

text_literal      ::= TEXT START { text_word } TEXT END

type              ::= NUMBER | TEXT | BOOL | VOID
```

Notes on the grammar:

- END_LINE is the token pair END LINE.
- FUNCTION START and FUNCTION END are token pairs that define block boundaries.
- The parser uses function signatures to determine the number of arguments consumed by CALL.
- A text literal consumes all words until TEXT END.

## 14. Standard Library Baseline

TLang v1 can be implemented without a large standard library, but the following runtime helpers are recommended:

- PRINT_TEXT
- PRINT_NUMBER
- PARSE_NUMBER_WORDS
- READ_TEXT_LINE
- READ_NUMBER_WORDS
- READ_BOOL_WORD

These helpers may exist only in the generated C++ runtime layer and do not need to be exposed as source-level keywords.

## 15. Example Program

The following example satisfies the minimum practical feature set:

```tlang
FUNCTION START SUM PARAMETER LEFT PARAMETER RIGHT RETURNS NUMBER END LINE
RETURN ADD LEFT RIGHT END LINE
FUNCTION END SUM END LINE

FUNCTION START MAIN RETURNS NUMBER END LINE
LET COUNT AS NUMBER VALUE ONE MILLION END LINE
LET MESSAGE AS TEXT VALUE TEXT START HELLO WORLD TEXT END END LINE
LET TOTAL AS NUMBER VALUE CALL SUM COUNT ONE END LINE

IF GREATER TOTAL ONE THOUSAND THEN END LINE
OUTPUT MESSAGE END LINE
ELSE END LINE
OUTPUT TEXT START SMALL NUMBER TEXT END END LINE
END IF END LINE

RETURN TOTAL END LINE
FUNCTION END MAIN END LINE
```

What this program demonstrates:

- variable declaration;
- number written as words;
- text literal;
- function definition;
- function call;
- condition;
- return value.

## 16. Limitations and Tradeoffs

TLang v1 deliberately accepts several constraints.

### 16.1 No punctuation syntax

The language has no punctuation-based operators or delimiters. This makes the surface syntax unusual but mechanically simple.

### 16.2 Wordy expressions

Prefix expressions are verbose compared with symbol-based languages. The tradeoff is a simpler parser and fewer precedence bugs.

### 16.3 Strong uppercase style

Requiring uppercase identifiers reduces ambiguity and enforces a single visual style, but it makes the language less flexible.

### 16.4 Limited literal forms

Numbers are word-based and text literals are marker-based. This keeps the character set small but requires a dedicated compiler pass for numeric normalization.

### 16.5 No implicit type coercion

The language should avoid silent coercions. This simplifies semantic analysis and makes generated C++ safer.

## 17. Implementation Checklist

A conforming v1 compiler should at minimum:

1. reject non-letter source characters except whitespace;
2. tokenize uppercase words;
3. parse declarations, functions, and blocks;
4. parse word-based numbers;
5. enforce types;
6. emit C++ backend code;
7. compile to a native executable;
8. provide clear diagnostics for malformed programs;
9. support the example program above;
10. require a valid MAIN function.

## 18. Summary

TLang v1 is a fully specified, compile-only, word-only language with a narrow imperative core and a C++ backend.

Its defining properties are:

- uppercase words only;
- no punctuation in source;
- statement boundaries through END LINE;
- function boundaries through FUNCTION START and FUNCTION END;
- numbers written as English words;
- a typed prefix-expression core;
- deterministic lowering to C++.