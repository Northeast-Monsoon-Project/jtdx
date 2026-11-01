// Radio/audio adapters derived from WSJT-X v3.3.0-beta5 unit fixtures.
// Only Hamlib Dummy and in-memory PCM are used; no hardware or RF.
#include <algorithm>
#include <memory>
#include <vector>
#include <QMessageBox>
#include "HamlibTransceiver.hpp"
#include "HamlibModePolicy.hpp"
#include "RxAudioBufferPolicy.hpp"

namespace RadioAudioFixture
{
  class RigFixture
  {
  public:
    explicit RigFixture (vfo_t receive_vfo, bool main_sub = false, bool single_vfo_addressing = false)
      : rx_vfo {receive_vfo}
      , tx_vfo {main_sub ? (receive_vfo == RIG_VFO_MAIN ? RIG_VFO_SUB : RIG_VFO_MAIN) : receive_vfo == RIG_VFO_A ? RIG_VFO_B : RIG_VFO_A}
      , vfos {main_sub ? RIG_VFO_MAIN | RIG_VFO_SUB : RIG_VFO_A | RIG_VFO_B}
    {
      active = this;
      auto dummy = std::unique_ptr<RIG, decltype (&rig_cleanup)> {rig_init (RIG_MODEL_DUMMY), rig_cleanup};
      if (!dummy) return;
      std::vector<rig_model_t> occupied_buckets;
      rig_list_foreach ([] (rig_caps const * existing, rig_ptr_t context)
                        {
                          static_cast<std::vector<rig_model_t> *> (context)
                            ->push_back (existing->rig_model % 65535);
                          return 1;
                        }, &occupied_buckets);
      // Empty buckets avoid Hamlib 4.7.2's fatal collisions and remain valid with newer registries.
      for (int index = MAX_MODELS_PER_BACKEND - 1; index > 0; --index)
        {
          auto const candidate = RIG_MAKE_MODEL (RIG_DUMMY, index);
          if (std::find (occupied_buckets.begin (), occupied_buckets.end (), candidate % 65535)
              == occupied_buckets.end ())
            {
              model = candidate;
              break;
            }
        }
      if (!model) return;
      original = *dummy->caps;
      caps = original;
      caps.rig_model = model;
      caps.model_name = "JTDX-F split-mode test";
      // The constructor accepts network rigs; Dummy initializes an in-memory port.
      caps.port_type = RIG_PORT_NETWORK;
      caps.ptt_type = RIG_PTT_RIG;
      caps.rig_init = [] (RIG * rig)
        {
          auto& self = *active;
          auto rc = self.original.rig_init (rig);
          if (RIG_OK != rc) return rc;
          self.rig = rig;
          rig->state.vfo_list = self.vfos;
          // Hamlib Dummy defaults to PTT=None. This adapter models a CAT
          // rig, so enable its in-memory set_ptt callback explicitly.
          rc = rig_set_conf (rig, rig_token_lookup (rig, "ptt_type"), "RIG");
          if (RIG_OK != rc) return rc;
          rc = rig_set_conf (rig, rig_token_lookup (rig, "rig_pathname"), "");
          if (RIG_OK != rc) return rc;
          rc = rig_set_conf (rig, rig_token_lookup (rig, "poll_interval"), "0");
          if (RIG_OK != rc) return rc;
          return self.original.set_vfo (rig, self.rx_vfo);
        };
      caps.set_mode = [] (RIG * rig, vfo_t vfo, rmode_t mode, pbwidth_t width)
        {
          active->mode_targets.push_back (vfo);
          if (active->reject_tx_mode && vfo == active->tx_vfo) return -RIG_EIO;
          return active->original.set_mode (rig, vfo, mode, width);
        };
      caps.get_mode = [] (RIG * rig, vfo_t vfo, rmode_t * mode, pbwidth_t * width)
        {
          active->mode_queries.push_back (vfo);
          return active->original.get_mode (rig, vfo, mode, width);
        };
      caps.set_freq = [] (RIG * rig, vfo_t vfo, freq_t frequency)
        {
          active->frequency_targets.push_back (vfo);
          auto rc = active->original.set_freq (rig, vfo, frequency);
          if (rc == RIG_OK && active->recall_tx_mode)
            {
              active->recall_tx_mode = false;
              rc = active->original.set_mode (rig, active->tx_vfo, RIG_MODE_LSB, 2400);
            }
          return rc;
        };
      caps.get_vfo = [] (RIG * rig, vfo_t * vfo)
        {
          ++active->vfo_queries;
          return active->original.get_vfo (rig, vfo);
        };
      if (single_vfo_addressing) caps.get_vfo = nullptr;
      caps.set_ptt = [] (RIG * rig, vfo_t vfo, ptt_t ptt)
        {
          if (RIG_PTT_OFF != ptt)
            {
              pbwidth_t width;
              active->mode_at_ptt_result = active->original.get_mode (rig, active->tx_vfo,
                                                                     &active->mode_at_ptt, &width);
              ++active->ptt_on_count;
            }
          return active->original.set_ptt (rig, vfo, ptt);
        };
      registered = RIG_OK == rig_register (&caps);
    }

    ~RigFixture ()
    {
      // Hamlib workers must finish before their capabilities and callback context die.
      if (transceiver) transceiver->stop ();
      transceiver.reset ();
      if (registered) rig_unregister (model);
      active = nullptr;
    }

    void start ()
    {
      TransceiverFactory::ParameterPack parameters {};
      parameters.ptt_type = TransceiverFactory::PTT_method_CAT;
      parameters.split_mode = TransceiverFactory::split_mode_rig;
      transceiver.reset (new HamlibTransceiver {model, parameters});
      QObject::connect (transceiver.get (), &Transceiver::failure,
                        [&] (QString const& message) { failure = message; });
      QObject::connect (transceiver.get (), &Transceiver::update,
                        [&] (Transceiver::TransceiverState const& value, unsigned) { state = value; });
      transceiver->start (1, &clock);
    }

    void establish_split ()
    {
      state.online (true);
      state.frequency (14074000);
      state.mode (Transceiver::USB);
      transceiver->set (state, 2);
      state.tx_frequency (14075000);
      state.split (true);
      transceiver->set (state, 3);
      mode_targets.clear ();
      vfo_queries = 0;
    }

    static RigFixture * active;
    rig_model_t model = 0;
    vfo_t rx_vfo;
    vfo_t tx_vfo;
    vfo_t vfos;
    bool reject_tx_mode = false;
    bool recall_tx_mode = false;
    bool registered = false;
    RIG * rig = nullptr;
    rig_caps original {};
    rig_caps caps {};
    JTDXDateTime clock;
    std::unique_ptr<HamlibTransceiver> transceiver;
    Transceiver::TransceiverState state;
    QString failure;
    std::vector<vfo_t> mode_targets;
    std::vector<vfo_t> mode_queries;
    std::vector<vfo_t> frequency_targets;
    int vfo_queries = 0;
    int ptt_on_count = 0;
    int mode_at_ptt_result = -RIG_EIO;
    rmode_t mode_at_ptt = RIG_MODE_NONE;
  };

  RigFixture * RigFixture::active = nullptr;
}

void RegressionTest::radioAudioPolicies()
{
  using namespace RxAudioBufferPolicy;
  QCOMPARE(select(0,false),-1);QCOMPARE(select(0,true),48000);
  for(bool duplex:{false,true}) {
    QCOMPARE(select(-1,duplex),-1);QCOMPARE(select(4096,duplex),4096);
  }
  HamlibVfoRoleState roles;
  roles.observe_active_vfo(RIG_VFO_B,false);QVERIFY(roles.reversed());
  roles.observe_active_vfo(RIG_VFO_A,true);QVERIFY(roles.reversed());
  roles.observe_active_vfo(RIG_VFO_CURR,false);QVERIFY(roles.reversed());
  auto pair=roles.resolve(RIG_VFO_A,RIG_VFO_B);
  QCOMPARE(std::get<0>(pair),vfo_t(RIG_VFO_B));QCOMPARE(std::get<1>(pair),vfo_t(RIG_VFO_A));
  roles.observe_active_vfo(RIG_VFO_MAIN,false);QVERIFY(!roles.reversed());
  roles.observe_active_vfo(RIG_VFO_SUB,false);QVERIFY(roles.reversed());
  roles.reset();QVERIFY(!roles.reversed());
  for(auto mode:{RIG_MODE_USBD1,RIG_MODE_USBD2,RIG_MODE_USBD3}) {
    QVERIFY(HamlibModePolicy::satisfies(RIG_MODE_PKTUSB,mode));
    QVERIFY(!HamlibModePolicy::satisfies(RIG_MODE_PKTLSB,mode));
    QCOMPARE(HamlibModePolicy::preserveProfile(RIG_MODE_PKTUSB,mode),rmode_t(mode));
  }
  for(auto mode:{RIG_MODE_LSBD1,RIG_MODE_LSBD2,RIG_MODE_LSBD3}) {
    QVERIFY(HamlibModePolicy::satisfies(RIG_MODE_PKTLSB,mode));
    QVERIFY(!HamlibModePolicy::satisfies(RIG_MODE_PKTUSB,mode));
  }
  // A band-memory recall can change the backend while Hamlib retains USB.
  auto rig=std::unique_ptr<RIG,decltype(&rig_cleanup)>{rig_init(RIG_MODEL_DUMMY),rig_cleanup};
  QVERIFY(rig);QCOMPARE(rig_set_conf(rig.get(),rig_token_lookup(rig.get(),"poll_interval"),"0"),RIG_OK);
  QCOMPARE(rig_open(rig.get()),RIG_OK);
  struct Close {RIG* rig;~Close(){rig_close(rig);}} close{rig.get()};
  QCOMPARE(rig_set_mode(rig.get(),RIG_VFO_A,RIG_MODE_USB,2400),RIG_OK);
  QCOMPARE(rig_set_cache_timeout_ms(rig.get(),HAMLIB_CACHE_ALL,HAMLIB_CACHE_ALWAYS),RIG_OK);
  rmode_t mode;pbwidth_t width;
  QCOMPARE(rig_get_mode(rig.get(),RIG_VFO_A,&mode,&width),RIG_OK);QCOMPARE(mode,rmode_t(RIG_MODE_USB));
  QCOMPARE(rig->caps->set_mode(rig.get(),RIG_VFO_A,RIG_MODE_LSB,1800),RIG_OK);
  QCOMPARE(rig_get_mode(rig.get(),RIG_VFO_A,&mode,&width),RIG_OK);QCOMPARE(mode,rmode_t(RIG_MODE_USB));
  QCOMPARE(HamlibModePolicy::uncachedMode(rig.get(),RIG_VFO_A,&mode,&width),RIG_OK);
  QCOMPARE(mode,rmode_t(RIG_MODE_LSB));QCOMPARE(width,pbwidth_t(1800));
  QCOMPARE(rig_get_cache_timeout_ms(rig.get(),HAMLIB_CACHE_ALL),HAMLIB_CACHE_ALWAYS);
}

void RegressionTest::soundOutputLifecycle()
{
  SoundOutput output;
  QSignalSpy errors(&output,&SoundOutput::playbackError),drained(&output,&SoundOutput::playbackDrained);
  QSignalSpy configuration(&output,&SoundOutput::error);
  QBuffer source;source.setData(QByteArray(32,'x'));QVERIFY(source.open(QIODevice::ReadOnly));
  output.setFormat(QAudioDeviceInfo(),0);
  QCOMPARE(configuration.size(),1);QVERIFY(!output.m_stream);
  output.restart(&source,100);QCOMPARE(errors.size(),1);QVERIFY(!source.isOpen());
  QCOMPARE(errors.last().first().toULongLong(),quint64(100));QCOMPARE(drained.size(),0);
  auto arm=[&](quint64 generation) {
    output.halt(true);QVERIFY(source.open(QIODevice::ReadOnly));
    output.m_source=&source;output.m_generation=generation;output.m_playing=true;
    errors.clear();drained.clear();
  };
  // Failure is reported once, after the source is closed; stale callbacks
  // from a stopped/replaced backend do not touch the current source.
  arm(101);auto serial=output.m_serial;
  output.handleStateChanged(QAudio::StoppedState,QAudio::FatalError,serial-1);
  QVERIFY(source.isOpen());QCOMPARE(errors.size(),0);
  output.handleStateChanged(QAudio::StoppedState,QAudio::FatalError,serial);
  QVERIFY(!source.isOpen());QCOMPARE(errors.size(),1);QCOMPARE(drained.size(),0);
  output.handleStateChanged(QAudio::IdleState,QAudio::NoError,serial);QCOMPARE(errors.size(),1);
  arm(102);serial=output.m_serial;output.sourceExhausted(101);
  QVERIFY(!output.m_sourceExhausted);
  output.handleStateChanged(QAudio::IdleState,QAudio::UnderrunError,serial);
  QCOMPARE(errors.size(),1);QCOMPARE(drained.size(),0);QVERIFY(!source.isOpen());
  arm(103);serial=output.m_serial;output.sourceExhausted(103);
  output.handleStateChanged(QAudio::IdleState,QAudio::UnderrunError,serial);
  output.handleStateChanged(QAudio::IdleState,QAudio::NoError,serial);
  QCOMPARE(errors.size(),0);QCOMPARE(drained.size(),1);
  QCOMPARE(drained.first().first().toULongLong(),quint64(103));
  output.reset();source.close();
  output.handleStateChanged(QAudio::StoppedState,QAudio::IOError,serial);QCOMPARE(errors.size(),0);
  // Immediate Stop invalidates callbacks even if a new request reuses a
  // generation (e.g. tune) or the same source object.
  arm(103);output.handleStateChanged(QAudio::StoppedState,QAudio::FatalError,serial);
  QVERIFY(source.isOpen());QCOMPARE(errors.size(),0);output.halt(true,true);

  // The actual Modulator must leave Active on a rejected backend and must
  // not synthesize source completion when no samples were delivered.
  JTDXDateTime clock;Modulator modulator(48000,15,&clock);
  QSignalSpy consumed(&modulator,&Modulator::pcmConsumed),wave(&modulator,&Modulator::waveformConsumed);
  errors.clear();modulator.startPcm(QByteArray(16,1),&output,AudioDevice::Mono,104);
  QVERIFY(!modulator.isActive());QVERIFY(!modulator.isOpen());
  QCOMPARE(consumed.size(),0);QCOMPARE(wave.size(),0);QCOMPARE(errors.size(),1);
  QCOMPARE(errors.last().first().toULongLong(),quint64(104));
}

void RegressionTest::hamlibSplitModeBeforePtt_data()
{
  QTest::addColumn<int>("receiveVfo");QTest::addColumn<bool>("mainSub");
  QTest::newRow("A-to-B")<<int(RIG_VFO_A)<<false;
  QTest::newRow("B-to-A")<<int(RIG_VFO_B)<<false;
  QTest::newRow("MAIN-to-SUB")<<int(RIG_VFO_MAIN)<<true;
  QTest::newRow("SUB-to-MAIN")<<int(RIG_VFO_SUB)<<true;
}

void RegressionTest::hamlibSplitModeBeforePtt()
{
  QFETCH(int,receiveVfo);QFETCH(bool,mainSub);
  RadioAudioFixture::RigFixture fixture{vfo_t(receiveVfo),mainSub};
  QVERIFY(fixture.registered);fixture.start();QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure));
  fixture.establish_split();QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure));
  freq_t receiveFrequency=0;
  QCOMPARE(fixture.original.get_freq(fixture.rig,fixture.rx_vfo,&receiveFrequency),RIG_OK);
  QCOMPARE(receiveFrequency,freq_t(14074000));
  fixture.state.mode(Transceiver::DIG_U);fixture.state.ptt(true);
  fixture.transceiver->set(fixture.state,4);
  QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure));QCOMPARE(fixture.ptt_on_count,1);
  QCOMPARE(fixture.mode_at_ptt_result,RIG_OK);QCOMPARE(fixture.mode_at_ptt,rmode_t(RIG_MODE_PKTUSB));
  QVERIFY(std::find(fixture.mode_targets.begin(),fixture.mode_targets.end(),fixture.tx_vfo)!=fixture.mode_targets.end());
}

void RegressionTest::hamlibSplitBandRecallAndFailure()
{
  for(bool reject:{false,true}) {
    RadioAudioFixture::RigFixture fixture{RIG_VFO_B};
    QVERIFY(fixture.registered);fixture.start();fixture.establish_split();
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure));
    fixture.recall_tx_mode=true;fixture.reject_tx_mode=reject;
    fixture.state.frequency(7074000);fixture.state.tx_frequency(7075000);fixture.state.ptt(true);
    fixture.transceiver->set(fixture.state,4);
    if(reject) {
      QVERIFY(!fixture.failure.isEmpty());QCOMPARE(fixture.ptt_on_count,0);
    } else {
      QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure));QCOMPARE(fixture.ptt_on_count,1);
      QCOMPARE(fixture.mode_at_ptt,rmode_t(RIG_MODE_USB));
      freq_t txFrequency=0;
      QCOMPARE(fixture.original.get_freq(fixture.rig,fixture.tx_vfo,&txFrequency),RIG_OK);
      QCOMPARE(txFrequency,freq_t(7075000));
    }
  }
  // Some radios report the transmitting VFO as current while keyed. A
  // subsequent split-frequency adjustment must retain the original RX role.
  RadioAudioFixture::RigFixture keyed{RIG_VFO_B};
  QVERIFY(keyed.registered);keyed.start();keyed.establish_split();
  QVERIFY2(keyed.failure.isEmpty(),qPrintable(keyed.failure));
  keyed.state.ptt(true);keyed.transceiver->set(keyed.state,4);
  QCOMPARE(rig_set_vfo(keyed.rig,RIG_VFO_A),RIG_OK);
  QCOMPARE(rig_set_cache_timeout_ms(keyed.rig,HAMLIB_CACHE_ALL,0),RIG_OK);
  keyed.state.tx_frequency(14075010);keyed.transceiver->set(keyed.state,5);
  QVERIFY2(keyed.failure.isEmpty(),qPrintable(keyed.failure));
  freq_t frequency=0;
  QCOMPARE(keyed.original.get_freq(keyed.rig,RIG_VFO_A,&frequency),RIG_OK);
  QCOMPARE(frequency,freq_t(14075010));
  QCOMPARE(keyed.original.get_freq(keyed.rig,RIG_VFO_B,&frequency),RIG_OK);
  QCOMPARE(frequency,freq_t(14074000));
}

void RegressionTest::hamlibDataProfilesRemainSelected()
{
  for(auto mode:{RIG_MODE_USBD1,RIG_MODE_USBD2,RIG_MODE_USBD3,RIG_MODE_LSBD2}) {
    RadioAudioFixture::RigFixture fixture{RIG_VFO_B};
    QVERIFY(fixture.registered);fixture.start();fixture.establish_split();
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure));
    QCOMPARE(rig_set_mode(fixture.rig,fixture.rx_vfo,mode,1800),RIG_OK);
    QCOMPARE(rig_set_mode(fixture.rig,fixture.tx_vfo,mode,1800),RIG_OK);
    fixture.mode_targets.clear();fixture.state.frequency(fixture.state.frequency()+10);
    fixture.state.mode(mode==RIG_MODE_LSBD2?Transceiver::DIG_L:Transceiver::DIG_U);
    fixture.state.ptt(true);fixture.transceiver->set(fixture.state,4);
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure));QCOMPARE(fixture.ptt_on_count,1);
    QCOMPARE(fixture.mode_at_ptt,rmode_t(mode));
    pbwidth_t width;rmode_t actual;
    QCOMPARE(fixture.original.get_mode(fixture.rig,fixture.tx_vfo,&actual,&width),RIG_OK);
    QCOMPARE(width,pbwidth_t(1800));
  }
}

void RegressionTest::audioPlaybackGenerationIsolation()
{
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  auto oldMode=w->m_mode,oldTxMode=w->m_modeTx;auto oldMonitorOff=w->m_monitoroff;auto oldTci=w->m_tci;
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&]{
    w->on_stopTxButton_clicked();w->m_mode=oldMode;w->m_modeTx=oldTxMode;w->m_monitoroff=oldMonitorOff;w->m_tci=oldTci;
  }};
  w->m_monitoroff=true;w->m_tci=false;w->m_tune=false;
  TransceiverCommandSpy commands(w.get());QVERIFY(commands.isValid());
  QSignalSpy stopAudio(w.get(),&MainWindow::endTransmitMessage);
  int dialogs=0;QTimer dismiss;
  connect(&dismiss,&QTimer::timeout,this,[&]{
    for(auto widget:QApplication::topLevelWidgets())
      if(auto box=qobject_cast<QMessageBox*>(widget))if(box->isVisible()){++dialogs;box->accept();}
  });
  dismiss.start(10);
  quint64 generation=w->m_signoffGeneration+100;
  for(QString mode:{QString("JTTY"),QString("Q65"),QString("FT8")}) {
    w->m_mode=w->m_modeTx=mode;
    w->m_audioPlaybackGeneration=++generation;
    w->m_jttyPcmConsumed=false;w->m_upstreamPcmConsumed=false;
    w->m_transmitting=true;w->m_enableTx=true;w->m_manualTxAuthorized=true;g_iptt=1;
    auto stopped=stopAudio.size();auto previousDialogs=dialogs;
    w->m_soundOutput->playbackError(generation-1,"obsolete playback failure");
    w->m_modulator->waveformConsumed(generation-1);
    w->m_soundOutput->playbackDrained(generation-1);
    QVERIFY(w->m_transmitting);QVERIFY(w->m_manualTxAuthorized);QCOMPARE(g_iptt,qint32(1));
    QVERIFY(!w->m_jttyPcmConsumed && !w->m_upstreamPcmConsumed);
    QCOMPARE(stopAudio.size(),stopped);QCOMPARE(dialogs,previousDialogs);
    if(mode!="FT8") {
      w->m_modulator->waveformConsumed(generation);
      QVERIFY(mode=="JTTY"?w->m_jttyPcmConsumed:w->m_upstreamPcmConsumed);
      w->m_soundOutput->playbackDrained(generation-1);QVERIFY(w->m_transmitting);
      w->m_soundOutput->playbackDrained(generation);
      QVERIFY(!w->m_transmitting);QCOMPARE(g_iptt,qint32(0));QVERIFY(w->m_manualTxAuthorized);
      // A later request has its own lifetime even when it reuses this mode.
      w->m_audioPlaybackGeneration=++generation;w->m_transmitting=true;g_iptt=1;
    }
    w->m_soundOutput->playbackError(generation,"synthetic current playback failure");
    QVERIFY(!w->m_transmitting);QVERIFY(!w->m_enableTx);QVERIFY(!w->m_manualTxAuthorized);
    QCOMPARE(g_iptt,qint32(0));QCOMPARE(w->m_audioPlaybackGeneration,quint64(0));
    QVERIFY(!w->ptt0Timer.isActive() && !w->ptt1Timer.isActive());
    QVERIFY(stopAudio.size()>stopped);QCOMPARE(dialogs,previousDialogs+1);
  }
  QCOMPARE(commands.pttOnCount(),0);
}
