#include "mainwindow.h"
#include "ui_mainwindow.h"

extern qint32 g_iptt;

bool MainWindow::ordinaryRetryDue()
{
  if(m_managed.active() || m_houndMode || m_tune || m_diskData || m_manualDecode
     || !m_enableTx || !m_manualTxAuthorized || m_transmitting || g_iptt || m_tx_when_ready
     || m_hisCall.isEmpty())return false;
  QString grid;
  auto state=m_qsoHistory.status(m_hisCall,grid);
  int count=m_qsoHistory.sent_count(m_hisCall),limit=0;
  if(m_signoff.matches(m_hisCall,generalContext())) {
    if(!m_signoff.hadReceiveOpportunity())return false;
    count=m_signoff.transmissions();
    state=m_signoff.message()==SignoffState::RR73?QsoHistory::SRR73:QsoHistory::S73;
  }
  if(state==QsoHistory::RFIN || state==QsoHistory::RCQ || state==QsoHistory::SCALL || (state==QsoHistory::SREPORT && m_skipTx1)) {
    if(m_config.answerCQCount())limit=m_config.nAnswerCQCounter();
  } else if(state==QsoHistory::RCALL || state==QsoHistory::SREPORT) {
    if(m_config.answerInCallCount())limit=m_config.nAnswerInCallCounter();
  } else if(state==QsoHistory::RREPORT || state==QsoHistory::SRREPORT) {
    if(m_config.sentRReportCount())limit=m_config.nSentRReportCounter();
  } else if(state==QsoHistory::RRREPORT || state==QsoHistory::RRR || state==QsoHistory::SRR
            || state==QsoHistory::RRR73 || state==QsoHistory::SRR73
            || state==QsoHistory::R73 || state==QsoHistory::S73) {
    if(m_config.sentRR7373Count())limit=m_config.nSentRR7373Counter();
  }
  return limit>0 && count>=limit;
}

void MainWindow::seedOrdinarySelection(DecodedText& decoded,bool changed)
{
  if(m_managed.active() || m_houndMode || decoded.isTX() || decoded.isWrong() || m_hisCall.isEmpty())return;
  QString previousGrid;auto previous=m_qsoHistory.status(m_hisCall,previousGrid);
  bool fresh=changed || !m_enableTx || (previous==QsoHistory::FIN && !m_signoff.active())
    || (!m_ordinarySelectionContext.isEmpty() && m_ordinarySelectionContext!=generalContext());
  m_ordinarySelectionContext=generalContext();
  if(!fresh)return; // Reclicking an in-progress /P call must not reset its budget.
  m_signoff.reset();
  seedSelectedQsoHistory(decoded,m_hisCall);
}

void MainWindow::seedSelectedQsoHistory(DecodedText& decoded,QString const& selectedCall)
{
  m_qsoHistory.remove(selectedCall);
  QString call,grid,direction,cqGrid;decoded.deCallAndGrid(call,grid);decoded.CQersCall(cqGrid,direction);
  auto words=decoded.message().split(' ',SkipEmptyParts);
  bool incoming=Radio::base_callsign(decoded.call().remove('<').remove('>'))==m_baseCall || words.value(0)=="DE";
  QString country;m_logBook.getDXCC(selectedCall,country);auto parts=country.split(',');
  auto mode=m_mode=="JT9+JT65"?(decoded.isJT65()?QString("JT65"):QString("JT9")):m_mode;
  m_qsoHistory.message(selectedCall,incoming?QsoHistory::RCALL:QsoHistory::RCQ,0,grid,direction,
    parts.value(0),parts.value(1),decoded.timeInSeconds(),decoded.report(),decoded.frequencyOffset(),mode);
  if(incoming) {
    QString report,type;decoded.report(m_baseCall,Radio::base_callsign(selectedCall),report,type);
    if(!report.isEmpty()) {
      m_qsoHistory.message(selectedCall,QsoHistory::RREPORT,0,report,direction,parts.value(0),parts.value(1),
        decoded.timeInSeconds(),decoded.report(),decoded.frequencyOffset(),mode);
      if(type=="R")m_qsoHistory.reset_count(selectedCall,QsoHistory::RRREPORT);
    } else if(words.value(words.size()-1)=="RRR")m_qsoHistory.reset_count(selectedCall,QsoHistory::RRR);
    else if(words.value(words.size()-1)=="RR73")m_qsoHistory.reset_count(selectedCall,QsoHistory::RRR73);
    else if(words.value(words.size()-1)=="73")m_qsoHistory.reset_count(selectedCall,QsoHistory::R73);
  }
}
