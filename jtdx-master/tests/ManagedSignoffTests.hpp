// Definitions for RegressionTest slots; all RF and timestamps are simulated.
void RegressionTest::managedSignoffKeepsPartner_data()
{
  QTest::addColumn<QString>("ending");
  QTest::newRow("RR73")<<QString("RR73");QTest::newRow("73")<<QString("73");
}

void RegressionTest::managedSignoffKeepsPartner()
{
  QFETCH(QString,ending);
  w->on_stopTxButton_clicked();stopWaveAudio();
  // These are the first GUI cases: the initial asynchronous operator-log load
  // must complete before a physical Enable TX activation can be authorized.
  QTRY_VERIFY_WITH_TIMEOUT(w->m_logReady && !w->m_logLoading && !w->m_logImportActive
    && !w->m_logRefreshPending && !w->m_logRefreshTimer.isActive(),20000);
  prepareManaged(ManagedMode::QsoEnthusiast);
  QVERIFY2(w->m_enableTx,qPrintable(w->statusBar()->currentMessage()));
  w->ui->dxCallEntry->setText("K7END");w->m_managed.begin("K7END");w->m_managedTxReady=true;
  w->m_qsoHistory.message("K7END",ending=="73"?QsoHistory::S73:QsoHistory::SRR73,0,"","","NA","K",30,"-10",1500,"FT8");
  w->genStdMsgs("-10");if(ending=="73")w->on_txb5_clicked();else w->on_txb4_clicked();
  w->prepareSignoff("K7END N0CALL "+ending);QVERIFY(w->m_signoff.active());
  TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  w->m_transmitting=true;g_iptt=1;w->m_signoff.started();w->m_signoff.consumed();
  w->applyGeneralPostQso("K7END");w->process_Auto();w->generalTick();
  QCOMPARE(w->m_hisCall,QString("K7END"));QCOMPARE(w->m_managed.current(),QString("K7END"));
  QVERIFY(w->m_enableTx);QVERIFY(!w->logQSOTimer.isActive());QVERIFY(w->m_signoff.active());
  w->m_transmitting=false;g_iptt=0;w->m_tx_when_ready=false;w->m_rigState.ptt(false);
  w->m_signoff.naturalEnd();w->signoffPttReleased();w->process_Auto();w->generalTick();
  // Sending either final text alone is not protocol completion or permission
  // to clear the partner, switch slot, log early or start a different QSO.
  QCOMPARE(w->m_hisCall,QString("K7END"));QVERIFY(w->m_signoff.active());
  QCOMPARE(w->m_managed.current(),QString("K7END"));QVERIFY(!w->logQSOTimer.isActive());
  QVERIFY(w->managedTxAllowed()==(ending=="RR73"));
  QCOMPARE(ptt.pttOnCount(),0);
  w->m_generalPostQsoCall.clear();w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);w->clearDX("test cleanup");
}

void RegressionTest::managedRr73WaitsForWaveAndPtt()
{
  w->on_stopTxButton_clicked();stopWaveAudio();
  // These are the first GUI cases: the initial asynchronous operator-log load
  // must complete before a physical Enable TX activation can be authorized.
  QTRY_VERIFY_WITH_TIMEOUT(w->m_logReady && !w->m_logLoading && !w->m_logImportActive
    && !w->m_logRefreshPending && !w->m_logRefreshTimer.isActive(),20000);
  prepareManaged(ManagedMode::QsoEnthusiast);
  QVERIFY2(w->m_enableTx,qPrintable(w->statusBar()->currentMessage()));
  w->ui->dxCallEntry->setText("K7ACK");w->m_managed.begin("K7ACK");w->m_managedTxReady=true;
  w->m_qsoHistory.message("K7ACK",QsoHistory::SRR73,0,"","","NA","K",30,"-10",1500,"FT8");
  w->prepareSignoff("K7ACK N0CALL RR73");w->m_signoff.started();
  auto slot=w->m_signoff.txSlot();w->m_signoff.receive("K7ACK",w->generalContext(),"73",slot);
  w->m_transmitting=true;g_iptt=1;w->m_rigState.ptt(true);
  QVERIFY(!w->finishSignoff());QVERIFY(w->m_signoff.allowsTransmit());
  w->m_signoff.consumed();w->m_signoff.naturalEnd();
  w->m_transmitting=false;g_iptt=0;w->m_tx_when_ready=false;
  w->signoffPttReleased();QVERIFY(w->m_signoff.active());QCOMPARE(w->m_hisCall,QString("K7ACK"));
  // Avoid writing a log in this scheduler test; a separate fixture covers logs.
  w->m_lastloggedcall="K7ACK";w->m_lastloggedtime=w->m_jtdxtime->currentDateTimeUtc2();
  w->m_rigState.ptt(false);w->signoffPttReleased();
  QVERIFY(!w->m_signoff.active());QVERIFY(w->m_hisCall.isEmpty());QVERIFY(w->m_managed.current().isEmpty());
  QVERIFY(w->m_enableTx);QVERIFY(w->m_manualTxAuthorized);
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);
}

void RegressionTest::managed73RequiresCompleteLiveReceive()
{
  w->on_stopTxButton_clicked();stopWaveAudio();
  // These are the first GUI cases: the initial asynchronous operator-log load
  // must complete before a physical Enable TX activation can be authorized.
  QTRY_VERIFY_WITH_TIMEOUT(w->m_logReady && !w->m_logLoading && !w->m_logImportActive
    && !w->m_logRefreshPending && !w->m_logRefreshTimer.isActive(),20000);
  prepareManaged(ManagedMode::QsoEnthusiast);
  QVERIFY2(w->m_enableTx,qPrintable(w->statusBar()->currentMessage()));
  w->ui->dxCallEntry->setText("K7QUI");w->m_managed.begin("K7QUI");w->m_managedTxReady=true;
  w->m_qsoHistory.message("K7QUI",QsoHistory::S73,0,"","","NA","K",30,"-10",1500,"FT8");
  w->prepareSignoff("K7QUI N0CALL 73");
  const qint64 period=15000,slot=w->m_jtdxtime->currentMSecsSinceEpoch2()/period*period-4*period;
  w->m_signoff.prepare("K7QUI",w->generalContext(),SignoffState::SeventyThree,slot,period);
  w->m_signoff.started();w->m_signoff.consumed();w->m_signoff.naturalEnd();w->m_signoff.released();
  w->m_rigState.ptt(false);w->m_tx_when_ready=false;w->m_transmitting=false;g_iptt=0;w->m_decoderBusy=false;
  w->m_manualDecode=true;w->signoffReceiveCycleFinished(slot+period);
  QVERIFY(w->m_signoffDecodedCycles.isEmpty());w->m_manualDecode=false;
  w->m_diskData=true;w->signoffReceiveCycleFinished(slot+period);
  QVERIFY(w->m_signoffDecodedCycles.isEmpty());w->m_diskData=false;
  w->signoffReceiveCycleFinished(slot+period);QVERIFY(!w->finishSignoff());
  QCOMPARE(w->m_hisCall,QString("K7QUI"));QVERIFY(!w->managedTxAllowed()); // No live audio yet.
  w->m_lastloggedcall="K7QUI";w->m_lastloggedtime=w->m_jtdxtime->currentDateTimeUtc2();
  w->m_monitoring=true; // Synthetic capture only; the actual device remains stopped.
  QByteArray pcm(6000,0);
  for(qint64 end=slot+period+250;end<slot+2*period;end+=250)
    w->m_detector->timedPcmWritten(pcm,end);
  QVERIFY(w->m_signoff.active());QCOMPARE(w->m_hisCall,QString("K7QUI"));
  w->m_detector->timedPcmWritten(pcm,slot+2*period);
  QVERIFY(!w->m_signoff.active());QVERIFY(w->m_hisCall.isEmpty());QVERIFY(w->m_enableTx);
  w->m_monitoring=false;w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);
}

void RegressionTest::managed73LiveCaptureHandoff_data()
{
  QTest::addColumn<int>("mode");QTest::addColumn<int>("blockMs");
  QTest::addColumn<int>("phaseMs");QTest::addColumn<int>("guard");
  for(auto mode:{ManagedMode::DxccHunter,ManagedMode::QsoEnthusiast}) {
    auto prefix=mode==ManagedMode::DxccHunter?"hunter":"enthusiast";
    for(int block:{100,200,400})
      QTest::newRow(qPrintable(QString("%1-live-%2ms").arg(prefix).arg(block)))<<int(mode)<<block<<0<<0;
    for(int phase:{40,79})
      QTest::newRow(qPrintable(QString("%1-live-80ms-phase%2").arg(prefix).arg(phase)))<<int(mode)<<80<<phase<<0;
    QTest::newRow(qPrintable(QString("%1-audio-gap").arg(prefix)))<<int(mode)<<100<<0<<1;
    QTest::newRow(qPrintable(QString("%1-repeated-RR73").arg(prefix)))<<int(mode)<<200<<0<<2;
  }
}

void RegressionTest::managed73LiveCaptureHandoff()
{
  QFETCH(int,mode);QFETCH(int,blockMs);QFETCH(int,phaseMs);QFETCH(int,guard);
  w->on_stopTxButton_clicked();stopWaveAudio();
  auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  const bool oldAuto=w->m_config.autolog(),oldPrompt=w->m_config.prompt_to_log();
  const auto oldManaged=w->m_config.managed_settings();
  auto configure=[&](bool automatic,bool prompt,ManagedSettings const& managed){
    QTimer::singleShot(0,dialog,[=]{
      dialog->findChild<QCheckBox*>("autolog_check_box")->setChecked(automatic);
      dialog->findChild<QCheckBox*>("prompt_to_log_check_box")->setChecked(prompt);
      dialog->findChild<ManagedSettingsWidget*>("managed_tab")->setSettings(managed);
      QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });QCOMPARE(w->m_config.exec(),int(QDialog::Accepted));
  };
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&]{
    w->m_monitoring=false;w->m_decoderBusy=false;w->m_lostaudio=false;
    w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);
    w->clearDX("live signoff test cleanup");configure(oldAuto,oldPrompt,oldManaged);
  }};
  configure(false,false,ManagedSettings{});
  QTRY_VERIFY_WITH_TIMEOUT(w->m_logReady && !w->m_logLoading && !w->m_logImportActive
    && !w->m_logRefreshPending && !w->m_logRefreshTimer.isActive(),20000);
  // Native audio shutdown may deliver a device error (e.g. no Wine capture
  // device). Drain it before the physical Enable TX gesture; the test then
  // supplies PCM through Detector without bypassing the safety interlock.
  w->m_managed=ManagedSession{};prepareManaged(ManagedMode(mode),false);
  stopWaveAudio();
  manualEnable();
  QVERIFY(w->m_enableTx && w->m_manualTxAuthorized);
  w->m_tci=false;w->m_monitoring=true;w->m_bTxTime=false;
  w->ui->tabWidget->setCurrentIndex(0);
  w->ui->dxCallEntry->setText("K7QUI");w->ui->dxGridEntry->setText("FN20");
  w->m_managed.begin("K7QUI");w->m_managedTxReady=true;
  w->genStdMsgs("-10");w->on_txb5_clicked();w->prepareSignoff("K7QUI N0CALL 73");
  const qint64 period=15000,slot=w->m_signoff.txSlot(),rx=slot+period;
  w->m_signoff.started();w->m_signoff.consumed();w->m_signoff.naturalEnd();w->m_signoff.released();
  w->m_transmitting=false;g_iptt=0;w->m_tx_when_ready=false;w->m_rigState.ptt(false);
  w->m_qsoHistory.message("K7QUI",QsoHistory::S73,0,"","","NA","K",30,"-10",1500,"FT8");
  TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  JTDXDateTime captureClock;
  Detector detector(12000,15,&captureClock,4);detector.setBlockSize(3456);
  QVERIFY(detector.initialize(QIODevice::WriteOnly,AudioDevice::Mono));
  int chunks=0;
  connect(&detector,&Detector::timedPcmWritten,this,[&](QByteArray const& pcm,qint64 end){
    ++chunks;w->m_detector->timedPcmWritten(pcm,end);
  });
  auto clock=[&](qint64 value){
    managedFixtureTime(QDateTime::fromMSecsSinceEpoch(value,Qt::UTC).time());
    captureClock.SetOffset(w->m_jtdxtime->GetOffset());
  };
  const auto stamp=QDateTime::fromMSecsSinceEpoch(rx,Qt::UTC).toString("hhmmss");
  auto row=[&](QString const& message){return (stamp+" -10  0.1 1700 ~ "+message+"\n").toUtf8();};
  bool decoded=false;
  // Resume the real 48 kHz capture path shortly before the opposite RX slot.
  // Detector downsamples and resets its FFT accumulation at UTC boundaries;
  // synthetic ideal 250 ms timedPcmWritten calls would bypass this behavior.
  QByteArray input(blockMs*48*2,0);
  for(qint64 end=rx-1200+blockMs+phaseMs;end<=rx+period+1600;end+=blockMs) {
    clock(end);
    if(guard!=1 || end<rx+5000 || end>rx+6400)
      QCOMPARE(detector.write(input),qint64(input.size()));
    if(!decoded && end>=rx+14500) {
      // This flag describes the PREVIOUS partial receive period after TX,
      // not the continuously captured slot being decoded now.
      w->m_lostaudio=true;w->m_dataAvailable=true;w->m_msDecStarted=rx+14000;
      w->m_decoderBusy=true;w->m_processAuto_done=true;
      if(guard==2)w->processDecodedOutput(row("N0CALL K7QUI RR73"));
      w->processDecodedOutput(row("N0CALL K8NXT FN31"));
      w->processDecodedOutput("<DecodeFinished>    2\n");decoded=true;
    }
  }
  QVERIFY(decoded);QVERIFY(chunks>50);
  w->generalTick();
  if(guard==0) {
    QVERIFY2(!w->m_signoff.active(),"A complete current RX slot must confirm 73 despite the previous period's lost-audio label");
    QVERIFY(w->m_hisCall.isEmpty());QVERIFY(w->ui->dxGridEntry->text().isEmpty());
    QVERIFY(w->m_managed.current().isEmpty());
    // The ordinary managed TX-window entry then selects the waiting caller.
    // Do not invoke guiUpdate here: this fixture must never assert real PTT.
    w->process_Auto();
    QCOMPARE(w->m_hisCall,QString("K8NXT"));QCOMPARE(w->m_managed.current(),w->m_hisCall);
    QVERIFY(w->m_managedTxReady);QVERIFY(w->selectedTransmitText().startsWith("K8NXT "));
  } else {
    QVERIFY(w->m_signoff.active());QCOMPARE(w->m_hisCall,QString("K7QUI"));
    QCOMPARE(w->m_managed.current(),QString("K7QUI"));
    QVERIFY(!w->m_signoff.complete());
    QCOMPARE(w->m_signoff.allowsTransmit(),guard==2);
  }
  QVERIFY(w->m_enableTx && w->m_manualTxAuthorized);
  QCOMPARE(g_iptt,0);QCOMPARE(ptt.pttOnCount(),0);
  detector.close();
}

void RegressionTest::signoffRejectsStaleWaveformCompletion()
{
  w->on_stopTxButton_clicked();stopWaveAudio();w->setManagedMode(ManagedMode::Off);
  w->m_mode=w->m_modeTx="FT8";w->m_TRperiod=15;w->m_houndMode=false;
  w->ui->dxCallEntry->setText("K7OLD");w->prepareSignoff("K7OLD N0CALL RR73");
  auto previous=w->m_signoffGeneration;w->m_signoff.started();
  // Emission is deliberately queued before Stop, then delivered after a new
  // attempt for the same callsign. Matching callsign/slot alone is insufficient.
  QMetaObject::invokeMethod(w.get(),[this,previous]{
    w->m_modulator->waveformStarted(previous);w->m_modulator->waveformConsumed(previous);
  },Qt::QueuedConnection);
  w->on_stopTxButton_clicked();w->ui->dxCallEntry->setText("K7OLD");
  w->prepareSignoff("K7OLD N0CALL RR73");QVERIFY(w->m_signoffGeneration!=previous);
  auto current=w->m_signoffGeneration;w->m_signoff.started();
  w->m_signoff.naturalEnd();w->m_signoff.released();
  QCoreApplication::sendPostedEvents(w.get(),QEvent::MetaCall);
  QVERIFY(w->m_signoff.active());QCOMPARE(w->m_signoff.transmissions(),0);
  QVERIFY(!w->m_signoffRoundStarted);QVERIFY(w->m_signoffLogOriginal.isEmpty());
  // TCI uses the same filter and must reject its delayed old notification too.
  w->m_config.transceiver_waveform_started(previous);
  w->m_config.transceiver_waveform_consumed(previous);QCOMPARE(w->m_signoff.transmissions(),0);
  QVERIFY(!w->m_signoffRoundStarted);QVERIFY(w->m_signoffLogOriginal.isEmpty());
  w->m_modulator->waveformConsumed(current);QCOMPARE(w->m_signoff.transmissions(),1);
  QVERIFY(!w->m_signoff.complete()); // No partner 73 has been received.
  w->on_stopTxButton_clicked();w->clearDX("test cleanup");
}

void RegressionTest::signoffLogsLatestRoundWithoutAdvancing()
{
  w->on_stopTxButton_clicked();stopWaveAudio();
  QTRY_VERIFY_WITH_TIMEOUT(w->m_logReady && !w->m_logLoading && !w->m_logImportActive
    && !w->m_logRefreshPending && !w->m_logRefreshTimer.isActive(),20000);
  prepareManaged(ManagedMode::QsoEnthusiast);QVERIFY(w->m_enableTx);QVERIFY(w->m_config.autolog());
  const auto offset=w->m_jtdxtime->GetOffset();
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&]{
    w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);w->clearDX("test cleanup");
    w->m_jtdxtime->SetOffset(offset);w->drainSignoffLogUpdates();
  }};
  QString call="K7UPD",error;auto full=OperatorLog::compatibilityFile(w->m_config.my_callsign());
  auto before=QFile::exists(full)?OperatorLog::read(full,&error).size():0;QVERIFY2(error.isEmpty(),qPrintable(error));
  w->ui->dxCallEntry->setText(call);w->ui->dxGridEntry->setText("FN20");
  w->m_managed.begin(call);w->m_managedTxReady=true;w->m_rrr=false;
  w->m_qsoHistory.message(call,QsoHistory::SRR73,0,"","","NA","K",30,"-10",1500,"FT8");
  w->genStdMsgs("-10");w->on_txb4_clicked();w->m_currentMessage=call+" N0CALL RR73";
  w->m_dateTimeQSOOn=w->m_jtdxtime->currentDateTimeUtc2().addSecs(-60);
  OperatorLog::Record initial,last;
  for(int round=0;round<2;++round) {
    // More than two FT8 periods: the former lastloggedcall time heuristic
    // would create a duplicate here. Only the software's test clock changes.
    w->m_jtdxtime->SetOffset(offset+round*60);
    w->m_rptSent=round?"-08":"-10";w->m_rptRcvd="-12";
    w->prepareSignoff(w->m_currentMessage);
    w->m_transmitting=true;g_iptt=1;w->m_tx_when_ready=false;w->m_rigState.ptt(true);
    auto generation=w->m_signoffGeneration;
    w->m_modulator->waveformStarted(generation-1);
    if(!round)QCOMPARE(OperatorLog::read(full,&error).size(),before);
    w->m_modulator->waveformStarted(generation);
    QVERIFY(w->m_signoffRoundStarted);QVERIFY(!w->m_signoffLogOriginal.isEmpty());
    QCOMPARE(w->m_hisCall,call);QCOMPARE(w->m_managed.current(),call);QVERIFY(w->m_enableTx);
    w->logChanged();QVERIFY(w->m_logRefreshPending);QVERIFY(w->m_signoffLogRefreshDeferred);
    QVERIFY(w->managedTxAllowed()); // An in-flight log update cannot cancel this burst.
    w->m_modulator->waveformConsumed(generation);w->m_signoff.naturalEnd();
    w->m_transmitting=false;g_iptt=0;w->m_rigState.ptt(false);w->signoffPttReleased();
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_signoffLogWriting && w->m_signoffLogUpdates.isEmpty(),10000);
    auto rows=OperatorLog::read(full,&error);QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(rows.size(),before+1);
    for(auto const& record:rows)if(record.value("CALL")==call)last=record;
    QVERIFY(!last.isEmpty());if(!round)initial=last;
    QCOMPARE(OperatorLog::identity(last),OperatorLog::identity(initial));
    auto quarter=OperatorLog::quarterFile(w->m_config.my_callsign(),QDate::fromString(last.value("QSO_DATE"),"yyyyMMdd"));
    auto quarterly=OperatorLog::read(quarter,&error);QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(quarterly.contains(last));
    QCOMPARE(w->m_hisCall,call);QVERIFY(w->m_signoff.active());
  }
  QCOMPARE(last.value("TIME_ON"),initial.value("TIME_ON"));QCOMPARE(last.value("RST_SENT"),QString("-08"));
  auto firstOff=QDateTime::fromString(initial.value("QSO_DATE_OFF")+initial.value("TIME_OFF"),"yyyyMMddhhmmss");
  auto lastOff=QDateTime::fromString(last.value("QSO_DATE_OFF")+last.value("TIME_OFF"),"yyyyMMddhhmmss");
  QVERIFY(firstOff.secsTo(lastOff)>=60);
  w->m_signoff.receive(call,w->generalContext(),"73",w->m_signoff.txSlot()+15000);
  QVERIFY(w->finishSignoff());QVERIFY(w->m_hisCall.isEmpty());QVERIFY(w->m_managed.current().isEmpty());
  QVERIFY(w->m_enableTx);QVERIFY(w->m_manualTxAuthorized);
  QTRY_VERIFY_WITH_TIMEOUT(!w->m_signoffLogWriting && w->m_signoffLogUpdates.isEmpty(),10000);
  QCOMPARE(OperatorLog::read(full,&error).size(),before+1);
}

void RegressionTest::signoffPromptRetainsSnapshotAcrossRounds()
{
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  bool originalAuto=w->m_config.autolog(),originalPrompt=w->m_config.prompt_to_log();
  auto configure=[&](bool automatic,bool prompt){
    QTimer::singleShot(0,dialog,[=]{
      dialog->findChild<QCheckBox*>("autolog_check_box")->setChecked(automatic);
      dialog->findChild<QCheckBox*>("prompt_to_log_check_box")->setChecked(prompt);
      QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });
    QCOMPARE(w->m_config.exec(),int(QDialog::Accepted));
  };
  auto offset=w->m_jtdxtime->GetOffset();
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&]{
    w->on_stopTxButton_clicked();if(w->m_logDlg->isVisible())w->m_logDlg->reject();
    w->m_jtdxtime->SetOffset(offset);configure(originalAuto,originalPrompt);w->clearDX("test cleanup");
  }};
  configure(false,true);
  QTRY_VERIFY_WITH_TIMEOUT(w->m_logReady && !w->m_logLoading && !w->m_logRefreshPending
    && !w->m_logImportActive && !w->m_logRefreshTimer.isActive(),20000);
  w->m_mode=w->m_modeTx="FT8";w->m_TRperiod=15;w->m_houndMode=false;w->m_autoseq=false;
  w->m_singleshot=false;w->ui->dxCallEntry->setText("K7PMP");manualEnable();QVERIFY(w->m_enableTx);
  w->prepareSignoff("K7PMP N0CALL RR73");w->m_transmitting=true;g_iptt=1;
  w->m_modulator->waveformStarted(w->m_signoffGeneration);QVERIFY(w->m_logDlg->isVisible());
  auto start=w->m_logDlg->findChild<QDateTimeEdit*>("start_date_time");
  auto end=w->m_logDlg->findChild<QDateTimeEdit*>("end_date_time");
  auto comments=w->m_logDlg->findChild<QLineEdit*>("comments");QVERIFY(start);QVERIFY(end);QVERIFY(comments);
  auto initialStart=start->dateTime(),initialEnd=end->dateTime();comments->setText("operator note");
  w->m_jtdxtime->SetOffset(offset+60);w->prepareSignoff("K7PMP N0CALL RR73");
  w->m_modulator->waveformStarted(w->m_signoffGeneration);
  QCOMPARE(start->dateTime(),initialStart);QVERIFY(initialEnd.secsTo(end->dateTime())>=60);
  QCOMPARE(comments->text(),QString("operator note"));QCOMPARE(w->m_hisCall,QString("K7PMP"));
  QVERIFY(w->m_signoffLogOriginal.isEmpty()); // No acceptance yet, so no saved identity.
  w->m_transmitting=false;g_iptt=0;w->on_stopTxButton_clicked();
  w->ui->dxCallEntry->setText("K7NEW");w->m_logDlg->accept();
  QCOMPARE(w->m_lastloggedcall,QString("K7PMP"));QCOMPARE(w->m_hisCall,QString("K7NEW"));
}
