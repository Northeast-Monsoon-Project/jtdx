// Retry decisions use real history accounting and the normal handoff path;
// no sound device or radio is driven by these tests.
void RegressionTest::ordinaryRetryLimits_data()
{
  QTest::addColumn<int>("stage");QTest::addColumn<bool>("automatic");
  QTest::addColumn<bool>("incoming");QTest::addColumn<bool>("enabled");QTest::addColumn<bool>("single");
  for(int stage=0;stage<7;++stage)for(bool automatic:{false,true})for(bool incoming:{false,true})
    QTest::newRow(qPrintable(QString("stage%1-auto%2-incoming%3").arg(stage).arg(automatic).arg(incoming)))
      <<stage<<automatic<<incoming<<true<<false;
  QTest::newRow("disabled-manual")<<0<<false<<false<<false<<false;
  QTest::newRow("disabled-auto")<<0<<true<<false<<false<<false;
  QTest::newRow("single-qso-stops")<<0<<true<<true<<true<<true;
}

void RegressionTest::ordinaryRetryLimits()
{
  QFETCH(int,stage);QFETCH(bool,automatic);QFETCH(bool,incoming);QFETCH(bool,enabled);QFETCH(bool,single);
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  QStringList checks{"answerCQCount_checkBox","answerInCallCount_checkBox","sentRReportCount_checkBox","sentRR7373Count_checkBox"};
  QStringList values{"sbAnswerCQCounter","sbAnswerInCallCounter","sbSentRReportCounter","sbSentRR7373Counter"};
  QMap<QString,QVariant> previous,selected;
  for(auto name:checks){previous[name]=dialog->findChild<QCheckBox*>(name)->isChecked();selected[name]=enabled;}
  for(auto name:values){previous[name]=dialog->findChild<QSpinBox*>(name)->value();selected[name]=2;}
  auto configure=[&](QMap<QString,QVariant> settings){
    QTimer::singleShot(0,dialog,[=]{
      for(auto it=settings.cbegin();it!=settings.cend();++it) {
        if(auto c=dialog->findChild<QCheckBox*>(it.key()))c->setChecked(it.value().toBool());
        else dialog->findChild<QSpinBox*>(it.key())->setValue(it.value().toInt());
      }
      QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });
    QCOMPARE(w->m_config.exec(),int(QDialog::Accepted));
  };
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&]{
    w->on_stopTxButton_clicked();w->clearDX("retry cleanup");configure(previous);
    w->m_autoseq=false;w->m_singleshot=false;w->m_skipTx1=false;
  }};
  configure(selected);
  QTRY_VERIFY_WITH_TIMEOUT(w->m_logReady && !w->m_logLoading && !w->m_logImportActive
    && !w->m_logRefreshPending && !w->m_logRefreshTimer.isActive(),20000);
  w->m_mode=w->m_modeTx="FT8";w->m_TRperiod=15;w->m_freqNominal=14074000;
  w->m_diskData=false;w->m_manualDecode=false;w->m_houndMode=false;w->m_decoderBusy=false;
  w->m_autoseq=automatic;w->m_autoTx=false;w->m_singleshot=single;w->m_skipTx1=stage==1;
  w->m_callMode=3;w->m_callPrioCQ=false;w->m_callWorkedB4=false;w->m_answerWorkedB4=true;
  w->m_counter=0;w->m_reply_other=false;w->m_lockTxFreq=false;
  w->m_qsoHistory=QsoHistory{};
  w->m_qsoHistory.init();w->m_qsoHistory.owndata("NA","K","AA00",false);w->m_qsoHistory.time(45);
  w->ui->dxCallEntry->setText("K7TRY");w->ui->dxGridEntry->setText("FN20");
  w->genStdMsgs("-10");w->on_txb1_clicked();manualEnable();QVERIFY(w->m_enableTx);
  // High novelty and a deliberately selected out-of-area directed CQ both
  // used to bypass ordinary retries (priority exemption / count clamped to 1).
  w->m_qsoHistory.message("K7TRY",stage==2?QsoHistory::RCALL:QsoHistory::RCQ,28,"FN20","JA","NA","K",15,"-10",1500,"FT8");
  auto sent=stage==0?QsoHistory::SCALL:stage<=2?QsoHistory::SREPORT:stage==3?QsoHistory::SRREPORT
    :stage==4?QsoHistory::SRR:stage==5?QsoHistory::SRR73:QsoHistory::S73;
  if(stage>=5) {
    w->m_signoffSelectionRevision=w->m_generalSelectionRevision;
    for(int n=0;n<2;++n) {
      w->m_signoff.prepare("K7TRY",w->generalContext(),stage==5?SignoffState::RR73:SignoffState::SeventyThree,n*30000,15000);
      w->m_signoff.started();w->m_signoff.consumed();w->m_signoff.naturalEnd();w->m_signoff.released();
      if(stage==6)w->m_signoff.receive("K7TRY",w->generalContext(),"RR73",n*30000+15000);
      w->m_signoff.receiveCycleFinished(n*30000+15000);
    }
  }
  w->m_qsoHistory.message("K7TRY",sent,0,"-10",stage==1?"S":"","NA","K",30,"-10",1500,"FT8");
  if(stage<5)QVERIFY(!w->ordinaryRetryDue());
  w->m_qsoHistory.message("K7TRY",sent,0,"-10",stage==1?"S":"","NA","K",30,"-10",1500,"FT8");
  QCOMPARE(w->m_qsoHistory.sent_count("K7TRY"),2);
  w->m_transmitting=true;g_iptt=1;QVERIFY(!w->ordinaryRetryDue());
  w->m_transmitting=false;g_iptt=0;w->m_tx_when_ready=false;
  QCOMPARE(w->ordinaryRetryDue(),enabled);
  if(incoming)w->m_qsoHistory.message("K7NXT",QsoHistory::RCALL,5,"FN31","","NA","K",45,"-12",1700,"FT8");
  TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  // Both AutoSeq-on and the independent AutoSeq-off transmit-slot check use
  // this exact continuation path; the selected search setting remains off.
  w->process_Auto();
  QCOMPARE(w->m_qsoHistory.outgoingCallDeferred("K7TRY"),enabled && stage<=1);
  if(!enabled){QCOMPARE(w->m_hisCall,QString("K7TRY"));QVERIFY(w->m_enableTx);}
  else if(single){QVERIFY(!w->m_enableTx);QVERIFY(!w->m_manualTxAuthorized);}
  else {
    QVERIFY(w->m_enableTx);QVERIFY(w->m_manualTxAuthorized);QVERIFY(w->m_txSession.active());
    QCOMPARE(w->m_hisCall,incoming?QString("K7NXT"):QString());
    QCOMPARE(w->m_ntx,incoming?2:6);QCOMPARE(w->m_autoseq,automatic);QVERIFY(!w->m_callPrioCQ);
    if(stage<=1) {
      // A stronger repeated invitation cannot bypass the new cooldown, even
      // with worked-station search enabled. An incoming reply still can.
      if(incoming)w->m_qsoHistory.complete("K7NXT");
      w->clearDX("retry followup");w->m_callPrioCQ=true;w->m_callWorkedB4=true;
      w->m_qsoHistory.time(60);
      w->m_qsoHistory.message("K7TRY",QsoHistory::RCQ,28,"FN20","","NA","K",60,"+05",1700,"FT8");
      w->process_Auto();QVERIFY(w->m_hisCall.isEmpty());QCOMPARE(w->m_ntx,6);
      w->m_qsoHistory.time(90);
      w->m_qsoHistory.message("K7TRY",QsoHistory::RCALL,28,"FN20","","NA","K",90,"-12",1700,"FT8");
      w->process_Auto();QCOMPARE(w->m_hisCall,QString("K7TRY"));QCOMPARE(w->m_ntx,2);
    }
  }
  QCOMPARE(ptt.pttOnCount(),0);
}

void RegressionTest::manualRetrySelectionKeepsOnlyCurrentAttempt()
{
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  bool prior=w->m_config.quick_call(),wasVisible=w->isVisible();
  auto configure=[&](bool quick){
    QTimer::singleShot(0,dialog,[=]{dialog->findChild<QCheckBox*>("quick_call_check_box")->setChecked(quick);QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);});
    QCOMPARE(w->m_config.exec(),int(QDialog::Accepted));
  };
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&]{
    w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);configure(prior);
    w->clearDX("retry selection cleanup");w->m_autoseq=false;if(!wasVisible)w->hide();
  }};
  configure(true);
  QTRY_VERIFY_WITH_TIMEOUT(w->m_logReady && !w->m_logLoading && !w->m_logRefreshPending
    && !w->m_logImportActive && !w->m_logRefreshTimer.isActive(),20000);
  w->m_mode=w->m_modeTx="FT8";w->m_TRperiod=15;w->m_freqNominal=14074000;
  w->m_diskData=false;w->m_manualDecode=false;w->m_houndMode=false;w->m_lockTxFreq=false;
  w->m_autoseq=false;w->m_autoTx=false;w->m_singleshot=false;w->m_skipTx1=false;
  w->m_qsoHistory.init();w->m_qsoHistory.owndata("NA","K","AA00",false);
  w->show();w->ui->tabWidget->setCurrentIndex(0);QCoreApplication::processEvents();
  QString call="K8RPT/P",row="000015 -10  0.0 1500 ~ CQ K8RPT/P FN20";
  w->m_qsoHistory.message(call,QsoHistory::S73,28,"","JA","NA","K",0,"-10",1500,"FT8");
  w->m_qsoHistory.complete(call);w->ui->dxCallEntry->setText(call);
  TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  DoubleClickTxFixture::click(w->ui->decodedTextBrowser2,row);
  QVERIFY(w->m_enableTx);QCOMPARE(w->m_hisCall,call);
  QString grid;QCOMPARE(w->m_qsoHistory.status(call,grid),QsoHistory::RCQ);
  QCOMPARE(w->m_qsoHistory.sent_count(call),0);
  for(int i=0;i<2;++i)w->m_qsoHistory.message(call,QsoHistory::SCALL,0,"FN20","","NA","K",30,"-10",1500,"FT8");
  QCOMPARE(w->m_qsoHistory.sent_count(call),2);
  DoubleClickTxFixture::click(w->ui->decodedTextBrowser2,row);
  QCOMPARE(w->m_qsoHistory.sent_count(call),2); // Same /P station is not a new attempt.
  DoubleClickTxFixture::click(w->ui->decodedTextBrowser,"000045 -12  0.0 1700 ~ CQ K8NEW FN31");
  QCOMPARE(w->m_hisCall,QString("K8NEW"));QCOMPARE(w->m_qsoHistory.sent_count("K8NEW"),0);
  QVERIFY(w->m_enableTx);QVERIFY(!w->m_autoseq);
  QCOMPARE(ptt.pttOnCount(),0);
  // Ordinary counters must not participate in a managed attempt.
  prepareManaged(ManagedMode::QsoEnthusiast);
  w->ui->dxCallEntry->setText("K8MAN");w->m_managed.begin("K8MAN");w->m_managedTxReady=true;
  for(int i=0;i<6;++i)w->m_qsoHistory.message("K8MAN",QsoHistory::SCALL,0,"FN20","","NA","K",30,"-10",1500,"FT8");
  QVERIFY(!w->ordinaryRetryDue());w->process_Auto();QCOMPARE(w->m_hisCall,QString("K8MAN"));
}

void RegressionTest::ordinaryRetryRecomputesTransmitSlot_data()
{
  QTest::addColumn<QString>("mode");QTest::addColumn<bool>("automatic");
  QTest::newRow("FT8-manual")<<QString("FT8")<<false;
  QTest::newRow("FT8-auto")<<QString("FT8")<<true;
  QTest::newRow("Q65-manual")<<QString("Q65")<<false;
}

void RegressionTest::ordinaryRetryRecomputesTransmitSlot()
{
  QFETCH(QString,mode);QFETCH(bool,automatic);
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  bool prior=w->m_config.answerCQCount();int limit=w->m_config.nAnswerCQCounter();
  auto configure=[&](bool enabled,int count){
    QTimer::singleShot(0,dialog,[=]{
      dialog->findChild<QCheckBox*>("answerCQCount_checkBox")->setChecked(enabled);
      dialog->findChild<QSpinBox*>("sbAnswerCQCounter")->setValue(count);
      QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });QCOMPARE(w->m_config.exec(),int(QDialog::Accepted));
  };
  auto offset=w->m_jtdxtime->GetOffset();
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&]{
    w->on_stopTxButton_clicked();w->m_jtdxtime->SetOffset(offset);configure(prior,limit);
    w->clearDX("retry slot cleanup");w->m_autoseq=false;w->on_actionFT8_triggered();stopWaveAudio();
  }};
  configure(true,2);
  if(mode=="Q65") {w->selectUpstreamMode(mode);w->m_upstreamPeriod->setCurrentText("15");}
  else w->on_actionFT8_triggered();
  stopWaveAudio();
  QTRY_VERIFY_WITH_TIMEOUT(w->m_logReady && !w->m_logLoading && !w->m_logRefreshPending
    && !w->m_logImportActive && !w->m_logRefreshTimer.isActive(),20000);
  w->m_mode=w->m_modeTx=mode;w->m_TRperiod=15;w->m_freqNominal=14074000;
  w->m_diskData=false;w->m_manualDecode=false;w->m_houndMode=false;w->m_decoderBusy=false;
  w->m_autoseq=automatic;w->m_autoTx=false;w->m_singleshot=false;w->m_skipTx1=false;
  w->m_callMode=3;w->m_callPrioCQ=false;w->m_callWorkedB4=false;w->m_answerWorkedB4=true;
  w->m_counter=0;w->m_reply_other=false;w->m_lockTxFreq=false;w->m_processAuto_done=false;
  w->m_qsoHistory.init();w->m_qsoHistory.owndata("NA","K","AA00",false);w->m_qsoHistory.time(32400);
  w->ui->dxCallEntry->setText("K8TIM");w->genStdMsgs("-10");w->on_txb1_clicked();manualEnable();QVERIFY(w->m_enableTx);
  for(int i=0;i<2;++i)w->m_qsoHistory.message("K8TIM",QsoHistory::SCALL,28,"FN20","JA","NA","K",32370,"-10",1500,mode);
  w->m_qsoHistory.message("K8NEW",QsoHistory::RCALL,5,"FN31","","NA","K",32400,"-12",1700,mode);
  w->assignTxSlot(true);
  // Q65 accepts a new TX decision only within the first 1000 ms. Starting
  // exactly at :01 made elapsed GUI/spy setup time skip that decision on
  // Wine, while faster runs could happen in the same millisecond. This test
  // checks the changed slot, so use the interior of the valid start window.
  auto now=w->m_jtdxtime->currentDateTimeUtc2(),target=now;target.setTime(QTime(9,0,0,250));
  w->m_jtdxtime->SetOffset(offset+now.msecsTo(target)/1000.0);
  TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  QVERIFY(w->ordinaryRetryDue());
  QVERIFY2(w->m_jtdxtime->currentMSecsSinceEpoch2()%15000<1000,
           "Fixture must exercise the transmit-start window, not the late-slot guard");
  w->guiUpdate();
  QCOMPARE(w->m_hisCall,QString("K8NEW"));QVERIFY(!w->m_txFirst);QCOMPARE(g_iptt,0);QVERIFY(w->m_enableTx);
  w->guiUpdate();QCOMPARE(g_iptt,0); // New TX slot starts at :15, not the old :00 slot.
  QCOMPARE(ptt.pttOnCount(),0);
}
