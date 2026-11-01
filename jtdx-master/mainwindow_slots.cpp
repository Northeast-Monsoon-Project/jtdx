#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScopedValueRollback>
#include <cmath>

extern qint32 g_iptt;

bool MainWindow::managedCandidateSlotAllowed(ManagedCandidate const& candidate) const
{
  if(!std::isfinite(m_TRperiod) || m_TRperiod<=0)return false;
  bool handoff=m_managed.current().isEmpty() && !m_signoff.active() && !m_transmitting
    && g_iptt==0 && !m_tx_when_ready && !m_managedCallOnAir && !m_managedCqOnAir;
  return m_managedSlotPolicy.canSelect(m_txFirst,
    ManagedSlotPolicy::replyTxFirst(candidate.utcSeconds,m_TRperiod),
    m_config.managed_settings().profile(m_managed.mode()).freeTxSlot,handoff,m_managedClock.elapsed());
}

bool MainWindow::applyManagedCandidateSlot(ManagedCandidate const& candidate)
{
  if(!managedCandidateSlotAllowed(candidate))return false;
  bool first=ManagedSlotPolicy::replyTxFirst(candidate.utcSeconds,m_TRperiod);
  if(first!=m_txFirst) {
    assignTxSlot(first);
    m_managedSlotPolicy.changed(m_managedClock.elapsed());
  }
  return true;
}

void MainWindow::assignTxSlot(bool first,bool manual)
{
  if(first!=m_txFirst && manual)m_managedSlotPolicy.changed(m_managedClock.elapsed());
  m_txFirst=first;ui->TxMinuteButton->setChecked(first);setMinButton();
}

bool MainWindow::handleTxSlotEvent(QObject* object,QEvent* event)
{
  if(object!=ui->TxMinuteButton || !ui->TxMinuteButton->isEnabled())return false;
  auto button=ui->TxMinuteButton;
  bool manual=event->type()==QEvent::Shortcut;
  if(event->type()==QEvent::MouseButtonRelease) {
    auto mouse=static_cast<QMouseEvent*>(event);
    manual=mouse->button()==Qt::LeftButton && button->isDown() && button->rect().contains(mouse->pos());
  } else if(event->type()==QEvent::KeyRelease) {
    auto key=static_cast<QKeyEvent*>(event);
    manual=!key->isAutoRepeat() && key->key()==Qt::Key_Space && button->isDown();
  } else if(event->type()==QEvent::KeyPress) {
    auto key=static_cast<QKeyEvent*>(event);
    manual=!key->isAutoRepeat() && (key->key()==Qt::Key_Return || key->key()==Qt::Key_Enter);
  }
  if(!manual)return false;
  QScopedValueRollback<bool> gesture(m_manualTxSlotChange,true);
  button->setDown(false);button->click();
  return true;
}
