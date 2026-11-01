// SPDX-License-Identifier: GPL-3.0-or-later
// Isolated WSJT-X 3.0.2 modes. Radio ownership and TX permission remain in JTDX-F.
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "Detector.hpp"
#include "Modulator.hpp"
#include "soundout.h"
#include "displaytext.h"
#include "widegraph.h"
#include <QRandomGenerator>
#include <QtMath>
#include <cmath>
#include <limits>

extern qint32 g_iptt;

bool MainWindow::upstreamMode() const {return m_mode=="Q65" || m_mode=="Echo" || m_mode=="MSK144";}

void MainWindow::setupUpstream() {
  m_upstream=new UpstreamController(this);m_astroBackend=new UpstreamController(this);
  m_astro=new AstroDialog(m_settings,this);
  for(auto const& name:{QString("Q65"),QString("Echo"),QString("MSK144")}) {
    auto action=ui->menuMode->addAction(name);action->setObjectName("action"+name);action->setCheckable(true);
    action->setActionGroup(ui->actionFT8->actionGroup());m_upstreamActions.insert(name,action);
    connect(action,&QAction::triggered,this,[this,name]{selectUpstreamMode(name);});
  }
  m_fullDuplexAction=ui->menuDecode->addAction(tr("Full duplex"));m_fullDuplexAction->setObjectName("actionFullDuplex");
  m_fullDuplexAction->setCheckable(true);m_fullDuplexAction->setChecked(m_config.full_duplex());
  connect(m_fullDuplexAction,&QAction::toggled,this,[this](bool enabled){
    m_config.set_full_duplex(enabled);
    applyRxAudioBufferSize();
    if(m_transmitting && !m_monitoroff)monitor(enabled);
  });
  m_astroAction=ui->menuView->addAction(tr("Astronomical data"));m_astroAction->setObjectName("actionAstronomicalData");
  connect(m_astroAction,&QAction::triggered,this,[this]{m_astro->show();m_astro->raise();m_lastAstroRequest=0;updateAstronomy();});
  connect(m_astro,&AstroDialog::trackingChanged,this,[this]{m_lastAstroRequest=0;applyAstroCorrection(m_astronomicalData);});
  connect(m_astro,&AstroDialog::frequencyRequested,this,[this](quint64 hz){
    if(m_transmitting || g_iptt)return;
    m_freqNominal=hz;m_freqTxNominal=hz;setRig();setXIT(ui->TxFreqSpinBox->value());
    m_lastAstroRequest=0;resetUpstreamContext();statusUpdate();
  });
  m_emeTools=addToolBar(tr("VHF / EME"));m_emeTools->setObjectName("vhf_eme_toolbar");m_emeTools->setMovable(false);
  m_emeTools->addWidget(new QLabel(tr("Period")));
  m_upstreamPeriod=new QComboBox;m_upstreamPeriod->setObjectName("upstream_period");m_emeTools->addWidget(m_upstreamPeriod);
  m_upstreamSubmode=new QComboBox;m_upstreamSubmode->setObjectName("q65_submode");m_upstreamSubmode->addItems({"A","B","C","D","E","F"});m_emeTools->addWidget(m_upstreamSubmode);
  m_upstreamTolerance=new QSpinBox;m_upstreamTolerance->setObjectName("upstream_tolerance");m_upstreamTolerance->setRange(10,1000);m_upstreamTolerance->setValue(100);m_upstreamTolerance->setSuffix(" Hz");m_emeTools->addWidget(m_upstreamTolerance);
  m_upstreamAverage=new QCheckBox(tr("Average"));m_upstreamAverage->setObjectName("q65_average");m_upstreamAverage->setChecked(true);m_emeTools->addWidget(m_upstreamAverage);
  auto clear=m_emeTools->addAction(tr("Clear average"));connect(clear,&QAction::triggered,this,[this]{m_upstreamClearAverage=true;});
  auto quick=new QToolButton;quick->setText(tr("EME presets"));quick->setPopupMode(QToolButton::InstantPopup);auto menu=new QMenu(quick);quick->setMenu(menu);m_emeTools->addWidget(quick);
  for(auto preset:{"15C","15A","30B","60C","60D","60E"}) {
    QString name(preset);auto action=menu->addAction("Q65-"+name);
    connect(action,&QAction::triggered,this,[this,name]{selectUpstreamMode("Q65");m_upstreamPeriod->setCurrentText(name.left(name.size()-1));m_upstreamSubmode->setCurrentText(name.right(1));});
  }
  connect(menu->addAction("Echo"),&QAction::triggered,this,[this]{selectUpstreamMode("Echo");});
  m_upstreamStatus=new QLabel;m_upstreamStatus->setObjectName("upstream_status");m_upstreamStatus->setTextFormat(Qt::PlainText);m_emeTools->addWidget(m_upstreamStatus);
  m_emeTools->setVisible(m_config.enable_VHF_features());
  connect(m_upstreamPeriod,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{configureUpstreamPeriod();});
  connect(m_upstreamSubmode,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{configureUpstreamPeriod();});
  connect(m_detector,&Detector::timedPcmWritten,this,&MainWindow::upstreamAudio);
  connect(&m_config,&Configuration::transceiver_timed_pcm,this,&MainWindow::upstreamAudio);
  connect(m_upstream,&UpstreamController::decoded,this,&MainWindow::receiveUpstream);
  connect(m_upstream,&UpstreamController::decodeFinished,this,[this](QJsonObject const& request){
    if(!upstreamMode() || request.value("mode").toString()!=m_mode)return;
    if(m_mode=="Q65") {
      auto slot=qint64(request.value("slot_utc_ms").toDouble(-1));
      m_managed.receiveCycleFinished(slot,qRound64(m_TRperiod*1000));
      m_upstreamState.completeQ65(slot);signoffReceiveCycleFinished(slot);
    } else if(m_mode=="MSK144") {
      auto slot=qint64(request.value("slot_utc_ms").toDouble(-1));
      if(slot>=0 && qint64(request.value("audio_end_ms").toDouble(-1))>=slot+qRound64(m_TRperiod*1000)) {
        m_managed.receiveCycleFinished(slot,qRound64(m_TRperiod*1000));
        signoffReceiveCycleFinished(slot);
      }
    }
    if(m_mode!="MSK144")m_blankLine=true;
    m_okToPost=true;
    if(m_mode!="Echo")queueOrdinaryAutoSelection();
    if(m_mode!="Echo" && !m_transmitting && !g_iptt && (m_autoseq || m_managed.active())){m_processAuto_done=true;process_Auto();}
  });
  connect(m_upstream,&UpstreamController::echoData,this,[this](QJsonObject value,QJsonObject){
    if(m_mode!="Echo")return;
    m_upstreamStatus->setText(tr("Echo: %1 dB; Δf %2 Hz; quality %3; averages %4")
      .arg(value.value("snr_db").toDouble(),0,'f',1).arg(value.value("offset_hz").toDouble(),0,'f',1)
      .arg(value.value("quality").toInt()).arg(value.value("integrations").toInt()));
    mcpEvent("echo",value);
  });
  connect(m_upstream,&UpstreamController::error,this,[this](QString const& error){
    if(!upstreamMode())return;
    on_stopTxButton_clicked();m_upstreamStatus->setText(error);statusBar()->showMessage(error,20000);
  });
  connect(m_upstream,&UpstreamController::encoded,this,[this](QString message,QByteArray pcm,QJsonObject result){
    auto now=m_jtdxtime->currentMSecsSinceEpoch2();auto slot=now/(qint64(m_TRperiod)*1000);
    if(!upstreamMode() || !m_enableTx || !m_manualTxAuthorized || !managedTxAllowed() || m_transmitting || g_iptt
       || m_upstreamTxSlot!=slot || m_upstreamTxContext!=m_upstream->context()
       || (m_mode!="Echo" && selectedTransmitText().simplified().toUpper()!=m_upstreamRequestedMessage))return;
    if(messageBlacklisted(message) || permanentlyBlocked(m_hisCall)){stopBlacklistedTransmit();return;}
    m_upstreamStartDelay=result.value("start_delay_ms").toInt();
    if(now-slot*qint64(m_TRperiod)*1000>m_upstreamStartDelay+1000)return;
    m_upstreamPcm=std::move(pcm);m_currentMessage=message;m_currentMessageType=0;m_upstreamPcmConsumed=false;
    prepareSignoff(message);
    m_tx_when_ready=true;g_iptt=1;setRig();setXIT(ui->TxFreqSpinBox->value());
    m_upstreamTxTimeout.start(5000);Q_EMIT m_config.transceiver_ptt(true);
  });
  connect(&m_config,&Configuration::transceiver_tx_pcm_finished,this,[this]{
    if(!upstreamMode() || !m_tci || !m_transmitting)return;
    auto revision=m_upstreamTxRevision;
    QTimer::singleShot(150,this,[this,revision]{if(revision==m_upstreamTxRevision && upstreamMode() && m_tci && m_transmitting)finishUpstreamTransmit();});
  });
  m_upstreamTxTimeout.setSingleShot(true);
  connect(&m_upstreamTxTimeout,&QTimer::timeout,this,[this]{if(upstreamMode()){on_stopTxButton_clicked();statusBar()->showMessage(tr("Transmission timed out; TX stopped."),15000);}});
  connect(m_astroBackend,&UpstreamController::astronomicalData,this,[this](QJsonObject const& data){
    if(qAbs(m_jtdxtime->currentMSecsSinceEpoch2()-qint64(data.value("utc_ms").toDouble()))>5000)return;
    m_astronomicalData=data;m_astro->showData(data);applyAstroCorrection(data);
  });
  connect(m_astroBackend,&UpstreamController::error,this,[this](QString const& error){m_astro->showError(error);applyAstroCorrection({});});
  connect(&m_config,&Configuration::hamlib_restart_requested,this,[this]{on_stopTxButton_clicked();close();});
  connect(this,&MainWindow::finished,this,[this]{m_upstream->stop();m_astroBackend->stop();});
}

void MainWindow::selectUpstreamMode(QString const& mode) {
  if(!m_upstreamActions.contains(mode))return;
  on_stopTxButton_clicked();m_mode=m_modeTx=mode;
  WSPR_config(false);enableHoundAccess(false);
  if(mode=="Echo")setManagedMode(ManagedMode::Off);
  m_upstreamActions.value(mode)->setChecked(true);
  {QSignalBlocker blocker(m_upstreamPeriod);m_upstreamPeriod->clear();
    if(mode=="Q65")m_upstreamPeriod->addItems({"15","30","60","120","300"});
    else if(mode=="MSK144")m_upstreamPeriod->addItems({"5","10","15","30"});else m_upstreamPeriod->addItem("6");
    m_settings->beginGroup("UpstreamModes");QString period=m_settings->value(mode+"Period",mode=="Q65"?60:mode=="MSK144"?15:6).toString();
    if(m_upstreamPeriod->findText(period)>=0)m_upstreamPeriod->setCurrentText(period);
    QSignalBlocker subblock(m_upstreamSubmode);
    const int maximum=UpstreamOperatingState::q65MaximumSubmode(m_upstreamPeriod->currentText().toInt());
    m_upstreamSubmode->clear();for(int i=0;i<=maximum;++i)m_upstreamSubmode->addItem(QString(QChar('A'+i)));
    m_upstreamSubmode->setCurrentIndex(qBound(0,m_settings->value("Q65Submode",0).toInt(),maximum));m_settings->endGroup();
  }
  m_TRperiod=m_upstreamPeriod->currentText().toInt();m_hsymStop=173;
  switch_mode(mode=="Q65"?Modes::Q65:mode=="Echo"?Modes::ECHO:Modes::MSK144);commonActions();
  m_emeTools->show();m_upstreamPeriod->setEnabled(true);m_upstreamTolerance->setEnabled(true);
  m_upstreamSubmode->setEnabled(mode=="Q65");m_upstreamAverage->setEnabled(mode=="Q65" || mode=="Echo");
  m_upstreamTolerance->setMaximum(mode=="MSK144"?200:1000);
  if(mode=="MSK144" || mode=="Echo") {ui->TxFreqSpinBox->setValue(1500);ui->TxFreqSpinBox->setEnabled(false);}
  else ui->TxFreqSpinBox->setEnabled(true);
  ui->AutoSeqButton->setEnabled(mode!="Echo");ui->menuAutoSeq->setEnabled(mode!="Echo");m_managedMenu->setEnabled(mode!="Echo");
  ui->TxMinuteButton->setEnabled(mode!="Echo");ui->tuneButton->setEnabled(mode!="Echo");
  ui->DecodeButton->setEnabled(false);ui->actionOpen->setEnabled(false);ui->actionOpen_next_in_directory->setEnabled(false);ui->actionDecode_remaining_files_in_directory->setEnabled(false);
  ui->pbTxMode->setText("Tx "+mode);ui->pbTxMode->setEnabled(false);
  ui->decodedTextLabel->setText("UTC     dB   DT "+tr("Freq   Message"));ui->decodedTextLabel2->setText(ui->decodedTextLabel->text());
  m_upstreamStatus->setText(mode=="Echo"?tr("Enable TX manually to measure lunar echoes."):QString());
  Q_EMIT continuousAudio(true);configureUpstreamPeriod();genStdMsgs(m_rpt);
  updateGeneralWindows();
}

void MainWindow::configureUpstreamPeriod() {
  if(!upstreamMode() || m_upstreamPeriod->currentText().isEmpty())return;
  if(m_transmitting || g_iptt || m_upstream->encoding())on_stopTxButton_clicked();
  if(m_mode=="Q65") {
    QSignalBlocker blocker(m_upstreamSubmode);
    const int maximum=UpstreamOperatingState::q65MaximumSubmode(m_upstreamPeriod->currentText().toInt());
    const int selected=qBound(0,m_upstreamSubmode->currentIndex(),maximum);
    m_upstreamSubmode->clear();for(int i=0;i<=maximum;++i)m_upstreamSubmode->addItem(QString(QChar('A'+i)));
    m_upstreamSubmode->setCurrentIndex(selected);
  }
  m_TRperiod=m_upstreamPeriod->currentText().toInt();m_periodAudio.setPeriod(int(m_TRperiod));m_mskAudio.clear();m_upstreamState.resetCapture();
  m_upstream->stop();m_upstream->setContext(QString());resetUpstreamContext();m_upstreamLastSlot=-1;
  m_settings->beginGroup("UpstreamModes");m_settings->setValue(m_mode+"Period",int(m_TRperiod));m_settings->setValue("Q65Submode",m_upstreamSubmode->currentIndex());m_settings->endGroup();
  m_wideGraph->setPeriod(m_TRperiod,m_nsps);
  if(m_tci)Q_EMIT m_config.transceiver_period(m_TRperiod);else Q_EMIT audioPeriod(m_TRperiod);
  progressBar->setMaximum(int(m_TRperiod));setMinButton();
}

void MainWindow::resetUpstreamContext() {
  if(!m_upstream || !upstreamMode())return;
  QString context=m_config.my_callsign()+"|"+m_config.my_grid()+"|"+QString::number(m_freqNominal)+"|"+m_mode+"|"+QString::number(m_TRperiod)+"|"+m_upstreamSubmode->currentText()
    +"|"+m_hisCall+"|"+m_hisGrid+"|"+QString::number(ui->RxFreqSpinBox->value());
  if(context==m_upstream->context())return;
  if(m_transmitting || g_iptt || m_tx_when_ready || m_upstream->encoding())on_stopTxButton_clicked();
  m_upstream->setContext(context);m_periodAudio.reset();m_mskAudio.clear();m_upstreamState.resetCapture();m_upstreamClearAverage=true;
}

QJsonObject MainWindow::upstreamParameters() const {
  const int low=qBound(100,m_wideGraph->nStartFreq(),4900),high=qBound(low+1,m_wideGraph->Fmax(),5000);
  return {{"mode",m_mode},{"period",int(m_TRperiod)},{"submode",m_upstreamSubmode->currentIndex()},
    {"rx_hz",qBound(m_mode=="MSK144"?500:100,ui->RxFreqSpinBox->value(),m_mode=="MSK144"?2500:5000)},
    {"audio_hz",qBound(100,ui->TxFreqSpinBox->value()-m_XIT,5000)},{"tolerance",m_upstreamTolerance->value()},
    {"low_hz",low},{"high_hz",high},{"single_decode",m_config.single_decode()},
    {"depth",qBound(1,m_ndepth,3)},{"my_call",m_config.my_callsign().left(12)},{"his_call",m_hisCall.left(12)},
    {"his_grid",m_hisGrid.left(6)},{"progress",qBound(0,int(m_QSOProgress),5)},{"eme_delay",m_config.eme_delay()},
    {"average",m_upstreamAverage->isChecked()},{"clear_average",m_upstreamClearAverage}};
}

void MainWindow::upstreamAudio(QByteArray const& pcm,qint64 endUtcMs) {
  if(!upstreamMode() || !m_monitoring || ((m_transmitting || g_iptt) && !m_config.full_duplex()) || m_diskData)return;
  resetUpstreamContext();
  if(pcm.isEmpty() || pcm.size()%2 || pcm.size()>24000*5 || endUtcMs<0)return;
  if(m_mode=="MSK144") {
    if(!m_upstreamState.continuousMsk(endUtcMs,pcm.size()/2))m_mskAudio.clear();
    m_mskAudio.append(pcm);
    while(m_mskAudio.size()>=7168*2) {
      auto request=upstreamParameters();qint64 start=endUtcMs-(m_mskAudio.size()/2)*1000/12000;
      qint64 slot=start/(qint64(m_TRperiod)*1000);auto utc=QDateTime::fromMSecsSinceEpoch(slot*qint64(m_TRperiod)*1000,Qt::UTC);
      request["utc"]=utc.toString("hhmmss").toInt();request["tsec"]=(start-slot*qint64(m_TRperiod)*1000)/1000.;
      request["slot_utc_ms"]=double(slot*qint64(m_TRperiod)*1000);
      request["audio_end_ms"]=double(start+7168*1000/12000);
      m_upstream->decode(m_mskAudio.left(7168*2),request);m_mskAudio.remove(0,3584*2);
    }return;
  }
  auto frames=m_periodAudio.append(pcm,endUtcMs);
  if(m_mode=="Q65") {
    const qint64 periodMs=qint64(m_TRperiod)*1000;
    m_upstreamState.observeQ65(((endUtcMs-pcm.size()/24)/periodMs)*periodMs);
    const int nsps=m_TRperiod==15?1800:m_TRperiod==30?3600:m_TRperiod==60?7200:m_TRperiod==120?16000:41472;
    const double payloadEnd=(m_TRperiod<=30?.5:1.)+85.*nsps/12000.+m_config.eme_delay()+.2;
    PeriodAudioBuffer::Frame snapshot;
    if(payloadEnd<m_TRperiod && m_periodAudio.snapshot(qCeil(payloadEnd*12000.),snapshot))frames.append(snapshot);
  }
  for(auto const& frame:frames) {
    auto request=upstreamParameters();request["utc"]=QDateTime::fromMSecsSinceEpoch(frame.utcMs,Qt::UTC).toString("hhmmss").toInt();request["slot_utc_ms"]=double(frame.utcMs);
    QByteArray audio=frame.pcm;
    if(m_mode=="Echo") {
      // Moon echoes start after the round-trip light time, not at local midnight
      // or when the input device restarts after the 2.048-second transmission.
      UpstreamOperatingState::Echo echo;
      if(!m_upstreamState.takeEcho(frame.utcMs,echo))continue;
      const double delay=echo.delaySeconds;
      const int start=int((echo.soundStartMs-frame.utcMs)*12)+qRound(delay*12000);
      if(start<frame.missingPrefixSamples || start<0 || start+24576>audio.size()/2)continue;
      audio=audio.mid(start*2,28672*2);
      request["doppler_hz"]=qBound(-3000,qRound(echo.selfDopplerHz+echo.txCorrectionHz-m_astroCorrection.rx),3000);
      request["rit_hz"]=qBound(-1000,m_astro->rit(),1000);request["auto_mode"]=m_enableTx;
      request["averages"]=m_upstreamAverage->isChecked()?10:1;
      request["width_hz"]=qBound(.04,m_astronomicalData.value("width_self_hz").toDouble(1),700.);request["audio_hz"]=1500.+echo.dither;
    } else if(frame.partial) {
      continue; // A mode/clock change must not masquerade as a complete Q65 cycle.
    }
    m_processAuto_done=false;m_nDecodes=0;
    if(m_upstream->decode(audio,request))m_upstreamClearAverage=false;
  }
}

void MainWindow::receiveUpstream(QJsonObject const& decoded,QJsonObject const& request) {
  if(!upstreamMode() || request.value("mode").toString()!=m_mode)return;
  QString message=decoded.value("message").toString().trimmed();if(message.isEmpty() || messageBlacklisted(message))return;
  QString line=QString("%1 %2 %3 %4 %5 %6\n")
    .arg(request.value("utc").toInt(),6,10,QChar('0')).arg(decoded.value("snr").toInt(),3)
    .arg(decoded.value("dt").toDouble(),4,'f',1).arg(qRound(decoded.value("frequency").toDouble()),4)
    .arg(m_mode=="Q65"?":":"&").arg(message.left(37));
  processDecodedOutput(line.toUtf8());
}

void MainWindow::upstreamGuiUpdate() {
  resetUpstreamContext();auto now=m_jtdxtime->currentMSecsSinceEpoch2();qint64 period=qint64(m_TRperiod)*1000,slot=now/period,within=now%period;
  auto utc=m_jtdxtime->currentDateTimeUtc2();ui->labUTC->setText(utc.toString("hh:mm:ss"));date_label->setText(utc.toString("dd.MM.yyyy"));update_watchdog_label();
  progressBar->setValue(int(within/1000));
  progressBar->setStyleSheet(m_config.progressBar_red() && m_transmitting?QStringLiteral("QProgressBar::chunk {background-color: #d84a4a;}"):QString());
  if(m_watchdogFinishing || m_txSession.expired(m_config.watchdog()))txwatchdog(true);
  if(!m_enableTx || !m_manualTxAuthorized || m_transmitting || g_iptt || m_upstream->encoding() || m_upstreamLastSlot==slot || within>1000)return;
  bool txSlot=m_mode=="Echo" || ((slot%2==0)==m_txFirst);
  if(!txSlot)return;
  // Wait for the preceding receive cycle before choosing/encoding the reply.
  // If it is not ready inside the existing first-second deadline, skip this
  // slot instead of keying the transmitter with the previous message.
  if(m_mode=="Q65" && (m_upstream->busy() || m_upstreamState.waitingForQ65((slot-1)*period)))return;
  if(m_mode!="Echo" && ordinaryRetryDue()) {process_Auto();return;}
  if(m_mode!="Echo" && m_managed.active()) {
    process_Auto();
    if((slot%2==0)!=m_txFirst)return;
  }
  if(!managedTxAllowed())return;
  if(m_config.my_callsign().isEmpty() || m_logLoading || m_logImportActive)return;
  QString message=m_mode=="Echo"?QString("ECHO"):selectedTransmitText().simplified().toUpper();
  if(message.isEmpty() || messageBlacklisted(message)){if(!message.isEmpty())stopBlacklistedTransmit();return;}
  auto parameters=upstreamParameters();
  if(m_mode=="Echo") {m_echoDither=m_astro->dither()?int(QRandomGenerator::global()->bounded(21))-10:0;parameters["audio_hz"]=1500+m_echoDither;}
  m_upstreamTxSlot=slot;m_upstreamTxContext=m_upstream->context();m_upstreamRequestedMessage=message;
  if(m_upstream->encode(message,parameters)){m_upstreamLastSlot=slot;++m_upstreamTxRevision;}
}

void MainWindow::startUpstreamAudio() {
  resetUpstreamContext();
  auto now=m_jtdxtime->currentMSecsSinceEpoch2();qint64 period=qint64(m_TRperiod)*1000;
  if(!upstreamMode() || !m_enableTx || !m_manualTxAuthorized || !managedTxAllowed() || !g_iptt || m_transmitting || m_upstreamPcm.isEmpty()
     || m_upstreamTxContext!=m_upstream->context() || now/period!=m_upstreamTxSlot
     || (m_mode!="Echo" && selectedTransmitText().simplified().toUpper()!=m_upstreamRequestedMessage)) {on_stopTxButton_clicked();return;}
  if(permanentlyBlocked(m_hisCall) || messageBlacklisted(m_currentMessage)){stopBlacklistedTransmit();return;}
  qint64 delta=now-(m_upstreamTxSlot*period+m_upstreamStartDelay);
  if(delta>1000){on_stopTxButton_clicked();statusBar()->showMessage(tr("The transmit slot was missed; enable TX again."),15000);return;}
  auto pcm=m_upstreamPcm;
  if(m_mode!="Echo") {
    if(delta>0)pcm.remove(0,int(delta*48*2));else if(delta<0)pcm.prepend(QByteArray(int(-delta*48*2),0));
  }
  if(pcm.isEmpty()){on_stopTxButton_clicked();return;}
  m_transmitting=true;m_btxok=true;m_upstreamPcmConsumed=false;m_tx_when_ready=false;
  if(m_mode=="Echo") {
    UpstreamOperatingState::Echo echo;echo.soundStartMs=now;echo.dither=m_echoDither;
    echo.selfDopplerHz=m_astronomicalData.value("self_doppler_hz").toDouble();echo.txCorrectionHz=m_astroCorrection.tx;
    echo.delaySeconds=m_astronomicalData.value("echo_delay_seconds").toDouble(2.5);
    m_upstreamState.rememberEcho(m_upstreamTxSlot*period,echo);
  }
  m_upstreamTxTimeout.start(pcm.size()/96+5000);m_config.set_station_locked(true);transmitDisplay(true);
  m_emeTools->setEnabled(false);m_fullDuplexAction->setEnabled(false);
  prepareManagedTransmission();
  m_transmittedQSOProgress=m_QSOProgress;m_nlasttx=m_ntx;
  if(m_mode!="Echo") {
    ui->decodedTextBrowser2->displayTransmittedText(m_currentMessage,m_baseCall,Radio::base_callsign(m_hisCall),(m_skipTx1?"S":""),m_mode,ui->TxFreqSpinBox->value(),m_config.color_TxMsg(),m_qsoHistory);
    m_rptSent=QString::number(ui->rptSpinBox->value());
  }
  beginAudioPlayback();
  if(m_tci)Q_EMIT m_config.transceiver_tx_pcm(pcm,m_signoffGeneration);else Q_EMIT sendJttyPcm(pcm,m_soundOutput,m_config.audio_output_channel(),m_signoffGeneration);
  last_tx_label->setText(tr("LastTx: ")+m_currentMessage);tx_status_label->setText(tr("Transmitting"));
  mcpEvent("transmit",{{"mode",m_mode},{"message",m_currentMessage}});statusUpdate();
}

void MainWindow::finishUpstreamTransmit() {
  if(!upstreamMode() || !m_transmitting)return;
  m_managedTxConsumed=true;m_managedTxNatural=true;
  m_signoff.consumed();m_signoff.naturalEnd();
  ++m_upstreamTxRevision;m_upstreamTxTimeout.stop();Q_EMIT endTransmitMessage(false);Q_EMIT m_config.transceiver_ptt(false);
  m_transmitting=false;g_iptt=0;m_btxok=false;m_tx_when_ready=false;m_upstreamPcm.clear();m_upstreamPcmConsumed=false;
  m_emeTools->setEnabled(true);m_fullDuplexAction->setEnabled(true);m_config.set_station_locked(false);
  transmitDisplay(false);managedTransmissionFinished();
  signoffPttReleased();
  if(!m_monitoroff)monitor(true);
  // finishTxWatchdog() has already installed the expiry status above.
  if(!m_txwatchdog)tx_status_label->setText(m_monitoring?tr("Receiving"):tr("Stopped"));
  progressBar->setStyleSheet(QString());statusUpdate();
}

quint64 MainWindow::correctedFrequency(quint64 nominal,qint64 correction) const {
  return UpstreamOperatingState::shifted(nominal,correction);
}

void MainWindow::updateAstronomy() {
  if(!m_astro || (!m_astro->isVisible() && !m_astro->tracking() && m_mode!="Echo"))return;
  qint64 now=m_jtdxtime->currentMSecsSinceEpoch2();if(now-m_lastAstroRequest>=0 && now-m_lastAstroRequest<1000)return;
  m_lastAstroRequest=now;m_astro->setNominalFrequency(m_freqNominal);
  if(!gridOK(m_config.my_grid().left(4))) {m_astro->showError(tr("Set a valid station grid locator to calculate astronomical data."));applyAstroCorrection({});return;}
  QString context=m_config.my_callsign()+"|"+m_config.my_grid()+"|"+m_hisGrid+"|"+QString::number(m_freqNominal);
  m_astroBackend->setContext(context);
  QJsonObject request{{"utc_ms",double(now)},{"frequency_hz",double(m_freqNominal)},{"my_grid",m_config.my_grid().left(6)},{"his_grid",m_hisGrid.left(6)},
    {"ephemeris_path",m_config.data_dir().absoluteFilePath("JPLEPH")}};
  if(!m_config.tx_QSY_allowed()) {
    qint64 period=qint64(m_TRperiod*1000),slot=now/period;
    if(m_mode!="Echo" && ((slot%2==0)!=m_txFirst))++slot;
    request["tx_utc_ms"]=double(slot*period+period/2);
  }
  m_astroBackend->astronomy(request);
}

void MainWindow::applyAstroCorrection(QJsonObject const& data) {
  if(QApplication::keyboardModifiers() & Qt::ControlModifier)return;
  AstroDialog::Correction correction;
  if(m_astro->tracking() && m_config.split_mode() && m_freqNominal>=21000000 && !data.isEmpty()
     && qAbs(m_jtdxtime->currentMSecsSinceEpoch2()-qint64(data.value("utc_ms").toDouble()))<5000)
    correction=m_astro->correction(data,m_mode=="Echo",!m_config.tx_QSY_allowed());
  if((m_transmitting || g_iptt) && !m_config.tx_QSY_allowed())return;
  if(correction.rx==m_astroCorrection.rx && correction.tx==m_astroCorrection.tx)return;
  m_astroCorrection=correction;
  if(m_config.transceiver_online()) {setRig();setXIT(ui->TxFreqSpinBox->value());}
}
