void RegressionTest::managedCandidateUsesDecodeUtc_data()
{
  QTest::addColumn<int>("mode");QTest::addColumn<QString>("protocol");QTest::addColumn<bool>("midnight");
  for(auto mode:{ManagedMode::DxccHunter,ManagedMode::QsoEnthusiast})
    for(auto protocol:{QString("FT8"),QString("FT4"),QString("FT2")})for(bool midnight:{false,true})
      QTest::newRow(qPrintable(QString("mode%1-%2-midnight%3").arg(int(mode)).arg(protocol).arg(midnight)))
        <<int(mode)<<protocol<<midnight;
}

void RegressionTest::managedCandidateUsesDecodeUtc()
{
  QFETCH(int,mode);QFETCH(QString,protocol);QFETCH(bool,midnight);
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  auto previous=w->m_config.managed_settings();auto profiles=previous;
  profiles.hunter.criteria=NewGrid;profiles.enthusiast.criteria=NewCall;
  profiles.hunter.freeTxSlot=profiles.enthusiast.freeTxSlot=true;
  saveManagedSettings(profiles);auto policy=w->m_managedSlotPolicy;auto wasVisible=w->isVisible();
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&]{
    w->m_decoderBusy=false;w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);
    w->killFileTimer.stop();saveManagedSettings(previous);w->m_managedSlotPolicy=policy;
    w->clearDX("candidate UTC cleanup");w->m_singleshot=false;w->m_mode=w->m_modeTx="FT8";w->m_TRperiod=15;
    if(!wasVisible)w->hide();
  }};
  QTRY_VERIFY_WITH_TIMEOUT(w->m_logReady && !w->m_logLoading && !w->m_logRefreshPending
    && !w->m_logImportActive && !w->m_logRefreshTimer.isActive(),20000);
  w->m_managed=ManagedSession{};prepareManaged(ManagedMode(mode));
  w->m_mode=w->m_modeTx=protocol;w->m_TRperiod=protocol=="FT8"?15:protocol=="FT4"?7.5:3.75;
  w->setManagedMode(ManagedMode(mode));manualEnable();w->m_managedSlotPolicy=ManagedSlotPolicy{};
  const qint64 period=qRound64(w->m_TRperiod*1000);
  auto day=w->m_jtdxtime->currentDateTimeUtc2();day.setTime(midnight?QTime(0,0):QTime(9,0));
  qint64 base=day.toMSecsSinceEpoch();
  auto clock=[&](qint64 stamp){w->m_jtdxtime->SetOffset(w->m_jtdxtime->GetOffset()
    +(stamp-w->m_jtdxtime->currentMSecsSinceEpoch2())/1000.0);};
  auto row=[&](qint64 slot,QString message){
    return QDateTime::fromMSecsSinceEpoch(slot,Qt::UTC).toString("hhmmss")
      +" -10  0.1 1700 "+(protocol=="FT8"?QString("~"):QString("+"))+" "+message;
  };
  auto decode=[&](qint64 slot,QString message){w->processDecodedOutput((row(slot,message)+"\n").toUtf8());};
  clock(base+250);w->m_decoderBusy=true;
  TransceiverCommandSpy commands(w.get());QVERIFY(commands.isValid());
  for(int age:{3,4})decode(base-age*period,QString("CQ K%1OLD FN31").arg(age));
  // Both receive slots are eligible under free-slot mode; UTC freshness,
  // rather than opposite-slot filtering, must reject these late decodes.
  QVERIFY(w->chooseManagedCandidate().call.isEmpty());w->process_Auto();QVERIFY(w->m_hisCall.isEmpty());
  decode(base-period,"CQ K8NEW FN31");QCOMPARE(w->chooseManagedCandidate().call,QString("K8NEW"));
  decode(base,"K9OTH K8NEW -12");
  decode(base-period,"CQ K8NEW FN31"); // Older CQ arrives after the newer third-party report.
  QVERIFY(w->chooseManagedCandidate().call.isEmpty());
  w->process_Auto();QVERIFY(w->m_hisCall.isEmpty());
  decode(base-period,"CQ K8FRE FN31");QCOMPARE(w->chooseManagedCandidate().call,QString("K8FRE"));
  clock(base+period+250);decode(base-period,"CQ K8FRE FN31");
  QVERIFY(w->chooseManagedCandidate().call.isEmpty()); // Same UTC row cannot renew its lifetime.
  decode(base,"CQ K8CUR FN31");
  // Complete this receive batch explicitly; this fixture does not run the
  // GUI TX window and must not inherit m_bTxTime from another test.
  w->m_msDecStarted=base;w->m_dataAvailable=true;
  w->processDecodedOutput("<DecodeFinished>    1\n");w->process_Auto();
  QCOMPARE(w->m_hisCall,QString("K8CUR"));QCOMPARE(w->m_managed.current(),w->m_hisCall);
  clock(base+5*period+250);w->process_Auto();
  QCOMPARE(w->m_hisCall,QString("K8CUR"));QVERIFY(w->managedTxAllowed()); // Current QSO stays in progress.
  // Human selection is deliberate and does not inherit automatic CQ expiry.
  w->show();w->ui->tabWidget->setCurrentIndex(0);QCoreApplication::processEvents();
  DoubleClickTxFixture::click(w->ui->decodedTextBrowser,row(base-3*period,"CQ K8MAN FN31"));
  QCOMPARE(w->m_hisCall,QString("K8MAN"));QCOMPARE(w->m_managed.current(),w->m_hisCall);
  w->process_Auto();QCOMPARE(w->m_hisCall,QString("K8MAN"));QVERIFY(w->managedTxAllowed());
  QCOMPARE(commands.pttOnCount(),0);
}
