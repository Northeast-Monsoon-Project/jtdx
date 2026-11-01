#include <QtTest>
#include "EmulateSplitTransceiver.hpp"

namespace {
// This transport records commands only; it has no CAT, audio or RF device.
class FakeTransceiver : public Transceiver {
public:
  FakeTransceiver() : Transceiver(nullptr) {}
  void set(TransceiverState const& value, unsigned sequence) noexcept override {
    state = value;lastSequence = sequence;++setCalls;
  }
  void start(unsigned sequence, JTDXDateTime* clock) noexcept override {
    lastSequence = sequence;lastClock = clock;++startCalls;
  }
  void stop() noexcept override {++stopCalls;}
  void set_tx_pcm(QByteArray const& pcm, quint64 generation) noexcept override {
    sentPcm.append(pcm);sentGenerations.append(generation);
  }
  TransceiverState state;
  unsigned lastSequence = 0;
  JTDXDateTime* lastClock = nullptr;
  int setCalls = 0, startCalls = 0, stopCalls = 0;
  QList<QByteArray> sentPcm;
  QList<quint64> sentGenerations;
};
}

class EmulateSplitSignalTests : public QObject {
  Q_OBJECT
private slots:
  void initTestCase()
  {
    qRegisterMetaType<Transceiver::TransceiverState>();
  }

  void waveformAndPcmEventsPreservePayload()
  {
    auto transport = new FakeTransceiver;
    EmulateSplitTransceiver wrapper{std::unique_ptr<Transceiver>{transport}};
    QSignalSpy started(&wrapper, &Transceiver::waveformStarted);
    QSignalSpy consumed(&wrapper, &Transceiver::waveformConsumed);
    QSignalSpy pcm(&wrapper, &Transceiver::timedPcmWritten);
    QSignalSpy finished(&wrapper, &Transceiver::txPcmFinished);
    quint64 const generation = (quint64(1) << 40) + 17;
    qint64 const endUtcMs = 1793448000123LL;
    QByteArray const samples = QByteArray::fromHex("000080ff007fff7f");
    transport->waveformStarted(generation);
    transport->timedPcmWritten(samples, endUtcMs);
    transport->waveformConsumed(generation);
    transport->txPcmFinished();
    QCOMPARE(started.count(), 1);QCOMPARE(consumed.count(), 1);
    QCOMPARE(started.at(0).at(0).toULongLong(), generation);
    QCOMPARE(consumed.at(0).at(0).toULongLong(), generation);
    QCOMPARE(pcm.count(), 1);QCOMPARE(pcm.at(0).at(0).toByteArray(), samples);
    QCOMPARE(pcm.at(0).at(1).toLongLong(), endUtcMs);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(transport->setCalls, 0);QVERIFY(!transport->state.ptt());
  }

  void queuedEventsKeepOriginalGeneration()
  {
    auto transport = new FakeTransceiver;
    EmulateSplitTransceiver wrapper{std::unique_ptr<Transceiver>{transport}};
    QObject receiver;
    QList<quint64> started, consumed;
    connect(&wrapper, &Transceiver::waveformStarted, &receiver,
      [&](quint64 generation) {started.append(generation);}, Qt::QueuedConnection);
    connect(&wrapper, &Transceiver::waveformConsumed, &receiver,
      [&](quint64 generation) {consumed.append(generation);}, Qt::QueuedConnection);
    transport->waveformStarted(71);
    transport->waveformConsumed(71);
    wrapper.stop();
    auto next = transport->state;next.waveform_generation(72);
    wrapper.set(next, 4);
    transport->waveformStarted(72);
    transport->waveformConsumed(72);
    QVERIFY(started.isEmpty());QVERIFY(consumed.isEmpty());
    QTRY_COMPARE(started.size(), 2);QTRY_COMPARE(consumed.size(), 2);
    QCOMPARE(started, (QList<quint64>{71, 72}));
    QCOMPARE(consumed, (QList<quint64>{71, 72}));
    QCOMPARE(transport->stopCalls, 1);
  }

  void pcmCommandsForwardThroughBaseSlotWithoutPtt()
  {
    auto transport = new FakeTransceiver;
    EmulateSplitTransceiver wrapper{std::unique_ptr<Transceiver>{transport}};
    QSignalSpy failures(&wrapper, &Transceiver::failure);
    Transceiver& interface = wrapper;
    QByteArray samples = QByteArray::fromHex("00000100ff7f0080");
    QByteArray const original = samples;
    quint64 const generation = (quint64(1) << 48) + 23;
    interface.set_tx_pcm(samples, generation);
    QCOMPARE(transport->sentPcm.size(), 1);
    QCOMPARE(transport->sentPcm.at(0), original);
    QCOMPARE(transport->sentGenerations.at(0), generation);
    QVERIFY(QMetaObject::invokeMethod(&wrapper, "set_tx_pcm", Qt::QueuedConnection,
      Q_ARG(QByteArray, samples), Q_ARG(quint64, generation)));
    samples.fill('x'); // Queued arguments must retain their own PCM bytes.
    QTRY_COMPARE(transport->sentPcm.size(), 2);
    QCOMPARE(transport->sentPcm.at(1), original);
    QCOMPARE(transport->sentGenerations.at(1), generation);
    interface.set_tx_pcm(QByteArray()); // Preserve the default generation too.
    QCOMPARE(transport->sentPcm.size(), 3);
    QVERIFY(transport->sentPcm.at(2).isEmpty());
    QCOMPARE(transport->sentGenerations.at(2), quint64(0));
    QCOMPARE(failures.count(), 0);
    QCOMPARE(transport->setCalls, 0);QVERIFY(!transport->state.ptt());
  }

  void splitFrequencyMappingIsUnchanged()
  {
    auto transport = new FakeTransceiver;
    EmulateSplitTransceiver wrapper{std::unique_ptr<Transceiver>{transport}};
    QSignalSpy updates(&wrapper, &Transceiver::update);
    QSignalSpy failures(&wrapper, &Transceiver::failure);
    Transceiver::TransceiverState request;
    request.online(true);request.frequency(14074000);request.tx_frequency(14075500);
    request.split(true);request.mode(Transceiver::DIG_U);request.waveform_generation(91);
    for (bool transmitting : {false, true, false}) {
      request.ptt(transmitting);
      wrapper.set(request, 123);
      QCOMPARE(transport->state.frequency(), quint64(transmitting ? 14075500 : 14074000));
      QCOMPARE(transport->state.tx_frequency(), quint64(0));
      QVERIFY(!transport->state.split());
      QCOMPARE(transport->state.ptt(), transmitting);
      QCOMPARE(transport->state.mode(), Transceiver::DIG_U);
      QCOMPARE(transport->state.waveform_generation(), quint64(91));
      QCOMPARE(transport->lastSequence, 123u);
      transport->update(transport->state, 123);
      QCOMPARE(updates.size(), 1);
      auto const reported = qvariant_cast<Transceiver::TransceiverState>(updates.at(0).at(0));
      QCOMPARE(reported.frequency(), quint64(14074000));
      QCOMPARE(reported.tx_frequency(), quint64(14075500));
      QVERIFY(reported.split());QCOMPARE(reported.ptt(), transmitting);
      QCOMPARE(reported.waveform_generation(), quint64(91));
      QCOMPARE(updates.at(0).at(1).toUInt(), 123u);
      updates.clear();
    }
    // A genuine RX VFO adjustment must still be visible to the application.
    auto changed = transport->state;changed.frequency(14076000);
    transport->update(changed, 123);
    QCOMPARE(qvariant_cast<Transceiver::TransceiverState>(updates.at(0).at(0)).frequency(), quint64(14076000));
    updates.clear();
    // Native rig split remains incompatible with the Fake It decorator.
    changed.split(true);transport->update(changed, 123);
    QCOMPARE(failures.count(), 1);QVERIFY(updates.isEmpty());
  }

  void existingTransportSignalsAndLifecycleStillForward()
  {
    auto transport = new FakeTransceiver;
    EmulateSplitTransceiver wrapper{std::unique_ptr<Transceiver>{transport}};
    QSignalSpy resolution(&wrapper, &Transceiver::resolution);
    QSignalSpy frames(&wrapper, &Transceiver::tciframeswritten);
    QSignalSpy active(&wrapper, &Transceiver::tci_mod_active);
    QSignalSpy finished(&wrapper, &Transceiver::finished);
    QSignalSpy failure(&wrapper, &Transceiver::failure);
    wrapper.start(8, nullptr);wrapper.stop();
    QCOMPARE(transport->startCalls, 1);QCOMPARE(transport->stopCalls, 1);
    QCOMPARE(transport->lastSequence, 8u);QVERIFY(!transport->lastClock);
    transport->resolution(-1);transport->tciframeswritten(3456);
    transport->tci_mod_active(true);transport->failure("simulated error");transport->finished();
    QCOMPARE(resolution.count(), 1);QCOMPARE(resolution.at(0).at(0).toInt(), -1);
    QCOMPARE(frames.count(), 1);QCOMPARE(frames.at(0).at(0).toLongLong(), qint64(3456));
    QCOMPARE(active.count(), 1);QVERIFY(active.at(0).at(0).toBool());
    QCOMPARE(failure.count(), 1);QCOMPARE(failure.at(0).at(0).toString(), QString("simulated error"));
    QCOMPARE(finished.count(), 1);
  }
};

QTEST_GUILESS_MAIN(EmulateSplitSignalTests)
#include "EmulateSplitSignalTests.moc"
