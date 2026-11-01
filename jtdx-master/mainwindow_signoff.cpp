#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "Detector.hpp"
#include "Modulator.hpp"
#include "logqso.h"
#include <QScopedValueRollback>
#include <QtConcurrent/QtConcurrentRun>

extern qint32 g_iptt;

namespace {
QString cleanCall(QString value) {return value.remove('<').remove('>').trimmed().toUpper();}
}

void MainWindow::setupSignoff()
{
  m_txTimingPool.setMaxThreadCount(1);
  auto started=[this](quint64 generation){
    if(generation==m_signoffGeneration && generation==m_txStartAudioGeneration
       && m_txAudioRequestedMs>=0 && m_txStartClock.isValid() && !m_txStartReported
       && m_enableTx && m_manualTxAuthorized && !m_tune) {
      m_txStartReported=true;
      auto elapsed=m_txStartClock.elapsed();
      if(elapsed>1000 || m_config.write_decoded_debug())
        writeTxTiming(QString("TX start timing: phase=first_samples mode=%1 call=%2 ptt_ack_ms=%3 audio_request_ms=%4 first_samples_ms=%5")
          .arg(m_modeTx,m_hisCall).arg(m_txPttAckMs).arg(m_txAudioRequestedMs).arg(elapsed));
    }
    if(generation==m_managedTxGeneration && (m_managedCallOnAir || m_managedCqOnAir)) {
      m_managedTxStarted=true;managedTransmissionFinished();
    }
    if(!m_signoff.active() || generation!=m_signoffGeneration || m_tune || m_signoffRoundStarted
       || m_signoffSelectionRevision!=m_generalSelectionRevision)return;
    m_signoff.started();m_signoffRoundStarted=true;
    m_signoffLogOff=m_jtdxtime->currentDateTimeUtc2();
    logSignoffRound();
  };
  connect(m_modulator,&Modulator::waveformStarted,this,started);
  connect(&m_config,&Configuration::transceiver_waveform_started,this,started);
  connect(&m_signoffLogUpdateWatcher,&QFutureWatcher<QString>::finished,this,[this]{
    auto error=m_signoffLogUpdateWatcher.result();
    m_signoffLogWriting=false;
    if(!error.isEmpty()) {statusBar()->showMessage(error,20000);mcpEvent("qso_update_failed",{{"error",error}});}
    startSignoffLogUpdate();flushSignoffLogRefresh();
  });
  auto consumed=[this](quint64 generation){
    if(generation==m_managedTxGeneration && m_managedTxStarted
       && (m_managedCallOnAir || m_managedCqOnAir)) {
      m_managedTxConsumed=true;managedTransmissionFinished();
    }
    if(m_signoff.active() && generation==m_signoffGeneration && !m_tune
       && m_signoffSelectionRevision==m_generalSelectionRevision) {
      m_signoff.consumed();signoffPttReleased();
    }
  };
  connect(m_modulator,&Modulator::waveformConsumed,this,consumed);
  connect(&m_config,&Configuration::transceiver_waveform_consumed,this,consumed);
  auto audio=[this](QByteArray const& pcm,qint64 endMs){
    if(!m_signoff.active() || !m_monitoring || m_diskData || pcm.size()%2
       || ((m_transmitting || g_iptt) && !m_config.full_duplex()))return;
    for(auto slot:m_signoffAudio.append(endMs,pcm.size()/2,qRound64(m_TRperiod*1000))) {
      m_signoffAudioCycles.insert(slot);
      if(m_signoffDecodedCycles.contains(slot))m_signoff.receiveCycleFinished(slot);
    }
    finishSignoff();
  };
  connect(m_detector,&Detector::timedPcmWritten,this,[this,audio](QByteArray const& pcm,qint64 end){if(!m_tci)audio(pcm,end);});
  connect(&m_config,&Configuration::transceiver_timed_pcm,this,[this,audio](QByteArray const& pcm,qint64 end){if(m_tci)audio(pcm,end);});
}

void MainWindow::prepareSignoff(QString const& message)
{
  auto words=message.simplified().split(' ');
  if(m_tune || m_houndMode || words.isEmpty() || m_hisCall.isEmpty())return;
  bool legacy=!m_mode.startsWith("FT") && !upstreamMode();
  bool addressed=words.size()>=3 && (cleanCall(words.first())==cleanCall(m_hisCall)
    || (legacy && cleanCall(words.first())==Radio::base_callsign(m_hisCall)));
  if(!addressed && !(legacy && words.size()==1 && (m_ntx==4 || m_ntx==5)))return;
  auto kind=words.last()=="RR73"?SignoffState::RR73:words.last()=="73"?SignoffState::SeventyThree:SignoffState::None;
  if(kind==SignoffState::None)return;
  qint64 period=qRound64(m_TRperiod*1000),now=m_jtdxtime->currentMSecsSinceEpoch2();
  if(period<=0)return;
  if(!m_signoff.matches(m_hisCall,generalContext()) || m_signoffSelectionRevision!=m_generalSelectionRevision) {
    m_signoffLogOriginal.clear();m_signoffLogOwner.clear();m_signoffLogOn=QDateTime();m_signoffLogOff=QDateTime();
    m_signoffLogPromptRequested=false;
  }
  m_signoffRoundStarted=false;m_signoffRoundEndRecorded=false;
  ++m_signoffGeneration;
  m_signoff.prepare(m_hisCall,generalContext(),kind,now/period*period,period);
  m_signoffSelectionRevision=m_generalSelectionRevision;
  m_signoffAudio.reset();m_signoffDecodedCycles.clear();m_signoffAudioCycles.clear();m_signoffRxCycleEnd=0;
  // Logging at selection used to revoke the pending PTT/audio start. The
  // generation-tagged first-sample event now starts logging instead.
  logQSOTimer.stop();logClearDXTimer.stop();
}

void MainWindow::observeSignoff(QString const& row)
{
  if(!m_signoff.active() || m_diskData || m_manualDecode)return;
  DecodedText decoded(row,this,upstreamMode());
  if(decoded.isWrong() || decoded.isTX() || decoded.isDebug())return;
  auto words=decoded.message().remove('<').remove('>').simplified().split(' ');
  if(words.size()<3 || (cleanCall(words.first())!=cleanCall(m_config.my_callsign())
      && cleanCall(words.first())!=cleanCall(m_baseCall)))return;
  QString sender,grid;decoded.deCallAndGrid(sender,grid);
  bool same=cleanCall(sender)==cleanCall(m_signoff.call());
  if(!same && !m_mode.startsWith("FT") && !upstreamMode())
    same=cleanCall(sender)==Radio::base_callsign(m_signoff.call());
  if(!same)return;
  auto text=row.left(6).trimmed();auto time=QTime::fromString(text,text.size()==6?"hhmmss":"hhmm");
  if(!time.isValid())return;
  auto stamp=m_jtdxtime->currentDateTimeUtc2();stamp.setTime(time);
  auto now=m_jtdxtime->currentMSecsSinceEpoch2();
  if(stamp.toMSecsSinceEpoch()>now+43200000)stamp=stamp.addDays(-1);
  else if(stamp.toMSecsSinceEpoch()<now-43200000)stamp=stamp.addDays(1);
  qint64 period=m_signoff.period();
  // Display timestamps omit fractional seconds (FT4's 7.5-second slots).
  auto slot=(stamp.toMSecsSinceEpoch()+999)/period*period;
  m_signoff.receive(m_signoff.call(),generalContext(),words.last(),slot);
}

void MainWindow::signoffReceiveCycleFinished(qint64 slot)
{
  if(!m_signoff.active() || slot<0 || m_diskData || m_manualDecode || m_decoderRecovering)return;
  m_signoffDecodedCycles.insert(slot);
  if(m_signoffAudioCycles.contains(slot))m_signoff.receiveCycleFinished(slot);
  m_signoffRxCycleEnd=qMax(m_signoffRxCycleEnd,slot+m_signoff.period());
  finishSignoff();
}

void MainWindow::signoffPttReleased()
{
  if(!m_signoff.active() || m_transmitting || g_iptt || m_tx_when_ready
     || ptt0Timer.isActive() || ptt1Timer.isActive() || m_rigState.ptt())return;
  m_signoff.released();
  if(m_signoff.waveformFinished() && !m_signoffRoundEndRecorded) {
    m_signoffRoundEndRecorded=true;logSignoffRound(true);
  }
  finishSignoff();
}

bool MainWindow::finishSignoff()
{
  if(m_finishingSignoff || !m_signoff.active())return false;
  if(!m_signoff.matches(m_hisCall,generalContext()) || m_signoffSelectionRevision!=m_generalSelectionRevision) {
    m_signoff.reset();return false;
  }
  if(!m_signoff.complete() || m_transmitting || g_iptt || m_tx_when_ready
     || ptt0Timer.isActive() || ptt1Timer.isActive() || m_rigState.ptt())return false;
  if(m_signoff.message()==SignoffState::SeventyThree
     && (m_decoderBusy || m_jtdxtime->currentMSecsSinceEpoch2()<m_signoffRxCycleEnd))return false;
  auto call=m_signoff.call();
  // A successful retry of the first log write can synchronously call
  // generalTick through acceptQSO. Complete and hand off this QSO only once.
  QScopedValueRollback<bool> finishing(m_finishingSignoff,true);
  logSignoffRound(); // Retry an earlier write failure, retaining the same identity.
  m_signoff.reset();flushSignoffLogRefresh();
  m_qsoHistory.complete(call);m_status=QsoHistory::FIN;
  // Snapshot before clearing editors; acceptQSO may synchronously apply the
  // post-QSO options, so subsequent code must not read the old selection.
  if(m_watchdogFinishing){finishTxWatchdog();return true;}
  if(m_managed.active() && m_managed.current()==call) {
    m_managed.complete(m_managedClock.elapsed());m_managedTxReady=false;
  }
  if(m_singleshot && !m_managed.active())autoStopTx(" confirmed single QSO complete ");
  else if(m_hisCall==call && (m_managed.active() || m_autoseq)) {
    clearDX(" cleared, final exchange confirmed");
    if(m_config.set_RXtoTX())ui->RxFreqSpinBox->setValue(ui->TxFreqSpinBox->value());
    // DecodeFinished may already have spent this cycle's AutoSeq pass before
    // the audio/peer confirmation arrived. Revisit its waiting callers once
    // all decodes and log updates are ready, before sending the selected CQ.
    queueOrdinaryAutoSelection();
  }
  return true;
}

void MainWindow::logSignoffRound(bool ending)
{
  if(!m_signoff.active() || (!m_signoffRoundStarted && !m_signoff.waveformFinished())
     || (!m_config.prompt_to_log() && !m_config.autolog()))return;
  if(ending || !m_signoffLogOff.isValid())m_signoffLogOff=m_jtdxtime->currentDateTimeUtc2();
  if(!m_signoffLogOriginal.isEmpty()) {
    auto record=m_signoffLogOriginal;
    record["QSO_DATE_OFF"]=m_signoffLogOff.toUTC().toString("yyyyMMdd");
    record["TIME_OFF"]=m_signoffLogOff.toUTC().toString("hhmmss");
    if(!m_rptSent.isEmpty())record["RST_SENT"]=m_rptSent;
    if(!m_rptRcvd.isEmpty())record["RST_RCVD"]=m_rptRcvd;
    queueSignoffLogUpdate(record);return;
  }
  if(m_signoffLogPromptRequested) {
    // A pending manual dialog owns its snapshot even if this QSO later ends.
    // Update only the round-dependent fields; preserve comments and TIME_ON.
    if(m_logDlg->isVisible() && m_generalLogSelectionRevision==m_signoffSelectionRevision
       && m_generalLogContext==generalContext())
      m_logDlg->updateSignoffSnapshot(m_signoffLogOff,m_rptSent,m_rptRcvd);
    return;
  }
  // Never replace a different QSO's still-open manual logging dialog.
  if(m_logDlg->isVisible())return;
  m_signoffLogPromptRequested=!m_config.autolog();
  QScopedValueRollback<bool> logging(m_loggingCompletedSignoff,true);
  logQSOTimer.stop();m_logqso73=true;on_logQSOButton_clicked();
}

void MainWindow::queueSignoffLogUpdate(OperatorLog::Record const& record)
{
  if(m_signoffLogOriginal.isEmpty() || record==m_signoffLogOriginal)return;
  auto identity=OperatorLog::identity(m_signoffLogOriginal,m_signoffLogOwner);
  for(auto& job:m_signoffLogUpdates) {
    if(job.owner==m_signoffLogOwner && OperatorLog::identity(job.original,job.owner)==identity) {
      job.replacement=record;return;
    }
  }
  SignoffLogUpdate job;job.owner=m_signoffLogOwner;job.original=m_signoffLogOriginal;job.replacement=record;
  m_signoffLogUpdates.append(job);startSignoffLogUpdate();
}

void MainWindow::startSignoffLogUpdate()
{
  if(m_signoffLogWriting || m_signoffLogUpdates.isEmpty())return;
  auto job=m_signoffLogUpdates.takeFirst();
  m_signoffLogWriting=true;
  m_signoffLogUpdateWatcher.setFuture(QtConcurrent::run([job]{
    QString error;
    OperatorLog::updateQso(job.owner,job.original,job.replacement,&error);
    return error;
  }));
}

void MainWindow::flushSignoffLogRefresh()
{
  if(!m_signoffLogRefreshDeferred || m_signoff.active() || m_signoffLogWriting
     || !m_signoffLogUpdates.isEmpty())return;
  m_signoffLogRefreshDeferred=false;m_logRefreshTimer.start(400);
}

void MainWindow::drainSignoffLogUpdates()
{
  // Jobs contain their original owner and record identity. Finish them before
  // Configuration is destroyed; a new operator never becomes their owner.
  m_signoffLogUpdateWatcher.waitForFinished();
  if(m_signoffLogWriting && m_signoffLogUpdateWatcher.future().resultCount()>0
     && !m_signoffLogUpdateWatcher.result().isEmpty())
    qWarning()<<"QSO update failed during shutdown:"<<m_signoffLogUpdateWatcher.result();
  m_signoffLogWriting=false;
  while(!m_signoffLogUpdates.isEmpty()) {
    auto job=m_signoffLogUpdates.takeFirst();QString error;
    if(!OperatorLog::updateQso(job.owner,job.original,job.replacement,&error))
      qWarning()<<"QSO update failed during shutdown:"<<error;
  }
}
