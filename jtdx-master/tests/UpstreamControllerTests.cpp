#include <QtTest>
#include <QDir>
#include <QSignalSpy>
#include <QFile>
#include "UpstreamController.hpp"
#include "PeriodAudioBuffer.hpp"

class UpstreamControllerTests:public QObject {
  Q_OBJECT
  QString fixture() const {
    return QDir(QCoreApplication::applicationDirPath()).filePath("fm_upstream_protocol_fixture"
#ifdef Q_OS_WIN
      ".exe"
#endif
    );
  }
  QJsonObject parameters() const {return {{"mode","Q65"},{"period",15}};}
private slots:
  void cleanup(){qunsetenv("FM_UPSTREAM_TEST_SCENARIO");}
  void invalidResponses_data() {
    QTest::addColumn<QByteArray>("scenario");
    for(auto name:{"bad_api","bad_id","bad_version","bad_commit","missing_mode","unmanaged_work_dir","missing_work_dir","malformed", "fractional_count","huge_count","bad_mode","bad_delay","wrong_path","wrong_size"})
      QTest::newRow(name)<<QByteArray(name);
  }
  void invalidResponses() {
    QFETCH(QByteArray,scenario);qputenv("FM_UPSTREAM_TEST_SCENARIO",scenario);
    UpstreamController controller(nullptr,fixture());controller.setContext("first");
    QSignalSpy errors(&controller,&UpstreamController::error),encoded(&controller,&UpstreamController::encoded);
    QVERIFY(controller.encode("CQ K1ABC FN20",parameters()));
    QTRY_COMPARE_WITH_TIMEOUT(errors.size(),1,3000);QCOMPARE(encoded.size(),0);QVERIFY(!controller.encoding());
  }
  void timeout() {
    qputenv("FM_UPSTREAM_TEST_SCENARIO","hang");
    UpstreamController controller(nullptr,fixture());controller.setContext("first");
    QSignalSpy errors(&controller,&UpstreamController::error);
    QVERIFY(controller.encode("CQ K1ABC FN20",parameters()));
    QTRY_VERIFY_WITH_TIMEOUT(controller.busy(),3000);controller.timeout_.start(25);
    QTRY_COMPARE_WITH_TIMEOUT(errors.size(),1,1000);QVERIFY(!controller.busy());
  }
  void cancelTransmit() {
    qputenv("FM_UPSTREAM_TEST_SCENARIO","delay_encode");
    UpstreamController controller(nullptr,fixture());controller.setContext("first");
    QSignalSpy errors(&controller,&UpstreamController::error),encoded(&controller,&UpstreamController::encoded);
    QVERIFY(controller.encode("CQ K1ABC FN20",parameters()));
    QTRY_VERIFY_WITH_TIMEOUT(controller.current_.request.value("op")=="encode",3000);
    controller.cancelTransmit();QVERIFY(!controller.encoding());
    QVERIFY(controller.encode("CQ W9XYZ EN37",parameters()));
    QTRY_COMPARE_WITH_TIMEOUT(encoded.size(),1,3000);
    QCOMPARE(encoded[0][0].toString(),QString("CQ W9XYZ EN37"));QCOMPARE(errors.size(),0);
  }
  void terminalCallbackCanQueueNextTransmit() {
    UpstreamController controller(nullptr,fixture());controller.setContext("first");
    QSignalSpy encoded(&controller,&UpstreamController::encoded),errors(&controller,&UpstreamController::error);
    bool accepted=false;
    connect(&controller,&UpstreamController::encoded,this,[&](QString const& message,QByteArray,QJsonObject){
      if(message=="CQ K1ABC FN20")accepted=controller.encode("CQ W9XYZ EN37",parameters());
    });
    QVERIFY(controller.encode("CQ K1ABC FN20",parameters()));
    QTRY_COMPARE_WITH_TIMEOUT(encoded.size(),2,3000);QVERIFY(accepted);QCOMPARE(errors.size(),0);
    QCOMPARE(encoded[0][1].toByteArray(),QByteArray::fromHex("010002000300"));
  }
  void contextChangesDuringDecode() {
    UpstreamController controller(nullptr,fixture());controller.setContext("first");
    QSignalSpy decoded(&controller,&UpstreamController::decoded),done(&controller,&UpstreamController::decodeFinished),errors(&controller,&UpstreamController::error);
    connect(&controller,&UpstreamController::decoded,this,[&](QJsonObject,QJsonObject request){
      if(request.value("context")=="first") {
        controller.setContext("second");QVERIFY(controller.decode(QByteArray(240,0),parameters()));
      }
    });
    QVERIFY(controller.decode(QByteArray(240,0),parameters()));
    QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3000);QCOMPARE(decoded.size(),2);QCOMPARE(errors.size(),0);
    QCOMPARE(done[0][0].toJsonObject().value("context").toString(),QString("second"));
  }
  void parentOwnsBackendFilesAcrossForcedStops() {
    qputenv("FM_UPSTREAM_TEST_SCENARIO","parent_scratch");
    QString parentPath;
    {
      UpstreamController controller(nullptr,fixture());parentPath=controller.directory_.path();
      const auto work=controller.directory_.filePath("backend"),ephemeris=work+"/JPLEPH";
      QSignalSpy encoded(&controller,&UpstreamController::encoded),errors(&controller,&UpstreamController::error);
      for(int cycle=0;cycle<3;++cycle) {
        const auto context="context-"+QString::number(cycle);
        controller.setContext(context);QVERIFY(controller.encode("CQ K1ABC FN20",parameters()));
        QTRY_COMPARE_WITH_TIMEOUT(encoded.size(),cycle+1,3000);
        QFile file(ephemeris);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),context.toUtf8());file.close();
        QCOMPARE(controller.process_.arguments(),QStringList({"--work-dir",work}));
        QVERIFY(QDir(work).exists());controller.stop();
        QTRY_VERIFY_WITH_TIMEOUT(controller.process_.state()==QProcess::NotRunning && !controller.stopping_,3000);
        QVERIFY(!QDir(work).exists());QVERIFY(QDir(parentPath).exists());
        QCOMPARE(QDir(parentPath).entryList(QDir::AllEntries|QDir::NoDotAndDotDot).size(),0);
      }
      // Destruction while the helper is still alive must also remove its files.
      controller.setContext("destructor");QVERIFY(controller.encode("CQ K1ABC FN20",parameters()));
      QTRY_COMPARE_WITH_TIMEOUT(encoded.size(),4,3000);QVERIFY(QFile::exists(ephemeris));QCOMPARE(errors.size(),0);
    }
    QVERIFY(!QDir(parentPath).exists());
  }
  void cancelledFailedStartCanRunNextContext() {
    UpstreamController controller(nullptr,fixture()+"-missing");controller.setContext("cancelled");
    QSignalSpy encoded(&controller,&UpstreamController::encoded),errors(&controller,&UpstreamController::error);
    QVERIFY(controller.encode("CQ K1ABC FN20",parameters()));
    // CreateProcess reports a missing executable synchronously on Windows;
    // Unix can report it only after stop(). Preserve the genuine former error
    // while requiring cancellation/restart to add no error of its own.
    const int startupErrors=errors.size();QVERIFY(startupErrors<=1);
    if(startupErrors) {
      QCOMPARE(controller.process_.state(),QProcess::NotRunning);
      QCOMPARE(controller.process_.error(),QProcess::FailedToStart);
      QVERIFY(!errors.first().first().toString().isEmpty());
    }
    controller.stop();controller.program_=fixture();controller.setContext("next");
    QVERIFY(controller.encode("CQ W9XYZ EN37",parameters()));
    QTRY_COMPARE_WITH_TIMEOUT(encoded.size(),1,3000);QVERIFY(!controller.stopping_);
    QCOMPARE(encoded[0][0].toString(),QString("CQ W9XYZ EN37"));QCOMPARE(errors.size(),startupErrors);
  }
  void periodCrossingsAndPartialStart() {
    PeriodAudioBuffer buffer;buffer.setPeriod(5);
    QCOMPARE(buffer.append(QByteArray(24000,1),4500).size(),0); // begins at3.5 s
    auto frames=buffer.append(QByteArray(24000,2),5500);
    QCOMPARE(frames.size(),1);QCOMPARE(frames[0].utcMs,qint64(0));QVERIFY(frames[0].partial);
    QCOMPARE(frames[0].pcm.size(),120000);QCOMPARE(frames[0].pcm.left(84000),QByteArray(84000,0));
    QCOMPARE(buffer.samples(),6000);
    frames=buffer.append(QByteArray(108000,3),10000);
    QCOMPARE(frames.size(),1);QCOMPARE(frames[0].utcMs,qint64(5000));QVERIFY(!frames[0].partial);
    QCOMPARE(frames[0].pcm.left(12000),QByteArray(12000,2));QCOMPARE(buffer.samples(),0);
  }
  void periodJitterAndUtcSteps() {
    PeriodAudioBuffer buffer;buffer.setPeriod(5);
    QVERIFY(buffer.append(QByteArray(24000,1),1000).isEmpty());
    QVERIFY(buffer.append(QByteArray(24000,2),2050).isEmpty()); // callback jitter, samples continuous
    auto frames=buffer.append(QByteArray(72000,3),5000);
    QCOMPARE(frames.size(),1);QVERIFY(!frames[0].partial);
    QVERIFY(buffer.append(QByteArray(24000,4),6000).isEmpty());
    QVERIFY(buffer.append(QByteArray(24000,5),9000).isEmpty()); // gap drops old incomplete frame
    frames=buffer.append(QByteArray(24000,6),10000);
    QCOMPARE(frames.size(),1);QVERIFY(frames[0].partial);QCOMPARE(frames[0].utcMs,qint64(5000));
    QVERIFY(buffer.append(QByteArray(24000,7),4000).isEmpty()); // UTC moves backward
    frames=buffer.append(QByteArray(24000,8),5000);
    QCOMPARE(frames.size(),1);QVERIFY(frames[0].partial);QCOMPARE(frames[0].utcMs,qint64(0));
  }
  void invalidCaptureCannotCompleteAValidFrame() {
    PeriodAudioBuffer buffer;buffer.setPeriod(5);
    QVERIFY(buffer.append(QByteArray(96000,1),4000).isEmpty());
    QVERIFY(buffer.append(QByteArray(1,0),4100).isEmpty());QCOMPARE(buffer.samples(),0);
    auto frames=buffer.append(QByteArray(24000,2),5000);
    QCOMPARE(frames.size(),1);QVERIFY(frames[0].partial);
    QVERIFY(buffer.append(QByteArray(2,0),std::numeric_limits<qint64>::max()).isEmpty());QCOMPARE(buffer.samples(),0);
  }
  void earlySnapshotDoesNotRepeatAtBoundary() {
    PeriodAudioBuffer buffer;buffer.setPeriod(15);PeriodAudioBuffer::Frame snapshot;
    for(int second=1;second<=13;++second)QVERIFY(buffer.append(QByteArray(24000,1),second*1000).isEmpty());
    QVERIFY(!buffer.snapshot(161400,snapshot));
    QVERIFY(buffer.append(QByteArray(12000,2),13500).isEmpty());
    QVERIFY(buffer.snapshot(161400,snapshot));QCOMPARE(snapshot.utcMs,qint64(0));QCOMPARE(snapshot.pcm.size(),324000);
    QVERIFY(!buffer.snapshot(161400,snapshot));
    QVERIFY(buffer.append(QByteArray(36000,3),15000).isEmpty());QCOMPARE(buffer.samples(),0);
    buffer.reset();QVERIFY(buffer.append(QByteArray(24000,4),14000).isEmpty());
    QVERIFY(!buffer.snapshot(161400,snapshot)); // padded initial data is never an early full payload
  }
};
QTEST_GUILESS_MAIN(UpstreamControllerTests)
#include "UpstreamControllerTests.moc"
