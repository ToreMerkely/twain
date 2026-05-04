#pragma once

#include <QStringList>
#include <QVector>

namespace Diff {

enum class Op { Equal, Delete, Insert };

struct Hunk {
    Op op;
    int leftStart;
    int leftCount;
    int rightStart;
    int rightCount;
};

QVector<Hunk> compute(const QStringList& left, const QStringList& right);

struct LineSegment {
    int start;    // character offset within the line
    int length;
    bool differ;  // true = unique to this side, false = matched the other side
};

struct LineDiff {
    QVector<LineSegment> left;
    QVector<LineSegment> right;
};

LineDiff lineDiff(const QString& left, const QString& right);

}  // namespace Diff
