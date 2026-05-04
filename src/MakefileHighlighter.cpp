#include "MakefileHighlighter.h"

MakefileHighlighter::MakefileHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    // Variable references: $(VAR), ${VAR}, $X, $@, $<, $^, $*, $?, $%, $+, $|
    QTextCharFormat varFormat;
    varFormat.setForeground(QColor(0x98, 0x76, 0xAA));
    m_rules.append({
        QRegularExpression("\\$(?:\\([^)]*\\)|\\{[^}]*\\}|[A-Za-z_]|[@<^*?%+|])"),
        varFormat
    });

    // Targets at start of line: name(s) followed by `:` (but not `:=` assignment)
    QTextCharFormat targetFormat;
    targetFormat.setForeground(QColor(0x98, 0x76, 0xAA));
    m_rules.append({
        QRegularExpression("^[A-Za-z0-9_./%$()-]+(?=\\s*:(?!=))"),
        targetFormat
    });

    // Variable assignments: VAR = / := / ?= / +=
    m_rules.append({
        QRegularExpression("^[A-Za-z_][\\w]*(?=\\s*[:?+]?=)"),
        targetFormat
    });

    // Built-in function names inside $( ... )
    QTextCharFormat funcFormat;
    funcFormat.setForeground(QColor(0xFF, 0xC6, 0x6D));
    m_rules.append({
        QRegularExpression(
            "(?<=\\$\\()(?:wildcard|shell|patsubst|subst|filter|filter-out"
            "|findstring|sort|word|words|wordlist|firstword|lastword|dir"
            "|notdir|suffix|basename|addsuffix|addprefix|join|realpath"
            "|abspath|if|or|and|foreach|call|eval|origin|flavor|error"
            "|warning|info|value|strip)\\b"),
        funcFormat
    });

    // Directives at start of line (orange bold)
    QTextCharFormat directiveFormat;
    directiveFormat.setForeground(QColor(0xCC, 0x78, 0x32));
    directiveFormat.setFontWeight(QFont::Bold);
    m_rules.append({
        QRegularExpression(
            "^\\s*(?:include|-include|sinclude|ifeq|ifneq|ifdef|ifndef"
            "|else|endif|define|endef|export|unexport|override|vpath"
            "|undefine)\\b"),
        directiveFormat
    });

    // Leading tabs — make them visible with a subtle blue-gray background
    QTextCharFormat tabFormat;
    tabFormat.setBackground(QColor(0x2A, 0x33, 0x40));
    m_rules.append({QRegularExpression("^\\t+"), tabFormat});

    // Comments (gray italic) — last so they win
    QTextCharFormat commentFormat;
    commentFormat.setForeground(QColor(0x80, 0x80, 0x80));
    commentFormat.setFontItalic(true);
    m_rules.append({QRegularExpression("#[^\\n]*"), commentFormat});
}

void MakefileHighlighter::highlightBlock(const QString &text)
{
    for (const Rule &rule : m_rules) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            auto match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}
