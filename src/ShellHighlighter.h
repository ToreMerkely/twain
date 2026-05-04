#ifndef SHELLHIGHLIGHTER_H
#define SHELLHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QVector>

class ShellHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit ShellHighlighter(QTextDocument *parent = nullptr);

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
