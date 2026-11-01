#include <QUdpSocket>
#include "MessageClient.hpp"
#include "NetworkMessage.hpp"
#include "JtAlertCompatibility.hpp"

void RegressionTest::udpHistoryIdentityAndClear()
{
  UdpDecodeRecord record;
  QVERIFY(UdpDecodeRecord::parse("123456 -10 -0.0 1500 ~ CQ K8UDP FN20",false,false,record));
  QVERIFY(record.matches(QTime(12,34,56),-10,0.f,1500,"~","CQ K8UDP FN20 a1"));
  QVERIFY(!record.matches(QTime(12,34,56),-10,0.f,1500,"~","CQ K8UDP FN21"));
  QVERIFY(!record.matches(QTime(12,34,56),-10,0.f,1501,"~","CQ K8UDP FN20"));
  QVERIFY(!UdpDecodeRecord::parse("123456 -10  NaN 1500 ~ CQ K8UDP FN20",false,false,record));
  QVERIFY(!UdpDecodeRecord::parse("999999 -10  0.0 1500 ~ CQ K8UDP FN20",false,false,record));
  QVERIFY(UdpDecodeRecord::parse("1234 -10  0.0 1500 # CQ K8UDP FN20",false,false,record));
  QCOMPARE(record.message,QString("CQ K8UDP FN20"));
  QString longMessage="K1ABC W9XYZ R FN20AB 0123456789";
  QVERIFY(UdpDecodeRecord::parse("123456 -10  0.0 1500 : "+longMessage,true,true,record));
  QCOMPARE(record.message,longMessage);QVERIFY(record.offAir);
  UdpDecodeHistory history;history.setContext("20m");
  for(int i=0;i<4200;++i){record.snr=i;history.append(record);}
  QCOMPARE(history.records().size(),4096);QCOMPARE(history.records().first().snr,104);
  history.setContext("40m");QVERIFY(history.records().isEmpty());
  w->m_udpDecodeHistory.append(record);auto generation=w->m_udpReplayGeneration;
  w->clearBandActivity();QVERIFY(w->m_udpDecodeHistory.records().isEmpty());
  QVERIFY(w->m_udpReplayGeneration!=generation);
  // Malformed WSPR rows must not index missing fields or send a decode.
  w->postWSPRDecode(true,{});w->postWSPRDecode(true,{"1234","-10"});
}

void RegressionTest::jtAlertReplyUsesSecondsAndKeepsManualGate_data()
{
  QTest::addColumn<QString>("protocol");QTest::addColumn<bool>("accept");
  for(auto protocol:{QString("FT8"),QString("FT4"),QString("FT2")})for(bool accept:{false,true})
    QTest::newRow(qPrintable(protocol+QString("-accept%1").arg(accept)))<<protocol<<accept;
}

void RegressionTest::jtAlertReplyUsesSecondsAndKeepsManualGate()
{
  QFETCH(QString,protocol);QFETCH(bool,accept);
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  if(protocol=="FT8")w->on_actionFT8_triggered();
  else if(protocol=="FT4")w->on_actionFT4_triggered();
  else w->on_actionFT2_triggered();
  stopWaveAudio();QVERIFY(!w->m_guiTimer.isActive());
  QCOMPARE(w->m_config.rig_name(),QString("None"));
  w->m_diskData=false;w->m_manualDecode=false;w->m_autoseq=false;w->m_autoTx=false;
  w->m_singleshot=false;w->m_lockTxFreq=false;w->clearDX("UDP integration setup");
  auto oldAccept=w->m_config.accept_udp_requests();auto oldQuick=w->m_config.quick_call();
  auto oldFilter=w->m_acceptUDP;auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  auto configure=[&](bool enabled,bool quick) {
    QTimer::singleShot(0,dialog,[=] {
      dialog->findChild<QCheckBox*>("accept_udp_requests_check_box")->setChecked(enabled);
      dialog->findChild<QCheckBox*>("quick_call_check_box")->setChecked(quick);
      QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });
    QCOMPARE(w->m_config.exec(),int(QDialog::Accepted));w->applyGeneralSettings();
  };
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&] {
    w->on_stopTxButton_clicked();configure(oldAccept,oldQuick);w->m_acceptUDP=oldFilter;
    w->m_messageClient->set_server_port(w->m_config.udp_server_port());
    w->m_messageClient->set_server(w->m_config.udp_server_name());
    w->clearDX("UDP integration cleanup");w->ui->decodedTextBrowser->clear();
    w->on_actionFT8_triggered();stopWaveAudio();
  }};
  configure(accept,true);w->m_acceptUDP=3;
  QTRY_VERIFY_WITH_TIMEOUT(w->m_logReady && !w->m_logLoading && !w->m_logImportActive
    && !w->m_logRefreshPending && !w->m_logRefreshTimer.isActive(),20000);
  QUdpSocket peer;QVERIFY(peer.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)));
  w->m_messageClient->set_server_port(peer.localPort());w->m_messageClient->set_server("127.0.0.1");
  QByteArray packet;QHostAddress address;quint16 port=0;
  QTRY_VERIFY_WITH_TIMEOUT(peer.hasPendingDatagrams(),3000);
  packet.resize(int(peer.pendingDatagramSize()));
  QCOMPARE(peer.readDatagram(packet.data(),packet.size(),&address,&port),qint64(packet.size()));
  QVERIFY(address.isLoopback());QVERIFY(port);
  auto id=JtAlertCompatibility::instanceId(QCoreApplication::applicationName());
  NetworkMessage::Reader heartbeat(packet);QCOMPARE(heartbeat.id(),id);
  QCOMPARE(heartbeat.type(),NetworkMessage::Heartbeat);
  while(peer.hasPendingDatagrams()) {packet.resize(int(peer.pendingDatagramSize()));peer.readDatagram(packet.data(),packet.size());}

  QString marker=protocol=="FT8"?"~":"+";
  QString row="000015 -10  0.0 1500 "+marker+" CQ K8UDP FN20";
  DoubleClickTxFixture::putRow(w->ui->decodedTextBrowser,row);
  w->postDecode(true,row);
  w->ui->decodedTextBrowser->clear(); // A display filter must not disable a published UDP reply.
  QTRY_VERIFY_WITH_TIMEOUT(peer.hasPendingDatagrams(),3000);
  packet.resize(int(peer.pendingDatagramSize()));peer.readDatagram(packet.data(),packet.size());
  NetworkMessage::Reader decoded(packet);QCOMPARE(decoded.type(),NetworkMessage::Decode);
  bool isNew=false,confidence=true,offAir=true;QTime time;qint32 snr=0;float delta=0;quint32 frequency=0;
  QByteArray mode,message;
  decoded>>isNew>>time>>snr>>delta>>frequency>>mode>>message>>confidence>>offAir;
  QCOMPARE(decoded.status(),QDataStream::Ok);QCOMPARE(decoded.id(),id);
  QCOMPARE(time,QTime(0,0,15));QCOMPARE(mode,marker.toUtf8());QVERIFY(isNew && !offAir);
  QCOMPARE(message,QByteArray("CQ K8UDP FN20"));
  QByteArray request;
  NetworkMessage::Builder reply(&request,NetworkMessage::Reply,id,decoded.schema());
  reply<<time<<snr<<delta<<frequency<<mode<<message<<confidence<<quint8(0);
  QSignalSpy received(w->m_messageClient,&MessageClient::reply);QVERIFY(received.isValid());
  TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  QCOMPARE(peer.writeDatagram(request,address,port),qint64(request.size()));
  QTRY_COMPARE_WITH_TIMEOUT(received.size(),1,3000);
  QCOMPARE(w->m_hisCall,accept?QString("K8UDP"):QString());
  QVERIFY(!w->m_enableTx && !w->m_manualTxAuthorized && !w->m_txSession.active());
  w->startTx2();QCOMPARE(g_iptt,0);QCOMPARE(ptt.pttOnCount(),0);
  if(accept) {
    manualEnable();QVERIFY(w->m_enableTx && w->m_manualTxAuthorized);
    w->clearDX("already enabled UDP selection");
    QCOMPARE(peer.writeDatagram(request,address,port),qint64(request.size()));
    QTRY_COMPARE_WITH_TIMEOUT(received.size(),2,3000);
    QCOMPARE(w->m_hisCall,QString("K8UDP"));QVERIFY(w->m_enableTx && w->m_manualTxAuthorized);
    w->on_stopTxButton_clicked();
    QCOMPARE(peer.writeDatagram(request,address,port),qint64(request.size()));
    QTRY_COMPARE_WITH_TIMEOUT(received.size(),3,3000);
    QVERIFY(!w->m_enableTx && !w->m_manualTxAuthorized);QCOMPARE(ptt.pttOnCount(),0);
  }
}
