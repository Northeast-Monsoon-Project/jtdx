// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "displaytext.h"
#include "MessageClient.hpp"
#include <QScopedValueRollback>
#include <cmath>

void MainWindow::clearBandActivity()
{
  ++m_udpReplayGeneration;
  m_udpDecodeHistory.clear();
  ui->decodedTextBrowser->clear();
  if(m_messageClient)m_messageClient->clear_decodes();
}

void MainWindow::replyToUDP(QTime time,qint32 snr,float dt,quint32 frequency,
                           QString const& mode,QString const& text,bool,quint8)
{
  if(!m_config.accept_udp_requests() || !time.isValid() || !std::isfinite(dt) || managedSelectionBlocked())return;
  auto message=UdpDecodeRecord::identity(text);
  bool cq=message.contains(QRegularExpression(QStringLiteral("^(CQ |CQDX |QRZ)")));
  bool end=message.endsWith(" 73") || message.endsWith(" RR73");
  if(!((m_acceptUDP==1 && cq) || (m_acceptUDP==2 && (cq || end)) || m_acceptUDP==3))return;
  m_udpDecodeHistory.setContext(generalContext());
  QString row;
  auto const& history=m_udpDecodeHistory.records();
  for(auto it=history.crbegin();it!=history.crend();++it)
    if(it->matches(time,snr,dt,frequency,mode,message)){row=it->row;break;}
  if(row.isEmpty() || messageBlacklisted(row))return;
  if(!m_hisCall.isEmpty() && message.contains(" "+m_hisCall+" "))
    m_messageClient->status_update(m_freqNominal,m_mode,m_hisCall,QString::number(ui->rptSpinBox->value()),
      m_modeTx,ui->enableTxButton->isChecked(),m_transmitting,m_decoderBusy,
      ui->RxFreqSpinBox->value(),ui->TxFreqSpinBox->value(),m_config.my_callsign(),m_config.my_grid(),
      m_hisGrid,m_txwatchdog,QString(),false,m_txFirst,true);
  if(m_config.udpWindowToFront()){show();raise();activateWindow();}
  if(m_config.udpWindowRestore() && isMinimized()){showNormal();raise();}
  QScopedValueRollback<bool> selected(m_decodedText2,true);
  // A remote reply is never an operator double-click and cannot grant TX.
  if(processMessage(row,row.size()-1,false,false)) {
    resetOrdinaryBusy();quickCallFromSelection(row,false,false,true);txwatchdog(false);
  }
  if(m_windowPopup)QApplication::alert(this);
}

void MainWindow::sendUdpDecode(bool isNew,UdpDecodeRecord const& record)
{
  if(messageBlacklisted(record.row))return;
  m_messageClient->decode(isNew,record.time,record.snr,record.dt,record.frequency,
    record.mode,record.message,record.lowConfidence,record.offAir);
}

void MainWindow::postDecode(bool isNew,QString const& row)
{
  if(messageBlacklisted(row))return;
  UdpDecodeRecord record;
  if(!UdpDecodeRecord::parse(row,upstreamMode(),m_diskData,record))return;
  if(isNew) {
    m_udpDecodeHistory.setContext(generalContext());
    m_udpDecodeHistory.append(record);
  }
  sendUdpDecode(isNew,record);
}

void MainWindow::replayDecodes()
{
  m_udpDecodeHistory.setContext(generalContext());
  auto rows=std::make_shared<QList<UdpReplayRow>>();
  for(auto const& record:m_udpDecodeHistory.records())rows->append({record.row,record.offAir,false});
  for(auto const& record:m_udpDecodeHistory.wsprRecords())rows->append({record.fields.join(' '),record.offAir,true});
  auto generation=++m_udpReplayGeneration;
  replayDecodeBatch(rows,0,generation,generalContext());
}

void MainWindow::replayDecodeBatch(std::shared_ptr<QList<UdpReplayRow>> rows,int offset,quint64 generation,QString const& context)
{
  if(generation!=m_udpReplayGeneration || context!=generalContext())return;
  int end=qMin(offset+32,rows->size());
  for(int i=offset;i<end;++i) {
    auto const& item=rows->at(i);
    if(item.wspr) {
      UdpWsprRecord record;
      if(UdpWsprRecord::parse(item.row.split(' ',Qt::SkipEmptyParts),item.offAir,record))sendWsprDecode(false,record);
    }
    else {
      UdpDecodeRecord record;
      if(UdpDecodeRecord::parse(item.row,upstreamMode(),item.offAir,record))sendUdpDecode(false,record);
    }
  }
  if(end<rows->size())QTimer::singleShot(10,this,[this,rows,end,generation,context]{replayDecodeBatch(rows,end,generation,context);});
  else statusChanged();
}

void MainWindow::sendWsprDecode(bool isNew,UdpWsprRecord const& record)
{
  if(messageBlacklisted(record.fields.join(' ')))return;
  m_messageClient->WSPR_decode(isNew,record.time,record.snr,record.dt,Radio::frequency(record.mhz,6),
    record.drift,record.fields[5],record.fields[6],record.power,record.offAir);
}

void MainWindow::postWSPRDecode(bool isNew,QStringList parts)
{
  if(messageBlacklisted(parts.join(' ')))return;
  UdpWsprRecord record;
  if(!UdpWsprRecord::parse(parts,m_diskData,record))return;
  if(isNew){m_udpDecodeHistory.setContext(generalContext());m_udpDecodeHistory.append(record);}
  sendWsprDecode(isNew,record);
}
