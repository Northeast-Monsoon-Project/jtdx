#include "OperatorLogLoader.hpp"
#include <QtTest>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <future>

class OperatorLogCacheTests : public QObject {
  Q_OBJECT
  static OperatorLog::Record record(QString call="K1AAA",QString date="20260101",QString grid="AA00") {
    return {{"STATION_CALLSIGN","N0CALL"},{"CALL",call},{"MODE","FT8"},{"BAND","20m"},
      {"QSO_DATE",date},{"TIME_ON","120000"},{"MY_GRIDSQUARE",grid},{"GRIDSQUARE","FN42"}};
  }
  static bool write(QString const& path,QByteArray const& bytes) {
    QFile f(path);return f.open(QIODevice::WriteOnly) && f.write(bytes)==bytes.size();
  }
  static bool writeRecords(QString const& path,QList<OperatorLog::Record> const& records) {
    QString text="<ADIF_VER:5>3.1.8<EOH>\n";
    for(auto const& r:records)text+=OperatorLog::serialize(r);
    return write(path,text.toUtf8());
  }
  static QByteArray bytes(QString const& path) {QFile f(path);f.open(QIODevice::ReadOnly);return f.readAll();}
  static QByteArray serialized(QList<OperatorLog::Record> const& records) {
    QString text=QString::fromUtf8("Imported original — do not discard this header\r\n<ADIF_VER:5>3.1.8<EOH>\r\n");
    for(auto const& r:records)text+=OperatorLog::serialize(r);
    return text.toUtf8();
  }
  static QList<OperatorLog::Record> mixed(QString const& active="N0CALL") {
    auto own=record();own["STATION_CALLSIGN"]=" "+active.toLower()+" ";
    own["COMMENT"]="Keep <EOR> inside the field\nand the next line.";
    auto operated=record("K2BBB");operated["STATION_CALLSIGN"]=active;operated["OPERATOR"]="N9OPR";
    auto unspecified=record("K3CCC");unspecified.remove("STATION_CALLSIGN");unspecified["OPERATOR"]="N9OPR";
    auto badDate=record("K4DDD","not-a-date");badDate["STATION_CALLSIGN"]="N1CALL";
    auto missingContact=record();missingContact["STATION_CALLSIGN"]="N2CALL";missingContact.remove("CALL");
    auto differentSuffix=record("K6FFF");differentSuffix["STATION_CALLSIGN"]=active=="N0CALL"?"N0CALL/P":"N0CALL";
    auto foreign=record("K7GGG");foreign["STATION_CALLSIGN"]="N3CALL";
    return {own,operated,unspecified,badDate,missingContact,differentSuffix,foreign};
  }
  static QString mixedArchive(QString const& source,QByteArray const& original) {
    return QFileInfo(source).dir().filePath("old/"+QFileInfo(source).completeBaseName()+"-mixed-"
      +QString::fromLatin1(QCryptographicHash::hash(original,QCryptographicHash::Sha256).toHex())+".adi");
  }
  static QString updateJournal(QDir const& root) {
    return OperatorLog::directory("N0CALL",root).filePath(".operator-log-update.json");
  }
  static QJsonObject transaction(QDir const& root,OperatorLog::Record const& before,OperatorLog::Record const& after) {
    QJsonArray patches;auto oldBytes=OperatorLog::serialize(before).toUtf8();
    auto newBytes=OperatorLog::serialize(after).toUtf8();
    for(auto const& path:{OperatorLog::quarterFile("N0CALL",QDate::fromString(before.value("QSO_DATE"),"yyyyMMdd"),root),
                         OperatorLog::compatibilityFile("N0CALL",root)}) {
      auto content=bytes(path);auto offset=content.indexOf(oldBytes);
      patches.append(QJsonObject{{"file",QFileInfo(path).fileName()},{"offset",QString::number(offset)},
        {"total",QString::number(content.size())},{"before",QString::fromLatin1(oldBytes.toBase64())},
        {"after",QString::fromLatin1(newBytes.toBase64())},
        {"anchor",QString::fromLatin1(content.mid(qMax(0,offset-128),qMin(128,offset)).toBase64())},
        {"tail",offset+oldBytes.size()==content.size()}});
    }
    return {{"version",1},{"station","N0CALL"},{"patches",patches}};
  }
private slots:
  void finalUpdatesKeepStartAndSeparateLaterQsos() {
    QTemporaryDir temporary;QDir root(temporary.path());
    auto first=record("K1AAA","20260930");first["TIME_ON"]="235945";
    first["QSO_DATE_OFF"]="20260930";first["TIME_OFF"]="235958";first["RST_SENT"]="-10";
    first["COMMENT"]=QString::fromUtf8("保留 <EOR> 文本与\n换行");
    auto quarter=OperatorLog::quarterFile("N0CALL",QDate(2026,9,30),root);
    auto full=OperatorLog::compatibilityFile("N0CALL",root);QString error;
    QVERIFY2(OperatorLog::append(quarter,OperatorLog::serialize(first),&error),qPrintable(error));
    OperatorLogLoader loader;QVERIFY(loader.load("N0CALL",{},{},root).error.isEmpty());
    auto next=first;next["QSO_DATE_OFF"]="20261001";next["TIME_OFF"]="000013";next["RST_SENT"]="-8";
    QVERIFY2(OperatorLog::updateQso("N0CALL",first,next,&error,root),qPrintable(error));
    QCOMPARE(OperatorLog::read(quarter),QList<OperatorLog::Record>{next});
    QCOMPARE(OperatorLog::read(full),OperatorLog::read(quarter));
    auto nextQuarter=OperatorLog::quarterFile("N0CALL",QDate(2026,10,1),root);QVERIFY(!QFile::exists(nextQuarter));
    auto changed=loader.load("N0CALL",{},{},root);QVERIFY(changed.error.isEmpty());QVERIFY(!changed.cacheHit);
    QCOMPARE(changed.book.get_qso_count(),1);QVERIFY(loader.load("N0CALL",{},{},root).cacheHit);
    auto later=record("K1AAA","20261001");later["TIME_ON"]="000100";
    QVERIFY2(OperatorLog::append(nextQuarter,OperatorLog::serialize(later),&error),qPrintable(error));
    auto laterBytes=bytes(nextQuarter);next["TIME_OFF"]="000043";next["COMMENT"]+=QString::fromUtf8("，最后一轮");
    QVERIFY2(OperatorLog::updateQso("N0CALL",first,next,&error,root),qPrintable(error));
    QCOMPARE(OperatorLog::read(quarter),QList<OperatorLog::Record>{next});
    QCOMPARE(OperatorLog::read(full),(QList<OperatorLog::Record>{next,later}));
    QCOMPARE(bytes(nextQuarter),laterBytes);QVERIFY(!QFile::exists(updateJournal(root)));
  }
  void finalTailUpdatesDoNotSynchronizeUnrelatedHistory() {
    QTemporaryDir temporary;QDir root(temporary.path());QString error;
    auto original=record();auto path=OperatorLog::quarterFile("N0CALL",QDate(2026,1,1),root);
    QVERIFY(OperatorLog::append(path,OperatorLog::serialize(original),&error));
    // A normal update must not parse, rebuild or rewrite unrelated quarters.
    auto unrelated=OperatorLog::quarterFile("N0CALL",QDate(2025,1,1),root);
    QVERIFY(write(unrelated,"external import still being written"));
    QFile pinned(path);QVERIFY(pinned.open(QIODevice::ReadOnly));
    for(int i=0;i<3;++i) {
      auto next=original;next["TIME_OFF"]=QString("1200%1").arg(i*15,2,10,QChar('0'));
      next["COMMENT"]=QString(i*10,'x');
      QVERIFY2(OperatorLog::updateQso("N0CALL",original,next,&error,root),qPrintable(error));
      QCOMPARE(OperatorLog::read(path),QList<OperatorLog::Record>{next});
      QCOMPARE(OperatorLog::read(OperatorLog::compatibilityFile("N0CALL",root)),OperatorLog::read(path));
      // An already open read handle sees the changed tail: the normal path
      // did not replace/copy the entire potentially large quarterly file.
      QVERIFY(pinned.seek(0));QVERIFY(pinned.readAll().contains(OperatorLog::serialize(next).toUtf8()));
    }
    QCOMPARE(bytes(unrelated),QByteArray("external import still being written"));
  }
  void finalUpdatePreservesExternalFieldsAndRecordOrder() {
    QTemporaryDir temporary;QDir root(temporary.path());QString error;
    auto first=record();first["COMMENT"]="Original comment";first["RST_SENT"]="-10";
    auto later=record("K2BBB");later["TIME_ON"]="130000";
    auto path=OperatorLog::quarterFile("N0CALL",QDate(2026,1,1),root);
    auto full=OperatorLog::compatibilityFile("N0CALL",root);
    QVERIFY(OperatorLog::append(path,OperatorLog::serialize(first),&error));
    auto quarter=first;quarter["APP_OTHER_NOTE"]=QString::fromUtf8("季度备注 <EOR>");
    auto compatible=first;compatible["NAME"]="External name";compatible["COMMENT"]="Externally edited comment";
    QVERIFY(writeRecords(path,{quarter,later}));QVERIFY(writeRecords(full,{compatible,later}));
    auto next=first;next["TIME_OFF"]="120030";next["RST_SENT"]="-8";
    QVERIFY2(OperatorLog::updateQso("N0CALL",first,next,&error,root),qPrintable(error));
    next["APP_OTHER_NOTE"]=quarter["APP_OTHER_NOTE"];next["NAME"]=compatible["NAME"];next["COMMENT"]=compatible["COMMENT"];
    QCOMPARE(OperatorLog::read(path),(QList<OperatorLog::Record>{next,later}));
    QCOMPARE(OperatorLog::read(full),OperatorLog::read(path));
    auto final=first;final["TIME_OFF"]="120045";
    QVERIFY2(OperatorLog::updateQso("N0CALL",first,final,&error,root),qPrintable(error));
    next["TIME_OFF"]="120045";next["RST_SENT"]="-10";
    QCOMPARE(OperatorLog::read(full),(QList<OperatorLog::Record>{next,later}));
  }
  void finalUpdateRejectsChangedIdentity_data() {
    QTest::addColumn<QString>("field");QTest::addColumn<QString>("value");
    QTest::newRow("later-time")<<QString("TIME_ON")<<QString("120015");
    QTest::newRow("later-date")<<QString("QSO_DATE")<<QString("20260401");
    QTest::newRow("other-station")<<QString("STATION_CALLSIGN")<<QString("N1CALL");
    QTest::newRow("other-contact")<<QString("CALL")<<QString("K2BBB");
    QTest::newRow("other-mode")<<QString("MODE")<<QString("FT4");
    QTest::newRow("other-band")<<QString("BAND")<<QString("40m");
    QTest::newRow("invalid-end-date")<<QString("QSO_DATE_OFF")<<QString("20260230");
    QTest::newRow("invalid-end-time")<<QString("TIME_OFF")<<QString("250000");
    QTest::newRow("end-before-start")<<QString("TIME_OFF")<<QString("110000");
  }
  void finalUpdateRejectsChangedIdentity() {
    QFETCH(QString,field);QFETCH(QString,value);QTemporaryDir temporary;QDir root(temporary.path());QString error;
    auto original=record();auto path=OperatorLog::quarterFile("N0CALL",QDate(2026,1,1),root);
    auto full=OperatorLog::compatibilityFile("N0CALL",root);
    QVERIFY(OperatorLog::append(path,OperatorLog::serialize(original),&error));
    auto before=bytes(path),beforeFull=bytes(full);auto next=original;next[field]=value;
    QVERIFY(!OperatorLog::updateQso("N0CALL",original,next,&error,root));QVERIFY(!error.isEmpty());
    QCOMPARE(bytes(path),before);QCOMPARE(bytes(full),beforeFull);QVERIFY(!QFile::exists(updateJournal(root)));
  }
  void finalUpdateMissingAmbiguousOrUnwritableIsNonDestructive_data() {
    QTest::addColumn<int>("kind");
    QTest::newRow("missing-quarter-record")<<0;QTest::newRow("missing-full-record")<<1;
    QTest::newRow("ambiguous-record")<<2;QTest::newRow("invalid-adif")<<3;
    QTest::newRow("journal-cannot-be-created")<<4;
  }
  void finalUpdateMissingAmbiguousOrUnwritableIsNonDestructive() {
    QFETCH(int,kind);QTemporaryDir temporary;QDir root(temporary.path());QString error;
    auto original=record();auto next=original;next["TIME_OFF"]="120030";
    auto path=OperatorLog::quarterFile("N0CALL",QDate(2026,1,1),root);
    auto full=OperatorLog::compatibilityFile("N0CALL",root);
    QVERIFY(OperatorLog::append(path,OperatorLog::serialize(original),&error));
    if(kind==0)QVERIFY(writeRecords(path,{record("K2BBB")}));
    if(kind==1)QVERIFY(writeRecords(full,{record("K2BBB")}));
    if(kind==2)QVERIFY(writeRecords(full,{original,original}));
    if(kind==3)QVERIFY(write(full,"<CALL:999>broken"));
    if(kind==4)QVERIFY(QDir().mkpath(updateJournal(root)));
    auto before=bytes(path),beforeFull=bytes(full);
    QVERIFY(!OperatorLog::updateQso("N0CALL",original,next,&error,root));QVERIFY(!error.isEmpty());
    QCOMPARE(bytes(path),before);QCOMPARE(bytes(full),beforeFull);
  }
  void pendingFinalUpdateRecoversBeforeCache_data() {
    QTest::addColumn<int>("state");
    QTest::newRow("journal-only-old-cache")<<0;QTest::newRow("quarter-committed")<<1;
    QTest::newRow("both-committed")<<2;QTest::newRow("torn-quarter-tail")<<3;
  }
  void pendingFinalUpdateRecoversBeforeCache() {
    QFETCH(int,state);QTemporaryDir temporary;QDir root(temporary.path());QString error;
    auto original=record();auto next=original;next["TIME_OFF"]="120030";next["COMMENT"]="latest final transmission";
    auto path=OperatorLog::quarterFile("N0CALL",QDate(2026,1,1),root);
    auto full=OperatorLog::compatibilityFile("N0CALL",root);
    QVERIFY(OperatorLog::append(path,OperatorLog::serialize(original),&error));
    OperatorLogLoader loader;QVERIFY(loader.load("N0CALL",{},{},root).error.isEmpty());
    QVERIFY(loader.load("N0CALL",{},{},root).cacheHit);
    auto transactionObject=transaction(root,original,next);
    QVERIFY(write(updateJournal(root),QJsonDocument(transactionObject).toJson()));
    auto oldBody=OperatorLog::serialize(original).toUtf8(),newBody=OperatorLog::serialize(next).toUtf8();
    auto originalQuarter=bytes(path),originalFull=bytes(full);
    auto prefix=originalQuarter.left(originalQuarter.indexOf(oldBody));
    if(state==1 || state==2)QVERIFY(write(path,prefix+newBody));
    if(state==2)QVERIFY(write(full,originalFull.left(originalFull.indexOf(oldBody))+newBody));
    if(state==3) {
      int split=newBody.indexOf("GRIDSQUARE")+3;QVERIFY(split>3);
      QVERIFY(write(path,prefix+newBody.left(split)+oldBody.mid(split)));
    }
    auto recovered=loader.load("N0CALL",{},{},root);QVERIFY2(recovered.error.isEmpty(),qPrintable(recovered.error));
    QVERIFY(!recovered.cacheHit);QCOMPARE(recovered.book.get_qso_count(),1);
    QCOMPARE(OperatorLog::read(path),QList<OperatorLog::Record>{next});QCOMPARE(OperatorLog::read(full),OperatorLog::read(path));
    QVERIFY(!QFile::exists(updateJournal(root)));QVERIFY(loader.load("N0CALL",{},{},root).cacheHit);
  }
  void pendingFinalUpdateRefusesExternalChangesOrUnsafeJournal_data() {
    QTest::addColumn<int>("kind");
    QTest::newRow("external-edit")<<0;QTest::newRow("path-escape")<<1;
    QTest::newRow("different-operator")<<2;QTest::newRow("wrong-identity")<<3;
  }
  void pendingFinalUpdateRefusesExternalChangesOrUnsafeJournal() {
    QFETCH(int,kind);QTemporaryDir temporary;QDir root(temporary.path());QString error;
    auto original=record();auto next=original;next["TIME_OFF"]="120030";
    auto path=OperatorLog::quarterFile("N0CALL",QDate(2026,1,1),root);
    auto full=OperatorLog::compatibilityFile("N0CALL",root);
    QVERIFY(OperatorLog::append(path,OperatorLog::serialize(original),&error));
    auto tx=transaction(root,original,next);
    if(kind==0){auto foreign=original;foreign["NAME"]="External edit after interruption";QVERIFY(writeRecords(full,{foreign}));}
    if(kind==1 || kind==3) {
      auto array=tx.value("patches").toArray();auto first=array[0].toObject();
      if(kind==1)first["file"]="../outside.adi";
      else {auto foreign=next;foreign["TIME_ON"]="130000";first["after"]=QString::fromLatin1(OperatorLog::serialize(foreign).toUtf8().toBase64());}
      array[0]=first;tx["patches"]=array;
    }
    if(kind==2)tx["station"]="N1CALL";
    QVERIFY(write(updateJournal(root),QJsonDocument(tx).toJson()));
    auto before=bytes(path),beforeFull=bytes(full),journal=bytes(updateJournal(root));
    OperatorLogLoader loader;auto rejected=loader.load("N0CALL",{},{},root);QVERIFY(!rejected.error.isEmpty());
    QCOMPARE(bytes(path),before);QCOMPARE(bytes(full),beforeFull);QCOMPARE(bytes(updateJournal(root)),journal);
    QVERIFY(!QFile::exists(root.filePath("outside.adi")));
  }
  void caseVariantWorkingNamesImportSafely_data() {
    QTest::addColumn<bool>("quarterSource");
    QTest::newRow("uppercase-complete")<<false;QTest::newRow("uppercase-quarter")<<true;
  }
  void caseVariantWorkingNamesImportSafely() {
    QFETCH(bool,quarterSource);QTemporaryDir temporary;QDir root(temporary.path());
    auto full=OperatorLog::compatibilityFile("N0CALL",root);
    auto source=QFileInfo(full).dir().filePath(quarterSource?"N0CALL2026S1.ADI":"WSJTX_LOG.ADI");
    auto original=serialized(mixed());QVERIFY(write(source,original));
    auto result=OperatorLog::importFiles("N0CALL",{source},root);
    QVERIFY2(result.errors.isEmpty(),qPrintable(result.errors.join("; ")));
    QCOMPARE(result.skipped,4);QCOMPARE(OperatorLog::read(full).size(),3);
    QDir old(QFileInfo(full).dir().filePath("old"));auto archives=old.entryList(QDir::Files);
    QCOMPARE(archives.size(),1);QCOMPARE(bytes(old.filePath(archives.first())),original);
    OperatorLogLoader loader;auto loaded=loader.load("N0CALL",{},{},root);
    QVERIFY2(loaded.error.isEmpty(),qPrintable(loaded.error));QCOMPARE(loaded.book.get_qso_count(),3);
  }
  void mixedWorkingLogsAreCleanedBeforeIndexing_data() {
    QTest::addColumn<QString>("active");QTest::addColumn<int>("sourceKind");
    for(auto const& call:{QString("N0CALL"),QString("N0CALL/P")})
      for(int kind=0;kind<3;++kind)
        QTest::newRow(qPrintable(call+"-"+QString::number(kind)))<<call<<kind;
  }
  void mixedWorkingLogsAreCleanedBeforeIndexing() {
    QFETCH(QString,active);QFETCH(int,sourceKind);
    QTemporaryDir temporary;QDir root(temporary.path());OperatorLogLoader loader;
    auto quarter=OperatorLog::quarterFile(active,QDate(2026,1,1),root);
    auto full=OperatorLog::compatibilityFile(active,root);
    auto original=serialized(mixed(active));QStringList sources;
    if(sourceKind!=0)sources.append(quarter);
    if(sourceKind!=1)sources.append(full);
    for(auto const& source:sources)QVERIFY(write(source,original));
    auto loaded=loader.load(active,{},{},root);
    QVERIFY2(loaded.error.isEmpty(),qPrintable(loaded.error));QVERIFY(loaded.synchronized);QVERIFY(!loaded.cacheHit);
    QCOMPARE(loaded.book.get_qso_count(),3);
    QString error;auto complete=OperatorLog::read(full,&error);QVERIFY2(error.isEmpty(),qPrintable(error));
    QCOMPARE(complete.size(),3);QCOMPARE(OperatorLog::read(quarter,&error),complete);QVERIFY(error.isEmpty());
    QSet<QString> contacts;
    for(auto const& row:complete) {
      QCOMPARE(row.value("STATION_CALLSIGN"),active);contacts.insert(row.value("CALL"));
      if(row.value("CALL")=="K1AAA")QCOMPARE(row.value("COMMENT"),mixed(active).first().value("COMMENT"));
      if(row.value("CALL")=="K2BBB" || row.value("CALL")=="K3CCC")QCOMPARE(row.value("OPERATOR"),QString("N9OPR"));
    }
    QCOMPARE(contacts,QSet<QString>({"K1AAA","K2BBB","K3CCC"}));
    for(auto const& source:sources)QCOMPARE(bytes(mixedArchive(source,original)),original);
    QDir old(QFileInfo(full).dir().filePath("old"));auto archived=old.entryList(QDir::Files);
    QCOMPARE(archived.size(),sources.size());
    auto beforeFull=bytes(full),beforeQuarter=bytes(quarter);
    auto again=loader.load(active,{},{},root);QVERIFY(again.cacheHit);QVERIFY(!again.synchronized);
    QCOMPARE(again.book.get_qso_count(),3);QCOMPARE(old.entryList(QDir::Files),archived);
    QCOMPARE(bytes(full),beforeFull);QCOMPARE(bytes(quarter),beforeQuarter);
    // A restart verifies the clean files without archiving them a second time.
    OperatorLogLoader restarted;QVERIFY(restarted.load(active,{},{},root).error.isEmpty());
    QCOMPARE(old.entryList(QDir::Files),archived);
    for(auto const& other:{QString("N1CALL"),QString("N2CALL"),QString("N3CALL"),QString("N9OPR")})
      QVERIFY(!QFileInfo(root.filePath(other)).exists());
    QVERIFY(!QFileInfo(root.filePath(active=="N0CALL"?"N0CALL_P":"N0CALL")).exists());
    // Normal recording must still append to both of the sanitized copies.
    auto next=record("K8HHH");next["STATION_CALLSIGN"]=active;
    QVERIFY2(OperatorLog::append(quarter,OperatorLog::serialize(next),&error),qPrintable(error));
    QCOMPARE(OperatorLog::read(full,&error),OperatorLog::read(quarter,&error));
    QCOMPARE(OperatorLog::read(full,&error).size(),4);
    auto appended=loader.load(active,{},{},root);QVERIFY(appended.error.isEmpty());QVERIFY(!appended.cacheHit);
    QCOMPARE(appended.book.get_qso_count(),4);QCOMPARE(old.entryList(QDir::Files),archived);
  }
  void mixedBackupFailureKeepsAllWorkingFiles_data() {
    QTest::addColumn<bool>("explicitImport");
    QTest::newRow("automatic-load")<<false;QTest::newRow("explicit-import")<<true;
  }
  void mixedBackupFailureKeepsAllWorkingFiles() {
    QFETCH(bool,explicitImport);
    QTemporaryDir temporary;QDir root(temporary.path());OperatorLogLoader loader;
    auto quarter=OperatorLog::quarterFile("N0CALL",QDate(2026,1,1),root);
    auto full=OperatorLog::compatibilityFile("N0CALL",root);
    auto source=explicitImport?root.filePath("incoming.adi"):full;
    QVERIFY(writeRecords(quarter,{record("W1KEP")}));QVERIFY(writeRecords(full,{record("W2KEP")}));
    auto original=serialized(mixed());QVERIFY(write(source,original));
    auto beforeQuarter=bytes(quarter),beforeFull=bytes(full);
    auto old=QFileInfo(full).dir().filePath("old");QVERIFY(write(old,"not a directory"));
    if(explicitImport)QVERIFY(!OperatorLog::importFiles("N0CALL",{source},root).errors.isEmpty());
    else QVERIFY(!loader.load("N0CALL",{},{},root).error.isEmpty());
    QCOMPARE(bytes(source),original);QCOMPARE(bytes(quarter),beforeQuarter);QCOMPARE(bytes(full),beforeFull);
    QCOMPARE(bytes(old),QByteArray("not a directory"));
    QCOMPARE(OperatorLog::files("N0CALL",root),QStringList{quarter});
    QVERIFY(!QFileInfo(root.filePath("N1CALL")).exists());
  }
  void foreignOnlyWorkingLogBecomesAnEmptyOwnLog_data() {
    QTest::addColumn<bool>("quarterSource");
    QTest::newRow("complete")<<false;QTest::newRow("quarter")<<true;
  }
  void foreignOnlyWorkingLogBecomesAnEmptyOwnLog() {
    QFETCH(bool,quarterSource);
    QTemporaryDir temporary;QDir root(temporary.path());OperatorLogLoader loader;
    auto quarter=OperatorLog::quarterFile("N0CALL",QDate(2026,1,1),root);
    auto full=OperatorLog::compatibilityFile("N0CALL",root);auto source=quarterSource?quarter:full;
    auto foreign=mixed().mid(3);auto original=serialized(foreign);QVERIFY(write(source,original));
    auto loaded=loader.load("N0CALL",{},{},root);QVERIFY2(loaded.error.isEmpty(),qPrintable(loaded.error));
    QCOMPARE(loaded.book.get_qso_count(),0);QString error;
    QVERIFY(OperatorLog::read(full,&error).isEmpty());QVERIFY2(error.isEmpty(),qPrintable(error));
    if(quarterSource){QVERIFY(OperatorLog::read(quarter,&error).isEmpty());QVERIFY(error.isEmpty());}
    QCOMPARE(bytes(mixedArchive(source,original)),original);
    QVERIFY(loader.load("N0CALL",{},{},root).cacheHit);
    QCOMPARE(root.entryList(QDir::Dirs|QDir::NoDotAndDotDot),QStringList{"N0CALL"});
  }
  void explicitMixedImportKeepsOriginalAndOnlyAddsCurrentStation() {
    QTemporaryDir temporary;QDir root(temporary.path());OperatorLogLoader loader;
    auto source=root.filePath("incoming.adi");auto original=serialized(mixed());QVERIFY(write(source,original));
    auto imported=OperatorLog::importFiles("N0CALL",{source},root);
    QVERIFY2(imported.errors.isEmpty(),qPrintable(imported.errors.join("; ")));
    QCOMPARE(imported.imported,3);QCOMPARE(imported.skipped,4);QCOMPARE(imported.archived,1);
    QCOMPARE(bytes(source),original);QCOMPARE(bytes(root.filePath("N0CALL/old/incoming.adi")),original);
    QCOMPARE(root.entryList(QDir::Dirs|QDir::NoDotAndDotDot),QStringList{"N0CALL"});
    auto loaded=loader.load("N0CALL",{},{},root);QVERIFY(loaded.error.isEmpty());QCOMPARE(loaded.book.get_qso_count(),3);
    QDir old(root.filePath("N0CALL/old"));auto before=old.entryList(QDir::Files);
    auto retry=OperatorLog::importFiles("N0CALL",{source},root);
    QVERIFY(retry.errors.isEmpty());QCOMPARE(retry.imported,0);QCOMPARE(retry.duplicates,3);QCOMPARE(retry.skipped,4);
    QCOMPARE(old.entryList(QDir::Files),before);QCOMPARE(bytes(source),original);
  }
  void invalidCurrentRecordsKeepOriginals_data() {
    QTest::addColumn<int>("damage");QTest::addColumn<bool>("explicitImport");
    for(int damage=0;damage<3;++damage)
      for(bool explicitImport:{false,true})
        QTest::newRow(qPrintable(QString::number(damage)+(explicitImport?"-import":"-load")))<<damage<<explicitImport;
  }
  void externalUtf8LengthsPreserveRecordsAndBackups_data() {
    QTest::addColumn<QString>("name");QTest::addColumn<bool>("byteCounted");
    for(auto const& name:{QString::fromUtf8("José"),QString::fromUtf8("李华"),QString::fromUtf8("😀")})
      for(bool bytes:{false,true})
        QTest::newRow(qPrintable(name+(bytes?"-bytes":"-characters")))<<name<<bytes;
  }
  void externalUtf8LengthsPreserveRecordsAndBackups() {
    QFETCH(QString,name);QFETCH(bool,byteCounted);
    QTemporaryDir temporary;QDir root(temporary.path());
    auto source=root.filePath("unicode.adi");
    auto first=OperatorLog::serialize(record("K1AAA"));
    auto length=byteCounted?name.toUtf8().size():name.size();
    first.replace("<EOR>","<NAME:"+QString::number(length)+":S>"+name+"<EOR>");
    auto second=record("K2BBB");second["COMMENT"]=QString::fromUtf8("中文<EOR>literal<EOH>备注");
    auto original=QByteArray::fromHex("efbbbf")+(first+OperatorLog::serialize(second)).toUtf8();
    QVERIFY(write(source,original));
    auto imported=OperatorLog::importFiles("N0CALL",{source},root);
    QVERIFY2(imported.errors.isEmpty(),qPrintable(imported.errors.join("; ")));
    QCOMPARE(imported.imported,2);QCOMPARE(imported.archived,1);
    QCOMPARE(bytes(source),original);QCOMPARE(bytes(root.filePath("N0CALL/old/unicode.adi")),original);
    auto full=OperatorLog::compatibilityFile("N0CALL",root);
    auto quarter=OperatorLog::quarterFile("N0CALL",QDate(2026,1,1),root);
    QString error;auto records=OperatorLog::read(full,&error);QVERIFY2(error.isEmpty(),qPrintable(error));
    QCOMPARE(records.size(),2);QCOMPARE(OperatorLog::read(quarter),records);
    QCOMPARE(records.at(0).value("CALL"),QString("K1AAA"));QCOMPARE(records.at(0).value("NAME"),name);
    QCOMPARE(records.at(1).value("COMMENT"),second.value("COMMENT"));
    // Imported fields now have canonical character lengths. Final-message updates
    // must still target the same exact QSO without damaging adjacent Unicode data.
    auto replacement=records.at(0);replacement["TIME_OFF"]="120200";
    QVERIFY2(OperatorLog::updateQso("N0CALL",records.at(0),replacement,&error,root),qPrintable(error));
    auto revised=OperatorLog::read(full,&error);QVERIFY(error.isEmpty());
    QCOMPARE(revised.size(),2);QCOMPARE(revised.at(0),replacement);QCOMPARE(revised.at(1),records.at(1));
    QCOMPARE(OperatorLog::read(quarter),revised);
    auto again=OperatorLog::importFiles("N0CALL",{source},root);
    QVERIFY(again.errors.isEmpty());QCOMPARE(again.imported,0);QCOMPARE(again.duplicates,2);
  }
  void externalBrokenAsciiLengthsRemainRejected_data() {
    QTest::addColumn<int>("length");
    for(auto length:{5,8,9999})QTest::newRow(qPrintable(QString::number(length)))<<length;
  }
  void externalBrokenAsciiLengthsRemainRejected() {
    QFETCH(int,length);
    QTemporaryDir temporary;QDir root(temporary.path());
    auto full=OperatorLog::compatibilityFile("N0CALL",root);
    auto quarter=OperatorLog::quarterFile("N0CALL",QDate(2026,1,1),root);
    QVERIFY(writeRecords(full,{record("W1KEP")}));QVERIFY(writeRecords(quarter,{record("W1KEP")}));
    auto beforeFull=bytes(full),beforeQuarter=bytes(quarter);
    auto source=root.filePath("broken-length.adi");
    auto first=OperatorLog::serialize(record("K1AAA"));
    first.replace("<EOR>","<COMMENT:"+QString::number(length)+">oops<EOR>");
    auto original=(first+OperatorLog::serialize(record("K2BBB"))).toUtf8();
    QVERIFY(write(source,original));
    auto imported=OperatorLog::importFiles("N0CALL",{source},root);
    QVERIFY(!imported.errors.isEmpty());QCOMPARE(imported.imported,0);
    QCOMPARE(bytes(source),original);QCOMPARE(bytes(full),beforeFull);QCOMPARE(bytes(quarter),beforeQuarter);
    QString error;QVERIFY(OperatorLog::parse("<CALL:9999>K1AAA<EOR>",&error).isEmpty());
    QVERIFY(!error.isEmpty()); // The working/journal parser still rejects malformed input.
  }
  void invalidCurrentRecordsKeepOriginals() {
    QFETCH(int,damage);QFETCH(bool,explicitImport);
    QTemporaryDir temporary;QDir root(temporary.path());OperatorLogLoader loader;
    auto quarter=OperatorLog::quarterFile("N0CALL",QDate(2026,1,1),root);
    auto full=OperatorLog::compatibilityFile("N0CALL",root);
    QVERIFY(writeRecords(quarter,{record("W1KEP")}));QVERIFY(writeRecords(full,{record("W2KEP")}));
    auto source=explicitImport?root.filePath("broken.adi"):full;auto damaged=record();
    if(damage==0)damaged["QSO_DATE"]="20260230";
    else if(damage==1)damaged.remove("CALL");
    QByteArray original=damage==2?QByteArray("<STATION_CALLSIGN:6>N0CALL<CALL:999>broken<EOR>"):serialized({damaged});
    QVERIFY(write(source,original));auto beforeFull=bytes(full),beforeQuarter=bytes(quarter);
    if(explicitImport)QVERIFY(!OperatorLog::importFiles("N0CALL",{source},root).errors.isEmpty());
    else QVERIFY(!loader.load("N0CALL",{},{},root).error.isEmpty());
    QCOMPARE(bytes(source),original);QCOMPARE(bytes(full),beforeFull);QCOMPARE(bytes(quarter),beforeQuarter);
  }
  void importArchiveCollisionChecksExactBytes_data() {
    QTest::addColumn<bool>("conflictingHashFile");
    QTest::newRow("different-basename-backup")<<false;
    QTest::newRow("conflicting-hash-backup")<<true;
  }
  void importArchiveCollisionChecksExactBytes() {
    QFETCH(bool,conflictingHashFile);
    QTemporaryDir temporary;QDir root(temporary.path());
    auto quarter=OperatorLog::quarterFile("N0CALL",QDate(2026,1,1),root);
    auto full=OperatorLog::compatibilityFile("N0CALL",root);
    QVERIFY(writeRecords(quarter,{record("W1KEP")}));QVERIFY(writeRecords(full,{record("W1KEP")}));
    auto beforeFull=bytes(full),beforeQuarter=bytes(quarter);
    auto source=root.filePath("incoming.adi");auto original=serialized(mixed());QVERIFY(write(source,original));
    QVERIFY(root.mkpath("N0CALL/old"));auto named=root.filePath("N0CALL/old/incoming.adi");
    QVERIFY(write(named,"older source bytes"));
    auto hashed=root.filePath("N0CALL/old/incoming-"
      +QString::fromLatin1(QCryptographicHash::hash(original,QCryptographicHash::Sha256).toHex())+".adi");
    if(conflictingHashFile)QVERIFY(write(hashed,"unexpected backup bytes"));
    auto imported=OperatorLog::importFiles("N0CALL",{source},root);
    QCOMPARE(bytes(source),original);QCOMPARE(bytes(named),QByteArray("older source bytes"));
    if(conflictingHashFile) {
      QVERIFY(!imported.errors.isEmpty());QCOMPARE(bytes(hashed),QByteArray("unexpected backup bytes"));
      QCOMPARE(bytes(full),beforeFull);QCOMPARE(bytes(quarter),beforeQuarter);
    } else {
      QVERIFY2(imported.errors.isEmpty(),qPrintable(imported.errors.join("; ")));
      QCOMPARE(bytes(hashed),original);QCOMPARE(imported.imported,3);QCOMPARE(imported.skipped,4);
      QCOMPARE(OperatorLog::read(full).size(),4);
    }
  }
  void reuseAcrossOperatorsAndKeepValueCopies() {
    QTemporaryDir temporary;QDir root(temporary.path());OperatorLogLoader loader;
    auto quarter=OperatorLog::quarterFile("N0CALL",QDate(2026,1,1),root);
    auto full=OperatorLog::compatibilityFile("N0CALL",root);
    QVERIFY(writeRecords(quarter,{record()}));QVERIFY(writeRecords(full,{record("K2BBB")}));
    auto first=loader.load("N0CALL",{},{},root);QVERIFY2(first.error.isEmpty(),qPrintable(first.error));
    QVERIFY(first.synchronized);QVERIFY(!first.cacheHit);QCOMPARE(first.book.get_qso_count(),2);
    QCOMPARE(OperatorLog::read(full).size(),2);QCOMPARE(OperatorLog::read(quarter).size(),2);
    auto other=loader.load("N1CALL",{},{},root);QVERIFY(other.error.isEmpty());
    auto again=loader.load("N0CALL",{},{},root);QVERIFY(again.cacheHit);QVERIFY(!again.synchronized);
    again.book.addAsWorked("K3CCC","20m","FT8","20260101","FN42",{});
    QCOMPARE(again.book.get_qso_count(),3);
    auto unmodified=loader.load("N0CALL",{},{},root);QCOMPARE(unmodified.book.get_qso_count(),2);
  }
  void detectsSameSizeSameTimestampExternalEdit() {
    QTemporaryDir temporary;QDir root(temporary.path());OperatorLogLoader loader;
    auto full=OperatorLog::compatibilityFile("N0CALL",root);
    QVERIFY(writeRecords(full,{record()}));QVERIFY(loader.load("N0CALL",{},{},root).error.isEmpty());
    auto timestamp=QFileInfo(full).lastModified();auto edited=bytes(full);auto size=edited.size();
    edited.replace("K1AAA","K1AAB");QVERIFY(write(full,edited));QCOMPARE(QFileInfo(full).size(),qint64(size));
    QFile file(full);QVERIFY(file.open(QIODevice::ReadWrite));QVERIFY(file.setFileTime(timestamp,QFileDevice::FileModificationTime));file.close();
    QCOMPARE(QFileInfo(full).lastModified(),timestamp);
    auto changed=loader.load("N0CALL",{},{},root);QVERIFY2(changed.error.isEmpty(),qPrintable(changed.error));
    QVERIFY(!changed.cacheHit);QVERIFY(changed.synchronized);QCOMPARE(changed.book.get_qso_count(),2);
    QCOMPARE(OperatorLog::read(full).size(),2);QVERIFY(loader.load("N0CALL",{},{},root).cacheHit);
  }
  void appendImportAndMissingCompanionsInvalidate() {
    QTemporaryDir temporary;QDir root(temporary.path());OperatorLogLoader loader;
    auto quarter=OperatorLog::quarterFile("N0CALL",QDate(2026,1,1),root);
    auto full=OperatorLog::compatibilityFile("N0CALL",root);
    QVERIFY(writeRecords(quarter,{record()}));QVERIFY(loader.load("N0CALL",{},{},root).error.isEmpty());
    QString error;QVERIFY(OperatorLog::append(quarter,OperatorLog::serialize(record("K2BBB")),&error));
    auto appended=loader.load("N0CALL",{},{},root);QVERIFY(appended.error.isEmpty());QVERIFY(!appended.cacheHit);
    QCOMPARE(appended.book.get_qso_count(),2);
    auto importPath=root.filePath("incoming.adi");QVERIFY(writeRecords(importPath,{record("K3CCC")}));
    auto imported=OperatorLog::importFiles("N0CALL",{importPath},root);QVERIFY(imported.errors.isEmpty());
    auto loaded=loader.load("N0CALL",{},{},root);QVERIFY(loaded.error.isEmpty());QCOMPARE(loaded.book.get_qso_count(),3);
    QVERIFY(QFile::remove(full));auto restored=loader.load("N0CALL",{},{},root);
    QVERIFY(restored.error.isEmpty());QVERIFY(restored.synchronized);QCOMPARE(OperatorLog::read(full).size(),3);
    QVERIFY(QFile::remove(quarter));restored=loader.load("N0CALL",{},{},root);
    QVERIFY(restored.error.isEmpty());QCOMPARE(OperatorLog::read(quarter).size(),3);
  }
  void filtersRebuildWithoutResynchronizing() {
    QTemporaryDir temporary;QDir root(temporary.path());OperatorLogLoader loader;
    auto full=OperatorLog::compatibilityFile("N0CALL",root);
    QVERIFY(writeRecords(full,{record(),record("K2BBB","20260102","BB11")}));
    QVERIFY(loader.load("N0CALL",{},{},root).error.isEmpty());
    auto grid=loader.load("N0CALL","AA00",{},root);
    QVERIFY(grid.error.isEmpty());QVERIFY(!grid.cacheHit);QVERIFY(!grid.synchronized);QCOMPARE(grid.book.get_qso_count(),1);
    auto date=loader.load("N0CALL",{},"20260102000000",root);
    QVERIFY(date.error.isEmpty());QVERIFY(!date.synchronized);QCOMPARE(date.book.get_qso_count(),1);
    QVERIFY(loader.load("N0CALL",{},{},root).cacheHit);
  }
  void databaseContentAndFinalLotwLineInvalidate() {
    QTemporaryDir temporary;QDir root(temporary.path());OperatorLogLoader loader;
    auto country=root.filePath("cty.dat");
    QByteArray cty="Test country: 05: 08: NA: 0.0: 0.0: 0.0: K:\n K;\n";
    QVERIFY(write(country,cty));
    auto today=QDate::currentDate().toString(Qt::ISODate);
    QVERIFY(write(root.filePath("lotw-user-activity.csv"),("K1AAA,"+today).toUtf8()));
    QVERIFY(writeRecords(OperatorLog::compatibilityFile("N0CALL",root),{record()}));
    auto initial=loader.load("N0CALL",{},{},root);QVERIFY(initial.error.isEmpty());
    QString name,lotw;initial.book.getDXCC("K1AAA",name);QVERIFY(name.contains("Test country"));
    initial.book.getLOTW("K1AAA",lotw);QCOMPARE(lotw,today);
    auto timestamp=QFileInfo(country).lastModified();cty.replace("Test country","Next country");
    QVERIFY(write(country,cty));QFile f(country);QVERIFY(f.open(QIODevice::ReadWrite));QVERIFY(f.setFileTime(timestamp,QFileDevice::FileModificationTime));f.close();
    auto changed=loader.load("N0CALL",{},{},root);QVERIFY(changed.error.isEmpty());
    QVERIFY(!changed.cacheHit);QVERIFY(!changed.synchronized);name.clear();changed.book.getDXCC("K1AAA",name);QVERIFY(name.contains("Next country"));
    QVERIFY(write(root.filePath("lotw-user-activity.csv"),("K2BBB,"+today).toUtf8()));
    auto updated=loader.load("N0CALL",{},{},root);QVERIFY(updated.error.isEmpty());QVERIFY(!updated.cacheHit);
    updated.book.getLOTW("K1AAA",lotw);QVERIFY(lotw.isEmpty());updated.book.getLOTW("K2BBB",lotw);QCOMPARE(lotw,today);
  }
  void brokenLogDoesNotReturnStaleCache() {
    QTemporaryDir temporary;QDir root(temporary.path());OperatorLogLoader loader;
    auto full=OperatorLog::compatibilityFile("N0CALL",root);
    QVERIFY(writeRecords(full,{record()}));QVERIFY(loader.load("N0CALL",{},{},root).error.isEmpty());
    QVERIFY(write(full,"missing"));auto broken=loader.load("N0CALL",{},{},root);
    QVERIFY(!broken.error.isEmpty());QVERIFY(!broken.cacheHit);QCOMPARE(bytes(full),QByteArray("missing"));
    QVERIFY(writeRecords(full,{record("K2BBB")}));auto repaired=loader.load("N0CALL",{},{},root);
    QVERIFY(repaired.error.isEmpty());QCOMPARE(repaired.book.get_qso_count(),2);
  }
  void boundedCacheAndConcurrentLoads() {
    QTemporaryDir temporary;QDir root(temporary.path());OperatorLogLoader loader;
    for(int i=0;i<5;++i)QVERIFY(loader.load(QString("N%1CALL").arg(i),{},{},root).error.isEmpty());
    QVERIFY(!loader.load("N0CALL",{},{},root).cacheHit);QVERIFY(loader.load("N0CALL",{},{},root).cacheHit);
    loader.clear();
    auto one=std::async(std::launch::async,[&]{return loader.load("N0CALL",{},{},root);});
    auto two=std::async(std::launch::async,[&]{return loader.load("N0CALL",{},{},root);});
    auto a=one.get(),b=two.get();QVERIFY(a.error.isEmpty());QVERIFY(b.error.isEmpty());
    QCOMPARE(int(a.cacheHit)+int(b.cacheHit),1);
  }
  void truncatedCountryDatabaseDoesNotHang() {
    QTemporaryDir temporary;QDir root(temporary.path());OperatorLogLoader loader;
    QVERIFY(write(root.filePath("cty.dat"),"Incomplete: 05: 08: NA: 0: 0: 0: K:\n K,"));
    auto result=loader.load("N0CALL",{},{},root);QVERIFY(result.error.isEmpty());
    QString name;result.book.getDXCC("K1AAA",name);QVERIFY(!name.contains("Incomplete"));
  }
  void recordBudgetRetainsLatestAndEvictsOlder() {
    QTemporaryDir temporary;QDir root(temporary.path());OperatorLogLoader loader(3);
    auto records=QList<OperatorLog::Record>{record(),record("K2BBB")};
    QVERIFY(writeRecords(OperatorLog::compatibilityFile("N0CALL",root),records));
    for(auto& r:records)r["STATION_CALLSIGN"]="N1CALL";
    QVERIFY(writeRecords(OperatorLog::compatibilityFile("N1CALL",root),records));
    QVERIFY(loader.load("N0CALL",{},{},root).error.isEmpty());
    QVERIFY(loader.load("N1CALL",{},{},root).error.isEmpty());
    QVERIFY(loader.load("N1CALL",{},{},root).cacheHit);
    QVERIFY(!loader.load("N0CALL",{},{},root).cacheHit);
    OperatorLogLoader tiny(1);
    QVERIFY(tiny.load("N0CALL",{},{},root).error.isEmpty());
    QVERIFY(tiny.load("N0CALL",{},{},root).cacheHit); // latest stays even above budget
  }
};
QTEST_GUILESS_MAIN(OperatorLogCacheTests)
#include "OperatorLogCache.moc"
