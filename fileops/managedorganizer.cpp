// SPDX-License-Identifier: GPL-3.0-or-later
#include "managedorganizer.h"
#include <core/coresettings.h>
#include <core/internalcoresettings.h>
#include <core/engine/audioloader.h>
#include <core/library/musiclibrary.h>
#include <core/player/playercontroller.h>
#include <core/plugins/coreplugincontext.h>
#include <utils/settings/settingsmanager.h>
#include <QCheckBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QVBoxLayout>
#include <QtConcurrentRun>

using namespace Qt::StringLiterals;
namespace Fooyin::FileOps {
namespace {
constexpr auto Enabled="FileOps/Managed/Enabled";
constexpr auto Root="FileOps/Managed/Root";
constexpr auto Imports="FileOps/Managed/Import";
constexpr auto Cleanup="FileOps/Managed/Cleanup";
constexpr auto CleanupArtwork="FileOps/Managed/CleanupArtwork";
constexpr auto CleanupNfo="FileOps/Managed/CleanupNfo";
qint64 modified(const QFileInfo& info) { return info.lastModified().toMSecsSinceEpoch(); }
class Provider final : public PluginSettingsProvider {
    ManagedOrganizer* m_organizer;
public:
    explicit Provider(ManagedOrganizer* organizer) : m_organizer(organizer) {}
    QDialog* createSettings(QWidget* parent) override { return m_organizer->dialog(parent); }
};
}
Managed::Tags ManagedOrganizer::tags(const Track& t)
{
    return {t.albumArtistsJoined(u"; "_s),t.artistsJoined(u"; "_s),t.album(),t.discNumber(),t.trackNumber(),t.title()};
}
QString ManagedOrganizer::root() const { return QDir::cleanPath(QDir::fromNativeSeparators(m_settings->fileValue(Root,u"D:/music"_s).toString())); }
bool ManagedOrganizer::enabled() const { return !m_blocked && m_settings->fileValue(Enabled,false).toBool(); }
bool ManagedOrganizer::imports() const { return m_settings->fileValue(Imports,true).toBool(); }
ManagedOrganizer::ManagedOrganizer(const CorePluginContext& c,QObject* parent)
    : QObject(parent),m_library(c.library),m_player(c.playerController),m_settings(c.settingsManager),m_loader(c.audioLoader)
{
    const auto state=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+u"/managed-fileops"_s;
    QDir{}.mkpath(state);
    m_journalPath=state+u"/pending.json"_s; m_logPath=state+u"/activity.log"_s;
    m_blocked=QFileInfo::exists(m_journalPath);
    m_timer.setInterval(1500);
    connect(&m_timer,&QTimer::timeout,this,&ManagedOrganizer::tick);
    connect(&m_watcher,&QFutureWatcher<Prepared>::finished,this,&ManagedOrganizer::prepared);
    const auto changed=[this](const TrackList& list) { acknowledge(); enqueue(list,true); };
    connect(m_library,&MusicLibrary::tracksAdded,this,changed);
    connect(m_library,&MusicLibrary::tracksUpdated,this,changed);
    connect(m_library,&MusicLibrary::tracksMetadataChanged,this,changed);
    connect(m_library,&MusicLibrary::tracksScanned,this,[changed](int,const TrackList& list) { changed(list); });
    connect(m_library,&MusicLibrary::scanProgress,this,[this](const ScanProgress& progress) {
        if(progress.id>=0 && progress.phase!=ScanProgress::Phase::Finished) m_scans.insert(progress.id);
    });
    connect(m_library,&MusicLibrary::scanFinished,this,[this](int id,ScanRequest::Type,bool) { m_scans.remove(id); });
    // Loading a playlist/database is not permission to import its entire history.
    connect(m_library,&MusicLibrary::tracksLoaded,this,[this](const TrackList&) { recover(); });
    connect(m_library,&MusicLibrary::tracksDeleted,this,[this](const TrackList& list) { for(const auto& t:list) m_queue.remove(t.id()); });
    QTimer::singleShot(3000,this,&ManagedOrganizer::recover);
    m_timer.start();
}
ManagedOrganizer::~ManagedOrganizer() { shutdown(); }
void ManagedOrganizer::shutdown()
{
    m_stopping=true; m_timer.stop();
    m_watcher.waitForFinished();
    // The staged copy is RAII-owned; the persistent journal protects a move awaiting DB acknowledgement.
    restoreMonitoring();
}
std::unique_ptr<PluginSettingsProvider> ManagedOrganizer::provider() { return std::make_unique<Provider>(this); }
bool ManagedOrganizer::eligible(const Track& t,bool allowExternal) const
{
    if(!t.isValid() || t.isRemote() || t.isInArchive() || t.hasCue() || t.isDatabaseOnlyMetadata() || t.subsong()>0) return false;
    if(Managed::inside(t.filepath(),root())) return true;
    // Never pull files out of a different configured library.
    return allowExternal && imports() && t.libraryId()<0;
}
bool ManagedOrganizer::playing(const Track& t) const
{
    return m_player->playState()!=Player::PlayState::Stopped && Managed::samePath(m_player->currentTrack().filepath(),t.filepath());
}
void ManagedOrganizer::enqueue(const TrackList& list,bool allowExternal)
{
    if(!enabled() || m_stopping) return;
    for(const auto& t:list) if(eligible(t,allowExternal) && !(m_busy && t.id()==m_track.id())) m_queue.insert(t.id(),0);
}
void ManagedOrganizer::tick()
{
    if(m_stopping) return;
    if(m_pending) {
        acknowledge();
        if(m_pending && QDateTime::currentMSecsSinceEpoch()-m_pendingSince>30000) {
            m_blocked=true;
            log(u"Paused: fooyin has not acknowledged a path update. Restart fooyin to recover it from pending.json."_s);
            m_pending=false; finish();
        }
        return;
    }
    if(m_busy || !enabled() || m_queue.isEmpty() || !m_scans.isEmpty()) return;
    for(auto it=m_queue.begin();it!=m_queue.end();) {
        if(m_retryAfter.value(it.key())>QDateTime::currentMSecsSinceEpoch()) { ++it; continue; }
        const Track t=m_library->trackForId(it.key());
        if(!eligible(t,true)) { it=m_queue.erase(it); continue; }
        if(playing(t)) {
            if(!m_waitingPlayback.contains(t.id())) { log(u"Waiting for playback to release: "_s+t.filepath()); m_waitingPlayback.insert(t.id()); }
            ++it; continue;
        }
        m_waitingPlayback.remove(t.id());
        const auto p=Managed::plan(t.filepath(),root(),tags(t),imports());
        if(!p.error.isEmpty()) { log(t.filepath()+u": "_s+p.error); it=m_queue.erase(it); continue; }
        if(Managed::samePath(p.source,p.destination)) { it=m_queue.erase(it); continue; }
        const auto lib=m_library->libraryForPath(p.destination);
        if(!lib || !Managed::samePath(lib->path,p.root)) {
            log(u"Add the managed folder itself as a fooyin library first: "_s+p.root); m_queue.clear(); return;
        }
        // Two cue/subsong records must never be split by moving their shared backing file.
        const auto all=m_library->tracks();
        if(std::ranges::any_of(all,[&](const Track& other){return other.id()!=t.id() && Managed::samePath(other.filepath(),t.filepath());})) {
            log(u"Skipped shared backing file: "_s+t.filepath()); it=m_queue.erase(it); continue;
        }
        m_busy=true; m_track=t; m_tags=tags(t); m_plan=p;
        const auto loader=m_loader; const auto expected=m_tags;
        m_watcher.setFuture(QtConcurrent::run([loader,t,p,expected] {
            Prepared result;
            const QFileInfo before(p.source); result.size=before.size(); result.modified=modified(before);
            Track disk(t.filepath(),t.subsong());
            if(!loader->readTrackMetadata(disk)) { result.error=u"Cannot read file tags yet."_s; return result; }
            result.tags=tags(disk);
            if(result.tags!=expected) { result.error=u"Waiting for file tags and fooyin tags to agree."_s; return result; }
            if(p.copy) {
                result.staged=std::make_shared<QTemporaryFile>(p.root+u"/.fooyin-import-XXXXXX.part"_s);
                if(!result.staged->open()) { result.error=result.staged->errorString(); return result; }
                QFile source(p.source);
                if(!source.open(QIODevice::ReadOnly)) { result.error=source.errorString(); return result; }
                while(!source.atEnd()) {
                    const auto bytes=source.read(1024*1024);
                    if(bytes.isEmpty() || result.staged->write(bytes)!=bytes.size()) { result.error=u"Import copy failed; original retained."_s; return result; }
                }
                if(!result.staged->flush() || result.staged->size()!=result.size) { result.error=u"Import copy incomplete."_s; return result; }
                result.staged->setFileTime(before.lastModified(),QFileDevice::FileModificationTime);
                result.staged->close();
            }
            const QFileInfo after(p.source);
            if(!after.exists() || after.size()!=result.size || modified(after)!=result.modified) result.error=u"File changed during preparation; waiting."_s;
            return result;
        }));
        return;
    }
}
void ManagedOrganizer::prepared()
{
    if(m_stopping) return;
    const Prepared result=m_watcher.result();
    if(!result.error.isEmpty()) { retry(result.error); return; }
    pauseMonitoring();
    // Settings notifications are queued. Allow watchers to detach before renaming.
    QTimer::singleShot(100,this,[this,result] { commit(result); });
}
void ManagedOrganizer::commit(const Prepared& result)
{
    if(m_stopping) return;
    const Track live=m_library->trackForId(m_track.id());
    const QFileInfo info(m_plan.source);
    if(!enabled() || !m_scans.isEmpty() || playing(live) || !live.isValid() || tags(live)!=m_tags || !Managed::samePath(live.filepath(),m_plan.source)
       || root()!=m_plan.root || info.size()!=result.size || modified(info)!=result.modified) {
        retry(u"Track or settings changed before move; waiting."_s); return;
    }
    const auto checked=Managed::plan(live.filepath(),root(),m_tags,imports());
    if(!checked.error.isEmpty()) { finish(checked.error); return; }
    if(!saveJournal(live,m_plan,result.size,result.modified)) { finish(u"Cannot write recovery journal; no file changed."_s); return; }
    Managed::Result operation;
    if(m_plan.copy) {
        if(!result.staged || !QDir{}.mkpath(QFileInfo(m_plan.destination).absolutePath())) operation={false,u"Cannot prepare import destination."_s};
        else {
            const bool ok=result.staged->rename(m_plan.destination);
            if(ok) result.staged->setAutoRemove(false);
            operation={ok,ok?QString{}:u"Cannot finish import; original retained."_s};
        }
    }
    else operation=Managed::execute(m_plan);
    if(!operation.succeeded) {
        QFile::remove(m_journalPath);
        if(operation.retryable) retry(operation.error,true);
        else finish(operation.error);
        return;
    }
    const auto lib=m_library->libraryForPath(m_plan.destination);
    Track updated=live; updated.setFilePath(m_plan.destination); if(lib) updated.setLibraryId(lib->id);
    m_pending=true; m_pendingSince=QDateTime::currentMSecsSinceEpoch();
    m_library->updateTrackMetadata({updated});
}
bool ManagedOrganizer::saveJournal(const Track& t,const Managed::Plan& p,qint64 size,qint64 time)
{
    if(QFileInfo::exists(m_journalPath)) return false;
    QSaveFile file(m_journalPath);
    if(!file.open(QIODevice::WriteOnly)) return false;
    QJsonObject object{{u"id"_s,t.id()},{u"source"_s,p.source},{u"destination"_s,p.destination},{u"root"_s,p.root},
                       {u"copy"_s,p.copy},{u"size"_s,QString::number(size)},{u"modified"_s,QString::number(time)}};
    const auto data=QJsonDocument(object).toJson();
    return file.write(data)==data.size() && file.commit();
}
void ManagedOrganizer::acknowledge()
{
    if(!m_pending) return;
    const auto current=m_library->trackForId(m_track.id());
    if(!Managed::samePath(current.filepath(),m_plan.destination)) return;
    m_pending=false;
    log((m_plan.copy?u"Imported: "_s:u"Moved: "_s)+m_plan.source+u" → "_s+m_plan.destination);
    if(!m_plan.copy) cleanupOldPath(m_plan.source);
    QFile::remove(m_journalPath);
    finish();
}
void ManagedOrganizer::recover()
{
    if(m_busy || m_stopping || !QFileInfo::exists(m_journalPath)) return;
    QFile file(m_journalPath); if(!file.open(QIODevice::ReadOnly)) { m_blocked=true; log(u"Cannot read pending recovery journal."_s); return; }
    const auto o=QJsonDocument::fromJson(file.readAll()).object(); file.close();
    const Track live=m_library->trackForId(o.value(u"id"_s).toInt(-1));
    if(!live.isValid()) return; // Database may not have loaded yet.
    const QString source=o.value(u"source"_s).toString(), dest=o.value(u"destination"_s).toString(), base=o.value(u"root"_s).toString();
    const bool copy=o.value(u"copy"_s).toBool(); const QFileInfo destination(dest);
    if(Managed::samePath(live.filepath(),dest) && destination.isFile()) { m_blocked=!QFile::remove(m_journalPath); return; }
    if(QFileInfo::exists(source) && !destination.exists()) { m_blocked=!QFile::remove(m_journalPath); return; }
    if(!Managed::samePath(base,root()) || !Managed::inside(dest,base) || Managed::hasLinkAncestor(dest)
       || !Managed::samePath(live.filepath(),source) || (!copy && QFileInfo::exists(source)) || !destination.isFile()
       || destination.size()!=o.value(u"size"_s).toString().toLongLong()
       || modified(destination)!=o.value(u"modified"_s).toString().toLongLong()) {
        m_blocked=true; log(u"Recovery needs review; automatic organization paused. Journal: "_s+m_journalPath); return;
    }
    const auto lib=m_library->libraryForPath(dest);
    if(!lib) { m_blocked=true; log(u"Recovery destination is no longer in a library."_s); return; }
    m_track=live; m_plan={source,dest,base,{},copy}; m_busy=true; m_pending=true;
    m_blocked=false;
    m_pendingSince=QDateTime::currentMSecsSinceEpoch();
    Track updated=live; updated.setFilePath(dest); updated.setLibraryId(lib->id);
    log(u"Recovering interrupted path update: "_s+dest);
    m_library->updateTrackMetadata({updated});
}
void ManagedOrganizer::retry(const QString& reason, bool temporaryFileError)
{
    const int id=m_track.id(); const int attempts=m_queue.value(id)+1;
    const int limit=temporaryFileError?10:5;
    if(attempts>=limit) { finish(reason+u" Skipped after %1 attempts; refresh or preview to retry."_s.arg(limit)); return; }
    if(temporaryFileError) {
        const int delay=std::min(30000,1500*(1<<std::min(attempts,5)));
        m_retryAfter.insert(id,QDateTime::currentMSecsSinceEpoch()+delay);
        log(u"Temporary move failure; retry %1 in %2 seconds: %3 — %4"_s.arg(attempts).arg(delay/1000).arg(m_plan.source,reason));
    }
    m_queue.insert(id,attempts); m_busy=false; restoreMonitoring();
}
void ManagedOrganizer::finish(const QString& error)
{
    if(!error.isEmpty()) log(m_plan.source+u": "_s+error);
    m_queue.remove(m_track.id()); m_retryAfter.remove(m_track.id()); m_busy=false; restoreMonitoring();
}
void ManagedOrganizer::pauseMonitoring()
{
    if(m_paused) return;
    m_monitorDirs=m_settings->value<Fooyin::Settings::Core::Internal::MonitorLibraryDirectories>();
    m_monitorFiles=m_settings->value<Fooyin::Settings::Core::Internal::MonitorTrackFiles>();
    m_paused=true;
    m_settings->set<Fooyin::Settings::Core::Internal::MonitorLibraryDirectories>(false);
    m_settings->set<Fooyin::Settings::Core::Internal::MonitorTrackFiles>(false);
}
void ManagedOrganizer::restoreMonitoring()
{
    if(!m_paused) return; m_paused=false;
    m_settings->set<Fooyin::Settings::Core::Internal::MonitorLibraryDirectories>(m_monitorDirs);
    m_settings->set<Fooyin::Settings::Core::Internal::MonitorTrackFiles>(m_monitorFiles);
}
void ManagedOrganizer::log(const QString& message)
{
    const auto line=QDateTime::currentDateTime().toString(Qt::ISODate)+u"  "_s+message;
    m_messages.append(line); while(m_messages.size()>200) m_messages.removeFirst();
    QFile file(m_logPath);
    if(file.open(QIODevice::Append|QIODevice::Text)) { file.write(line.toUtf8()); file.write("\n"); }
    Q_EMIT activityChanged();
}
void ManagedOrganizer::cleanupOldPath(const QString& source)
{
    if(!m_settings->fileValue(Cleanup,true).toBool()) return;
    const Managed::CleanupOptions options{m_settings->fileValue(CleanupArtwork,false).toBool(),m_settings->fileValue(CleanupNfo,false).toBool()};
    for(const auto& message:Managed::cleanupOldParents(source,root(),options)) log(message);
}
void ManagedOrganizer::cleanupHistory()
{
    if(m_busy) { log(u"Wait for the current operation before cleaning old folders."_s); return; }
    QFile file(m_logPath);
    if(!file.open(QIODevice::ReadOnly)) { log(u"No previous moves to clean up."_s); return; }
    QSet<QString> oldPaths;
    while(!file.atEnd()) {
        const QString line=QString::fromUtf8(file.readLine()).trimmed();
        const auto marker=line.indexOf(u"  Moved: "_s);
        if(marker<0) continue;
        const auto paths=line.mid(marker+9).split(u" → "_s);
        if(paths.size()!=2 || !Managed::inside(paths[0],root()) || !Managed::inside(paths[1],root())) continue;
        // Existing files and all folders containing music are protected by cleanupOldParents.
        oldPaths.insert(paths[0]);
    }
    file.close();
    for(const auto& path:oldPaths) cleanupOldPath(path);
    log(u"Finished checking old folders recorded in the activity log."_s);
}
QDialog* ManagedOrganizer::dialog(QWidget* parent)
{
    auto* d=new QDialog(parent); d->setWindowTitle(tr("File Operations — Managed library")); d->resize(850,600);
    auto* layout=new QVBoxLayout(d);
    auto* description=new QLabel(tr("Organize files as Album Artist / Album / Disc-Track Title.\n"
        "Artist is used when Album Artist is empty. New external additions are copied; their originals remain.\n"
        "Enable fooyin's library and track-file monitoring to detect edits made in other applications."),d);
    description->setWordWrap(true); layout->addWidget(description);
    auto* enable=new QCheckBox(tr("Automatically organize new additions and tag changes"),d); enable->setChecked(enabled()); layout->addWidget(enable);
    auto* form=new QFormLayout; auto* folder=new QLineEdit(QDir::toNativeSeparators(root()),d); form->addRow(tr("Managed folder:"),folder); layout->addLayout(form);
    auto* import=new QCheckBox(tr("Copy newly added files from outside this folder (including network files)"),d); import->setChecked(imports()); layout->addWidget(import);
    auto* cleanup=new QCheckBox(tr("Remove empty old folders, stopping at the managed folder"),d); cleanup->setChecked(m_settings->fileValue(Cleanup,true).toBool()); layout->addWidget(cleanup);
    auto* artwork=new QCheckBox(tr("Also recycle leftover cover artwork in old folders"),d); artwork->setChecked(m_settings->fileValue(CleanupArtwork,false).toBool()); layout->addWidget(artwork);
    auto* nfo=new QCheckBox(tr("Also recycle leftover NFO files in old folders"),d); nfo->setChecked(m_settings->fileValue(CleanupNfo,false).toBool()); layout->addWidget(nfo);
    artwork->setEnabled(cleanup->isChecked()); nfo->setEnabled(cleanup->isChecked());
    connect(cleanup,&QCheckBox::toggled,artwork,&QWidget::setEnabled); connect(cleanup,&QCheckBox::toggled,nfo,&QWidget::setEnabled);
    auto* note=new QLabel(tr("Existing files are not bulk-organized when you enable this. Use Preview existing library below.\n"
        "Playing files wait. Missing tags, conflicting names, cue sheets and linked folders are skipped.\n"
        "Companion cleanup runs only when no music, subfolders or unknown files remain.\n"
        "Recognized cover images and NFO files go to the Recycle Bin when selected above.\n"
        "Embedded artwork is not checked; enable artwork cleanup only if you no longer need the image files."),d); note->setWordWrap(true); layout->addWidget(note);
    auto* activity=new QPlainTextEdit(d); activity->setReadOnly(true); activity->setPlainText(m_messages.join(u'\n')); layout->addWidget(activity,1);
    connect(this,&ManagedOrganizer::activityChanged,d,[this,activity]{activity->setPlainText(m_messages.join(u'\n'));});
    auto* preview=new QPushButton(tr("Preview existing library…"),d); layout->addWidget(preview);
    const auto save=[this,d,enable,folder,import,cleanup,artwork,nfo] {
        if(m_busy) { QMessageBox::information(d,tr("Operation in progress"),tr("Wait for the current operation to finish before changing settings.")); return false; }
        const auto path=QDir::cleanPath(QDir::fromNativeSeparators(folder->text().trimmed()));
        const QFileInfo info(path); const auto lib=m_library->libraryForPath(path+u"/probe.mp3"_s);
        if(enable->isChecked() && (!info.isAbsolute() || !info.isDir() || info.isRoot() || Managed::hasLinkAncestor(path) || !lib || !Managed::samePath(lib->path,path))) {
            QMessageBox::warning(d,tr("Choose a library folder"),tr("Choose an existing folder that is itself configured as a fooyin library. Drive roots and linked folders are not supported.")); return false;
        }
        if(m_blocked && enable->isChecked()) { QMessageBox::warning(d,tr("Recovery needed"),tr("Resolve the pending recovery journal before enabling organization.")); return false; }
        m_settings->fileSet(Root,path);
        m_settings->fileSet(Imports,import->isChecked());
        m_settings->fileSet(Cleanup,cleanup->isChecked());
        m_settings->fileSet(CleanupArtwork,artwork->isChecked());
        m_settings->fileSet(CleanupNfo,nfo->isChecked());
        m_settings->fileSet(Enabled,enable->isChecked());
        const bool ok=root()==path && imports()==import->isChecked()
            && m_settings->fileValue(Cleanup).toBool()==cleanup->isChecked()
            && m_settings->fileValue(CleanupArtwork).toBool()==artwork->isChecked()
            && m_settings->fileValue(CleanupNfo).toBool()==nfo->isChecked()
            && m_settings->fileValue(Enabled).toBool()==enable->isChecked();
        if(!ok) { QMessageBox::warning(d,tr("Settings error"),tr("Could not save organizer settings.")); return false; }
        if(!enable->isChecked()) { m_queue.clear(); m_retryAfter.clear(); }
        return true;
    };
    connect(preview,&QPushButton::clicked,d,[this,d,folder] {
        const auto base=QDir::cleanPath(QDir::fromNativeSeparators(folder->text().trimmed()));
        QStringList lines; TrackList candidates; int unchanged=0,skipped=0;
        for(const auto& t:m_library->libraryTracks()) {
            if(!Managed::inside(t.filepath(),base)) continue;
            if(!eligible(t,false)) { ++skipped; continue; }
            const auto p=Managed::plan(t.filepath(),base,tags(t),false);
            if(!p.error.isEmpty()) { ++skipped; lines.append(u"SKIP: "_s+t.filepath()+u" — "_s+p.error); }
            else if(Managed::samePath(p.source,p.destination)) ++unchanged;
            else { candidates.push_back(t); lines.append(p.source+u"\n  → "_s+p.destination); }
        }
        auto* pd=new QDialog(d); pd->setAttribute(Qt::WA_DeleteOnClose); pd->setWindowTitle(tr("Preview existing library")); pd->resize(1000,650);
        auto* pl=new QVBoxLayout(pd); pl->addWidget(new QLabel(tr("%1 proposed moves; %2 already organized; %3 skipped.\nEach file is rechecked before moving.").arg(candidates.size()).arg(unchanged).arg(skipped),pd));
        auto* text=new QPlainTextEdit(pd); text->setReadOnly(true); text->setPlainText(lines.join(u'\n')); pl->addWidget(text);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,pd); auto* run=buttons->addButton(tr("Organize these files"),QDialogButtonBox::ActionRole);
        run->setEnabled(enabled() && Managed::samePath(base,root()) && !candidates.empty());
        pl->addWidget(new QLabel(tr("To organize this list, first save the settings with automatic organization enabled."),pd)); pl->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::rejected,pd,&QDialog::reject);
        connect(run,&QPushButton::clicked,pd,[this,pd,candidates] { enqueue(candidates,false); pd->accept(); }); pd->open();
    });
    auto* cleanHistory=new QPushButton(tr("Clean up old folders from activity log"),d); layout->addWidget(cleanHistory);
    connect(cleanHistory,&QPushButton::clicked,d,[this,save] { if(save()) cleanupHistory(); });
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,d); layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,d,[d,save]{if(save()) d->accept();});
    connect(buttons,&QDialogButtonBox::rejected,d,&QDialog::reject);
    return d;
}
} // namespace Fooyin::FileOps
