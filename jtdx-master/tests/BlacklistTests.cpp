#include "Blacklist.hpp"
#include <QtTest>
#include <QFile>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QPushButton>
#include <QSaveFile>
#include <QTemporaryDir>

class BlacklistTests : public QObject {
  Q_OBJECT
  static bool write(QString const& path,QByteArray const& bytes) {
    QSaveFile f(path);return f.open(QIODevice::WriteOnly) && f.write(bytes)==bytes.size() && f.commit();
  }
  static QByteArray read(QString const& path) {QFile f(path);f.open(QIODevice::ReadOnly);return f.readAll();}
private slots:
  void persistentCrudAndMatching() {
    QTemporaryDir temp;QDir dir(temp.path());Blacklist list;
    QVERIFY(list.load("N0CALL",dir));QVERIFY(list.ready());QVERIFY(list.entries().isEmpty());
    QVERIFY(!QFile::exists(list.filePath()));
    QVERIFY(list.save({{" k1abc ",QString::fromUtf8("备注：测试")},{"K2BBB/P","Portable only"}}));
    QVERIFY(QFile::exists(dir.filePath("blacklist.json")));QVERIFY(!QFile::exists(dir.filePath("wsjtx_log.adi")));
    for(auto call:{"K1ABC","k1abc","K1ABC/P","EA8/K1ABC","EA8/K1ABC/P","<K1ABC>","<EA8/K1ABC/P>"})QVERIFY(list.matches(call));
    QVERIFY(!list.matches("K1ABC2"));QVERIFY(!list.matches("K2BBB"));QVERIFY(list.matches("K2BBB/P"));
    QVERIFY(!list.matches("K2BBB/QRP"));QVERIFY(!list.matches("EA8/K2BBB/P"));
    Blacklist restarted;QVERIFY(restarted.load("N0CALL",dir));QCOMPARE(restarted.entries().size(),2);
    QCOMPARE(restarted.entries().first().note,QString::fromUtf8("备注：测试"));
    QVERIFY(restarted.save({{"K3CCC","Edited"}}));QVERIFY(!restarted.matches("K1ABC"));QVERIFY(restarted.matches("K3CCC"));
    QVERIFY(restarted.save({}));QVERIFY(restarted.entries().isEmpty());
    QVERIFY(list.reload());QVERIFY(list.entries().isEmpty());
  }
  void operatorIsolationAndDirectoryCopy() {
    QTemporaryDir temp;QDir root(temp.path());root.mkpath("first");root.mkpath("second");root.mkpath("copied");Blacklist list;
    QVERIFY(list.load("N0CALL",QDir(root.filePath("first"))));QVERIFY(list.save({{"K1ABC","one"}}));
    auto original=read(list.filePath());
    QVERIFY(list.load("N1CALL",QDir(root.filePath("second"))));QVERIFY(!list.matches("K1ABC"));QVERIFY(list.save({{"K2BBB","two"}}));
    QCOMPARE(read(root.filePath("first/blacklist.json")),original);
    QVERIFY(QFile::copy(root.filePath("first/blacklist.json"),root.filePath("copied/blacklist.json")));
    QVERIFY(list.load("N2CALL",QDir(root.filePath("copied"))));QVERIFY(list.matches("K1ABC"));QVERIFY(!list.matches("K2BBB"));
    QVERIFY(!list.load({},root));QVERIFY(!list.ready());QVERIFY(list.entries().isEmpty());QVERIFY(!list.save({{"K3CCC",{}}}));
    QVERIFY(!QFile::exists(root.filePath("blacklist.json")));
  }
  void invalidInputsAndCorruptFileArePreserved() {
    QTemporaryDir temp;QDir dir(temp.path());Blacklist list;QVERIFY(list.load("N0CALL",dir));
    QString error;
    QVERIFY(!list.save({{"K1ABC",{}},{" k1abc ",{}}},&error));QVERIFY(!error.isEmpty());
    QVERIFY(!list.save({{"../other",{}}},&error));QVERIFY(!list.save({{"K1ABC",QString(513,'x')}},&error));
    QVERIFY(list.save({{"K1ABC","last good"}}));
    QByteArray invalid="{broken JSON";QVERIFY(write(list.filePath(),invalid));
    QVERIFY(!list.reload(&error));QVERIFY(!list.ready());QVERIFY(list.matches("K1ABC"));
    QVERIFY(!list.save({}));QCOMPARE(read(list.filePath()),invalid);
    Blacklist reopened;QVERIFY(!reopened.load("N0CALL",dir));QVERIFY(reopened.entries().isEmpty());
    auto future=QByteArray("{\"format\":\"JTDX-F blacklist\",\"version\":2,\"entries\":[]}");
    QVERIFY(write(list.filePath(),future));QVERIFY(!list.reload());QCOMPARE(read(list.filePath()),future);
    auto duplicated=QByteArray("{\"format\":\"JTDX-F blacklist\",\"version\":1,\"entries\":[{\"call\":\"K1ABC\"},{\"call\":\"k1abc\"}]}");
    QVERIFY(write(list.filePath(),duplicated));QVERIFY(!list.reload());QCOMPARE(read(list.filePath()),duplicated);
    QVERIFY(write(list.filePath(),QByteArray(1024*1024+1,'x')));QVERIFY(!list.reload());
    QCOMPARE(QFileInfo(list.filePath()).size(),qint64(1024*1024+1));
  }
  void externalChangesAndStaleDialogCannotOverwrite() {
    QTemporaryDir temp;QDir dir(temp.path());Blacklist list;QVERIFY(list.load("N0CALL",dir));
    QVERIFY(list.save({{"K1ABC","one"}}));auto revision=list.contentRevision();
    auto external=read(list.filePath());external.replace("one","two");QVERIFY(write(list.filePath(),external));
    QString error;QVERIFY(!list.save({{"K3CCC","stale"}},&error));QCOMPARE(read(list.filePath()),external);
    QVERIFY(list.reload());QVERIFY(list.contentRevision()!=revision);
    QVERIFY(!list.save({{"K3CCC","stale dialog"}},&error,revision));QCOMPARE(read(list.filePath()),external);
    QVERIFY(list.save({{"K3CCC","fresh"}},&error,list.contentRevision()));QVERIFY(list.matches("K3CCC"));
  }
  void watcherReloadsAtomicReplacementAndDeletion() {
    QTemporaryDir temp;QDir dir(temp.path());Blacklist list;QVERIFY(list.load("N0CALL",dir));
    QVERIFY(list.save({{"K1ABC","one"}}));QSignalSpy changes(&list,&Blacklist::changed);
    auto updated=read(list.filePath());updated.replace("K1ABC","K2BBB");QVERIFY(write(list.filePath(),updated));
    QTRY_VERIFY_WITH_TIMEOUT(list.matches("K2BBB"),2000);QVERIFY(!list.matches("K1ABC"));QVERIFY(changes.count()>0);
    QVERIFY(QFile::remove(list.filePath()));QTRY_VERIFY_WITH_TIMEOUT(list.entries().isEmpty(),2000);QVERIFY(list.ready());
    QVERIFY(write(list.filePath(),updated));QTRY_VERIFY_WITH_TIMEOUT(list.matches("K2BBB"),2000);
  }
  void dialogContainsEntriesAndMatchingHelp() {
    QTemporaryDir temp;Blacklist list;QVERIFY(list.load("N0CALL",QDir(temp.path())));
    QVERIFY(list.save({{"K1ABC","note"}}));BlacklistDialog dialog(&list);
    auto entries=dialog.findChild<QListWidget*>("blacklist_entries");QVERIFY(entries);QCOMPARE(entries->count(),1);
    QVERIFY(entries->toolTip().contains("K1ABC/P"));
    QCOMPARE(dialog.objectName(),QString("blacklist_manager"));
  }
  void staleDialogCannotWriteDifferentOperatorWithSameContent() {
    QTemporaryDir temp;QDir root(temp.path());root.mkpath("a");root.mkpath("b");Blacklist list;
    QVERIFY(list.load("N0CALL",QDir(root.filePath("a"))));QVERIFY(list.save({{"K1ABC","same"}}));
    auto original=read(list.filePath());auto revision=list.contentRevision();BlacklistDialog dialog(&list);
    QVERIFY(write(root.filePath("b/blacklist.json"),original));
    QVERIFY(list.load("N1CALL",QDir(root.filePath("b"))));QVERIFY(list.contentRevision()!=revision);
    QSignalSpy accepted(&dialog,&QDialog::accepted);
    auto buttons=dialog.findChild<QDialogButtonBox*>();QVERIFY(buttons);buttons->button(QDialogButtonBox::Ok)->click();
    QCOMPARE(accepted.count(),0);QCOMPARE(read(list.filePath()),original);
    QVERIFY(!list.save({{"K3CCC","stale"}},nullptr,revision));
  }
};
QTEST_MAIN(BlacklistTests)
#include "BlacklistTests.moc"
