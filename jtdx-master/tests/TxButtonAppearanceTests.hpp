// Include after the RegressionTest declaration; no audio or CAT is started.
void RegressionTest::txEnableButtonShowsSessionAndOnAir()
{
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  w->on_actionFT8_triggered();stopWaveAudio();w->m_diskData=false;
  QTRY_VERIFY_WITH_TIMEOUT(!w->m_logRefreshTimer.isActive() && !w->m_logLoading
    && !w->m_logRefreshPending && !w->m_logImportActive && w->m_logReady,20000);
  auto priorDark=w->m_useDarkStyle;
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&] {
    w->m_transmitting=false;w->m_tx_when_ready=false;
    w->on_stopTxButton_clicked();w->m_useDarkStyle=priorDark;w->styleChanged();
  }};
  for(bool dark:{false,true}) {
    w->m_useDarkStyle=dark;w->on_stopTxButton_clicked();w->clearDX("color test");
    auto background=[&](char const* color) {
      return w->ui->enableTxButton->styleSheet().contains("background: "+Radio::convert_dark(color,dark)+";");
    };
    QVERIFY(background("#dcdcdc"));
    manualEnable();QVERIFY(w->m_enableTx);QVERIFY(background("#ff3c3c"));
    w->m_transmitting=true;w->m_tx_when_ready=true;w->transmitDisplay(true);
    QVERIFY(background("#ff3c3c")); // PTT preparation is not audio transmission yet.
    // CAT acknowledgement clears the ready flag before the configured audio
    // delay expires. The normal GUI tick must update the indicator itself.
    w->m_tx_when_ready=false;w->ptt1Timer.start(60000);w->generalTick();
    QVERIFY(background("#ff3c3c"));
    w->ptt1Timer.stop();w->generalTick();QVERIFY(background("#00ff00"));
    w->styleChanged();QVERIFY(background("#00ff00"));
    w->updateGeneralButtonColors();QVERIFY(background("#00ff00"));
    w->m_transmitting=false;w->transmitDisplay(false);
    QVERIFY(w->m_enableTx);QVERIFY(background("#ff3c3c"));
    w->on_stopTxButton_clicked();QVERIFY(!w->m_enableTx);QVERIFY(background("#dcdcdc"));
  }
}
