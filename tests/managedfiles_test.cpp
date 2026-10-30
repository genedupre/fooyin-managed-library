// SPDX-License-Identifier: GPL-3.0-or-later
#include "managedfiles.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <memory>
#endif
using namespace Fooyin::FileOps::Managed;
class ManagedFilesTest : public QObject {
    Q_OBJECT
    static void write(const QString& p, const QByteArray& b = "audio fixture") {
        QVERIFY(QDir{}.mkpath(QFileInfo(p).absolutePath()));
        QFile f(p); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(b), b.size());
    }
private Q_SLOTS:
#ifdef Q_OS_WIN
    void windowsTemporaryLockDoesNotCopyAndCanBeRetried() {
        QTemporaryDir t; const auto root=t.path()+"/music"; const auto source=root+"/old.mp3"; write(source);
        const auto p=plan(source,root,{"Artist","","Album","1","1","Title"},false);
        const auto native=QDir::toNativeSeparators(source);
        HANDLE handle=CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        QVERIFY(handle!=INVALID_HANDLE_VALUE);
        auto closer=[](void* h){CloseHandle(h);}; std::unique_ptr<void,decltype(closer)> lock(handle,closer);
        auto result=execute(p); QVERIFY(!result.succeeded); QVERIFY(result.retryable);
        QVERIFY(QFile::exists(source)); QVERIFY(!QFile::exists(p.destination));
        lock.reset(); result=execute(p); QVERIFY2(result.succeeded,qPrintable(result.error));
        QVERIFY(!QFile::exists(source)); QVERIFY(QFile::exists(p.destination));
    }
#endif
    void moveAndPrune() {
        QTemporaryDir t; QVERIFY(t.isValid());
        const auto root=t.path()+"/music"; const auto source=root+"/Taku Inoue/Tekken/old.mp3";
        write(source);
        auto p=plan(source, root, {"Game Soundtracks","Taku Inoue","Tekken","6","63","Siren's Call"},false);
        QVERIFY2(p.error.isEmpty(),qPrintable(p.error)); QVERIFY(!p.copy);
        QCOMPARE(p.destination,root+"/Game Soundtracks/Tekken/6-63 Siren's Call.mp3");
        QVERIFY(execute(p).succeeded); QVERIFY(!QFile::exists(source)); QVERIFY(QFile::exists(p.destination));
        QCOMPARE(removeEmptyParents(source,root).size(),2); QVERIFY(QDir(root).exists());
        QVERIFY(execute(plan(p.destination,root,{"Game Soundtracks","","Tekken","6","63","Siren's Call"},false)).succeeded);
    }
    void customPatternFlat() {
        QTemporaryDir t; QVERIFY(t.isValid());
        const auto root=t.path()+"/music"; const auto source=root+"/old.mp3"; write(source);
        auto p=plan(source,root,{"Artist","","Album","1","5","Song"},false,"%albumartist% - %album% - $num(%track%,3) %title%");
        QVERIFY2(p.error.isEmpty(),qPrintable(p.error));
        QCOMPARE(p.destination,root+"/Artist - Album - 005 Song.mp3");
    }
    void customPatternWithIf() {
        QTemporaryDir t; QVERIFY(t.isValid());
        const auto root=t.path()+"/music"; const auto source=root+"/old.mp3"; write(source);
        Tags withDisc{"Artist","","Album","2","5","Song"};
        auto p=plan(source,root,withDisc,false,"$if(%disc%,%albumartist%/%album%/Disc %disc%/$num(%track%,2) %title%,%albumartist%/%album%/$num(%track%,2) %title%)");
        QVERIFY2(p.error.isEmpty(),qPrintable(p.error));
        QCOMPARE(p.destination,root+"/Artist/Album/Disc 2/05 Song.mp3");
        Tags noDisc{"Artist","","Album","","5","Song"};
        p=plan(source,root,noDisc,false,"$if(%disc%,%albumartist%/%album%/Disc %disc%/$num(%track%,2) %title%,%albumartist%/%album%/$num(%track%,2) %title%)");
        QVERIFY2(p.error.isEmpty(),qPrintable(p.error));
        QCOMPARE(p.destination,root+"/Artist/Album/05 Song.mp3");
    }
    void evaluatePatternFunctions() {
        Tags tags{"Artist","","Album","","1","Song"};
        QCOMPARE(evaluatePattern("$lower(%albumartist%)",tags),QString("artist"));
        QCOMPARE(evaluatePattern("$upper(%albumartist%)",tags),QString("ARTIST"));
        QCOMPARE(evaluatePattern("$if2(%disc%,No Disc)",tags),QString("No Disc"));
        QCOMPARE(evaluatePattern("$if2(%albumartist%,fallback)",tags),QString("Artist"));
        QCOMPARE(evaluatePattern("$num(%track%,3)",tags),QString("001"));
        QCOMPARE(evaluatePattern("$trim(  hello  )",tags),QString("hello"));
        QCOMPARE(evaluatePattern("$replace(%albumartist%,Art,Mus)",tags),QString("Musist"));
    }
    void defaultPatternMatchesOldBehavior() {
        Tags tags{"Game Soundtracks","Taku Inoue","Tekken","6","63","Siren's Call"};
        auto result=evaluatePattern(DefaultPattern,tags);
        QCOMPARE(result,QString("Game Soundtracks/Tekken/6-63 Siren's Call"));
        Tags noDisc{"Artist","","Album","","5","Title"};
        result=evaluatePattern(DefaultPattern,noDisc);
        QCOMPARE(result,QString("Artist/Album/05 Title"));
    }
    void importKeepsOriginalAndCollisionRefusesOverwrite() {
        QTemporaryDir t; const auto root=t.path()+"/music"; QVERIFY(QDir{}.mkpath(root));
        const auto source=t.path()+"/incoming/song.mp3"; write(source);
        Tags tags{"","Artist","Album","1/2","3/4","Title"};
        QVERIFY(!plan(source,root,tags,false).error.isEmpty());
        auto p=plan(source,root,tags,true); QVERIFY(p.copy); QVERIFY(execute(p).succeeded);
        QVERIFY(QFile::exists(source)); QFile f(p.destination); QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(),QByteArray("audio fixture")); f.close();
        write(source,"different"); QVERIFY(!execute(p).succeeded);
        QVERIFY(!plan(source,root,tags,true).error.isEmpty());
        QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(),QByteArray("audio fixture"));
    }
    void cleanupPreservesNonemptyAndRoot() {
        QTemporaryDir t; const auto root=t.path()+"/music"; const auto old=root+"/A/B/song.mp3";
        write(root+"/A/B/.keep"); QVERIFY(removeEmptyParents(old,root).isEmpty());
        QVERIFY(removeEmptyParents(t.path()+"/outside/song.mp3",root).isEmpty());
        QVERIFY(QDir(root).exists()); QVERIFY(QFile::exists(root+"/A/B/.keep"));
    }
    void rejectsUnsafePathsAndTags() {
        QTemporaryDir t; auto root=t.path()+"/music"; auto source=root+"/old.mp3"; write(source);
        QVERIFY(!inside(t.path()+"/music-other/a",root));
        QVERIFY(!plan(source,root,{"A","","","1","1","T"},false).error.isEmpty());
        auto p=plan(source,root,{"../CON","","Album/../x","1","1","Hi: *?"},false);
        QVERIFY(p.error.isEmpty()); QVERIFY(inside(p.destination,root)); QVERIFY(!p.destination.contains("/../"));
        QCOMPARE(component("CON.txt"),QString("_CON.txt")); QCOMPARE(component(" dots... "),QString("dots"));
        QVERIFY(!plan(source,"relative",{"A","","B","1","1","T"},false).error.isEmpty());
        QVERIFY(!plan(source,root,{"A","","B","1","1",QString(300,'x')},false).error.isEmpty());
    }
    void companionTogglesAreIndependent() {
        QTemporaryDir t; const auto root=t.path()+"/music"; const auto old=root+"/Artist/Album/song.mp3";
        const auto cover=root+"/Artist/Album/Cover.jpg"; const auto nfo=root+"/Artist/Album/album.nfo";
        write(cover); write(nfo);
        QStringList recycled;
        const auto recycle=[&](const QString& p) { recycled.append(p); return QFile::remove(p); };
        QVERIFY(cleanupOldParents(old,root,{},recycle).isEmpty()); QVERIFY(recycled.isEmpty());
        auto messages=cleanupOldParents(old,root,{false,true},recycle);
        QCOMPARE(recycled,QStringList{nfo}); QVERIFY(QFile::exists(cover)); QVERIFY(!QFile::exists(nfo)); QCOMPARE(messages.size(),1);
        messages=cleanupOldParents(old,root,{true,false},recycle);
        QCOMPARE(recycled,QStringList({nfo,cover})); QVERIFY(!QDir(root+"/Artist").exists()); QVERIFY(QDir(root).exists()); QCOMPARE(messages.size(),3);
    }
    void companionsRemainWithMusicUnknownFilesAndSubfolders() {
        QTemporaryDir t; const auto root=t.path()+"/music"; const auto dir=root+"/Artist/Album"; const auto old=dir+"/moved.mp3";
        write(dir+"/Cover.jpg"); write(dir+"/album.nfo"); write(dir+"/still-here.mp3");
        int calls=0; const auto recycle=[&](const QString& p) { ++calls; return QFile::remove(p); };
        QVERIFY(cleanupOldParents(old,root,{true,true},recycle).isEmpty()); QCOMPARE(calls,0);
        QVERIFY(QFile::remove(dir+"/still-here.mp3")); write(dir+"/.keep");
        QVERIFY(cleanupOldParents(old,root,{true,true},recycle).isEmpty()); QCOMPARE(calls,0);
        QVERIFY(QFile::remove(dir+"/.keep")); QVERIFY(QDir{}.mkpath(dir+"/subfolder"));
        QVERIFY(cleanupOldParents(old,root,{true,true},recycle).isEmpty()); QCOMPARE(calls,0);
        QVERIFY(cleanupOldParents(root+"/fake.mp3",root,{true,true},recycle).isEmpty()); QCOMPARE(calls,0);
        QVERIFY(cleanupOldParents(t.path()+"/outside/old.mp3",root,{true,true},recycle).isEmpty()); QCOMPARE(calls,0);
    }
    void recycleFailureDoesNotDeleteOrFallBack() {
        QTemporaryDir t; const auto root=t.path()+"/music"; const auto dir=root+"/A/B"; write(dir+"/album.nfo");
        const auto messages=cleanupOldParents(dir+"/old.mp3",root,{false,true},[](const QString&) { return false; });
        QVERIFY(QFile::exists(dir+"/album.nfo")); QVERIFY(QDir(dir).exists()); QCOMPARE(messages.size(),1);
        QVERIFY(messages.front().startsWith("Could not recycle"));
    }
    void newMusicAppearingDuringCleanupStopsFurtherRemoval() {
        QTemporaryDir t; const auto root=t.path()+"/music"; const auto dir=root+"/A/B";
        write(dir+"/Cover.jpg"); write(dir+"/album.nfo");
        int calls=0;
        cleanupOldParents(dir+"/old.mp3",root,{true,true},[&](const QString& p) {
            ++calls;
            QFile incoming(dir+"/new.mp3"); if(!incoming.open(QIODevice::WriteOnly)) return false;
            incoming.write("new audio"); incoming.close();
            return QFile::remove(p);
        });
        QCOMPARE(calls,1); QVERIFY(QFile::exists(dir+"/new.mp3"));
        QVERIFY(QFile::exists(dir+"/Cover.jpg") || QFile::exists(dir+"/album.nfo"));
    }
};
QTEST_GUILESS_MAIN(ManagedFilesTest)
#include "managedfiles_test.moc"
