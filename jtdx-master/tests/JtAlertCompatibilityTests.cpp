#include <QtTest>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSettings>
#include <QTemporaryDir>
#include "JtAlertCompatibility.hpp"

class JtAlertCompatibilityTests : public QObject {
  Q_OBJECT
  QVariantMap sample() const {
    return {{"UDPServer","239.255.0.1"},{"UDPServerPort",2237},{"AcceptUDPRequests",true},
            {"MyCall","N0CALL"},{"MyGrid","AA00"},{"Rig","PRIVATE CAT"},
            {"MCP/Token","secret-token"},{"NTP/Enabled",true},{"LogDirectory","private-logs"}};
  }
  QString target(QDir const& root,QString const& application) const {
    auto id=JtAlertCompatibility::instanceId(application);
    return root.filePath(id+'/'+id+".ini");
  }
  QByteArray bytes(QString const& path) const {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return file.readAll();
  }
private slots:
  void identityAndTitleKeepRealVersionAndUniqueInstances() {
    QCOMPARE(JtAlertCompatibility::instanceId("JTDX-F"),QString("JTDX - JTDX-F"));
    QCOMPARE(JtAlertCompatibility::instanceId("JTDX-F - station two"),QString("JTDX - JTDX-F - station two"));
    auto title=JtAlertCompatibility::windowTitle("JTDX-F - station two","3.6.6","abcdef");
    QVERIFY(title.startsWith("JTDX - JTDX-F - station two v3.6.6 "));
    QVERIFY(title.indexOf("by Fluxmancer")>title.indexOf("v3.6.6"));
    QVERIFY(title.contains("abcdef"));QVERIFY(!title.contains("2.2.159"));
  }

  void realIniContainsOnlyProjectionAndOwnership() {
    QTemporaryDir temporary;QVERIFY(temporary.isValid());QDir root(temporary.path());
    auto source=root.filePath("JTDX-F.ini");
    QFile original(source);QVERIFY(original.open(QIODevice::WriteOnly));
    original.write("[Configuration]\nPrivate=value\n");original.close();auto before=bytes(source);
    QString error="old error";
    QVERIFY2(JtAlertCompatibility::writeDiscovery(root,"JTDX-F",source,sample(),&error),qPrintable(error));
    QVERIFY(error.isEmpty());QCOMPARE(bytes(source),before);
    QSettings projected(target(root,"JTDX-F"),QSettings::IniFormat);
    projected.beginGroup("Configuration");
    QCOMPARE(projected.allKeys(),QStringList({"AcceptUDPRequests","MyCall","MyGrid","UDPServer","UDPServerPort"}));
    QCOMPARE(projected.value("UDPServer").toString(),QString("239.255.0.1"));
    QCOMPARE(projected.value("UDPServerPort").toInt(),2237);QVERIFY(projected.value("AcceptUDPRequests").toBool());
    QCOMPARE(projected.value("MyCall").toString(),QString("N0CALL"));projected.endGroup();
    QCOMPARE(projected.value("JTDXFCompatibility/format").toInt(),1);
    QCOMPARE(projected.value("JTDXFCompatibility/applicationName").toString(),QString("JTDX-F"));
    QCOMPARE(projected.value("JTDXFCompatibility/sourceSettingsPath").toString(),source);
    QVERIFY(!bytes(target(root,"JTDX-F")).contains("secret-token"));
    QVERIFY(!bytes(target(root,"JTDX-F")).contains("PRIVATE CAT"));
    QVERIFY(!projected.contains("Configuration/NTP/Enabled"));
  }

  void unicodeInstancesAndOwnedRefresh() {
    QTemporaryDir temporary;QVERIFY(temporary.isValid());
    QDir root(temporary.path()+QString::fromUtf8("/配置资料"));
    auto first=QString::fromUtf8("JTDX-F - 二号台");auto second=QString("JTDX-F - collective");
    auto source=root.filePath(first+".ini");QString error;
    QVERIFY2(JtAlertCompatibility::writeDiscovery(root,first,source,sample(),&error),qPrintable(error));
    QVERIFY2(JtAlertCompatibility::writeDiscovery(root,second,root.filePath(second+".ini"),sample(),&error),qPrintable(error));
    auto secondBefore=bytes(target(root,second));
    auto changed=sample();changed["UDPServerPort"]=2240;changed["MyCall"]="W1AW";
    QVERIFY2(JtAlertCompatibility::writeDiscovery(root,first,source,changed,&error),qPrintable(error));
    QSettings projected(target(root,first),QSettings::IniFormat);
    QCOMPARE(projected.value("Configuration/MyCall").toString(),QString("W1AW"));
    QCOMPARE(projected.value("Configuration/UDPServerPort").toInt(),2240);
    QCOMPARE(projected.value("JTDXFCompatibility/applicationName").toString(),first);
    QCOMPARE(projected.value("JTDXFCompatibility/sourceSettingsPath").toString(),source);
    QCOMPARE(bytes(target(root,second)),secondBefore);
  }

  void refusesUnownedAndOtherOwnerWithoutChangingBytes() {
    QTemporaryDir temporary;QVERIFY(temporary.isValid());QDir root(temporary.path());
    auto path=target(root,"JTDX-F");QVERIFY(root.mkpath("JTDX - JTDX-F"));
    QFile unrelated(path);QVERIFY(unrelated.open(QIODevice::WriteOnly));
    unrelated.write("[Configuration]\nMyCall=OTHER\n");unrelated.close();auto before=bytes(path);
    QString error;
    QVERIFY(!JtAlertCompatibility::writeDiscovery(root,"JTDX-F",root.filePath("main.ini"),sample(),&error));
    QVERIFY(!error.isEmpty());QCOMPARE(bytes(path),before);
    QVERIFY(QFile::remove(path));
    QVERIFY(JtAlertCompatibility::writeDiscovery(root,"JTDX-F",root.filePath("owner.ini"),sample(),&error));
    before=bytes(path);
    QVERIFY(!JtAlertCompatibility::writeDiscovery(root,"JTDX-F",root.filePath("other.ini"),sample(),&error));
    QCOMPARE(bytes(path),before);
    // The real JTDX settings are outside this unique identity and untouched.
    QVERIFY(root.mkpath("JTDX"));QFile legacy(root.filePath("JTDX/JTDX.ini"));
    QVERIFY(legacy.open(QIODevice::WriteOnly));legacy.write("[Configuration]\nMyCall=LEGACY\n");legacy.close();
    auto legacyBefore=bytes(legacy.fileName());
    QVERIFY(JtAlertCompatibility::writeDiscovery(root,"JTDX-F",root.filePath("owner.ini"),sample(),&error));
    QCOMPARE(bytes(legacy.fileName()),legacyBefore);
    QVERIFY(!JtAlertCompatibility::writeDiscovery(root,"JTDX-F",path,sample(),&error));
    QCOMPARE(bytes(path),before);
  }

  void rejectsUnsafeNames_data() {
    QTest::addColumn<QString>("name");
    for(auto value:{"","..","JTDX-F/../JTDX","JTDX-F\\other","JTDX-F:stream","JTDX-F.","JTDX-F ","JTDX-F\nother"})
      QTest::newRow(value)<<QString::fromUtf8(value);
  }
  void rejectsUnsafeNames() {
    QFETCH(QString,name);QTemporaryDir temporary;QVERIFY(temporary.isValid());QDir root(temporary.path());QString error;
    QVERIFY(!JtAlertCompatibility::writeDiscovery(root,name,root.filePath("main.ini"),sample(),&error));
    QVERIFY(!error.isEmpty());QVERIFY(root.entryList(QDir::AllEntries|QDir::NoDotAndDotDot).isEmpty());
  }

  void rejectsLockedTargetAndNonDirectoryRoot() {
    QTemporaryDir temporary;QVERIFY(temporary.isValid());QDir root(temporary.path());QString error;
    auto path=target(root,"JTDX-F");QVERIFY(root.mkpath("JTDX - JTDX-F"));
    QLockFile lock(path+".lock");QVERIFY(lock.tryLock());
    QVERIFY(!JtAlertCompatibility::writeDiscovery(root,"JTDX-F",root.filePath("main.ini"),sample(),&error));
    QVERIFY(!QFileInfo::exists(path));lock.unlock();
    QFile blocked(root.filePath("file"));QVERIFY(blocked.open(QIODevice::WriteOnly));blocked.write("keep");blocked.close();
    QVERIFY(!JtAlertCompatibility::writeDiscovery(QDir(blocked.fileName()),"JTDX-F",root.filePath("main.ini"),sample(),&error));
    QCOMPARE(bytes(blocked.fileName()),QByteArray("keep"));
  }

  void rejectsSymlinkDirectoryAndFile() {
#ifdef Q_OS_WIN
    QSKIP("QFile::link creates a shortcut on Windows; symlink traversal is tested on POSIX.");
#else
    QTemporaryDir temporary;QVERIFY(temporary.isValid());QDir root(temporary.path());QString error;
    QVERIFY(root.mkpath("outside"));
    auto outside=root.filePath("outside");auto instance=root.filePath("JTDX - JTDX-F");
    QVERIFY(QFile::link(outside,instance));
    QVERIFY(!JtAlertCompatibility::writeDiscovery(root,"JTDX-F",root.filePath("main.ini"),sample(),&error));
    QVERIFY(QDir(outside).entryList(QDir::AllEntries|QDir::NoDotAndDotDot).isEmpty());
    QVERIFY(QFile::remove(instance));QVERIFY(root.mkpath("JTDX - JTDX-F"));
    QFile other(root.filePath("outside.ini"));QVERIFY(other.open(QIODevice::WriteOnly));other.write("unchanged");other.close();
    auto path=target(root,"JTDX-F");QVERIFY(QFile::link(other.fileName(),path));
    QVERIFY(!JtAlertCompatibility::writeDiscovery(root,"JTDX-F",root.filePath("main.ini"),sample(),&error));
    QCOMPARE(bytes(other.fileName()),QByteArray("unchanged"));
    QVERIFY(QFile::link(outside,root.filePath("alias")));
    QVERIFY(!JtAlertCompatibility::writeDiscovery(QDir(root.filePath("alias/sub")),"JTDX-F",root.filePath("main.ini"),sample(),&error));
#endif
  }
};

QTEST_GUILESS_MAIN(JtAlertCompatibilityTests)
#include "JtAlertCompatibilityTests.moc"
