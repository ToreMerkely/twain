#ifndef YAMLHIGHLIGHTER_H
#define YAMLHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QVector>

class YamlHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit YamlHighlighter(QTextDocument *parent = nullptr);

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
