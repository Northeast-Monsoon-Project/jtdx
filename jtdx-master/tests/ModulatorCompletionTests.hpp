// Source consumption tests: no sound device, CAT connection or RF is opened.
#include <QtWebSockets/QWebSocketServer>
#include <vector>

void RegressionTest::modulatorWaveformCompletion()
{
  w->on_stopTxButton_clicked();stopWaveAudio();
  std::vector<float> original(std::begin(foxcom_.wave),std::end(foxcom_.wave));
  const int cw=icw[0];icw[0]=0;
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&] {
    std::copy(original.begin(),original.end(),std::begin(foxcom_.wave));icw[0]=cw;
  }};
  std::fill(std::begin(foxcom_.wave),std::end(foxcom_.wave),.25f);
  JTDXDateTime clock;
  struct Case {unsigned symbols;double nsps;};
  for(auto c:{Case{79,1920},Case{105,576},Case{105,288}}) {
    Modulator source(48000,15,&clock);
    auto prepare=[&] {
      source.initialize(QIODevice::ReadOnly,AudioDevice::Mono);
      source.m_state=Modulator::Active;source.m_pcmMode=false;source.m_waveformConsumed=false;source.m_waveformStarted=false;
      source.m_waveformGeneration=quint64(c.nsps);
      source.m_symbolsLength=c.symbols;source.m_nsps=c.nsps;source.m_ic=0;
      source.m_TRperiod=15;source.m_toneSpacing=-2;source.m_tuning=false;
      source.m_addNoise=false;source.m_amp=32767;
    };
    QSignalSpy consumed(&source,&Modulator::waveformConsumed);
    QSignalSpy started(&source,&Modulator::waveformStarted);
    QSignalSpy pcmConsumed(&source,&Modulator::pcmConsumed);
    QList<quint64> queued;QObject receiver;
    connect(&source,&Modulator::waveformConsumed,&receiver,[&](quint64 generation){queued.append(generation);},Qt::QueuedConnection);
    prepare();const unsigned frames=unsigned(c.symbols*4*c.nsps);
    QByteArray data(8192,0);
    for(unsigned offset=0;offset<frames;) {
      auto count=qMin(4096u,frames-offset);
      QCOMPARE(source.readData(data.data(),count*2),qint64(count*2));
      offset+=count;QCOMPARE(source.m_ic,offset);
      QCOMPARE(consumed.size(),offset==frames?1:0);
      QCOMPARE(started.size(),1);QCOMPARE(started.first().first().toULongLong(),quint64(c.nsps));
    }
    QCOMPARE(reinterpret_cast<qint16 const*>(data.constData())[0],qint16(8192));
    QCOMPARE(source.readData(data.data(),8),qint64(8));
    QCOMPARE(data.left(8),QByteArray(8,0));QCOMPARE(source.m_ic,frames);
    QCOMPARE(consumed.size(),1);QCOMPARE(pcmConsumed.size(),0);
    QCOMPARE(consumed.first().first().toULongLong(),quint64(c.nsps));
    source.stop();QCOMPARE(consumed.size(),1);
    ++source.m_waveformGeneration;QCoreApplication::processEvents();
    QCOMPARE(queued.size(),1);QCOMPARE(queued.first(),quint64(c.nsps));

    // Stopping in the middle or before the first sample is cancellation.
    for(bool partiallySent:{false,true}) {
      prepare();consumed.clear();started.clear();
      if(partiallySent)QCOMPARE(source.readData(data.data(),8),qint64(8));
      source.stop(true);QCOMPARE(consumed.size(),0);QVERIFY(!source.isActive());
      QCOMPARE(started.size(),partiallySent?1:0);
    }
    prepare();consumed.clear();started.clear();source.m_tuning=true;
    QCOMPARE(source.readData(data.data(),8),qint64(8));QCOMPARE(consumed.size(),0);QCOMPARE(started.size(),0);
    source.stop(true);
    prepare();started.clear();source.m_state=Modulator::Synchronizing;source.m_silentFrames=4;
    QCOMPARE(source.readData(data.data(),8),qint64(8));QCOMPARE(started.size(),0);
    QCOMPARE(source.readData(data.data(),8),qint64(8));QCOMPARE(started.size(),1);
    source.stop(true);
  }

  // PCM can prepend synchronization silence. Announce the first waveform
  // samples with their generation, independently of PCM source exhaustion.
  {
    Modulator source(48000,15,&clock);source.initialize(QIODevice::ReadOnly,AudioDevice::Mono);
    source.m_state=Modulator::Active;source.m_pcmMode=true;source.m_pcm=QByteArray(8,0);
    source.m_waveformGeneration=4242;source.m_pcmOffset=0;
    qToLittleEndian<qint16>(2000,reinterpret_cast<uchar*>(source.m_pcm.data()+4));
    qToLittleEndian<qint16>(-2000,reinterpret_cast<uchar*>(source.m_pcm.data()+6));
    QSignalSpy started(&source,&Modulator::waveformStarted),consumed(&source,&Modulator::pcmConsumed);
    QByteArray data(4,0);QCOMPARE(source.readData(data.data(),4),qint64(4));QCOMPARE(started.size(),0);
    QCOMPARE(source.readData(data.data(),2),qint64(2));QCOMPARE(started.size(),1);
    QCOMPARE(started.first().first().toULongLong(),quint64(4242));QCOMPARE(consumed.size(),0);
    QCOMPARE(source.readData(data.data(),2),qint64(2));QCOMPARE(started.size(),1);QCOMPARE(consumed.size(),1);
    source.stop(true);
  }

  // The oscillator modes use the same exclusive end frame. Exercise their
  // last two samples without spending the complete long transmit periods.
  for(auto c:{Case{126,4096.0*12000/11025},Case{85,6912},Case{162,8192}}) {
    Modulator source(48000,120,&clock);source.initialize(QIODevice::ReadOnly,AudioDevice::Mono);
    source.m_state=Modulator::Active;source.m_symbolsLength=c.symbols;source.m_nsps=c.nsps;
    const unsigned frames=unsigned(c.symbols*4*c.nsps);source.m_ic=frames-2;
    source.m_TRperiod=120;source.m_toneSpacing=1;source.m_amp=32767;source.m_isym0=~0u;
    QSignalSpy consumed(&source,&Modulator::waveformConsumed);QByteArray data(8,0);
    QCOMPARE(source.readData(data.data(),8),qint64(8));QCOMPARE(source.m_ic,frames);
    QCOMPARE(consumed.size(),1);QCOMPARE(data.mid(4,4),QByteArray(4,0));
    source.stop(true);QCOMPARE(consumed.size(),1);
  }
}

void RegressionTest::tciWaveformCompletion()
{
  w->on_stopTxButton_clicked();stopWaveAudio();
  // A loopback WebSocket checks packet acceptance; this is not a radio CAT
  // session and no network device receives audio or PTT commands.
  QWebSocketServer server("waveform test",QWebSocketServer::NonSecureMode);
  QVERIFY(server.listen(QHostAddress::LocalHost,0));
  QWebSocket socket;socket.open(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())));
  QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(),3000);
  std::unique_ptr<QWebSocket> peer(server.nextPendingConnection());
  QTRY_COMPARE_WITH_TIMEOUT(socket.state(),QAbstractSocket::ConnectedState,3000);
  JTDXDateTime clock;TCITransceiver rig({},"0","unused",true,1000);
  rig.commander_=&socket;rig.inConnected=true;rig.m_jtdxtime=&clock;
  QSignalSpy consumed(&rig,&Transceiver::waveformConsumed);
  QSignalSpy started(&rig,&Transceiver::waveformStarted);
  QSignalSpy failures(&rig,&Transceiver::failure);
  const int cw=icw[0];icw[0]=0;
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&] {
    rig.commander_=nullptr;icw[0]=cw;
  }};
  auto prepare=[&](unsigned symbols,double nsps) {
    rig.do_modulator_stop(true);rig.do_modulator_start(symbols,nsps,1500,-2,false,99,15,quint64(symbols+42));
    rig.m_ic=unsigned(symbols*4*nsps)-1;consumed.clear();started.clear();
  };
  Data_Stream request{};request.receiver=0;request.sampleRate=48000;request.format=3;
  request.length=8;request.type=TxChrono;
  QByteArray packet(reinterpret_cast<char const*>(&request),64);
  for(bool ft8:{true,false}) {
    unsigned symbols=ft8?79:105;double nsps=ft8?1920:576;
    prepare(symbols,nsps);
    rig.onBinaryReceived(packet);
    QCOMPARE(rig.m_ic,unsigned(symbols*4*nsps));QCOMPARE(consumed.size(),1);
    QCOMPARE(consumed.first().first().toULongLong(),quint64(symbols+42));
    QCOMPARE(started.size(),1);QCOMPARE(started.first().first().toULongLong(),quint64(symbols+42));
    rig.onBinaryReceived(packet);QCOMPARE(consumed.size(),1);QCOMPARE(started.size(),1);
    rig.do_modulator_stop(true);QCOMPARE(consumed.size(),1);
    prepare(symbols,nsps);rig.do_modulator_stop(true);
    rig.onBinaryReceived(packet);QCOMPARE(consumed.size(),0);QCOMPARE(started.size(),0);
  }
  prepare(79,1920);rig.m_state=TCITransceiver::Synchronizing;rig.m_silentFrames=100;
  rig.onBinaryReceived(packet);QCOMPARE(started.size(),0);QCOMPARE(consumed.size(),0);
  prepare(79,1920);rig.m_tuning=true;
  rig.onBinaryReceived(packet);QCOMPARE(started.size(),0);QCOMPARE(consumed.size(),0);
  rig.do_modulator_stop(true);rig.m_tuning=false;

  // PCM starts only on actual audio and its notification also crosses the
  // transport with the original generation.
  rig.tci_Ready=true;rig.PTT_=true;QByteArray pcm(16,0);
  qToLittleEndian<qint16>(2000,reinterpret_cast<uchar*>(pcm.data()+8));
  rig.set_tx_pcm(pcm,4243);started.clear();
  rig.onBinaryReceived(packet);QCOMPARE(started.size(),0);
  rig.onBinaryReceived(packet);QCOMPARE(started.size(),1);
  QCOMPARE(started.first().first().toULongLong(),quint64(4243));
  rig.do_modulator_stop(true);rig.PTT_=false;
  // A consumed last sample whose packet could not be sent is not success.
  prepare(79,1920);socket.abort();
  rig.onBinaryReceived(packet);QCOMPARE(failures.size(),1);QCOMPARE(consumed.size(),0);QCOMPARE(started.size(),0);
  QVERIFY(!rig.m_waveformConsumed);QVERIFY(!rig.m_waveformNotified);
}
