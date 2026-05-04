#include "JsonHighlighter.h"

JsonHighlighter::JsonHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    // Strings (green) — applied first so the key rule below overrides
    QTextCharFormat stringFormat;
    stringFormat.setForeground(QColor(0x6A, 0x87, 0x59));
    m_rules.append({
        QRegularExpression("\"(?:[^\"\\\\]|\\\\.)*\""),
        stringFormat
    });

    // Keys: quoted strings followed by a colon (lavender)
    QTextCharFormat keyFormat;
    keyFormat.setForeground(QColor(0x98, 0x76, 0xAA));
    m_rules.append({
        QRegularExpression("\"(?:[^\"\\\\]|\\\\.)*\"(?=\\s*:)"),
        keyFormat
    });

    // Numbers (blue)
    QTextCharFormat numberFormat;
    numberFormat.setForeground(QColor(0x68, 0x97, 0xBB));
    m_rules.append({
        QRegularExpression("-?\\b\\d+(\\.\\d+)?([eE][+-]?\\d+)?\\b"),
        numberFormat
    });

    // true / false / null (orange, bold)
    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(QColor(0xCC, 0x78, 0x32));
    keywordFormat.setFontWeight(QFont::Bold);
    m_rules.append({
        QRegularExpression("\\b(?:true|false|null)\\b"),
        keywordFormat
    });
}

void JsonHighlighter::highlightBlock(const QString &text)
{
    for (const Rule &rule : m_rules) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            auto match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}
