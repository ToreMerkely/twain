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

}  // namespace Diff
