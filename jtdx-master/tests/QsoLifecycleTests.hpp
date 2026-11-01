// Definitions for the RegressionTest slots; include after the class declaration.
void RegressionTest::upstreamSingleQsoPersistsBeforeClearing()
{
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  QVERIFY(w->m_config.autolog());
  TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  for(auto mode:{QString("Q65"),QString("MSK144")}) {
    w->selectUpstreamMode(mode);w->m_upstreamPeriod->setCurrentText("15");stopWaveAudio();
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_logLoading && !w->m_logRefreshPending,20000);
    w->m_houndMode=false;w->m_autoseq=false;w->m_autoTx=false;w->m_singleshot=true;
    w->m_diskData=false;w->m_manualDecode=false;w->m_lockTxFreq=false;
    w->m_qsoHistory.init();w->m_qsoHistory.owndata("NA","K","AA00",false);
    QString call=mode=="Q65"?"K9QSL":"K9MSK";
    auto full=OperatorLog::compatibilityFile(w->m_config.my_callsign());
    QString error;QList<OperatorLog::Record> before;
    if(QFile::exists(full))before=OperatorLog::read(full,&error);
    QVERIFY2(error.isEmpty(),qPrintable(error));
    w->ui->dxCallEntry->setText(call);w->ui->dxGridEntry->setText("FN20");
    w->m_dateTimeQSOOn=w->m_jtdxtime->currentDateTimeUtc2().addSecs(-10);
    w->m_rptSent="-10";w->m_rptRcvd="-12";
    manualEnable();QVERIFY(w->m_enableTx);
    w->m_currentMessage=call+" "+w->m_config.my_callsign()+" 73";
    // Record an actually sent message without opening the audio device or PTT.
    w->ui->decodedTextBrowser2->displayTransmittedText(w->m_currentMessage,w->m_baseCall,call,"",mode,
      w->ui->TxFreqSpinBox->value(),w->m_config.color_TxMsg(),w->m_qsoHistory,false);
    w->prepareSignoff(w->m_currentMessage);
    const qint64 period=15000,slot=w->m_jtdxtime->currentMSecsSinceEpoch2()/period*period-4*period;
    w->m_signoff.prepare(call,w->generalContext(),SignoffState::SeventyThree,slot,period);
    w->m_transmitting=true;g_iptt=1;w->m_lastloggedcall.clear();
    w->m_rigState.ptt(false);w->m_decoderBusy=false;
    w->m_modulator->waveformStarted(w->m_signoffGeneration);
    // First real audio may log, but neither logging nor waveform completion
    // may clear the editors until the following quiet RX cycle confirms it.
    QCOMPARE(OperatorLog::read(full,&error).size(),before.size()+1);
    QCOMPARE(w->m_hisCall,call);QVERIFY(w->m_enableTx);
    w->finishUpstreamTransmit();
    QCOMPARE(OperatorLog::read(full,&error).size(),before.size()+1);
    QCOMPARE(w->m_hisCall,call);QVERIFY(w->m_enableTx);QVERIFY(w->m_signoff.active());
    w->m_signoffAudioCycles.insert(slot+period);
    w->signoffReceiveCycleFinished(slot+period);
    // The confirmation preserves the existing record and only then clears.
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_signoffLogWriting && w->m_signoffLogUpdates.isEmpty(),10000);
    auto after=OperatorLog::read(full,&error);QVERIFY2(error.isEmpty(),qPrintable(error));
    QCOMPARE(after.size(),before.size()+1);
    auto saved=after.last();QCOMPARE(saved.value("CALL"),call);
    QCOMPARE(saved.value("RST_SENT"),QString("-10"));QCOMPARE(saved.value("RST_RCVD"),QString("-12"));
    QCOMPARE(saved.value(mode=="Q65"?"SUBMODE":"MODE"),mode);
    auto quarter=OperatorLog::quarterFile(w->m_config.my_callsign(),QDate::fromString(saved.value("QSO_DATE"),"yyyyMMdd"));
    auto rows=OperatorLog::read(quarter,&error);QVERIFY2(error.isEmpty(),qPrintable(error));
    QVERIFY(rows.contains(saved));QVERIFY(w->m_hisCall.isEmpty());QVERIFY(w->m_hisGrid.isEmpty());
    QVERIFY(!w->logQSOTimer.isActive());QVERIFY(!w->m_enableTx);QCOMPARE(g_iptt,0);
    QCOMPARE(w->m_lastloggedcall,call);
  }
  QCOMPARE(ptt.pttOnCount(),0);
  w->m_singleshot=false;w->on_actionFT8_triggered();stopWaveAudio();
}

void RegressionTest::delayedLogConfirmationKeepsNewSelection()
{
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  auto initialFrequency=w->m_freqNominal;
  for(int change=0;change<4;++change) {
    w->m_mode=w->m_modeTx="FT8";w->m_TRperiod=15;w->m_freqNominal=14074000;
    w->m_houndMode=false;w->m_autoseq=false;w->m_autoTx=false;w->m_singleshot=false;w->m_lockTxFreq=false;
    w->m_generalPostQsoCall.clear();w->m_qsoHistory.init();
    QString original="K8L"+QString::number(change)+"A";
    w->ui->dxCallEntry->setText(original);w->ui->dxGridEntry->setText("FN20");
    w->ui->TxFreqSpinBox->setValue(1250);w->ui->RxFreqSpinBox->setValue(1850);
    w->m_dateTimeQSOOn=w->m_jtdxtime->currentDateTimeUtc2().addSecs(-20);
    w->m_logqso73=false;w->on_logQSOButton_clicked();QVERIFY(w->m_logDlg->isVisible());
    auto capturedRevision=w->m_generalLogSelectionRevision;
    QString selected=original;
    if(change==0)w->m_freqNominal=7074000;
    else if(change==1)w->m_mode=w->m_modeTx="FT4";
    else if(change==2) {
      w->ui->dxCallEntry->setText("K8OTHER");w->ui->dxCallEntry->setText(original);
      QVERIFY(w->m_generalSelectionRevision>capturedRevision);
    } else {
      w->m_houndMode=true;selected="K8FOX";w->ui->dxCallEntry->setText(selected);
    }
    w->ui->dxGridEntry->setText("FN31");
    w->m_logDlg->accept();w->generalTick();
    QCOMPARE(w->m_lastloggedcall,original);QVERIFY(!w->m_logDlg->isVisible());
    QCOMPARE(w->m_hisCall,selected);QCOMPARE(w->m_hisGrid,QString("FN31"));
    QCOMPARE(w->ui->RxFreqSpinBox->value(),1850);QCOMPARE(w->ui->TxFreqSpinBox->value(),1250);
    QVERIFY(w->m_generalPostQsoCall.isEmpty());QVERIFY(!w->m_enableTx);
    w->m_houndMode=false;w->clearDX("test cleanup");
  }
  w->m_freqNominal=initialFrequency;w->m_mode=w->m_modeTx="FT8";w->m_TRperiod=15;
}

void RegressionTest::managedLoggedQsoAdvancesWithoutRepeating()
{
  QTRY_VERIFY_WITH_TIMEOUT(!w->m_logLoading && !w->m_logRefreshPending && !w->m_logImportActive,20000);
  saveManagedSettings(ManagedSettings{});
  // The preceding log-dialog test writes ADIF and schedules a delayed reload.
  // Do not try to select a station while that intentional safety gate is busy.
  QTRY_VERIFY_WITH_TIMEOUT(!w->m_logRefreshTimer.isActive() && !w->m_logLoading
    && !w->m_logRefreshPending && !w->m_logImportActive && w->m_logReady,20000);
  prepareManaged(ManagedMode::QsoEnthusiast);QVERIFY(w->m_enableTx);
  managedDecode("N0CALL K8MGA FN20","000015",-5);
  managedDecode("N0CALL K8MGB FN20","000015",-20);
  w->process_Auto();QCOMPARE(w->m_hisCall,QString("K8MGA"));
  for(auto call:{QString("K8MGA"),QString("K8MGB")}) {
    w->m_qsoHistory.message(call,QsoHistory::S73,0,"","","NA","K",30,"-10",1500,"FT8");
    w->m_qsoHistory.complete(call);
    w->applyGeneralPostQso(call);
    QVERIFY(w->m_hisCall.isEmpty());QVERIFY(w->m_managed.current().isEmpty());QVERIFY(w->m_enableTx);
    QVERIFY(!w->m_managedTxReady);
    w->process_Auto();
    if(call=="K8MGA")QCOMPARE(w->m_hisCall,QString("K8MGB"));
    else {QVERIFY(w->m_hisCall.isEmpty());QVERIFY(!w->managedTxAllowed());}
  }
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);w->m_singleshot=false;
}

void RegressionTest::independentPostQsoCleanupOptions()
{
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);
  w->m_autoseq=false;w->m_autoTx=false;w->m_singleshot=false;w->m_houndMode=false;w->m_lockTxFreq=false;
  auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  auto configure=[&](bool call,bool grid,bool rx){
    QTimer::singleShot(0,dialog,[=]{
      dialog->findChild<QCheckBox*>("cbClearDXcall")->setChecked(call);
      dialog->findChild<QCheckBox*>("cbClearDXgrid")->setChecked(grid);
      dialog->findChild<QCheckBox*>("cbRxToTxAfterQSO")->setChecked(rx);
      QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });
    QCOMPARE(w->m_config.exec(),int(QDialog::Accepted));
  };
  bool previousCall=w->m_config.clear_DXcall(),previousGrid=w->m_config.clear_DXgrid(),previousRx=w->m_config.set_RXtoTX();
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&]{configure(previousCall,previousGrid,previousRx);w->clearDX("test cleanup");}};
  for(int flags=0;flags<4;++flags) {
    bool clearCall=flags&1,clearGrid=flags&2;configure(clearCall,clearGrid,false);
    w->ui->dxCallEntry->setText("K8OPT");w->ui->dxGridEntry->setText("FN20");
    w->ui->TxFreqSpinBox->setValue(1250);w->ui->RxFreqSpinBox->setValue(1850);
    w->applyGeneralPostQso("K8OPT");
    QCOMPARE(w->m_hisCall,clearCall?QString():QString("K8OPT"));
    QCOMPARE(w->m_hisGrid,clearGrid?QString():QString("FN20"));
    QCOMPARE(w->ui->RxFreqSpinBox->value(),1850);QVERIFY(!w->m_enableTx);
  }
}
