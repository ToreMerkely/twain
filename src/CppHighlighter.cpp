#include "CppHighlighter.h"

CppHighlighter::CppHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    // Keywords (orange)
    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(QColor(0xCC, 0x78, 0x32));
    keywordFormat.setFontWeight(QFont::Bold);

    const QString keywords[] = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto",
        "bitand", "bitor", "bool", "break", "case", "catch",
        "char", "char8_t", "char16_t", "char32_t", "class", "compl",
        "concept", "const", "consteval", "constexpr", "constinit",
        "const_cast", "continue", "co_await", "co_return", "co_yield",
        "decltype", "default", "delete", "do", "double",
        "dynamic_cast", "else", "enum", "explicit", "export",
        "extern", "false", "float", "for", "friend", "goto",
        "if", "inline", "int", "long", "mutable", "namespace",
        "new", "noexcept", "not", "not_eq", "nullptr", "operator",
        "or", "or_eq", "private", "protected", "public", "register",
        "reinterpret_cast", "requires", "return", "short", "signed",
        "sizeof", "static", "static_assert", "static_cast", "struct",
        "switch", "template", "thread_local", "throw",
        "true", "try", "typedef", "typeid", "typename", "union",
        "unsigned", "using", "virtual", "void", "volatile",
        "wchar_t", "while", "xor", "xor_eq", "override", "final"
    };

    for (const QString &kw : keywords) {
        m_rules.append({
            QRegularExpression("\\b" + kw + "\\b"),
            keywordFormat
        });
    }

    // "this" keyword (purple, italic)
    QTextCharFormat thisFormat;
    thisFormat.setForeground(QColor(0x98, 0x76, 0xAA));
    thisFormat.setFontItalic(true);
    m_rules.append({
        QRegularExpression("\\bthis\\b"),
        thisFormat
    });

    // Preprocessor directives (olive/green)
    QTextCharFormat preprocessorFormat;
    preprocessorFormat.setForeground(QColor(0xBB, 0xB5, 0x29));
    m_rules.append({
        QRegularExpression("^\\s*#\\s*\\w+"),
        preprocessorFormat
    });

    // Preprocessor macros / all-caps constants (purple)
    QTextCharFormat macroFormat;
    macroFormat.setForeground(QColor(0x98, 0x76, 0xAA));
    m_rules.append({
        QRegularExpression("\\b[A-Z][A-Z0-9_]{2,}\\b"),
        macroFormat
    });

    // Types — uppercase-starting identifiers (lavender/purple)
    QTextCharFormat typeFormat;
    typeFormat.setForeground(QColor(0xB3, 0x89, 0xC5));
    m_rules.append({
        QRegularExpression("\\b[A-Z][A-Za-z0-9_]*[a-z][A-Za-z0-9_]*\\b"),
        typeFormat
    });

    // Strings (green)
    QTextCharFormat stringFormat;
    stringFormat.setForeground(QColor(0x6A, 0x87, 0x59));
    m_rules.append({
        QRegularExpression("\"(?:[^\"\\\\]|\\\\.)*\""),
        stringFormat
    });

    // Characters (green)
    m_rules.append({
        QRegularExpression("'(?:[^'\\\\]|\\\\.)*'"),
        stringFormat
    });

    // Include paths (green)
    m_rules.append({
        QRegularExpression("<[A-Za-z0-9_/\\.]+>"),
        stringFormat
    });

    // Numbers (blue)
    QTextCharFormat numberFormat;
    numberFormat.setForeground(QColor(0x68, 0x97, 0xBB));
    m_rules.append({
        QRegularExpression("\\b[0-9]+(\\.[0-9]+)?([eE][+-]?[0-9]+)?[fFlLuU]*\\b"),
        numberFormat
    });
    m_rules.append({
        QRegularExpression("\\b0[xX][0-9a-fA-F]+[uUlL]*\\b"),
        numberFormat
    });

    // Function calls/declarations (yellow)
    QTextCharFormat functionFormat;
    functionFormat.setForeground(QColor(0xFF, 0xC6, 0x6D));
    m_rules.append({
        QRegularExpression("\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()"),
        functionFormat
    });

    // Member access (after . or ->)
    QTextCharFormat memberFormat;
    memberFormat.setForeground(QColor(0x98, 0x76, 0xAA));
    m_rules.append({
        QRegularExpression("(?<=\\.|->)[A-Za-z_][A-Za-z0-9_]*"),
        memberFormat
    });

    // Single-line comments (grey, italic)
    QTextCharFormat commentFormat;
    commentFormat.setForeground(QColor(0x80, 0x80, 0x80));
    commentFormat.setFontItalic(true);
    m_rules.append({
        QRegularExpression("//[^\n]*"),
        commentFormat
    });

    // Multi-line comments
    m_multiLineCommentFormat = commentFormat;
    m_commentStartExpr = QRegularExpression("/\\*");
    m_commentEndExpr = QRegularExpression("\\*/");
}

void CppHighlighter::highlightBlock(const QString &text)
{
    // Apply single-line rules
    for (const Rule &rule : m_rules) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            auto match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    // Multi-line comment handling
    setCurrentBlockState(0);

    int startIndex = 0;
    if (previousBlockState() != 1)
        startIndex = text.indexOf(m_commentStartExpr);

    while (startIndex >= 0) {
        auto endMatch = m_commentEndExpr.match(text, startIndex);
        int endIndex = endMatch.capturedStart();
        int commentLength;

        if (endIndex == -1 || !endMatch.hasMatch()) {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        } else {
            commentLength = endIndex - startIndex + endMatch.capturedLength();
        }

        setFormat(startIndex, commentLength, m_multiLineCommentFormat);
        startIndex = text.indexOf(m_commentStartExpr, startIndex + commentLength);
    }
}
