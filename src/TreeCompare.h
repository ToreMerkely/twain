#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

namespace TreeCompare {

enum class Status {
    Same,
    Different,
    LeftOnly,
    RightOnly,
};

struct Entry {
    QString name;
    bool isDir = false;
    Status status = Status::Same;

    qint64 leftSize = -1;
    qint64 rightSize = -1;
    QDateTime leftMTime;
    QDateTime rightMTime;
    QString leftPath;
    QString rightPath;

    QVector<Entry> children;
    Entry* parent = nullptr;  // populated after the tree is built (TreeCompareModel does this)
};

Entry compare(const QString& leftRoot, const QString& rightRoot);

}  // namespace TreeCompare
