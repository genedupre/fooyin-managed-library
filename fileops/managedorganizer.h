// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "managedfiles.h"
#include <core/track.h>
#include <gui/plugins/pluginsettingsprovider.h>
#include <QFutureWatcher>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <memory>
class QTemporaryFile;
namespace Fooyin {
struct CorePluginContext;
class AudioLoader;
class MusicLibrary;
class PlayerController;
class SettingsManager;
namespace FileOps {
class ManagedOrganizer : public QObject {
    Q_OBJECT
public:
    explicit ManagedOrganizer(const CorePluginContext& context, QObject* parent);
    ~ManagedOrganizer() override;
    std::unique_ptr<PluginSettingsProvider> provider();
    QDialog* dialog(QWidget* parent);
    void shutdown();
private:
    struct Prepared {
        Managed::Tags tags;
        QString error;
        qint64 size{}, modified{};
        std::shared_ptr<QTemporaryFile> staged;
    };
    static Managed::Tags tags(const Track& track);
    QString root() const;
    bool enabled() const;
    bool imports() const;
    bool eligible(const Track& track, bool allowExternal) const;
    bool playing(const Track& track) const;
    void enqueue(const TrackList& tracks, bool allowExternal);
    void tick();
    void prepared();
    void commit(const Prepared& prepared);
    void acknowledge();
    void recover();
    void finish(const QString& error = {});
    void retry(const QString& reason, bool temporaryFileError = false);
    void pauseMonitoring();
    void restoreMonitoring();
    void log(const QString& message);
    void cleanupOldPath(const QString& source);
    void cleanupHistory();
    bool saveJournal(const Track& track, const Managed::Plan& plan, qint64 size, qint64 modified);
    MusicLibrary* m_library;
    PlayerController* m_player;
    SettingsManager* m_settings;
    std::shared_ptr<AudioLoader> m_loader;
    QTimer m_timer;
    QFutureWatcher<Prepared> m_watcher;
    QMap<int,int> m_queue;
    QMap<int,qint64> m_retryAfter;
    QSet<int> m_scans;
    QSet<int> m_waitingPlayback;
    Track m_track;
    Managed::Tags m_tags;
    Managed::Plan m_plan;
    QString m_journalPath, m_logPath;
    QStringList m_messages;
    bool m_busy{false}, m_pending{false}, m_stopping{false}, m_paused{false};
    bool m_monitorDirs{false}, m_monitorFiles{false}, m_blocked{false};
    qint64 m_pendingSince{};
Q_SIGNALS:
    void activityChanged();
};
} // namespace FileOps
} // namespace Fooyin
