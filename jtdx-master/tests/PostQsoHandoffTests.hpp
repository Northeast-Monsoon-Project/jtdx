// Drive decoded-output and capture callbacks in both orders. Calling
// process_Auto explicitly after completion would hide the missing scheduler.
void RegressionTest::completedQsoAnswersWaitingCaller_data()
{
  QTest::addColumn<int>("callMode");QTest::addColumn<bool>("rr73");
  QTest::addColumn<bool>("lateAudio");QTest::addColumn<bool>("report");
  QTest::addColumn<int>("guard");
  for(int mode=1;mode<=3;++mode)for(bool rr73:{false,true})
    for(bool late:{false,true})for(bool report:{false,true})
      QTest::newRow(qPrintable(QString("mode%1-%2-audio%3-%4").arg(mode)
        .arg(rr73?"RR73":"73").arg(late?"late":"early").arg(report?"report":"call")))
        <<mode<<rr73<<late<<report<<0;
  for(int guard=1;guard<=7;++guard)
    QTest::newRow(qPrintable(QString("handoff-guard-%1").arg(guard)))<<3<<false<<true<<false<<guard;
  QTest::newRow("first-log-saved-at-confirmation")<<3<<false<<true<<true<<8;
}

void RegressionTest::completedQsoAnswersWaitingCaller()
{
  QFETCH(int,callMode);QFETCH(bool,rr73);QFETCH(bool,lateAudio);QFETCH(bool,report);
  QFETCH(int,guard);
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  bool oldAuto=w->m_config.autolog(),oldPrompt=w->m_config.prompt_to_log();
  auto originalFrequency=w->m_freqNominal;
  auto configure=[&](bool automatic,bool prompt){
    QTimer::singleShot(0,dialog,[=]{
      dialog->findChild<QCheckBox*>("autolog_check_box")->setChecked(automatic);
      dialog->findChild<QCheckBox*>("prompt_to_log_check_box")->setChecked(prompt);
      QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });QCOMPARE(w->m_config.exec(),int(QDialog::Accepted));
  };
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&]{
    w->m_monitoring=false;w->m_decoderBusy=false;w->m_logLoading=false;w->m_freqNominal=originalFrequency;
    w->on_stopTxButton_clicked();w->m_singleshot=false;
    w->clearDX("handoff test cleanup");w->m_autoseq=false;configure(oldAuto,oldPrompt);
  }};
  configure(guard==8,false);
  QTRY_VERIFY_WITH_TIMEOUT(w->m_logReady && !w->m_logLoading && !w->m_logImportActive
    && !w->m_logRefreshPending && !w->m_logRefreshTimer.isActive(),20000);
  w->m_mode=w->m_modeTx="FT8";w->m_TRperiod=15;w->m_freqNominal=14074000;
  w->m_diskData=false;w->m_manualDecode=false;w->m_houndMode=false;w->m_tci=false;
  w->m_autoseq=true;w->m_autoTx=false;w->m_singleshot=false;w->m_skipTx1=false;
  w->m_callMode=callMode;w->m_callPrioCQ=report;w->m_callWorkedB4=false;w->m_answerWorkedB4=false;
  w->m_callHigherNewCall=false;w->m_lockTxFreq=false;w->m_reply_other=false;w->m_counter=0;
  w->m_generalPostQsoCall.clear();w->ui->tabWidget->setCurrentIndex(0);
  w->ui->actionReport_message_priority->setChecked(true);
  w->m_qsoHistory.init();w->m_qsoHistory.owndata("NA","K","AA00",false);
  w->m_logBook.init("N0CALL","","","ZZ9HO",QDir(temp.path()+"/handoff"));
  w->m_logBook.addAsWorked("K1AAA","20m","FT8","20261030","FN20","");
  w->ui->dxCallEntry->setText("K7END");w->ui->dxGridEntry->setText("FN20");
  w->genStdMsgs("-10");if(rr73)w->on_txb4_clicked();else w->on_txb5_clicked();
  manualEnable();QVERIFY(w->m_enableTx);
  const qint64 period=15000,slot=w->m_jtdxtime->currentMSecsSinceEpoch2()/period*period-4*period;
  auto stamp=[](qint64 time){return QDateTime::fromMSecsSinceEpoch(time,Qt::UTC).toString("hhmmss");};
  w->prepareSignoff(QString("K7END N0CALL ")+(rr73?"RR73":"73"));
  w->m_signoff.prepare("K7END",w->generalContext(),rr73?SignoffState::RR73:SignoffState::SeventyThree,slot,period);
  w->m_signoff.started();w->m_signoff.consumed();w->m_signoff.naturalEnd();w->m_signoff.released();
  if(guard==8) {
    // Model an earlier failed save: the first successful write will be the
    // confirmation retry, with acceptQSO -> generalTick running synchronously.
    w->m_signoffRoundEndRecorded=true;w->m_lastloggedcall.clear();
    w->m_dateTimeQSOOn=QDateTime::fromMSecsSinceEpoch(slot,Qt::UTC);
  }
  w->m_qsoHistory.message("K7END",rr73?QsoHistory::SRR73:QsoHistory::S73,0,"","","NA","K",
    QTime::fromString(stamp(slot),"hhmmss").msecsSinceStartOfDay()/1000,"-10",1500,"FT8");
  w->m_transmitting=false;g_iptt=0;w->m_tx_when_ready=false;w->m_rigState.ptt(false);
  w->m_decoderBusy=true;w->m_monitoring=true;w->m_lostaudio=false;w->m_dataAvailable=true;
  w->m_msDecStarted=slot+period;w->m_processAuto_done=true;
  TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  auto decode=[&](QString const& message,int snr,int frequency){
    w->processDecodedOutput((stamp(slot+period)+QString(" %1  0.1 %2 ~ ").arg(snr,3).arg(frequency)+message+"\n").toUtf8());
  };
  if(rr73) {
    decode("N0CALL K7END 73",-10,1500);
    // GUI ticks can confirm RR73 before the decoder emits later callers.
    w->generalTick();
  }
  decode(report?"N0CALL K8NXT -12":"N0CALL K8NXT FN31",-18,1700);
  decode("CQ JA1AAA PM95",-5,1900); // A stronger new DXCC must not beat our caller.
  auto audio=[&]{
    QByteArray pcm(6000,0);
    for(qint64 end=slot+period+250;end<=slot+2*period;end+=250)
      w->m_detector->timedPcmWritten(pcm,end);
  };
  if(!lateAudio)audio();
  w->processDecodedOutput("<DecodeFinished>    2\n");
  if(lateAudio) {
    if(guard==1)w->m_logLoading=true;
    if(guard==4)w->m_singleshot=true;
    if(guard==5) {
      w->m_watchdogFinishing=true;w->m_watchdogCall="K7END";
      w->m_watchdogContext=w->m_config.my_callsign()+"|"+QString::number(w->m_freqNominal)+"|"+w->m_mode;
    }
    audio();
    if(guard==2)w->on_stopTxButton_clicked();
    if(guard==3)w->ui->dxCallEntry->setText("K8MAN");
    if(guard==6)w->m_autoseq=false;
    if(guard==7)w->m_freqNominal=7074000;
    w->generalTick();
    if(guard==1) {
      QVERIFY(w->m_hisCall.isEmpty());QVERIFY(!w->generalTxAllowed());
      w->m_logLoading=false;w->generalTick();
    }
  }
  QVERIFY(!w->m_signoff.active());
  if(guard==8)QTRY_VERIFY_WITH_TIMEOUT((w->generalTick(),w->m_hisCall=="K8NXT"),20000);
  if(guard<=1 || guard==8) {
    QCOMPARE(w->m_hisCall,QString("K8NXT"));QCOMPARE(w->m_ntx,report?3:2);
    QVERIFY(w->m_enableTx);QVERIFY(w->m_manualTxAuthorized);
    QVERIFY(w->selectedTransmitText().startsWith("K8NXT "));
  } else {
    QVERIFY(w->m_hisCall!="K8NXT");
    if(guard==3)QCOMPARE(w->m_hisCall,QString("K8MAN"));
    if(guard==2 || guard==4 || guard==5){QVERIFY(!w->m_enableTx);QVERIFY(!w->m_manualTxAuthorized);}
    QVERIFY(!w->m_ordinaryAutoPending);
  }
  QCOMPARE(g_iptt,0);QCOMPARE(ptt.pttOnCount(),0);
}

void RegressionTest::cqLateDecodeStillAnswers_data()
{
  QTest::addColumn<int>("mode");QTest::addColumn<bool>("onAir");QTest::addColumn<bool>("single");
  for(int mode=1;mode<=3;++mode)for(bool onAir:{false,true})for(bool single:{false,true})
    QTest::newRow(qPrintable(QString("mode%1-onAir%2-single%3").arg(mode).arg(onAir).arg(single)))
      <<mode<<onAir<<single;
}

void RegressionTest::cqLateDecodeStillAnswers()
{
  QFETCH(int,mode);QFETCH(bool,onAir);QFETCH(bool,single);
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  QTRY_VERIFY_WITH_TIMEOUT(w->m_logReady && !w->m_logLoading && !w->m_logImportActive
    && !w->m_logRefreshPending && !w->m_logRefreshTimer.isActive(),20000);
  auto offset=w->m_jtdxtime->GetOffset();
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&]{
    w->m_transmitting=false;g_iptt=0;w->m_tx_when_ready=false;w->m_rigState.ptt(false);
    w->m_decoderBusy=false;w->m_monitoring=false;w->on_stopTxButton_clicked();
    w->clearDX("late CQ test cleanup");w->m_autoseq=false;w->m_singleshot=false;w->m_jtdxtime->SetOffset(offset);
  }};
  w->m_mode=w->m_modeTx="FT8";w->m_TRperiod=15;w->m_freqNominal=14074000;
  w->m_diskData=false;w->m_manualDecode=false;w->m_houndMode=false;
  w->m_autoseq=true;w->m_autoTx=false;w->m_singleshot=single;w->m_skipTx1=false;
  w->m_callMode=mode;w->m_callPrioCQ=true;w->m_callWorkedB4=false;w->m_answerWorkedB4=false;
  w->m_callHigherNewCall=false;w->m_lockTxFreq=false;w->m_reply_other=false;w->m_counter=0;
  w->m_generalPostQsoCall.clear();w->ui->tabWidget->setCurrentIndex(0);
  w->ui->actionReport_message_priority->setChecked(true);
  w->m_qsoHistory.init();w->m_qsoHistory.owndata("NA","K","AA00",false);
  w->m_logBook.init("N0CALL","","","ZZ9LC",QDir(temp.path()+"/late-cq"));
  w->m_logBook.addAsWorked("K1AAA","20m","FT8","20261030","FN20","");
  w->clearDX("CQ test");manualEnable();QVERIFY(w->m_enableTx);
  w->m_decoderBusy=true;w->m_processAuto_done=!onAir;
  w->m_transmitting=onAir;g_iptt=onAir?1:0;w->m_rigState.ptt(onAir);
  w->m_currentMessage=w->ui->tx6->text();w->m_QSOProgress=MainWindow::CALLING;
  TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  w->processDecodedOutput("090000 -18  0.1 1700 ~ N0CALL K8LATE FN31\n");
  w->processDecodedOutput("090000  -5  0.1 1900 ~ CQ JA1AAA PM95\n");
  w->processDecodedOutput("<DecodeFinished>    2\n");
  if(onAir) {
    QVERIFY(w->m_hisCall.isEmpty());QCOMPARE(w->m_ntx,6);
    QVERIFY(w->m_ordinaryAutoPending);QVERIFY(w->generalTxAllowed());
    w->generalTick();QVERIFY(w->m_hisCall.isEmpty());QCOMPARE(g_iptt,1);
    w->m_transmitting=false;g_iptt=0;w->m_rigState.ptt(false);
    // Real guiUpdate dispatches the delayed decision before calculating the
    // next TX slot. At :01 the caller's reply slot (:15) must not key PTT.
    auto now=w->m_jtdxtime->currentDateTimeUtc2(),target=now;target.setTime(QTime(9,0,1));
    w->m_jtdxtime->SetOffset(offset+now.msecsTo(target)/1000.0);
    w->guiUpdate();
  }
  QCOMPARE(w->m_hisCall,QString("K8LATE"));QCOMPARE(w->m_ntx,2);
  QVERIFY(w->m_enableTx);QVERIFY(w->m_manualTxAuthorized);QVERIFY(!w->m_txFirst);
  QVERIFY(!w->m_ordinaryAutoPending);QCOMPARE(g_iptt,0);
  QCOMPARE(ptt.pttOnCount(),0);
}
