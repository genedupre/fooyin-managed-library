// SPDX-License-Identifier: GPL-3.0-or-later
#include "managedfiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <system_error>
#endif

using namespace Qt::StringLiterals;
namespace Fooyin::FileOps::Managed {
namespace {
QString clean(const QString& path) { return QDir::cleanPath(QDir::fromNativeSeparators(path)); }
constexpr Qt::CaseSensitivity pathCase =
#ifdef Q_OS_WIN
    Qt::CaseInsensitive;
#else
    Qt::CaseSensitive;
#endif
}
QString component(QString value)
{
    value = value.trimmed();
    value.replace(QRegularExpression(u"[\\x00-\\x1f<>:\"/\\\\|?*]"_s), u"_"_s);
    while(value.endsWith(u'.') || value.endsWith(u' ')) value.chop(1);
    if(value == u"." || value == u"..") value.clear();
    static const QRegularExpression reserved(u"^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\\.|$)"_s,
                                             QRegularExpression::CaseInsensitiveOption);
    if(reserved.match(value).hasMatch()) value.prepend(u'_');
    return value;
}
bool samePath(const QString& a, const QString& b) { return clean(a).compare(clean(b), pathCase) == 0; }
bool inside(const QString& path, const QString& root)
{
    QString prefix = clean(root);
    if(!prefix.endsWith(u'/')) prefix += u'/';
    return clean(path).startsWith(prefix, pathCase);
}
bool hasLinkAncestor(QString path)
{
    path = clean(path);
    for(;;) {
        const QFileInfo info(path);
        if(info.isSymLink() || info.isJunction()) return true;
        const QString parent = info.absolutePath();
        if(samePath(parent, path)) return false;
        path = parent;
    }
}
namespace {
QString sanitizeTagValue(const QString& value)
{
    QString result = value;
    result.replace(u'/', u'_');
    result.replace(u'\\', u'_');
    return result;
}
QString tagValue(const Tags& tags, const QString& name)
{
    if(name == u"albumartist") return sanitizeTagValue(tags.albumArtist.isEmpty() ? tags.artist : tags.albumArtist);
    if(name == u"artist") return sanitizeTagValue(tags.artist);
    if(name == u"album") return sanitizeTagValue(tags.album);
    if(name == u"title") return sanitizeTagValue(tags.title);
    if(name == u"disc") {
        const QString s = tags.disc.section(u'/', 0, 0).trimmed();
        bool ok = false; const int n = s.toInt(&ok);
        return ok && n > 0 ? QString::number(n) : QString{};
    }
    if(name == u"track") {
        const QString s = tags.track.section(u'/', 0, 0).trimmed();
        bool ok = false; const int n = s.toInt(&ok);
        return ok && n > 0 ? QString::number(n) : QString{};
    }
    if(name == u"genre") return {};
    if(name == u"year" || name == u"date") return {};
    return {};
}
int findMatchingParen(const QString& s, int start)
{
    int depth = 1;
    for(int i = start; i < s.size(); ++i) {
        if(s[i] == u'(') ++depth;
        else if(s[i] == u')') { if(--depth == 0) return i; }
    }
    return -1;
}
QStringList splitArgs(const QString& s)
{
    QStringList result; int depth = 0, start = 0;
    for(int i = 0; i < s.size(); ++i) {
        if(s[i] == u'(') ++depth;
        else if(s[i] == u')') --depth;
        else if(s[i] == u',' && depth == 0) { result.append(s.mid(start, i - start)); start = i + 1; }
    }
    result.append(s.mid(start));
    return result;
}
}
QString evaluatePattern(const QString& pattern, const Tags& tags)
{
    QString result;
    int i = 0;
    while(i < pattern.size()) {
        if(pattern[i] == u'%') {
            const int end = pattern.indexOf(u'%', i + 1);
            if(end > i) {
                result += tagValue(tags, pattern.mid(i + 1, end - i - 1).toLower());
                i = end + 1; continue;
            }
        }
        if(pattern[i] == u'$') {
            const int paren = pattern.indexOf(u'(', i);
            if(paren > i) {
                const QString func = pattern.mid(i + 1, paren - i - 1).toLower();
                const int close = findMatchingParen(pattern, paren + 1);
                if(close > paren) {
                    const auto args = splitArgs(pattern.mid(paren + 1, close - paren - 1));
                    if(func == u"if" && args.size() >= 2) {
                        const auto cond = evaluatePattern(args[0], tags);
                        result += cond.isEmpty()
                            ? (args.size() >= 3 ? evaluatePattern(args[2], tags) : QString{})
                            : evaluatePattern(args[1], tags);
                    } else if(func == u"if2" && args.size() >= 2) {
                        const auto val = evaluatePattern(args[0], tags);
                        result += val.isEmpty() ? evaluatePattern(args[1], tags) : val;
                    } else if(func == u"num" && args.size() >= 2) {
                        const auto val = evaluatePattern(args[0], tags);
                        bool ok = false; const int width = evaluatePattern(args[1], tags).toInt(&ok);
                        if(ok && !val.isEmpty()) {
                            bool numOk = false; const int n = val.toInt(&numOk);
                            result += numOk ? QString::number(n).rightJustified(width, u'0') : val;
                        }
                    } else if(func == u"lower" && args.size() >= 1) {
                        result += evaluatePattern(args[0], tags).toLower();
                    } else if(func == u"upper" && args.size() >= 1) {
                        result += evaluatePattern(args[0], tags).toUpper();
                    } else if(func == u"replace" && args.size() >= 3) {
                        result += evaluatePattern(args[0], tags).replace(
                            evaluatePattern(args[1], tags), evaluatePattern(args[2], tags));
                    } else if(func == u"trim" && args.size() >= 1) {
                        result += evaluatePattern(args[0], tags).trimmed();
                    }
                    i = close + 1; continue;
                }
            }
        }
        result += pattern[i]; ++i;
    }
    return result;
}
Plan plan(const QString& source, const QString& root, const Tags& tags, bool allowImport,
          const QString& pattern)
{
    Plan p{clean(source), {}, clean(root), {}, false};
    const QFileInfo rootInfo(p.root), file(p.source);
    if(!rootInfo.isAbsolute() || !rootInfo.isDir() || rootInfo.isRoot() || hasLinkAncestor(p.root)) {
        p.error = u"Choose an existing music folder, not a drive root or linked folder."_s;
        return p;
    }
    if(!file.isAbsolute() || !file.isFile() || hasLinkAncestor(p.source)) {
        p.error = u"Source is missing or uses a symbolic link/junction."_s;
        return p;
    }
    p.copy = !inside(p.source, p.root);
    if(p.copy && !allowImport) {
        p.error = u"Outside the managed folder; automatic import is disabled."_s;
        return p;
    }
    const QString evaluated = evaluatePattern(pattern, tags);
    const auto segments = evaluated.split(u'/');
    if(segments.isEmpty()) {
        p.error = u"Organization pattern produced an empty path."_s;
        return p;
    }
    QString relative;
    for(int idx = 0; idx < segments.size(); ++idx) {
        const QString sanitized = component(segments[idx]);
        if(sanitized.isEmpty()) {
            p.error = u"A required tag is empty; cannot organize this file."_s;
            return p;
        }
        if(idx > 0) relative += u'/';
        relative += sanitized;
    }
    const QString filename = relative + u'.' + file.suffix();
    p.destination = clean(p.root + u'/' + filename);
    if(!inside(p.destination, p.root) || hasLinkAncestor(QFileInfo(p.destination).absolutePath()))
        p.error = u"Destination escapes the music folder or uses a symbolic link/junction."_s;
    else if(p.destination.size() >= 250)
        p.error = u"Destination path is too long; shorten the tags or music folder path."_s;
    else if(!samePath(p.source, p.destination) && QFileInfo::exists(p.destination))
        p.error = u"Destination already exists; neither file was changed."_s;
    return p;
}
Result execute(const Plan& p)
{
    if(!p.error.isEmpty()) return {false, p.error};
    if(samePath(p.source, p.destination)) return {true, {}};
    if(!inside(p.destination, p.root) || hasLinkAncestor(p.source)
       || hasLinkAncestor(QFileInfo(p.destination).absolutePath()))
        return {false, u"Path validation changed before operation."_s};
    if(QFileInfo::exists(p.destination)) return {false, u"Destination already exists."_s};
    if(!QDir{}.mkpath(QFileInfo(p.destination).absolutePath()))
        return {false, u"Could not create the destination folder."_s};
    QFile f(p.source);
#ifdef Q_OS_WIN
    if(!p.copy) {
        // QFile::rename may fall back to copy/delete when a Windows handle blocks
        // the rename. A managed same-volume move must instead fail without copying.
        const auto source=QDir::toNativeSeparators(p.source), destination=QDir::toNativeSeparators(p.destination);
        if(MoveFileExW(reinterpret_cast<LPCWSTR>(source.utf16()),reinterpret_cast<LPCWSTR>(destination.utf16()),0)) return {true,{}};
        const auto code=GetLastError();
        const bool retryable=code==ERROR_SHARING_VIOLATION || code==ERROR_LOCK_VIOLATION || code==ERROR_ACCESS_DENIED;
        const auto message=std::system_category().message(static_cast<int>(code));
        return {false,QString::fromLocal8Bit(message.data(),static_cast<qsizetype>(message.size())).trimmed(),retryable};
    }
#endif
    const bool ok = p.copy ? f.copy(p.destination) : f.rename(p.destination);
    return {ok, ok ? QString{} : f.errorString()};
}
QStringList removeEmptyParents(const QString& oldFile, const QString& root)
{
    QStringList removed;
    QString folder = QFileInfo(oldFile).absolutePath();
    while(inside(folder, root) && !samePath(folder, root) && !hasLinkAncestor(folder)) {
        QDir d(folder);
        if(!d.exists() || !d.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty()) break;
        if(!QDir{}.rmdir(folder)) break;
        removed.append(folder);
        folder = QFileInfo(folder).absolutePath();
    }
    return removed;
}
QStringList cleanupOldParents(const QString& oldFile, const QString& root, CleanupOptions options, Recycler recycle)
{
    // Never recurse or infer that an arbitrary file is disposable. Any music, unknown file,
    // child directory or link prevents companion-file cleanup at that level.
    enum class Kind { Other, Artwork, Nfo };
    const auto kind=[](const QFileInfo& info) {
        if(!info.isFile() || info.isSymLink() || info.isJunction()) return Kind::Other;
        if(info.suffix().compare(u"nfo",Qt::CaseInsensitive)==0) return Kind::Nfo;
        static const QStringList extensions{u"jpg"_s,u"jpeg"_s,u"png"_s,u"webp"_s,u"gif"_s,u"bmp"_s,u"tif"_s,u"tiff"_s};
        static const QStringList names{u"cover"_s,u"folder"_s,u"front"_s,u"back"_s,u"fanart"_s,u"artist"_s,
            u"poster"_s,u"banner"_s,u"logo"_s,u"disc"_s,u"cdart"_s,u"clearart"_s,u"landscape"_s,u"thumb"_s};
        return extensions.contains(info.suffix().toLower()) && names.contains(info.completeBaseName().toLower())
            ? Kind::Artwork : Kind::Other;
    };
    if(!recycle) recycle=[](const QString& path) { return QFile::moveToTrash(path); };
    constexpr auto flags=QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot;
    QStringList messages;
    QString folder=QFileInfo(oldFile).absolutePath();
    while(inside(folder,root) && !samePath(folder,root) && !hasLinkAncestor(folder)) {
        QDir directory(folder);
        if(!directory.exists()) break;
        const auto entries=directory.entryInfoList(flags);
        const auto hasOther=[&] {
            directory.refresh();
            const auto current=directory.entryInfoList(flags);
            return std::any_of(current.cbegin(),current.cend(),[&](const QFileInfo& f) { return kind(f)==Kind::Other; });
        };
        if(hasOther()) break;
        for(const auto& entry:entries) {
            const auto type=kind(entry);
            if((type==Kind::Artwork && options.artwork) || (type==Kind::Nfo && options.nfo)) {
                if(hasLinkAncestor(folder) || hasOther()) return messages;
                const auto path=entry.absoluteFilePath();
                if(recycle(path)) messages.append(u"Recycled "_s+(type==Kind::Artwork?u"artwork: "_s:u"NFO: "_s)+path);
                else messages.append(u"Could not recycle; retained: "_s+path);
            }
        }
        directory.refresh();
        if(!directory.entryList(flags).isEmpty()) break;
        if(!QDir{}.rmdir(folder)) break;
        messages.append(u"Removed empty folder: "_s+folder);
        folder=QFileInfo(folder).absolutePath();
    }
    return messages;
}
} // namespace Fooyin::FileOps::Managed
