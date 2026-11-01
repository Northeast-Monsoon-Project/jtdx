#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "MessageClient.hpp"
#include "Bands.hpp"
#include "GeneralOperatingState.hpp"
#include "LiveFrequencyValidator.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QStatusBar>

extern qint32 g_iptt;

namespace {
QString normalizedGeneralCall(QString call) {return call.remove('<').remove('>').trimmed().toUpper();}
bool sameGeneralCall(QString const& first,QString const& second) {
  auto a=normalizedGeneralCall(first),b=normalizedGeneralCall(second);
  return !a.isEmpty() && a==b;
}
}

QString MainWindow::generalContext() const
{
  return m_config.my_callsign()+"|"+QString::number(m_freqNominal)+"|"+m_mode;
}

void MainWindow::applyGeneralSettings()
{
  if(!m_generalClock.isValid())m_generalClock.start();
  if(auto validator=findChild<LiveFrequencyValidator*>())validator->setKHzWithoutK(m_config.kHz_without_k());
  ui->actionSingleShot->setChecked(m_config.disable_TX_on_73());
  on_actionSingleShot_toggled(m_config.disable_TX_on_73());
  // The legacy second 73 checkbox must not contradict Single QSO.
  {QSignalBlocker blocker(ui->actionDisableTx73);ui->actionDisableTx73->setChecked(m_config.disable_TX_on_73());}
  m_disable_TX_on_73=m_config.disable_TX_on_73();
  ui->actionEraseWindowsAtBandChange->setChecked(m_config.erase_BandActivity());
  m_autoEraseBC=m_config.erase_BandActivity();
  for(auto action:findChildren<QAction*>()) {
    const char* property="jtdxfGeneralOriginalShortcuts";
    if(!action->property(property).isValid()) {
      QVariantList original;for(auto const& key:action->shortcuts())original<<QVariant::fromValue(key);
      action->setProperty(property,original);
    }
    QList<QKeySequence> keys;
    for(auto const& value:action->property(property).toList()) {
      auto key=value.value<QKeySequence>();
      if(!m_config.alternate_bindings() || key.count()!=1 || key[0]<Qt::Key_F1 || key[0]>Qt::Key_F6)keys<<key;
    }
    action->setShortcuts(keys);
  }
  ui->EraseButton->setToolTip(m_config.alternate_erase_button()
    ? tr("Left-click to erase the left window; right-click to erase the right window; double-click to erase both.")
    : tr("Click to erase the right window; double-click to erase both."));
  ui->enableTxButton->setToolTip(m_config.Wait_features_enabled()
    ? tr("Enable or stop TX. Right-click for Wait & Reply, Wait & Call or Wait & Pounce. Waiting requires a manually enabled TX session.")
    : tr("Enable or stop TX."));
  if(!m_config.Wait_features_enabled())resetGeneralWait();
  ui->decodedTextBrowser->setDxHighlightTarget(m_hisCall,m_hisGrid);
  ui->decodedTextBrowser2->setDxHighlightTarget(m_hisCall,m_hisGrid);
  updateGeneralButtonColors();
  updateGeneralWindows();
}

void MainWindow::updateGeneralWindows()
{
  if(m_emeTools)m_emeTools->setVisible(upstreamMode() || m_config.enable_VHF_features());
  if(!m_astro)return;
  switch(GeneralOperatingState::astroWindowAction(m_mode,m_config.auto_astro(),m_config.decode_at_52s())) {
  case GeneralOperatingState::ShowAstroWindow:
    if(!m_astro->isVisible()){m_astro->show();m_lastAstroRequest=0;}
    break;
  case GeneralOperatingState::HideAstroWindow:m_astro->hide();break;
  case GeneralOperatingState::KeepAstroWindow:break;
  }
}

bool MainWindow::continueGeneralTx()
{
  if(!m_manualTxAuthorized || !m_enableTx || m_logLoading || m_logImportActive || m_logRefreshPending || !m_logReady || m_tune
     || m_diskData || m_manualDecode || !ui->enableTxButton->isEnabled() || m_managed.active())return false;
  if(permanentlyBlocked(m_hisCall) || messageBlacklisted(selectedTransmitText()))return false;
  // This helper only continues an existing, manually authorized session.
  enableTx_mode(true);
  return m_enableTx;
}

bool MainWindow::handleGeneralKeyEvent(QKeyEvent* event)
{
  if(!m_config.alternate_bindings() || event->key()<Qt::Key_F1 || event->key()>Qt::Key_F6
     || event->modifiers()!=Qt::NoModifier || QApplication::activeModalWidget())return false;
  if(event->isAutoRepeat())return true;
  if(event->key()==Qt::Key_F6) {
    // Existing first/mid/end strategies retain JTDX's selection semantics.
    int next=(m_callMode+1)%4;
    (next==0?ui->actionCallNone:next==1?ui->actionCallFirst:next==2?ui->actionCallMid:ui->actionCallEnd)->setChecked(true);
    return true;
  }
  if(m_logLoading || m_logImportActive || m_logRefreshPending || !m_logReady || m_managed.active()
     || m_tune || m_diskData || m_manualDecode)return true;
  if(event->key()!=Qt::Key_F1 && (m_hisCall.size()<3 || permanentlyBlocked(m_hisCall)))return true;
  resetGeneralWait();
  switch(event->key()) {
  case Qt::Key_F1:on_txb6_clicked();break;
  case Qt::Key_F2:on_txb2_clicked();break;
  case Qt::Key_F3:on_txb3_clicked();break;
  case Qt::Key_F4:on_txb4_clicked();break;
  case Qt::Key_F5:on_txb5_clicked();break;
  default:break;
  }
  continueGeneralTx();
  return true;
}

bool MainWindow::managedSelectionBlocked() const
{
  return m_managed.active() && (m_transmitting || g_iptt!=0 || m_tx_when_ready
    || m_managedCallOnAir || m_managedCqOnAir || m_tune || ptt0Timer.isActive()
    || ptt1Timer.isActive() || m_rigState.ptt());
}

void MainWindow::quickCallFromSelection(QString const& row,bool alt,bool ctrl,bool remoteSelection)
{
  bool startingSession=!m_enableTx;
  if(remoteSelection && (startingSession || !m_manualTxAuthorized))return;
  // Quick Call controls opening a new TX session. An already authorized
  // managed session must still accept a manual target when it is disabled.
  if((!m_config.quick_call() && (startingSession || !m_managed.active()))
     || alt || ctrl || m_diskData || m_manualDecode || m_tune
     || m_logLoading || m_logImportActive || m_logRefreshPending || !m_logReady
     || !ui->enableTxButton->isEnabled() || QApplication::activeModalWidget())return;
  if(managedSelectionBlocked())return;
  DecodedText decoded(row,this,upstreamMode());
  QString call,grid;decoded.deCallAndGrid(call,grid);
  call.remove('<').remove('>');
  if(decoded.isTX() || decoded.isWrong() || !Radio::is_callsign(call) || !sameGeneralCall(call,m_hisCall)
     || Radio::base_callsign(call)==Radio::base_callsign(m_config.my_callsign()) || permanentlyBlocked(call)
     || messageBlacklisted(decoded.message()) || messageBlacklisted(selectedTransmitText()))return;
  bool gesture=ui->decodedTextBrowser->callSelectionInProgress() || ui->decodedTextBrowser2->callSelectionInProgress();
  if(startingSession) {
    if(!gesture)return;
    m_manualTxAuthorized=true;
    ui->enableTxButton->click(); // Reuse normal station/log/blacklist checks and watchdog startup.
    if(!m_enableTx){m_manualTxAuthorized=false;return;}
    // Starting a managed session clears DX in the normal Enable TX handler.
    // Restore this explicit selection before letting the normal sequence run.
    if(m_managed.active() && !processMessage(row,row.size()-1,false,false)) {on_stopTxButton_clicked();return;}
  }
  resetGeneralWait();
  if(m_managed.active()) {
    if((!gesture && !remoteSelection) || !m_manualTxAuthorized || !sameGeneralCall(call,m_hisCall))return;
    auto context=m_config.my_callsign()+"|"+QString::number(m_freqNominal)+"|"+m_mode;
    QString previousGrid;auto previousStatus=m_qsoHistory.status(call,previousGrid);
    bool finished=previousStatus==QsoHistory::FIN && !m_signoff.active();
    if(startingSession || !sameGeneralCall(m_managed.current(),call) || context!=m_managedContext || finished) {
      m_signoff.reset();
      m_managed.begin(m_hisCall);
      // A deliberate new call must not inherit an older FIN/S73 or report.
      // Seed from the selected row even if it is already in the right pane
      // and processMessage therefore did not display/process it again.
      seedSelectedQsoHistory(decoded,call);
      observeManagedDecode(decoded);
    }
    m_managedContext=context;m_managedTxReady=true;
  } else continueGeneralTx();
}

bool MainWindow::handleGeneralMouseEvent(QObject* object,QEvent* event)
{
  if(event->type()==QEvent::KeyPress && handleGeneralKeyEvent(static_cast<QKeyEvent*>(event)))return true;
  if(event->type()!=QEvent::MouseButtonPress)return false;
  auto mouse=static_cast<QMouseEvent*>(event);
  if(mouse->button()!=Qt::RightButton)return false;
  if(object==ui->EraseButton && m_config.alternate_erase_button()) {eraseGeneralMessages(true);return true;}
  if(object!=ui->enableTxButton || !m_config.Wait_features_enabled())return false;
  QMenu menu(this);QActionGroup group(&menu);group.setExclusive(true);
  auto add=[&](QString const& label,GeneralOperatingState::WaitMode mode){
    auto action=menu.addAction(label);action->setCheckable(true);action->setActionGroup(&group);
    action->setData(int(mode));action->setChecked(m_generalState.mode()==mode);
    action->setEnabled(mode==GeneralOperatingState::Off || (!m_managed.active() && !m_tune && !m_transmitting
      && (mode==GeneralOperatingState::Pounce || !m_hisCall.isEmpty())));
  };
  add(tr("Off"),GeneralOperatingState::Off);add(tr("Wait & Reply"),GeneralOperatingState::Reply);
  add(tr("Wait & Call"),GeneralOperatingState::Call);add(tr("Wait & Pounce"),GeneralOperatingState::Pounce);
  auto action=menu.exec(mouse->globalPos());if(!action)return true;
  auto mode=GeneralOperatingState::WaitMode(action->data().toInt());
  if(mode==GeneralOperatingState::Off)resetGeneralWait();
  else {
    m_generalState.arm(mode,m_hisCall,generalContext());
    statusBar()->showMessage(tr("Waiting is selected. Click Enable TX to authorize transmission; Stop cancels waiting."),15000);
  }
  return true;
}

void MainWindow::resetGeneralWait()
{
  m_generalState.reset();
}

bool MainWindow::generalTxAllowed() const
{
  return m_generalState.contextMatches(generalContext()) && m_generalState.allowsTx()
    // Do not interrupt a CQ already keyed when late decodes arrived. The
    // pending choice blocks only the next PTT start, not its current audio.
    && (!m_ordinaryAutoPending || g_iptt || m_transmitting)
    && (!ordinaryBusyWaiting() || g_iptt || m_transmitting)
    && (!m_signoff.matches(m_hisCall,generalContext()) || m_signoff.allowsTransmit());
}

void MainWindow::processGeneralDecode(QString const& row)
{
  observeSignoff(row);
  if(!m_generalClock.isValid())m_generalClock.start();
  if(m_diskData || m_manualDecode || messageBlacklisted(row))return;
  DecodedText decoded(row,this,upstreamMode());if(decoded.isWrong() || decoded.isTX())return;
  QString sender,grid;decoded.deCallAndGrid(sender,grid);
  if(!sender.isEmpty() && Radio::base_callsign(normalizedGeneralCall(sender))==Radio::base_callsign(m_config.my_callsign()))return;
  auto text=decoded.message();text.remove('<').remove('>');auto words=text.simplified().split(' ');
  bool toUs=!words.isEmpty() && (sameGeneralCall(words.first(),m_config.my_callsign()) || sameGeneralCall(words.first(),m_baseCall));
  bool cq=!words.isEmpty() && words.first()=="CQ";
  bool end=!words.isEmpty() && (words.last()=="73" || words.last()=="RR73" || words.last()=="RRR");
  bool safe=m_enableTx && m_manualTxAuthorized && m_logReady && !m_logLoading && !m_logImportActive
    && !m_logRefreshPending && !m_tune && !m_transmitting && !m_watchdogFinishing && !m_managed.active()
    && !permanentlyBlocked(sender) && watchdogTxAllowed();
  if(m_config.Wait_features_enabled() && m_generalState.consider(sender,toUs,cq,end,safe,generalContext(),m_generalClock.elapsed(),m_TRperiod)) {
    processMessage(row,row.size()-1,false,false);
    statusBar()->showMessage(tr("Waiting matched %1; continuing the manually enabled TX session.").arg(sender),10000);
  }

}

void MainWindow::applyGeneralPostQso(QString const& completedCall)
{
  if(sameGeneralCall(completedCall,m_generalState.target()))resetGeneralWait();
  m_generalPostQsoCall=completedCall;
  m_generalPostQsoContext=generalContext();
  generalTick();
}

void MainWindow::generalTick()
{
  signoffPttReleased();
  finishSignoff();
  flushSignoffLogRefresh();
  ordinaryBusyTick();
  if(!m_generalClock.isValid())m_generalClock.start();
  if(!m_generalState.contextMatches(generalContext()) || !m_config.Wait_features_enabled() || m_managed.active())resetGeneralWait();
  if(m_generalState.expired(m_generalClock.elapsed())) {
    resetGeneralWait();on_stopTxButton_clicked();
    statusBar()->showMessage(tr("Waiting timed out; TX stopped."),10000);
  }
  if(m_config.force_call_1st() && !m_managed.active() && m_enableTx && m_manualTxAuthorized
     && selectedTransmitText().startsWith("CQ ") && m_callMode==0) {
    ui->actionCallFirst->setChecked(true);
    ui->AutoSeqButton->setChecked(true);on_AutoSeqButton_clicked(true);
  }
  if(!m_generalPostQsoCall.isEmpty()) {
    if(m_generalPostQsoContext!=generalContext()
       || (!m_hisCall.isEmpty() && !sameGeneralCall(m_generalPostQsoCall,m_hisCall)))m_generalPostQsoCall.clear();
    else if(!m_signoff.active() && !m_transmitting && !m_tx_when_ready && !g_iptt && !m_watchdogFinishing) {
      // m_status is only refreshed by AutoSeq and can still describe the
      // received report after the final message has actually been sent.
      QString grid;
      auto state=m_qsoHistory.status(m_generalPostQsoCall,grid);
      bool finished=state==QsoHistory::FIN;
      bool repeat=upstreamMode() && !m_managed.active() && m_enableTx
        && m_upstreamState.waitingFor73(m_config.repeat_Tx(),m_mode,m_upstream->context(),m_hisCall);
      if(!repeat && (!m_enableTx || m_hisCall.isEmpty() || finished)) {
        auto completed=m_generalPostQsoCall;
        m_generalPostQsoCall.clear();
        if(m_config.clear_DXcall() && !m_hisCall.isEmpty()) {
          // Complete the scheduler before removing its selected partner. This
          // also prevents a stale invitation from immediately selecting it again.
          if(m_managed.active() && sameGeneralCall(m_managed.current(),completed)) {
            m_managed.complete(m_managedClock.elapsed());m_managedTxReady=false;
          }
          m_qsoHistory.complete(completed);
          auto retainedGrid=ui->dxGridEntry->text();
          if(m_enableTx && m_singleshot && !m_managed.active())autoStopTx(" logged single QSO completed ");
          clearDX(" cleared after logged QSO completed");
          if(!m_config.clear_DXgrid())ui->dxGridEntry->setText(retainedGrid);
        } else if(m_config.clear_DXgrid())ui->dxGridEntry->clear();
        if(m_config.set_RXtoTX())ui->RxFreqSpinBox->setValue(ui->TxFreqSpinBox->value());
      }
    }
  }
  if(m_ordinaryAutoPending)process_Auto();
  ui->decodedTextBrowser->setDxHighlightTarget(m_hisCall,m_hisGrid);
  ui->decodedTextBrowser2->setDxHighlightTarget(m_hisCall,m_hisGrid);
  updateGeneralButtonColors();
}

void MainWindow::eraseGeneralMessages(bool rightButton)
{
  qint64 now=m_jtdxtime->currentMSecsSinceEpoch2();bool both=!rightButton && now-m_msErase>=0 && now-m_msErase<500;
  bool left=both || m_mode.startsWith("WSPR") || (m_config.alternate_erase_button() && !rightButton);
  bool right=both || rightButton || !m_config.alternate_erase_button();
  if(left) {clearBandActivity();}
  if(right) {ui->decodedTextBrowser2->clear();m_QSOText.clear();}
  if(!rightButton)m_msErase=now;
}

void MainWindow::updateGeneralButtonColors()
{
  updateTxButtonAppearance();
  // Only decorative controls: TX/Tune/Stop, the PTT lamp and alarm labels keep
  // their safety colors even when less button coloring is selected.
  QList<QAbstractButton*> buttons{ui->AutoSeqButton,ui->AnsB4Button,ui->filterButton,ui->HoundButton,ui->TxMinuteButton};
  QString neutral="QPushButton { border: 1px solid palette(mid); border-radius: 4px; padding: 2px; } QPushButton:checked { font-weight: bold; border: 2px solid palette(text); }";
  for(auto button:buttons) {
    const char* original="jtdxfGeneralOriginalStyle";
    if(m_config.button_coloring_disabled()) {
      if(button->styleSheet()!=neutral) {button->setProperty(original,button->styleSheet());button->setStyleSheet(neutral);}
    } else if(button->property(original).isValid()) {
      if(button->styleSheet()==neutral)button->setStyleSheet(button->property(original).toString());
      button->setProperty(original,QVariant());
    }
  }
}

void MainWindow::updateTxButtonAppearance()
{
  // Armed while receiving/preparing is red; an active send is green.
  // Keep this state indicator even when decorative button coloring is off.
  bool onAir=m_transmitting && !m_tx_when_ready && !ptt1Timer.isActive();
  auto color=!m_enableTx?"#dcdcdc":onAir?"#00ff00":"#ff3c3c";
  auto border=m_enableTx?"#000000":"#adadad";
  auto style=QString("QPushButton {color: %1;background: %2;border-style: solid;border-width: 1px;border-radius: 5px;border-color: %3;min-width: 63px;padding: 0px}")
    .arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark(color,m_useDarkStyle),Radio::convert_dark(border,m_useDarkStyle));
  if(ui->enableTxButton->styleSheet()!=style)ui->enableTxButton->setStyleSheet(style);
}
