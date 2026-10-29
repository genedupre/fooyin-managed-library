// SPDX-License-Identifier: GPL-3.0-or-later
// Exercises the replacement DLL against fooyin's actual library, SQLite and tag reader.
#include <core/database/dbschema.h>
#include <core/internalcoresettings.h>
#include <core/library/librarymanager.h>
#include <core/library/unifiedmusiclibrary.h>
#include <core/engine/audioloader.h>
#include <core/engine/input/taglibparser.h>
#include <core/network/networkaccessmanager.h>
#include <core/network/remoteioservice.h>
#include <core/player/playercontroller.h>
#include <core/playlist/playlisthandler.h>
#include <core/playlist/playlistloader.h>
#include <core/plugins/coreplugin.h>
#include <core/plugins/plugin.h>
#include <core/scripting/scriptregistry.h>
#include <gui/plugins/pluginconfigguiplugin.h>
#include <gui/plugins/pluginsettingsprovider.h>
#include <utils/database/dbconnectionhandler.h>
#include <utils/database/dbconnectionprovider.h>
#include <utils/settings/settingsmanager.h>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QPluginLoader>
#include <QPushButton>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace Fooyin;
using namespace Qt::StringLiterals;
static QString dllPath,fixturePath,screenshotPath;
class IntegrationTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void realLibraryAndPlugin() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString root=temp.path()+u"/music"_s; QVERIFY(QDir{}.mkpath(root));
        SettingsManager settings(temp.path()+u"/settings.ini"_s); CoreSettings coreSettings(&settings);
        settings.set<Settings::Core::Internal::MonitorLibraryDirectories>(true);
        settings.set<Settings::Core::Internal::MonitorTrackFiles>(true);
        DbConnection::DbParams params; params.type=u"QSQLITE"_s; params.filePath=temp.path()+u"/library.db"_s;
        auto pool=DbConnectionPool::create(params,u"managed-integration"_s); DbConnectionHandler handler(pool); QVERIFY(handler.hasConnection());
        DbSchema schema{DbConnectionProvider(pool)};
        QCOMPARE(schema.upgradeDatabase(22,QString::fromUtf8(FOOYIN_SOURCE)+u"/data/dbschema.xml"_s),DbSchema::UpgradeResult::Success);
        LibraryManager manager(pool,&settings);
        auto loader=std::make_shared<AudioLoader>(); loader->addReader(u"TagLib"_s,[]{return std::make_unique<TagLibReader>();});
        auto playlistLoader=std::make_shared<PlaylistLoader>();
        auto network=std::make_shared<NetworkAccessManager>(&settings);
        auto remote=std::make_shared<RemoteIoService>(network,&settings);
        UnifiedMusicLibrary library(&manager,pool,playlistLoader,loader,remote,&settings);
        PlaylistHandler playlists(pool,loader,&library,&settings); PlayerController player(&settings,&playlists);
        const int libId=manager.addLibrary(root,u"Test music"_s); QVERIFY(libId>=0);
        settings.fileSet("FileOps/Managed/Root",root);
        settings.fileSet("FileOps/Managed/Import",true);
        settings.fileSet("FileOps/Managed/Cleanup",true);
        QPluginLoader plugin(dllPath); QObject* object=plugin.instance(); QVERIFY2(object,qPrintable(plugin.errorString()));
        std::unique_ptr<QObject> pluginOwner(object);
        auto* core=qobject_cast<CorePlugin*>(object); QVERIFY(core);
        CorePluginContext context(nullptr,&player,&manager,&library,&playlists,&settings,loader,playlistLoader,nullptr,network,pool,nullptr);
        core->initialise(context);
        auto* config=qobject_cast<PluginConfigGuiPlugin*>(object); QVERIFY(config);
        auto provider=config->settingsProvider(); QVERIFY(provider); provider->showSettings(nullptr);
        QDialog* dialog=nullptr;
        for(auto* w:QApplication::topLevelWidgets()) if(auto* d=qobject_cast<QDialog*>(w)) dialog=d;
        QVERIFY(dialog); QTest::qWait(100);
        QVERIFY(dialog->grab().save(screenshotPath));
        auto checks=dialog->findChildren<QCheckBox*>(); QVERIFY(checks.size()==5);
        QVERIFY(!checks[3]->isChecked()); QVERIFY(!checks[4]->isChecked());
        QVERIFY(!checks.front()->isChecked()); // Fresh installation is inert.
        // Save twice to ensure unchanged fileSet values do not abort later fields.
        auto* buttons=dialog->findChild<QDialogButtonBox*>(); QVERIFY(buttons);
        checks.front()->setChecked(true); buttons->button(QDialogButtonBox::Save)->click();
        QVERIFY(settings.fileValue("FileOps/Managed/Enabled").toBool());
        QVERIFY(!settings.fileValue("FileOps/Managed/CleanupArtwork").toBool());
        QVERIFY(!settings.fileValue("FileOps/Managed/CleanupNfo").toBool());
        QTest::qWait(50); provider->showSettings(nullptr); QTest::qWait(50);
        for(auto* w:QApplication::topLevelWidgets()) if(auto* d=qobject_cast<QDialog*>(w)) dialog=d;
        checks=dialog->findChildren<QCheckBox*>(); checks.front()->setChecked(false);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
        QVERIFY(!settings.fileValue("FileOps/Managed/Enabled").toBool());
        const QString source=root+u"/Taku Inoue/Tekken/song.mp3"_s; QVERIFY(QDir{}.mkpath(QFileInfo(source).absolutePath()));
        QVERIFY(QFile::copy(fixturePath,source));
        Track initial(source,0); QVERIFY(loader->readTrackMetadata(initial));
        initial.setAlbumArtists({u"Game Soundtracks"_s}); initial.setArtists({u"Taku Inoue"_s}); initial.setAlbum(u"Tekken"_s);
        initial.setDiscNumber(u"6"_s); initial.setTrackNumber(u"63"_s); initial.setTitle(u"Siren's Call"_s);
        QVERIFY(loader->writeTrackMetadata(initial,AudioReader::Metadata));
        library.scanFiles({QUrl::fromLocalFile(source)});
        QTRY_COMPARE_WITH_TIMEOUT(library.tracks().size(),size_t(1),10000);
        QTest::qWait(1800); QVERIFY(QFile::exists(source));
        Track track=library.tracks().front(); const int id=track.id(); QVERIFY(id>=0);
        Playlist* playlist=playlists.createPlaylist(u"Keep my references"_s,{track}); QVERIFY(playlist);
        settings.fileSet("FileOps/Managed/Enabled",true);
        QSignalSpy playRequests(&player,&PlayerController::trackChangeRequested);
        player.startPlayback(playlist); QCOMPARE(playRequests.size(),1);
        player.commitCurrentTrack(playRequests.front().front().value<Player::TrackChangeRequest>());
        player.syncPlayStateFromEngine(Player::PlayState::Playing);
        library.scanTracks({track});
        QTest::qWait(2200); QVERIFY(QFile::exists(source)); QCOMPARE(library.trackForId(id).filepath(),source);
        const QString dest=root+u"/Game Soundtracks/Tekken/6-63 Siren's Call.mp3"_s;
#ifdef Q_OS_WIN
        const auto nativeSource=QDir::toNativeSeparators(source);
        HANDLE handle=CreateFileW(reinterpret_cast<LPCWSTR>(nativeSource.utf16()),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        QVERIFY(handle!=INVALID_HANDLE_VALUE);
        auto closer=[](void* h){CloseHandle(h);}; std::unique_ptr<void,decltype(closer)> temporaryLock(handle,closer);
#endif
        player.reset();
#ifdef Q_OS_WIN
        QTest::qWait(4500); QVERIFY(QFile::exists(source)); QVERIFY(!QFile::exists(dest));
        const auto activityPath=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+u"/managed-fileops/activity.log"_s;
        QFile activity(activityPath); QVERIFY(activity.open(QIODevice::ReadOnly));
        QVERIFY(activity.readAll().contains("Temporary move failure; retry")); activity.close();
        temporaryLock.reset(); // No refresh or new metadata event: the queued retry must finish the move.
#endif
        QTRY_COMPARE_WITH_TIMEOUT(library.trackForId(id).filepath(),dest,12000);
        QVERIFY(QFile::exists(dest)); QVERIFY(!QFile::exists(source)); QVERIFY(!QDir(root+u"/Taku Inoue"_s).exists());
        QCOMPARE(library.tracks().size(),size_t(1));
        QCOMPARE(playlist->tracks().front().id(),id);
        QCOMPARE(playlist->tracks().front().filepath(),dest);
        // Edit tags on disk only, exactly as a separate tag editor would.
        Track external(dest,0); QVERIFY(loader->readTrackMetadata(external)); external.setAlbumArtists({u"Edited Elsewhere"_s});
        QVERIFY(loader->writeTrackMetadata(external,AudioReader::Metadata));
        const QString edited=root+u"/Edited Elsewhere/Tekken/6-63 Siren's Call.mp3"_s;
#ifndef Q_OS_WIN
        // inotify may not fire inside a Flatpak SDK sandbox; rescan explicitly.
        QTest::qWait(500);
        library.scanTracks({library.trackForId(id)});
#endif
        QTRY_COMPARE_WITH_TIMEOUT(library.trackForId(id).filepath(),edited,25000);
        QCOMPARE(playlist->tracks().front().filepath(),edited);
        QVERIFY(!QDir(root+u"/Game Soundtracks"_s).exists());
        // A fooyin-only tag change must not move the file before its tags have been written.
        Track pending=library.trackForId(id); pending.setAlbumArtists({u"Pending Write"_s});
        library.updateTrackMetadata({pending});
        QTest::qWait(3500); QCOMPARE(library.trackForId(id).filepath(),edited); QVERIFY(QFile::exists(edited));
        QVERIFY(loader->writeTrackMetadata(pending,AudioReader::Metadata));
        const QString written=root+u"/Pending Write/Tekken/6-63 Siren's Call.mp3"_s;
#ifndef Q_OS_WIN
        QTest::qWait(500);
        library.scanTracks({library.trackForId(id)});
#endif
        QTRY_COMPARE_WITH_TIMEOUT(library.trackForId(id).filepath(),written,25000);
        // Import from outside the music root preserves original bytes and adopts the existing track ID.
        const QString incoming=temp.path()+u"/incoming.mp3"_s; QVERIFY(QFile::copy(fixturePath,incoming));
        Track incomingTags(incoming,0); QVERIFY(loader->readTrackMetadata(incomingTags));
        incomingTags.setAlbumArtists({u"Game Soundtracks"_s}); incomingTags.setArtists({u"Taku Inoue"_s}); incomingTags.setAlbum(u"Import Test"_s);
        incomingTags.setTitle(u"Incoming"_s); incomingTags.setDiscNumber(u"1"_s); incomingTags.setTrackNumber(u"1"_s);
        QVERIFY(loader->writeTrackMetadata(incomingTags,AudioReader::Metadata));
        library.scanFiles({QUrl::fromLocalFile(incoming)}); QTRY_COMPARE_WITH_TIMEOUT(library.tracks().size(),size_t(2),10000);
        int incomingId=-1; for(const auto& t:library.tracks()) if(t.filepath()==incoming) incomingId=t.id(); QVERIFY(incomingId>=0);
        const QString imported=root+u"/Game Soundtracks/Import Test/1-01 Incoming.mp3"_s;
        QTRY_COMPARE_WITH_TIMEOUT(library.trackForId(incomingId).filepath(),imported,12000);
        QFile original(incoming),copy(imported); QVERIFY(original.open(QIODevice::ReadOnly)); QVERIFY(copy.open(QIODevice::ReadOnly)); QCOMPARE(copy.readAll(),original.readAll()); original.close(); copy.close();
        QCOMPARE(library.trackForId(incomingId).libraryId(),libId);
        // Existing destination must remain untouched.
        Track clash=library.trackForId(id); clash.setAlbumArtists({u"Game Soundtracks"_s}); clash.setAlbum(u"Import Test"_s); clash.setTitle(u"Incoming"_s); clash.setDiscNumber(u"1"_s); clash.setTrackNumber(u"1"_s);
        QVERIFY(loader->writeTrackMetadata(clash,AudioReader::Metadata)); library.scanTracks({clash}); QTest::qWait(4000);
        QCOMPARE(library.trackForId(id).filepath(),written); QVERIFY(QFile::exists(written)); QVERIFY(QFile::exists(imported));
        QCOMPARE(library.tracks().size(),size_t(2));
        auto* pluginInterface=qobject_cast<Plugin*>(object); QVERIFY(pluginInterface); pluginInterface->shutdown();
        provider.reset();
        // Prevent QObject children outliving their library dependencies in this harness.
        pluginOwner.reset();
        // Simulate a process exit after rename but before the asynchronous database commit.
        settings.fileSet("FileOps/Managed/Enabled",false);
        settings.set<Settings::Core::Internal::MonitorLibraryDirectories>(false);
        settings.set<Settings::Core::Internal::MonitorTrackFiles>(false);
        QTest::qWait(100);
        const QString recovered=root+u"/Recovery/recovered.mp3"_s; QVERIFY(QDir{}.mkpath(QFileInfo(recovered).absolutePath()));
        const QFileInfo originalInfo(imported); const auto recoverySize=originalInfo.size(); const auto recoveryTime=originalInfo.lastModified().toMSecsSinceEpoch();
        QVERIFY(QFile::rename(imported,recovered));
        const QString journal=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+u"/managed-fileops/pending.json"_s;
        QFile recoveryFile(journal); QVERIFY(recoveryFile.open(QIODevice::WriteOnly));
        QJsonObject recovery{{u"id"_s,incomingId},{u"source"_s,imported},{u"destination"_s,recovered},{u"root"_s,root},{u"copy"_s,false},
            {u"size"_s,QString::number(recoverySize)},{u"modified"_s,QString::number(recoveryTime)}};
        recoveryFile.write(QJsonDocument(recovery).toJson()); recoveryFile.close();
        object=plugin.instance(); QVERIFY(object); pluginOwner.reset(object);
        core=qobject_cast<CorePlugin*>(object); QVERIFY(core); core->initialise(context);
        QTRY_COMPARE_WITH_TIMEOUT(library.trackForId(incomingId).filepath(),recovered,10000);
        QVERIFY(QFile::exists(recovered)); QVERIFY(!QFile::exists(journal)); QCOMPARE(library.tracks().size(),size_t(2));
        qobject_cast<Plugin*>(object)->shutdown(); pluginOwner.reset();
        qInfo()<<"Verified real MP3 tags, external tag monitoring, stable playlist IDs, playing-file deferral, automatic retry after a Windows file lock, pending writes, imports, collisions and interrupted-move recovery.";
    }
};
int main(int argc,char** argv) {
    QApplication app(argc,argv); if(argc!=4) return 2;
    QCoreApplication::setApplicationName(u"fooyin-managed-test-%1"_s.arg(QCoreApplication::applicationPid()));
    QStandardPaths::setTestModeEnabled(true);
#ifdef Q_OS_WIN
    app.setFont(QFont(u"Segoe UI"_s,10));
#else
    app.setFont(QFont(u"Sans"_s,10));
#endif
    ScriptRegistry::instance().finalise();
    dllPath=QString::fromLocal8Bit(argv[1]); fixturePath=QString::fromLocal8Bit(argv[2]); screenshotPath=QString::fromLocal8Bit(argv[3]);
    IntegrationTest test;
    return QTest::qExec(&test,QStringList{u"integration_test"_s,u"-o"_s,u"integration-results.txt,txt"_s,u"-v2"_s});
}
#include "integration_test.moc"
