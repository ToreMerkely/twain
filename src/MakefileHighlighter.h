#ifndef MAKEFILEHIGHLIGHTER_H
#define MAKEFILEHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QVector>

class MakefileHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit MakefileHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QVector<Rule> m_rules;
};

#endif
