// Include after RegressionTest. Uses loopback UDP and simulated PTT only.
#include <QUdpSocket>
#include <QScopedValueRollback>
#include "MessageClient.hpp"
#include "NetworkMessage.hpp"

namespace UdpReplayFixture {
struct Decode {
  quint32 type=0;
  bool isNew=false,offAir=false,lowConfidence=false,valid=false;
  QTime time;
  qint32 snr=0,drift=0,power=0;
  float dt=0;
  quint32 offset=0;
  quint64 frequency=0;
  QByteArray mode,message,call,grid;
};
QList<Decode> drain(QUdpSocket& socket)
{
  QList<Decode> result;
  while(socket.hasPendingDatagrams()) {
    QByteArray packet(int(socket.pendingDatagramSize()),0);
    if(socket.readDatagram(packet.data(),packet.size())!=packet.size())continue;
    NetworkMessage::Reader input(packet);Decode item;item.type=input.type();
    if(item.type==NetworkMessage::Decode)
      input>>item.isNew>>item.time>>item.snr>>item.dt>>item.offset>>item.mode>>item.message>>item.lowConfidence>>item.offAir;
    else if(item.type==NetworkMessage::WSPRDecode)
      input>>item.isNew>>item.time>>item.snr>>item.dt>>item.frequency>>item.drift>>item.call>>item.grid>>item.power>>item.offAir;
    else continue;
    item.valid=input.status()==QDataStream::Ok;result.append(item);
  }
  return result;
}
}

void RegressionTest::udpReplyCacheAndSafety()
{
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  w->on_actionFT8_triggered();stopWaveAudio();w->m_diskData=false;w->m_manualDecode=false;
  w->m_autoTx=true;w->m_autoseq=false;w->m_houndMode=false;w->m_singleshot=false;
  w->m_freqNominal=14074000;w->clearBandActivity();w->clearDX("UDP identity setup");
  auto oldAccept=w->m_config.accept_udp_requests(),oldQuick=w->m_config.quick_call();
  auto oldFilter=w->m_acceptUDP;auto blocked=w->m_blacklist->entries();
  auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  auto configure=[&](bool accept,bool quick) {
    QTimer::singleShot(0,dialog,[=]{
      dialog->findChild<QCheckBox*>("accept_udp_requests_check_box")->setChecked(accept);
      dialog->findChild<QCheckBox*>("quick_call_check_box")->setChecked(quick);
      QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });
    QCOMPARE(w->m_config.exec(),int(QDialog::Accepted));w->applyGeneralSettings();
  };
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&]{
    w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);w->m_autoTx=false;
    w->m_blacklist->save(blocked);configure(oldAccept,oldQuick);w->m_acceptUDP=oldFilter;
    w->clearBandActivity();w->clearDX("UDP identity cleanup");w->m_qsoHistory.init();
  }};
  configure(true,true);w->m_acceptUDP=3;QVERIFY(w->m_blacklist->save({}));
  QTRY_VERIFY_WITH_TIMEOUT(w->m_logReady && !w->m_logLoading && !w->m_logImportActive
    && !w->m_logRefreshPending && !w->m_logRefreshTimer.isActive(),20000);
  TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  const QString row="000015 -10  0.0 1500 ~ CQ K8UDP FN20";
  w->postDecode(true,row);w->ui->decodedTextBrowser->clear();

  // A matching substring or call alone is insufficient: every wire identity
  // field must identify the exact received row, even after it was hidden.
  for(int mismatch=0;mismatch<6;++mismatch) {
    w->replyToUDP(mismatch==0?QTime(0,0,30):QTime(0,0,15),mismatch==1?-11:-10,
      mismatch==2?0.1f:0.f,mismatch==3?1501:1500,mismatch==4?"+":"~",
      mismatch==5?"CQ K8UDP FN21":"CQ K8UDP FN20",false,0);
    QVERIFY(w->m_hisCall.isEmpty());
    QVERIFY(!w->m_enableTx && !w->m_manualTxAuthorized && !w->m_txSession.active());
  }
  w->replyToUDP(QTime(0,0,15),-10,0.f,1500,"~","CQ K8UDP FN20",false,0);
  QCOMPARE(w->m_hisCall,QString("K8UDP"));
  QVERIFY(!w->m_enableTx && !w->m_manualTxAuthorized && !w->m_txSession.active());
  w->startTx2();QCOMPARE(g_iptt,0);QCOMPARE(ptt.pttOnCount(),0);

  // The cache never overrides a permanent block added after the decode.
  w->clearDX("UDP blacklist setup");
  QVERIFY(w->m_blacklist->save({{"K8UDP","Synthetic UDP test"}}));
  w->replyToUDP(QTime(0,0,15),-10,0.f,1500,"~","CQ K8UDP FN20",false,0);
  QVERIFY(w->m_hisCall.isEmpty());QVERIFY(w->m_blacklist->save({}));

  for(auto mode:{ManagedMode::DxccHunter,ManagedMode::QsoEnthusiast}) {
    QTRY_VERIFY_WITH_TIMEOUT(w->m_logReady && !w->m_logLoading && !w->m_logRefreshPending,20000);
    prepareManaged(mode);w->ui->dxCallEntry->setText("K8OLD");
    w->m_managed.begin("K8OLD");w->m_managedContext=w->generalContext();w->m_managedTxReady=true;
    w->postDecode(true,row);
    for(int phase=0;phase<5;++phase) {
      QScopedValueRollback<bool> transmitting(w->m_transmitting,phase==0);
      QScopedValueRollback<qint32> keyed(g_iptt,phase==1?1:0);
      QScopedValueRollback<bool> pending(w->m_tx_when_ready,phase==2);
      QScopedValueRollback<bool> calling(w->m_managedCallOnAir,phase==3);
      QScopedValueRollback<bool> cq(w->m_managedCqOnAir,phase==4);
      w->replyToUDP(QTime(0,0,15),-10,0.f,1500,"~","CQ K8UDP FN20",false,0);
      QCOMPARE(w->m_hisCall,QString("K8OLD"));QCOMPARE(w->m_managed.current(),QString("K8OLD"));
    }
    // An idle, already authorized managed session can accept the remote target.
    w->replyToUDP(QTime(0,0,15),-10,0.f,1500,"~","CQ K8UDP FN20",false,0);
    QCOMPARE(w->m_hisCall,QString("K8UDP"));QCOMPARE(w->m_managed.current(),QString("K8UDP"));
    QVERIFY(w->m_managedTxReady && w->m_enableTx && w->m_manualTxAuthorized);
    w->on_stopTxButton_clicked();
    w->replyToUDP(QTime(0,0,15),-10,0.f,1500,"~","CQ K8UDP FN20",false,0);
    QVERIFY(!w->m_enableTx && !w->m_manualTxAuthorized && !w->m_txSession.active());
  }
  QCOMPARE(ptt.pttOnCount(),0);

  // Production Fortran emits a26,a1: the confidence flag is at column 49
  // for HHMMSS rows, not at the end of the 24-character message field.
  for(auto flag:QString::fromUtf8("*^°?")) {
    UdpDecodeRecord parsed;
    QVERIFY(UdpDecodeRecord::parse(row.leftJustified(49,' ')+flag,false,false,parsed));
    QVERIFY(parsed.lowConfidence);QCOMPARE(parsed.message,QString("CQ K8UDP FN20"));
  }
}

void RegressionTest::udpReplaySnapshotAndCancellation()
{
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  w->on_actionFT8_triggered();stopWaveAudio();w->m_diskData=false;w->m_manualDecode=false;
  w->m_freqNominal=14074000;w->clearBandActivity();
  auto blocked=w->m_blacklist->entries();QVERIFY(w->m_blacklist->save({}));
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&]{
    w->on_stopTxButton_clicked();w->m_blacklist->save(blocked);w->m_diskData=false;
    w->on_actionFT8_triggered();stopWaveAudio();w->clearBandActivity();
    w->m_messageClient->set_server_port(w->m_config.udp_server_port());
    w->m_messageClient->set_server(w->m_config.udp_server_name());
  }};
  QUdpSocket peer;QVERIFY(peer.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)));
  w->m_messageClient->set_server_port(peer.localPort());w->m_messageClient->set_server("127.0.0.1");
  QTRY_VERIFY_WITH_TIMEOUT(peer.hasPendingDatagrams(),3000);UdpReplayFixture::drain(peer);
  auto row=[](int frequency,QString call="K8UDP") {
    return QString("000015 -10  0.0 %1 ~ CQ %2 FN20").arg(frequency,4).arg(call);
  };
  for(int i=0;i<64;++i) {
    w->m_diskData=i==0;w->postDecode(true,row(1000+i));
  }
  w->m_diskData=false;w->postDecode(true,row(2000,"K8BLK"));
  QVERIFY(w->m_blacklist->save({{"K8BLK","Synthetic replay test"}}));
  w->ui->decodedTextBrowser->clear(); // Hidden published rows still replay.
  QTest::qWait(20);UdpReplayFixture::drain(peer);
  w->replayDecodes();QList<UdpReplayFixture::Decode> replay;
  auto received=[&]{replay.append(UdpReplayFixture::drain(peer));return replay.size();};
  QTRY_COMPARE_WITH_TIMEOUT(received(),64,3000);
  for(int i=0;i<replay.size();++i) {
    auto const& decoded=replay.at(i);QVERIFY(decoded.valid && !decoded.isNew);
    QCOMPARE(decoded.type,quint32(NetworkMessage::Decode));QCOMPARE(decoded.offset,quint32(1000+i));
    QCOMPARE(decoded.offAir,i==0);QCOMPARE(decoded.message,QByteArray("CQ K8UDP FN20"));
  }

  // Clear must cancel already scheduled batches as well as the reply cache.
  w->replayDecodes();w->clearBandActivity();QTest::qWait(60);
  auto cancelled=UdpReplayFixture::drain(peer);
  QCOMPARE(cancelled.size(),32);QVERIFY(w->m_udpDecodeHistory.records().isEmpty());
  w->replayDecodes();QTest::qWait(30);QVERIFY(UdpReplayFixture::drain(peer).isEmpty());

  // A context change also cancels pending replay. Retained on-screen text is
  // intentionally left behind: it must never become a new band's decode.
  for(int i=0;i<64;++i)w->postDecode(true,row(1000+i));
  QTest::qWait(20);UdpReplayFixture::drain(peer);
  w->ui->decodedTextBrowser->setPlainText(row(1500)+"\n");
  w->replayDecodes();w->m_freqNominal=7074000;QTest::qWait(60);
  QCOMPARE(UdpReplayFixture::drain(peer).size(),32);
  w->ui->decodedTextBrowser->setPlainText(row(1500)+"\n");
  QVERIFY(!w->ui->decodedTextBrowser->toPlainText().isEmpty());
  w->replayDecodes();QTest::qWait(30);QVERIFY(UdpReplayFixture::drain(peer).isEmpty());

  // WSPR has a distinct packet format and must preserve each row's off-air bit
  // even when the current receive source and the visible pane have changed.
  w->m_mode=w->m_modeTx="WSPR-2";w->m_freqNominal=14095600;w->clearBandActivity();
  w->m_diskData=true;
  w->postWSPRDecode(true,{"1234","-20","0.1","14.095600","0","K8UDP","FN20","37"});
  w->m_diskData=false;
  w->postWSPRDecode(true,{"1236","-21","0.2","14.095610","0","K8UDP/P","37"});
  w->ui->decodedTextBrowser->clear();QTest::qWait(20);UdpReplayFixture::drain(peer);
  w->replayDecodes();replay.clear();QTRY_COMPARE_WITH_TIMEOUT(received(),2,3000);
  QCOMPARE(replay.at(0).type,quint32(NetworkMessage::WSPRDecode));
  QVERIFY(replay.at(0).valid && !replay.at(0).isNew && replay.at(0).offAir);
  QCOMPARE(replay.at(0).frequency,quint64(14095600));QCOMPARE(replay.at(0).grid,QByteArray("FN20"));
  QVERIFY(replay.at(1).valid && !replay.at(1).isNew && !replay.at(1).offAir);
  QCOMPARE(replay.at(1).call,QByteArray("K8UDP/P"));QVERIFY(replay.at(1).grid.isEmpty());
  w->clearBandActivity();QVERIFY(w->m_udpDecodeHistory.wsprRecords().isEmpty());
}
