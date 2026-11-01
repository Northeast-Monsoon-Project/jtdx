#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <limits>
#include "HamlibUpdateSupport.hpp"
#include "RigTelemetry.hpp"
#include "PcmTxQueue.hpp"
#include "HRDMessage.hpp"
class HamlibUpdateTests:public QObject {
  Q_OBJECT
  static void write(QString const& path,QByteArray const& data){QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write(data),qint64(data.size()));}
  static QByteArray read(QString const& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
  static QByteArray pe(){QByteArray b(512,0);b.replace(0,2,"MZ");qToLittleEndian<quint32>(64,reinterpret_cast<uchar*>(b.data()+60));b.replace(64,4,QByteArray("PE\0\0",4));qToLittleEndian<quint16>(0x8664,reinterpret_cast<uchar*>(b.data()+68));qToLittleEndian<quint16>(0x2000,reinterpret_cast<uchar*>(b.data()+86));qToLittleEndian<quint16>(0x20b,reinterpret_cast<uchar*>(b.data()+88));return b;}
private slots:
  void peHeaderValidation(){
    QTemporaryDir d;auto f=d.filePath("candidate.dll");QString error;
    QVERIFY(!HamlibUpdateSupport::validatePe(f,&error));
    write(f,pe());QVERIFY(HamlibUpdateSupport::validatePe(f,&error));
    auto malformed=pe();malformed[68]=char(0x4c);malformed[69]=char(1);write(f,malformed);QVERIFY(!HamlibUpdateSupport::validatePe(f,&error));
    malformed=pe();qToLittleEndian<quint32>(0xffffffff,reinterpret_cast<uchar*>(malformed.data()+60));write(f,malformed);QVERIFY(!HamlibUpdateSupport::validatePe(f,&error));
    malformed=pe();malformed[87]=0;write(f,malformed);QVERIFY(!HamlibUpdateSupport::validatePe(f,&error));
    write(f,"<html>download failed</html>");QVERIFY(!HamlibUpdateSupport::validatePe(f,&error));
  }
  void atomicUpdateAndRollback(){
    QTemporaryDir d;auto active=d.filePath("active"),next=d.filePath("next"),backup=d.filePath("backup");QString error;
    write(active,"original");write(next,"updated");write(backup,"older");
    QVERIFY2(HamlibUpdateSupport::replaceWithBackup(next,active,backup,&error),qPrintable(error));
    QCOMPARE(read(active),QByteArray("updated"));QCOMPARE(read(backup),QByteArray("original"));
    QVERIFY2(HamlibUpdateSupport::replaceWithBackup(backup,active,backup,&error),qPrintable(error));
    QCOMPARE(read(active),QByteArray("original"));QCOMPARE(read(backup),QByteArray("updated"));
    QCOMPARE(HamlibUpdateSupport::sha256(active),QString("0682c5f2076f099c34cfdd15a9e063849ed437a49677e6fcc5b4198c76575be5"));
  }
  void failedTransactionPreservesFiles(){
    QTemporaryDir d;auto active=d.filePath("active"),next=d.filePath("next"),backup=d.filePath("backup");QString error;
    write(active,"original");write(backup,"older");
    QVERIFY(!HamlibUpdateSupport::replaceWithBackup(next,active,backup,&error));
    QCOMPARE(read(active),QByteArray("original"));QCOMPARE(read(backup),QByteArray("older"));
    write(next,"updated");QDir().mkdir(d.filePath("directory"));
    QVERIFY(!HamlibUpdateSupport::replaceWithBackup(next,active,d.filePath("directory"),&error));
    QCOMPARE(read(active),QByteArray("original"));QCOMPARE(read(next),QByteArray("updated"));
  }
  void telemetryValues(){
    QCOMPARE(RigTelemetry::scaled(QString("2.50"),100,1,1000),250u);
    QCOMPARE(RigTelemetry::scaled(QString("2.51"),100,1,1000),251u);
    QCOMPARE(RigTelemetry::scaled(QString("3"),100,1,1000),300u);
    QCOMPARE(RigTelemetry::scaled(QString("2.567"),100,1,1000),257u);
    for(auto text:{"","NaN","inf","-3","1e30","garbage","0.8"})QCOMPARE(RigTelemetry::scaled(QString(text),100,1,1000),0u);
    QCOMPARE(RigTelemetry::scaled(std::numeric_limits<double>::infinity(),100,1,1000),0u);
  }
  void hrdFrames(){
    QString payload=QString("A")+QChar(0)+QString::fromUtf8("频率");
    auto frame=HRDMessage::make(payload);
    for(int i=0;i<frame.size();++i){auto p=HRDMessage::parse(frame.left(i));QVERIFY(p.valid);QVERIFY(!p.complete);}
    auto p=HRDMessage::parse(frame);QVERIFY(p.valid);QVERIFY(p.complete);QCOMPARE(p.payload,payload);
    frame[frame.size()-2]=1;QVERIFY(!HRDMessage::parse(frame).valid);
    frame=HRDMessage::make("test");frame[4]=0;QVERIFY(!HRDMessage::parse(frame).valid);
  }
  void pcmRequiresPttAndFinishesOnce(){
    PcmTxQueue queue;QByteArray pcm(6,0);qToLittleEndian<qint16>(32767,reinterpret_cast<uchar*>(pcm.data()));qToLittleEndian<qint16>(-32768,reinterpret_cast<uchar*>(pcm.data()+2));
    QVERIFY(queue.start(pcm));float samples[4]={1,1,1,1};
    QVERIFY(!queue.read(samples,4,false));for(auto s:samples)QCOMPARE(s,0.f);QVERIFY(queue.active());
    QVERIFY(!queue.read(samples,4,true));QCOMPARE(samples[0],32767/32768.f);QCOMPARE(samples[1],samples[0]);QCOMPARE(samples[2],-1.f);QCOMPARE(samples[3],-1.f);
    QVERIFY(queue.read(samples,4,true));QVERIFY(!queue.active());for(auto s:samples)QCOMPARE(s,0.f);
    QVERIFY(!queue.read(samples,4,true));QVERIFY(!queue.start(QByteArray(3,0)));
    QVERIFY(queue.start(pcm));queue.clear();QVERIFY(!queue.active());
    for(float level:{0.999f,0.499f}) {
      QVERIFY(queue.start(pcm));QVERIFY(!queue.read(samples,4,true,level*32768.f/32767.f));
      QVERIFY(qAbs(samples[0]-level)<0.000001f);QCOMPARE(samples[0],samples[1]);
      QVERIFY(qAbs(samples[2]+level*32768.f/32767.f)<0.000001f);QCOMPARE(samples[2],samples[3]);
    }
  }
};
QTEST_GUILESS_MAIN(HamlibUpdateTests)
#include "HamlibUpdateTests.moc"
