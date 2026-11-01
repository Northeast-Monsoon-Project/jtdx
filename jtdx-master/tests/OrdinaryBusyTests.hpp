// Exercise the real decoded-output and AutoSeq paths with no audio/PTT starts.
namespace OrdinaryBusyFixture {
struct Settings {
  Configuration& config;
  QDialog* dialog;
  bool oldWait,oldCount;
  int oldLimit;
  Settings(Configuration& value,QDialog* widget) : config(value),dialog(widget),
    oldWait(value.halttxreplyother()),oldCount(value.answerCQCount()),oldLimit(value.nAnswerCQCounter()) {}
  bool apply(bool wait,bool count=true,int limit=3) {
    QTimer::singleShot(0,dialog,[=]{
      dialog->findChild<QCheckBox*>("haltTxReplyOther_checkBox")->setChecked(wait);
      dialog->findChild<QCheckBox*>("answerCQCount_checkBox")->setChecked(count);
      dialog->findChild<QSpinBox*>("sbAnswerCQCounter")->setValue(limit);
      QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });
    return config.exec()==QDialog::Accepted;
  }
  ~Settings() {apply(oldWait,oldCount,oldLimit);}
};
struct Cleanup {std::function<void()> action;~Cleanup(){action();}};
QByteArray row(QString const& message,QString stamp="000045",int frequency=1900)
{
  return (stamp+QString(" -10  0.1 %1 ~ ").arg(frequency)+message+"\n").toUtf8();
}
}

void RegressionTest::prepareOrdinaryBusy(bool automatic,int sent)
{
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  QTRY_VERIFY_WITH_TIMEOUT(w->m_logReady && !w->m_logLoading && !w->m_logRefreshPending
    && !w->m_logImportActive && !w->m_logRefreshTimer.isActive(),20000);
  w->m_mode=w->m_modeTx="FT8";w->m_TRperiod=15;w->m_freqNominal=14074000;
  w->m_diskData=false;w->m_manualDecode=false;w->m_houndMode=false;w->m_decoderBusy=false;
  w->m_autoseq=automatic;w->m_autoTx=false;w->m_singleshot=false;w->m_skipTx1=false;
  w->m_callMode=3;w->m_callPrioCQ=true;w->m_callWorkedB4=true;w->m_answerWorkedB4=true;
  w->m_counter=0;w->m_reply_other=false;w->m_reply_me=false;w->m_reply_CQ73=false;
  w->m_processAuto_done=false;w->m_lockTxFreq=false;w->m_nguardfreq=51;
  w->m_rigState.ptt(false);w->ptt0Timer.stop();w->ptt1Timer.stop();
  w->m_qsoHistory=QsoHistory{};w->m_qsoHistory.init();w->m_qsoHistory.owndata("NA","K","AA00",false);
  w->m_qsoHistory.time(45);w->ui->tabWidget->setCurrentIndex(0);
  w->ui->dxCallEntry->setText("K8BSY");w->ui->dxGridEntry->setText("FN20");
  w->ui->TxFreqSpinBox->setValue(1500);w->ui->RxFreqSpinBox->setValue(1900);
  w->genStdMsgs("-10");w->on_txb1_clicked();manualEnable();QVERIFY(w->m_enableTx);
  w->m_qsoHistory.message("K8BSY",QsoHistory::RCQ,5,"FN20","","NA","K",15,"-10",1500,"FT8");
  for(int n=0;n<sent;++n)
    w->m_qsoHistory.message("K8BSY",QsoHistory::SCALL,0,"FN20","","NA","K",30,"-10",1500,"FT8");
}

void RegressionTest::ordinaryBusyOptionBehavior_data()
{
  QTest::addColumn<bool>("checked");QTest::addColumn<bool>("automatic");QTest::addColumn<int>("frequency");
  QTest::newRow("checked-away-from-tx")<<true<<true<<1900;
  QTest::newRow("checked-same-frequency")<<true<<true<<1500;
  QTest::newRow("unchecked-same-frequency")<<false<<true<<1500;
  QTest::newRow("unchecked-away-from-tx")<<false<<true<<1900;
  QTest::newRow("manual-legacy-halt")<<true<<false<<1900;
}

void RegressionTest::ordinaryBusyOptionBehavior()
{
  QFETCH(bool,checked);QFETCH(bool,automatic);QFETCH(int,frequency);
  auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  OrdinaryBusyFixture::Settings config(w->m_config,dialog);QVERIFY(config.apply(checked));
  OrdinaryBusyFixture::Cleanup cleanup{[&]{w->on_stopTxButton_clicked();w->clearDX("busy cleanup");w->m_autoseq=false;}};
  prepareOrdinaryBusy(automatic);
  auto manualWait=w->m_config.Wait_features_enabled();
  TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  w->processDecodedOutput(OrdinaryBusyFixture::row("K9OTH K8BSY -12","000045",frequency));
  QCOMPARE(w->ordinaryBusyWaiting(),automatic && checked);
  QCOMPARE(w->m_enableTx,automatic);QCOMPARE(w->m_manualTxAuthorized,automatic);
  QCOMPARE(w->m_config.Wait_features_enabled(),manualWait);QVERIFY(!w->m_generalState.waiting());
  if(automatic) {
    w->process_Auto();
    QCOMPARE(w->m_hisCall,QString("K8BSY"));QCOMPARE(w->m_qsoHistory.sent_count("K8BSY"),1);
    QCOMPARE(w->generalTxAllowed(),!checked);QVERIFY(w->m_txSession.active());
  }
  QCOMPARE(ptt.pttOnCount(),0);
}

void RegressionTest::ordinaryBusyResumesOnTarget_data()
{
  QTest::addColumn<QString>("message");QTest::addColumn<int>("tx");
  QTest::newRow("CQ")<<QString("CQ K8BSY FN20")<<1;
  QTest::newRow("73")<<QString("K9OTH K8BSY 73")<<1;
  QTest::newRow("RR73")<<QString("K9OTH K8BSY RR73")<<1;
  QTest::newRow("RRR")<<QString("K9OTH K8BSY RRR")<<1;
  QTest::newRow("direct-report")<<QString("N0CALL K8BSY -12")<<3;
}

void RegressionTest::ordinaryBusyResumesOnTarget()
{
  QFETCH(QString,message);QFETCH(int,tx);
  auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  OrdinaryBusyFixture::Settings config(w->m_config,dialog);QVERIFY(config.apply(true));
  OrdinaryBusyFixture::Cleanup cleanup{[&]{
    w->decodeBusy(false);w->killFileTimer.stop();w->on_stopTxButton_clicked();w->clearDX("busy cleanup");w->m_autoseq=false;
  }};
  prepareOrdinaryBusy();TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  w->decodeBusy(true);
  w->processDecodedOutput(OrdinaryBusyFixture::row("K9OTH K8BSY -12"));QVERIFY(w->ordinaryBusyWaiting());
  w->processDecodedOutput("<DecodeFinished>    1\n");QVERIFY(!w->m_decoderBusy);QVERIFY(w->ordinaryBusyWaiting());
  w->decodeBusy(true);w->m_processAuto_done=false;
  w->m_reply_me=w->m_reply_other=w->m_reply_CQ73=false;
  w->processDecodedOutput(OrdinaryBusyFixture::row("CQ K8ALT FN31","000115"));
  w->process_Auto();QVERIFY(w->ordinaryBusyWaiting());QCOMPARE(w->m_hisCall,QString("K8BSY"));
  w->processDecodedOutput(OrdinaryBusyFixture::row(message,"000145"));
  QVERIFY(w->ordinaryBusyWaiting()); // Real decoder output arrives before the completion marker.
  w->generalTick();QVERIFY(w->ordinaryBusyWaiting());
  w->processDecodedOutput("<DecodeFinished>    1\n");
  QVERIFY(!w->m_decoderBusy);
  QVERIFY(!w->ordinaryBusyWaiting());QCOMPARE(w->m_hisCall,QString("K8BSY"));QCOMPARE(w->m_ntx,tx);
  QVERIFY(w->m_enableTx && w->m_manualTxAuthorized && w->generalTxAllowed());
  // No idle GUI timer interval is required before the next decoder pass.
  w->decodeBusy(true);QVERIFY(!w->ordinaryBusyWaiting());
  if(tx==1)QCOMPARE(w->m_qsoHistory.sent_count("K8BSY"),1);
  QCOMPARE(ptt.pttOnCount(),0);
}

void RegressionTest::ordinaryBusyDecodeOrder_data()
{
  QTest::addColumn<QString>("initialUtc");QTest::addColumn<QString>("availableUtc");
  QTest::addColumn<QString>("busyUtc");QTest::addColumn<bool>("availableFirst");
  QTest::addColumn<bool>("resume");
  QTest::newRow("old-report-after-final")<<QString("000045")<<QString("000145")<<QString("000115")<<true<<true;
  QTest::newRow("old-report-before-final")<<QString("000045")<<QString("000145")<<QString("000115")<<false<<true;
  QTest::newRow("same-slot-report-after-final")<<QString("000045")<<QString("000145")<<QString("000145")<<true<<true;
  QTest::newRow("same-slot-report-before-final")<<QString("000045")<<QString("000145")<<QString("000145")<<false<<true;
  QTest::newRow("new-report-after-final")<<QString("000045")<<QString("000115")<<QString("000145")<<true<<false;
  QTest::newRow("new-report-before-old-final")<<QString("000045")<<QString("000115")<<QString("000145")<<false<<false;
  QTest::newRow("midnight-old-report-after-final")<<QString("235915")<<QString("000015")<<QString("235945")<<true<<true;
  QTest::newRow("midnight-new-report-before-old-final")<<QString("235915")<<QString("235945")<<QString("000015")<<false<<false;
}

void RegressionTest::ordinaryBusyDecodeOrder()
{
  QFETCH(QString,initialUtc);QFETCH(QString,availableUtc);QFETCH(QString,busyUtc);
  QFETCH(bool,availableFirst);QFETCH(bool,resume);
  auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  OrdinaryBusyFixture::Settings config(w->m_config,dialog);QVERIFY(config.apply(true));
  OrdinaryBusyFixture::Cleanup cleanup{[&]{
    w->decodeBusy(false);w->killFileTimer.stop();w->on_stopTxButton_clicked();w->clearDX("busy cleanup");w->m_autoseq=false;
  }};
  prepareOrdinaryBusy();TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  w->decodeBusy(true);
  w->processDecodedOutput(OrdinaryBusyFixture::row("K9OTH K8BSY -12",initialUtc));
  w->processDecodedOutput("<DecodeFinished>    1\n");QVERIFY(w->ordinaryBusyWaiting());
  w->decodeBusy(true);w->m_processAuto_done=false;
  w->m_reply_me=w->m_reply_other=w->m_reply_CQ73=false;
  auto available=OrdinaryBusyFixture::row("K9OTH K8BSY RR73",availableUtc);
  auto busy=OrdinaryBusyFixture::row("K9OTH K8BSY -12",busyUtc);
  w->processDecodedOutput(availableFirst?available:busy);
  w->processDecodedOutput(availableFirst?busy:available);
  QVERIFY(w->ordinaryBusyWaiting());QVERIFY(!w->generalTxAllowed());
  w->processDecodedOutput("<DecodeFinished>    1\n");
  QCOMPARE(w->ordinaryBusyWaiting(),!resume);QCOMPARE(w->generalTxAllowed(),resume);
  QCOMPARE(w->m_hisCall,QString("K8BSY"));QCOMPARE(w->m_qsoHistory.sent_count("K8BSY"),1);
  QVERIFY(w->m_enableTx && w->m_manualTxAuthorized);QCOMPARE(w->m_ntx,1);
  QCOMPARE(ptt.pttOnCount(),0);
}

void RegressionTest::ordinaryBusyInitialDecodeOrder_data()
{
  QTest::addColumn<QString>("available");QTest::addColumn<QString>("busyUtc");
  QTest::addColumn<bool>("wait");
  QTest::newRow("initial-cq-then-old-report")<<QString("CQ K8BSY FN20")<<QString("000115")<<false;
  QTest::newRow("initial-final-then-old-report")<<QString("K9OTH K8BSY RR73")<<QString("000115")<<false;
  QTest::newRow("initial-cq-then-new-report")<<QString("CQ K8BSY FN20")<<QString("000215")<<true;
  QTest::newRow("initial-final-then-new-report")<<QString("K9OTH K8BSY RR73")<<QString("000215")<<true;
}

void RegressionTest::ordinaryBusyInitialDecodeOrder()
{
  QFETCH(QString,available);QFETCH(QString,busyUtc);QFETCH(bool,wait);
  auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  OrdinaryBusyFixture::Settings config(w->m_config,dialog);QVERIFY(config.apply(true));
  OrdinaryBusyFixture::Cleanup cleanup{[&]{
    w->decodeBusy(false);w->killFileTimer.stop();w->on_stopTxButton_clicked();w->clearDX("busy cleanup");w->m_autoseq=false;
  }};
  prepareOrdinaryBusy();TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  QVERIFY(!w->ordinaryBusyWaiting());QVERIFY(w->m_ordinaryBusyCall.isEmpty());
  w->decodeBusy(true);
  w->processDecodedOutput(OrdinaryBusyFixture::row(available,"000145"));
  QVERIFY(!w->ordinaryBusyWaiting());QVERIFY(w->generalTxAllowed());
  w->processDecodedOutput(OrdinaryBusyFixture::row("K9OTH K8BSY -12",busyUtc));
  w->processDecodedOutput("<DecodeFinished>    1\n");
  QCOMPARE(w->ordinaryBusyWaiting(),wait);QCOMPARE(w->generalTxAllowed(),!wait);
  // A later current report still creates a real wait even if this batch had
  // already set the legacy m_reply_CQ73 flag. Availability is not a blanket veto.
  if(!wait) {
    w->decodeBusy(true);
    w->processDecodedOutput(OrdinaryBusyFixture::row("K9OTH K8BSY -12","000215"));
    w->processDecodedOutput("<DecodeFinished>    1\n");
    QVERIFY(w->ordinaryBusyWaiting());QVERIFY(!w->generalTxAllowed());
  }
  w->decodeBusy(true);
  w->processDecodedOutput(OrdinaryBusyFixture::row(available,"000245"));
  w->processDecodedOutput("<DecodeFinished>    1\n");
  QVERIFY(!w->ordinaryBusyWaiting());QVERIFY(w->generalTxAllowed());
  // A late callback after the completed receive cycle must not recreate the
  // wait that was just released; the passive UTC watermark survives resuming.
  w->decodeBusy(true);
  w->processDecodedOutput(OrdinaryBusyFixture::row("K9OTH K8BSY -12","000215"));
  w->processDecodedOutput("<DecodeFinished>    1\n");
  QVERIFY(!w->ordinaryBusyWaiting());QVERIFY(w->generalTxAllowed());
  QCOMPARE(w->m_hisCall,QString("K8BSY"));QCOMPARE(w->m_qsoHistory.sent_count("K8BSY"),1);
  QVERIFY(w->m_enableTx && w->m_manualTxAuthorized);QCOMPARE(w->m_ntx,1);
  QCOMPARE(ptt.pttOnCount(),0);
}

void RegressionTest::ordinaryBusyCancellation()
{
  auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  OrdinaryBusyFixture::Settings config(w->m_config,dialog);QVERIFY(config.apply(true));
  bool wasVisible=w->isVisible();
  OrdinaryBusyFixture::Cleanup cleanup{[&]{
    w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);w->clearDX("busy cleanup");
    w->m_autoseq=false;w->m_freqNominal=14074000;if(!wasVisible)w->hide();
  }};
  for(int scenario=0;scenario<12;++scenario) {
    prepareOrdinaryBusy();
    bool waiting=scenario<6;
    w->processDecodedOutput(OrdinaryBusyFixture::row(waiting?"K9OTH K8BSY -12":"CQ K8BSY FN20"));
    QCOMPARE(w->ordinaryBusyWaiting(),waiting);QVERIFY(w->ordinaryBusyContextValid());
    int operation=scenario%6;
    if(operation==0)w->on_stopTxButton_clicked();
    if(operation==1)w->on_AutoSeqButton_clicked(false);
    if(operation==2)w->ui->dxCallEntry->setText("K8NEW");
    if(operation==3)w->m_freqNominal=7074000;
    if(operation==4)w->setManagedMode(ManagedMode::QsoEnthusiast);
    if(operation==5) {
      w->show();QApplication::processEvents();
      DoubleClickTxFixture::click(w->ui->decodedTextBrowser2,"000115 -10  0.1 1900 ~ CQ K8BSY FN20");
    }
    w->refreshOrdinaryBusy();QVERIFY(w->m_ordinaryBusyCall.isEmpty());
    QVERIFY(!w->m_ordinaryBusyActive);QCOMPARE(w->m_ordinaryBusyUtc,-1);
    if(operation==0) {
      w->processDecodedOutput(OrdinaryBusyFixture::row("CQ K8BSY FN20","000115"));
      QVERIFY(!w->m_enableTx && !w->m_manualTxAuthorized);QCOMPARE(g_iptt,0);
    }
  }
}

void RegressionTest::ordinaryBusyBudgetStillExpires_data()
{
  QTest::addColumn<QString>("message");
  QTest::newRow("CQ")<<QString("CQ K8BSY FN20");
  QTest::newRow("73")<<QString("K9OTH K8BSY 73");
  QTest::newRow("RR73")<<QString("K9OTH K8BSY RR73");
  QTest::newRow("RRR")<<QString("K9OTH K8BSY RRR");
}

void RegressionTest::ordinaryBusyBudgetStillExpires()
{
  QFETCH(QString,message);
  auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  OrdinaryBusyFixture::Settings config(w->m_config,dialog);QVERIFY(config.apply(true));
  OrdinaryBusyFixture::Cleanup cleanup{[&]{
    w->decodeBusy(false);w->killFileTimer.stop();w->on_stopTxButton_clicked();w->clearDX("busy cleanup");w->m_autoseq=false;
  }};
  prepareOrdinaryBusy(true,2);
  w->processDecodedOutput(OrdinaryBusyFixture::row("K9OTH K8BSY -12"));QVERIFY(w->ordinaryBusyWaiting());
  w->m_qsoHistory.message("K8BSY",QsoHistory::SCALL,0,"FN20","","NA","K",60,"-10",1500,"FT8");
  w->decodeBusy(true);w->processDecodedOutput(OrdinaryBusyFixture::row(message,"000115"));
  QCOMPARE(w->m_qsoHistory.sent_count("K8BSY"),3);
  w->processDecodedOutput("<DecodeFinished>    1\n");QVERIFY(!w->m_decoderBusy);
  QVERIFY(!w->ordinaryBusyWaiting());QVERIFY(w->m_hisCall.isEmpty());
  QVERIFY(w->m_qsoHistory.outgoingCallDeferred("K8BSY"));
  QVERIFY(w->m_enableTx && w->m_manualTxAuthorized);QCOMPARE(g_iptt,0);
}

void RegressionTest::ordinaryBusyKeepsCurrentWave()
{
  auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  OrdinaryBusyFixture::Settings config(w->m_config,dialog);QVERIFY(config.apply(true));
  OrdinaryBusyFixture::Cleanup cleanup{[&]{
    w->m_transmitting=false;g_iptt=0;w->on_stopTxButton_clicked();w->clearDX("busy cleanup");w->m_autoseq=false;
  }};
  prepareOrdinaryBusy();w->m_transmitting=true;g_iptt=1;
  QSignalSpy stopped(w.get(),&MainWindow::endTransmitMessage);QVERIFY(stopped.isValid());
  TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  w->processDecodedOutput(OrdinaryBusyFixture::row("K9OTH K8BSY -12"));
  QVERIFY(w->ordinaryBusyWaiting());QVERIFY(w->generalTxAllowed());QVERIFY(w->m_transmitting);QCOMPARE(g_iptt,1);
  w->processDecodedOutput(OrdinaryBusyFixture::row("CQ K8BSY FN20","000115"));
  QVERIFY(w->ordinaryBusyWaiting());QCOMPARE(stopped.count(),0);QCOMPARE(ptt.count(),0);
  w->m_transmitting=false;g_iptt=0;QVERIFY(!w->generalTxAllowed());
  w->ordinaryBusyTick();QVERIFY(!w->ordinaryBusyWaiting());QVERIFY(w->m_enableTx);
  QCOMPARE(w->m_qsoHistory.sent_count("K8BSY"),1);QCOMPARE(stopped.count(),0);QCOMPARE(ptt.count(),0);
}

void RegressionTest::ordinaryBusySafetyGates()
{
  auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  OrdinaryBusyFixture::Settings config(w->m_config,dialog);QVERIFY(config.apply(true));
  auto watchdog=w->m_config.watchdog();auto blacklist=w->m_blacklist->entries();
  OrdinaryBusyFixture::Cleanup cleanup{[&]{
    w->m_logLoading=false;w->on_stopTxButton_clicked();w->clearDX("busy cleanup");w->m_autoseq=false;
    w->m_config.set_watchdog(watchdog);w->m_blacklist->save(blacklist);
  }};
  prepareOrdinaryBusy();
  w->processDecodedOutput(OrdinaryBusyFixture::row("K9OTH K8BSY -12"));
  w->m_logLoading=true;w->processDecodedOutput(OrdinaryBusyFixture::row("CQ K8BSY FN20","000115"));
  QVERIFY(w->ordinaryBusyWaiting());QVERIFY(!w->generalTxAllowed());
  w->m_logLoading=false;w->ordinaryBusyTick();QVERIFY(!w->ordinaryBusyWaiting());
  prepareOrdinaryBusy();
  w->processDecodedOutput(OrdinaryBusyFixture::row("K9OTH K8BSY -12"));
  w->m_qsoHistory.deferOutgoingCall("K8BSY");
  w->processDecodedOutput(OrdinaryBusyFixture::row("CQ K8BSY FN20","000115"));
  QVERIFY(w->ordinaryBusyWaiting());QVERIFY(!w->generalTxAllowed());
  w->processDecodedOutput(OrdinaryBusyFixture::row("N0CALL K8BSY -12","000145"));
  QVERIFY(!w->ordinaryBusyWaiting());QCOMPARE(w->m_ntx,3);QVERIFY(w->m_enableTx);
  prepareOrdinaryBusy();w->m_config.set_watchdog(1);
  w->processDecodedOutput(OrdinaryBusyFixture::row("K9OTH K8BSY -12"));
  w->m_txSession.start(TxWatchdog::Clock::now()-std::chrono::minutes(2));
  w->ordinaryBusyTick();QVERIFY(!w->m_enableTx && !w->m_manualTxAuthorized);
  QVERIFY(w->m_txwatchdog);QVERIFY(!w->ordinaryBusyWaiting());
  prepareOrdinaryBusy();
  w->processDecodedOutput(OrdinaryBusyFixture::row("K9OTH K8BSY -12"));
  QVERIFY(w->m_blacklist->save({{"K8BSY","busy test"}}));
  w->processDecodedOutput(OrdinaryBusyFixture::row("CQ K8BSY FN20","000115"));
  QVERIFY(!w->m_enableTx && !w->m_manualTxAuthorized);QVERIFY(!w->ordinaryBusyWaiting());QCOMPARE(g_iptt,0);
}
