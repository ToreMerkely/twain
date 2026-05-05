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

struct Options {
    bool ignoreCase = false;
    bool ignoreWhitespace = false;  // collapses runs and strips leading/trailing
};

QVector<Hunk> compute(const QStringList& left, const QStringList& right, Options opts = {});

struct LineSegment {
    int start;    // character offset within the line
    int length;
    bool differ;  // true = unique to this side, false = matched the other side
};

struct LineDiff {
    QVector<LineSegment> left;
    QVector<LineSegment> right;
};

LineDiff lineDiff(const QString& left, const QString& right, Options opts = {});

// 0..1 word-Jaccard similarity (used for smart alignment within change blocks).
double similarity(const QString& a, const QString& b);

struct AlignmentPair {
    int leftIdx;   // -1 if unpaired
    int rightIdx;  // -1 if unpaired
};

// Order-preserving best alignment of two short line lists. Pairs with
// similarity below a threshold are dropped so unrelated lines stay
// unpaired.
QVector<AlignmentPair> alignBlock(const QStringList& left, const QStringList& right);

}  // namespace Diff
