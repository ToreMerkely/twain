#include "YamlHighlighter.h"

YamlHighlighter::YamlHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    // Strings (green) — both quoting styles
    QTextCharFormat stringFormat;
    stringFormat.setForeground(QColor(0x6A, 0x87, 0x59));
    m_rules.append({QRegularExpression("\"(?:[^\"\\\\]|\\\\.)*\""), stringFormat});
    m_rules.append({QRegularExpression("'[^']*'"), stringFormat});

    // Numbers (blue)
    QTextCharFormat numberFormat;
    numberFormat.setForeground(QColor(0x68, 0x97, 0xBB));
    m_rules.append({
        QRegularExpression("\\b-?\\d+(?:\\.\\d+)?(?:[eE][+-]?\\d+)?\\b"),
        numberFormat
    });

    // Booleans / null (orange bold)
    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(QColor(0xCC, 0x78, 0x32));
    keywordFormat.setFontWeight(QFont::Bold);
    m_rules.append({
        QRegularExpression(
            "\\b(?:true|false|yes|no|on|off|null"
            "|True|False|Yes|No|On|Off|Null"
            "|TRUE|FALSE|YES|NO|ON|OFF|NULL)\\b"),
        keywordFormat
    });
    // ~ as null (only when used as a value)
    m_rules.append({QRegularExpression("(?<=:\\s)~(?=\\s|$)"), keywordFormat});

    // Document markers (---, ...) at the start of a line
    m_rules.append({QRegularExpression("^---$|^\\.\\.\\.$"), keywordFormat});

    // Block scalar indicators (| >) at end of a mapping value
    m_rules.append({QRegularExpression("(?<=:\\s)[|>][-+]?\\s*$"), keywordFormat});

    // Anchors and aliases (&name, *name)
    QTextCharFormat anchorFormat;
    anchorFormat.setForeground(QColor(0x98, 0x76, 0xAA));
    m_rules.append({QRegularExpression("[&*][A-Za-z_][\\w-]*"), anchorFormat});

    // Keys (lavender): identifier-ish text followed by `:`
    QTextCharFormat keyFormat;
    keyFormat.setForeground(QColor(0x98, 0x76, 0xAA));
    m_rules.append({
        QRegularExpression("(?:(?<=^)|(?<=\\s)|(?<=-\\s))[A-Za-z_][\\w./-]*(?=\\s*:)"),
        keyFormat
    });

    // Comments (gray italic) — last so they win against earlier rules
    QTextCharFormat commentFormat;
    commentFormat.setForeground(QColor(0x80, 0x80, 0x80));
    commentFormat.setFontItalic(true);
    m_rules.append({QRegularExpression("#[^\\n]*"), commentFormat});
}

void YamlHighlighter::highlightBlock(const QString &text)
{
    for (const Rule &rule : m_rules) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            auto match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}
