#include "MarkdownHighlighter.h"

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
    , m_codeFenceStart("^\\s*(```|~~~)")
{
    // Headings: # to ######, bold blue
    QTextCharFormat headingFormat;
    headingFormat.setForeground(QColor(0x56, 0xA8, 0xF5));
    headingFormat.setFontWeight(QFont::Bold);
    m_rules.append({QRegularExpression("^#{1,6}\\s+.*$"), headingFormat, 0});

    // Bold: **text** or __text__
    QTextCharFormat boldFormat;
    boldFormat.setFontWeight(QFont::Bold);
    m_rules.append({QRegularExpression("\\*\\*[^*\\n]+\\*\\*"), boldFormat, 0});
    m_rules.append({QRegularExpression("__[^_\\n]+__"), boldFormat, 0});

    // Italic: *text* or _text_ (don't catch ** or __)
    QTextCharFormat italicFormat;
    italicFormat.setFontItalic(true);
    m_rules.append({QRegularExpression("(?<!\\*)\\*(?!\\*)[^*\\n]+(?<!\\*)\\*(?!\\*)"), italicFormat, 0});
    m_rules.append({QRegularExpression("(?<!_)_(?!_)[^_\\n]+(?<!_)_(?!_)"), italicFormat, 0});

    // Inline code: `code`
    QTextCharFormat inlineCodeFormat;
    inlineCodeFormat.setForeground(QColor(0x98, 0x76, 0xAA));
    inlineCodeFormat.setFontFamilies({"JetBrains Mono", "monospace"});
    m_rules.append({QRegularExpression("`[^`\\n]+`"), inlineCodeFormat, 0});

    // Links: [text](url) — color text blue, url green
    QTextCharFormat linkTextFormat;
    linkTextFormat.setForeground(QColor(0x56, 0xA8, 0xF5));
    m_rules.append({QRegularExpression("\\[([^\\]\\n]+)\\]\\([^)\\n]+\\)"), linkTextFormat, 1});

    QTextCharFormat linkUrlFormat;
    linkUrlFormat.setForeground(QColor(0x6A, 0x87, 0x59));
    m_rules.append({QRegularExpression("\\[[^\\]\\n]+\\]\\(([^)\\n]+)\\)"), linkUrlFormat, 1});

    // List markers at start of line: -, *, +, or N.
    QTextCharFormat listMarkerFormat;
    listMarkerFormat.setForeground(QColor(0xCC, 0x78, 0x32));
    listMarkerFormat.setFontWeight(QFont::Bold);
    m_rules.append({QRegularExpression("^\\s*([-*+])\\s"), listMarkerFormat, 1});
    m_rules.append({QRegularExpression("^\\s*(\\d+\\.)\\s"), listMarkerFormat, 1});

    // Blockquote: lines starting with >
    QTextCharFormat blockquoteFormat;
    blockquoteFormat.setForeground(QColor(0x88, 0x88, 0x88));
    blockquoteFormat.setFontItalic(true);
    m_rules.append({QRegularExpression("^>\\s?.*$"), blockquoteFormat, 0});

    // Horizontal rule: ---, ***, ___
    QTextCharFormat hrFormat;
    hrFormat.setForeground(QColor(0x88, 0x88, 0x88));
    hrFormat.setFontWeight(QFont::Bold);
    m_rules.append({QRegularExpression("^\\s*([-*_])\\s*\\1\\s*\\1[\\s\\1]*$"), hrFormat, 0});

    // Format used for fenced code blocks (multi-line, handled in highlightBlock)
    m_codeBlockFormat.setForeground(QColor(0x6A, 0x87, 0x59));
    m_codeBlockFormat.setFontFamilies({"JetBrains Mono", "monospace"});
}

void MarkdownHighlighter::highlightBlock(const QString &text)
{
    // State: 0 = normal, 1 = inside ``` fenced code, 2 = inside ~~~ fenced code
    int prev = previousBlockState();
    if (prev < 0) prev = 0;
    int state = prev;

    if (state != 0) {
        // We were inside a code block — paint the whole line first
        setFormat(0, text.length(), m_codeBlockFormat);
        // Check if this line closes the fence
        QString fence = (state == 1) ? "```" : "~~~";
        if (text.trimmed().startsWith(fence))
            state = 0;
        setCurrentBlockState(state);
        return;
    }

    // Normal line: check if a fenced code block starts here
    auto fenceMatch = m_codeFenceStart.match(text);
    if (fenceMatch.hasMatch()) {
        setFormat(0, text.length(), m_codeBlockFormat);
        QString fence = fenceMatch.captured(1);
        setCurrentBlockState(fence == "```" ? 1 : 2);
        return;
    }

    // Apply single-line rules
    for (const Rule &rule : m_rules) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            auto m = it.next();
            int start = m.capturedStart(rule.captureGroup);
            int len   = m.capturedLength(rule.captureGroup);
            if (len > 0)
                setFormat(start, len, rule.format);
        }
    }

    setCurrentBlockState(0);
}
