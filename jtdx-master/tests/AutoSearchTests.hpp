// Definitions for RegressionTest slots; include after the class declaration.
#include <QActionGroup>

void RegressionTest::autoPriorityIsExclusiveAndMigrates()
{
  w->on_stopTxButton_clicked();
  auto report=w->ui->actionReport_message_priority;
  auto distance=w->ui->actionMaxDistance;
  QVERIFY(report->actionGroup());
  QCOMPARE(report->actionGroup(),distance->actionGroup());
  QVERIFY(report->actionGroup()->isExclusive());
  QString distanceKey="Common/MaxDistance",reportKey="Common/ReportMessagePriority";
  auto oldDistance=settings.value(distanceKey),oldReport=settings.value(reportKey);
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&]{
    if(oldDistance.isValid())settings.setValue(distanceKey,oldDistance);else settings.remove(distanceKey);
    if(oldReport.isValid())settings.setValue(reportKey,oldReport);else settings.remove(reportKey);
    w->readSettings();w->on_stopTxButton_clicked();
  }};
  // A fresh installation and an old "neither checked" profile both choose
  // report priority. A deliberately chosen distance preference survives.
  for(int scenario=0;scenario<3;++scenario) {
    if(scenario==0){settings.remove(distanceKey);settings.remove(reportKey);}
    else {settings.setValue(distanceKey,scenario==2);settings.setValue(reportKey,false);}
    w->readSettings();
    QCOMPARE(report->isChecked(),scenario!=2);QCOMPARE(distance->isChecked(),scenario==2);
    QCOMPARE(w->m_rprtPriority,report->isChecked());QCOMPARE(w->m_maxDistance,distance->isChecked());
    w->writeSettings();
    QCOMPARE(settings.value(distanceKey).toBool(),scenario==2);
    QCOMPARE(settings.value(reportKey).toBool(),scenario!=2);
  }
  report->trigger();QVERIFY(report->isChecked());QVERIFY(!distance->isChecked());
  report->trigger();QVERIFY(report->isChecked());QVERIFY(!distance->isChecked());
  distance->trigger();QVERIFY(!report->isChecked());QVERIFY(distance->isChecked());
  distance->trigger();QVERIFY(!report->isChecked());QVERIFY(distance->isChecked());
  QVERIFY(!w->m_rprtPriority);QVERIFY(w->m_maxDistance);QVERIFY(!w->m_enableTx);
}

void RegressionTest::autoSearchIndependentOfHighlights_data()
{
  QTest::addColumn<QString>("message");QTest::addColumn<bool>("allOff");QTest::addColumn<bool>("distance");
  QTest::addColumn<bool>("worked");QTest::addColumn<bool>("allowWorked");QTest::addColumn<bool>("higherOnly");
  QTest::newRow("dxcc-only-report")<<QString("CQ K3CCC FN31")<<false<<false<<false<<false<<false;
  QTest::newRow("dxcc-only-distance")<<QString("CQ K3CCC FN31")<<false<<true<<false<<false<<false;
  QTest::newRow("no-highlights-report")<<QString("CQ K3CCC FN31")<<true<<false<<false<<false<<false;
  QTest::newRow("no-highlights-distance")<<QString("CQ K3CCC FN31")<<true<<true<<false<<false<<false;
  QTest::newRow("no-highlights-73")<<QString("K9ZZZ K3CCC 73")<<true<<false<<false<<false<<false;
  QTest::newRow("no-highlights-RR73")<<QString("K9ZZZ K3CCC RR73")<<true<<false<<false<<false<<false;
  QTest::newRow("no-highlights-RRR")<<QString("K9ZZZ K3CCC RRR")<<true<<false<<false<<false<<false;
  QTest::newRow("worked-not-allowed")<<QString("CQ K3CCC FN31")<<true<<false<<true<<false<<false;
  QTest::newRow("worked-allowed")<<QString("CQ K3CCC FN31")<<true<<false<<true<<true<<false;
  for(auto ending:{QString("73"),QString("RR73"),QString("RRR")}) {
    QTest::newRow(qPrintable("worked-"+ending+"-rejected"))<<QString("K9ZZZ K3CCC ")+ending<<true<<false<<true<<false<<false;
    QTest::newRow(qPrintable("worked-"+ending+"-allowed"))<<QString("K9ZZZ K3CCC ")+ending<<true<<false<<true<<true<<false;
  }
  QTest::newRow("higher-priority-filter")<<QString("CQ K3CCC FN31")<<false<<false<<false<<false<<true;
}

void RegressionTest::autoSearchIndependentOfHighlights()
{
  QFETCH(QString,message);QFETCH(bool,allOff);QFETCH(bool,distance);
  QFETCH(bool,worked);QFETCH(bool,allowWorked);QFETCH(bool,higherOnly);
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  QMap<QString,bool> previous,selected;
  for(auto kind:{"CQZ","ITUZ","DXCC","Grid","Px","Call"})
    for(auto suffix:{"","Band","BandMode"}) {
      QString name=QString("new")+kind+suffix+"_check_box";
      auto checkbox=dialog->findChild<QCheckBox*>(name);QVERIFY(checkbox);
      previous[name]=checkbox->isChecked();selected[name]=false;
    }
  auto workedHidden=dialog->findChild<QCheckBox*>("workedDontShow_check_box");QVERIFY(workedHidden);
  previous["workedDontShow_check_box"]=workedHidden->isChecked();selected["workedDontShow_check_box"]=false;
  selected["newDXCC_check_box"]=!allOff;
  auto configure=[&](QMap<QString,bool> values) {
    QTimer::singleShot(0,dialog,[dialog,values]{
      for(auto it=values.cbegin();it!=values.cend();++it)
        dialog->findChild<QCheckBox*>(it.key())->setChecked(it.value());
      QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });
    QCOMPARE(w->m_config.exec(),int(QDialog::Accepted));
    w->ui->decodedTextBrowser->setConfiguration(&w->m_config);
    w->ui->decodedTextBrowser2->setConfiguration(&w->m_config);
  };
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&]{
    w->on_stopTxButton_clicked();configure(previous);w->clearDX("test cleanup");
    w->m_autoseq=false;w->m_callHigherNewCall=false;w->m_callWorkedB4=false;
    w->ui->actionCallHigherNewCall->setChecked(false);w->ui->actionCallWorkedB4->setChecked(false);
    w->ui->actionReport_message_priority->setChecked(true);
  }};
  configure(selected);
  QTRY_VERIFY_WITH_TIMEOUT(!w->m_logRefreshTimer.isActive() && !w->m_logLoading
    && !w->m_logRefreshPending && !w->m_logImportActive && w->m_logReady,20000);
  w->m_mode=w->m_modeTx="FT8";w->m_TRperiod=15;w->m_freqNominal=14074000;
  w->m_diskData=false;w->m_manualDecode=false;w->m_decoderBusy=false;
  w->m_houndMode=false;w->m_singleshot=false;w->m_skipTx1=false;w->m_autoTx=false;w->m_autoseq=true;
  w->m_lockTxFreq=false;w->m_counter=0;w->m_processAuto_done=false;w->m_transmitting=false;g_iptt=0;
  w->ui->tabWidget->setCurrentIndex(0);
  w->ui->actionCallEnd->setChecked(true);w->ui->actionCallPriorityAndSearchCQ->setChecked(true);
  w->m_callMode=3;w->m_callPrioCQ=true;
  w->ui->actionCallWorkedB4->setChecked(allowWorked);w->m_callWorkedB4=allowWorked;
  w->ui->actionCallHigherNewCall->setChecked(higherOnly);w->m_callHigherNewCall=higherOnly;
  (distance?w->ui->actionMaxDistance:w->ui->actionReport_message_priority)->setChecked(true);
  w->m_qsoHistory.init();w->m_qsoHistory.owndata("NA","K","AA00",false);
  // Known DXCC, but K3CCC is a new station unless this row explicitly logs it.
  w->m_logBook.init("N0CALL","","","ZZ9AS",QDir(temp.path()+"/auto-search"));
  w->m_logBook.addAsWorked("K1AAA","20m","FT8","20261030","FN20","");
  if(worked)w->m_logBook.addAsWorked("K3CCC","20m","FT8","20261030","FN31","");
  w->clearDX("test setup");w->on_txb6_clicked();QCOMPARE(w->m_ntx,6);
  TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  manualEnable();QVERIFY(w->m_enableTx);QVERIFY(w->m_manualTxAuthorized);
  // Exercise the actual decoded-output path, not hand-built history ranks.
  w->processDecodedOutput(("000015 -10  0.1 1700 ~ "+message+"\n").toUtf8());
  w->process_Auto();
  bool expected=(!worked || allowWorked) && !higherOnly;
  QCOMPARE(w->m_hisCall,expected?QString("K3CCC"):QString());
  QCOMPARE(w->m_ntx,expected?1:6);
  QVERIFY(w->m_enableTx);QCOMPARE(g_iptt,0);
  QCOMPARE(ptt.pttOnCount(),0);
}

void RegressionTest::ownFinalMessagesDoNotRestartQso_data()
{
  QTest::addColumn<QString>("ending");QTest::addColumn<int>("received");
  QTest::newRow("73")<<QString("73")<<int(QsoHistory::R73);
  QTest::newRow("RR73")<<QString("RR73")<<int(QsoHistory::RRR73);
  QTest::newRow("RRR")<<QString("RRR")<<int(QsoHistory::RRR);
}

void RegressionTest::ownFinalMessagesDoNotRestartQso()
{
  QFETCH(QString,ending);QFETCH(int,received);
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  QTRY_VERIFY_WITH_TIMEOUT(!w->m_logRefreshTimer.isActive() && !w->m_logLoading
    && !w->m_logRefreshPending && !w->m_logImportActive && w->m_logReady,20000);
  w->m_mode=w->m_modeTx="FT8";w->m_TRperiod=15;w->m_freqNominal=14074000;
  w->m_diskData=false;w->m_manualDecode=false;w->m_decoderBusy=false;
  w->m_houndMode=false;w->m_singleshot=false;w->m_skipTx1=false;w->m_autoTx=false;w->m_autoseq=true;
  w->m_callMode=3;w->m_callPrioCQ=true;w->m_answerWorkedB4=true;w->m_callWorkedB4=true;
  w->m_callHigherNewCall=false;w->m_lockTxFreq=false;w->m_counter=0;w->m_processAuto_done=false;
  w->m_transmitting=false;g_iptt=0;w->ui->tabWidget->setCurrentIndex(0);
  w->ui->actionReport_message_priority->setChecked(true);
  w->m_qsoHistory.init();w->m_qsoHistory.owndata("NA","K","AA00",false);
  w->m_logBook.init("N0CALL","","","ZZ9AS",QDir(temp.path()+"/own-final"));
  w->ui->dxCallEntry->setText("K3CCC");w->ui->dxGridEntry->setText("FN31");
  w->m_qsoHistory.message("K3CCC",QsoHistory::RCQ,5,"FN31","","NA","K",0,"-10",1700,"FT8");
  w->m_qsoHistory.message("K3CCC",QsoHistory::SREPORT,5,"-10","","NA","K",0,"-10",1700,"FT8");
  w->m_qsoHistory.message("K3CCC",QsoHistory::RREPORT,5,"-12","","NA","K",0,"-10",1700,"FT8");
  w->m_status=QsoHistory::RRREPORT;w->genStdMsgs("-10");w->on_txb3_clicked();
  TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  manualEnable();QVERIFY(w->m_enableTx);
  w->processDecodedOutput(("000015 -10  0.1 1700 ~ N0CALL K3CCC "+ending+"\n").toUtf8());
  w->process_Auto();
  QString grid;QCOMPARE(w->m_qsoHistory.status("K3CCC",grid),QsoHistory::Status(received));
  QCOMPARE(w->m_hisCall,QString("K3CCC"));QCOMPARE(w->m_ntx,5);
  // Complete the active contact, then receive the same acknowledgement twice.
  // A different station's fresh CQ makes the selection pass active; it must
  // not make the completed contact's repeated RR73 eligible as an incoming call.
  w->m_qsoHistory.message("K3CCC",QsoHistory::S73,0,"","","NA","K",30,"-10",1700,"FT8");
  w->m_qsoHistory.complete("K3CCC"); // The protocol-completion tests cover the handshake.
  w->process_Auto();QVERIFY(w->m_hisCall.isEmpty());
  for(int repeat=0;repeat<2;++repeat)
    w->processDecodedOutput(("000045 -10  0.1 1700 ~ N0CALL K3CCC "+ending+"\n").toUtf8());
  QCOMPARE(w->m_qsoHistory.status("K3CCC",grid),QsoHistory::FIN);
  w->processDecodedOutput("000045 -12  0.1 1800 ~ CQ K2BBB FN20\n");
  w->process_Auto();QCOMPARE(w->m_hisCall,QString("K2BBB"));QCOMPARE(w->m_ntx,1);
  QCOMPARE(g_iptt,0);QCOMPARE(ptt.pttOnCount(),0);
  w->on_stopTxButton_clicked();w->clearDX("test cleanup");
  w->m_autoseq=false;w->m_answerWorkedB4=false;w->m_callWorkedB4=false;
}
