/*
 * Fooyin
 * Copyright © 2024, Luke Taylor <LukeT1@proton.me>
 *
 * Fooyin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Fooyin is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Fooyin.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include <QDataStream>
#include <QString>

namespace Fooyin::FileOps {
enum class Operation : uint8_t
{
    Copy = 0,
    Move,
    Rename,
    Create,
    Remove,
    Delete,
    Extract,
    RemoveArchive
};

struct FileOpPreset
{
    static constexpr quint8 Magic   = 99;
    static constexpr qint32 Version = 1;

    Operation op{Operation::Copy};
    QString name;

    QString dest;
    QString filename;

    bool overwrite{false};
    bool wholeDir{false};
    bool removeEmpty{false};
    bool removeSourceArchive{false};

    friend QDataStream& operator<<(QDataStream& stream, const FileOpPreset& preset)
    {
        stream << Magic;
        stream << Version;
        stream << preset.op;
        stream << preset.name;
        stream << preset.dest;
        stream << preset.filename;
        stream << preset.overwrite;
        stream << preset.wholeDir;
        stream << preset.removeEmpty;
        stream << preset.removeSourceArchive;
        return stream;
    }

    friend QDataStream& operator>>(QDataStream& stream, FileOpPreset& preset)
    {
        quint8 opOrMagic{0};
        stream >> opOrMagic;

        qint32 version{0};

        if(opOrMagic == Magic) {
            stream >> version;
            stream >> preset.op;
        }
        else {
            preset.op = static_cast<Operation>(opOrMagic);
        }

        stream >> preset.name;
        stream >> preset.dest;
        stream >> preset.filename;
        stream >> preset.overwrite;
        stream >> preset.wholeDir;
        stream >> preset.removeEmpty;
        if(version >= 1) {
            stream >> preset.removeSourceArchive;
        }

        return stream;
    }
};
} // namespace Fooyin::FileOps
