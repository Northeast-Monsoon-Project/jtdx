// Include after RegressionTest. Completion itself is tested by SignoffState;
// this fixture supplies completed attempts without emitting PTT or RF.
void RegressionTest::managedSlotPolicyIntegration()
{
  managedFixtureTime(QTime(0,0,15,250)); // Both 00:00 and 00:15 invitations are current.
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  w->on_actionFT8_triggered();stopWaveAudio();w->m_diskData=false;w->m_manualDecode=false;
  w->m_houndMode=false;w->m_autoseq=false;w->m_autoTx=false;w->m_singleshot=false;
  w->m_freqNominal=14074000;w->m_TRperiod=15;
  auto previousSettings=w->m_config.managed_settings();
  auto previousPolicy=w->m_managedSlotPolicy;auto previousFirst=w->m_txFirst;
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&] {
    w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);
    saveManagedSettings(previousSettings);w->m_managedSlotPolicy=previousPolicy;w->assignTxSlot(previousFirst);
    w->clearDX("slot policy cleanup");w->m_qsoHistory.init();w->m_qsoHistory2.init();
    w->ui->decodedTextBrowser->clear();w->ui->decodedTextBrowser2->clear();
  }};
  TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  for(auto mode:{ManagedMode::DxccHunter,ManagedMode::QsoEnthusiast}) {
    ManagedSettings fixed;saveManagedSettings(fixed);
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_logRefreshTimer.isActive() && !w->m_logLoading
      && !w->m_logImportActive && !w->m_logRefreshPending && w->m_logReady,20000);
    w->m_logBook.init("N0CALL","","","ZZ9QA");w->m_qsoHistory.init();
    w->m_qsoHistory.owndata("NA","K","AA00",false);
    w->setManagedMode(mode);w->m_managedSlotPolicy=ManagedSlotPolicy{};w->assignTxSlot(false);
    manualEnable();QVERIFY(w->m_enableTx);
    managedDecode("CQ K8SLA FN20","000000",-20);managedDecode("CQ K8SLB FN20","000015",10);
    QCOMPARE(w->chooseManagedCandidate().call,QString("K8SLA")); // Stronger opposite-slot CQ is ineligible.
    w->process_Auto();QCOMPARE(w->m_hisCall,QString("K8SLA"));QVERIFY(!w->m_txFirst);
    w->on_stopTxButton_clicked();

    auto flexible=fixed;flexible.hunter.freeTxSlot=true;flexible.enthusiast.freeTxSlot=true;
    saveManagedSettings(flexible);
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_logRefreshTimer.isActive() && !w->m_logLoading
      && !w->m_logImportActive && !w->m_logRefreshPending && w->m_logReady,20000);
    w->m_logBook.init("N0CALL","","","ZZ9QA");w->m_qsoHistory.init();
    w->m_qsoHistory.owndata("NA","K","AA00",false);
    w->setManagedMode(mode);w->m_managedSlotPolicy=ManagedSlotPolicy{};w->assignTxSlot(false);
    manualEnable();QVERIFY(w->m_enableTx);
    managedDecode("CQ K8SLA FN20","000000",-20);managedDecode("CQ K8SLB FN20","000015",10);
    QCOMPARE(w->chooseManagedCandidate().call,QString("K8SLB"));
    w->process_Auto();QCOMPARE(w->m_hisCall,QString("K8SLB"));QVERIFY(w->m_txFirst);
    QVERIFY(w->m_managedSlotPolicy.remaining(w->m_managedClock.elapsed())>290000);
    // Complete the fixture's attempt; do not synthesize an immediate S73 finish.
    w->m_managed.complete(w->m_managedClock.elapsed());w->clearDX("synthetic completed slot attempt");
    w->process_Auto();QVERIFY(w->m_hisCall.isEmpty());QVERIFY(w->m_txFirst); // Opposite slot is cooling down.
    managedDecode("CQ K8SLC FN20","000015",-25);
    w->process_Auto();QCOMPARE(w->m_hisCall,QString("K8SLC"));QVERIFY(w->m_txFirst);
    w->m_managed.complete(w->m_managedClock.elapsed());w->clearDX("synthetic completed slot attempt");
    w->m_managedSlotPolicy.changed(w->m_managedClock.elapsed()-300000);
    managedDecode("CQ K8SLA FN20","000000",-20);
    w->process_Auto();QCOMPARE(w->m_hisCall,QString("K8SLA"));QVERIFY(!w->m_txFirst);

    // Actual human button gestures bypass the automatic cooldown, and renew
    // it so the scheduler cannot immediately undo the user's selected slot.
    QTest::mouseClick(w->ui->TxMinuteButton,Qt::LeftButton);
    QVERIFY(w->m_txFirst);QVERIFY(!w->m_manualTxSlotChange);
    QVERIFY(w->m_managedSlotPolicy.remaining(w->m_managedClock.elapsed())>290000);
    w->m_managedSlotPolicy.changed(w->m_managedClock.elapsed()-100000);
    w->ui->TxMinuteButton->click(); // Internal/remote activation is not a physical gesture.
    QVERIFY(!w->m_txFirst);
    QVERIFY(w->m_managedSlotPolicy.remaining(w->m_managedClock.elapsed())<210000);
    w->on_stopTxButton_clicked();
  }
  QCOMPARE(ptt.pttOnCount(),0);
}
