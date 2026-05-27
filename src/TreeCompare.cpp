#include "TreeCompare.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

namespace TreeCompare {

namespace {

QByteArray hashFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha1);
    if (!h.addData(&f)) return {};
    return h.result();
}

bool filesAreSame(const QFileInfo& l, const QFileInfo& r) {
    if (l.size() != r.size()) return false;
    const QByteArray hl = hashFile(l.absoluteFilePath());
    const QByteArray hr = hashFile(r.absoluteFilePath());
    return !hl.isEmpty() && hl == hr;
}

void sortChildren(QVector<Entry>& children) {
    std::sort(children.begin(), children.end(), [](const Entry& a, const Entry& b) {
        if (a.isDir != b.isDir) return a.isDir;  // dirs first
        return a.name.localeAwareCompare(b.name) < 0;
    });
}

bool aggregateFolderHasDifferences(const Entry& folder) {
    for (const auto& c : folder.children) {
        if (c.status != Status::Same) return true;
    }
    return false;
}

void buildSingleSidedTree(Entry& entry, const QString& path, bool leftSide,
                          QSet<QString>& visited);

void fillSingleSidedDir(Entry& entry, const QString& path, bool leftSide,
                        QSet<QString>& visited) {
    QDir dir(path);
    const QFileInfoList list = dir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const QFileInfo& info : list) {
        Entry child;
        child.name = info.fileName();
        child.isDir = info.isDir();
        child.status = leftSide ? Status::LeftOnly : Status::RightOnly;
        if (leftSide) {
            child.leftPath = info.absoluteFilePath();
            child.leftSize = info.isDir() ? -1 : info.size();
            child.leftMTime = info.lastModified();
        } else {
            child.rightPath = info.absoluteFilePath();
            child.rightSize = info.isDir() ? -1 : info.size();
            child.rightMTime = info.lastModified();
        }
        if (child.isDir) {
            buildSingleSidedTree(child, info.absoluteFilePath(), leftSide, visited);
        }
        entry.children.append(child);
    }
    sortChildren(entry.children);
}

void buildSingleSidedTree(Entry& entry, const QString& path, bool leftSide,
                          QSet<QString>& visited) {
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (canonical.isEmpty() || visited.contains(canonical)) return;
    visited.insert(canonical);
    fillSingleSidedDir(entry, path, leftSide, visited);
}

void compareDir(Entry& entry, const QString& leftPath, const QString& rightPath,
                QSet<QString>& visitedLeft, QSet<QString>& visitedRight);

void compareDirContents(Entry& entry, const QString& leftPath, const QString& rightPath,
                        QSet<QString>& visitedLeft, QSet<QString>& visitedRight) {
    QDir lDir(leftPath);
    QDir rDir(rightPath);
    const QFileInfoList lList = lDir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
    const QFileInfoList rList = rDir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);

    QMap<QString, QFileInfo> lByName, rByName;
    for (const auto& fi : lList) lByName.insert(fi.fileName(), fi);
    for (const auto& fi : rList) rByName.insert(fi.fileName(), fi);

    QSet<QString> names;
    for (auto it = lByName.constBegin(); it != lByName.constEnd(); ++it) names.insert(it.key());
    for (auto it = rByName.constBegin(); it != rByName.constEnd(); ++it) names.insert(it.key());

    QStringList sortedNames(names.constBegin(), names.constEnd());
    std::sort(sortedNames.begin(), sortedNames.end());

    for (const QString& name : sortedNames) {
        const bool inLeft = lByName.contains(name);
        const bool inRight = rByName.contains(name);

        Entry child;
        child.name = name;

        if (inLeft && !inRight) {
            const QFileInfo& info = lByName[name];
            child.isDir = info.isDir();
            child.status = Status::LeftOnly;
            child.leftPath = info.absoluteFilePath();
            child.leftSize = info.isDir() ? -1 : info.size();
            child.leftMTime = info.lastModified();
            if (child.isDir) buildSingleSidedTree(child, info.absoluteFilePath(), true, visitedLeft);
        } else if (inRight && !inLeft) {
            const QFileInfo& info = rByName[name];
            child.isDir = info.isDir();
            child.status = Status::RightOnly;
            child.rightPath = info.absoluteFilePath();
            child.rightSize = info.isDir() ? -1 : info.size();
            child.rightMTime = info.lastModified();
            if (child.isDir) buildSingleSidedTree(child, info.absoluteFilePath(), false, visitedRight);
        } else {
            const QFileInfo& lInfo = lByName[name];
            const QFileInfo& rInfo = rByName[name];
            child.leftPath = lInfo.absoluteFilePath();
            child.rightPath = rInfo.absoluteFilePath();
            child.leftMTime = lInfo.lastModified();
            child.rightMTime = rInfo.lastModified();

            if (lInfo.isDir() && rInfo.isDir()) {
                child.isDir = true;
                child.leftSize = -1;
                child.rightSize = -1;
                compareDir(child, lInfo.absoluteFilePath(), rInfo.absoluteFilePath(),
                           visitedLeft, visitedRight);
            } else if (lInfo.isDir() != rInfo.isDir()) {
                child.isDir = lInfo.isDir() || rInfo.isDir();
                child.status = Status::Different;
                child.leftSize = lInfo.isDir() ? -1 : lInfo.size();
                child.rightSize = rInfo.isDir() ? -1 : rInfo.size();
            } else {
                child.isDir = false;
                child.leftSize = lInfo.size();
                child.rightSize = rInfo.size();
                child.status = filesAreSame(lInfo, rInfo) ? Status::Same : Status::Different;
            }
        }
        entry.children.append(child);
    }
    sortChildren(entry.children);
}

void compareDir(Entry& entry, const QString& leftPath, const QString& rightPath,
                QSet<QString>& visitedLeft, QSet<QString>& visitedRight) {
    const QString canonL = QFileInfo(leftPath).canonicalFilePath();
    const QString canonR = QFileInfo(rightPath).canonicalFilePath();
    bool descendL = !canonL.isEmpty() && !visitedLeft.contains(canonL);
    bool descendR = !canonR.isEmpty() && !visitedRight.contains(canonR);
    if (!descendL || !descendR) {
        entry.status = Status::Same;  // cycle hit; treat as same to avoid infinite work
        return;
    }
    visitedLeft.insert(canonL);
    visitedRight.insert(canonR);
    compareDirContents(entry, leftPath, rightPath, visitedLeft, visitedRight);
    entry.status = aggregateFolderHasDifferences(entry) ? Status::Different : Status::Same;
}

}  // namespace

Entry compare(const QString& leftRoot, const QString& rightRoot) {
    Entry root;
    root.name = QString();
    root.isDir = true;
    root.leftPath = leftRoot;
    root.rightPath = rightRoot;

    QSet<QString> visitedLeft, visitedRight;
    const QString canonL = QFileInfo(leftRoot).canonicalFilePath();
    const QString canonR = QFileInfo(rightRoot).canonicalFilePath();
    if (!canonL.isEmpty()) visitedLeft.insert(canonL);
    if (!canonR.isEmpty()) visitedRight.insert(canonR);

    compareDirContents(root, leftRoot, rightRoot, visitedLeft, visitedRight);
    root.status = aggregateFolderHasDifferences(root) ? Status::Different : Status::Same;
    return root;
}

}  // namespace TreeCompare
