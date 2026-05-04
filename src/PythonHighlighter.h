#ifndef PYTHONHIGHLIGHTER_H
#define PYTHONHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QTextCharFormat>
#include <QVector>

class PythonHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit PythonHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    QVector<Rule> m_rules;

    QTextCharFormat m_multiLineStringFormat;
    QRegularExpression m_tripleDoubleStart;
    QRegularExpression m_tripleDoubleEnd;
    QRegularExpression m_tripleSingleStart;
    QRegularExpression m_tripleSingleEnd;
};

#endif
