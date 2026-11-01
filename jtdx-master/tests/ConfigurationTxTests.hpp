#include <QMetaMethod>
#include "StationList.hpp"

void RegressionTest::configurationTxFrequencyDeduplicates_data()
{
  QTest::addColumn<bool>("emulated");
  QTest::addColumn<bool>("calibrated");
  QTest::newRow("native") << false << false;
  QTest::newRow("fake-it") << true << false;
  QTest::newRow("native-calibrated-transverter") << false << true;
  QTest::newRow("fake-it-calibrated-transverter") << true << true;
}

void RegressionTest::configurationTxFrequencyDeduplicates()
{
  QFETCH(bool,emulated);
  QFETCH(bool,calibrated);
  // Hamlib factories have process-wide resources. Close the previous fixture
  // first; this test is deliberately placed after the other MainWindow tests.
  if(w) {w->close();w.reset();}
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  QSettings stored(directory.path()+"/cat.ini",QSettings::IniFormat);
  auto split = emulated ? TransceiverFactory::split_mode_emulate : TransceiverFactory::split_mode_rig;
  Radio::Frequency const frequency = 144174000;
  Radio::FrequencyDelta offset = calibrated ? -116000000 : 0;
  double const intercept = calibrated ? 25. : 0.;
  double const slope = calibrated ? 1.25 : 0.;
  stored.setValue("Configuration/MyCall","N0CALL");
  stored.setValue("Configuration/MyGrid","AA00");
  stored.setValue("Configuration/Rig","Hamlib Dummy No VFO");
  stored.setValue("Configuration/SplitMode",QVariant::fromValue(split));
  stored.setValue("Configuration/DataMode",QVariant::fromValue(Configuration::data_mode_USB));
  stored.setValue("Configuration/CalibrationIntercept",intercept);
  stored.setValue("Configuration/CalibrationSlopePPM",slope);
  stored.setValue("Configuration/stations",QVariant::fromValue(StationList::Stations{{"2m",offset,"test"}}));
  QWidget owner;
  JTDXDateTime clock;
  Configuration config(&stored,&owner);
  config.set_jtdxtime(&clock);
  // Do not open even the dummy backend: inspect the real command signal at
  // the queue boundary, without any device, PTT or audio operations.
  QVERIFY(!config.is_transceiver_online());
  QVERIFY(config.split_mode());
  auto dialog=owner.findChild<QDialog*>("configuration_dialog");
  QVERIFY(dialog);
  QMetaMethod command;
  for(int i=0;i<dialog->metaObject()->methodCount();++i) {
    auto method=dialog->metaObject()->method(i);
    if(method.methodType()==QMetaMethod::Signal && method.name()=="set_transceiver") {
      command=method;
      break;
    }
  }
  QVERIFY(command.isValid());
  QSignalSpy commands(dialog,command);
  QVERIFY(commands.isValid());
  auto lastState=[&] {return qvariant_cast<Transceiver::TransceiverState>(commands.last().at(0));};
  auto desired=[&](Radio::Frequency f) {
    return Radio::Frequency(qRound64(intercept+(1.+slope/1.e6)*(qint64(f)+offset)));
  };

  config.transceiver_tx_frequency(frequency);
  QCOMPARE(commands.size(),1);
  QVERIFY(lastState().split());
  QCOMPARE(lastState().tx_frequency(),desired(frequency));
  QCOMPARE(lastState().mode(),Transceiver::USB);
  QVERIFY(!lastState().ptt());
  auto firstSequence=commands.first().at(1).toUInt();
  for(int i=0;i<20;++i) config.transceiver_tx_frequency(frequency);
  QCOMPARE(commands.size(),1);

  config.transceiver_tx_frequency(frequency+1000);
  QCOMPARE(commands.size(),2);
  QCOMPARE(lastState().tx_frequency(),desired(frequency+1000));
  QCOMPARE(commands.last().at(1).toUInt(),firstSequence+1);
  auto previousPhysicalFrequency=lastState().tx_frequency();
  // An offset change must be retained even if the resulting CAT frequency
  // stays equal: that offset also maps subsequent feedback to the RF band.
  offset+=1000;
  config.stations()->station_list({{"2m",offset,"changed"}});
  config.transceiver_tx_frequency(frequency);
  QCOMPARE(commands.size(),3);
  QCOMPARE(lastState().tx_frequency(),previousPhysicalFrequency);
  config.transceiver_tx_frequency(frequency);
  QCOMPARE(commands.size(),3);

  config.transceiver_mode(Transceiver::DIG_U);
  QCOMPARE(commands.size(),4);
  QCOMPARE(lastState().mode(),Transceiver::DIG_U);
  config.transceiver_tx_frequency(frequency);
  QCOMPARE(commands.size(),5);
  QCOMPARE(lastState().mode(),Transceiver::USB);
  QCOMPARE(lastState().tx_frequency(),desired(frequency));
  config.transceiver_tx_frequency(frequency);
  QCOMPARE(commands.size(),5);

  config.transceiver_tx_frequency(0);
  QCOMPARE(commands.size(),6);
  QVERIFY(!lastState().split());
  QCOMPARE(lastState().tx_frequency(),Radio::Frequency(0));
  config.transceiver_tx_frequency(0);
  QCOMPARE(commands.size(),6);
  config.transceiver_tx_frequency(frequency);
  QCOMPARE(commands.size(),7);
  QVERIFY(lastState().split());
  QCOMPARE(lastState().tx_frequency(),desired(frequency));
  QCOMPARE(commands.last().at(1).toUInt(),firstSequence+6);
  for(auto const& emission:commands)
    QVERIFY(!qvariant_cast<Transceiver::TransceiverState>(emission.at(0)).ptt());
  QVERIFY(!config.is_transceiver_online());
}
