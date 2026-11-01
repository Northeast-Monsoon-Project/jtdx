#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QCheckBox>
#include <QComboBox>
#include "UpstreamOperatingState.hpp"
#include "AstroDialog.hpp"

class UpstreamOperatingTests:public QObject {
  Q_OBJECT
private slots:
  void officialQ65PeriodSubmodes() {
    QCOMPARE(UpstreamOperatingState::q65MaximumSubmode(15),2);
    QCOMPARE(UpstreamOperatingState::q65MaximumSubmode(30),3);
    QCOMPARE(UpstreamOperatingState::q65MaximumSubmode(60),4);
    QCOMPARE(UpstreamOperatingState::q65MaximumSubmode(120),5);
    QCOMPARE(UpstreamOperatingState::q65MaximumSubmode(300),4);
  }
  void boundedRr73Repeats() {
    UpstreamOperatingState state;
    for(int n=1;n<10;++n)QVERIFY(!state.finishedMessage(true,"Q65","ctx","W9XYZ","W9XYZ K1ABC RR73"));
    QVERIFY(state.finishedMessage(true,"Q65","ctx","W9XYZ","W9XYZ K1ABC RR73"));
    state.cancelTransmit();
    QVERIFY(!state.finishedMessage(true,"Q65","ctx","W9XYZ","W9XYZ K1ABC RR73"));
    QVERIFY(state.finishedMessage(true,"MSK144","ctx","W9XYZ","W9XYZ K1ABC 73"));
    for(int n=0;n<12;++n)QVERIFY(!state.finishedMessage(false,"Q65","ctx","W9XYZ","W9XYZ K1ABC RR73"));
    for(int n=0;n<9;++n)QVERIFY(!state.finishedMessage(true,"Q65","ctx","W9XYZ","W9XYZ K1ABC RR73"));
    QVERIFY(!state.finishedMessage(true,"Q65","ctx","JA1AAA","JA1AAA K1ABC RR73"));
  }
  void echoMetadataSurvivesNextSlot() {
    UpstreamOperatingState state;UpstreamOperatingState::Echo first,second,result;
    first.soundStartMs=123;first.dither=-10;first.delaySeconds=2.6;
    second.soundStartMs=6042;second.dither=7;
    state.rememberEcho(0,first);state.rememberEcho(6000,second);
    QVERIFY(state.takeEcho(0,result));QCOMPARE(result.soundStartMs,qint64(123));QCOMPARE(result.dither,-10);QCOMPARE(result.delaySeconds,2.6);
    QVERIFY(!state.takeEcho(0,result));state.cancelTransmit();QVERIFY(!state.takeEcho(6000,result));
  }
  void catFeedbackDoesNotAccumulateCorrection() {
    UpstreamOperatingState state;const quint64 nominal=144120000;
    state.expectReceiveFrequency(nominal,100);
    QCOMPARE(state.nominalFromReport(nominal,nominal,nominal,100),nominal); // old telemetry
    QCOMPARE(state.nominalFromReport(nominal+100,nominal,nominal,100),nominal); // command confirmation
    state.expectReceiveFrequency(nominal,110);
    QCOMPARE(state.nominalFromReport(nominal+100,nominal+100,nominal,110),nominal);
    QCOMPARE(state.nominalFromReport(nominal+110,nominal+100,nominal,110),nominal);
    QCOMPARE(state.nominalFromReport(nominal+100,nominal+110,nominal,110),nominal); // late confirmation
    // A physical manual dial change is interpreted using the last applied offset.
    QCOMPARE(state.nominalFromReport(nominal+600,nominal+100,nominal,110),nominal+500);
    QCOMPARE(UpstreamOperatingState::shifted(100,std::numeric_limits<qint64>::min()),quint64(100));
  }
  void catRevisitedBandIsNotAStaleDopplerReply() {
    UpstreamOperatingState state;
    state.expectReceiveFrequency(14074000,0);
    QCOMPARE(state.nominalFromReport(14074000,7074000,14074000,0),quint64(14074000));
    state.expectReceiveFrequency(7074000,0);
    QCOMPARE(state.nominalFromReport(7074000,14074000,7074000,0),quint64(7074000));
    // Turn the rig back to 20 m without asking the application to QSY.
    QCOMPARE(state.nominalFromReport(14074000,7074000,7074000,0),quint64(14074000));
    QCOMPARE(state.nominalFromReport(7074000,14074000,14074000,0),quint64(7074000));
    // Changing the nominal frequency while preserving a physical frequency
    // must refresh the command's context, not reuse the earlier offset.
    state.expectReceiveFrequency(144120000,100);
    state.expectReceiveFrequency(144120050,50);
    QCOMPARE(state.nominalFromReport(144120100,7074000,144120050,50),quint64(144120050));
    QCOMPARE(state.nominalFromReport(144120600,144120100,144120050,50),quint64(144120550));
  }
  void mskCaptureDiscontinuity() {
    UpstreamOperatingState state;
    QVERIFY(state.continuousMsk(1000,1200));QVERIFY(state.continuousMsk(1102,1200));
    QVERIFY(!state.continuousMsk(3000,1200));QVERIFY(!state.continuousMsk(1000,1200));
    state.resetCapture();QVERIFY(state.continuousMsk(5000,1200));
  }
  void receiveCompletionGatesNextQ65Slot() {
    UpstreamOperatingState state;
    QVERIFY(!state.waitingForQ65(15000));
    state.observeQ65(15000);QVERIFY(state.waitingForQ65(15000));
    state.completeQ65(0);QVERIFY(state.waitingForQ65(15000));
    state.completeQ65(15000);QVERIFY(!state.waitingForQ65(15000));
    state.observeQ65(45000);QVERIFY(state.waitingForQ65(45000));
    state.resetCapture();QVERIFY(!state.waitingForQ65(45000));
  }
  void backwardsUtcInvalidatesQ65Completion() {
    UpstreamOperatingState state;
    state.observeQ65(45000);state.completeQ65(45000);
    QVERIFY(!state.waitingForQ65(45000));
    state.observeQ65(15000);QVERIFY(state.waitingForQ65(15000));
    // The old request may finish after the clock correction. Its future
    // timestamp cannot satisfy the fresh receive period's completion gate.
    state.completeQ65(45000);QVERIFY(state.waitingForQ65(15000));
    state.completeQ65(0);QVERIFY(state.waitingForQ65(15000));
    state.completeQ65(15000);QVERIFY(!state.waitingForQ65(15000));
    state.observeQ65(30000);QVERIFY(state.waitingForQ65(30000));
  }
  void dopplerMethodsAndMidpoint() {
    auto c=AstroDialog::dopplerCorrection(1,120,240,1000,false);QCOMPARE(c.rx,qint64(240));QCOMPARE(c.tx,qint64(760));
    c=AstroDialog::dopplerCorrection(2,120,240,1000,false);QCOMPARE(c.rx,qint64(60));QCOMPARE(c.tx,qint64(940));
    c=AstroDialog::dopplerCorrection(3,120,240,1000,false);QCOMPARE(c.rx,qint64(120));QCOMPARE(c.tx,qint64(0));
    c=AstroDialog::dopplerCorrection(4,120,240,1000,false);QCOMPARE(c.rx,qint64(240));QCOMPARE(c.tx,qint64(1120));
    c=AstroDialog::dopplerCorrection(5,120,240,1000,false);QCOMPARE(c.rx,qint64(0));QCOMPARE(c.tx,qint64(880));
    QTemporaryDir dir;QSettings settings(dir.filePath("astro.ini"),QSettings::IniFormat);
    AstroDialog dialog(&settings);
    dialog.findChild<QCheckBox*>("doppler_tracking")->setChecked(true);
    dialog.findChild<QComboBox*>("doppler_method")->setCurrentIndex(1);
    QJsonObject data{{"self_doppler_hz",120},{"dx_doppler_hz",240},{"tx_self_doppler_hz",130},{"tx_dx_doppler_hz",250}};
    c=dialog.correction(data,false,true);QCOMPARE(c.rx,qint64(240));QCOMPARE(c.tx,qint64(-250));
    dialog.findChild<QCheckBox*>("doppler_tracking")->setChecked(false);
    c=dialog.correction(data,false,true);QCOMPARE(c.rx,qint64(0));QCOMPARE(c.tx,qint64(0));
  }
};
QTEST_MAIN(UpstreamOperatingTests)
#include "UpstreamOperatingTests.moc"
