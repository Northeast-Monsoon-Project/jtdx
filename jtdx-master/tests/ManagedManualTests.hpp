// Real left-button gestures with simulated radio state; no RF or audio starts.
void RegressionTest::managedManualSelection_data()
{
  QTest::addColumn<int>("mode");QTest::addColumn<bool>("rightPane");QTest::addColumn<bool>("quick");
  for(auto mode:{ManagedMode::DxccHunter,ManagedMode::QsoEnthusiast})
    for(bool right:{false,true})for(bool quick:{false,true})
      QTest::newRow(qPrintable(QString("mode%1-right%2-quick%3").arg(int(mode)).arg(right).arg(quick)))
        <<int(mode)<<right<<quick;
}

void RegressionTest::managedManualSelection()
{
  QFETCH(int,mode);QFETCH(bool,rightPane);QFETCH(bool,quick);
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  w->on_actionFT8_triggered();stopWaveAudio();w->m_managed=ManagedSession{};
  auto previous=w->m_config.managed_settings();auto previousQuick=w->m_config.quick_call();
  auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  auto wasVisible=w->isVisible();auto blacklist=w->m_blacklist->entries();
  auto configure=[&](bool enabled,ManagedSettings const& profiles) {
    QTimer::singleShot(0,dialog,[=] {
      dialog->findChild<QCheckBox*>("quick_call_check_box")->setChecked(enabled);
      dialog->findChild<ManagedSettingsWidget*>("managed_tab")->setSettings(profiles);
      QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });
    QCOMPARE(w->m_config.exec(),int(QDialog::Accepted));w->applyGeneralSettings();
  };
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&] {
    w->ptt0Timer.stop();w->ptt1Timer.stop();w->m_rigState.ptt(false);
    w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);
    configure(previousQuick,previous);w->m_blacklist->save(blacklist);
    w->clearDX("managed manual selection cleanup");w->m_qsoHistory.init();w->m_qsoHistory2.init();
    w->m_singleshot=false;w->ui->decodedTextBrowser->clear();w->ui->decodedTextBrowser2->clear();
    if(!wasVisible)w->hide();
  }};
  auto profiles=previous;
  profiles.hunter.criteria=profiles.enthusiast.criteria=0;
  profiles.hunter.answerWorked=profiles.enthusiast.answerWorked=false;
  profiles.hunter.freeTxSlot=profiles.enthusiast.freeTxSlot=false;
  profiles.hunter.callLimit=profiles.enthusiast.callLimit=3;
  configure(quick,profiles);QCOMPARE(w->m_config.quick_call(),quick);
  w->show();QCoreApplication::processEvents();
  QTRY_VERIFY_WITH_TIMEOUT(w->m_logReady && !w->m_logLoading && !w->m_logRefreshPending
    && !w->m_logImportActive && !w->m_logRefreshTimer.isActive(),20000);
  prepareManaged(ManagedMode(mode));QVERIFY(w->m_enableTx && w->m_manualTxAuthorized);
  w->ui->tabWidget->setCurrentIndex(0);w->m_lockTxFreq=false;
  w->m_logBook.addAsWorked("K8MNA","20m","FT8","20261030","FN31","");
  w->m_logBook.addAsWorked("K8MNB","20m","FT8","20261030","FN31","");
  auto pane=rightPane?w->ui->decodedTextBrowser2:w->ui->decodedTextBrowser;
  QString first="000015 -10  0.0 1500 ~ CQ K8MNA FN31";
  QString second="000000 -12  0.0 1700 ~ CQ K8MNB FN31";
  TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  w->m_txSession.start(TxWatchdog::Clock::now()-std::chrono::seconds(2));
  DoubleClickTxFixture::click(pane,first);
  QCOMPARE(w->m_hisCall,QString("K8MNA"));QCOMPARE(w->m_managed.current(),w->m_hisCall);
  QVERIFY(w->m_managedTxReady && w->managedTxAllowed());QVERIFY(w->m_txFirst);
  QVERIFY(w->m_txSession.elapsedMs()>=1900);
  // Incoming/automatic decisions must not erase or replace this explicit
  // target even though it fails every configured novelty/worked criterion.
  managedDecode("N0CALL K8MNC FN31");w->process_Auto();
  QCOMPARE(w->m_hisCall,QString("K8MNA"));QVERIFY(w->managedTxAllowed());
  w->m_managed.transmitted("K8MNA N0CALL AA00");
  DoubleClickTxFixture::click(pane,first);QCOMPARE(w->m_managed.transmissions(),1);
  // PTT preparation/release and the rig's own PTT report are still part of
  // the existing send, so a manual replacement cannot cut that waveform.
  for(auto timer:{&w->ptt0Timer,&w->ptt1Timer}) {
    timer->start(60000);DoubleClickTxFixture::click(pane,second);timer->stop();
    QCOMPARE(w->m_hisCall,QString("K8MNA"));QCOMPARE(w->m_managed.transmissions(),1);
  }
  w->m_rigState.ptt(true);DoubleClickTxFixture::click(pane,second);w->m_rigState.ptt(false);
  QCOMPARE(w->m_hisCall,QString("K8MNA"));
  // A real gesture can change TX slots without the automatic five-minute
  // cooldown, and replaces the attempt only once it is safe to do so.
  DoubleClickTxFixture::click(pane,second);
  QCOMPARE(w->m_hisCall,QString("K8MNB"));QCOMPARE(w->m_managed.current(),w->m_hisCall);
  QCOMPARE(w->m_managed.transmissions(),0);QVERIFY(!w->m_txFirst);
  w->process_Auto();QCOMPARE(w->m_hisCall,QString("K8MNB"));QVERIFY(w->managedTxAllowed());
  QVERIFY(w->m_txSession.elapsedMs()>=1900);
  QVERIFY(w->m_blacklist->save({{"K8MND","Synthetic managed manual selection"}}));
  DoubleClickTxFixture::click(pane,"000000 -10  0.0 1700 ~ CQ K8MND FN31");
  QCOMPARE(w->m_hisCall,QString("K8MNB"));
  if(!quick) {
    w->on_stopTxButton_clicked();DoubleClickTxFixture::click(pane,first);
    QVERIFY(!w->m_enableTx && !w->m_manualTxAuthorized && !w->m_txSession.active());
    QVERIFY(w->m_managed.current().isEmpty());
  }
  QCOMPARE(ptt.pttOnCount(),0);
}
