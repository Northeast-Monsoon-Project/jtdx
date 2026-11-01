// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "displaytext.h"
#include <QTextBlock>
extern qint32 g_iptt;

void MainWindow::setupBlacklist() {
  m_blacklist=new Blacklist(this);
  connect(m_blacklist,&Blacklist::changed,this,&MainWindow::applyBlacklist);
  connect(m_blacklist,&Blacklist::loadError,this,[this](QString const& error){statusBar()->showMessage(error,20000);});
  loadBlacklist();
}

void MainWindow::loadBlacklist() {
  if(!m_blacklist)return;
  if(m_config.my_callsign().isEmpty()) {
    QSignalBlocker block(m_blacklist);m_blacklist->load({},m_config.log_directory());return;
  }
  m_blacklist->load(m_config.my_callsign(),m_config.log_directory());
}

void MainWindow::showBlacklistManager() {
  if(!m_blacklist || m_config.my_callsign().isEmpty())return;
  m_blacklist->reload();
  BlacklistDialog dialog(m_blacklist,this);
  dialog.setWindowTitle(dialog.windowTitle()+QStringLiteral(" — ")+m_config.my_callsign());
  dialog.exec();
}

bool MainWindow::permanentlyBlocked(QString const& call) const {
  return m_blacklist && (!m_blacklist->ready() || m_blacklist->matches(call));
}

bool MainWindow::messageBlacklisted(QString const& text) const {
  // A fresh installation can receive before an operator is configured.
  // Once an operator is selected, a damaged blacklist remains fail-closed.
  if(!m_blacklist || m_config.my_callsign().isEmpty())return false;
  if(!m_blacklist->ready())return true;
  if(m_blacklist->entries().isEmpty())return false;
  static QRegularExpression tokens(QStringLiteral("[A-Z0-9]+(?:/[A-Z0-9]+)*"));
  auto matches=tokens.globalMatch(text.toUpper());
  while(matches.hasNext())if(m_blacklist->matches(matches.next().captured()))return true;
  return false;
}

QString MainWindow::selectedTransmitText() const {
  if(m_mode=="JTTY")return m_jttyText?m_jttyText->text():QString();
  if(m_mode.startsWith("WSPR"))return {};
  switch(m_ntx) {
  case 1:return ui->tx1->text();
  case 2:return ui->tx2->text();
  case 3:return ui->tx3->text();
  case 4:return ui->tx4->text();
  case 5:return ui->tx5->currentText().remove(" ^");
  case 6:return ui->tx6->text();
  case 7:return ui->genMsg->text();
  case 8:return ui->freeTextMsg->currentText().remove(" ^");
  default:return {};
  }
}

void MainWindow::stopBlacklistedTransmit() {
  on_stopTxButton_clicked();
  m_currentMessage.clear();m_curMsgTx.clear();m_jttyPendingText.clear();
  // Clear only an offending selected message, preserving unrelated templates.
  if(messageBlacklisted(selectedTransmitText())) {
    if(m_mode=="JTTY")m_jttyText->clear();
    else switch(m_ntx) {
    case 1:ui->tx1->clear();break;
    case 2:ui->tx2->clear();break;
    case 3:ui->tx3->clear();break;
    case 4:ui->tx4->clear();break;
    case 5:ui->tx5->clearEditText();break;
    case 6:ui->tx6->clear();break;
    case 7:ui->genMsg->clear();break;
    case 8:ui->freeTextMsg->clearEditText();break;
    }
  }
  statusBar()->showMessage(tr("This station is blocked, or the permanent blacklist is unavailable."),15000);
}

void MainWindow::applyBlacklist() {
  // Remove earlier decoded lines as well as filtering future decoder output.
  // Work backwards to retain unaffected lines' formatting and selection order.
  for(auto browser:{ui->decodedTextBrowser,ui->decodedTextBrowser2}) {
    auto document=browser->document();
    for(auto block=document->lastBlock();block.isValid();) {
      auto previous=block.previous();
      if(messageBlacklisted(block.text())) {
        int first=block.position(),last=block.next().isValid()?block.next().position():document->characterCount()-1;
        if(!block.next().isValid() && first>0)--first;
        QTextCursor cursor(document);cursor.setPosition(first);cursor.setPosition(last,QTextCursor::KeepAnchor);cursor.removeSelectedText();
      }
      block=previous;
    }
  }
  auto purge=[this](QJsonArray& items) {
    QJsonArray filtered;
    for(auto const& item:items) {
      auto object=item.toObject();
      auto content=object.value("raw").toString()+' '+object.value("message").toString()+' '
        +object.value("call").toString()+' '+object.value("text").toString();
      if(!messageBlacklisted(content))filtered.append(item);
    }
    items=filtered;
  };
  purge(m_mcpDecodes);purge(m_mcpEvents);
  for(auto history:{&m_qsoHistory,&m_qsoHistory2})
    for(auto const& call:history->calls())if(permanentlyBlocked(call))history->discardDecodedCall(call);
  if(m_jttyLive && messageBlacklisted(m_jttyLive->text()))m_jttyLive->clear();
  const bool blockedTarget=permanentlyBlocked(m_hisCall);
  const bool active=m_transmitting || g_iptt!=0 || m_tx_when_ready;
  // JTTY's editor may already contain the next message. Inspect the snapshot
  // being encoded/on air, not an unrelated edit made while it is sending.
  const bool blockedPending=m_mode=="JTTY"
    ? m_jttyPreparing && messageBlacklisted(m_jttyPendingText)
    : (m_enableTx || active) && messageBlacklisted(selectedTransmitText());
  if(blockedTarget || blockedPending || (active && messageBlacklisted(m_currentMessage))) {
    stopBlacklistedTransmit();
    if(blockedTarget){clearDXfields(QString());genStdMsgs(QString());}
  }
  if(messageBlacklisted(last_tx_label->text()))last_tx_label->clear();
  if(messageBlacklisted(lastlogged_label->text()))lastlogged_label->clear();
}
