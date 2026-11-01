// Simulated CAT/PTT and source callbacks: no physical transmitter or audio is
// driven. Exercise the real startTx2/guiUpdate/DecodeFinished accounting path.
void RegressionTest::managedCqSpamCannotBypassBudget_data()
{
  QTest::addColumn<int>("mode");QTest::addColumn<int>("limit");
  QTest::addColumn<bool>("lateConsumed");QTest::addColumn<bool>("responded");
  for(auto mode:{ManagedMode::DxccHunter,ManagedMode::QsoEnthusiast})
    for(int limit:{0,2,3})for(bool late:{false,true})for(bool response:{false,true})
      QTest::newRow(qPrintable(QString("mode%1-limit%2-late%3-response%4").arg(int(mode)).arg(limit).arg(late).arg(response)))
        <<int(mode)<<limit<<late<<response;
}

void RegressionTest::managedCqSpamCannotBypassBudget()
{
  QFETCH(int,mode);QFETCH(int,limit);QFETCH(bool,lateConsumed);QFETCH(bool,responded);
  w->on_stopTxButton_clicked();stopWaveAudio();
  // Each data row models a fresh process. Mode changes intentionally retain
  // failed-call cooldowns, so reusing K8LIM must not inherit a prior row's ban.
  w->setManagedMode(ManagedMode::Off);w->m_managed=ManagedSession{};
  auto previous=w->m_config.managed_settings();ManagedSettings selected=previous;
  selected.hunter.callLimit=selected.enthusiast.callLimit=limit;
  selected.hunter.criteria=NewGrid;selected.enthusiast.criteria=NewCall;
  saveManagedSettings(selected);auto offset=w->m_jtdxtime->GetOffset();
  // Keep generation requests visible to the test, but disconnect the sink.
  QObject::disconnect(w.get(),&MainWindow::sendMessage,w->m_modulator,&Modulator::start);
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&]{
    w->m_decoderBusy=false;w->m_monitoring=false;w->m_rigState.ptt(false);
    w->on_stopTxButton_clicked();w->m_jtdxtime->SetOffset(offset);
    QObject::connect(w.get(),&MainWindow::sendMessage,w->m_modulator,&Modulator::start);
    w->setManagedMode(ManagedMode::Off);saveManagedSettings(previous);w->clearDX("managed retry cleanup");
  }};
  QTRY_VERIFY_WITH_TIMEOUT(w->m_logReady && !w->m_logLoading && !w->m_logRefreshPending
    && !w->m_logImportActive && !w->m_logRefreshTimer.isActive(),20000);
  prepareManaged(ManagedMode(mode));QVERIFY(w->m_enableTx);
  w->ui->tabWidget->setCurrentIndex(0);w->m_tci=false;w->m_monitoroff=true;w->m_lockTxFreq=false;
  auto now=w->m_jtdxtime->currentDateTimeUtc2(),start=now;start.setTime(QTime(9,0,0));
  qint64 base=start.toMSecsSinceEpoch();
  auto clock=[&](qint64 stamp){w->m_jtdxtime->SetOffset(w->m_jtdxtime->GetOffset()
    +(stamp-w->m_jtdxtime->currentMSecsSinceEpoch2())/1000.0);};
  auto decode=[&](qint64 slot,QString message){
    auto stamp=QDateTime::fromMSecsSinceEpoch(slot,Qt::UTC).toString("hhmmss");
    w->processDecodedOutput((stamp+" -10  0.1 1700 ~ "+message+"\n").toUtf8());
  };
  auto finishReceive=[&](qint64 slot){
    w->m_msDecStarted=slot;w->m_monitoring=true;w->m_lostaudio=false;w->m_dataAvailable=true;
    w->m_diskData=false;w->m_manualDecode=false;w->processDecodedOutput("<DecodeFinished>    1\n");
  };
  clock(base-1000);w->m_decoderBusy=true;
  decode(base-15000,"CQ K8LIM FN31");finishReceive(base-15000);
  QCOMPARE(w->m_hisCall,QString("K8LIM"));QCOMPARE(w->m_ntx,1);
  if(responded) {
    decode(base-15000,"N0CALL K8LIM -12");w->process_Auto();QCOMPARE(w->m_ntx,3);
  }
  QSignalSpy starts(w.get(),&MainWindow::sendMessage);QVERIFY(starts.isValid());
  TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  int rounds=limit?limit:3;
  for(int round=0;round<rounds;++round) {
    qint64 slot=base+round*30000;
    clock(slot+250);w->m_decoderBusy=false;w->m_monitoring=false;
    w->m_rigState.ptt(false);w->m_tx_when_ready=false;w->m_btxok=true;w->m_haltTrans=false;
    // A fake already-keyed rig allows guiUpdate to generate/display the real
    // message without emitting a key-up request to any transceiver.
    g_iptt=1;w->m_transmitting=true;w->guiUpdate();
    w->startTx2();QCOMPARE(starts.size(),round+1);
    auto generation=w->m_managedTxGeneration;
    QCOMPARE(w->m_managed.transmissions(),round);
    w->m_modulator->waveformStarted(generation);
    w->m_modulator->waveformConsumed(generation-1); // Stale source event cannot count.
    if(!lateConsumed)w->m_modulator->waveformConsumed(generation);
    QCOMPARE(w->m_managed.transmissions(),round);
    clock(slot+14000);w->guiUpdate(); // Natural end of the existing FT8 wave.
    QCOMPARE(g_iptt,0);w->ptt0Timer.stop();w->stopTx2();w->m_rigState.ptt(false);
    if(lateConsumed)w->m_modulator->waveformConsumed(generation);
    QCOMPARE(w->m_managed.transmissions(),round+1);
    w->m_modulator->waveformConsumed(generation);QCOMPARE(w->m_managed.transmissions(),round+1);
    QVERIFY(!w->m_managed.exhausted(limit)); // The next receive opportunity is still owed.
    w->m_decoderBusy=true;w->m_monitoring=true;
    decode(slot+15000,responded?"N0CALL K8LIM -12":"CQ K8LIM FN31");
    QCOMPARE(w->m_managed.current(),QString("K8LIM"));
    QCOMPARE(w->m_managed.transmissions(),round+1); // Fresh CQ/repeated report cannot reset it.
    if(round+1<rounds)finishReceive(slot+15000);
  }
  if(limit) {
    QVERIFY(!w->managedTxAllowed());
    // This is the previous starvation bug: every next TX window arrived
    // while decoding was busy, so exhausted() was skipped and N+1 got keyed.
    clock(base+rounds*30000+250);w->guiUpdate();QCOMPARE(g_iptt,0);
    QCOMPARE(w->m_hisCall,QString("K8LIM"));QVERIFY(w->m_enableTx);
    finishReceive(base+(rounds-1)*30000+15000);
    QVERIFY(w->m_hisCall.isEmpty());QVERIFY(w->m_managed.current().isEmpty());
    QVERIFY(w->m_managed.blocked("K8LIM",w->m_managedClock.elapsed()));
  } else {
    QVERIFY(w->managedTxAllowed());QVERIFY(!w->m_managed.exhausted(0));
    QCOMPARE(w->m_hisCall,QString("K8LIM"));
  }
  QVERIFY(w->m_enableTx);QVERIFY(w->m_manualTxAuthorized);
  QCOMPARE(ptt.pttOnCount(),0);
  // A cancelled prepared/started source cannot spend another complete round.
  auto old=w->m_managedTxGeneration;w->on_stopTxButton_clicked();
  w->m_modulator->waveformStarted(old);w->m_modulator->waveformConsumed(old);
  QCOMPARE(w->m_managed.transmissions(),0);QVERIFY(!w->m_enableTx);
}

void RegressionTest::managedFinalBudgetWaitsForReceive_data()
{
  QTest::addColumn<int>("mode");QTest::addColumn<QString>("ending");
  for(auto mode:{ManagedMode::DxccHunter,ManagedMode::QsoEnthusiast})for(auto ending:{QString("RR73"),QString("73")})
    QTest::newRow(qPrintable(QString::number(int(mode))+ending))<<int(mode)<<ending;
}

void RegressionTest::managedFinalBudgetWaitsForReceive()
{
  QFETCH(int,mode);QFETCH(QString,ending);
  w->on_stopTxButton_clicked();stopWaveAudio();auto previous=w->m_config.managed_settings();
  auto selected=previous;selected.hunter.callLimit=selected.enthusiast.callLimit=1;saveManagedSettings(selected);
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&]{
    w->m_decoderBusy=false;w->m_monitoring=false;w->m_rigState.ptt(false);
    w->on_stopTxButton_clicked();w->drainSignoffLogUpdates();w->setManagedMode(ManagedMode::Off);
    saveManagedSettings(previous);w->clearDX("managed final retry cleanup");
  }};
  QTRY_VERIFY_WITH_TIMEOUT(w->m_logReady && !w->m_logLoading && !w->m_logRefreshPending
    && !w->m_logImportActive && !w->m_logRefreshTimer.isActive(),20000);
  prepareManaged(ManagedMode(mode));QVERIFY(w->m_enableTx);
  w->m_tci=false;w->m_monitoroff=true;w->ui->dxCallEntry->setText("K8FIN");w->ui->dxGridEntry->setText("FN31");
  w->m_managed.begin("K8FIN");w->m_managedTxReady=true;w->genStdMsgs("-12");w->m_rrr=false;
  if(ending=="73")w->on_txb5_clicked();else w->on_txb4_clicked();
  w->m_currentMessage="K8FIN N0CALL "+ending;w->prepareSignoff(w->m_currentMessage);w->prepareManagedTransmission();
  const qint64 period=15000,slot=w->m_jtdxtime->currentMSecsSinceEpoch2()/period*period-4*period;
  w->m_managedTxSlot=slot;w->m_signoff.prepare("K8FIN",w->generalContext(),ending=="73"?SignoffState::SeventyThree:SignoffState::RR73,slot,period);
  w->m_qsoHistory.message("K8FIN",ending=="73"?QsoHistory::S73:QsoHistory::SRR73,0,"","","NA","K",0,"-12",1500,"FT8");
  w->m_transmitting=true;g_iptt=1;w->m_modulator->waveformStarted(w->m_managedTxGeneration);
  w->m_modulator->waveformConsumed(w->m_managedTxGeneration);w->m_managedTxNatural=true;w->managedTransmissionFinished();
  w->m_signoff.naturalEnd();w->m_transmitting=false;g_iptt=0;w->m_tx_when_ready=false;w->m_rigState.ptt(false);w->signoffPttReleased();
  QCOMPARE(w->m_signoff.transmissions(),1);QVERIFY(!w->managedTxAllowed());
  w->process_Auto();QCOMPARE(w->m_hisCall,QString("K8FIN")); // No early drop before the RX opportunity.
  if(ending=="73")w->m_signoff.receive("K8FIN",w->generalContext(),"RR73",slot+period);
  w->m_monitoring=true;w->m_decoderBusy=true;w->m_lostaudio=false;w->m_dataAvailable=true;
  w->m_msDecStarted=slot+period;QByteArray pcm(6000,0);
  for(qint64 end=slot+period+250;end<=slot+2*period;end+=250)w->m_detector->timedPcmWritten(pcm,end);
  w->processDecodedOutput("<DecodeFinished>    0\n");
  QVERIFY(w->m_hisCall.isEmpty());QVERIFY(!w->m_signoff.active());
  QVERIFY(w->m_managed.blocked("K8FIN",w->m_managedClock.elapsed()));QVERIFY(w->m_enableTx);
}

void RegressionTest::managedPeriodicCqResumesAfterCallers_data()
{
  QTest::addColumn<int>("mode");
  QTest::newRow("hunter")<<int(ManagedMode::DxccHunter);
  QTest::newRow("enthusiast")<<int(ManagedMode::QsoEnthusiast);
}

void RegressionTest::managedPeriodicCqResumesAfterCallers()
{
  QFETCH(int,mode);w->on_stopTxButton_clicked();stopWaveAudio();
  auto previous=w->m_config.managed_settings();auto selected=previous;
  selected.hunter.callLimit=selected.enthusiast.callLimit=1;
  selected.hunter.criteria=selected.enthusiast.criteria=0;saveManagedSettings(selected);
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&]{
    w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);saveManagedSettings(previous);
    w->clearDX("managed CQ continuation cleanup");
  }};
  QTRY_VERIFY_WITH_TIMEOUT(w->m_logReady && !w->m_logLoading && !w->m_logRefreshPending
    && !w->m_logImportActive && !w->m_logRefreshTimer.isActive(),20000);
  prepareManaged(ManagedMode(mode));QVERIFY(w->m_enableTx);
  w->m_managed.setMode(ManagedMode(mode),w->m_managedClock.elapsed()-900000);
  w->process_Auto();QCOMPARE(w->m_ntx,6);QCOMPARE(w->m_managed.cqRemaining(),3);
  managedSentRound("CQ N0CALL AA00");QCOMPARE(w->m_managed.cqRemaining(),2);
  auto incoming=[&](QString call){
    w->processDecodedOutput(("000045 -10  0.1 1700 ~ N0CALL "+call+" FN31\n").toUtf8());
  };
  auto confirmed=[&]{
    auto call=w->m_hisCall;w->prepareSignoff(call+" N0CALL RR73");
    // This scheduler fixture suppresses logging, whose full source callback
    // path is exercised separately. Confirmation itself uses SignoffState.
    w->m_signoffLogPromptRequested=true;
    w->m_signoff.started();w->m_signoff.consumed();w->m_signoff.naturalEnd();w->m_signoff.released();
    w->m_signoff.receive(call,w->generalContext(),"73",w->m_signoff.txSlot()+15000);
    QVERIFY(w->finishSignoff());
  };
  incoming("K8ONE");w->process_Auto();QCOMPARE(w->m_hisCall,QString("K8ONE"));
  incoming("K8TWO");confirmed();w->process_Auto();
  QCOMPARE(w->m_hisCall,QString("K8TWO"));QCOMPARE(w->m_managed.cqRemaining(),2);
  confirmed();w->process_Auto();QCOMPARE(w->m_ntx,6);QVERIFY(w->managedTxAllowed());
  for(int remaining:{1,0}) {
    managedSentRound("CQ N0CALL AA00");QCOMPARE(w->m_managed.cqRemaining(),remaining);w->process_Auto();
  }
  QVERIFY(!w->managedTxAllowed());QVERIFY(w->m_enableTx);
  QVERIFY(!w->m_managed.cqDue(w->m_managedClock.elapsed()+899000));
  QVERIFY(w->m_managed.cqDue(w->m_managedClock.elapsed()+900000));QCOMPARE(w->m_managed.cqRemaining(),3);
}
