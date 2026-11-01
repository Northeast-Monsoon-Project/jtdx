// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "JttyController.hpp"
#include "Detector.hpp"
#include "Modulator.hpp"
#include "soundout.h"
#include "displaytext.h"
#include "widegraph.h"
extern qint32 g_iptt;

void MainWindow::setupJtty() {
  m_jtty=new JttyController(this);
  m_jttyPage=new QWidget(ui->controls_stack_widget);m_jttyPage->setObjectName("jtty_page");
  auto layout=new QVBoxLayout(m_jttyPage);layout->setContentsMargins(6,4,6,4);layout->setSpacing(4);
  auto hint=new QLabel(tr("Enable TX manually, then Send each message (max. 80 characters)."));
  hint->setWordWrap(true);hint->setMinimumHeight(2*hint->fontMetrics().height());layout->addWidget(hint);
  m_jttyLive=new QLabel(tr("Listening…"));m_jttyLive->setTextFormat(Qt::PlainText);m_jttyLive->setWordWrap(true);m_jttyLive->setMinimumHeight(40);layout->addWidget(m_jttyLive);
  auto row=new QHBoxLayout;layout->addLayout(row);
  m_jttyText=new QLineEdit;m_jttyText->setObjectName("jtty_text");m_jttyText->setMaxLength(80);row->addWidget(m_jttyText,1);
  m_jttySend=new QPushButton(tr("Send"));m_jttySend->setObjectName("jtty_send");row->addWidget(m_jttySend);
  auto macros=new QHBoxLayout;layout->addLayout(macros);
  for(auto label:{QT_TR_NOOP("CQ"),QT_TR_NOOP("CALL"),QT_TR_NOOP("REPORT"),QT_TR_NOOP("73")}) {
    auto button=new QPushButton(tr(label));macros->addWidget(button);
    connect(button,&QPushButton::clicked,this,[this,label]{
      QString text;
      if(QString(label)=="CQ")text="CQ "+m_config.my_callsign()+" CQ";
      else if(QString(label)=="CALL")text=m_hisCall+" "+m_config.my_callsign();
      else if(QString(label)=="REPORT")text=m_hisCall+" 599 "+m_config.my_grid().left(4);
      else text=m_hisCall+" 73 "+m_config.my_callsign();
      m_jttyText->setText(text.trimmed());m_jttyText->setFocus();
    });
  }
  ui->controls_stack_widget->addWidget(m_jttyPage);
  connect(m_jttySend,&QPushButton::clicked,this,&MainWindow::requestJttyTransmit);
  connect(m_jttyText,&QLineEdit::returnPressed,this,&MainWindow::requestJttyTransmit);
  connect(m_jttyText,&QLineEdit::textChanged,this,&MainWindow::updateJttyUi);
  connect(this,&MainWindow::continuousAudio,m_detector,&Detector::setContinuous);
  connect(this,&MainWindow::sendJttyPcm,m_modulator,&Modulator::startPcm);
  connect(m_detector,&Detector::pcmWritten,this,[this](QByteArray const& pcm){
    if(m_mode!="JTTY" || !m_monitoring || ((m_transmitting || g_iptt) && !m_config.full_duplex()))return;
    if(!m_jttyRxAnchor)m_jttyRxAnchor=m_jtdxtime->currentMSecsSinceEpoch2()-pcm.size()*1000/24000;
    m_jtty->setReceiveRange(m_wideGraph->nStartFreq(),m_wideGraph->Fmax(),ui->RxFreqSpinBox->value());
    m_jtty->audio(pcm);
  });
  connect(m_jtty,&JttyController::received,this,&MainWindow::receiveJtty);
  connect(m_jtty,&JttyController::error,this,[this](QString message){
    if(m_mode=="JTTY") {on_stopTxButton_clicked();m_jttyLive->setText(message);statusBar()->showMessage(message,20000);}
  });
  connect(m_jtty,&JttyController::encoded,this,[this](QString text,QByteArray pcm){
    if(!m_jttyPreparing || m_mode!="JTTY" || !m_enableTx || !m_manualTxAuthorized ||
       m_txSession.expired(m_config.watchdog()) || m_jttyTxContext!=m_config.my_callsign()+"|"+QString::number(m_freqNominal)+"|"+QString::number(ui->TxFreqSpinBox->value())) {
      m_jttyPreparing=false;updateJttyUi();return;
    }
    if(permanentlyBlocked(m_hisCall) || messageBlacklisted(text)) {
      stopBlacklistedTransmit();return;
    }
    m_jttyPendingText.clear();
    m_jttyPreparing=false;m_jttyPcm=pcm;m_currentMessage=text;m_jttyPcmConsumed=false;
    if(!m_config.full_duplex())monitor(false);
    m_tx_when_ready=true;g_iptt=1;setRig();setXIT(ui->TxFreqSpinBox->value());
    m_jttyPttTimeout.start(10000);Q_EMIT m_config.transceiver_ptt(true);updateJttyUi();
  });
  m_jttyPttTimeout.setSingleShot(true);
  connect(&m_jttyPttTimeout,&QTimer::timeout,this,[this]{
    if(m_mode=="JTTY"){on_stopTxButton_clicked();statusBar()->showMessage(tr("JTTY transmission timed out; TX stopped."),15000);}
  });
  connect(this,&MainWindow::finished,m_jtty,[this]{m_jtty->setActive(false);});
}
void MainWindow::on_actionJTTY_triggered() {
  on_stopTxButton_clicked();setManagedMode(ManagedMode::Off);
  m_mode=m_modeTx="JTTY";m_TRperiod=60;m_hsymStop=173;
  WSPR_config(false);switch_mode(Modes::JTTY);commonActions();enableHoundAccess(false);
  on_AutoSeqButton_clicked(false);ui->actionJTTY->setChecked(true);
  ui->controls_stack_widget->setCurrentWidget(m_jttyPage);
  ui->AutoSeqButton->setEnabled(false);ui->AutoTxButton->setEnabled(false);ui->singleQSOButton->setEnabled(false);
  ui->menuAutoSeq->setEnabled(false);m_managedMenu->setEnabled(false);
  ui->TxMinuteButton->setEnabled(false);ui->TxMinuteButton->setText("JTTY");ui->tuneButton->setEnabled(false);
  ui->DecodeButton->setEnabled(false);ui->actionOpen->setEnabled(false);ui->actionOpen_next_in_directory->setEnabled(false);ui->actionDecode_remaining_files_in_directory->setEnabled(false);
  ui->pbTxMode->setText("Tx JTTY");ui->pbTxMode->setEnabled(false);
  clearBandActivity();ui->decodedTextBrowser2->clear();
  m_jttyRxAnchor=0;m_jtty->setActive(true);Q_EMIT continuousAudio(true);updateJttyUi();
}
void MainWindow::updateJttyUi() {
  if(!m_jttySend)return;
  m_jttySend->setEnabled(m_mode=="JTTY" && m_enableTx && m_manualTxAuthorized && !m_transmitting && !g_iptt && !m_jttyPreparing && !m_watchdogFinishing && !m_jttyText->text().trimmed().isEmpty());
}
void MainWindow::requestJttyTransmit() {
  if(m_mode!="JTTY" || !m_enableTx || !m_manualTxAuthorized || m_transmitting || g_iptt || m_jttyPreparing)return;
  if(m_txSession.expired(m_config.watchdog()) || m_watchdogFinishing){txwatchdog(true);return;}
  if(m_tci){statusBar()->showMessage(tr("JTTY requires a local audio input and output device."),15000);return;}
  auto text=m_jttyText->text().simplified().toUpper();if(text.isEmpty())return;
  if(permanentlyBlocked(m_hisCall) || messageBlacklisted(text)) {
    stopBlacklistedTransmit();return;
  }
  m_jttyTxContext=m_config.my_callsign()+"|"+QString::number(m_freqNominal)+"|"+QString::number(ui->TxFreqSpinBox->value());
  m_jttyPendingText=text;
  m_jttyPreparing=m_jtty->encode(text,ui->TxFreqSpinBox->value()-m_XIT);updateJttyUi();
}
void MainWindow::startJttyAudio() {
  if(m_mode!="JTTY" || !m_enableTx || !m_manualTxAuthorized || m_jttyPcm.isEmpty() || !g_iptt || m_transmitting || m_txSession.expired(m_config.watchdog()) ||
     m_jttyTxContext!=m_config.my_callsign()+"|"+QString::number(m_freqNominal)+"|"+QString::number(ui->TxFreqSpinBox->value())) {
    if(m_mode=="JTTY")on_stopTxButton_clicked();
    return;
  }
  if(permanentlyBlocked(m_hisCall) || messageBlacklisted(m_currentMessage)) {
    stopBlacklistedTransmit();return;
  }
  m_transmitting=true;m_jttyOnAir.start();m_jttyPttTimeout.start(m_jttyPcm.size()*1000/(48000*2)+10000);
  m_config.set_station_locked(true);transmitDisplay(true);
  beginAudioPlayback();
  Q_EMIT sendJttyPcm(m_jttyPcm,m_soundOutput,m_config.audio_output_channel(),m_audioPlaybackGeneration);
  last_tx_label->setText(tr("LastTx: ")+m_currentMessage);tx_status_label->setText(tr("Transmitting"));
  ui->decodedTextBrowser2->displayOwnText(m_jtdxtime->currentDateTimeUtc2().toString("hhmmss")+" TX JTTY "+m_currentMessage);
  mcpEvent("jtty_transmit",{{"message",m_currentMessage}});updateJttyUi();statusUpdate();
}
void MainWindow::finishJttyTransmit() {
  m_jttyPttTimeout.stop();Q_EMIT endTransmitMessage(false);Q_EMIT m_config.transceiver_ptt(false);
  m_transmitting=false;g_iptt=0;m_btxok=false;m_tx_when_ready=false;m_jttyPcm.clear();m_jttyPcmConsumed=false;
  transmitDisplay(false);tx_status_label->setText(tr("Receiving"));
  if(m_watchdogFinishing)finishTxWatchdog();
  if(!m_monitoroff)monitor(true);
  updateJttyUi();statusUpdate();
}
void MainWindow::receiveJtty(QJsonObject const& update) {
  if(m_mode!="JTTY")return;
  auto text=update.value("text").toString();
  if(messageBlacklisted(text))return;
  // A partial callsign is not yet identifiable. With an active blacklist,
  // wait for the completed message before displaying its live preview.
  if(m_blacklist && !m_blacklist->entries().isEmpty() && update.value("terminal").toInt()==0)return;
  m_jttyLive->setText(text);
  auto stamp=QDateTime::fromMSecsSinceEpoch(m_jttyRxAnchor+qRound64(update.value("seconds").toDouble()*1000),Qt::UTC).toString("hhmmss");
  auto object=update;object["utc"]=stamp;object["mode"]="JTTY";mcpEvent("jtty_receive",object);
  if(update.value("terminal").toInt()!=0) {
    auto line=stamp+QString(" %1 %2 JTTY ").arg(update.value("snr").toInt(),3).arg(qRound(update.value("frequency").toDouble()),4)+text;
    ui->decodedTextBrowser->appendText(line);
    object["raw"]=line;object["message_id"]=object.value("id");object["id"]=++m_mcpDecodeId;object["disk"]=false;object["dial_hz"]=double(m_freqNominal);m_mcpDecodes.append(object);while(m_mcpDecodes.size()>500)m_mcpDecodes.removeFirst();
  }
}
