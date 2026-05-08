#include "Diff.h"

#include <QSet>

namespace Diff {

namespace {

struct Edit {
    Op op;
    int leftIndex;
    int rightIndex;
};

QString normalize(const QString& s, Options opts) {
    QString r = s;
    if (opts.ignoreWhitespace) r = r.simplified();
    if (opts.ignoreCase) r = r.toCaseFolded();
    return r;
}

QStringList normalizeAll(const QStringList& lines, Options opts) {
    if (!opts.ignoreCase && !opts.ignoreWhitespace) return lines;
    QStringList out;
    out.reserve(lines.size());
    for (const auto& s : lines) out.append(normalize(s, opts));
    return out;
}

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

QVector<Hunk> compute(const QStringList& left, const QStringList& right, Options opts) {
    const QStringList normLeft = normalizeAll(left, opts);
    const QStringList normRight = normalizeAll(right, opts);
    QVector<Edit> edits = myersEditScript(normLeft, normRight);

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
        const bool isWord = c.isLetterOrNumber();
        if (isWord) {
            int j = i + 1;
            while (j < n) {
                const QChar c2 = line[j];
                if (!c2.isLetterOrNumber()) break;
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

LineDiff lineDiff(const QString& left, const QString& right, Options opts) {
    const auto tl = tokenize(left);
    const auto tr = tokenize(right);

    QStringList ls, rs;
    ls.reserve(tl.size());
    rs.reserve(tr.size());
    for (const auto& t : tl) ls.append(t.text);
    for (const auto& t : tr) rs.append(t.text);

    const auto hunks = compute(ls, rs, opts);

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

double similarity(const QString& a, const QString& b) {
    if (a == b) return 1.0;
    const auto ta = tokenize(a);
    const auto tb = tokenize(b);
    QSet<QString> sa;
    QSet<QString> sb;
    for (const auto& t : ta) {
        if (!t.text.isEmpty() &&
            (t.text[0].isLetterOrNumber() || t.text[0] == QLatin1Char('_'))) {
            sa.insert(t.text);
        }
    }
    for (const auto& t : tb) {
        if (!t.text.isEmpty() &&
            (t.text[0].isLetterOrNumber() || t.text[0] == QLatin1Char('_'))) {
            sb.insert(t.text);
        }
    }
    if (sa.isEmpty() && sb.isEmpty()) return 0.0;
    int intersect = 0;
    for (const auto& t : sa) if (sb.contains(t)) ++intersect;
    const int unionSize = sa.size() + sb.size() - intersect;
    if (unionSize == 0) return 0.0;
    return double(intersect) / double(unionSize);
}

QVector<AlignmentPair> alignBlock(const QStringList& left, const QStringList& right) {
    const int n = left.size();
    const int m = right.size();
    constexpr double kSimilarityThreshold = 0.1;

    QVector<QVector<double>> score(n + 1, QVector<double>(m + 1, 0.0));
    QVector<QVector<int>> back(n + 1, QVector<int>(m + 1, 0));
    // back: 0 = skip-left, 1 = skip-right, 2 = pair

    for (int i = 0; i <= n; ++i) {
        for (int j = 0; j <= m; ++j) {
            if (i == 0 && j == 0) continue;
            double best = -1.0;
            int b = 0;
            if (i > 0) {
                best = score[i - 1][j];
                b = 0;
            }
            if (j > 0 && score[i][j - 1] > best) {
                best = score[i][j - 1];
                b = 1;
            }
            if (i > 0 && j > 0) {
                const double s = similarity(left[i - 1], right[j - 1]);
                if (s >= kSimilarityThreshold) {
                    const double pairScore = score[i - 1][j - 1] + s;
                    if (pairScore > best) {
                        best = pairScore;
                        b = 2;
                    }
                }
            }
            score[i][j] = best;
            back[i][j] = b;
        }
    }

    QVector<AlignmentPair> rev;
    int i = n;
    int j = m;
    while (i > 0 || j > 0) {
        const int b = (i > 0 && j > 0) ? back[i][j] : (i > 0 ? 0 : 1);
        if (b == 2) {
            rev.append({i - 1, j - 1});
            --i;
            --j;
        } else if (b == 0) {
            rev.append({i - 1, -1});
            --i;
        } else {
            rev.append({-1, j - 1});
            --j;
        }
    }
    QVector<AlignmentPair> out;
    out.reserve(rev.size());
    for (int k = rev.size() - 1; k >= 0; --k) out.append(rev[k]);
    return out;
}

}  // namespace Diff
