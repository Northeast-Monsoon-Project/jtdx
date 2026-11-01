#include <QtTest>
#include <QApplication>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QSharedMemory>
#include <QSettings>
#include <QTemporaryDir>
#include <QNetworkAccessManager>
#include <QProcessEnvironment>
#include <QBuffer>
#include <QDataStream>
#include <QTcpSocket>
#include <QTcpServer>
#include <future>
#include <functional>
#include <thread>
#include "HRDTransceiver.hpp"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "about.h"
#include "getfile.h"
#include "McpServer.hpp"
#include "TxWatchdog.hpp"
#include "MetaDataRegistry.hpp"
#include "revision_utils.hpp"
#include "logbook/adif.h"
#include "OperatorLog.hpp"
#include "StationProfiles.hpp"
#include "NtpSettings.hpp"
#include "ManagedSettings.hpp"
#include "displaytext.h"
#include "decodedtext.h"
#include "soundin.h"
#include "soundout.h"
#include "JttyController.hpp"
#include "Modulator.hpp"
#include "Detector.hpp"
#include "TCITransceiver.hpp"
#include "TransceiverCommandSpy.hpp"
#include "Audio/BWFFile.hpp"
#include "LogForwarder.hpp"
#include "logqso.h"
#include "widegraph.h"
#include <QCryptographicHash>
#include <QLockFile>
extern dec_data_t dec_data;
extern qint32 g_iptt;
extern "C" {
  void genft2_(char*,int*,char*,signed char*,int*,size_t,size_t);
  void gen_ft2wave_(int*,int*,int*,float*,float*,float*,float*,int*,int*);
}

class RegressionTest : public QObject {
  Q_OBJECT
  QTemporaryDir temp;
  QSettings settings {temp.path()+"/test.ini", QSettings::IniFormat};
  QSharedMemory shared;
  QNetworkAccessManager network;
  std::unique_ptr<MainWindow> w;
  bool managedFixtureClockActive=false;
  double managedFixtureOffset=0;
  void managedFixtureTime(QTime time=QTime(0,0,30,250)) {
    if(!managedFixtureClockActive) {
      managedFixtureOffset=w->m_jtdxtime->GetOffset();managedFixtureClockActive=true;
    }
    auto now=w->m_jtdxtime->currentDateTimeUtc2(),target=now;target.setTime(time);
    w->m_jtdxtime->SetOffset(w->m_jtdxtime->GetOffset()+now.msecsTo(target)/1000.0);
  }
  void prepareOrdinaryBusy(bool automatic=true,int sent=1);
  void manualEnable() {
    if (!w->ui->enableTxButton->isChecked()) QTest::mouseClick(w->ui->enableTxButton,Qt::LeftButton);
  }
  void stopWaveAudio() {
    // The File/Open action suspends live capture before loading a WAV.
    // These direct test calls must honor the same precondition and drain
    // queued capture callbacks before reusing the shared sample buffer.
    w->on_monitorButton_clicked(false);
    QMetaObject::invokeMethod(w->m_soundInput,[]{},Qt::BlockingQueuedConnection);
    QCoreApplication::processEvents();
    QVERIFY(!w->m_monitoring);
  }
  void saveManagedSettings(ManagedSettings const& selected,bool hide=false) {
    auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
    QTimer::singleShot(0,dialog,[dialog,selected,hide]{
      auto page=dialog->findChild<ManagedSettingsWidget*>("managed_tab");QVERIFY(page);page->setSettings(selected);
      dialog->findChild<QCheckBox*>("workedDontShow_check_box")->setChecked(hide);
      QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });
    QCOMPARE(w->m_config.exec(),int(QDialog::Accepted));
    w->ui->decodedTextBrowser->setConfiguration(&w->m_config);
  }
  int managedDecode(QString const& message,QString stamp="000015",int snr=-10) {
    DecodedText decoded(stamp+QString(" %1  0.1 1700 ~ ").arg(snr,3)+message);
    w->observeManagedDecode(decoded);
    return w->ui->decodedTextBrowser->displayDecodedText(&decoded,"N0CALL",Radio::base_callsign(w->m_hisCall),w->m_hisGrid,false,w->m_logBook,w->m_qsoHistory,w->m_qsoHistory,14074000,"FT8",false,false,1500,{},{},{},{},false,w.get(),true);
  }
  void prepareManaged(ManagedMode mode,bool enable=true) {
    managedFixtureTime(); // Synthetic 00:15 decodes belong to the previous RX slot.
    w->on_stopTxButton_clicked();w->m_mode=w->m_modeTx="FT8";w->m_TRperiod=15;w->m_freqNominal=14074000;
    w->m_houndMode=false;w->m_autoseq=false;w->m_callMode=0;w->m_singleshot=true;w->m_autoTx=false;w->m_skipTx1=false;
    w->m_diskData=false;w->m_manualDecode=false;w->m_decoderBusy=false;
    w->m_qsoHistory.init();w->m_qsoHistory.owndata("NA","K","AA00",false);
    w->m_logBook.init("N0CALL","","","ZZ9QA");
    // Shared managed fixtures receive at 00:15/00:45. Fixed-slot mode replies
    // in the opposite (first) TX slot unless a test explicitly changes it.
    w->assignTxSlot(true);
    w->setManagedMode(mode);if(enable)manualEnable();
  }
  void managedSentRound(QString message=QString()) {
    w->m_currentMessage=message.isEmpty()?w->selectedTransmitText():message;
    w->prepareManagedTransmission();w->m_managedTxSlot=0;
    w->m_modulator->waveformStarted(w->m_managedTxGeneration);
    w->m_modulator->waveformConsumed(w->m_managedTxGeneration);
    w->m_managedTxNatural=true;w->managedTransmissionFinished();
    w->m_managed.receiveCycleFinished(15000,15000);
  }
private slots:
  void audioPlaybackGenerationIsolation();
  void udpHistoryIdentityAndClear();
  void udpReplyCacheAndSafety();
  void udpReplaySnapshotAndCancellation();
  void radioAudioPolicies();
  void soundOutputLifecycle();
  void hamlibSplitModeBeforePtt_data();
  void hamlibSplitModeBeforePtt();
  void hamlibSplitBandRecallAndFailure();
  void hamlibDataProfilesRemainSelected();
  void ft8ThreadMenuLimits();
  void cleanup() {
    if(w && managedFixtureClockActive)w->m_jtdxtime->SetOffset(managedFixtureOffset);
    managedFixtureClockActive=false;
  }
  void ordinaryBusyOptionBehavior_data();
  void ordinaryBusyOptionBehavior();
  void ordinaryBusyResumesOnTarget_data();
  void ordinaryBusyResumesOnTarget();
  void ordinaryBusyDecodeOrder_data();
  void ordinaryBusyDecodeOrder();
  void ordinaryBusyInitialDecodeOrder_data();
  void ordinaryBusyInitialDecodeOrder();
  void ordinaryBusyCancellation();
  void ordinaryBusyBudgetStillExpires_data();
  void ordinaryBusyBudgetStillExpires();
  void ordinaryBusyKeepsCurrentWave();
  void ordinaryBusySafetyGates();
  void managedCqSpamCannotBypassBudget_data();
  void managedCqSpamCannotBypassBudget();
  void managedFinalBudgetWaitsForReceive_data();
  void managedFinalBudgetWaitsForReceive();
  void managedPeriodicCqResumesAfterCallers_data();
  void managedPeriodicCqResumesAfterCallers();
  void cqLateDecodeStillAnswers_data();
  void cqLateDecodeStillAnswers();
  void completedQsoAnswersWaitingCaller_data();
  void completedQsoAnswersWaitingCaller();
  void ordinaryRetryRecomputesTransmitSlot_data();
  void ordinaryRetryRecomputesTransmitSlot();
  void manualRetrySelectionKeepsOnlyCurrentAttempt();
  void ordinaryRetryLimits_data();
  void ordinaryRetryLimits();
  void signoffRejectsStaleWaveformCompletion();
  void signoffLogsLatestRoundWithoutAdvancing();
  void signoffPromptRetainsSnapshotAcrossRounds();
  void managedSignoffKeepsPartner_data();
  void managedSignoffKeepsPartner();
  void managedRr73WaitsForWaveAndPtt();
  void managed73RequiresCompleteLiveReceive();
  void managed73LiveCaptureHandoff_data();
  void managed73LiveCaptureHandoff();
  void modulatorWaveformCompletion();
  void tciWaveformCompletion();
  void txEnableButtonShowsSessionAndOnAir();
  void catBandChangeUpdatesSelectionAndWorkedColors();
  void managedSlotPolicyIntegration();
  void doubleClickTxManualGesture();
  void doubleClickTxRejectsUnsafeSelections();
  void managedManualSelection_data();
  void managedManualSelection();
  void managedTransmitStartTiming_data();
  void managedTransmitStartTiming();
  void managedCandidateUsesDecodeUtc_data();
  void managedCandidateUsesDecodeUtc();
  void jtAlertReplyUsesSecondsAndKeepsManualGate_data();
  void jtAlertReplyUsesSecondsAndKeepsManualGate();
  void autoPriorityIsExclusiveAndMigrates();
  void autoSearchIndependentOfHighlights_data();
  void autoSearchIndependentOfHighlights();
  void ownFinalMessagesDoNotRestartQso_data();
  void ownFinalMessagesDoNotRestartQso();
  void ownMessagesUseSharedTransmitColor();
  void ownTransmitColorIsConfigurable();
  void hiddenTransmitStillUpdatesQsoHistory();
  void upstreamSingleQsoPersistsBeforeClearing();
  void delayedLogConfirmationKeepsNewSelection();
  void managedLoggedQsoAdvancesWithoutRepeating();
  void independentPostQsoCleanupOptions();
void loggedQsoCleanup_data() {
  QTest::addColumn<bool>("automatic");QTest::addColumn<bool>("single");
  QTest::newRow("manual")<<false<<false;
  QTest::newRow("automatic-continuous")<<true<<false;
  QTest::newRow("automatic-single")<<true<<true;
}
void loggedQsoCleanup() {
  QFETCH(bool,automatic);QFETCH(bool,single);
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);
  QTRY_VERIFY_WITH_TIMEOUT(!w->m_logLoading && !w->m_logRefreshPending && !w->m_logImportActive && w->m_logReady,20000);
  w->m_diskData=false;w->m_manualDecode=false;
  w->m_mode=w->m_modeTx="FT8";w->m_TRperiod=15;w->m_houndMode=false;
  w->m_autoseq=automatic;w->m_autoTx=automatic;w->m_singleshot=single;w->m_lockTxFreq=false;
  w->m_qsoHistory.init();w->m_qsoHistory.owndata("NA","K","AA00",false);
  w->ui->dxCallEntry->setText("K8QCL");w->ui->dxGridEntry->setText("FN20");
  w->ui->TxFreqSpinBox->setValue(1200);w->ui->RxFreqSpinBox->setValue(1800);
  manualEnable();QVERIFY(w->m_enableTx);
  w->m_qsoHistory.message("K8QCL",QsoHistory::S73,0,"","","NA","K",30,"-10",1800,"FT8");
  w->m_qsoHistory.complete("K8QCL"); // This fixture covers cleanup of a confirmed QSO.
  w->m_status=QsoHistory::RREPORT; // The cached AutoSeq status is intentionally stale.
  w->m_transmitting=true;g_iptt=1;
  w->applyGeneralPostQso("K8QCL");
  QCOMPARE(w->m_hisCall,QString("K8QCL"));QVERIFY(!w->m_generalPostQsoCall.isEmpty());
  w->m_transmitting=false;w->generalTick();
  QCOMPARE(w->m_hisCall,QString("K8QCL")); // CAT has not released PTT yet.
  g_iptt=0;w->m_tx_when_ready=true;w->generalTick();
  QCOMPARE(w->m_hisCall,QString("K8QCL"));
  w->m_tx_when_ready=false;w->generalTick();
  QVERIFY(w->m_hisCall.isEmpty());QVERIFY(w->m_hisGrid.isEmpty());
  QVERIFY(w->ui->dxCallEntry->text().isEmpty());QVERIFY(w->ui->dxGridEntry->text().isEmpty());
  QCOMPARE(w->ui->RxFreqSpinBox->value(),1200);QVERIFY(!w->m_lockTxFreq);
  QCOMPARE(w->m_enableTx,!single);QVERIFY(w->m_generalPostQsoCall.isEmpty());
  QString grid;QCOMPARE(w->m_qsoHistory.status("K8QCL",grid),QsoHistory::FIN);
  QVERIFY(!w->selectedTransmitText().contains("K8QCL"));
  w->on_stopTxButton_clicked();w->m_autoseq=false;w->m_autoTx=false;w->m_singleshot=false;
}
void delayedQsoCleanupProtectsNextPartner() {
  w->on_stopTxButton_clicked();w->m_lockTxFreq=false;
  w->ui->TxFreqSpinBox->setValue(1200);w->ui->RxFreqSpinBox->setValue(1700);
  w->ui->dxCallEntry->setText("K8OLD");w->ui->dxGridEntry->setText("FN20");
  w->m_transmitting=true;g_iptt=1;w->applyGeneralPostQso("K8OLD");
  w->ui->dxCallEntry->setText("K8NEW");w->ui->dxGridEntry->setText("FN31");
  w->m_transmitting=false;g_iptt=0;w->generalTick();
  QCOMPARE(w->m_hisCall,QString("K8NEW"));QCOMPARE(w->m_hisGrid,QString("FN31"));
  QCOMPARE(w->ui->RxFreqSpinBox->value(),1700);QVERIFY(w->m_generalPostQsoCall.isEmpty());
  // A late log acceptance for the previous partner must also be harmless.
  w->applyGeneralPostQso("K8OLD");QCOMPARE(w->m_hisCall,QString("K8NEW"));
  QCOMPARE(w->ui->RxFreqSpinBox->value(),1700);
  w->clearDX("test cleanup");
}
void manualClearResetsReceiveWithoutLockingTx() {
  w->on_stopTxButton_clicked();w->m_lockTxFreq=false;
  w->ui->TxFreqSpinBox->setValue(1250);w->ui->RxFreqSpinBox->setValue(1850);
  // Short/incomplete editors do not populate the model, but still need clearing.
  w->ui->dxCallEntry->setText("K");w->ui->dxGridEntry->setText("F");
  w->ui->ClearDxButton->click();
  QVERIFY(w->ui->dxCallEntry->text().isEmpty());QVERIFY(w->ui->dxGridEntry->text().isEmpty());
  QCOMPARE(w->ui->RxFreqSpinBox->value(),1250);QCOMPARE(w->ui->TxFreqSpinBox->value(),1250);
  QVERIFY(!w->m_lockTxFreq);QVERIFY(!w->m_enableTx);QVERIFY(!w->m_manualTxAuthorized);
  w->ui->RxFreqSpinBox->setValue(1800);QCOMPARE(w->ui->TxFreqSpinBox->value(),1250);
}
void selfDecodeCannotSelectOwnCall() {
  w->on_stopTxButton_clicked();w->m_mode=w->m_modeTx="FT8";w->m_lockTxFreq=false;
  w->ui->dxCallEntry->setText("K8REAL");w->ui->dxGridEntry->setText("FN20");
  w->ui->TxFreqSpinBox->setValue(1250);w->ui->RxFreqSpinBox->setValue(1850);
  bool slot=w->m_txFirst;
  for(auto message:{"CQ N0CALL AA00","K1ABC N0CALL -10","CQ N0CALL/P AA00","K1ABC <N0CALL> RR73"}) {
    QString row="000015 -10  0.1 1500 ~ "+QString(message);
    w->processMessage(row,row.size()-1,false,false);
    QCOMPARE(w->m_hisCall,QString("K8REAL"));QCOMPARE(w->m_hisGrid,QString("FN20"));
    QCOMPARE(w->ui->RxFreqSpinBox->value(),1850);QCOMPARE(w->ui->TxFreqSpinBox->value(),1250);
    QCOMPARE(w->m_txFirst,slot);QVERIFY(!w->m_enableTx);
  }
  w->clearDX("test cleanup");
}

  void initTestCase() {
    auto dialogGuard=new QTimer(this);
    connect(dialogGuard,&QTimer::timeout,this,[] {
      if (auto dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
        qWarning("Headless test dialog: %s %s",dialog->metaObject()->className(),qPrintable(dialog->windowTitle()));
        if (auto box=qobject_cast<QMessageBox*>(dialog)) qWarning("%s",qPrintable(box->text()));
        dialog->reject();
      }
    });
    dialogGuard->start(50);
    settings.setValue("Configuration/MyCall", "N0CALL");
    settings.setValue("Configuration/MyGrid", "AA00");
    StationProfile primary;primary.call="N0CALL";primary.grid="AA00";
    auto secondary=primary;secondary.call="N1CALL/P";secondary.grid="FN20";secondary.region=2;secondary.type2=0;
    settings.setValue("Configuration/StationProfiles",StationProfiles::encode({primary,secondary}));
    settings.setValue("Configuration/PSKReporter", false);
    settings.setValue("Configuration/IDint",99);
    settings.setValue("Configuration/IDafter73",true);
    shared.setKey(QCoreApplication::applicationName());
    if (!shared.create(sizeof(dec_data))) {
      QVERIFY(shared.attach()); shared.detach();
      QVERIFY(shared.create(sizeof(dec_data)));
    }
    memset(shared.data(), 0, sizeof(dec_data));
    w.reset(new MainWindow(true, &settings, &shared, 4, &network, QProcessEnvironment::systemEnvironment()));
    w->m_guiTimer.stop(); w->minuteTimer.stop();
    w->on_monitorButton_clicked(false);
    w->m_autoTx = false;
    w->m_mode = w->m_modeTx = "FT8";
    w->m_TRperiod = 15;
  }
  void txRxSplitDefaultsAndUpgrade() {
    QVERIFY(!w->m_lockTxFreq);
    QVERIFY(!w->ui->pbTxLock->isChecked());
    w->ui->TxFreqSpinBox->setValue(1200);
    w->ui->RxFreqSpinBox->setValue(1700);
    QCOMPARE(w->ui->TxFreqSpinBox->value(),1200);
    w->ui->TxFreqSpinBox->setValue(1900);
    QCOMPARE(w->ui->RxFreqSpinBox->value(),1700);

    // An existing V3.1.0 configuration is corrected once.
    settings.remove("Common/TxRxSplitDefaultV311");
    settings.setValue("Common/LockTxFreq",true);
    w->readSettings();
    QVERIFY(!w->m_lockTxFreq);
    QVERIFY(!settings.value("Common/LockTxFreq").toBool());
    QVERIFY(settings.value("Common/TxRxSplitDefaultV311").toBool());

    // A later manual choice survives settings save/load.
    w->ui->pbTxLock->click();
    QVERIFY(w->m_lockTxFreq);
    w->writeSettings();
    w->m_lockTxFreq=false;
    w->readSettings();
    QVERIFY(w->m_lockTxFreq);
    w->ui->pbTxLock->click();
    QVERIFY(!w->m_lockTxFreq);
    w->writeSettings();
    w->readSettings();
    QVERIFY(!w->m_lockTxFreq);
  }
  void cwIdentificationDisabled() {
    QCOMPARE(w->m_config.id_interval(),0);QVERIFY(!w->m_config.id_after_73());
    auto dialog=w->findChild<QWidget*>("configuration_dialog");QVERIFY(dialog);
    QVERIFY(!dialog->findChild<QSpinBox*>("CW_id_interval_spin_box"));
    QVERIFY(!dialog->findChild<QCheckBox*>("CW_id_after_73_check_box"));
  }
  void upstreamMenusKeepTransmissionDisabled() {
    w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);w->m_autoseq=false;w->m_diskData=false;
    TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
    for(auto mode:{"Q65","MSK144","Echo"}) {
      w->selectUpstreamMode(mode);QCOMPARE(w->m_mode,QString(mode));
      QVERIFY(w->m_upstreamActions.value(mode)->isChecked());QVERIFY(w->upstreamMode());
      QVERIFY(!w->m_enableTx);QVERIFY(!w->m_manualTxAuthorized);QCOMPARE(g_iptt,0);
      w->upstreamGuiUpdate();QVERIFY(!w->m_upstream->encoding());
      w->on_stopTxButton_clicked();
      if(QString(mode)!="Q65")QVERIFY(!w->ui->TxFreqSpinBox->isEnabled());
    }
    w->selectUpstreamMode("Q65");w->m_upstreamPeriod->setCurrentText("300");QCOMPARE(w->m_TRperiod,300.);
    w->m_upstreamSubmode->setCurrentText("C");QCOMPARE(w->upstreamParameters().value("submode").toInt(),2);
    w->m_emeTools->setEnabled(false);w->m_fullDuplexAction->setEnabled(false);
    w->on_stopTxButton_clicked();QVERIFY(w->m_emeTools->isEnabled());QVERIFY(w->m_fullDuplexAction->isEnabled());
    QCOMPARE(ptt.pttOnCount(),0);
    w->m_upstreamPeriod->setCurrentText("60");w->on_actionFT8_triggered();
  }
  void extendedDecodeKeepsLongCallsignsAndReports() {
    QString message="EA8/AB1CDE BD4XNS/QRP R-15";
    DecodedText extended("235945 -18  2.5 1500 : "+message,nullptr,true);
    QCOMPARE(extended.message(),message);QCOMPARE(extended.timeInSeconds(),unsigned(86385));
    QCOMPARE(extended.frequencyOffset(),1500);QVERIFY(!extended.isDebug());QVERIFY(!extended.isDXped());
    QString sender,grid;extended.deCallAndGrid(sender,grid);QCOMPARE(sender,QString("BD4XNS/QRP"));
    DecodedText decorated("000015 -10  0.0 1500 : CQ BD4XNS/QRP OM92"+QString(QChar::Nbsp)+"China 1200 km",nullptr,true);
    QCOMPARE(decorated.message(),QString("CQ BD4XNS/QRP OM92"));
  }
  void upstreamAudioReachesMainWindow_data() {
    QTest::addColumn<QString>("mode");QTest::newRow("Q65")<<QString("Q65");QTest::newRow("MSK144")<<QString("MSK144");
  }
  void upstreamAudioReachesMainWindow() {
    QFETCH(QString,mode);w->on_stopTxButton_clicked();stopWaveAudio();w->selectUpstreamMode(mode);
    w->m_upstreamPeriod->setCurrentText("15");w->m_upstreamSubmode->setCurrentIndex(0);
    stopWaveAudio();
    QVERIFY(QObject::disconnect(w->m_detector,&Detector::timedPcmWritten,w.get(),&MainWindow::upstreamAudio));
    struct Restore {std::function<void()> run;~Restore(){run();}} restore{[&]{
      w->on_stopTxButton_clicked();w->m_monitoring=false;w->on_monitorButton_clicked(false);
      connect(w->m_detector,&Detector::timedPcmWritten,w.get(),&MainWindow::upstreamAudio);
    }};
    // An unshown waterfall initially covers only about 732 Hz. Keep all of
    // the Q65-A tones within that known search range; MSK is centered at1500.
    const int audioFrequency=mode=="Q65"?250:1500;
    w->ui->RxFreqSpinBox->setValue(audioFrequency);w->ui->TxFreqSpinBox->setValue(audioFrequency);
    w->m_diskData=false;w->m_autoseq=false;w->m_mcpDecodes=QJsonArray{};
    UpstreamController generator;generator.setContext("offline-receive-fixture");
    QSignalSpy encoded(&generator,&UpstreamController::encoded),errors(&generator,&UpstreamController::error);
    auto parameters=w->upstreamParameters();parameters["audio_hz"]=audioFrequency;
    if(mode=="Q65")QVERIFY(parameters.value("low_hz").toInt()<=audioFrequency && parameters.value("high_hz").toInt()>audioFrequency);
    QVERIFY(generator.encode("CQ K1ABC FN20",parameters));QTRY_COMPARE_WITH_TIMEOUT(encoded.size(),1,20000);QCOMPARE(errors.size(),0);
    qInfo().noquote()<<"Receive fixture parameters"<<QJsonDocument(parameters).toJson(QJsonDocument::Compact)
      <<"live parameters"<<QJsonDocument(w->upstreamParameters()).toJson(QJsonDocument::Compact);
    QObject observer;QElapsedTimer arrival;arrival.start();
    connect(w->m_upstream,&UpstreamController::decodeFinished,&observer,[&](QJsonObject request){qInfo().noquote()<<"Receive done after"<<arrival.elapsed()<<QJsonDocument(request).toJson(QJsonDocument::Compact);});
    connect(w->m_upstream,&UpstreamController::error,&observer,[&](QString error){qInfo().noquote()<<"Receive error after"<<arrival.elapsed()<<error;});
    auto wave=encoded.first().at(1).toByteArray();auto metadata=encoded.first().at(2).toJsonObject();
    QByteArray audio(metadata.value("start_delay_ms").toInt()*24,0);
    // Clean synthetic 48 kHz tones need no anti-alias filter for this fixture.
    for(int i=0;i+7<wave.size();i+=8)audio.append(wave.constData()+i,2);
    audio.append(QByteArray(qMax(0,15*24000-audio.size()),0));
    TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
    w->m_monitoring=true; // Feed captured samples directly; no physical audio device.
    qint64 utc=QDateTime(QDate(2026,10,30),QTime(0,0),Qt::UTC).toMSecsSinceEpoch();
    const int limit=mode=="Q65"?audio.size():24000;
    for(int offset=0;offset<limit;offset+=2400) {
      auto chunk=audio.mid(offset,qMin(2400,limit-offset));
      w->upstreamAudio(chunk,utc+(offset+chunk.size())/24);
      QCoreApplication::processEvents();
    }
    qInfo()<<"Capture submitted after"<<arrival.elapsed()<<"mode"<<w->m_mode<<"period"<<w->m_TRperiod<<"monitor"<<w->m_monitoring<<"buffer"<<w->m_periodAudio.samples();
    auto decoded=[this]{for(auto const& d:w->m_mcpDecodes)if(d.toObject().value("raw").toString().contains("CQ K1ABC FN20"))return true;return false;};
    QTRY_VERIFY_WITH_TIMEOUT(decoded(),20000);QTRY_VERIFY_WITH_TIMEOUT(!w->m_upstream->busy(),20000);
    QVERIFY(!w->m_manualTxAuthorized && !w->m_enableTx);QCOMPARE(g_iptt,0);
    QCOMPARE(ptt.pttOnCount(),0);
    w->m_monitoring=false;w->on_actionFT8_triggered();stopWaveAudio();
  }
  void fullDuplexAndSwrPreserveManualAuthorization() {
    w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
    QVERIFY(QObject::disconnect(w.get(),&MainWindow::resumeAudioInputStream,w->m_soundInput,&SoundInput::resume));
    struct Restore {std::function<void()> run;~Restore(){run();}} restore{[&]{
      w->on_stopTxButton_clicked();w->on_monitorButton_clicked(false);w->m_config.set_full_duplex(false);
      connect(w.get(),&MainWindow::resumeAudioInputStream,w->m_soundInput,&SoundInput::resume);
    }};
    TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
    w->m_config.set_full_duplex(true);w->on_monitorButton_clicked(true);
    w->m_transmitting=true;g_iptt=1;w->transmitDisplay(true);
    QVERIFY(w->m_monitoring);QVERIFY(!w->m_manualTxAuthorized);
    w->m_config.set_full_duplex(false);w->transmitDisplay(true);QVERIFY(!w->m_monitoring);
    w->on_stopTxButton_clicked();

    auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
    QTimer::singleShot(0,dialog,[dialog]{
      dialog->findChild<QCheckBox*>("check_SWR_check_box")->setChecked(true);
      dialog->findChild<QCheckBox*>("output_power_check_box")->setChecked(false);
      QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });
    QCOMPARE(w->m_config.exec(),int(QDialog::Accepted));QVERIFY(w->m_config.check_SWR());QVERIFY(!w->m_config.do_pwr());
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_logLoading && !w->m_logRefreshPending,20000);
    manualEnable();QVERIFY(w->m_manualTxAuthorized);
    w->m_transmitting=true;g_iptt=1;w->m_tx_when_ready=false;w->m_rigState.online(true);w->m_rigState.ptt(true);
    auto state=w->m_rigState;state.frequency(w->m_freqNominal);state.swr(250);
    w->handle_transceiver_update(state);QVERIFY(w->m_manualTxAuthorized);QVERIFY(w->m_enableTx);
    state.swr(251);w->handle_transceiver_update(state);
    QVERIFY(!w->m_manualTxAuthorized);QVERIFY(!w->m_enableTx);QVERIFY(!w->m_transmitting);QCOMPARE(g_iptt,0);
    QVERIFY(!w->ptt0Timer.isActive() && !w->ptt1Timer.isActive());
    state.ptt(false);state.swr(100);w->handle_transceiver_update(state);
    w->TxAgain();QVERIFY(!w->m_enableTx);QVERIFY(!w->m_manualTxAuthorized);
    QCOMPARE(ptt.pttOnCount(),0);
  }
  void generalQuickCallCannotAuthorizeTransmission() {
    w->on_stopTxButton_clicked();stopWaveAudio();w->on_actionFT8_triggered();w->m_diskData=false;
    auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
    auto configure=[&](bool value){
      QTimer::singleShot(0,dialog,[dialog,value]{
        dialog->findChild<QCheckBox*>("quick_call_check_box")->setChecked(value);
        dialog->findChild<QCheckBox*>("alternate_bindings_check_box")->setChecked(value);
        QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
      });
      QCOMPARE(w->m_config.exec(),int(QDialog::Accepted));w->applyGeneralSettings();
    };
    configure(true);w->m_hisCall="K1ABC";w->ui->dxCallEntry->setText("K1ABC");
    TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
    w->quickCallFromSelection("000015 -10  0.0 1500 ~ CQ K1ABC FN20",false,false);
    QVERIFY(!w->m_manualTxAuthorized && !w->m_enableTx);
    for(int key=Qt::Key_F1;key<=Qt::Key_F5;++key) {
      QKeyEvent event(QEvent::KeyPress,key,Qt::NoModifier);
      QVERIFY(w->handleGeneralKeyEvent(&event));QVERIFY(!w->m_manualTxAuthorized && !w->m_enableTx);QCOMPARE(g_iptt,0);
    }
    QCOMPARE(ptt.pttOnCount(),0);
    configure(false);w->clearDX("test cleanup");
  }
  void uncheckingEnableStopsUpstreamImmediately() {
    w->on_stopTxButton_clicked();stopWaveAudio();w->selectUpstreamMode("Q65");w->m_upstreamPeriod->setCurrentText("15");
    w->m_diskData=false;w->m_autoseq=false;stopWaveAudio();
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_logLoading && !w->m_logRefreshPending,20000);
    stopWaveAudio();
    TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
    for(int phase=0;phase<3;++phase) {
      manualEnable();QVERIFY(w->m_manualTxAuthorized && w->m_enableTx);
      if(phase==0)QVERIFY(w->m_upstream->encode("CQ N0CALL AA00",w->upstreamParameters()));
      else {
        w->m_upstreamPcm=QByteArray(48000,0);w->m_upstreamTxTimeout.start(10000);
        g_iptt=1;w->m_tx_when_ready=phase==1;w->m_transmitting=phase==2;
        w->ptt1Timer.start(10000);
      }
      w->ui->enableTxButton->click(); // The checked button is itself a Stop action.
      QVERIFY(!w->m_manualTxAuthorized && !w->m_enableTx && !w->m_transmitting);QCOMPARE(g_iptt,0);
      QVERIFY(!w->m_upstream->encoding());QVERIFY(w->m_upstreamPcm.isEmpty());
      QVERIFY(!w->m_upstreamTxTimeout.isActive() && !w->ptt1Timer.isActive());
      QCOMPARE(w->tx_status_label->text(),w->tr("Stopped"));QVERIFY(w->progressBar->styleSheet().isEmpty());
    }
    QTest::qWait(150);QCOMPARE(ptt.pttOnCount(),0);
    manualEnable();w->m_hisCall="K1ABC";w->m_lastloggedcall="K1ABC";w->m_lastloggedtime=w->m_jtdxtime->currentDateTimeUtc2();
    w->m_currentMessage="K1ABC N0CALL 73";w->prepareSignoff(w->m_currentMessage);
    const qint64 period=15000,slot=w->m_jtdxtime->currentMSecsSinceEpoch2()/period*period-4*period;
    w->m_signoff.prepare("K1ABC",w->generalContext(),SignoffState::SeventyThree,slot,period);
    w->m_signoff.started();w->m_watchdogFinishing=true;w->m_transmitting=true;g_iptt=1;
    w->m_rigState.ptt(false);w->m_tx_when_ready=false;w->m_decoderBusy=false;
    w->finishUpstreamTransmit();
    QVERIFY(w->m_watchdogFinishing && w->m_enableTx);QVERIFY(!w->m_txwatchdog);
    w->m_signoffAudioCycles.insert(slot+period);w->signoffReceiveCycleFinished(slot+period);
    QVERIFY(w->m_txwatchdog && !w->m_enableTx && !w->m_manualTxAuthorized);QCOMPARE(g_iptt,0);
    QCOMPARE(w->tx_status_label->text(),w->tr("Tx watchdog expired"));
    w->clearDX("test cleanup");
    w->on_actionFT8_triggered();stopWaveAudio();
  }
  void continuousCaptureDoesNotLoseAudioAtLegacyRollover() {
    stopWaveAudio();JTDXDateTime clock;
    Detector detector(12000,60,&clock,4);detector.setBlockSize(512);detector.setContinuous(true);
    QVERIFY(detector.initialize(QIODevice::WriteOnly,AudioDevice::Mono));
    qint64 captured=0;
    connect(&detector,&Detector::pcmWritten,this,[&](QByteArray const& pcm){captured+=pcm.size()/2;});
    QByteArray input(2000,0);
    for(int i=0;i<3000;++i)QCOMPARE(detector.write(input),qint64(input.size()));
    QCOMPARE(captured,qint64((3000000/(512*4))*512));
    detector.close();detector.reset();
  }
  void generalMenusShareSettingsWithoutReadSideEffects() {
    w->ui->actionSingleShot->setChecked(true);QVERIFY(w->m_config.disable_TX_on_73());QVERIFY(w->ui->actionDisableTx73->isChecked());
    w->ui->actionDisableTx73->setChecked(false);QVERIFY(!w->m_config.disable_TX_on_73());QVERIFY(!w->ui->actionSingleShot->isChecked());
    w->ui->actionEraseWindowsAtBandChange->setChecked(true);QVERIFY(w->m_config.erase_BandActivity());
    w->ui->actionEraseWindowsAtBandChange->setChecked(false);QVERIFY(!w->m_config.erase_BandActivity());
    w->ui->RxFreqSpinBox->setValue(1700);w->ui->TxFreqSpinBox->setValue(1200);
    w->clearDX("test manual clear");QCOMPARE(w->ui->RxFreqSpinBox->value(),1700);
  }
  void satelliteFieldsSurviveBothLogFormats() {
    QDir base(temp.path()+"/satellite-log");auto file=OperatorLog::quarterFile("N0CALL",QDate(2026,9,30),base);
    ADIF log;log.init(file);auto start=QDateTime::fromString("2026-10-01T07:59:00+08:00",Qt::ISODate);
    QMap<QString,QString> fields{{"PROP_MODE","SAT"},{"SAT_NAME","RS-44"},{"SAT_MODE","V/U"},{"FREQ_RX","435.640000"},{"BAND_RX","70cm"}};
    QVERIFY(log.addQSOToFile("K4XYZ","FN20","Q65","-10","-12",start,start.addSecs(60),"2m","","","145.965000","N0CALL","AA00","5",false,fields));
    QString error;auto quarter=OperatorLog::read(file,&error);QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(quarter.size(),1);
    auto legacy=OperatorLog::read(OperatorLog::directory("N0CALL",base).filePath("wsjtx_log.adi"),&error);
    QCOMPARE(legacy.size(),1);QCOMPARE(legacy.first(),quarter.first());
    auto row=quarter.first();QCOMPARE(row.value("MODE"),QString("MFSK"));QCOMPARE(row.value("SUBMODE"),QString("Q65"));
    for(auto it=fields.begin();it!=fields.end();++it)QCOMPARE(row.value(it.key()),it.value());
    QCOMPARE(row.value("QSO_DATE_OFF"),QString("20261001"));
    QCOMPARE(row.value("QSO_DATE"),QString("20260930"));QCOMPARE(row.value("TIME_ON"),QString("235900"));
    w->m_logDlg->initLogQSO("K4XYZ","FN20","Q65","-10","-12","","",start,start.addSecs(60),145965000,false,435640000);
    auto date=w->m_logDlg->findChild<QDateTimeEdit*>("start_date_time");QVERIFY(date);
    QCOMPARE(date->timeSpec(),Qt::UTC);QCOMPARE(date->dateTime(),start.toUTC());QCOMPARE(date->date(),QDate(2026,9,30));
    auto rx=w->m_logDlg->findChild<QDoubleSpinBox*>("log_rx_frequency");QVERIFY(rx);QCOMPARE(rx->value(),435.64);
    auto remember=w->m_logDlg->findChild<QCheckBox*>("log_remember_propagation");QVERIFY(remember);remember->setChecked(true);
    w->m_logDlg->initLogQSO("K4XYZ","FN20","FT8","-10","-12","","",start,start.addSecs(60),14074000,false,14074000);
    QCOMPARE(rx->value(),0.);remember->setChecked(false);
    w->m_logDlg->reject();
  }
void displayPeriodStartsAtTop()
{
  DisplayText view;view.resize(800,300);view.show();
  view.decodesFromTop_=true;view.updatePeriodMargin();
  for(int i=0;i<30;++i)view.appendText(QString("old row %1").arg(i));
  view.new_period();view.appendText("new period first decode");
  auto first=view.lastAppendedLine_;
  QCoreApplication::processEvents();
  QVERIFY2(qAbs(view.cursorRect(first).top()-int(view.document()->documentMargin()))<3,"First decode must be anchored at the top");
  int scroll=view.verticalScrollBar()->value();
  for(int i=0;i<30;++i)view.appendText(QString("new row %1").arg(i));
  QCOMPARE(view.verticalScrollBar()->value(),scroll);
  view.new_period();view.appendText("next period");
  QCoreApplication::processEvents();
  QVERIFY(qAbs(view.cursorRect(view.lastAppendedLine_).top()-int(view.document()->documentMargin()))<3);
  view.decodesFromTop_=false;view.updatePeriodMargin();view.appendText("normal scrolling restored");
  QVERIFY(view.cursorRect(view.lastAppendedLine_).top()<view.viewport()->height());
}

void displayExtendedMessagesPreserveEntirePayload()
{
  DisplayText view;
  QString payload="EA8/K1ABC/P W9XYZ R FN20AA EXTRA12345";
  QCOMPARE(payload.size(),37);
  QString row="123000 -10  0.1 1234 : "+payload;
  QCOMPARE(row.size(),60);
  auto labels=DecodedPresentation::appendage(61,"United States","NJ","123 km",true,2,0);
  view.appendText(row,"#ffffff","#000000",0," ","#000000",labels,false,true);
  auto rendered=view.toPlainText();
  QCOMPARE(rendered.left(60),row);QVERIFY(rendered.contains("EXTRA12345"));
  QVERIFY(view.document()->toRawText().indexOf(QChar::Nbsp)>=60);
  QTextCursor cursor(view.document());cursor.setPosition(58);cursor.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
  QVERIFY(cursor.charFormat().fontStrikeOut());
}

void displayTrimmingKeepsHighlightCursor()
{
  DisplayText view;view.document()->setMaximumBlockCount(3);
  view.setDxHighlightTarget("K1ABC","FN20");
  for(int i=0;i<5;++i)view.appendText("an old row");
  QString row="123000 -10  0.1 1234 ~ K1ABC K1ABCD FN20";
  view.appendText(row,"#ffffff","#000000",0," ","#000000","",false,true);
  view.highlightLastMessage(row.size(),"","");
  QCOMPARE(view.document()->blockCount(),3);
  auto first=view.lastAppendedLine_;
  QCOMPARE(first.position(),view.document()->lastBlock().position());
  QVERIFY2(view.document()->lastBlock().text().startsWith(row),qPrintable(view.document()->lastBlock().text()));
  auto formatAt=[&](int position){QTextCursor c=first;c.setPosition(first.position()+position);c.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);return c.charFormat();};
  auto call=formatAt(row.indexOf("K1ABC"));
  QCOMPARE(call.foreground().color(),QColor("#cc0000"));QVERIFY(call.fontStrikeOut());
  QCOMPARE(formatAt(row.indexOf("K1ABCD")).foreground().color(),QColor("#000000"));
  QCOMPARE(formatAt(row.indexOf("FN20")).foreground().color(),QColor("#0000c8"));
}


  void aboutDescribesUpdatedComponents() {
    CAboutDlg about;auto text=about.findChild<QTextBrowser*>();QVERIFY(text);
    auto content=text->toPlainText();QVERIFY(content.contains("Fluxmancer"));QVERIFY(content.contains("3.0.2"));QVERIFY(content.contains("Hamlib"));
    QVERIFY(content.contains("UA3DJY"));QVERIFY(content.contains("K1JT"));QVERIFY(!content.contains("FT8 code is forked from WSJT-X v1.8"));
  }
  void modernDecodeOptionsPersist() {
    QVERIFY(!w->ui->actionModernFT8Decode->isChecked());
    QVERIFY(!w->ui->actionModernFT4Decode->isChecked());
    QVERIFY(w->ui->menuFT8_decoding->actions().contains(w->ui->actionModernFT8Decode));
    QVERIFY(w->ui->menuFT4_decoding->actions().contains(w->ui->actionModernFT4Decode));
    for(bool ft8:{false,true})for(bool ft4:{false,true}) {
      w->ui->actionModernFT8Decode->setChecked(ft8);w->ui->actionModernFT4Decode->setChecked(ft4);
      w->writeSettings();
      w->ui->actionModernFT8Decode->setChecked(!ft8);w->ui->actionModernFT4Decode->setChecked(!ft4);
      w->readSettings();
      QCOMPARE(w->ui->actionModernFT8Decode->isChecked(),ft8);
      QCOMPARE(w->ui->actionModernFT4Decode->isChecked(),ft4);
    }
    w->ui->actionModernFT8Decode->setChecked(false);w->ui->actionModernFT4Decode->setChecked(false);w->writeSettings();
  }
  void managedModeExitPreservesMonitoring() {
    w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);
    w->m_mode=w->m_modeTx="FT8";w->m_diskData=false;w->m_tci=false;
    // Exercise the GUI/PTT state machine without opening an audio input or
    // sending RF. A simulated TX below only changes bookkeeping flags.
    stopWaveAudio();
    QVERIFY(QObject::disconnect(w.get(),&MainWindow::resumeAudioInputStream,w->m_soundInput,&SoundInput::resume));
    struct Restore {std::function<void()> run;~Restore(){run();}} restore{[&]{
      w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);w->on_monitorButton_clicked(false);
      w->m_tci=false;w->m_mode=w->m_modeTx="FT8";
      connect(w.get(),&MainWindow::resumeAudioInputStream,w->m_soundInput,&SoundInput::resume);
    }};
    QSignalSpy resumed(w.get(),&MainWindow::resumeAudioInputStream);
    QSignalSpy suspended(w.get(),&MainWindow::suspendAudioInputStream);
    TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
    w->on_monitorButton_clicked(true);QVERIFY(w->m_monitoring && !w->m_monitoroff);
    w->setManagedMode(ManagedMode::QsoEnthusiast);resumed.clear();suspended.clear();
    w->m_delay=37;w->setManagedMode(ManagedMode::Off);
    QVERIFY(w->m_monitoring);QCOMPARE(w->m_delay,37);
    QCOMPARE(resumed.size(),0);QCOMPARE(suspended.size(),0);
    QVERIFY(!w->m_enableTx && !w->m_manualTxAuthorized);

    w->setManagedMode(ManagedMode::DxccHunter);
    w->m_transmitting=true;w->m_btxok=true;g_iptt=1;w->transmitDisplay(true);
    QVERIFY(!w->m_monitoring);QVERIFY(!w->m_monitoroff);resumed.clear();
    w->ptt0Timer.start(500);w->ptt1Timer.start(500);
    w->setManagedMode(ManagedMode::Off);
    QVERIFY(w->m_monitoring);QVERIFY(w->ui->monitorButton->isChecked());QCOMPARE(resumed.size(),1);
    QVERIFY(!w->ptt0Timer.isActive() && !w->ptt1Timer.isActive());
    QVERIFY(!w->m_transmitting && !w->m_enableTx && !w->m_manualTxAuthorized);QCOMPARE(g_iptt,0);
    QTest::qWait(250);QVERIFY(w->m_monitoring);

    // A user who had explicitly stopped reception must remain stopped,
    // including when a delayed PTT-off acknowledgement arrives.
    w->on_monitorButton_clicked(false);w->setManagedMode(ManagedMode::DxccHunter);
    w->m_transmitting=true;w->m_btxok=true;g_iptt=1;resumed.clear();
    w->setManagedMode(ManagedMode::Off);w->stopTx2();
    QVERIFY(!w->m_monitoring);QVERIFY(w->m_monitoroff);QCOMPARE(resumed.size(),0);
    w->on_monitorButton_clicked(true);w->on_stopButton_clicked();w->stopTx2();
    QVERIFY(!w->m_monitoring);QVERIFY(w->m_monitoroff);
    // TCI resumes capture in the queued PTT-off callback. Cancelling its
    // zero-delay timer must not discard that pending receive restoration.
    w->on_monitorButton_clicked(true);w->m_tci=true;
    w->m_transmitting=true;w->m_btxok=true;g_iptt=1;w->transmitDisplay(true);
    w->stopTx();QVERIFY(w->ptt0Timer.isActive());QVERIFY(!w->m_monitoring);QCOMPARE(g_iptt,0);
    w->setManagedMode(ManagedMode::Off);
    QVERIFY(w->m_monitoring);QVERIFY(!w->ptt0Timer.isActive());w->m_tci=false;
    // JTTY has its own immediate cancellation path when Enable TX is cleared.
    w->m_mode="JTTY";w->on_monitorButton_clicked(true);
    w->m_transmitting=true;w->m_btxok=true;g_iptt=1;w->transmitDisplay(true);resumed.clear();
    w->on_enableTxButton_clicked(false);
    QVERIFY(w->m_monitoring);QCOMPARE(resumed.size(),1);QCOMPARE(g_iptt,0);
    w->m_mode=w->m_modeTx="FT8";
    QCOMPARE(ptt.pttOnCount(),0);
  }
  void permanentBlacklistHistoryAndControl() {
    w->on_stopTxButton_clicked();
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_logLoading && !w->m_logImportActive && !w->m_logRefreshPending,20000);
    QVERIFY(w->m_blacklist && w->m_blacklist->ready());
    const auto original=w->m_blacklist->entries();
    struct Restore {std::function<void()> run;~Restore(){run();}} restore{[&]{
      w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);
      w->m_mode=w->m_modeTx="FT8";w->m_diskData=false;
      w->m_blacklist->save(original);w->clearDX(QString());
      w->ui->decodedTextBrowser->clear();w->ui->decodedTextBrowser2->clear();
      w->m_mcpDecodes=QJsonArray{};w->m_mcpEvents=QJsonArray{};w->m_qsoHistory.init();w->m_qsoHistory2.init();
    }};
    QVERIFY(w->m_blacklist->save({}));
    w->m_mode=w->m_modeTx="FT8";w->m_TRperiod=15;w->m_diskData=false;w->m_manualDecode=false;
    w->m_autoseq=false;w->m_autoTx=false;w->setManagedMode(ManagedMode::Off);
    w->clearDX(QString());w->m_qsoHistory.init();w->m_qsoHistory.owndata("NA","K","AA00",false);
    w->ui->decodedTextBrowser->clear();w->ui->decodedTextBrowser2->clear();
    managedDecode("CQ K1ABC FN20");
    QVERIFY(w->m_qsoHistory.calls().contains("K1ABC"));
    w->ui->decodedTextBrowser->appendText("CQ K2BBB FN31");
    w->ui->decodedTextBrowser2->appendText("N0CALL K1ABC RR73");
    w->ui->decodedTextBrowser2->appendText("N0CALL K2BBB -12");
    w->m_mcpDecodes=QJsonArray{QJsonObject{{"id",1},{"message","N0CALL K1ABC -12"}},
                              QJsonObject{{"id",2},{"message","CQ K2BBB FN31"}}};
    w->m_mcpEvents=QJsonArray{QJsonObject{{"id",1},{"call","K1ABC"}},QJsonObject{{"id",2},{"call","K2BBB"}}};
    w->m_hisCall="K1ABC";w->ui->dxCallEntry->setText("K1ABC");
    manualEnable();QVERIFY(w->m_manualTxAuthorized);
    QVERIFY(w->m_blacklist->save({{"K1ABC","Synthetic regression entry"},{"K3CCC/P","Exact portable call"}}));
    QVERIFY(!w->m_enableTx && !w->m_manualTxAuthorized);QCOMPARE(g_iptt,0);QVERIFY(w->m_hisCall.isEmpty());
    for(auto browser:{w->ui->decodedTextBrowser,w->ui->decodedTextBrowser2}) {
      QVERIFY(!browser->toPlainText().contains("K1ABC"));QVERIFY(browser->toPlainText().contains("K2BBB"));
    }
    QVERIFY(!w->m_qsoHistory.calls().contains("K1ABC"));
    QCOMPARE(w->m_mcpDecodes.size(),1);QCOMPARE(w->m_mcpEvents.size(),1);
    QCOMPARE(w->m_mcpEvents.first().toObject().value("call").toString(),QString("K2BBB"));

    for(auto message:{"CQ K1ABC FN20","N0CALL K1ABC -12","N0CALL K1ABC R-12",
                      "N0CALL K1ABC RRR","N0CALL K1ABC RR73","N0CALL K1ABC 73",
                      "EA8/K1ABC/P K2BBB -10","CQ <EA8/K1ABC/P> FN20","CQ K3CCC/P FN20"})
      QVERIFY2(w->messageBlacklisted(message),message);
    for(auto message:{"CQ K1ABC2 FN20","CQ K3CCC FN20","CQ EA8/K3CCC/P FN20"})
      QVERIFY2(!w->messageBlacklisted(message),message);

    // Actual JTTY receive rendering must suppress direct replies and previews too.
    w->m_mode="JTTY";w->m_jttyLive->clear();w->ui->decodedTextBrowser->clear();
    w->m_mcpDecodes=QJsonArray{};
    for(auto message:{"N0CALL K1ABC -12","N0CALL K1ABC RR73","CQ EA8/K1ABC/P FN20"})
      w->receiveJtty({{"text",message},{"terminal",1},{"seconds",1.0},{"frequency",1500},{"snr",-10}});
    w->receiveJtty({{"text","CQ K1A"},{"terminal",0}});
    QVERIFY(w->ui->decodedTextBrowser->toPlainText().isEmpty());QVERIFY(w->m_jttyLive->text().isEmpty());
    QVERIFY(w->m_mcpDecodes.isEmpty());
    w->receiveJtty({{"text","CQ K2BBB FN31"},{"terminal",1},{"seconds",1.0},{"frequency",1500},{"snr",-10}});
    QVERIFY(w->ui->decodedTextBrowser->toPlainText().contains("K2BBB"));QCOMPARE(w->m_mcpDecodes.size(),1);

    w->m_mode=w->m_modeTx="FT8";w->setManagedMode(ManagedMode::QsoEnthusiast);
    DecodedText direct("000015 -10  0.1 1700 ~ N0CALL K1ABC -12");
    w->observeManagedDecode(direct);
    QVERIFY(w->m_managed.candidates(w->m_managedClock.elapsed(),32000).isEmpty());
    // A candidate cached before an external update must also be rejected at selection.
    ManagedCandidate cached;cached.call="K1ABC";cached.grid="FN20";cached.mode="FT8";
    cached.available=true;cached.seen=w->m_managedClock.elapsed();w->m_managed.observe(cached);
    QVERIFY(w->chooseManagedCandidate().call.isEmpty());
    QVERIFY(w->mcpCall("set_target",{{"callsign","K1ABC"}}).value("isError").toBool());
    QVERIFY(w->mcpCall("set_target",{{"callsign","EA8/K1ABC/P"}}).value("isError").toBool());
    const QString line="000015 -10  0.1 1700 ~ N0CALL K1ABC -12";
    w->processMessage(line,line.size(),false,false);QVERIFY(w->m_hisCall.isEmpty());
    // Keep the model and editor synchronized so clearDX() in Restore emits
    // the normal textChanged signal and does not leak a target to later tests.
    w->ui->dxCallEntry->setText("K1ABC");QVERIFY(!w->managedTxAllowed());
  }
  void permanentBlacklistChecksActualTransmitBody() {
    w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_logLoading && !w->m_logImportActive && !w->m_logRefreshPending,20000);
    QVERIFY(w->m_blacklist && w->m_blacklist->ready());
    const auto original=w->m_blacklist->entries();
    struct Restore {std::function<void()> run;~Restore(){run();}} restore{[&]{
      w->on_stopTxButton_clicked();w->m_jttyPendingText.clear();w->m_mode=w->m_modeTx="FT8";
      w->m_blacklist->save(original);w->clearDX(QString());w->m_ntx=6;
    }};
    QVERIFY(w->m_blacklist->save({}));w->clearDX(QString());QVERIFY(w->m_hisCall.isEmpty());
    w->m_mode=w->m_modeTx="FT8";w->m_tci=false;w->m_autoseq=false;w->m_autoTx=false;
    TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
    QSignalSpy pcm(w.get(),&MainWindow::sendJttyPcm);
    const QString blocked="K1ABC N0CALL -10",safe="CQ N0CALL AA00";
    const QList<Blacklist::Entry> entries{{"K1ABC","Actual transmit body regression"}};

    // A historical message and an unselected template must not stop safe CQ.
    w->ui->tx6->setText(safe);w->ui->freeTextMsg->setCurrentText(blocked);w->m_ntx=6;
    w->m_currentMessage=blocked;manualEnable();QVERIFY(w->m_enableTx);
    QVERIFY(w->m_blacklist->save(entries));QVERIFY(w->m_enableTx);
    QVERIFY(w->managedTxAllowed());QCOMPARE(w->ui->freeTextMsg->currentText(),blocked);
    // Changing the selected body after Enable TX must still fail before PTT.
    w->m_ntx=8;QVERIFY(!w->managedTxAllowed());w->guiUpdate();
    QVERIFY(!w->m_enableTx && !w->m_manualTxAuthorized);QCOMPARE(g_iptt,0);
    QVERIFY(w->ui->freeTextMsg->currentText().isEmpty());
    w->ui->freeTextMsg->setCurrentText(blocked);manualEnable();
    QVERIFY(!w->m_enableTx && !w->m_manualTxAuthorized);

    // Updating the blacklist during a prepared/on-air transmission checks its
    // snapshot even when the editor has moved on to a different safe message.
    QVERIFY(w->m_blacklist->save({}));w->m_ntx=6;manualEnable();
    w->m_currentMessage=blocked;g_iptt=1;w->m_tx_when_ready=true;
    QVERIFY(w->m_blacklist->save(entries));QCOMPARE(g_iptt,0);
    QVERIFY(!w->m_tx_when_ready && !w->m_enableTx);QVERIFY(w->m_currentMessage.isEmpty());
    QCOMPARE(w->ui->tx6->text(),safe);

    // Exercise each JTTY asynchronous boundary using bookkeeping and an
    // injected completion, with no encoder process, audio or PTT assertion.
    w->m_mode=w->m_modeTx="JTTY";w->m_jttyText->setText(safe);manualEnable();
    QVERIFY(w->m_enableTx);w->m_jttyText->setText(blocked);w->requestJttyTransmit();
    QVERIFY(!w->m_jttyPreparing && !w->m_enableTx);QCOMPARE(g_iptt,0);
    QVERIFY(w->m_jttyText->text().isEmpty());
    w->m_jttyText->setText(safe);manualEnable();
    w->m_jttyTxContext=w->m_config.my_callsign()+"|"+QString::number(w->m_freqNominal)+"|"+QString::number(w->ui->TxFreqSpinBox->value());
    w->m_jttyPreparing=true;w->m_jttyPendingText=blocked;
    w->m_jtty->encoded(blocked,QByteArray(8,'\0'));
    QVERIFY(!w->m_jttyPreparing && !w->m_enableTx);QCOMPARE(g_iptt,0);
    QVERIFY(w->m_jttyPendingText.isEmpty());QCOMPARE(w->m_jttyText->text(),safe);
    manualEnable();w->m_jttyPcm=QByteArray(8,'\0');w->m_currentMessage=blocked;g_iptt=1;
    w->startJttyAudio();QVERIFY(!w->m_transmitting && !w->m_enableTx);QCOMPARE(g_iptt,0);
    QVERIFY(w->m_jttyPcm.isEmpty());QCOMPARE(pcm.size(),0);
    QVERIFY(w->m_blacklist->save({}));manualEnable();
    w->m_jttyPreparing=true;w->m_jttyPendingText=blocked;
    QVERIFY(w->m_blacklist->save(entries));
    QVERIFY(!w->m_jttyPreparing && !w->m_enableTx);QVERIFY(w->m_jttyPendingText.isEmpty());
    QCOMPARE(w->m_jttyText->text(),safe);
    QCOMPARE(ptt.pttOnCount(),0);
  }
  void permanentBlacklistFiltersRealFt8Decoder() {
    w->on_stopTxButton_clicked();
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_logLoading && !w->m_logImportActive && !w->m_logRefreshPending,20000);
    QVERIFY(w->m_blacklist && w->m_blacklist->ready());
    const auto original=w->m_blacklist->entries();
    struct Restore {std::function<void()> run;~Restore(){run();}} restore{[&]{
      w->on_stopTxButton_clicked();w->m_diskData=false;w->m_blacklist->save(original);
      w->ui->decodedTextBrowser->clear();w->ui->decodedTextBrowser2->clear();w->m_mcpDecodes=QJsonArray{};
    }};
    QVERIFY(w->m_blacklist->save({}));w->setManagedMode(ManagedMode::Off);
    w->m_autoseq=false;w->m_autoTx=false;w->on_actionFT8_triggered();stopWaveAudio();
    w->m_path=QCoreApplication::applicationDirPath()+"/test-ft8.wav";QVERIFY(QFile::exists(w->m_path));
    w->m_mcpDecodes=QJsonArray{};w->m_diskData=true;w->read_wav_file(w->m_path);
    QTRY_VERIFY_WITH_TIMEOUT(w->ui->decodedTextBrowser->toPlainText().contains("CQ K1ABC FN20"),20000);
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_decoderBusy,20000);QVERIFY(!w->m_mcpDecodes.isEmpty());
    QVERIFY(w->m_blacklist->save({{"K1ABC","Decoder pipeline test"}}));
    QVERIFY(!w->ui->decodedTextBrowser->toPlainText().contains("K1ABC"));QVERIFY(w->m_mcpDecodes.isEmpty());
    QSignalSpy loaded(&w->m_wav_future_watcher,&QFutureWatcher<void>::finished);
    w->read_wav_file(w->m_path);QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,20000);
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_decoderBusy,20000);
    QVERIFY(!w->ui->decodedTextBrowser->toPlainText().contains("K1ABC"));
    QVERIFY(!w->ui->decodedTextBrowser2->toPlainText().contains("K1ABC"));
    QVERIFY(w->m_mcpDecodes.isEmpty());QVERIFY(!w->m_enableTx);QCOMPARE(g_iptt,0);
  }
  void permanentBlacklistOperatorIsolationAndUnconfiguredListener() {
    w->on_stopTxButton_clicked();
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_logLoading && !w->m_logImportActive && !w->m_logRefreshPending,20000);
    const auto originalCall=w->m_config.my_callsign();
    QVERIFY(originalCall=="N0CALL");QVERIFY(w->m_blacklist && w->m_blacklist->ready());
    const auto original=w->m_blacklist->entries();
    Blacklist other;QVERIFY(other.load("N1CALL/P",OperatorLog::directory("N1CALL/P")));
    const auto otherOriginal=other.entries();
    struct Restore {std::function<void()> run;~Restore(){run();}} restore{[&]{
      w->on_stopTxButton_clicked();w->m_config.select_station(originalCall);w->applyStationIdentity();w->updateStationSelector();
      w->m_blacklist->save(original);other.reload();other.save(otherOriginal);
      w->m_mode=w->m_modeTx="FT8";w->ui->decodedTextBrowser->clear();w->m_mcpDecodes=QJsonArray{};
    }};
    QVERIFY(w->m_blacklist->save({{"K1ABC","First operator"}}));
    QVERIFY(other.save({{"K2BBB","Second operator"}}));
    QVERIFY(w->m_config.select_station("N1CALL/P"));w->applyStationIdentity();
    QVERIFY(!w->messageBlacklisted("CQ K1ABC FN20"));QVERIFY(w->messageBlacklisted("CQ K2BBB FN31"));
    QVERIFY(w->m_config.select_station(originalCall));w->applyStationIdentity();
    QVERIFY(w->messageBlacklisted("CQ K1ABC FN20"));QVERIFY(!w->messageBlacklisted("CQ K2BBB FN31"));

    auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
    QTimer::singleShot(0,dialog,[dialog]{
      auto selector=dialog->findChild<QComboBox*>("station_profile_selector");QVERIFY(selector);
      selector->setCurrentIndex(-1);QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });
    QCOMPARE(w->m_config.exec(),int(QDialog::Accepted));QVERIFY(w->m_config.my_callsign().isEmpty());
    w->applyStationIdentity();QVERIFY(!w->messageBlacklisted("CQ K1ABC FN20"));
    w->m_mode="JTTY";w->ui->decodedTextBrowser->clear();w->m_jttyLive->clear();
    w->receiveJtty({{"text","CQ K1ABC FN20"},{"terminal",1},{"seconds",1.0},{"frequency",1500},{"snr",-10}});
    QVERIFY(w->ui->decodedTextBrowser->toPlainText().contains("CQ K1ABC FN20"));
    w->m_mode=w->m_modeTx="FT8";manualEnable();QVERIFY(!w->m_manualTxAuthorized);QVERIFY(!w->m_enableTx);QCOMPARE(g_iptt,0);
  }
  void logProcessingDialogAndResponsiveness() {
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_logLoading && !w->m_logImportActive,15000);
    auto directory=OperatorLog::directory(w->m_config.my_callsign());
    QLockFile held(directory.filePath(".operator-log.lock"));QVERIFY(held.tryLock());
    QString source=temp.path()+"/processing.adi";QFile file(source);QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("<CALL:5>K9ZZZ<QSO_DATE:8>20260101<TIME_ON:6>123456<MODE:3>FT8<BAND:3>20m<EOR>\n");file.close();
    int ticks=0;QTimer timer;connect(&timer,&QTimer::timeout,this,[&]{++ticks;});timer.start(5);
    w->importOperatorLogs({source});
    QTRY_VERIFY_WITH_TIMEOUT(w->m_logProcessingDialog && w->m_logProcessingDialog->isVisible(),2000);
    QTest::qWait(70);QVERIFY(ticks>3);
    auto output=qEnvironmentVariable("JTDX_FM_QA_DIR");if(!output.isEmpty()){QDir().mkpath(output);w->m_logProcessingDialog->grab().save(output+"/log-processing.png");}
    held.unlock();
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_logImportActive && !w->m_logLoading && !w->m_logRefreshPending,20000);
    QVERIFY(!w->m_logProcessingDialog);QVERIFY(file.exists());
    w->refreshOperatorLogs(true);
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_logLoading && !w->m_logRefreshPending,15000);QVERIFY(!w->m_logProcessingDialog);
    QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));file.write("<CALL:999>broken");file.close();
    w->importOperatorLogs({source});QTRY_VERIFY_WITH_TIMEOUT(!w->m_logImportActive && !w->m_logLoading && !w->m_logRefreshPending,20000);
    QVERIFY(!w->m_logProcessingDialog);QVERIFY(file.exists());
  }
  void minimizeRestoreDoesNotReloadOrReshowFinishedProgress() {
    w->on_stopTxButton_clicked();
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_logLoading && !w->m_logImportActive && !w->m_logRefreshPending && !w->m_logRefreshTimer.isActive(),20000);
    QVERIFY(!w->m_logProcessingDialog);
    QSignalSpy finished(&w->m_logLoadWatcher,&QFutureWatcher<MainWindow::LogSnapshot>::finished);
    bool wasVisible=w->isVisible();w->showNormal();QCoreApplication::processEvents();
    w->showMinimized();QTest::qWait(100);w->showNormal();QTest::qWait(250);
    QCOMPARE(finished.count(),0);QVERIFY(!w->m_logProcessingDialog);QVERIFY(!w->m_optimizingProgress.isVisible());

    // Keep a real worker running across minimize/restore, then finish it while
    // minimized. A subsequent restore must not resurrect the old modal window.
    QLockFile held(OperatorLog::directory(w->m_config.my_callsign()).filePath(".operator-log.lock"));QVERIFY(held.tryLock());
    w->refreshOperatorLogs(true);
    QVERIFY(!w->m_logProcessingDialog); // no immediate flash for a cached check
    manualEnable();QVERIFY(!w->m_enableTx && !w->m_manualTxAuthorized);
    QTest::mouseClick(w->ui->tuneButton,Qt::LeftButton);QVERIFY(!w->m_tune && !w->m_manualTuneAuthorized);
    QTRY_VERIFY_WITH_TIMEOUT(w->m_logProcessingDialog && w->m_logProcessingDialog->isVisible(),2000);
    QPointer<ProcessingDialog> previous=w->m_logProcessingDialog;
    w->showMinimized();QCoreApplication::processEvents();QVERIFY(!w->m_logProcessingDialog->isVisible());
    w->showNormal();QTRY_VERIFY_WITH_TIMEOUT(w->m_logProcessingDialog->isVisible(),2000);
    QCOMPARE(finished.count(),0); // restoring did not create another load
    w->showMinimized();held.unlock();
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_logLoading && !w->m_logRefreshPending && !w->m_logRefreshTimer.isActive(),20000);
    QTRY_VERIFY(previous.isNull());QVERIFY(!w->m_logProcessingDialog);
    int completed=finished.count();QVERIFY(completed>=1);
    w->showNormal();QTest::qWait(250);
    QCOMPARE(finished.count(),completed);QVERIFY(!w->m_logProcessingDialog);QVERIFY(!w->m_optimizingProgress.isVisible());
    if(!wasVisible)w->hide();
  }
  void manualTxGate() {
    w->on_stopTxButton_clicked();
    QVERIFY(!w->m_manualTxAuthorized);
    TransceiverCommandSpy ptt(w.get());QVERIFY(ptt.isValid());
    w->enableTx_mode(true); w->ui->enableTxButton->click(); w->TxAgain();
    QVERIFY(!w->m_enableTx); QVERIFY(!w->m_manualTxAuthorized);
    QVERIFY(w->mcpCall("start_tx",{}).value("isError").toBool());
    w->startTx2();
    QVERIFY(!w->m_transmitting); QCOMPARE(g_iptt,0);
    QCOMPARE(ptt.pttOnCount(),0);
    manualEnable(); QVERIFY(w->m_manualTxAuthorized); QVERIFY(w->m_enableTx);
    auto selector=w->findChild<QComboBox*>("station_profile_selector");
    QVERIFY(selector); QVERIFY(!selector->isEnabled());
    w->TxAgainTimer.start(1); w->enableTxButtonTimer.start(1); w->ptt1Timer.start(1);
    w->on_stopTxButton_clicked();
    QVERIFY(!w->TxAgainTimer.isActive()); QVERIFY(!w->enableTxButtonTimer.isActive()); QVERIFY(!w->ptt1Timer.isActive());
    w->TxAgain(); w->startTx2(); w->on_tuneButton_clicked(true);
    QVERIFY(!w->m_enableTx); QVERIFY(!w->m_tune); QVERIFY(!w->m_manualTxAuthorized); QCOMPARE(g_iptt,0);
    QVERIFY(selector->isEnabled());
  }
  void quickStationSwitchAndLogRefresh() {
    w->on_stopTxButton_clicked();
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_logLoading && !w->m_logImportActive && !w->m_logRefreshPending && !w->m_logRefreshTimer.isActive(),20000);
    QCOMPARE(w->ui->tabWidget->count(),3);
    QCOMPARE(w->ui->tabWidget->tabText(2),QString("3"));
    QVERIFY(w->m_quickStationSelector->findText("N1CALL/P")>=0);
    QVERIFY(!w->m_confirmStationSwitchButton->isEnabled());
    w->m_quickStationSelector->setCurrentText("N1CALL/P");
    QMetaObject::invokeMethod(w->m_quickStationSelector,"activated",Qt::DirectConnection,Q_ARG(int,w->m_quickStationSelector->currentIndex()));
    QCOMPARE(w->m_config.my_callsign(),QString("N0CALL"));
    QCOMPARE(w->m_quickStationGrid->text(),QString("AA00"));
    QCOMPARE(settings.value("Configuration/MyCall").toString(),QString("N0CALL"));
    QVERIFY(w->m_confirmStationSwitchButton->isEnabled());
    // The operator page preserves the previous message panel's TX semantics.
    for(int panel:{0,1}) {
      w->ui->tabWidget->setCurrentIndex(panel);w->ui->tabWidget->setCurrentIndex(2);
      QCOMPARE(w->txTabIndex(),panel);
      w->clearDX(QString());QCOMPARE(w->m_ntx,panel==0?6:7);
    }
    w->ui->tabWidget->setCurrentIndex(0);w->ui->tabWidget->setCurrentIndex(2);
    w->writeSettings();w->readSettings();
    QCOMPARE(w->ui->tabWidget->currentIndex(),2);QCOMPARE(w->txTabIndex(),0);
    QSignalSpy finished(&w->m_logLoadWatcher,&QFutureWatcher<MainWindow::LogSnapshot>::finished);
    w->refreshOperatorLogs(true);w->refreshOperatorLogs(true);
    QVERIFY(!w->m_logRefreshPending);
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_logLoading,15000);QTest::qWait(500);
    QCOMPARE(finished.count(),1);QVERIFY(w->m_logLoadWatcher.result().cacheHit);
    // Accepting unrelated settings must not start another complete load.
    auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
    QTimer::singleShot(0,dialog,[dialog]{QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);});
    w->on_actionSettings_triggered();QTest::qWait(500);QCOMPARE(finished.count(),1);
    manualEnable();QVERIFY(!w->m_quickStationSelector->isEnabled());
    QVERIFY(!w->m_confirmStationSwitchButton->isEnabled());
    w->m_confirmStationSwitchButton->click();
    QCOMPARE(w->m_config.my_callsign(),QString("N0CALL"));
    QVERIFY(!w->m_config.select_station("N1CALL/P"));
    w->switchStation("N1CALL/P");QCOMPARE(w->m_config.my_callsign(),QString("N0CALL"));
    w->on_stopTxButton_clicked();QVERIFY(w->m_quickStationSelector->isEnabled());
    w->m_quickStationSelector->setCurrentText("N1CALL/P");
    QVERIFY(w->m_confirmStationSwitchButton->isEnabled());
    w->m_confirmStationSwitchButton->click();
    QCOMPARE(w->m_config.my_callsign(),QString("N1CALL/P"));QCOMPARE(w->m_config.my_grid(),QString("FN20"));
    QCOMPARE(w->m_config.region(),IARURegions::Region(2));QCOMPARE(w->m_config.type_2_msg_gen(),Configuration::type_2_msg_1_full);
    QVERIFY(w->ui->tx6->text().contains("N1CALL/P"));
    QVERIFY(!w->m_manualTxAuthorized && !w->m_enableTx);QCOMPARE(g_iptt,0);
    QVERIFY(!w->m_confirmStationSwitchButton->isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_logLoading && !w->m_logRefreshPending && !w->m_logRefreshTimer.isActive(),20000);
    QCOMPARE(w->m_logLoadWatcher.result().call,QString("N1CALL/P"));
    w->m_quickStationSelector->setCurrentText("N0CALL");
    QCOMPARE(w->m_config.my_callsign(),QString("N1CALL/P"));
    w->m_confirmStationSwitchButton->click();
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_logLoading && !w->m_logRefreshPending && !w->m_logRefreshTimer.isActive(),20000);
    QVERIFY(w->m_logLoadWatcher.result().cacheHit);
    QCOMPARE(w->m_quickStationSelector->currentText(),QString("N0CALL"));
    QCOMPARE(settings.value("Configuration/MyCall").toString(),QString("N0CALL"));
    QVERIFY(!w->m_enableTx);w->ui->tabWidget->setCurrentIndex(0);
  }
  void profilesAndQuarterBoundaries() {
    StationProfile a; a.call=" n0call/qrp ";a.grid="AA00";
    QVERIFY(StationProfiles::validate(a).isEmpty());
    QCOMPARE(StationProfiles::folder(a.call),QString("N0CALL_QRP"));
    auto list=StationProfiles::decode(StationProfiles::encode({a,a})); QCOMPARE(list.size(),1);
    StationProfile b=a;b.call="N0CALL/P";
    QCOMPARE(StationProfiles::decode(StationProfiles::encode({a,b})).size(),2);
    a.call="../N0CALL"; QVERIFY(!StationProfiles::validate(a).isEmpty());
    a.call="COM1"; QVERIFY(!StationProfiles::validate(a).isEmpty());
    QDir base(temp.path()+"/quarters");
    for(auto date: {QDate(2025,12,31),QDate(2026,1,1),QDate(2026,3,31),QDate(2026,4,1),QDate(2026,7,1),QDate(2026,10,1)}) {
      QString path=OperatorLog::quarterFile("N0CALL/QRP",date,base);
      QCOMPARE(QFileInfo(path).fileName(),QString("N0CALL_QRP%1S%2.adi").arg(date.year()).arg((date.month()-1)/3+1));
      QVERIFY(QFileInfo(path).dir().exists());
    }
  }
  void profileManagerEditingKeepsLogs() {
    QDir base(temp.path()+"/manager");auto dir=OperatorLog::directory("N0CALL/P",base);
    QFile keep(dir.filePath("keep.adi"));QVERIFY(keep.open(QIODevice::WriteOnly));keep.write("private synthetic log marker");keep.close();
    StationProfile initial;initial.call="N0CALL";initial.grid="AA00";
    StationManager manager({initial});manager.show();
    auto edit=[&](QString call,QString grid,int action) {
      QTimer::singleShot(0,&manager,[&,call,grid] {
        auto dialog=manager.findChild<QDialog*>("station_editor");
        if(!dialog) return;
        dialog->findChild<QLineEdit*>("profile_call")->setText(call);
        dialog->findChild<QLineEdit*>("profile_grid")->setText(grid);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
      });
      manager.findChild<QPushButton*>(QString("station_action_%1").arg(action))->click();
    };
    edit("n0call/p","FN20",0);QCOMPARE(manager.profiles().size(),2);QCOMPARE(manager.profiles()[1].call,QString("N0CALL/P"));
    edit("N0CALL/QRP","FN21",1);QCOMPARE(manager.profiles()[1].call,QString("N0CALL/QRP"));QCOMPARE(manager.profiles()[1].grid,QString("FN21"));
    manager.findChild<QPushButton*>("station_action_2")->click();QCOMPARE(manager.profiles().size(),1);
    QVERIFY(keep.exists());QCOMPARE(OperatorLog::directory("N0CALL/P",base).absolutePath(),dir.absolutePath());
  }
  void quarterlyImportPreservesAndDeduplicates() {
    QDir base(temp.path()+"/import");QString source=temp.path()+"/foreign.adi";
    OperatorLog::Record a{{"CALL","K1ABC"},{"STATION_CALLSIGN","N0CALL"},{"QSO_DATE","20260331"},{"TIME_ON","2359"},{"MODE","FT8"},{"BAND","20m"},{"COMMENT","line one\n<EOR> and <QSO_DATE:8>19990101 remain field text"}};
    auto b=a;b["QSO_DATE"]="20260401";b["CALL"]="K2ABC";
    auto c=b;c["STATION_CALLSIGN"]="N0CALL/QRP";
    QFile input(source);QVERIFY(input.open(QIODevice::WriteOnly));auto bytes=(OperatorLog::serialize(a)+OperatorLog::serialize(b)+OperatorLog::serialize(c)).toUtf8();input.write(bytes);input.close();
    auto imported=OperatorLog::importFiles("N0CALL",{source},base);QVERIFY2(imported.errors.isEmpty(),qPrintable(imported.errors.join(';')));QCOMPARE(imported.imported,2);QCOMPARE(imported.skipped,1);QCOMPARE(imported.archived,1);
    QVERIFY(input.open(QIODevice::ReadOnly));QCOMPARE(input.readAll(),bytes);input.close();
    QFile archive(base.filePath("N0CALL/old/foreign.adi"));QVERIFY(archive.open(QIODevice::ReadOnly));QCOMPARE(archive.readAll(),bytes);
    QCOMPARE(OperatorLog::files("N0CALL",base).size(),2);QVERIFY(!QDir(base.filePath("N0CALL_QRP")).exists());
    auto retry=OperatorLog::importFiles("N0CALL",{source},base);QCOMPARE(retry.imported,0);QCOMPARE(retry.duplicates,2);QCOMPARE(retry.skipped,1);
    QString error;auto records=OperatorLog::read(OperatorLog::quarterFile("N0CALL",QDate(2026,3,31),base),&error);QVERIFY(error.isEmpty());QCOMPARE(records.size(),1);QCOMPARE(records[0].value("COMMENT"),a.value("COMMENT"));
    CountryDat countries;ADIF adif;adif.init(OperatorLog::files("N0CALL",base),&countries);adif.load("N0CALL","","");
    QVERIFY(adif.match("K1ABC","20m","FT8"));QVERIFY(adif.match("K2ABC","20m","FT8"));QCOMPARE(adif.getCount("FT8"),2);
    // A malformed source must remain intact and must not create an archive.
    QString bad=base.filePath("N0CALL/bad.adi");QFile broken(bad);QVERIFY(broken.open(QIODevice::WriteOnly));broken.write("<CALL:999>K1ABC<EOR>");broken.close();
    auto failure=OperatorLog::importFiles("N0CALL",{bad},base);QVERIFY(!failure.errors.isEmpty());QVERIFY(broken.exists());QVERIFY(!QFile::exists(base.filePath("N0CALL/old/bad.adi")));
  }
  void legacyImportFailureIsRetryable() {
    QDir base(temp.path()+"/retry-import");
    OperatorLog::Record a{{"CALL","k1abc"},{"QSO_DATE","20260101"},{"TIME_ON","010203"},{"MODE","FT8"},{"FREQ","14.074"}};
    auto b=a;b["QSO_DATE"]="20260401";
    QString source=temp.path()+"/retry.adi";QFile file(source);QVERIFY(file.open(QIODevice::WriteOnly));file.write((OperatorLog::serialize(a)+OperatorLog::serialize(b)).toUtf8());file.close();
    QString blocked=OperatorLog::quarterFile("N0CALL",QDate(2026,4,1),base);QVERIFY(QDir().mkpath(blocked));
    auto failed=OperatorLog::importFiles("N0CALL",{source},base);QVERIFY(!failed.errors.isEmpty());QVERIFY(file.exists());QVERIFY(QFile::exists(base.filePath("N0CALL/old/retry.adi")));
    QVERIFY(QDir().rmdir(blocked));auto retry=OperatorLog::importFiles("N0CALL",{source},base);QVERIFY(retry.errors.isEmpty());QCOMPARE(retry.imported,1);QCOMPARE(retry.duplicates,1);QCOMPARE(retry.archived,1);
    CountryDat countries;ADIF history;history.init(OperatorLog::files("N0CALL",base),&countries);history.load("N0CALL","","");
    QVERIFY(history.match("K1ABC","20m","FT8"));
    QString csv=temp.path()+"/legacy.log";QFile old(csv);QVERIFY(old.open(QIODevice::WriteOnly));old.write("2026-01-01,01:02:03,2026-01-01,01:03:04,K1ABC,FN20,14.074,FT8,-10,-11,5,Hello, world,Test Name\n");old.close();
    QString error;auto rows=OperatorLog::read(csv,&error);QVERIFY(error.isEmpty());QCOMPARE(rows.size(),1);QCOMPARE(rows[0].value("COMMENT"),QString("Hello, world"));QCOMPARE(rows[0].value("NAME"),QString("Test Name"));QCOMPARE(rows[0].value("TIME_OFF"),QString("010304"));
    auto converted=OperatorLog::importFiles("N0CALL",{csv},base);QCOMPARE(converted.imported,0);QCOMPARE(converted.duplicates,1);QVERIFY(converted.errors.isEmpty());
    // A file named old must not turn failed backup creation into source deletion.
    QDir blockedBase(temp.path()+"/blocked-archive");auto dir=OperatorLog::directory("N0CALL",blockedBase);
    QFile blocker(dir.filePath("old"));QVERIFY(blocker.open(QIODevice::WriteOnly));blocker.write("keep");blocker.close();
    QFile deposited(dir.filePath("import.adi"));QVERIFY(deposited.open(QIODevice::WriteOnly));auto original=OperatorLog::serialize(a).toUtf8();deposited.write(original);deposited.close();
    auto unarchived=OperatorLog::importFiles("N0CALL",{deposited.fileName()},blockedBase);QVERIFY(!unarchived.errors.isEmpty());
    QVERIFY(deposited.open(QIODevice::ReadOnly));QCOMPARE(deposited.readAll(),original);deposited.close();
  }
  void privateReferenceLog() {
    QString source=qEnvironmentVariable("JTDXF_PRIVATE_LOG");
    if(source.isEmpty()) QSKIP("Private user log is optional and is never distributed");
    QFile file(source);QVERIFY(file.open(QIODevice::ReadOnly));auto bytes=file.readAll();file.close();
    QString error;auto records=OperatorLog::read(source,&error);QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(!records.isEmpty());
    // Use the dominant station in this private fixture without disclosing it
    // in test output; records owned by other stations stay only in the backup.
    QMap<QString,int> stationCounts;for(auto const& r:records) {
      auto call=StationProfiles::normalize(r.value("STATION_CALLSIGN"));if(!call.isEmpty())++stationCounts[call];
    }
    QString active="N0CALL";int most=0;
    for(auto i=stationCounts.cbegin();i!=stationCounts.cend();++i)if(i.value()>most){active=i.key();most=i.value();}
    QDir base(temp.path()+"/private-reference");auto result=OperatorLog::importFiles(active,{source},base);QVERIFY2(result.errors.isEmpty(),"Private reference import failed (see local validation, not source contents)");
    QCOMPARE(result.imported+result.duplicates+result.skipped,records.size());
    auto retry=OperatorLog::importFiles(active,{source},base);QVERIFY(retry.errors.isEmpty());QCOMPARE(retry.imported,0);QCOMPARE(retry.duplicates+retry.skipped,records.size());
    int total=0,quarters=0;for(auto const& dir:base.entryList(QDir::Dirs|QDir::NoDotAndDotDot)) {
      for(auto const& path:OperatorLog::files(dir,base)) {++quarters;total+=OperatorLog::read(path,&error).size();QVERIFY(error.isEmpty());}
    }
    QCOMPARE(total,result.imported);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256),QCryptographicHash::hash(bytes,QCryptographicHash::Sha256));
    qInfo("Private reference: %d records, %d unique records, %d quarterly files, repeated import adds zero",records.size(),total,quarters);
  }
  void dualLogSynchronization() {
    QDir base(temp.path()+"/dual-log");QString error;
    OperatorLog::Record a{{"STATION_CALLSIGN","N0CALL"},{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","0102"},{"MODE","FT8"},{"FREQ","14.074"},{"RST_SENT","-10"}};
    auto b=a;b["CALL"]="K2BBB";b["QSO_DATE"]="20260402";
    auto c=a;c["CALL"]="K3CCC";c["QSO_DATE"]="20260703";
    auto write=[](QString path,QString body){QFile f(path);return f.open(QIODevice::WriteOnly) && f.write(body.toUtf8())==body.toUtf8().size();};
    auto pa=OperatorLog::quarterFile("N0CALL",QDate(2026,1,1),base);
    auto pb=OperatorLog::quarterFile("N0CALL",QDate(2026,4,2),base);
    QVERIFY(write(pa,OperatorLog::serialize(a)));QVERIFY(write(pb,OperatorLog::serialize(b)));
    auto migrated=OperatorLog::synchronize("N0CALL",base);QVERIFY(migrated.errors.isEmpty());
    auto full=OperatorLog::compatibilityFile("N0CALL",base);
    QCOMPARE(OperatorLog::read(full,&error).size(),2);QVERIFY(error.isEmpty());
    // Complete log has one extra QSO, a duplicate represented differently, and
    // a missing field to fill. The quarter has a QSO absent from the full copy.
    auto duplicate=a;duplicate["TIME_ON"]="010200";duplicate["BAND"]="20m";duplicate["NAME"]="Alice";duplicate["RST_SENT"]="-19";
    QVERIFY(write(full,OperatorLog::serialize(duplicate)+OperatorLog::serialize(duplicate)+OperatorLog::serialize(c)));
    auto synced=OperatorLog::synchronize("N0CALL",base);QVERIFY2(synced.errors.isEmpty(),qPrintable(synced.errors.join(';')));QCOMPARE(synced.imported,1);
    auto complete=OperatorLog::read(full,&error);QCOMPARE(complete.size(),3);
    QList<OperatorLog::Record> quarters;for(auto const& path:OperatorLog::files("N0CALL",base))quarters+=OperatorLog::read(path,&error);
    QCOMPARE(quarters,complete);QCOMPARE(complete[0].value("NAME"),QString("Alice"));QCOMPARE(complete[0].value("RST_SENT"),QString("-10"));
    QCOMPARE(OperatorLog::files("N0CALL",base).size(),3);
    QVERIFY(!QFile::exists(base.filePath("N0CALL/old/wsjtx_log.adi")));
    auto stamp=QFileInfo(full).lastModified();auto retry=OperatorLog::synchronize("N0CALL",base);
    QVERIFY(retry.errors.isEmpty());QCOMPARE(retry.imported,0);QCOMPARE(QFileInfo(full).lastModified(),stamp);
    // A newly logged QSO is immediately available in both copies.
    auto d=c;d["CALL"]="K4DDD";
    QVERIFY2(OperatorLog::append(OperatorLog::quarterFile("N0CALL",QDate(2026,7,3),base),OperatorLog::serialize(d),&error),qPrintable(error));
    QCOMPARE(OperatorLog::read(full,&error).size(),4);
    QCOMPARE(OperatorLog::read(OperatorLog::quarterFile("N0CALL",QDate(2026,7,3),base),&error).size(),2);
    auto imported=OperatorLog::importFiles("N0CALL",{full},base);QVERIFY(imported.errors.isEmpty());QCOMPARE(imported.imported,0);QCOMPARE(imported.archived,0);QVERIFY(QFile::exists(full));
    // A malformed complete copy must not damage either original or quarters.
    QVERIFY(write(full,"<CALL:999>broken"));QFile before(pa);QVERIFY(before.open(QIODevice::ReadOnly));auto bytes=before.readAll();before.close();
    QVERIFY(!OperatorLog::synchronize("N0CALL",base).errors.isEmpty());
    QVERIFY(before.open(QIODevice::ReadOnly));QCOMPARE(before.readAll(),bytes);
    QFile broken(full);QVERIFY(broken.open(QIODevice::ReadOnly));QCOMPARE(broken.readAll(),QByteArray("<CALL:999>broken"));
  }
  void dualLogFailureRecovery() {
    QDir base(temp.path()+"/dual-failure");QString error;
    OperatorLog::Record a{{"STATION_CALLSIGN","N0CALL/P"},{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","010203"},{"MODE","FT8"},{"BAND","20m"}};
    auto quarter=OperatorLog::quarterFile("N0CALL/P",QDate(2026,1,1),base);
    QVERIFY(OperatorLog::append(quarter,OperatorLog::serialize(a),&error));
    auto full=OperatorLog::compatibilityFile("N0CALL/P",base);
    QCOMPARE(OperatorLog::read(full,&error).size(),1);
    // Simulate a process stopping after only the primary append committed.
    auto b=a;b["CALL"]="K2BBB";QFile f(quarter);QVERIFY(f.open(QIODevice::Append));f.write(OperatorLog::serialize(b).toUtf8());f.close();
    QVERIFY(OperatorLog::synchronize("N0CALL/P",base).errors.isEmpty());
    QCOMPARE(OperatorLog::read(full,&error),OperatorLog::read(quarter,&error));
    // Cannot open the compatibility destination: primary must stay unchanged.
    QString backup=full+".keep";QVERIFY(QFile::rename(full,backup));QVERIFY(QDir().mkpath(full));
    auto c=a;c["CALL"]="K3CCC";QVERIFY(!OperatorLog::append(quarter,OperatorLog::serialize(c),&error));
    QCOMPARE(OperatorLog::read(quarter,&error).size(),2);
    QVERIFY(QDir().rmdir(full));QVERIFY(QFile::rename(backup,full));
    QVERIFY(OperatorLog::append(quarter,OperatorLog::serialize(c),&error));
    QCOMPARE(OperatorLog::read(full,&error).size(),3);
  }
  void mixedCompatibilityImportPreservesSource() {
    QTemporaryDir logs;QDir base(logs.path());QString error;
    auto path=OperatorLog::compatibilityFile("N0CALL",base);
    OperatorLog::Record own{{"STATION_CALLSIGN","N0CALL"},{"CALL","K1ABC"},{"QSO_DATE","20260101"},{"TIME_ON","010203"},{"MODE","FT8"},{"BAND","20m"}};
    auto other=own;other["STATION_CALLSIGN"]="N1CALL";
    auto raw=(OperatorLog::serialize(own)+OperatorLog::serialize(other)).toUtf8();
    QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));f.write(raw);f.close();
    auto result=OperatorLog::importFiles("N0CALL",{path},base);QVERIFY2(result.errors.isEmpty(),qPrintable(result.errors.join(";")));
    QCOMPARE(OperatorLog::read(path,&error).size(),1);
    QVERIFY(!QDir(base.filePath("N1CALL")).exists());QCOMPARE(result.skipped,1);
    auto backups=QDir(OperatorLog::directory("N0CALL",base).filePath("old")).entryList({"wsjtx_log-mixed-*.adi"},QDir::Files);
    QCOMPARE(backups.size(),1);QFile backup(OperatorLog::directory("N0CALL",base).filePath("old/"+backups.first()));QVERIFY(backup.open(QIODevice::ReadOnly));QCOMPARE(backup.readAll(),raw);
  }
  void watchdogBoundaries() {
    TxWatchdog wd; auto t = TxWatchdog::Time{};
    wd.start(t);
    QVERIFY(!wd.expired(1, t+std::chrono::milliseconds(59999)));
    QVERIFY(wd.expired(1, t+std::chrono::milliseconds(60000)));
    QVERIFY(!wd.expired(0, t+std::chrono::hours(10000)));
    QVERIFY(!wd.expired(999, t+std::chrono::minutes(998)));
    QVERIFY(wd.expired(999, t+std::chrono::minutes(999)));
    wd.stop(); QVERIFY(!wd.expired(1, t+std::chrono::hours(10000)));
  }
  void activityDoesNotResetWatchdog() {
    w->clearDX("");
    w->m_config.set_watchdog(1);
    manualEnable();
    w->m_txSession.start(TxWatchdog::Clock::now()-std::chrono::seconds(65));
    w->txwatchdog(false); w->setRxFreq4(1500);
    QVERIFY(w->m_txSession.expired(1));
    w->txwatchdog(true);
    QVERIFY(!w->m_enableTx); QVERIFY(w->m_txwatchdog);
    w->txwatchdog(false); QVERIFY(w->m_txwatchdog);
    manualEnable(); QVERIFY(!w->m_txwatchdog); QVERIFY(!w->m_txSession.expired(1));
    w->enableTx_mode(false);
    w->m_config.set_watchdog(999); QCOMPARE(w->m_config.watchdog(), 999);
    w->m_config.set_watchdog(0);
  }
  void watchdogFinishesCurrentQso() {
    w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);w->m_qsoHistory.init();
    w->m_autoseq=true;w->m_singleshot=false;w->m_houndMode=false;w->m_config.set_watchdog(1);
    w->ui->dxCallEntry->setText("K1AAA");w->on_txb3_clicked();
    w->m_qsoHistory.message("K1AAA",QsoHistory::RREPORT,0,"-10","","NA","K",15,"-10",1500,"FT8");
    manualEnable();w->m_txSession.start(TxWatchdog::Clock::now()-std::chrono::minutes(2));
    w->txwatchdog(true);QVERIFY(w->m_watchdogFinishing);QVERIFY(w->m_enableTx);QVERIFY(!w->m_txwatchdog);QVERIFY(w->watchdogTxAllowed());
    w->m_qsoHistory.message("K2BBB",QsoHistory::RCALL,1,"FN20","","NA","K",30,"-12",1500,"FT8");
    w->m_qsoHistory.message("K1AAA",QsoHistory::S73,0,"","","NA","K",30,"-10",1500,"FT8");
    w->m_transmitting=true;g_iptt=1;w->txwatchdog(true);w->process_Auto();
    QVERIFY(w->m_enableTx);QVERIFY(w->m_watchdogFinishing);QCOMPARE(g_iptt,1); // full last 73, never truncated
    w->logClearDX();QCOMPARE(w->m_hisCall,QString("K1AAA"));
    w->m_transmitting=false;g_iptt=0;
    w->m_qsoHistory.complete("K1AAA"); // Protocol confirmation is tested in SignoffState.
    w->process_Auto();
    QVERIFY(w->m_txwatchdog);QVERIFY(!w->m_enableTx);QVERIFY(!w->m_manualTxAuthorized);QVERIFY(!w->m_watchdogFinishing);
    QVERIFY(w->m_hisCall!="K2BBB");w->TxAgain();QVERIFY(!w->m_enableTx);
    manualEnable();QVERIFY(!w->m_txwatchdog);QVERIFY(!w->m_watchdogFinishing);w->on_stopTxButton_clicked();w->m_config.set_watchdog(0);
  }
  void watchdogManagedLimitAndManualStop() {
    QTRY_VERIFY(!w->m_logLoading && !w->m_logImportActive);saveManagedSettings(ManagedSettings{});prepareManaged(ManagedMode::QsoEnthusiast);
    managedDecode("CQ K9WDX FN31");w->process_Auto();QCOMPARE(w->m_hisCall,QString("K9WDX"));
    w->m_qsoHistory.message("K9WDX",QsoHistory::SCALL,0,"FN31","","NA","K",30,"-10",1500,"FT8");
    w->m_config.set_watchdog(1);w->m_txSession.start(TxWatchdog::Clock::now()-std::chrono::minutes(2));
    w->txwatchdog(true);QVERIFY(w->m_watchdogFinishing);
    managedDecode("CQ JA1AAA PM95");
    for(int i=0;i<3;++i){managedSentRound();}
    w->process_Auto();QVERIFY(!w->m_enableTx);QVERIFY(w->m_txwatchdog);QVERIFY(w->m_hisCall!="JA1AAA");
    prepareManaged(ManagedMode::DxccHunter);managedDecode("N0CALL K2BBB FN31");w->process_Auto();
    w->txwatchdog(true);QVERIFY(w->m_watchdogFinishing);
    w->m_transmitting=true;g_iptt=1;w->on_stopTxButton_clicked();
    QVERIFY(!w->m_transmitting);QCOMPARE(g_iptt,0);QVERIFY(!w->m_watchdogFinishing);QVERIFY(!w->m_manualTxAuthorized);
    w->setManagedMode(ManagedMode::Off);w->m_config.set_watchdog(0);w->m_singleshot=false;
  }
  void managedStatusMessageDoesNotOverlap() {
    w->statusBar()->showMessage("Managed mode is listening",15000);w->update_watchdog_label();
    QVERIFY(w->txwatchdog_label->isHidden());
    w->statusBar()->clearMessage();w->update_watchdog_label();QVERIFY(!w->txwatchdog_label->isHidden());
  }
  void continuousQso_data() {
    QTest::addColumn<int>("status"); QTest::addColumn<bool>("incoming"); QTest::addColumn<bool>("cq"); QTest::addColumn<int>("rxTime");
    for (int s : {int(QsoHistory::SRR73), int(QsoHistory::S73), int(QsoHistory::FIN)}) {
      QTest::newRow(qPrintable(QString("incoming-%1").arg(s))) << s << true << false << 15;
      QTest::newRow(qPrintable(QString("cq-%1").arg(s))) << s << false << true << 15;
      QTest::newRow(qPrintable(QString("idle-%1").arg(s))) << s << false << false << 15;
      QTest::newRow(qPrintable(QString("cq-even-%1").arg(s))) << s << false << true << 30;
    }
  }
  void continuousQso() {
    QFETCH(int, status); QFETCH(bool, incoming); QFETCH(bool, cq); QFETCH(int, rxTime);
    w->m_qsoHistory.init(); w->m_qsoHistory.owndata("NA", "K", "AA00", false);
    w->m_autoseq=true; w->m_singleshot=false; w->m_houndMode=false; w->m_callMode=2;
    w->m_callPrioCQ=false; w->m_callWorkedB4=cq; w->m_answerWorkedB4=true; w->m_lockTxFreq=true;
    w->m_counter=0; w->m_txFirst=true; w->m_msDecStarted=15000; w->m_transmitting=false; g_iptt=0;
    w->ui->dxCallEntry->setText("K1AAA");
    w->m_qsoHistory.message("K1AAA", QsoHistory::Status(status), 0, "", "", "NA", "K", 0, "-10", 1500, "FT8");
    w->m_qsoHistory.complete("K1AAA"); // This fixture tests continuation after a confirmed QSO.
    w->m_qsoHistory.time(rxTime);
    if (incoming) w->m_qsoHistory.message("K2BBB", QsoHistory::RCALL, 1, "FN20", "", "NA", "K", rxTime, "-12", 1600, "FT8");
    if (cq) w->m_qsoHistory.message("K3CCC", QsoHistory::RCQ, 1, "FN31", "", "NA", "K", rxTime, "-5", 1700, "FT8");
    manualEnable(); auto before=w->m_txSession.elapsedMs();
    w->process_Auto();
    QVERIFY(w->m_enableTx); QVERIFY(w->m_txSession.active()); QVERIFY(w->m_txSession.elapsedMs() >= before);
    QCOMPARE(w->m_hisCall, incoming ? QString("K2BBB") : cq ? QString("K3CCC") : QString());
    if (incoming || cq) QCOMPARE(w->m_txFirst, rxTime == 15);
    QCOMPARE(w->m_ntx, incoming ? 2 : cq ? (w->m_skipTx1 ? 2 : 1) : 6);
    w->enableTx_mode(false);
  }
  void singleQsoStops() {
    w->m_singleshot=true; w->m_qsoHistory.init();
    w->ui->dxCallEntry->setText("K1AAA");
    w->m_qsoHistory.message("K1AAA", QsoHistory::FIN, 0, "", "", "NA", "K", 0, "-10", 1500, "FT8");
    manualEnable(); w->process_Auto(); QVERIFY(!w->m_enableTx); w->m_singleshot=false;
  }
  void finalMessageIsNotInterrupted() {
    w->m_qsoHistory.init(); w->ui->dxCallEntry->setText("K1AAA");
    w->m_qsoHistory.message("K1AAA", QsoHistory::S73, 0, "", "", "NA", "K", 0, "-10", 1500, "FT8");
    w->m_transmitting=true; g_iptt=1; manualEnable();
    w->process_Auto(); QCOMPARE(w->m_hisCall, QString("K1AAA")); QVERIFY(w->m_enableTx);
    w->m_transmitting=false; g_iptt=0; w->enableTx_mode(false);
  }
  void historyPriorityAndMidnight() {
    QsoHistory h; h.init(); h.owndata("NA", "K", "AA00", false); h.time(15);
    h.message("K1AAA", QsoHistory::RCALL, 1, "FN20", "", "NA", "K", 86385, "-10", 1500, "FT8");
    h.message("K2BBB", QsoHistory::RCQ, 18, "FN20", "", "NA", "K", 15, "-1", 1500, "FT8");
    QString call,grid,rpt,mode; int rx=0,tx=0,count=0,prio=0; unsigned flags=1|64|128;
    QCOMPARE(h.autoseq(call,grid,rpt,rx,tx,flags,count,prio,mode), QsoHistory::RCALL);
    QCOMPARE(call,QString("K1AAA"));
    // Invalid locators must not overflow the fixed coordinate array.
    h.owndata("NA", "K", QString(10000,'A'), false);
    h.message("K3CCC", QsoHistory::RCALL, 1, "AA00AA00AAEXTRA", "", "NA", "K", 15, "-10", 1500, "FT8");
    h.init(); call.clear(); flags=1|64|128;
    QCOMPARE(h.autoseq(call,grid,rpt,rx,tx,flags,count,prio,mode),QsoHistory::NONE);
  }
  void searchCqAndFinalMessages_data() {
    QTest::addColumn<QString>("message");
    QTest::newRow("CQ") << QString("CQ K3CCC FN31");
    QTest::newRow("73") << QString("K9ZZZ K3CCC 73");
    QTest::newRow("RR73") << QString("K9ZZZ K3CCC RR73");
    QTest::newRow("RRR") << QString("K9ZZZ K3CCC RRR");
  }
  void searchCqAndFinalMessages() {
    QFETCH(QString,message);
    w->on_stopTxButton_clicked();w->clearDX("");
    w->m_qsoHistory.init();w->m_qsoHistory.owndata("NA","K","AA00",false);
    w->m_callMode=2;w->m_callPrioCQ=true;w->m_callWorkedB4=true;w->m_houndMode=false;
    w->m_counter=4; // Prior retry counter must not force a CQ before selecting.
    DecodedText decoded(QString("000015 -10  0.1 1700 ~ ")+message);
    w->ui->decodedTextBrowser->displayDecodedText(&decoded,"N0CALL","","",false,w->m_logBook,w->m_qsoHistory,w->m_qsoHistory,14074000,"FT8",true,true,1500,{},{},{},{},false,w.get());
    manualEnable();w->process_Auto();
    QCOMPARE(w->m_hisCall,QString("K3CCC"));
    QVERIFY(w->m_ntx==1 || (w->m_skipTx1 && w->m_ntx==2));
    QVERIFY(w->m_enableTx);w->on_stopTxButton_clicked();
  }
  void workedVisibilityDefaultsAndQsoMessages() {
    QVERIFY(!w->m_config.workedDontShow());
    QTRY_VERIFY(!w->m_logLoading && !w->m_logImportActive);
    saveManagedSettings(ManagedSettings{},true);QVERIFY(w->m_config.workedDontShow());
    w->m_logBook.init("N0CALL","","","ZZ9QA");w->m_logBook.addAsWorked("K1AAA","20m","FT8","20261030","FN31","");
    w->clearDX("");w->m_qsoHistory.init();
    QVERIFY(!(managedDecode("CQ K1AAA FN31")&2));
    QVERIFY(!(managedDecode("K9ZZZ K1AAA -10")&2));
    for(auto text:{"N0CALL K1AAA -10","N0CALL K1AAA R-10","N0CALL K1AAA RRR","N0CALL K1AAA RR73","N0CALL K1AAA 73"})QVERIFY(managedDecode(text)&2);
    w->ui->dxCallEntry->setText("K1AAA");QVERIFY(managedDecode("K9ZZZ K1AAA -10")&2);
    w->clearDX("");saveManagedSettings(ManagedSettings{},false);QVERIFY(managedDecode("CQ K1AAA FN31")&2);
  }
  void managedMenusAndManualGate() {
    QCOMPARE(w->m_managed.mode(),ManagedMode::Off);
    auto menus=w->ui->menuBar->actions();QCOMPARE(menus.indexOf(w->m_managedMenu->menuAction()),menus.indexOf(w->ui->menuAutoSeq->menuAction())+1);
    QCOMPARE(menus.indexOf(w->ui->menuDXpedition->menuAction()),menus.indexOf(w->m_managedMenu->menuAction())+1);
    w->setManagedMode(ManagedMode::DxccHunter);QVERIFY(!w->m_enableTx);QVERIFY(!w->m_manualTxAuthorized);
    w->enableTx_mode(true);QVERIFY(!w->m_enableTx);w->process_Auto();QVERIFY(!w->managedTxAllowed());QCOMPARE(g_iptt,0);
    w->setManagedMode(ManagedMode::QsoEnthusiast);QCOMPARE(w->m_managed.mode(),ManagedMode::QsoEnthusiast);
    int checked=0;for(auto action:w->m_managedMenu->actions())checked+=action->isChecked();QCOMPARE(checked,1);
    w->writeSettings();QVERIFY(!settings.contains("Common/ManagedMode"));
    w->setManagedMode(ManagedMode::Off);QVERIFY(w->m_managedOff->isChecked());QVERIFY(w->ui->menuAutoSeq->isEnabled());
    auto remote=w->mcpCall("set_options",{{"managed_mode","dxcc_hunter"}});QVERIFY(!remote.value("isError").toBool());
    QCOMPARE(w->mcpStatus().value("managed_mode").toString(),QString("dxcc_hunter"));QVERIFY(!w->m_manualTxAuthorized);QVERIFY(!w->m_enableTx);
    w->setManagedMode(ManagedMode::Off);
  }
  void managedCriteriaAndIncomingWorkedPolicy() {
    QTRY_VERIFY(!w->m_logLoading && !w->m_logImportActive);
    ManagedSettings config;config.hunter.criteria=0;saveManagedSettings(config,true);prepareManaged(ManagedMode::DxccHunter);
    managedDecode("CQ JA1AAA PM95");w->process_Auto();QVERIFY(w->m_hisCall.isEmpty());QVERIFY(!w->managedTxAllowed());
    managedDecode("N0CALL K2BBB FN31");w->process_Auto();QCOMPARE(w->m_hisCall,QString("K2BBB"));QVERIFY(w->managedTxAllowed());
    w->on_stopTxButton_clicked();config.hunter.criteria=NewDxcc;saveManagedSettings(config,true);prepareManaged(ManagedMode::DxccHunter);
    w->m_logBook.addAsWorked("K1AAA","20m","FT8","20261030","FN31","");
    managedDecode("N0CALL K1AAA FN31");managedDecode("CQ JA1AAA PM95");w->process_Auto();QCOMPARE(w->m_hisCall,QString("JA1AAA"));
    w->on_stopTxButton_clicked();config.hunter.answerWorked=true;saveManagedSettings(config,true);prepareManaged(ManagedMode::DxccHunter);
    w->m_logBook.addAsWorked("K1AAA","20m","FT8","20261030","FN31","");
    managedDecode("N0CALL K1AAA FN31");managedDecode("CQ JA1AAA PM95");w->process_Auto();QCOMPARE(w->m_hisCall,QString("K1AAA"));
    w->setManagedMode(ManagedMode::Off);saveManagedSettings(ManagedSettings{},false);
  }
  void managedQsoContinuationAndCallLimit() {
    QTRY_VERIFY(!w->m_logLoading && !w->m_logImportActive);prepareManaged(ManagedMode::QsoEnthusiast);
    managedDecode("CQ K3CCC FN31");w->process_Auto();QCOMPARE(w->m_hisCall,QString("K3CCC"));QCOMPARE(w->m_ntx,1);
    // Complete three real-send accounting events without asserting PTT.
    for(int i=0;i<3;++i){managedSentRound();}
    w->process_Auto();QVERIFY(w->m_hisCall.isEmpty());QVERIFY(!w->managedTxAllowed());QVERIFY(w->m_managed.blocked("K3CCC",w->m_managedClock.elapsed()));
    managedDecode("CQ K3CCC FN31");w->process_Auto();QVERIFY(w->m_hisCall.isEmpty());
    managedDecode("CQ JA1AAA PM95");w->process_Auto();QCOMPARE(w->m_hisCall,QString("JA1AAA"));
    managedSentRound();
    managedDecode("N0CALL JA1AAA -12","000045");w->process_Auto();QCOMPARE(w->m_ntx,3);
    for(int i=0;i<4;++i){managedSentRound();}
    w->process_Auto();QVERIFY(w->m_hisCall.isEmpty());
    QVERIFY(w->m_managed.blocked("JA1AAA",w->m_managedClock.elapsed()));
    managedDecode("N0CALL K2BBB FN31","000045");
    w->process_Auto();QCOMPARE(w->m_hisCall,QString("K2BBB"));QCOMPARE(w->m_ntx,2);QVERIFY(w->m_enableTx);
    w->m_qsoHistory.message("K2BBB",QsoHistory::S73,0,"","","NA","K",90,"-10",1500,"FT8");
    w->m_qsoHistory.complete("K2BBB");
    w->process_Auto();QVERIFY(w->m_hisCall.isEmpty());QVERIFY(!w->managedTxAllowed());QVERIFY(w->m_enableTx);
    w->on_stopTxButton_clicked();w->process_Auto();QVERIFY(!w->m_manualTxAuthorized);QVERIFY(!w->m_enableTx);QCOMPARE(g_iptt,0);
    w->setManagedMode(ManagedMode::Off);w->m_singleshot=false;
  }
  void managedNoveltyUsesOperatorBandAndModeHistory() {
    QTRY_VERIFY(!w->m_logLoading && !w->m_logImportActive);saveManagedSettings(ManagedSettings{});
    // Novelty is independent of failed calls in earlier test cases. Runtime
    // mode switches deliberately retain those per-process retry cooldowns.
    w->setManagedMode(ManagedMode::Off);w->m_managed=ManagedSession{};
    prepareManaged(ManagedMode::QsoEnthusiast);
    w->m_logBook.addAsWorked("K1AAA","20m","FT8","20261030","FN31","");
    ManagedCandidate c;c.call="K1AAA";c.grid="FN31";c.mode="FT8";c.utcSeconds=15;c.available=true;c.seen=w->m_managedClock.elapsed();
    w->m_managed.observe(c);QVERIFY(w->chooseManagedCandidate().call.isEmpty());
    c.mode="FT4";w->m_managed.observe(c);auto selected=w->chooseManagedCandidate();QCOMPARE(selected.call,c.call);
    QVERIFY(selected.novelty&NewBandModeCall);QVERIFY(selected.novelty&NewBandModeDxcc);QVERIFY(!(selected.novelty&NewCall));QVERIFY(!(selected.novelty&NewDxcc));
    c.mode="FT8";w->m_freqNominal=7074000;w->m_managed.observe(c);selected=w->chooseManagedCandidate();QVERIFY(selected.novelty&NewBandModeCall);QVERIFY(selected.novelty&NewBandModeDxcc);
    w->m_freqNominal=14074000;c.call="JA1AAA";c.grid="PM95";w->m_managed.clearCandidates();w->m_managed.observe(c);selected=w->chooseManagedCandidate();
    QCOMPARE(selected.call,c.call);
    QVERIFY(selected.novelty&NewCqZone);QVERIFY(selected.novelty&NewItuZone);QVERIFY(selected.novelty&NewDxcc);QVERIFY(selected.novelty&NewGrid);QVERIFY(selected.novelty&NewCall);
    w->setManagedMode(ManagedMode::Off);w->m_singleshot=false;
  }
  void managedFreshInvitationAfterPreviousQso() {
    QTRY_VERIFY(!w->m_logLoading && !w->m_logImportActive);
    ManagedSettings config;config.enthusiast.answerWorked=true;saveManagedSettings(config);prepareManaged(ManagedMode::QsoEnthusiast);
    w->m_logBook.addAsWorked("K4DDD","20m","FT8","20261030","FN31","");
    w->m_qsoHistory.message("K4DDD",QsoHistory::RCALL,0,"FN31","","NA","K",0,"-10",1500,"FT8");
    w->m_qsoHistory.complete("K4DDD");
    managedDecode("N0CALL K4DDD -12");w->process_Auto();QCOMPARE(w->m_hisCall,QString("K4DDD"));QCOMPARE(w->m_ntx,3);
    w->process_Auto();QCOMPARE(w->m_hisCall,QString("K4DDD"));QVERIFY(w->managedTxAllowed());
    w->setManagedMode(ManagedMode::Off);saveManagedSettings(ManagedSettings{});w->m_singleshot=false;
  }
  void managedPeriodicCqAndStop() {
    QTRY_VERIFY(!w->m_logLoading && !w->m_logImportActive);prepareManaged(ManagedMode::DxccHunter);
    w->process_Auto();QVERIFY(!w->managedTxAllowed());
    w->m_managed.setMode(ManagedMode::DxccHunter,w->m_managedClock.elapsed()-900000);
    w->process_Auto();QVERIFY(w->managedTxAllowed());QCOMPARE(w->m_ntx,6);QCOMPARE(w->m_managed.cqRemaining(),3);
    managedSentRound("CQ N0CALL AA00");
    managedDecode("N0CALL K2BBB FN31");w->process_Auto();QCOMPARE(w->m_hisCall,QString("K2BBB"));QCOMPARE(w->m_managed.cqRemaining(),2);
    w->m_qsoHistory.message("K2BBB",QsoHistory::S73,0,"","","NA","K",30,"-10",1500,"FT8");
    w->m_qsoHistory.complete("K2BBB");
    w->process_Auto();QCOMPARE(w->m_ntx,6);QVERIFY(w->managedTxAllowed());
    for(int i=0;i<2;++i){managedSentRound("CQ N0CALL AA00");w->process_Auto();}
    QVERIFY(!w->managedTxAllowed());QCOMPARE(w->m_managed.cqRemaining(),0);
    w->on_stopTxButton_clicked();w->m_managed.setMode(ManagedMode::DxccHunter,w->m_managedClock.elapsed()-900000);w->process_Auto();
    QVERIFY(!w->managedTxAllowed());w->startTx2();QCOMPARE(g_iptt,0);QVERIFY(!w->m_tx_when_ready);
    w->setManagedMode(ManagedMode::Off);w->m_singleshot=false;
  }
  void malformedAndUnicodeWave() {
    QString file = temp.path()+QString::fromUtf8("/测试_123456.wav");
    QByteArray bytes; QDataStream stream(&bytes,QIODevice::WriteOnly); stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF",4); stream<<quint32(40); stream.writeRawData("WAVEfmt ",8);
    stream<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(12000)<<quint32(24000)<<quint16(2)<<quint16(16);
    stream.writeRawData("data",4); stream<<quint32(4)<<qint16(123)<<qint16(-123);
    QFile f(file); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(bytes); f.close();
    getfile(file,15); QCOMPARE(dec_data.params.kin,2); QCOMPARE(dec_data.d2[0],short(123)); QCOMPARE(dec_data.params.nutc,123456);
    QVERIFY(f.open(QIODevice::WriteOnly)); f.write("RIFF"); f.close();
    getfile(file,15); QCOMPARE(dec_data.params.kin,0); QCOMPARE(dec_data.params.newdat,0);
    bytes.replace(12,4,"junk"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(bytes); f.close();
    getfile(file,15); QCOMPARE(dec_data.params.kin,0);
  }
  void mcpProtocolAndPermissions() {
    int invoked=0;
    McpServer server([&invoked](QString const&, QJsonObject const&) { ++invoked; return McpServer::success({{"ok",true}}); });
    auto call=[&](QString name,QJsonObject args=QJsonObject{}) { return server.dispatch({{"jsonrpc","2.0"},{"id",1},{"method","tools/call"},{"params",QJsonObject{{"name",name},{"arguments",args}}}}); };
    QVERIFY(call("start_tx").value("result").toObject().value("isError").toBool()); QCOMPARE(invoked,0);
    QVERIFY(!call("stop_tx").value("result").toObject().value("isError").toBool()); QCOMPARE(invoked,1);
    QCOMPARE(call("set_options",{{"watchdog_minutes",1000}}).value("error").toObject().value("code").toInt(),-32602);
    QCOMPARE(call("set_options",{{"managed_mode","invalid"}}).value("error").toObject().value("code").toInt(),-32602);
    QCOMPARE(call("set_monitor",{{"enabled","yes"}}).value("error").toObject().value("code").toInt(),-32602);
    QVERIFY(server.dispatch({{"jsonrpc","2.0"},{"method","tools/call"},{"params",QJsonObject{{"name","stop_tx"}}}}).isEmpty()); QCOMPARE(invoked,1);
    auto init=server.dispatch({{"jsonrpc","2.0"},{"id",7},{"method","initialize"},{"params",QJsonObject{{"protocolVersion","2025-11-25"},{"capabilities",QJsonObject{}},{"clientInfo",QJsonObject{{"name","test"},{"version","1"}}}}}});
    QCOMPARE(init.value("result").toObject().value("protocolVersion").toString(),QString("2025-11-25"));
    QCOMPARE(init.value("result").toObject().value("serverInfo").toObject().value("version").toString(),QCoreApplication::applicationVersion());
    QCOMPARE(McpServer::tools().size(),22);
  }
  void mcpApplicationControls() {
    w->enableTx_mode(false); w->clearDX("");
    auto result=w->mcpCall("set_target",{{"callsign","K1ABC"},{"grid","FN20"},{"report",-12}});
    QVERIFY(!result.value("isError").toBool()); QCOMPARE(w->m_hisCall,QString("K1ABC")); QVERIFY(!w->m_enableTx);
    QVERIFY(w->mcpCall("set_target",{{"callsign","bad\nCALL"}}).value("isError").toBool());
    w->mcpCall("set_options",{{"watchdog_minutes",999},{"single_qso",false},{"selection","middle"}});
    QCOMPARE(w->m_config.watchdog(),999); QCOMPARE(w->m_callMode,2);
    QVERIFY(!w->mcpCall("get_config",{}).value("structuredContent").toObject().contains("token"));
    QVERIFY(w->mcpCall("get_log",{{"file","../foreign.adi"}}).value("isError").toBool());
    manualEnable();
    QVERIFY(w->mcpCall("set_frequency",{{"hz",14074000}}).value("isError").toBool());
    w->mcpCall("stop_tx",{}); QVERIFY(!w->m_enableTx);
  }
  void hrdFragmentedAndMalformedReplies() {
    for (int test=0; test<4; ++test) {
      QByteArray reply; QDataStream bytes(&reply,QIODevice::WriteOnly); bytes.setByteOrder(QDataStream::LittleEndian);
      bytes << quint32(test==3 ? 8 : 22) << quint32(test==1 ? 0 : 0x1234ABCD)
            << quint32(test==2 ? 0 : 0xABCD1234) << quint32(0) << quint16('O') << quint16('K') << quint16(0);
      std::promise<quint16> port; auto address=port.get_future();
      std::thread peer([&port, reply] {
        QTcpServer server; if(!server.listen(QHostAddress::LocalHost,0)) { port.set_value(0); return; }
        port.set_value(server.serverPort());
        if(!server.waitForNewConnection(3000)) return;
        auto sock=server.nextPendingConnection();
        if(!sock->bytesAvailable() && !sock->waitForReadyRead(3000)) return;
        sock->readAll();
        sock->write(reply.left(7)); sock->waitForBytesWritten(1000);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        sock->write(reply.mid(7)); sock->waitForBytesWritten(1000);
        sock->disconnectFromHost();
      });
      HRDTransceiver rig({}, "localhost", false, TransceiverFactory::TX_audio_source_front, 1000);
      rig.hrd_=new QTcpSocket(&rig); rig.protocol_=HRDTransceiver::v5;
      rig.hrd_->connectToHost(QHostAddress::LocalHost,address.get());
      bool connected=rig.hrd_->waitForConnected(3000), rejected=false; QString received;
      try { if(connected) received=rig.send_command("test",true,false); } catch (std::exception const&) { rejected=true; }
      peer.join();
      QVERIFY(connected);
      if(test==0) { QVERIFY(!rejected); QCOMPARE(received,QString("OK")); }
      else QVERIFY(rejected);
    }
  }
  void rigReconnectPreservesStopAndWave() {
    w->on_monitorButton_clicked(false);
    auto state=w->m_rigState;
    state.online(true); state.frequency(w->m_freqNominal);
    for (bool disk : {false,true}) {
      w->m_diskData=disk;
      w->m_rigState.online(false);
      w->handle_transceiver_update(state);
      QCOMPARE(w->m_diskData,disk);
      QVERIFY(!w->m_monitoring);
      QVERIFY(w->m_monitoroff);
    }
    w->m_diskData=false;
  }
  void realFt8WaveDecodes_data() {
    QTest::addColumn<bool>("modern");
    QTest::newRow("traditional")<<false;QTest::newRow("modern")<<true;
  }
  void realFt8WaveDecodes() {
    QFETCH(bool,modern);w->ui->actionModernFT8Decode->setChecked(modern);
    qInfo("FT8 test: selecting mode");
    w->enableTx_mode(false); w->m_autoseq=false; w->m_autoTx=false;
    w->on_actionFT8_triggered(); w->m_mcpDecodes=QJsonArray{};
    stopWaveAudio();
    qInfo("FT8 test: loading WAV");
    w->m_diskData=true; w->m_path=QCoreApplication::applicationDirPath()+"/test-ft8.wav";
    QVERIFY(QFile::exists(w->m_path));
    w->read_wav_file(w->m_path);
    auto decoded=[this] {
      for (auto const& d : w->m_mcpDecodes)
        if (d.toObject().value("raw").toString().contains("CQ K1ABC FN20")) return true;
      return false;
    };
    QElapsedTimer wait; wait.start();
    while (!decoded() && wait.elapsed()<20000) QTest::qWait(50);
    QVERIFY2(decoded(),qPrintable(QString("No fixture decode: disk=%1 busy=%2 monitor=%3 decodes=%4")
      .arg(w->m_diskData).arg(w->m_decoderBusy).arg(w->m_monitoring)
      .arg(QString::fromUtf8(QJsonDocument(w->m_mcpDecodes).toJson(QJsonDocument::Compact)))));
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_decoderBusy,30000);
    QCOMPARE(dec_data.params.nmodernft8,modern?1:0);
    QCOMPARE(dec_data.params.nmodernft4,0);
    QVERIFY(!w->m_enableTx);
    w->m_diskData=false;
    w->ui->actionModernFT8Decode->setChecked(false);
  }
  void realFt2WaveDecodes() {
    w->on_stopTxButton_clicked();w->on_actionFT2_triggered();stopWaveAudio();
    QCOMPARE(w->m_TRperiod,3.75);QVERIFY(!w->m_enableTx);
    // Capture the real output command with audio physically disconnected.
    QVERIFY(QObject::disconnect(w.get(),&MainWindow::sendMessage,w->m_modulator,&Modulator::start));
    double selectedSpacing=0,selectedNsps=0;unsigned selectedSymbols=0;
    auto capture=connect(w.get(),&MainWindow::sendMessage,this,[&](unsigned n,double nsps,double,double spacing){selectedSymbols=n;selectedNsps=nsps;selectedSpacing=spacing;});
    w->transmit();QObject::disconnect(capture);
    connect(w.get(),&MainWindow::sendMessage,w->m_modulator,&Modulator::start);
    QCOMPARE(selectedSymbols,105u);QCOMPARE(selectedNsps,288.0);QVERIFY(selectedSpacing<0);
    QByteArray message=QByteArray("CQ K1ABC FN20").leftJustified(37,' '),sent(37,' ');
    signed char bits[77]{};int tones[103]{},check=0;
    genft2_(message.data(),&check,sent.data(),bits,tones,37,37);
    QCOMPARE(sent.trimmed(),QByteArray("CQ K1ABC FN20"));
    int symbols=103,nsps=288,nwave=30240,complex=0;float rate=12000,freq=1500;
    QVector<float> wave(nwave),scratch(2*nwave);
    gen_ft2wave_(tones,&symbols,&nsps,&rate,&freq,scratch.data(),wave.data(),&complex,&nwave);
    auto reference=wave;
    // Same process can generate both rates; cached shaping must follow the rate.
    nsps=1152;nwave=120960;rate=48000;wave.resize(nwave);scratch.resize(2*nwave);
    gen_ft2wave_(tones,&symbols,&nsps,&rate,&freq,scratch.data(),wave.data(),&complex,&nwave);
    nsps=288;nwave=30240;rate=12000;wave.resize(nwave);
    gen_ft2wave_(tones,&symbols,&nsps,&rate,&freq,scratch.data(),wave.data(),&complex,&nwave);
    QCOMPARE(wave,reference);
    auto path=temp.path()+"/ft2_000000.wav";QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));
    QDataStream out(&file);out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF",4);out<<quint32(36+90000);out.writeRawData("WAVEfmt ",8);
    out<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(12000)<<quint32(24000)<<quint16(2)<<quint16(16);
    out.writeRawData("data",4);out<<quint32(90000);
    for(int i=0;i<45000;++i)out<<qint16((i>=6000 && i<6000+nwave ? 9000*wave[i-6000]:0)+((i*313)%201)-100);
    file.close();w->m_mcpDecodes=QJsonArray{};w->m_diskData=true;w->m_path=path;w->read_wav_file(path);
    auto decoded=[this]{for(auto const& d:w->m_mcpDecodes)if(d.toObject().value("raw").toString().contains("CQ K1ABC FN20"))return true;return false;};
    QTRY_VERIFY_WITH_TIMEOUT(decoded(),20000);QTRY_VERIFY_WITH_TIMEOUT(!w->m_decoderBusy,20000);
    QCOMPARE(g_iptt,0);QVERIFY(!w->m_enableTx);w->m_diskData=false;
    w->on_actionFT8_triggered();stopWaveAudio();
  }
  void jttyControllerAndManualAuthorization() {
    w->on_stopTxButton_clicked();stopWaveAudio();w->on_actionJTTY_triggered();
    QCOMPARE(w->m_mode,QString("JTTY"));QVERIFY(!w->m_enableTx);QVERIFY(!w->m_manualTxAuthorized);
    w->m_jttyText->setText("CQ N0CALL");w->requestJttyTransmit();
    QVERIFY(!w->m_jttyPreparing);QCOMPARE(g_iptt,0);QVERIFY(!w->m_jttySend->isEnabled());
    JttyController controller;QSignalSpy encoded(&controller,&JttyController::encoded),errors(&controller,&JttyController::error);
    controller.setActive(true);QVERIFY(controller.encode("CQ N0CALL",1500));
    QTRY_COMPARE_WITH_TIMEOUT(encoded.size(),1,15000);QCOMPARE(errors.size(),0);
    auto pcm=encoded.at(0).at(1).toByteArray();QVERIFY(pcm.size()>48000);
    QVERIFY(controller.encode("STOP ME",1500));controller.cancelTransmit();QTest::qWait(300);QCOMPARE(encoded.size(),1);
    controller.setActive(false);controller.setActive(true);QVERIFY(controller.encode("73",1500));
    QTRY_COMPARE_WITH_TIMEOUT(encoded.size(),2,15000);QCOMPARE(errors.size(),0);controller.setActive(false);
    w->on_stopTxButton_clicked();QVERIFY(!w->m_enableTx);QCOMPARE(g_iptt,0);
    w->on_actionFT8_triggered();stopWaveAudio();QVERIFY(w->ui->menuAutoSeq->isEnabled());
  }
  void savedAudioSnapshotAndUtc() {
    QDir dir(temp.path());QString nested=QString::fromUtf8("录音验证/")+QString(90,'a');QVERIFY(dir.mkpath(nested));
    auto path=dir.filePath(nested+"/261030_000000");QVector<short> samples(12000,321);
    auto stamp=QDateTime::fromString("2026-10-30T08:00:00+08:00",Qt::ISODate);
    auto error=MainWindow::save_wave_file(path,samples,"N0CALL","AA00","FT2",14084000,"K1ABC","FN20",stamp);
    QVERIFY2(error.isEmpty(),qPrintable(error));QFile file(path+".wav");QVERIFY(file.open(QIODevice::ReadOnly));
    auto bytes=file.readAll();QVERIFY(bytes.contains("2026-10-30T00:00:00.000Z"));
    BWFFile decoded(QAudioFormat{},path+".wav");QVERIFY(decoded.open(QIODevice::ReadOnly));
    QCOMPARE(decoded.readAll(),QByteArray::fromHex("4101").repeated(12000));
    QString parseError;OperatorLog::parse("damaged log",&parseError);QVERIFY(!parseError.isEmpty());
  }
  void malformedWaveMetadataAndTciPackets() {
    auto path=temp.path()+"/bad-metadata.wav";
    auto check=[&](QByteArray chunk){
      QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setByteOrder(QDataStream::LittleEndian);
      stream.writeRawData("RIFF",4);stream<<quint32(4+chunk.size());stream.writeRawData("WAVE",4);stream.writeRawData(chunk.constData(),chunk.size());
      QFile f(path);if(!f.open(QIODevice::WriteOnly))return false;f.write(bytes);f.close();
      BWFFile reader(QAudioFormat{},path);return !reader.open(QIODevice::ReadOnly);
    };
    QVERIFY(check(QByteArray("bext")+QByteArray::fromHex("04000000")+"oops"));
    QVERIFY(check(QByteArray("LIST")+QByteArray::fromHex("ffffffff")+"INFO"));
    QVERIFY(check(QByteArray("fmt ")+QByteArray::fromHex("01000000")+"x"));
    TCITransceiver rig({},"0","localhost:40001",true,1000);QSignalSpy failures(&rig,&Transceiver::failure);
    for(auto bytes:{QByteArray(),QByteArray(63,0),QByteArray(64,0)})
      QVERIFY(QMetaObject::invokeMethod(&rig,"onBinaryReceived",Qt::DirectConnection,Q_ARG(QByteArray,bytes)));
    QCOMPARE(failures.size(),3);
    Data_Stream packet{};packet.format=3;packet.sampleRate=48000;packet.type=RxAudioStream;packet.length=8192;
    QByteArray shortPayload(reinterpret_cast<char*>(&packet),64);
    QVERIFY(QMetaObject::invokeMethod(&rig,"onBinaryReceived",Qt::DirectConnection,Q_ARG(QByteArray,shortPayload)));
    QCOMPARE(failures.size(),4);
  }
  void externalLoggerIsAsynchronous() {
    QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));
    auto sender=new LogForwarder("127.0.0.1",server.serverPort(),"<command:3>Log");QSignalSpy done(sender,&LogForwarder::finished);
    QTRY_VERIFY(server.hasPendingConnections());auto peer=server.nextPendingConnection();
    QTRY_VERIFY(peer->bytesAvailable());QCOMPARE(peer->readAll(),QByteArray("<command:3>Log"));
    int ticks=0;QTimer timer;timer.setInterval(10);connect(&timer,&QTimer::timeout,this,[&]{++ticks;});timer.start();
    QTest::qWait(70);QVERIFY(ticks>=3);QCOMPARE(done.size(),0);
    peer->write("A");QTest::qWait(20);QCOMPARE(done.size(),0);peer->write("CK");
    QTRY_COMPARE(done.size(),1);QVERIFY(done.first().first().toString().isEmpty());peer->deleteLater();
    auto rejected=new LogForwarder("127.0.0.1",server.serverPort(),"log");QSignalSpy failure(rejected,&LogForwarder::finished);
    QTRY_VERIFY(server.hasPendingConnections());peer=server.nextPendingConnection();peer->write("NAK");
    QTRY_COMPARE(failure.size(),1);QVERIFY(!failure.first().first().toString().isEmpty());peer->deleteLater();
  }
  void broadcastWaveMetadataBoundsAndEndian() {
    QAudioFormat format;format.setCodec("audio/pcm");format.setChannelCount(1);format.setSampleRate(12000);
    format.setSampleSize(16);format.setSampleType(QAudioFormat::SignedInt);
    for(auto order:{QAudioFormat::LittleEndian,QAudioFormat::BigEndian}) {
      format.setByteOrder(order);auto path=temp.path()+QString("/bext-%1.wav").arg(order);
      BWFFile writer(format,path);QCOMPARE(writer.bext_version(),BWFFile::BextVersion::v_0);
      writer.bext_version(BWFFile::BextVersion::v_2);writer.bext_description(QByteArray(256,'X'));
      writer.bext_originator(QByteArray(32,'Y'));writer.bext_originator_reference(QByteArray(32,'Z'));
      auto stamp=QDateTime::fromString("2026-10-30T08:30:12+08:00",Qt::ISODate);
      writer.bext_origination_date_time(stamp);writer.bext_time_reference(0x1234567887654321ULL);
      writer.bext_loudness_value(2000);writer.bext_coding_history("A=PCM,F=12000");
      QVERIFY(writer.open(QIODevice::WriteOnly));QCOMPARE(writer.write(QByteArray(10,'a')),qint64(10));writer.close();
      BWFFile reader(QAudioFormat{},path);QVERIFY(reader.open(QIODevice::ReadOnly));
      QCOMPARE(reader.bext_version(),BWFFile::BextVersion::v_2);QCOMPARE(reader.bext_description(),QByteArray(256,'X'));
      QCOMPARE(reader.bext_originator(),QByteArray(32,'Y'));QCOMPARE(reader.bext_originator_reference(),QByteArray(32,'Z'));
      QCOMPARE(reader.bext_origination_date_time(),stamp.toUTC());QCOMPARE(reader.bext_time_reference(),quint64(0x1234567887654321ULL));
      QCOMPARE(reader.bext_loudness_value(),quint16(2000));QCOMPARE(reader.bext_coding_history(),QByteArray("A=PCM,F=12000"));
      QCOMPARE(reader.readAll(),QByteArray(10,'a'));QVERIFY(reader.reset());QCOMPARE(reader.readAll(),QByteArray(10,'a'));
      QVERIFY(!reader.map(9,2));reader.close();
    }
  }
  void realSuperFoxWaveDecodes() {
    w->on_stopTxButton_clicked();w->m_autoseq=false;w->m_autoTx=false;
    w->ui->actionSuperFox->setChecked(true);
    QVERIFY(w->m_superFoxMode);QVERIFY(w->m_houndMode);QVERIFY(!w->m_houndTXfreqJumps);
    QCOMPARE(w->ui->RxFreqSpinBox->value(),750);
    stopWaveAudio();
    w->m_mcpDecodes=QJsonArray{};w->m_diskData=true;
    w->m_path=QCoreApplication::applicationDirPath()+"/test-superfox_000000.wav";
    QVERIFY(QFile::exists(w->m_path));w->read_wav_file(w->m_path);
    auto decoded=[this] {for(auto const& d:w->m_mcpDecodes) if(d.toObject().value("raw").toString().contains("W0AAA K1JT RR73")) return true;return false;};
    QElapsedTimer wait;wait.start();while(!decoded() && wait.elapsed()<30000) QTest::qWait(50);
    QVERIFY2(decoded(),qPrintable(QString("SuperFox decode missing; busy=%1 nsftol=%2 rx=%3 mode=%4 decodes=%5 stderr=%6")
      .arg(w->m_decoderBusy).arg(dec_data.params.nsftol).arg(dec_data.params.nfqso).arg(w->m_mode)
      .arg(QString::fromUtf8(QJsonDocument(w->m_mcpDecodes).toJson(QJsonDocument::Compact)))
      .arg(QString::fromUtf8(w->proc_jtdxjt9.readAllStandardError()))));
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_decoderBusy,30000);QVERIFY(!w->m_enableTx);QCOMPARE(g_iptt,0);
    w->m_diskData=false;w->ui->actionSuperFox->setChecked(false);w->ui->actionEnable_hound_mode->setChecked(false);
  }
  void consecutiveWaveDecodeHandoffs() {
    w->on_stopTxButton_clicked(); w->m_autoseq=false; w->m_autoTx=false;
    w->on_actionFT8_triggered();
    stopWaveAudio();
    auto restarts=w->m_decoderRestarts;
    // Prime the delayed live-period suppression without a sound device or
    // decoder job. The subsequent WAV requests must not inherit that timer.
    QScopedValueRollback<int> saveMode(w->m_saveWav,0);
    getfile(QCoreApplication::applicationDirPath()+"/test-ft8.wav",15);
    w->m_diskData=false;w->m_delay=1;w->m_modeChanged=true;w->m_decoderBusy=true;
    for(int n=1;n<=w->m_hsymStop;++n)w->dataSink((n+1)*w->m_FFTSize);
    w->m_decoderBusy=false;w->m_modeChanged=true;
    for (int cycle=0; cycle<6; ++cycle) {
      w->m_mcpDecodes=QJsonArray{}; w->m_diskData=true;
      w->m_path=QCoreApplication::applicationDirPath()+"/test-ft8.wav";
      w->read_wav_file(w->m_path);
      auto decoded=[this] {
        for (auto const& d:w->m_mcpDecodes)
          if(d.toObject().value("raw").toString().contains("CQ K1ABC FN20")) return true;
        return false;
      };
      QElapsedTimer deadline; deadline.start();
      while ((!decoded() || w->m_decoderBusy) && deadline.elapsed()<15000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents,1);
        QThread::yieldCurrentThread();
      }
      QVERIFY2(decoded() && !w->m_decoderBusy,qPrintable(QString("Decode handoff failed at cycle %1: busy=%2 wav=%3 lock=%4 newdat=%5 kin=%6 decodes=%7 stderr=%8")
        .arg(cycle).arg(w->m_decoderBusy).arg(w->m_wav_future_watcher.isRunning())
        .arg(QFile::exists(w->m_config.temp_dir().absoluteFilePath(".lock")))
        .arg(dec_data.params.newdat).arg(dec_data.params.kin)
        .arg(QString::fromUtf8(QJsonDocument(w->m_mcpDecodes).toJson(QJsonDocument::Compact)))
        .arg(QString::fromUtf8(w->proc_jtdxjt9.readAllStandardError()))));
      QCOMPARE(w->m_decoderRestarts,restarts);
      QVERIFY(!w->m_enableTx); QCOMPARE(g_iptt,0);
      // Submit the next file immediately: no artificial acknowledgement delay.
    }
    w->m_diskData=false;
  }
  void decoderFailureDoesNotCloseApplication() {
    QTRY_COMPARE(w->proc_jtdxjt9.state(),QProcess::Running);
    w->m_autoTx=false; manualEnable();
    w->proc_jtdxjt9.kill();
    QTRY_VERIFY(w->m_decoderRestarts > 0);
    QTRY_VERIFY(!w->m_decoderRecovering);
    QTRY_COMPARE(w->proc_jtdxjt9.state(),QProcess::Running);
    QVERIFY(w->m_valid); QVERIFY(!w->m_enableTx); QVERIFY(!w->m_decoderBusy); QVERIFY(!w->m_monitoring);
  }
  void macroModelIsReused() {
    w->ui->dxCallEntry->setText("K1AAA"); auto model=w->m_generatedMacros;
    w->ui->dxCallEntry->setText("K2BBB"); QCOMPARE(w->m_generatedMacros,model);
  }
  void snapshots() {
    QString output=qEnvironmentVariable("JTDX_FM_QA_DIR"); if(output.isEmpty()) return;
    QApplication::processEvents(); // Deliver the queued initial log request.
    QTRY_VERIFY_WITH_TIMEOUT(!w->m_logLoading && !w->m_logImportActive && !w->m_logRefreshPending && !w->m_logRefreshTimer.isActive(),20000);
    w->statusBar()->clearMessage();w->update_watchdog_label();
    QDir().mkpath(output); w->show();QApplication::processEvents();
    auto recordLayout=[&](QString const& name) {
      QJsonObject details{{"width",w->width()},{"height",w->height()},{"minimum_width",w->minimumSizeHint().width()},{"minimum_height",w->minimumSizeHint().height()}};
      QJsonArray controls;
      for(auto object:{"DX_controls_widget","controls_stack_widget","tabWidget","enableTxButton","stopTxButton","TxFreqSpinBox","RxFreqSpinBox","rptSpinBox","tx1","pbSpotDXCall","dxCallEntry","signal_meter_widget"}) {
        auto widget=w->findChild<QWidget*>(object);if(!widget)continue;
        auto hint=widget->minimumSizeHint();auto pos=widget->mapTo(w.get(),QPoint());
        controls.append(QJsonObject{{"name",object},{"x",pos.x()},{"y",pos.y()},{"width",widget->width()},{"height",widget->height()},
          {"hint_width",hint.width()},{"hint_height",hint.height()},{"font_height",widget->fontMetrics().height()}});
      }
      details["controls"]=controls;QFile file(output+"/"+name+".json");QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(details).toJson());
    };
    recordLayout("main-layout");
    w->grab().save(output+"/main.png");
    w->on_stopTxButton_clicked();
    w->ui->tabWidget->setCurrentIndex(2);w->updateStationSelector();
    w->m_quickStationSelector->setCurrentText("N1CALL/P");
    QApplication::processEvents();w->grab().save(output+"/operator-tab.png");
    w->m_quickStationSelector->setCurrentText(w->m_config.my_callsign());
    BlacklistDialog blacklist(w->m_blacklist,w.get());
    blacklist.setWindowTitle(blacklist.windowTitle()+QStringLiteral(" — ")+w->m_config.my_callsign());
    blacklist.show();QApplication::processEvents();blacklist.grab().save(output+"/blacklist.png");blacklist.hide();
    w->ui->tabWidget->setCurrentIndex(0);
    w->on_actionJTTY_triggered();QApplication::processEvents();w->grab().save(output+"/jtty.png");
    w->on_actionFT2_triggered();QApplication::processEvents();w->grab().save(output+"/ft2.png");
    w->on_actionFT8_triggered();
    CAboutDlg about; about.show(); about.grab().save(output+"/about.png");
    QWidget *dialog=w->m_config.findChild<QWidget*>("configuration_dialog");
    // The impl dialog is parented to MainWindow, not Configuration.
    if (!dialog) dialog=w->findChild<QWidget*>("configuration_dialog");
    QVERIFY(dialog);
    auto tabs=dialog->findChild<QTabWidget*>("configuration_tabs"); QVERIFY(tabs);
    auto managed=dialog->findChild<ManagedSettingsWidget*>("managed_tab");QVERIFY(managed);
    auto managedScroll=qobject_cast<QScrollArea*>(managed->parentWidget()->parentWidget());QVERIFY(managedScroll);
    QCOMPARE(tabs->indexOf(managedScroll),tabs->indexOf(dialog->findChild<QWidget*>("filters_tab"))+1);
    QCOMPARE(tabs->indexOf(dialog->findChild<QWidget*>("scheduler_tab")),tabs->indexOf(managedScroll)+1);
    tabs->setCurrentWidget(managedScroll);dialog->show();QApplication::processEvents();
    dialog->grab().save(output+"/managed-settings.png");
    auto enthusiastLimit=managed->findChild<QSpinBox*>("managed_enthusiast_call_limit");QVERIFY(enthusiastLimit);
    managedScroll->ensureWidgetVisible(enthusiastLimit);QApplication::processEvents();
    dialog->grab().save(output+"/managed-settings-bottom.png");
    auto page=dialog->findChild<QWidget*>("mcp_tab"); QVERIFY(page);
    tabs->setCurrentWidget(page); dialog->show(); dialog->grab().save(output+"/mcp-settings.png");
    tabs->setCurrentIndex(0);dialog->grab().save(output+"/station-settings.png");
    auto ntp=dialog->findChild<NtpSettingsWidget*>("ntp_group");QVERIFY(ntp);
    auto previous=ntp->settings();NtpSettings preview;preview.enabled=true;preview.custom=true;preview.server="192.0.2.1";
    ntp->setSettings(preview); // Signal-blocked layout preview, never requests real privileges.
    auto status=ntp->findChild<QLabel*>("ntp_status");auto previousStatus=status->text();status->setText(QString());
    ntp->layout()->activate();
    dialog->grab().save(output+"/ntp-custom-settings.png");ntp->setSettings(previous);status->setText(previousStatus);dialog->hide();
    StationProfile profile;profile.call="N0CALL";profile.grid="AA00";
    StationManager manager({profile},w.get());manager.show();manager.grab().save(output+"/station-manager.png");
    manager.hide();about.hide();
    // Layout previews only. No manual TX permission is granted.
    w->on_stopTxButton_clicked();w->selectUpstreamMode("Q65");
    QApplication::processEvents();QVERIFY(!w->m_enableTx);QVERIFY(!w->m_manualTxAuthorized);
    // A requested old small size must not compress text below Qt's font-aware
    // layout minimum. The window remains resizable above that minimum.
    w->resize(733,422);QApplication::processEvents();
    QVERIFY(w->width()>=w->minimumSizeHint().width());QVERIFY(w->height()>=w->minimumSizeHint().height());
    for(auto name:{"enableTxButton","stopTxButton","TxFreqSpinBox","RxFreqSpinBox","rptSpinBox","tx1","genStdMsgsPushButton"}) {
      auto control=w->findChild<QWidget*>(name);QVERIFY(control);
      QVERIFY2(control->width()>=control->minimumSizeHint().width(),name);
      QVERIFY2(control->height()>=control->minimumSizeHint().height(),name);
    }
    recordLayout("q65-layout");
    w->grab().save(output+"/q65-eme.png");
    w->m_astro->show();QApplication::processEvents();
    w->m_astro->showData({{"utc_ms",double(QDateTime(QDate(2026,10,30),QTime(0,0),Qt::UTC).toMSecsSinceEpoch())},
      {"moon_az_deg",132.5},{"moon_el_deg",38.2},{"dx_moon_az_deg",287.4},{"dx_moon_el_deg",24.1},
      {"sun_az_deg",104.2},{"sun_el_deg",12.3},{"self_doppler_hz",240},{"dx_doppler_hz",-125},
      {"echo_delay_seconds",2.521},{"width_self_hz",1.2},{"width_dx_hz",2.4},
      {"sky_temperature_k",210},{"degradation_db",1.3},{"polarization_deg",33.4}});
    w->m_astro->setNominalFrequency(144174000);QApplication::processEvents();
    bool astroDataVisible=false;
    for(auto label:w->m_astro->findChildren<QLabel*>())if(label->text().contains("132.5")) {
      QCOMPARE(label->text().count('\n'),6);QVERIFY(label->height()>=label->sizeHint().height());astroDataVisible=true;
    }
    QVERIFY(astroDataVisible);
    w->m_astro->grab().save(output+"/astronomical-data.png");w->m_astro->hide();
    auto utc=QDateTime(QDate(2026,10,30),QTime(0,0),Qt::UTC);
    w->m_logDlg->initLogQSO("K1ABC","FN20","Q65","-10","-12","","",utc,utc.addSecs(120),145950000,false,435850000);
    auto propagation=w->m_logDlg->findChild<QComboBox*>("log_propagation");QVERIFY(propagation);
    propagation->setCurrentIndex(propagation->findData("SAT"));
    w->m_logDlg->findChild<QLineEdit*>("log_satellite")->setText("FO-29");
    w->m_logDlg->findChild<QLineEdit*>("log_satellite_mode")->setText("V/U");
    w->m_logDlg->findChild<QDoubleSpinBox*>("log_rx_frequency")->setValue(435.850);
    w->m_logDlg->show();QApplication::processEvents();
    w->m_logDlg->grab().save(output+"/satellite-log.png");w->m_logDlg->hide();
    QVERIFY(!w->m_enableTx);QVERIFY(!w->m_manualTxAuthorized);
  }
  void ntpIntervalPersistsAcrossRestarts() {
    // Configuration owns process-wide Hamlib resources; test restart after closing the main window.
    w->close();w.reset();JTDXDateTime testClock;
    QSettings stored(temp.path()+"/ntp-settings.ini",QSettings::IniFormat);
    stored.setValue("Configuration/MyCall","N0CALL");stored.setValue("Configuration/MyGrid","AA00");
    stored.setValue("Configuration/workedDontShow",true); // Old uninitialized default, corrected once.
    {
      QWidget owner;Configuration config(&stored,&owner);
      config.set_jtdxtime(&testClock);
      QVERIFY(!config.workedDontShow());QVERIFY(stored.value("Configuration/WorkedVisibilityDefaultV330").toBool());
      auto dialog=owner.findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
      auto ntp=dialog->findChild<NtpSettingsWidget*>("ntp_group");QVERIFY(ntp);
      QCOMPARE(ntp->settings().intervalMinutes,15);QVERIFY(!ntp->settings().enabled);
      QTimer::singleShot(0,dialog,[dialog,ntp]{
        auto selected=ntp->settings();selected.intervalMinutes=720;ntp->setSettings(selected);
        dialog->findChild<QCheckBox*>("workedDontShow_check_box")->setChecked(true);
        auto managed=dialog->findChild<ManagedSettingsWidget*>("managed_tab");
        auto profile=managed->settings();profile.hunter.criteria=NewGrid;profile.hunter.callLimit=0;profile.enthusiast.answerWorked=true;profile.enthusiast.callLimit=99;managed->setSettings(profile);
        QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
      });
      QCOMPARE(config.exec(),int(QDialog::Accepted));
    }
    stored.sync();QCOMPARE(stored.value("Configuration/NTP/IntervalMinutes").toInt(),720);
    {
      QWidget owner;Configuration reopened(&stored,&owner);
      reopened.set_jtdxtime(&testClock);
      QVERIFY(reopened.workedDontShow()); // A deliberate new choice is preserved.
      auto ntp=owner.findChild<NtpSettingsWidget*>("ntp_group");QVERIFY(ntp);
      QCOMPARE(ntp->settings().intervalMinutes,720);QVERIFY(!ntp->settings().enabled);
      QCOMPARE(reopened.managed_settings().hunter.criteria,unsigned(NewGrid));QCOMPARE(reopened.managed_settings().hunter.callLimit,0);
      QVERIFY(reopened.managed_settings().enthusiast.answerWorked);QCOMPARE(reopened.managed_settings().enthusiast.callLimit,99);
    }
  }
  void configurationTxFrequencyDeduplicates_data();
  void configurationTxFrequencyDeduplicates();
  void cleanupTestCase() { if(w) w->close(); w.reset(); }
};
#include "TxMessagePresentation.hpp"
#include "QsoLifecycleTests.hpp"
#include "AutoSearchTests.hpp"
#include "DoubleClickTxTests.hpp"
#include "ManagedManualTests.hpp"
#include "ManagedTimingTests.hpp"
#include "ManagedFreshnessTests.hpp"
#include "JtAlertIntegrationTests.hpp"
#include "UdpReplayTests.hpp"
#include "RadioAudioTests.hpp"
#include "Ft8ThreadMenuTests.hpp"
#include "ModulatorCompletionTests.hpp"
#include "TxButtonAppearanceTests.hpp"
#include "CatBandTests.hpp"
#include "ManagedSlotUiTests.hpp"
#include "ManagedSignoffTests.hpp"
#include "ManagedRetryTests.hpp"
#include "OrdinaryRetryTests.hpp"
#include "OrdinaryBusyTests.hpp"
#include "PostQsoHandoffTests.hpp"
#include "ConfigurationTxTests.hpp"
int main(int argc,char** argv) {
  register_types();
  QApplication app(argc,argv);
  QCoreApplication::setApplicationName("JTDX-F-regression");
  QCoreApplication::setApplicationVersion(version());
  QCoreApplication::setOrganizationName("Fluxmancer");
  QTranslator translation;
  if (!qEnvironmentVariable("JTDX_FM_QA_LANG").isEmpty()) {
    translation.load(":/Translations/jtdx_"+qEnvironmentVariable("JTDX_FM_QA_LANG")+".qm");
    app.installTranslator(&translation);
  }
  RegressionTest test;
  return QTest::qExec(&test,argc,argv);
}
#include "Regression.moc"
