#include <QtTest>
#include <QDateTime>
#include <QHostAddress>
#include <QUdpSocket>
#include <QHostInfo>
#include <QNetworkProxy>
#include <limits>
#include <memory>

#include "JtAlertCompatibility.hpp"
#include "MessageClient.hpp"
#include "NetworkMessage.hpp"

class DirectUdpSocket : public QUdpSocket {
public:
  DirectUdpSocket() { setProxy(QNetworkProxy::NoProxy); }
};

// Exercise the actual UDP transport, without a GUI, radio, or public server.
class UdpProtocolTests : public QObject {
  Q_OBJECT
  struct Endpoint { QHostAddress address; quint16 port=0; };

  static bool receive(QUdpSocket& socket,QByteArray& bytes,Endpoint* sender=nullptr) {
    if(!socket.hasPendingDatagrams()) {
      QSignalSpy ready(&socket,&QUdpSocket::readyRead);
      if(!ready.isValid() || !ready.wait(3000))return false;
    }
    if(!socket.hasPendingDatagrams())return false;
    bytes.resize(int(socket.pendingDatagramSize()));
    Endpoint source;
    auto count=socket.readDatagram(bytes.data(),bytes.size(),&source.address,&source.port);
    if(sender)*sender=source;
    return count==bytes.size() && source.address.isLoopback();
  }

  static bool send(QUdpSocket& peer,Endpoint const& endpoint,QByteArray const& bytes) {
    return endpoint.address.isLoopback() && endpoint.port
      && peer.writeDatagram(bytes,endpoint.address,endpoint.port)==bytes.size();
  }

  static QByteArray request(NetworkMessage::Type type,QString const& id,quint32 schema) {
    QByteArray bytes;
    NetworkMessage::Builder out(&bytes,type,id,schema);
    if(type==NetworkMessage::Reply)
      out << QTime(12,34,45) << qint32(-17) << float(0.25) << quint32(1900)
          << QByteArray("~") << QByteArray("CQ K1ABC FN31") << false << quint8(2);
    else if(type==NetworkMessage::HaltTx)out << false;
    return bytes;
  }

private slots:
  void applicationProxyDoesNotBlockDirectUdp() {
    struct RestoreProxy {
      QNetworkProxy original=QNetworkProxy::applicationProxy();
      ~RestoreProxy() { QNetworkProxy::setApplicationProxy(original); }
    } restore;
    QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy,"127.0.0.1",9));
    DirectUdpSocket peer;
    QVERIFY2(peer.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)),qPrintable(peer.errorString()));
    MessageClient client("JTDX - direct UDP","3.7.0","127.0.0.1",peer.localPort());
    QByteArray packet;QVERIFY(receive(peer,packet));
    NetworkMessage::Reader heartbeat(packet);QCOMPARE(heartbeat.type(),NetworkMessage::Heartbeat);
    client.clear_decodes();QVERIFY(receive(peer,packet));
    NetworkMessage::Reader clear(packet);QCOMPARE(clear.type(),NetworkMessage::Clear);
  }

  void truncatedRequestsAreRejected() {
    DirectUdpSocket peer;QVERIFY(peer.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)));
    QString id="JTDX - protocol boundaries";
    MessageClient client(id,"3.7.0","127.0.0.1",peer.localPort());
    QByteArray packet;Endpoint endpoint;QVERIFY(receive(peer,packet,&endpoint));
    QSignalSpy replies(&client,&MessageClient::reply),replays(&client,&MessageClient::replay);
    QSignalSpy texts(&client,&MessageClient::free_text),frequencies(&client,&MessageClient::set_tx_deltafreq);
    QSignalSpy cq(&client,&MessageClient::trigger_CQ);
    auto full=request(NetworkMessage::Reply,id,2);
    QByteArray header;NetworkMessage::Builder out(&header,NetworkMessage::Reply,id,2);
    int required=full.size()-2;
    for(int size=header.size();size<required;++size)QVERIFY(send(peer,endpoint,full.left(size)));
    // Legacy Reply has six required fields; trailing low confidence and
    // modifiers remain optional for older JTAlert/WSJT-X integrations.
    QVERIFY(send(peer,endpoint,full.left(required)));
    QVERIFY(send(peer,endpoint,full.left(required+1)));
    QVERIFY(send(peer,endpoint,full));
    for(bool badTime:{false,true}) {
      QByteArray invalid;NetworkMessage::Builder body(&invalid,NetworkMessage::Reply,id,2);
      body << (badTime?QTime():QTime(12,34,45)) << qint32(-10)
           << (badTime?float(0.25):std::numeric_limits<float>::quiet_NaN()) << quint32(1900)
           << QByteArray("~") << QByteArray("CQ K1ABC FN31");
      QVERIFY(send(peer,endpoint,invalid));
    }
    for(auto type:{NetworkMessage::SetTxDeltaFreq,NetworkMessage::FreeText,NetworkMessage::TriggerCQ}) {
      QByteArray invalid;NetworkMessage::Builder body(&invalid,type,id,2);
      if(type==NetworkMessage::TriggerCQ)body << QByteArray("DX") << true; // missing Send
      else if(type==NetworkMessage::FreeText)body << quint32(8) << quint8('A'); // truncated QByteArray
      else body << quint16(1500); // only two of four frequency bytes
      QVERIFY(send(peer,endpoint,invalid));
    }
    QByteArray trigger;NetworkMessage::Builder body(&trigger,NetworkMessage::TriggerCQ,id,2);
    body << QByteArray("DX") << true << false;
    QVERIFY(send(peer,endpoint,trigger));
    QVERIFY(send(peer,endpoint,request(NetworkMessage::Replay,id,2)));
    QTRY_COMPARE(replays.count(),1);
    QCOMPARE(replies.count(),3);QCOMPARE(texts.count(),0);QCOMPARE(frequencies.count(),0);
    QCOMPARE(replies.at(0).at(7).toUInt(),quint32(0));QCOMPARE(replies.at(2).at(7).toUInt(),quint32(2));
    QCOMPARE(cq.count(),1);QCOMPARE(cq.first().at(0).toString(),QString("DX"));
    QVERIFY(cq.first().at(1).toBool());QVERIFY(!cq.first().at(2).toBool());
  }

  void serverChangesCancelOldLookup() {
    DirectUdpSocket peer;QVERIFY(peer.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)));
    MessageClient client("JTDX - DNS lifecycle","3.7.0","localhost",peer.localPort());
    client.decode(true,QTime(0,0,0),-10,.1f,1500,"~","CQ OLD FN31",false,false);
    client.set_server("127.0.0.1");
    QCOMPARE(client.server_address(),QHostAddress(QHostAddress::LocalHost));
    auto socket=client.findChild<QUdpSocket*>();QVERIFY(socket);
    // Deterministically deliver an obsolete lookup callback after a direct
    // address has replaced it. abortHostLookup alone cannot prevent this.
    QHostInfo obsolete;obsolete.setLookupId(2147483647);obsolete.setAddresses({QHostAddress("127.0.0.2")});
    QVERIFY(QMetaObject::invokeMethod(socket,"host_info_results",Qt::DirectConnection,Q_ARG(QHostInfo,obsolete)));
    QCOMPARE(client.server_address(),QHostAddress(QHostAddress::LocalHost));
    QCoreApplication::processEvents();
    QByteArray packet;Endpoint endpoint;QVERIFY(receive(peer,packet,&endpoint));
    NetworkMessage::Reader first(packet);QCOMPARE(first.type(),NetworkMessage::Heartbeat);
    QVERIFY(!peer.hasPendingDatagrams()); // Old destination's backlog was discarded.
    client.set_server();
    QVERIFY(QMetaObject::invokeMethod(socket,"host_info_results",Qt::DirectConnection,Q_ARG(QHostInfo,obsolete)));
    QVERIFY(client.server_address().isNull());
    client.decode(true,QTime(0,0,0),-10,.1f,1500,"~","CQ DISABLED FN31",false,false);
    QCoreApplication::processEvents();QVERIFY(!peer.hasPendingDatagrams());
  }

  void dnsBacklogIsBoundedAndRecent() {
    DirectUdpSocket ipv4,ipv6;
    QVERIFY2(ipv4.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)),qPrintable(ipv4.errorString()));
    QVERIFY2(ipv6.bind(QHostAddress(QHostAddress::LocalHostIPv6),ipv4.localPort()),qPrintable(ipv6.errorString()));
    ipv4.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption,1024*1024);
    ipv6.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption,1024*1024);
    MessageClient client("JTDX - DNS backlog","3.7.0","localhost",ipv4.localPort());
    // No event processing occurs until every decode has been submitted, so
    // lookup delivery is still pending even when localhost is cached.
    for(int index=0;index<1000;++index)
      client.decode(true,QTime(0,0,0),-10,.1f,1500,"~",QString("CQ TEST%1 FN31").arg(index),false,false);
    QTRY_VERIFY(!client.server_address().isNull());
    auto& peer=client.server_address().protocol()==QAbstractSocket::IPv6Protocol?ipv6:ipv4;
    QByteArray packet;Endpoint endpoint;QVERIFY(receive(peer,packet,&endpoint));
    NetworkMessage::Reader heartbeat(packet);QCOMPARE(heartbeat.type(),NetworkMessage::Heartbeat);
    int count=0;QString last;
    while(peer.hasPendingDatagrams()) {
      QVERIFY(receive(peer,packet));NetworkMessage::Reader in(packet);
      QCOMPARE(in.type(),NetworkMessage::Decode);
      bool isNew;QTime time;qint32 snr;float dt;quint32 df;QByteArray mode,message;
      in >> isNew >> time >> snr >> dt >> df >> mode >> message;
      last=QString::fromUtf8(message);++count;
    }
    QCOMPARE(count,128);QCOMPARE(last,QString("CQ TEST999 FN31"));
  }

  void newPortStartsFreshSession() {
    DirectUdpSocket first,second;
    QVERIFY(first.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)));
    QVERIFY(second.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)));
    MessageClient client("JTDX - port","3.7.0","127.0.0.1",first.localPort());
    QByteArray packet;QVERIFY(receive(first,packet));
    client.clear_decodes();QVERIFY(receive(first,packet));
    client.set_server_port(second.localPort());QVERIFY(receive(second,packet));
    NetworkMessage::Reader heartbeat(packet);QCOMPARE(heartbeat.type(),NetworkMessage::Heartbeat);
    client.clear_decodes();QVERIFY(receive(second,packet));
    NetworkMessage::Reader clear(packet);QCOMPARE(clear.type(),NetworkMessage::Clear);
  }

  void outgoingPackets_data() {
    QTest::addColumn<quint32>("schema");
    QTest::newRow("initial-schema-2") << quint32(2);
    QTest::newRow("negotiated-schema-3") << quint32(3);
  }

  void outgoingPackets() {
    QFETCH(quint32,schema);
    DirectUdpSocket peer;
    QVERIFY(peer.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)));
    auto id=JtAlertCompatibility::instanceId("JTDX-F");
    QCOMPARE(id,QString("JTDX - JTDX-F"));
    auto client=std::unique_ptr<MessageClient>(new MessageClient(id,"3.6.6","127.0.0.1",peer.localPort()));
    QSignalSpy errors(client.get(),&MessageClient::error);QVERIFY(errors.isValid());
    QByteArray packet;Endpoint endpoint;
    QVERIFY(receive(peer,packet,&endpoint));
    {
      NetworkMessage::Reader in(packet);
      QCOMPARE(in.type(),NetworkMessage::Heartbeat);QCOMPARE(in.id(),id);QCOMPARE(in.schema(),quint32(2));
      quint32 maximum=0;QByteArray version;
      in >> maximum >> version;
      QCOMPARE(maximum,quint32(NetworkMessage::Builder::schema_number));
      QCOMPARE(version,QByteArray("3.6.6")); // Never claim to be the upstream 2.2.159 release.
      QCOMPARE(in.status(),QDataStream::Ok);QVERIFY(in.atEnd());
    }
    QCOMPARE(client->server_address(),QHostAddress(QHostAddress::LocalHost));
    QCOMPARE(client->server_port(),peer.localPort());
    if(schema==3) {
      QByteArray heartbeat;
      NetworkMessage::Builder out(&heartbeat,NetworkMessage::Heartbeat,id,3);
      out << quint32(3) << QByteArray("2.81.7");
      QVERIFY(send(peer,endpoint,heartbeat));
      // Replay acts as a processing fence after the negotiation datagram.
      QSignalSpy replay(client.get(),&MessageClient::replay);QVERIFY(replay.isValid());
      QVERIFY(send(peer,endpoint,request(NetworkMessage::Replay,id,schema)));
      QTRY_COMPARE(replay.count(),1);
    }

    client->status_update(14074000,"FT8","K1ABC","-12","FT8",false,false,true,
      1700,1200,"N0CALL","AA00","FN31",false,"",false,true,false);
    QVERIFY(receive(peer,packet));
    {
      NetworkMessage::Reader in(packet);
      QCOMPARE(in.type(),NetworkMessage::Status);QCOMPARE(in.id(),id);QCOMPARE(in.schema(),schema);
      quint64 frequency=0;QByteArray mode,dx,report,txMode,de,deGrid,dxGrid,submode;
      bool enabled=true,transmitting=true,decoding=false,watchdog=true,fast=true,first=false;
      qint32 rxDf=0,txDf=0;
      in >> frequency >> mode >> dx >> report >> txMode >> enabled >> transmitting >> decoding
         >> rxDf >> txDf >> de >> deGrid >> dxGrid >> watchdog >> submode >> fast >> first;
      QCOMPARE(in.status(),QDataStream::Ok);QVERIFY(in.atEnd());
      QCOMPARE(frequency,quint64(14074000));QCOMPARE(mode,QByteArray("FT8"));
      QCOMPARE(dx,QByteArray("K1ABC"));QCOMPARE(report,QByteArray("-12"));QCOMPARE(txMode,QByteArray("FT8"));
      QVERIFY(!enabled && !transmitting && decoding);QCOMPARE(rxDf,qint32(1700));QCOMPARE(txDf,qint32(1200));
      QCOMPARE(de,QByteArray("N0CALL"));QCOMPARE(deGrid,QByteArray("AA00"));QCOMPARE(dxGrid,QByteArray("FN31"));
      QVERIFY(!watchdog && submode.isEmpty() && !fast && first);
    }

    client->decode(true,QTime(12,34,45),-17,0.25f,1900,"~","CQ K1ABC FN31",false,false);
    QVERIFY(receive(peer,packet));
    {
      NetworkMessage::Reader in(packet);
      QCOMPARE(in.type(),NetworkMessage::Decode);QCOMPARE(in.id(),id);QCOMPARE(in.schema(),schema);
      bool isNew=false,low=true,offAir=true;QTime time;qint32 snr=0;float dt=0;quint32 df=0;
      QByteArray mode,message;
      in >> isNew >> time >> snr >> dt >> df >> mode >> message >> low >> offAir;
      QCOMPARE(in.status(),QDataStream::Ok);QVERIFY(in.atEnd());
      QVERIFY(isNew && !low && !offAir);QCOMPARE(time,QTime(12,34,45));QCOMPARE(snr,qint32(-17));
      QCOMPARE(dt,0.25f);QCOMPARE(df,quint32(1900));QCOMPARE(mode,QByteArray("~"));
      QCOMPARE(message,QByteArray("CQ K1ABC FN31"));
    }

    QByteArray record("<CALL:5>K1ABC<STATION_CALLSIGN:6>N0CALL<MODE:3>FT8");
    client->logged_ADIF(record);
    QVERIFY(receive(peer,packet));
    {
      NetworkMessage::Reader in(packet);
      QCOMPARE(in.type(),NetworkMessage::LoggedADIF);QCOMPARE(in.id(),id);QCOMPARE(in.schema(),schema);
      QByteArray adif;in >> adif;
      QCOMPARE(in.status(),QDataStream::Ok);QVERIFY(in.atEnd());
      QCOMPARE(adif,QByteArray("\n<adif_ver:5>3.1.0\n<programid:4>JTDX\n<EOH>\n")+record);
    }

    QDateTime on(QDate(2026,10,31),QTime(23,59,45),Qt::UTC);
    auto off=on.addSecs(45);
    QString comments=QString::fromUtf8("合成协议测试");
    client->qso_logged(off,"K1ABC","FN31",14074000,"FT8","-12","-17","80",comments,"Test",on,
      "N0OP","N0CALL","AA00");
    QVERIFY(receive(peer,packet));
    {
      NetworkMessage::Reader in(packet);
      QCOMPARE(in.type(),NetworkMessage::QSOLogged);QCOMPARE(in.id(),id);QCOMPARE(in.schema(),schema);
      QDateTime readOff,readOn;quint64 frequency=0;
      QByteArray call,grid,mode,sent,received,power,comment,name,op,myCall,myGrid;
      in >> readOff >> call >> grid >> frequency >> mode >> sent >> received >> power
         >> comment >> name >> readOn >> op >> myCall >> myGrid;
      QCOMPARE(in.status(),QDataStream::Ok);QVERIFY(in.atEnd());
      QCOMPARE(readOff,off);QCOMPARE(readOn,on);QCOMPARE(readOff.timeSpec(),Qt::UTC);QCOMPARE(readOn.timeSpec(),Qt::UTC);
      QCOMPARE(call,QByteArray("K1ABC"));QCOMPARE(grid,QByteArray("FN31"));QCOMPARE(frequency,quint64(14074000));
      QCOMPARE(mode,QByteArray("FT8"));QCOMPARE(sent,QByteArray("-12"));QCOMPARE(received,QByteArray("-17"));
      QCOMPARE(power,QByteArray("80"));QCOMPARE(comment,comments.toUtf8());QCOMPARE(name,QByteArray("Test"));
      QCOMPARE(op,QByteArray("N0OP"));QCOMPARE(myCall,QByteArray("N0CALL"));QCOMPARE(myGrid,QByteArray("AA00"));
    }

    client->clear_decodes();
    QVERIFY(receive(peer,packet));
    {
      NetworkMessage::Reader in(packet);
      QCOMPARE(in.type(),NetworkMessage::Clear);QCOMPARE(in.id(),id);QCOMPARE(in.schema(),schema);
      QCOMPARE(in.status(),QDataStream::Ok);QVERIFY(in.atEnd());
    }
    QCOMPARE(errors.count(),0);
    client.reset();
    QVERIFY(receive(peer,packet));
    NetworkMessage::Reader close(packet);
    QCOMPARE(close.type(),NetworkMessage::Close);QCOMPARE(close.id(),id);QCOMPARE(close.schema(),schema);
    QCOMPARE(close.status(),QDataStream::Ok);QVERIFY(close.atEnd());
  }

  void incomingRequestsStayWithTheirInstance() {
    DirectUdpSocket peer;
    QVERIFY(peer.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)));
    auto firstId=JtAlertCompatibility::instanceId("JTDX-F");
    auto secondId=JtAlertCompatibility::instanceId("JTDX-F - TEST");
    QVERIFY(firstId!=secondId);QVERIFY(secondId.startsWith("JTDX - "));
    MessageClient first(firstId,"3.6.6","127.0.0.1",peer.localPort());
    MessageClient second(secondId,"3.6.6","127.0.0.1",peer.localPort());
    Endpoint firstEndpoint,secondEndpoint;QByteArray packet;
    for(int i=0;i<2;++i) {
      Endpoint sender;QVERIFY(receive(peer,packet,&sender));
      NetworkMessage::Reader in(packet);QCOMPARE(in.type(),NetworkMessage::Heartbeat);
      QVERIFY(in.id()==firstId || in.id()==secondId);
      if(in.id()==firstId)firstEndpoint=sender;else secondEndpoint=sender;
    }
    QVERIFY(firstEndpoint.port && secondEndpoint.port && firstEndpoint.port!=secondEndpoint.port);
    QSignalSpy firstReply(&first,&MessageClient::reply),secondReply(&second,&MessageClient::reply);
    QSignalSpy firstReplay(&first,&MessageClient::replay),secondReplay(&second,&MessageClient::replay);
    QSignalSpy firstHalt(&first,&MessageClient::halt_tx),secondHalt(&second,&MessageClient::halt_tx);
    QSignalSpy firstErrors(&first,&MessageClient::error),secondErrors(&second,&MessageClient::error);
    for(auto spy:{&firstReply,&secondReply,&firstReplay,&secondReplay,&firstHalt,&secondHalt,&firstErrors,&secondErrors})
      QVERIFY(spy->isValid());

    // A datagram delivered to the correct socket but carrying another ID is
    // neither actionable nor allowed to change this client's negotiated schema.
    for(auto wrongId:{secondId,QString("JTDX"),QString("JTDX-F")})
      for(auto type:{NetworkMessage::Reply,NetworkMessage::Replay,NetworkMessage::HaltTx})
        QVERIFY(send(peer,firstEndpoint,request(type,wrongId,3)));
    QVERIFY(send(peer,firstEndpoint,request(NetworkMessage::Replay,firstId,2)));
    QTRY_COMPARE(firstReplay.count(),1);
    QCOMPARE(firstReply.count(),0);QCOMPARE(firstHalt.count(),0);
    QCOMPARE(secondReplay.count(),0);QCOMPARE(secondReply.count(),0);QCOMPARE(secondHalt.count(),0);
    first.clear_decodes();QVERIFY(receive(peer,packet));
    {
      NetworkMessage::Reader in(packet);
      QCOMPARE(in.type(),NetworkMessage::Clear);QCOMPARE(in.id(),firstId);QCOMPARE(in.schema(),quint32(2));
    }

    QVERIFY(send(peer,firstEndpoint,request(NetworkMessage::Reply,firstId,2)));
    QTRY_COMPARE(firstReply.count(),1);
    auto args=firstReply.first();QCOMPARE(args.size(),8);
    QCOMPARE(args.at(0).toTime(),QTime(12,34,45));QCOMPARE(args.at(1).toInt(),-17);
    QCOMPARE(args.at(2).toFloat(),0.25f);QCOMPARE(args.at(3).toUInt(),quint32(1900));
    QCOMPARE(args.at(4).toString(),QString("~"));QCOMPARE(args.at(5).toString(),QString("CQ K1ABC FN31"));
    QVERIFY(!args.at(6).toBool());QCOMPARE(args.at(7).toUInt(),quint32(2));
    QCOMPARE(secondReply.count(),0);
    QVERIFY(send(peer,secondEndpoint,request(NetworkMessage::Reply,secondId,3)));
    QTRY_COMPARE(secondReply.count(),1);QCOMPARE(firstReply.count(),1);
    QVERIFY(send(peer,secondEndpoint,request(NetworkMessage::Replay,secondId,3)));
    QTRY_COMPARE(secondReplay.count(),1);QCOMPARE(firstReplay.count(),1);
    QVERIFY(send(peer,secondEndpoint,request(NetworkMessage::HaltTx,secondId,3)));
    QTRY_COMPARE(secondHalt.count(),1);QVERIFY(!secondHalt.first().first().toBool());
    QCOMPARE(firstHalt.count(),0);
    QByteArray halt;
    NetworkMessage::Builder out(&halt,NetworkMessage::HaltTx,firstId,2);out << true;
    QVERIFY(send(peer,firstEndpoint,halt));
    QTRY_COMPARE(firstHalt.count(),1);QVERIFY(firstHalt.first().first().toBool());
    QCOMPARE(firstErrors.count(),0);QCOMPARE(secondErrors.count(),0);
  }
};

QTEST_GUILESS_MAIN(UdpProtocolTests)
#include "UdpProtocolTests.moc"
