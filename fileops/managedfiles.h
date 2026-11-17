// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>
#include <functional>

namespace Fooyin::FileOps::Managed {
struct Tags {
    QString albumArtist, artist, album, disc, track, title;
    bool operator==(const Tags&) const = default;
};
struct Plan {
    QString source, destination, root, error;
    bool copy{false};
};
struct Result {
    bool succeeded{false};
    QString error;
    bool retryable{false};
};
inline const QString DefaultPattern = QStringLiteral(
    "%albumartist%/%album%/$if(%disc%,%disc%-,)$num(%track%,2) %title%");
QString component(QString value);
bool samePath(const QString& a, const QString& b);
bool inside(const QString& path, const QString& root);
bool hasLinkAncestor(QString path);
QString evaluatePattern(const QString& pattern, const Tags& tags);
Plan plan(const QString& source, const QString& root, const Tags& tags, bool allowImport,
          const QString& pattern = DefaultPattern);
Result execute(const Plan& plan);
QStringList removeEmptyParents(const QString& oldFile, const QString& root);
struct CleanupOptions { bool artwork{false}; bool nfo{false}; };
using Recycler = std::function<bool(const QString&)>;
QStringList cleanupOldParents(const QString& oldFile, const QString& root, CleanupOptions options,
                              Recycler recycle = {});
} // namespace Fooyin::FileOps::Managed
