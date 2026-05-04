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

    bool setFiles(const QString& leftPath, const QString& rightPath, QString* error = nullptr);
    int differenceCount() const { return m_diffRows.size(); }
    int currentDifference() const { return m_currentDiff; }

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

    void syncScroll(DiffPane* source, DiffPane* target, int value);
    void goToDiff(int index);
};
