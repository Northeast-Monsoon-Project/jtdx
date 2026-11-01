// Include after the RegressionTest declaration. All PTT remains simulated.
#include <QScopedValueRollback>
namespace DoubleClickTxFixture {
QPoint putRow(DisplayText* view,QString const& row)
{
  view->setPlainText(row+"\n");
  QTextCursor cursor(view->document());
  auto start=row.indexOf("CQ ");
  cursor.setPosition(start<0?0:start+3);
  view->setTextCursor(cursor);view->ensureCursorVisible();
  return view->cursorRect(cursor).center();
}
void click(DisplayText* view,QString const& row,Qt::KeyboardModifiers modifiers=Qt::NoModifier,
           Qt::MouseButton button=Qt::LeftButton)
{
  auto point=putRow(view,row);
  QVERIFY(view->viewport()->rect().contains(point));
  QTest::mouseDClick(view->viewport(),button,modifiers,point);
  QVERIFY(!view->callSelectionInProgress());
}
}

void RegressionTest::doubleClickTxManualGesture()
{
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  w->on_actionFT8_triggered();stopWaveAudio();w->m_diskData=false;w->m_manualDecode=false;
  w->m_houndMode=false;w->m_autoseq=false;w->m_autoTx=false;w->m_singleshot=false;
  w->m_lockTxFreq=false;w->m_freqNominal=14074000;
  auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  auto previous=w->m_config.quick_call();auto wasVisible=w->isVisible();
  auto configure=[&](bool enabled) {
    QTimer::singleShot(0,dialog,[=] {
      dialog->findChild<QCheckBox*>("quick_call_check_box")->setChecked(enabled);
      QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });
    QCOMPARE(w->m_config.exec(),int(QDialog::Accepted));w->applyGeneralSettings();
  };
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&] {
    w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);
    configure(previous);w->m_singleshot=false;w->m_autoTx=false;w->m_autoseq=false;
    w->clearDX("double click test cleanup");w->m_qsoHistory.init();w->m_qsoHistory2.init();
    w->ui->decodedTextBrowser->clear();w->ui->decodedTextBrowser2->clear();
    if(!wasVisible)w->hide();
  }};
  TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  w->show();QCoreApplication::processEvents();
  QTRY_VERIFY_WITH_TIMEOUT(!w->m_logRefreshTimer.isActive() && !w->m_logLoading
    && !w->m_logImportActive && !w->m_logRefreshPending && w->m_logReady,20000);
  // Defaults and migration are covered by GeneralSettingsTests; this gesture
  // regression must also run independently with either saved preference.
  QVERIFY(!w->m_guiTimer.isActive());
  const QString row="000015 -10  0.0 1500 ~ CQ K8DCL FN20";
  configure(false);
  for(auto pane:{w->ui->decodedTextBrowser,w->ui->decodedTextBrowser2}) {
    w->clearDX("disabled double click");DoubleClickTxFixture::click(pane,row);
    QCOMPARE(w->m_hisCall,QString("K8DCL"));
    QVERIFY(!w->m_enableTx && !w->m_manualTxAuthorized && !w->m_txSession.active());
  }
  configure(true);QVERIFY(w->m_config.quick_call());
  QTRY_VERIFY_WITH_TIMEOUT(!w->m_logRefreshTimer.isActive() && !w->m_logLoading
    && !w->m_logImportActive && !w->m_logRefreshPending && w->m_logReady,20000);
  for(auto pane:{w->ui->decodedTextBrowser,w->ui->decodedTextBrowser2}) {
    w->on_stopTxButton_clicked();w->clearDX("enabled double click");
    w->m_qsoHistory.init();w->m_qsoHistory.owndata("NA","K","AA00",false);
    DoubleClickTxFixture::click(pane,row);
    QCOMPARE(w->m_hisCall,QString("K8DCL"));
    QVERIFY(w->m_enableTx && w->m_manualTxAuthorized && w->m_txSession.active());
    QVERIFY(w->ui->enableTxButton->isChecked());QCOMPARE(g_iptt,0);
  }

  // The gesture establishes a normal session without overriding Single QSO.
  // Actual RR73/73 completion is covered by the shared protocol lifecycle tests.
  for(bool single:{false,true}) {
    w->on_stopTxButton_clicked();w->m_singleshot=single;w->clearDX("completion setup");
    w->m_qsoHistory.init();w->m_qsoHistory.owndata("NA","K","AA00",false);
    DoubleClickTxFixture::click(w->ui->decodedTextBrowser,row);
    QVERIFY(w->m_enableTx);QCOMPARE(w->m_singleshot,single);
    QVERIFY(w->m_manualTxAuthorized && w->m_txSession.active());
    if(!single) {
      w->m_txSession.start(TxWatchdog::Clock::now()-std::chrono::seconds(1));
      DoubleClickTxFixture::click(w->ui->decodedTextBrowser2,"000045 -12  0.0 1700 ~ CQ K8DCM FN31");
      QCOMPARE(w->m_hisCall,QString("K8DCM"));QVERIFY(w->m_enableTx);
      QVERIFY(w->m_txSession.elapsedMs()>=900); // Selection cannot reset the regulator.
    }
  }
  w->on_stopTxButton_clicked();w->m_singleshot=false;
  for(auto mode:{ManagedMode::DxccHunter,ManagedMode::QsoEnthusiast}) {
    w->setManagedMode(mode);w->m_qsoHistory.init();w->m_qsoHistory.owndata("NA","K","AA00",false);
    // The right pane can retain an old received row after the original QSO ended.
    w->m_qsoHistory.message("K8DCL",QsoHistory::S73,0,"","","NA","K",30,"-10",1500,"FT8");
    w->m_qsoHistory.complete("K8DCL");
    DoubleClickTxFixture::click(w->ui->decodedTextBrowser2,row);
    QCOMPARE(w->m_hisCall,QString("K8DCL"));QCOMPARE(w->m_managed.current(),QString("K8DCL"));
    QVERIFY(w->m_managedTxReady && w->m_enableTx && w->m_manualTxAuthorized);
    QVERIFY(w->managedTxAllowed());QVERIFY(w->m_txSession.active());
    w->process_Auto();QCOMPARE(w->m_hisCall,QString("K8DCL"));QVERIFY(w->m_managedTxReady);
    w->m_managed.transmitted();w->m_managed.transmitted();QVERIFY(w->m_managed.exhausted(2));
    DoubleClickTxFixture::click(w->ui->decodedTextBrowser2,row);
    QVERIFY(w->m_managed.exhausted(2)); // Reclicking cannot restart the attempt budget.
    ManagedCandidate reply;reply.call="K8DCL";reply.grid="FN20";reply.mode="FT8";
    reply.incoming=true;reply.available=true;reply.responseStage=2;reply.seen=w->m_managedClock.elapsed();
    reply.utcSeconds=45; // This response follows the selected 00:15 CQ.
    w->m_managed.observe(reply);QVERIFY(!w->m_managed.exhausted(2));
    DoubleClickTxFixture::click(w->ui->decodedTextBrowser2,row);
    w->m_managed.transmitted("K8DCL N0CALL R-10");QVERIFY(!w->m_managed.exhausted(2));
    w->m_managed.transmitted("K8DCL N0CALL R-10");QVERIFY(w->m_managed.exhausted(2));
    // Progress gives the next message its own budget, not a permanent retry
    // exemption. Repeated replies and selecting the same active call cannot
    // replenish the report budget either.
    w->m_managed.observe(reply);QVERIFY(w->m_managed.exhausted(2));
    DoubleClickTxFixture::click(w->ui->decodedTextBrowser2,row);
    QVERIFY(w->m_managed.exhausted(2));
    for(int phase=0;phase<5;++phase) {
      QScopedValueRollback<bool> transmitting(w->m_transmitting,phase==0);
      QScopedValueRollback<qint32> pttState(g_iptt,phase==1?1:0);
      QScopedValueRollback<bool> pending(w->m_tx_when_ready,phase==2);
      QScopedValueRollback<bool> callOnAir(w->m_managedCallOnAir,phase==3);
      QScopedValueRollback<bool> cqOnAir(w->m_managedCqOnAir,phase==4);
      DoubleClickTxFixture::click(w->ui->decodedTextBrowser,"000045 -12  0.0 1700 ~ CQ K8DCM FN31");
      QCOMPARE(w->m_hisCall,QString("K8DCL"));QCOMPARE(w->m_managed.current(),QString("K8DCL"));
    }
    w->on_stopTxButton_clicked();
  }
  QCOMPARE(ptt.pttOnCount(),0);
}

void RegressionTest::doubleClickTxRejectsUnsafeSelections()
{
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  w->on_actionFT8_triggered();stopWaveAudio();w->m_diskData=false;w->m_manualDecode=false;
  w->m_houndMode=false;w->m_autoseq=false;w->m_autoTx=true;w->m_singleshot=false;w->m_lockTxFreq=false;
  auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  const bool previous=w->m_config.quick_call(),previousUdp=w->m_config.accept_udp_requests();
  auto previousAccept=w->m_acceptUDP;auto wasVisible=w->isVisible();
  QVERIFY(w->m_blacklist && w->m_blacklist->ready());auto blacklist=w->m_blacklist->entries();
  auto configure=[&](bool enabled,bool udp) {
    QTimer::singleShot(0,dialog,[=] {
      dialog->findChild<QCheckBox*>("quick_call_check_box")->setChecked(enabled);
      dialog->findChild<QCheckBox*>("accept_udp_requests_check_box")->setChecked(udp);
      QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });
    QCOMPARE(w->m_config.exec(),int(QDialog::Accepted));w->applyGeneralSettings();
  };
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&] {
    w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);
    configure(previous,previousUdp);w->m_acceptUDP=previousAccept;w->m_blacklist->save(blacklist);
    w->m_autoTx=false;w->m_autoseq=false;w->clearDX("unsafe double click cleanup");
    w->m_qsoHistory.init();w->m_qsoHistory2.init();
    w->ui->decodedTextBrowser->clear();w->ui->decodedTextBrowser2->clear();
    if(!wasVisible)w->hide();
  }};
  configure(true,true);w->m_acceptUDP=3;QVERIFY(w->m_config.quick_call());QVERIFY(w->m_config.accept_udp_requests());
  w->show();QCoreApplication::processEvents();
  QVERIFY(w->m_blacklist->save({}));
  QTRY_VERIFY_WITH_TIMEOUT(!w->m_logRefreshTimer.isActive() && !w->m_logLoading
    && !w->m_logImportActive && !w->m_logRefreshPending && w->m_logReady,20000);
  TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
  const QString row="000015 -10  0.0 1500 ~ CQ K8DCL FN20";
  auto stopped=[&] {
    QVERIFY(!w->m_enableTx && !w->m_manualTxAuthorized && !w->m_txSession.active());
    QVERIFY(!w->ui->enableTxButton->isChecked());QCOMPARE(g_iptt,0);
  };
  // Selection from internal or network callbacks never constitutes a mouse gesture.
  DoubleClickTxFixture::putRow(w->ui->decodedTextBrowser,row);
  w->ui->decodedTextBrowser->selectCallsign(false,false);stopped();
  QCOMPARE(w->m_hisCall,QString("K8DCL"));
  w->quickCallFromSelection(row,false,false);stopped();
  w->clearDX("UDP must select without authorizing");
  w->postDecode(true,row);
  w->replyToUDP(QTime(0,0,15),-10,0.0,1500,"~","CQ K8DCL FN20",false,0);
  QCOMPARE(w->m_hisCall,QString("K8DCL"));stopped();
  for(auto modifiers:{Qt::KeyboardModifiers(Qt::ControlModifier),Qt::KeyboardModifiers(Qt::AltModifier)}) {
    DoubleClickTxFixture::click(w->ui->decodedTextBrowser,row,modifiers);stopped();
  }
  DoubleClickTxFixture::click(w->ui->decodedTextBrowser,row,Qt::NoModifier,Qt::RightButton);stopped();
  // Leave an old valid target selected: rejected text must not arm that target.
  for(auto rejected:{QString("000015 -10  0.0 1500 ~ CQ N0CALL AA00"),
      QString("000015 -10  0.0 1500 ~ CQ N0CALL/P AA00"),
      QString("000015  Tx      1500 ~ CQ N0CALL AA00"),
      QString("000015 -10  0.0 1500 ~ INVALID TEXT"),row.leftJustified(49,' ')+"?"}) {
    w->ui->dxCallEntry->setText("K8DCL");
    DoubleClickTxFixture::click(w->ui->decodedTextBrowser,rejected);stopped();
  }
  for(int gate=0;gate<4;++gate) {
    QScopedValueRollback<bool> loading(w->m_logLoading,gate==0);
    QScopedValueRollback<bool> importing(w->m_logImportActive,gate==1);
    QScopedValueRollback<bool> pending(w->m_logRefreshPending,gate==2);
    QScopedValueRollback<bool> ready(w->m_logReady,gate!=3);
    DoubleClickTxFixture::click(w->ui->decodedTextBrowser2,row);stopped();
  }
  for(bool disk:{false,true}) {
    QScopedValueRollback<bool> fromFile(w->m_diskData,disk);
    QScopedValueRollback<bool> manualDecode(w->m_manualDecode,!disk);
    DoubleClickTxFixture::click(w->ui->decodedTextBrowser,row);stopped();
  }
  QVERIFY(w->m_blacklist->save({{"K8DCL","Synthetic double-click test"}}));
  DoubleClickTxFixture::click(w->ui->decodedTextBrowser,row);stopped();
  QVERIFY(w->m_blacklist->save({}));
  QTRY_VERIFY_WITH_TIMEOUT(!w->m_logRefreshTimer.isActive() && !w->m_logLoading
    && !w->m_logImportActive && !w->m_logRefreshPending && w->m_logReady,20000);
  DoubleClickTxFixture::click(w->ui->decodedTextBrowser,row);QVERIFY(w->m_enableTx && w->m_manualTxAuthorized);
  QMetaObject::invokeMethod(w->ui->decodedTextBrowser,"selectCallsign",Qt::QueuedConnection,
    Q_ARG(bool,false),Q_ARG(bool,false));
  QTimer::singleShot(0,w.get(),[&,row]{w->quickCallFromSelection(row,false,false);w->TxAgain();});
  w->on_stopTxButton_clicked();QCoreApplication::processEvents();stopped();
  QCOMPARE(ptt.pttOnCount(),0);
}
