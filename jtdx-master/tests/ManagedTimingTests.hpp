// Exercise actual GUI/decode sequencing with an isolated dummy transceiver.
// The modulator sink and unsolicited CAT updates are disconnected throughout.
void RegressionTest::managedTransmitStartTiming_data()
{
  QTest::addColumn<int>("mode");QTest::addColumn<QString>("protocol");QTest::addColumn<bool>("late");
  for(auto mode:{ManagedMode::DxccHunter,ManagedMode::QsoEnthusiast})
    for(auto protocol:{QString("FT8"),QString("FT4")})for(bool late:{false,true})
      QTest::newRow(qPrintable(QString("mode%1-%2-late%3").arg(int(mode)).arg(protocol).arg(late)))
        <<int(mode)<<protocol<<late;
}

void RegressionTest::managedTransmitStartTiming()
{
  QFETCH(int,mode);QFETCH(QString,protocol);QFETCH(bool,late);
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  QCOMPARE(w->m_config.rig_name(),QString("None")); // Never key a physical test rig.
  auto profiles=w->m_config.managed_settings();auto selected=profiles;
  selected.hunter.criteria=NewGrid;selected.enthusiast.criteria=NewCall;
  selected.hunter.callLimit=selected.enthusiast.callLimit=3;
  saveManagedSettings(selected);auto offset=w->m_jtdxtime->GetOffset();
  QObject::disconnect(w.get(),&MainWindow::sendMessage,w->m_modulator,&Modulator::start);
  QObject::disconnect(&w->m_config,&Configuration::transceiver_update,w.get(),&MainWindow::handle_transceiver_update);
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&]{
    w->m_decoderBusy=false;w->m_monitoring=false;w->m_rigState.ptt(false);
    w->on_stopTxButton_clicked();w->ptt0Timer.stop();w->ptt1Timer.stop();w->killFileTimer.stop();
    w->m_jtdxtime->SetOffset(offset);w->setManagedMode(ManagedMode::Off);
    QObject::connect(w.get(),&MainWindow::sendMessage,w->m_modulator,&Modulator::start);
    QObject::connect(&w->m_config,&Configuration::transceiver_update,w.get(),&MainWindow::handle_transceiver_update);
    saveManagedSettings(profiles);w->on_actionFT8_triggered();stopWaveAudio();
    w->clearDX("managed timing cleanup");w->m_singleshot=false;
  }};
  QTRY_VERIFY_WITH_TIMEOUT(w->m_logReady && !w->m_logLoading && !w->m_logRefreshPending
    && !w->m_logImportActive && !w->m_logRefreshTimer.isActive(),20000);
  w->m_managed=ManagedSession{};prepareManaged(ManagedMode(mode));
  w->m_mode=w->m_modeTx=protocol;w->m_TRperiod=protocol=="FT8"?15.0:7.5;
  w->setManagedMode(ManagedMode(mode));manualEnable();QVERIFY(w->m_enableTx && w->m_manualTxAuthorized);
  w->ui->tabWidget->setCurrentIndex(0);w->m_tci=false;w->m_monitoroff=true;w->m_lockTxFreq=false;
  w->m_rigState.ptt(false);w->m_rigState.frequency(w->m_freqNominal);
  w->m_haltTrans=false;w->m_restart=false;w->m_btxok=false;w->m_tx_when_ready=false;
  const qint64 period=qRound64(w->m_TRperiod*1000);
  auto day=w->m_jtdxtime->currentDateTimeUtc2();day.setTime(QTime(9,0,0));
  const qint64 base=day.toMSecsSinceEpoch();
  auto clock=[&](qint64 target){
    auto now=w->m_jtdxtime->currentMSecsSinceEpoch2();
    w->m_jtdxtime->SetOffset(w->m_jtdxtime->GetOffset()+(target-now)/1000.0);
  };
  auto decode=[&]{
    QString stamp=QDateTime::fromMSecsSinceEpoch(base-period,Qt::UTC).toString("hhmmss");
    QString marker=protocol=="FT8"?"~":"+";
    w->processDecodedOutput((stamp+" -10  0.1 1700 "+marker+" CQ K8TIM FN31\n").toUtf8());
  };
  auto finish=[&]{
    w->m_msDecStarted=base-period;w->m_dataAvailable=true;w->m_lostaudio=false;
    w->processDecodedOutput("<DecodeFinished>    1\n");
  };
  auto release=[&]{w->ptt0Timer.stop();w->ptt1Timer.stop();w->stopTx2();w->m_rigState.ptt(false);};
  TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  QSignalSpy audio(w.get(),&MainWindow::sendMessage);QVERIFY(audio.isValid());
  auto keys=[&]{return ptt.pttOnCount();};
  clock(base-250);w->guiUpdate(); // Reset the real GUI's previous-PTT edge state.
  w->m_decoderBusy=true;
  // Due periodic CQ must not preempt a decoder that has not reported any
  // usable station yet. A real candidate need not wait for DecodeFinished.
  w->m_managed.setMode(ManagedMode(mode),w->m_managedClock.elapsed()-900000);
  qint64 start=late?(protocol=="FT8"?6500:3000):250;
  clock(base+start);w->guiUpdate();QCOMPARE(keys(),0);QCOMPARE(g_iptt,0);
  decode();QVERIFY(w->m_decoderBusy);
  auto previousGeneration=w->m_signoffGeneration;
  w->guiUpdate();QCOMPARE(w->m_hisCall,QString("K8TIM"));QCOMPARE(g_iptt,1);QCOMPARE(keys(),1);
  QCOMPARE(w->m_managed.transmissions(),0);QVERIFY(w->m_decoderBusy);
  QCOMPARE(w->m_txAudioRequestedMs,qint64(-1));QVERIFY(!w->m_txStartReported);
  w->m_modulator->waveformStarted(previousGeneration);
  QVERIFY(!w->m_txStartReported); // Old audio cannot acknowledge a newly keyed PTT before startTx2.
  // Mid-slot transmission remains available. Reducing decision latency must
  // not introduce a first-second cutoff or interrupt an active waveform.
  w->startTx2();QCOMPARE(audio.size(),1);
  QVERIFY(w->m_txAudioRequestedMs>=0);
  QCOMPARE(w->m_txStartAudioGeneration,w->m_managedTxGeneration);
  w->m_modulator->waveformStarted(previousGeneration);QVERIFY(!w->m_txStartReported);
  w->m_modulator->waveformStarted(w->m_managedTxGeneration);
  QVERIFY(w->m_txStartReported);
  clock(base+start+500);w->guiUpdate();
  QCOMPARE(g_iptt,1);QVERIFY(w->m_transmitting);QCOMPARE(audio.size(),1);
  QCOMPARE(w->m_managed.transmissions(),0); // Not a completed wave yet.
  finish();QCOMPARE(w->m_hisCall,QString("K8TIM"));QCOMPARE(g_iptt,1);
  w->on_stopTxButton_clicked();release();int priorKeys=keys();
  QVERIFY(!w->m_txStartClock.isValid());
  // Exercise an unreported callback arriving after cancellation as well.
  w->m_txStartReported=false;w->m_modulator->waveformStarted(w->m_managedTxGeneration);
  QVERIFY(!w->m_txStartReported);
  clock(base+8*period+250);finish();w->guiUpdate();
  QCOMPARE(g_iptt,0);QCOMPARE(keys(),priorKeys);QCOMPARE(audio.size(),1);
  QVERIFY(!w->m_enableTx && !w->m_manualTxAuthorized);
}
