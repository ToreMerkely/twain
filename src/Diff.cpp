#include "Diff.h"

namespace Diff {

namespace {

struct Edit {
    Op op;
    int leftIndex;
    int rightIndex;
};

QVector<Edit> myersEditScript(const QStringList& a, const QStringList& b) {
    const int n = a.size();
    const int m = b.size();
    const int max = n + m;

    if (max == 0) return {};

    const int offset = max;
    QVector<int> v(2 * max + 1, 0);
    QVector<QVector<int>> trace;
    trace.reserve(max + 1);

    int reachedD = -1;
    for (int d = 0; d <= max; ++d) {
        trace.append(v);
        for (int k = -d; k <= d; k += 2) {
            int x;
            if (k == -d || (k != d && v[k - 1 + offset] < v[k + 1 + offset])) {
                x = v[k + 1 + offset];  // insertion (down)
            } else {
                x = v[k - 1 + offset] + 1;  // deletion (right)
            }
            int y = x - k;
            while (x < n && y < m && a[x] == b[y]) {
                ++x;
                ++y;
            }
            v[k + offset] = x;
            if (x >= n && y >= m) {
                reachedD = d;
                break;
            }
        }
        if (reachedD >= 0) break;
    }

    QVector<Edit> edits;
    int x = n, y = m;
    for (int d = reachedD; d > 0; --d) {
        const QVector<int>& vPrev = trace[d];
        int k = x - y;
        int prevK;
        if (k == -d || (k != d && vPrev[k - 1 + offset] < vPrev[k + 1 + offset])) {
            prevK = k + 1;  // came from insertion
        } else {
            prevK = k - 1;  // came from deletion
        }
        int prevX = vPrev[prevK + offset];
        int prevY = prevX - prevK;

        while (x > prevX && y > prevY) {
            edits.append({Op::Equal, x - 1, y - 1});
            --x;
            --y;
        }
        if (d > 0) {
            if (x == prevX) {
                edits.append({Op::Insert, -1, y - 1});
            } else {
                edits.append({Op::Delete, x - 1, -1});
            }
        }
        x = prevX;
        y = prevY;
    }
    while (x > 0 && y > 0) {
        edits.append({Op::Equal, x - 1, y - 1});
        --x;
        --y;
    }

    std::reverse(edits.begin(), edits.end());
    return edits;
}

}  // namespace

QVector<Hunk> compute(const QStringList& left, const QStringList& right) {
    QVector<Edit> edits = myersEditScript(left, right);

    QVector<Hunk> hunks;
    int i = 0;
    while (i < edits.size()) {
        Op op = edits[i].op;
        int j = i;
        while (j < edits.size() && edits[j].op == op) ++j;

        Hunk h{op, 0, 0, 0, 0};
        if (op == Op::Equal) {
            h.leftStart = edits[i].leftIndex;
            h.rightStart = edits[i].rightIndex;
            h.leftCount = j - i;
            h.rightCount = j - i;
        } else if (op == Op::Delete) {
            h.leftStart = edits[i].leftIndex;
            h.leftCount = j - i;
            h.rightStart = (i > 0) ? edits[i - 1].rightIndex + 1 : 0;
            h.rightCount = 0;
        } else {  // Insert
            h.rightStart = edits[i].rightIndex;
            h.rightCount = j - i;
            h.leftStart = (i > 0) ? edits[i - 1].leftIndex + 1 : 0;
            h.leftCount = 0;
        }
        hunks.append(h);
        i = j;
    }
    return hunks;
}

namespace {

struct Token {
    QString text;
    int start;
    int length;
};

QVector<Token> tokenize(const QString& line) {
    QVector<Token> result;
    const int n = line.size();
    int i = 0;
    while (i < n) {
        const QChar c = line[i];
        const bool isWord = c.isLetterOrNumber() || c == QLatin1Char('_');
        if (isWord) {
            int j = i + 1;
            while (j < n) {
                const QChar c2 = line[j];
                if (!c2.isLetterOrNumber() && c2 != QLatin1Char('_')) break;
                ++j;
            }
            result.append({line.mid(i, j - i), i, j - i});
            i = j;
        } else {
            result.append({line.mid(i, 1), i, 1});
            ++i;
        }
    }
    return result;
}

}  // namespace

LineDiff lineDiff(const QString& left, const QString& right) {
    const auto tl = tokenize(left);
    const auto tr = tokenize(right);

    QStringList ls, rs;
    ls.reserve(tl.size());
    rs.reserve(tr.size());
    for (const auto& t : tl) ls.append(t.text);
    for (const auto& t : tr) rs.append(t.text);

    const auto hunks = compute(ls, rs);

    LineDiff out;
    for (const auto& h : hunks) {
        if (h.op == Op::Equal) {
            const int ls_ = tl[h.leftStart].start;
            const int le_ = tl[h.leftStart + h.leftCount - 1].start +
                            tl[h.leftStart + h.leftCount - 1].length;
            out.left.append({ls_, le_ - ls_, false});
            const int rs2 = tr[h.rightStart].start;
            const int re2 = tr[h.rightStart + h.rightCount - 1].start +
                            tr[h.rightStart + h.rightCount - 1].length;
            out.right.append({rs2, re2 - rs2, false});
        } else if (h.op == Op::Delete) {
            if (h.leftCount > 0) {
                const int s = tl[h.leftStart].start;
                const int e = tl[h.leftStart + h.leftCount - 1].start +
                              tl[h.leftStart + h.leftCount - 1].length;
                out.left.append({s, e - s, true});
            }
        } else {
            if (h.rightCount > 0) {
                const int s = tr[h.rightStart].start;
                const int e = tr[h.rightStart + h.rightCount - 1].start +
                              tr[h.rightStart + h.rightCount - 1].length;
                out.right.append({s, e - s, true});
            }
        }
    }
    return out;
}

}  // namespace Diff
