#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QCheckBox>
#include <QSpinBox>
#include "ManagedMode.hpp"
#include "ManagedSettings.hpp"

class ManagedTests : public QObject {
  Q_OBJECT
  ManagedCandidate station(QString call,unsigned flags,int snr=-10,bool incoming=false) {
    ManagedCandidate c;c.call=call;c.novelty=flags;c.snr=snr;c.incoming=incoming;c.available=!incoming;c.seen=1000;return c;
  }
private slots:
  void defaultsAndPersistence() {
    ManagedSession session;QVERIFY(!session.active());
    ManagedSettings defaults;QCOMPARE(defaults.hunter.callLimit,3);QVERIFY(!defaults.enthusiast.answerWorked);
    QVERIFY(!defaults.hunter.freeTxSlot && !defaults.enthusiast.freeTxSlot);
    ManagedSettingsWidget widget;auto forced=widget.findChildren<QCheckBox*>(QRegularExpression(".*periodic_cq"));
    QCOMPARE(forced.size(),2);for(auto box:forced){QVERIFY(box->isChecked());QVERIFY(!box->isEnabled());}
    auto slotOptions=widget.findChildren<QCheckBox*>(QRegularExpression(".*free_tx_slot"));
    QCOMPARE(slotOptions.size(),2);for(auto box:slotOptions){QVERIFY(!box->isChecked());QVERIFY(box->isEnabled());}
    auto count=widget.findChild<QSpinBox*>("managed_hunter_call_limit");QVERIFY(count);QCOMPARE(count->minimum(),0);QCOMPARE(count->maximum(),99);
    defaults.hunter.criteria=NewGrid;defaults.hunter.callLimit=0;defaults.enthusiast.answerWorked=true;defaults.enthusiast.callLimit=99;
    defaults.hunter.freeTxSlot=true;
    widget.setSettings(defaults);auto selected=widget.settings();
    QTemporaryDir dir;QSettings stored(dir.path()+"/test.ini",QSettings::IniFormat);selected.write(stored);stored.sync();
    ManagedSettings loaded;loaded.read(stored);QCOMPARE(loaded.hunter.criteria,unsigned(NewGrid));QCOMPARE(loaded.hunter.callLimit,0);
    QVERIFY(loaded.enthusiast.answerWorked);QCOMPARE(loaded.enthusiast.callLimit,99);
    QVERIFY(loaded.hunter.freeTxSlot);QVERIFY(!loaded.enthusiast.freeTxSlot);
    loaded.hunter.freeTxSlot=false;loaded.enthusiast.freeTxSlot=true;widget.setSettings(loaded);widget.settings().write(stored);
    ManagedSettings restarted;restarted.read(stored);QVERIFY(!restarted.hunter.freeTxSlot);QVERIFY(restarted.enthusiast.freeTxSlot);
    session.setMode(ManagedMode::DxccHunter,0);session.setMode(ManagedMode::QsoEnthusiast,1);
    ManagedSession reopened;QVERIFY(!reopened.active());QVERIFY(!stored.allKeys().contains("Mode"));
  }
  void txSlotTimingAndCooldown() {
    for(double period:{3.75,7.5,15.0,30.0,60.0}) {
      QVERIFY(!ManagedSlotPolicy::replyTxFirst(0,period));
      QVERIFY(ManagedSlotPolicy::replyTxFirst(unsigned(period),period));
      QVERIFY(!ManagedSlotPolicy::replyTxFirst(unsigned(2*period),period));
    }
    ManagedSlotPolicy policy;
    for(bool current:{false,true}) {
      QVERIFY(policy.canSelect(current,current,false,false,0));
      QVERIFY(!policy.canSelect(current,!current,false,true,0));
      QVERIFY(!policy.canSelect(current,!current,true,false,0)); // No handoff mid-QSO.
      QVERIFY(policy.canSelect(current,!current,true,true,0));
    }
    policy.changed(1234);
    QCOMPARE(policy.remaining(1234),qint64(300000));
    QVERIFY(!policy.canSelect(false,true,true,true,301233));
    QVERIFY(policy.canSelect(false,true,true,true,301234));
    QVERIFY(policy.canSelect(true,true,false,false,1235)); // Same-slot QSOs remain available.
    policy.changed(2000); // A manual change is unrestricted and renews the cooldown.
    QCOMPARE(policy.remaining(2000),qint64(300000));
    QVERIFY(!policy.canSelect(true,false,true,true,301234));
    QVERIFY(policy.canSelect(true,false,true,true,302000));
    QVERIFY(!policy.canSelect(true,false,true,true,1000)); // Never bypass on a backward clock.
    ManagedSlotPolicy freshProcess;QVERIFY(freshProcess.canSelect(true,false,true,true,0));
  }
  void eachCriterionIsIndependent() {
    ManagedSession session;session.setMode(ManagedMode::DxccHunter,0);
    for(unsigned flag:{NewCqZone,NewDxcc,NewBandModeDxcc,NewItuZone,NewGrid,NewCall,NewBandModeCall}) {
      ManagedProfile p(flag);auto wanted=station("K1AAA",flag),other=station("K2BBB",0,30);
      QCOMPARE(session.choose({other,wanted},p,1000).call,wanted.call);
      p.criteria=0;QVERIFY(session.choose({wanted},p,1000).call.isEmpty());
    }
  }
  void priorityAndSignalStrength() {
    ManagedSession session;session.setMode(ManagedMode::QsoEnthusiast,0);ManagedProfile p(127);
    QList<ManagedCandidate> stations;
    for(int i=0;i<7;++i)stations.append(station("K"+QString::number(i+1)+"AAA",1u<<i,i));
    for(int i=0;i<7;++i) {QCOMPARE(session.choose(stations,p,1000).call,stations.first().call);stations.removeFirst();}
    auto weak=station("K1AAA",NewCall,-25),strong=station("K2BBB",NewCall,5);
    QCOMPARE(session.choose({weak,strong},p,1000).call,strong.call);
    // Ranking uses all novelty, eligibility uses only this profile's criteria.
    weak.novelty|=NewCqZone;p.criteria=NewCall;QCOMPARE(session.choose({strong,weak},p,1000).call,weak.call);
  }
  void callersAndWorkedPolicy() {
    ManagedSession s;s.setMode(ManagedMode::DxccHunter,0);ManagedProfile p(NewDxcc);
    auto cq=station("K1AAA",NewDxcc,20),caller=station("K2BBB",0,-25,true);
    QCOMPARE(s.choose({cq,caller},p,1000).call,caller.call);
    caller.workedBandMode=true;QCOMPARE(s.choose({caller,cq},p,1000).call,cq.call);
    p.answerWorked=true;QCOMPARE(s.choose({caller,cq},p,1000).call,caller.call);
    s.begin(caller.call);s.failed(1000);QVERIFY(s.blocked(caller.call,1001));
    QCOMPARE(s.choose({caller,cq},p,1001).call,caller.call);
  }
  void attemptLimitsAndReplies() {
    ManagedSession s;s.setMode(ManagedMode::QsoEnthusiast,0);s.begin("K1AAA");
    for(int i=0;i<99;++i){QVERIFY(!s.exhausted(99));s.transmitted();}
    QVERIFY(s.exhausted(99));QVERIFY(!s.exhausted(0));
    auto reply=station("K1AAA",0,-10,true);reply.responseStage=2;s.observe(reply);QVERIFY(!s.exhausted(1));
    s.cancelAttempt();s.begin("K2BBB");QVERIFY(!s.exhausted(1));s.transmitted();QVERIFY(s.exhausted(1));
  }
  void retryEscalationAndRestart() {
    ManagedSession s;s.setMode(ManagedMode::QsoEnthusiast,0);
    s.begin("K1AAA");s.failed(1000);QVERIFY(s.blocked("K1AAA",600999));QVERIFY(!s.blocked("K1AAA",601000));
    s.begin("K1AAA/P");s.failed(601000);QVERIFY(s.blocked("K1AAA",2400999));QVERIFY(!s.blocked("K1AAA",2401000));
    s.begin("K1AAA");s.failed(2401000);QVERIFY(s.blocked("K1AAA",qint64(10000000000)));
    s.setMode(ManagedMode::Off,2401001);s.setMode(ManagedMode::QsoEnthusiast,2401002);QVERIFY(s.blocked("K1AAA",2401003));
    ManagedSession restarted;QVERIFY(!restarted.blocked("K1AAA",2401003));
    s.setMode(ManagedMode::DxccHunter,2401004);s.begin("K2BBB");s.failed(2401004);
    QVERIFY(s.blocked("K2BBB",3001003));QVERIFY(!s.blocked("K2BBB",3001004));
    s.begin("K2BBB");s.failed(3001004);QVERIFY(!s.blocked("K2BBB",3601004));
  }
  void perMessageBudgets_data() {
    QTest::addColumn<int>("mode");
    QTest::newRow("hunter")<<int(ManagedMode::DxccHunter);
    QTest::newRow("enthusiast")<<int(ManagedMode::QsoEnthusiast);
  }
  void perMessageBudgets() {
    QFETCH(int,mode);ManagedSession s;s.setMode(ManagedMode(mode),0);s.begin("K1AAA");
    qint64 slot=0;
    for(auto ending:{QString("AA00"),QString("-10"),QString("R-10"),QString("RRR"),QString("RR73"),QString("73")}) {
      QString message="K1AAA N0CALL "+ending;
      QVERIFY(!s.budgetReached(2,message));
      for(int round=0;round<2;++round,slot+=30000) {
        s.transmitted(message,slot,15000);QCOMPARE(s.transmissions(),round+1);
        QVERIFY(!s.exhausted(2)); // Every stage gives the peer its next RX slot.
        s.receiveCycleFinished(slot,15000);QVERIFY(!s.exhausted(2));
        s.receiveCycleFinished(slot+15000,15000);
      }
      QVERIFY(s.exhausted(2));QVERIFY(s.budgetReached(2,message));QVERIFY(!s.budgetReached(0,message));
    }
    // Switching back to an earlier message must not replenish its allowance.
    QVERIFY(s.budgetReached(2,"K1AAA N0CALL AA00"));QVERIFY(s.budgetReached(2,"K1AAA N0CALL -04"));
    auto reply=station("K1AAA",0,-10,true);reply.responseStage=2;reply.utcSeconds=unsigned(slot/1000);
    s.observe(reply);QVERIFY(!s.exhausted(2)); // New protocol progress can select its next phase.
    s.transmitted("K1AAA N0CALL R-04",slot,15000);
    s.observe(reply);s.receiveCycleFinished(slot+15000,15000);
    QVERIFY(s.exhausted(2));QCOMPARE(s.transmissions(),3); // Repeated old report is no progress.
    s.begin("K2BBB");QVERIFY(!s.budgetReached(2,"K2BBB N0CALL R-04"));
  }
  void periodicCqIsIndependentOfQsoRetries() {
    for(auto mode:{ManagedMode::DxccHunter,ManagedMode::QsoEnthusiast}) {
      ManagedSession s;s.setMode(mode,0);int rounds=3;
      QVERIFY(s.cqDue(900000));QCOMPARE(s.cqRemaining(),rounds);
      for(int n=0;n<rounds;++n)s.cqTransmitted(900000+n*30000);
      QCOMPARE(s.cqRemaining(),0);
      qint64 next=900000+(rounds-1)*30000+900000;
      QVERIFY(!s.cqDue(next-1));QVERIFY(s.cqDue(next));QCOMPARE(s.cqRemaining(),rounds);
    }
  }
  void freshReceptionRequiredAfterTimeout() {
    ManagedSession s;s.setMode(ManagedMode::QsoEnthusiast,0);auto cq=station("K1AAA",NewCall);s.observe(cq);
    s.begin(cq.call);s.failed(1000);QVERIFY(s.candidates(601000,32000).isEmpty());
    cq.seen=601001;s.observe(cq);QCOMPARE(s.candidates(601002,32000).size(),1);
    QVERIFY(s.candidates(634002,32000).isEmpty());
    cq.incoming=true;cq.seen=700000;s.observe(cq);QCOMPARE(s.candidates(800000,32000).size(),1);
    QVERIFY(s.candidates(820001,32000).isEmpty());
  }
  void invitationUsesMessageSlot() {
    constexpr qint64 day=86400000;
    for(qint64 period:{3750,7500,15000}) {
      for(qint64 current:{day+32400000,day}) {
        auto stamp=[&](qint64 slot){return unsigned((slot%day)/1000);};
        qint64 now=current+period/2;
        QVERIFY(ManagedSession::currentInvitation(stamp(current),now,period));
        QVERIFY(ManagedSession::currentInvitation(stamp(current-period),now,period));
        for(int age:{2,3,4})QVERIFY(!ManagedSession::currentInvitation(stamp(current-age*period),now,period));
        QVERIFY(!ManagedSession::currentInvitation(stamp(current+period),now,period));
        // Repeated late delivery cannot extend the original UTC slot.
        QVERIFY(!ManagedSession::currentInvitation(stamp(current-period),current+period,period));
      }
    }
  }
  void olderDecodeCannotReplaceNewerConversation() {
    ManagedSession s;s.setMode(ManagedMode::QsoEnthusiast,0);ManagedProfile p(NewCall);
    auto cq=station("K1AAA",NewCall);cq.utcSeconds=86370;s.observe(cq);
    auto busy=cq;busy.available=false;busy.utcSeconds=0;busy.seen=2000;s.observe(busy);
    cq.seen=3000;s.observe(cq);auto cached=s.candidates(3000,32000);
    QCOMPARE(cached.size(),1);QCOMPARE(cached.first().utcSeconds,0u);QVERIFY(!cached.first().available);
    QVERIFY(s.choose(cached,p,3000).call.isEmpty());
    cq.utcSeconds=30;cq.seen=4000;s.observe(cq);
    QCOMPARE(s.choose(s.candidates(4000,32000),p,4000).call,cq.call);
    // An older incoming report cannot undo the newer conversation either.
    busy.incoming=true;busy.responseStage=2;busy.seen=5000;s.begin(cq.call);s.transmitted();s.observe(busy);
    QVERIFY(s.exhausted(1));QCOMPARE(s.candidates(5000,32000).first().utcSeconds,30u);
  }
  void expiredUtcWatermarkCanRefresh() {
    ManagedSession s;s.setMode(ManagedMode::QsoEnthusiast,0);
    auto old=station("K8DAY",NewCall);old.utcSeconds=36000;s.observe(old);
    auto fresh=old;fresh.utcSeconds=82800;fresh.seen+=13*3600000;
    s.observe(fresh);
    QCOMPARE(s.candidates(fresh.seen,32000).first().utcSeconds,82800u);
    auto delayed=fresh;delayed.utcSeconds-=15;delayed.seen+=120000;
    s.observe(delayed);auto cached=s.candidates(delayed.seen,120000);
    QCOMPARE(cached.size(),1);QCOMPARE(cached.first().utcSeconds,82800u);
    ++delayed.seen;s.observe(delayed);cached=s.candidates(delayed.seen,32000);
    QCOMPARE(cached.size(),1);QCOMPARE(cached.first().utcSeconds,82785u);
  }
  void completedCallDoesNotLoop() {
    ManagedSession s;s.setMode(ManagedMode::QsoEnthusiast,0);auto caller=station("K1AAA",0,-10,true);s.observe(caller);
    ManagedProfile p(0,true);s.begin(caller.call);s.complete(1001);
    QVERIFY(s.choose(s.candidates(1002,32000),p,1002).call.isEmpty());
    caller.seen=2000;s.observe(caller);QCOMPARE(s.choose(s.candidates(2000,32000),p,2000).call,caller.call);
  }
  void periodicCqThreeRounds() {
    ManagedSession s;QVERIFY(!s.cqDue(900000));s.setMode(ManagedMode::DxccHunter,1000);
    QVERIFY(!s.cqDue(900999));QVERIFY(s.cqDue(901000));QCOMPARE(s.cqRemaining(),3);
    s.cqTransmitted(901000);QCOMPARE(s.cqRemaining(),2);
    s.begin("K1AAA");s.transmitted();s.complete(920000);QCOMPARE(s.cqRemaining(),2);
    s.cqTransmitted(950000);s.cqTransmitted(980000);QCOMPARE(s.cqRemaining(),0);
    QVERIFY(!s.cqDue(1879999));QVERIFY(s.cqDue(1880000));
    s.restartSchedule(1880001);QCOMPARE(s.cqRemaining(),0);QVERIFY(!s.cqDue(1880002));
  }
};
QTEST_MAIN(ManagedTests)
#include "ManagedTests.moc"
