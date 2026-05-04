#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QWidget>

class DiffPane;
class QSplitter;

class DiffView : public QWidget {
    Q_OBJECT
public:
    explicit DiffView(QWidget* parent = nullptr);

    struct Options {
        bool ignoreCase = false;
        bool ignoreWhitespace = false;
        bool ignoreBlankLines = false;
    };

    bool setFiles(const QString& leftPath, const QString& rightPath, QString* error = nullptr);
    void setOptions(Options opts);
    Options options() const { return m_options; }
    int differenceCount() const { return m_diffRows.size(); }
    int currentDifference() const { return m_currentDiff; }
    QString leftPath() const { return m_leftPath; }
    QString rightPath() const { return m_rightPath; }

    void nextDifference();
    void prevDifference();

    QByteArray saveSplitterState() const;
    void restoreSplitterState(const QByteArray& state);

signals:
    void currentDifferenceChanged(int index, int total);

private:
    QSplitter* m_splitter;
    DiffPane* m_left;
    DiffPane* m_right;
    bool m_syncing = false;

    QVector<int> m_diffRows;
    int m_currentDiff = -1;
    QString m_leftPath;
    QString m_rightPath;
    Options m_options;

    void syncScroll(DiffPane* source, DiffPane* target, int value);
    void goToDiff(int index);
};
