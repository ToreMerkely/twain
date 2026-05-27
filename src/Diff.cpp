#include "Diff.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace Diff {

namespace {

// Drain pending paint/timer events so the status-bar busy indicator keeps
// animating during long diffs. Excludes user input to prevent re-entry
// (e.g. another setFiles fired mid-diff).
inline void pumpUi() {
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

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

    auto coarseScript = [&]() {
        QVector<Edit> edits;
        edits.reserve(n + m);
        for (int x = 0; x < n; ++x) edits.append({Op::Delete, x, -1});
        for (int y = 0; y < m; ++y) edits.append({Op::Insert, -1, y});
        return edits;
    };

    // Either side empty: no diff search needed.
    if (n == 0 || m == 0) return coarseScript();

    // Cap edit distance: Myers is O((N+M)*D) in time and snapshots `v` per
    // iteration so memory grows as O(D*(N+M)). On lopsided inputs (e.g.
    // 10,000 lines vs 1) D ≈ max(N,M) and the algorithm allocates hundreds
    // of MB. Past this cap we emit a coarse delete-all/insert-all script.
    constexpr int kMaxD = 5000;
    const int maxD = std::min(max, kMaxD);

    // Edit distance is at least |n - m|; if that already exceeds the cap,
    // there's no point running the loop — we'd allocate ~maxD copies of v
    // (each 2*max+1 ints) before giving up.
    const int absDiff = n > m ? n - m : m - n;
    if (absDiff > maxD) return coarseScript();

    const int offset = max;
    QVector<int> v(2 * max + 1, 0);
    QVector<QVector<int>> trace;
    trace.reserve(maxD + 1);

    int reachedD = -1;
    for (int d = 0; d <= maxD; ++d) {
        if ((d & 63) == 0) pumpUi();
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

    if (reachedD < 0) return coarseScript();

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

// Patience-style anchor finding: returns (leftIdx, rightIdx) pairs of lines
// that (a) appear exactly once on each side, (b) match across sides, and (c)
// form an order-preserving subset (longest increasing subsequence of rightIdx
// when sorted by leftIdx). Used to chunk Myers so it never has to plow through
// a highly-lopsided block end-to-end.
QVector<QPair<int, int>> patienceAnchors(const QStringList& left,
                                         const QStringList& right) {
    if (left.isEmpty() || right.isEmpty()) return {};
    QHash<QString, int> leftCount, rightCount;
    leftCount.reserve(left.size());
    rightCount.reserve(right.size());
    for (const auto& s : left) ++leftCount[s];
    for (const auto& s : right) ++rightCount[s];

    QHash<QString, int> leftPos, rightPos;
    for (int i = 0; i < left.size(); ++i) {
        if (leftCount.value(left[i]) == 1) leftPos.insert(left[i], i);
    }
    for (int i = 0; i < right.size(); ++i) {
        if (rightCount.value(right[i]) == 1) rightPos.insert(right[i], i);
    }

    // Candidate pairs, sorted by leftIdx.
    QVector<QPair<int, int>> cand;
    cand.reserve(qMin(leftPos.size(), rightPos.size()));
    for (auto it = leftPos.constBegin(); it != leftPos.constEnd(); ++it) {
        const auto rit = rightPos.constFind(it.key());
        if (rit != rightPos.constEnd()) cand.append({it.value(), rit.value()});
    }
    std::sort(cand.begin(), cand.end());

    // LIS on rightIdx via patience sort, with parent links for reconstruction.
    const int N = cand.size();
    if (N == 0) return {};
    QVector<int> tails;        // tails[k] = index into cand of current top of pile k
    QVector<int> prev(N, -1);  // prev[i] = index into cand of predecessor in LIS
    for (int i = 0; i < N; ++i) {
        const int r = cand[i].second;
        int lo = 0, hi = tails.size();
        while (lo < hi) {
            const int mid = (lo + hi) / 2;
            if (cand[tails[mid]].second < r) lo = mid + 1;
            else hi = mid;
        }
        if (lo > 0) prev[i] = tails[lo - 1];
        if (lo == tails.size()) tails.append(i);
        else tails[lo] = i;
    }

    QVector<QPair<int, int>> out;
    out.reserve(tails.size());
    int cur = tails.isEmpty() ? -1 : tails.last();
    while (cur != -1) {
        out.append(cand[cur]);
        cur = prev[cur];
    }
    std::reverse(out.begin(), out.end());
    return out;
}

// Diff already-normalized line lists. Trims shared prefix/suffix, picks
// patience-style anchors, recurses on each between-anchor chunk; only the
// leaf chunks (no anchors and no trim possible) bottom out into plain
// myersEditScript. Returns Edits with indices into `left` / `right`.
QVector<Edit> diffNormalized(const QStringList& left, const QStringList& right) {
    const int nL = left.size();
    const int nR = right.size();

    int prefix = 0;
    while (prefix < nL && prefix < nR && left[prefix] == right[prefix]) ++prefix;
    int suffix = 0;
    while (prefix + suffix < nL && prefix + suffix < nR &&
           left[nL - 1 - suffix] == right[nR - 1 - suffix]) {
        ++suffix;
    }

    const QStringList midLeft = left.mid(prefix, nL - prefix - suffix);
    const QStringList midRight = right.mid(prefix, nR - prefix - suffix);

    const QVector<QPair<int, int>> anchors = patienceAnchors(midLeft, midRight);

    QVector<Edit> midEdits;
    if (anchors.isEmpty()) {
        midEdits = myersEditScript(midLeft, midRight);
    } else {
        auto appendChunk = [&](const QStringList& subL, const QStringList& subR,
                               int baseL, int baseR) {
            // Recurse: a chunk between anchors may itself have a shared
            // prefix/suffix or internal anchors to find.
            QVector<Edit> sub = diffNormalized(subL, subR);
            for (auto& e : sub) {
                if (e.leftIndex >= 0) e.leftIndex += baseL;
                if (e.rightIndex >= 0) e.rightIndex += baseR;
                midEdits.append(e);
            }
        };
        int curL = 0, curR = 0;
        for (const auto& a : anchors) {
            appendChunk(midLeft.mid(curL, a.first - curL),
                        midRight.mid(curR, a.second - curR), curL, curR);
            midEdits.append({Op::Equal, a.first, a.second});
            curL = a.first + 1;
            curR = a.second + 1;
        }
        appendChunk(midLeft.mid(curL), midRight.mid(curR), curL, curR);
    }

    QVector<Edit> edits;
    edits.reserve(prefix + midEdits.size() + suffix);
    for (int k = 0; k < prefix; ++k) edits.append({Op::Equal, k, k});
    for (const auto& e : midEdits) {
        Edit shifted = e;
        if (shifted.leftIndex >= 0) shifted.leftIndex += prefix;
        if (shifted.rightIndex >= 0) shifted.rightIndex += prefix;
        edits.append(shifted);
    }
    for (int k = 0; k < suffix; ++k) {
        edits.append({Op::Equal, nL - suffix + k, nR - suffix + k});
    }
    return edits;
}

}  // namespace

QVector<Hunk> compute(const QStringList& left, const QStringList& right, Options opts) {
    const QStringList normLeft = normalizeAll(left, opts);
    const QStringList normRight = normalizeAll(right, opts);

    const QVector<Edit> edits = diffNormalized(normLeft, normRight);

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

    // Cap the DP size. alignBlock is O(n*m) in both time and memory: the
    // score/back matrices alone need ~12 bytes per cell, and each cell runs
    // a similarity() over the two lines. For pathological inputs (e.g. when
    // compute() fell back to a coarse delete-all/insert-all script over
    // thousands of lines) this would allocate >1 GB and run for many
    // minutes. Past the cap, fall back to a positional 1-to-1 pairing.
    if (static_cast<long long>(n) * static_cast<long long>(m) > kMaxAlignCells) {
        QVector<AlignmentPair> out;
        const int paired = std::min(n, m);
        out.reserve(n + m - paired);
        for (int k = 0; k < paired; ++k) out.append({k, k});
        for (int k = paired; k < n; ++k) out.append({k, -1});
        for (int k = paired; k < m; ++k) out.append({-1, k});
        return out;
    }

    // Precompute the word-set for each line once. similarity() otherwise
    // re-tokenizes both lines on every DP cell — for blocks near the
    // kMaxAlignCells cap that means ~n*m tokenize() calls instead of n+m,
    // which dominates the running time.
    auto wordSet = [](const QString& s) {
        QSet<QString> out;
        for (const auto& t : tokenize(s)) {
            if (!t.text.isEmpty() &&
                (t.text[0].isLetterOrNumber() || t.text[0] == QLatin1Char('_'))) {
                out.insert(t.text);
            }
        }
        return out;
    };
    QVector<QSet<QString>> leftSets;
    QVector<QSet<QString>> rightSets;
    leftSets.reserve(n);
    rightSets.reserve(m);
    for (const auto& s : left) leftSets.append(wordSet(s));
    for (const auto& s : right) rightSets.append(wordSet(s));

    QVector<QVector<double>> score(n + 1, QVector<double>(m + 1, 0.0));
    QVector<QVector<int>> back(n + 1, QVector<int>(m + 1, 0));
    // back: 0 = skip-left, 1 = skip-right, 2 = pair

    for (int i = 0; i <= n; ++i) {
        if ((i & 255) == 0) pumpUi();
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
                double s = 0.0;
                if (left[i - 1] == right[j - 1]) {
                    s = 1.0;
                } else {
                    const auto& sa = leftSets[i - 1];
                    const auto& sb = rightSets[j - 1];
                    if (!sa.isEmpty() || !sb.isEmpty()) {
                        // Iterate over the smaller set for the intersection.
                        const auto& small = sa.size() <= sb.size() ? sa : sb;
                        const auto& big = sa.size() <= sb.size() ? sb : sa;
                        int inter = 0;
                        for (const auto& t : small) if (big.contains(t)) ++inter;
                        const int uni = sa.size() + sb.size() - inter;
                        if (uni > 0) s = double(inter) / double(uni);
                    }
                }
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
