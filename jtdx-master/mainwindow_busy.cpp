#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QStatusBar>

extern qint32 g_iptt;

namespace {
QString busyBaseCall(QString call)
{
  return Radio::base_callsign(call.remove('<').remove('>').trimmed());
}

int busyTimeDifference(int current,int previous)
{
  // Decoder passes can deliver earlier rows after later ones. Compare the
  // received UTC slots, including a QSO spanning midnight, not arrival order.
  int difference=current-previous;
  if(difference>43200)difference-=86400;
  if(difference< -43200)difference+=86400;
  return difference;
}
}

bool MainWindow::ordinaryBusyContextValid() const
{
  return !m_ordinaryBusyCall.isEmpty() && m_autoseq && m_config.halttxreplyother()
    && m_enableTx && m_manualTxAuthorized && !m_managed.active() && !m_houndMode
    && m_ordinaryBusyContext==generalContext()
    && m_ordinaryBusyRevision==m_generalSelectionRevision
    && busyBaseCall(m_hisCall)==busyBaseCall(m_ordinaryBusyCall);
}

bool MainWindow::ordinaryBusyWaiting() const
{
  return m_ordinaryBusyActive && ordinaryBusyContextValid();
}

void MainWindow::resetOrdinaryBusy()
{
  m_ordinaryBusyCall.clear();m_ordinaryBusyContext.clear();m_ordinaryBusyReadyRow.clear();
  m_ordinaryBusyReadyAt=-1;m_ordinaryBusyDirectReply=false;m_ordinaryBusyHadExchange=false;
  m_ordinaryBusyUtc=-1;m_ordinaryBusyActive=false;
}

void MainWindow::refreshOrdinaryBusy()
{
  if(!m_ordinaryBusyCall.isEmpty() && !ordinaryBusyContextValid())resetOrdinaryBusy();
}

bool MainWindow::beginOrdinaryBusy(DecodedText& decoded)
{
  refreshOrdinaryBusy();
  if(!m_autoseq || !m_config.halttxreplyother() || !m_enableTx || !m_manualTxAuthorized
     || m_managed.active() || m_houndMode || m_tune || m_diskData || m_manualDecode
     || m_signoff.active() || m_generalState.waiting() || decoded.isWrong() || decoded.isTX()
     || decoded.isDebug() || m_hisCall.isEmpty() || permanentlyBlocked(m_hisCall)
     || messageBlacklisted(decoded.message()))return false;
  QString sender,grid;decoded.deCallAndGrid(sender,grid);
  auto receiver=busyBaseCall(decoded.call());
  if(busyBaseCall(sender)!=busyBaseCall(m_hisCall) || receiver.size()<3
     || !Radio::is_callsign(receiver) || receiver==busyBaseCall(m_config.my_callsign()))return false;
  if(!m_generalClock.isValid())m_generalClock.start();
  bool alreadyWaiting=ordinaryBusyWaiting();
  int utc=decoded.timeInSeconds();
  if(ordinaryBusyContextValid() && m_ordinaryBusyUtc>=0) {
    int difference=busyTimeDifference(utc,m_ordinaryBusyUtc);
    if(difference<0 || (difference==0 && m_ordinaryBusyReadyAt>=0))return false;
  }
  m_ordinaryBusyCall=m_hisCall;m_ordinaryBusyContext=generalContext();
  m_ordinaryBusyRevision=m_generalSelectionRevision;
  m_ordinaryBusyUtc=utc;m_ordinaryBusyActive=true;
  m_ordinaryBusyReadyRow.clear();m_ordinaryBusyReadyAt=-1;m_ordinaryBusyDirectReply=false;
  unsigned time=0;QString received,sent;
  m_qsoHistory.log_data(m_hisCall,time,received,sent);
  m_ordinaryBusyHadExchange=(alreadyWaiting && m_ordinaryBusyHadExchange) || !received.isEmpty();
  if(!alreadyWaiting)statusBar()->showMessage(
    tr("%1 is replying to another station; waiting for its CQ or final message.").arg(m_hisCall),10000);
  return true;
}

void MainWindow::observeOrdinaryBusy(DecodedText& decoded)
{
  refreshOrdinaryBusy();
  if(!m_autoseq || !m_config.halttxreplyother() || !m_enableTx || !m_manualTxAuthorized
     || m_managed.active() || m_houndMode || m_tune || m_diskData || m_manualDecode
     || m_signoff.active() || m_generalState.waiting() || decoded.isWrong() || decoded.isTX()
     || decoded.isDebug() || m_hisCall.isEmpty() || permanentlyBlocked(m_hisCall)
     || messageBlacklisted(decoded.message()))return;
  QString sender,grid;decoded.deCallAndGrid(sender,grid);
  if(busyBaseCall(sender)!=busyBaseCall(m_hisCall))return;
  auto text=decoded.message();text.remove('<').remove('>');
  auto words=text.simplified().split(' ',SkipEmptyParts);
  bool direct=!words.isEmpty() && busyBaseCall(words.first())==busyBaseCall(m_config.my_callsign());
  bool cq=words.value(0)=="CQ";
  bool ending=!words.isEmpty() && (words.last()=="73" || words.last()=="RR73" || words.last()=="RRR");
  if(!direct && !cq && !ending)return;
  int utc=decoded.timeInSeconds();
  if(m_ordinaryBusyUtc>=0) {
    int difference=busyTimeDifference(utc,m_ordinaryBusyUtc);
    if(difference<0 || (difference==0 && m_ordinaryBusyDirectReply && !direct))return;
  }
  // Remember availability even before any wait starts. Later decoder passes
  // must not turn an older report into a new wait after a newer CQ/final row.
  if(!m_generalClock.isValid())m_generalClock.start();
  m_ordinaryBusyCall=m_hisCall;m_ordinaryBusyContext=generalContext();
  m_ordinaryBusyRevision=m_generalSelectionRevision;m_ordinaryBusyUtc=utc;
  m_ordinaryBusyReadyRow=decoded.string();m_ordinaryBusyReadyAt=m_generalClock.elapsed();
  m_ordinaryBusyDirectReply=direct;
  if(direct) {
    unsigned time=0;QString received,sent;
    m_qsoHistory.log_data(m_hisCall,time,received,sent);
    if(!received.isEmpty())m_ordinaryBusyHadExchange=true;
  }
}

void MainWindow::ordinaryBusyTick()
{
  refreshOrdinaryBusy();
  if(!ordinaryBusyWaiting())return;
  // A late full-duplex decode may suspend only the next transmission. Keep
  // the current keyed waveform, its callbacks and the PTT tail intact.
  if(m_transmitting || g_iptt || m_tx_when_ready || ptt0Timer.isActive()
     || ptt1Timer.isActive() || m_rigState.ptt() || m_tune)return;
  if(!m_ordinaryBusyHadExchange && (m_watchdogFinishing || m_txSession.expired(m_config.watchdog()))) {
    resetOrdinaryBusy();finishTxWatchdog();return;
  }
  if(m_decoderBusy || m_diskData || m_manualDecode)return;
  // Waiting never resets or postpones a budget that was already exhausted.
  if(ordinaryRetryDue()) {resetOrdinaryBusy();process_Auto();return;}
  if(m_ordinaryBusyReadyRow.isEmpty())return;
  if(m_generalClock.elapsed()-m_ordinaryBusyReadyAt>qRound64(m_TRperiod*2000)+2000) {
    m_ordinaryBusyReadyRow.clear();return; // Require a fresh invitation after long log work.
  }
  if(!m_logReady || m_logLoading || m_logImportActive || m_logRefreshPending
     || permanentlyBlocked(m_hisCall) || messageBlacklisted(m_ordinaryBusyReadyRow)
     || !watchdogTxAllowed() || m_generalState.waiting())return;
  if(!m_ordinaryBusyDirectReply && m_qsoHistory.outgoingCallDeferred(m_hisCall))return;
  DecodedText decoded(m_ordinaryBusyReadyRow,this,upstreamMode());
  auto target=m_hisCall;
  // Use the normal AutoSeq state already updated by DisplayText. Do not
  // reseed history or reset counts as a synthetic manual selection would.
  assignTxSlot(ManagedSlotPolicy::replyTxFirst(decoded.timeInSeconds(),m_TRperiod));
  m_reply_other=false;
  // Keep the UTC watermark after resuming: a late callback from the previous
  // exchange must not start another wait. Explicit cancellation still resets it.
  m_ordinaryBusyActive=false;process_Auto();
  if(m_enableTx && busyBaseCall(m_hisCall)==busyBaseCall(target))statusBar()->showMessage(
    tr("%1 is available; continuing the manually enabled TX session.").arg(target),10000);
}
