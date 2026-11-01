#include <QtTest>
#include "SignoffState.hpp"

class SignoffTests : public QObject {
  Q_OBJECT
private slots:
  void rr73RequiresPeer73AndNaturalRelease() {
    SignoffState state;state.prepare("K1AAA","N0CALL|20m|FT8",SignoffState::RR73,0,15000);
    state.started();state.receive("K1AAA","N0CALL|20m|FT8","73",15000);
    QVERIFY(state.allowsTransmit());QVERIFY(!state.complete()); // Duplex ACK cannot cut the burst.
    state.consumed();QVERIFY(!state.complete());state.naturalEnd();QVERIFY(!state.complete());
    state.released();QVERIFY(state.complete());QCOMPARE(state.transmissions(),1);QVERIFY(!state.allowsTransmit());
  }
  void partialOrCancelledWaveIsNeverComplete() {
    for(int omitted=0;omitted<4;++omitted) {
      SignoffState state;state.prepare("K1AAA","ctx",SignoffState::RR73,0,15000);
      if(omitted!=0)state.started();if(omitted!=1)state.consumed();
      if(omitted!=2)state.naturalEnd();if(omitted!=3)state.released();
      state.receive("K1AAA","ctx","73",15000);QVERIFY(!state.complete());
      state.reset();QVERIFY(!state.active());state.consumed();state.naturalEnd();state.released();QVERIFY(!state.complete());
    }
  }
  void queuedCompletionCanArriveAfterPttRelease() {
    SignoffState state;state.prepare("K1AAA","ctx",SignoffState::RR73,0,15000);
    state.started();state.naturalEnd();state.released();
    state.receive("K1AAA","ctx","73",15000);
    QVERIFY(!state.complete());QCOMPARE(state.transmissions(),0);
    state.consumed();QVERIFY(state.complete());QCOMPARE(state.transmissions(),1);
    state.consumed();state.naturalEnd();state.released();QCOMPARE(state.transmissions(),1);
  }
  void final73WaitsForQuietReceiveCycle() {
    SignoffState state;state.prepare("K1AAA","ctx",SignoffState::SeventyThree,0,7500);
    state.started();state.consumed();state.naturalEnd();state.released();
    QVERIFY(!state.complete());QVERIFY(!state.allowsTransmit());
    state.receiveCycleFinished(0);state.receiveCycleFinished(15000);QVERIFY(!state.complete());
    state.receiveCycleFinished(7500);QVERIFY(state.complete());
  }
  void repeatedRr73RequestsAnotherFull73() {
    SignoffState state;state.prepare("K1AAA","ctx",SignoffState::SeventyThree,0,15000);
    state.started();state.consumed();state.naturalEnd();state.released();
    state.receive("K2BBB","ctx","RR73",15000);QVERIFY(!state.allowsTransmit());
    state.receive("K1AAA","oldctx","RR73",15000);QVERIFY(!state.allowsTransmit());
    state.receive("K1AAA","ctx","RR73",15000);state.receiveCycleFinished(15000);
    QVERIFY(state.allowsTransmit());QVERIFY(!state.complete());QVERIFY(state.hadReceiveOpportunity());
    state.prepare("K1AAA","ctx",SignoffState::SeventyThree,30000,15000);
    state.started();state.consumed();state.naturalEnd();state.released();QCOMPARE(state.transmissions(),2);
    state.receiveCycleFinished(15000);QVERIFY(!state.complete());
    state.receiveCycleFinished(45000);QVERIFY(state.complete());
  }
  void rr73DoesNotFinishOnQuietPeriodOrOtherStation() {
    SignoffState state;state.prepare("K1AAA","ctx",SignoffState::RR73,30000,15000);
    state.started();state.consumed();state.naturalEnd();state.released();
    state.receiveCycleFinished(45000);state.receive("K2BBB","ctx","73",45000);
    state.receive("K1AAA","ctx","73",15000);state.receive("K1AAA","ctx","RR73",45000);
    QVERIFY(!state.complete());QVERIFY(state.allowsTransmit());QVERIFY(state.hadReceiveOpportunity());
  }
  void audioCoverageRequiresEntireSlot() {
    SignoffAudioCoverage audio;
    // Start halfway through a slot: it must never count as fully received.
    for(qint64 end=4000;end<=7500;end+=250)QVERIFY(audio.append(end,3000,7500).isEmpty());
    QList<qint64> completed;
    for(qint64 end=7750;end<=15000;end+=250)completed.append(audio.append(end,3000,7500));
    QCOMPARE(completed,QList<qint64>{7500});
    // A discontinuity invalidates that slot and cannot be repaired by a timer.
    audio.reset();for(qint64 end=250;end<=3000;end+=250)audio.append(end,3000,7500);
    completed.clear();for(qint64 end=4250;end<=7500;end+=250)completed.append(audio.append(end,3000,7500));
    QVERIFY(completed.isEmpty());
    for(qint64 end=7750;end<=15000;end+=250)completed.append(audio.append(end,3000,7500));
    QCOMPARE(completed,QList<qint64>{7500});
  }
  void audioCoverageTracksUtcAcrossPartialFftBlocks() {
    // Actual Detector output with 80 ms callbacks: the display buffer drops
    // its partial FFT block at each boundary. Two sub-500 ms gaps used to
    // accumulate into a false discontinuity and discard the first full RX.
    for(qint64 phase:{qint64(0),qint64(39)}) {
      SignoffAudioCoverage audio;QList<qint64> completed;
      for(qint64 end=14128+phase;end<=14704+phase;end+=288)
        completed.append(audio.append(end,3456,15000));
      for(int block=0;block<51;++block)
        completed.append(audio.append(15248+phase+288*block,3456,15000));
      QVERIFY(completed.isEmpty());
      completed.append(audio.append(30208+phase,3456,15000));
      QCOMPARE(completed,QList<qint64>{15000});
    }
  }
  void audioCoverageDoesNotCountRepeatedOrOverlappingSamples() {
    SignoffAudioCoverage audio;QList<qint64> completed;
    completed.append(audio.append(250,3000,15000));
    for(int n=0;n<100;++n)completed.append(audio.append(250,3000,15000));
    for(qint64 end=300;end<=2500;end+=50)
      completed.append(audio.append(end,3000,15000));
    QVERIFY(completed.isEmpty());
    // A delayed older callback must not rewind/re-anchor this healthy slot.
    completed.append(audio.append(125,1500,15000));
    for(qint64 end=2750;end<=15000;end+=250)
      completed.append(audio.append(end,3000,15000));
    QCOMPARE(completed,QList<qint64>{0});
    QVERIFY(audio.append(15000,3000,15000).isEmpty());
  }
  void audioCoverageDoesNotFillSparseInputUsingTimestamps() {
    for(bool singleSample:{false,true}) {
      SignoffAudioCoverage audio;QList<qint64> completed;
      completed.append(audio.append(250,3000,15000));
      // Each gap alone is within 500 ms, but this is not a whole RX slot.
      for(qint64 end=650;end<=30650;end+=400)
        completed.append(audio.append(end,singleSample?1:2400,15000));
      QVERIFY(completed.isEmpty());
    }
  }
  void audioCoverageRetainsBoundedTimestampTolerance() {
    SignoffAudioCoverage audio;QList<qint64> completed;
    for(qint64 end=250;end<=5000;end+=250)audio.append(end,3000,15000);
    // A single 500 ms timestamp/capture discrepancy is allowed, as before.
    for(qint64 end=5750;end<=15000;end+=250)
      completed.append(audio.append(end,3000,15000));
    QCOMPARE(completed,QList<qint64>{0});
    audio.reset();completed.clear();
    for(qint64 end=250;end<=5000;end+=250)audio.append(end,3000,15000);
    // A 200 ms late callback followed by an overlapping timestamp neither
    // loses the slot nor double-counts the overlapping samples.
    completed.append(audio.append(5450,3000,15000));
    for(qint64 end=5500;end<=15000;end+=250)
      completed.append(audio.append(end,3000,15000));
    QCOMPARE(completed,QList<qint64>{0});
  }
  void audioCoverageRejectsGapAndRecoversAtNextCompleteSlot() {
    SignoffAudioCoverage audio;QList<qint64> completed;
    for(qint64 end=250;end<=5000;end+=250)audio.append(end,3000,15000);
    // 601 ms genuinely absent: the affected slot is invalid, but continued
    // reception can establish the next complete slot without a manual reset.
    for(qint64 end=5851;end<=30500;end+=250)
      completed.append(audio.append(end,3000,15000));
    QCOMPARE(completed,QList<qint64>{15000});
  }
  void finalMessagesKeepIndependentBudgets() {
    SignoffState state;
    auto sent=[&](SignoffState::Message kind,qint64 slot){
      state.prepare("K1AAA","ctx",kind,slot,15000);state.started();state.consumed();state.naturalEnd();state.released();
    };
    sent(SignoffState::RR73,0);sent(SignoffState::RR73,30000);QCOMPARE(state.transmissions(),2);
    sent(SignoffState::SeventyThree,60000);QCOMPARE(state.transmissions(),1);
    state.prepare("K1AAA","ctx",SignoffState::RR73,90000,15000);QCOMPARE(state.transmissions(),2);
    state.prepare("K1AAA","ctx",SignoffState::SeventyThree,120000,15000);QCOMPARE(state.transmissions(),1);
    state.prepare("K2BBB","ctx",SignoffState::RR73,150000,15000);QCOMPARE(state.transmissions(),0);
  }
};
QTEST_GUILESS_MAIN(SignoffTests)
#include "SignoffTests.moc"
