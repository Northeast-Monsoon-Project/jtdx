#include "logqso.h"
#include "OperatorLog.hpp"
#include "LogForwarder.hpp"

#include <QTcpSocket>
#include <QString>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

#include "JTDXMessageBox.hpp"

#include "logbook/adif.h"
#include "Configuration.hpp"
#include "Bands.hpp"

#include "ui_logqso.h"
#include "moc_logqso.cpp"

LogQSO::LogQSO(QSettings * settings, Configuration const * config, JTDXDateTime * jtdxtime, QWidget *parent)
  : QDialog(parent)
  , ui(new Ui::LogQSO)
  , m_settings (settings)
  , m_config {config}
  , m_jtdxtime {jtdxtime}
{
  ui->setupUi(this);
  ui->start_date_time->setTimeSpec(Qt::UTC);
  ui->end_date_time->setTimeSpec(Qt::UTC);
  auto propagation=new QFormLayout;
  m_propagation=new QComboBox(this);m_propagation->setObjectName("log_propagation");
  m_propagation->addItem(tr("Unspecified"),"");m_propagation->addItem(tr("Satellite"),"SAT");
  m_propagation->addItem(tr("Earth–Moon–Earth"),"EME");m_propagation->addItem(tr("Meteor scatter"),"MS");m_propagation->addItem(tr("Tropospheric"),"TR");
  m_satellite=new QLineEdit(this);m_satellite->setObjectName("log_satellite");m_satellite->setMaxLength(32);
  m_satMode=new QLineEdit(this);m_satMode->setObjectName("log_satellite_mode");m_satMode->setMaxLength(16);m_satMode->setPlaceholderText("U/V, V/U, S/X…");
  m_receiveFrequency=new QDoubleSpinBox(this);m_receiveFrequency->setObjectName("log_rx_frequency");m_receiveFrequency->setDecimals(6);m_receiveFrequency->setRange(0,1000000);m_receiveFrequency->setSuffix(" MHz");m_receiveFrequency->setSpecialValueText(tr("Unspecified"));
  m_keepPropagation=new QCheckBox(tr("Remember propagation and satellite"),this);
  m_keepPropagation->setObjectName("log_remember_propagation");
  propagation->addRow(tr("Propagation"),m_propagation);propagation->addRow(tr("Satellite name"),m_satellite);propagation->addRow(tr("Satellite mode"),m_satMode);propagation->addRow(tr("Receive frequency"),m_receiveFrequency);propagation->addRow(m_keepPropagation);
  ui->verticalLayout_11->insertLayout(ui->verticalLayout_11->count()-1,propagation);
  connect(m_propagation,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{bool satellite=m_propagation->currentData()=="SAT";m_satellite->setEnabled(satellite);m_satMode->setEnabled(satellite);});
  ui->buttonBox->button(QDialogButtonBox::Ok)->setText(tr("&OK"));
  ui->buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("&Cancel"));
//  setWindowTitle("JTDX " + versnumber.simplified () + " - Log QSO");
  setWindowTitle(QCoreApplication::applicationName () + " v" + QCoreApplication::applicationVersion () + " - Log QSO");

  loadSettings ();
}

LogQSO::~LogQSO ()
{
}

void LogQSO::loadSettings ()
{
  m_settings->beginGroup ("LogQSO");
  restoreGeometry (m_settings->value ("geometry", saveGeometry ()).toByteArray ());
  ui->cbTxPower->setChecked (m_settings->value ("SaveTxPower", false).toBool ());
  ui->cbComments->setChecked (m_settings->value ("SaveComments", false).toBool ());
  ui->cbEqslComments->setChecked (m_settings->value ("SaveEQSLComments", false).toBool ());
  m_txPower = m_settings->value ("TxPower", "").toString ();
  m_comments = m_settings->value ("LogComments", "").toString();
  m_eqslcomments = m_settings->value ("LogEQSLComments", "").toString();
  m_keepPropagation->setChecked(m_settings->value("SavePropagation",false).toBool());
  if(m_keepPropagation->isChecked()) {
    m_propagation->setCurrentIndex(qMax(0,m_propagation->findData(m_settings->value("Propagation",""))));
    m_satellite->setText(m_settings->value("Satellite","").toString());m_satMode->setText(m_settings->value("SatelliteMode","").toString());m_receiveFrequency->setValue(m_settings->value("ReceiveFrequency",0).toDouble());
  }
  m_satellite->setEnabled(m_propagation->currentData()=="SAT");m_satMode->setEnabled(m_propagation->currentData()=="SAT");
  m_settings->endGroup ();
}

void LogQSO::storeSettings () const
{
  m_settings->beginGroup ("LogQSO");
  m_settings->setValue ("geometry", saveGeometry ());
  m_settings->setValue ("SaveTxPower", ui->cbTxPower->isChecked ());
  m_settings->setValue ("SaveComments", ui->cbComments->isChecked ());
  m_settings->setValue ("SaveEQSLComments", ui->cbEqslComments->isChecked ());
  m_settings->setValue ("TxPower", m_txPower);
  m_settings->setValue ("LogComments", m_comments);
  m_settings->setValue ("LogEQSLComments", m_eqslcomments);
  m_settings->setValue("SavePropagation",m_keepPropagation->isChecked());m_settings->setValue("Propagation",m_propagation->currentData());m_settings->setValue("Satellite",m_satellite->text());m_settings->setValue("SatelliteMode",m_satMode->text());m_settings->setValue("ReceiveFrequency",m_receiveFrequency->value());
  m_settings->endGroup ();
}

void LogQSO::initLogQSO(QString const& hisCall, QString const& hisGrid, QString mode,
                        QString const& rptSent, QString const& rptRcvd, QString const& distance,
                        QString const& name, QDateTime const& dateTimeOn, QDateTime const& dateTimeOff,
                        Radio::Frequency dialFreq, bool autologging,Radio::Frequency receiveFrequency)
{
  m_send_to_eqsl=m_config->send_to_eqsl();
  if(!m_keepPropagation->isChecked()) {m_propagation->setCurrentIndex(0);m_satellite->clear();m_satMode->clear();m_receiveFrequency->setValue(0);}
  // Remembering the satellite must not carry a previous QSO's RX frequency
  // into a same-frequency contact or one without a valid RX reading.
  m_receiveFrequency->setValue(receiveFrequency>0 && receiveFrequency!=dialFreq ? receiveFrequency/1.e6 : 0);
  ui->call->setText(hisCall);
  ui->grid->setText(hisGrid);
  ui->name->setText(name);
  ui->txPower->setText("");
  ui->comments->setText("");
  ui->eqslcomments->setText("");
  ui->lab11->setVisible(m_send_to_eqsl);
  ui->eqslcomments->setVisible(m_send_to_eqsl);
  ui->cbEqslComments->setVisible(m_send_to_eqsl);
  if (ui->cbTxPower->isChecked ()) ui->txPower->setText(m_txPower);
  if (ui->cbComments->isChecked ()) ui->comments->setText(m_comments);
  if (ui->cbEqslComments->isChecked ()) ui->eqslcomments->setText(m_eqslcomments);
  QString t="";
  if(m_config->report_in_comments()) {
    t=mode;
    if(rptSent!="") t+="  Sent: " + rptSent;
    if(rptRcvd!="") t+="  Rcvd: " + rptRcvd;
    ui->comments->setText(t);
  }
  if(m_config->distance_in_comments()) {
    if(t.isEmpty()) t="Distance: " + distance;
    else t+="  Distance: " + distance;
    ui->comments->setText(t);
  }
  if(m_config->log_as_RTTY() and mode.left(3)=="JT9") mode="RTTY";
  ui->mode->setText(mode);
  ui->sent->setText(rptSent);
  ui->rcvd->setText(rptRcvd);
  ui->start_date_time->setDateTime (dateTimeOn.toUTC());
  ui->end_date_time->setDateTime (dateTimeOff.toUTC());
  m_dialFreq=dialFreq;
  m_myCall=m_config->my_callsign();
  m_myGrid=m_config->my_grid();
  m_tcp_server_name=m_config->tcp_server_name();
  m_tcp_server_port=m_config->tcp_server_port();
  m_enable_tcp_connection=m_config->enable_tcp_connection();
  m_debug=m_config->write_decoded_debug();
  ui->band->setText(m_config->bands ()->find (dialFreq));

  if(!autologging) {
	 show ();
  }
  else {
	 accept();
  }
}

void LogQSO::updateSignoffSnapshot(QDateTime const& end,QString const& sent,QString const& received)
{
  ui->end_date_time->setDateTime(end.toUTC());
  if(!sent.isEmpty())ui->sent->setText(sent);
  if(!received.isEmpty())ui->rcvd->setText(received);
}

void LogQSO::accept()
{
  QString hisCall,hisGrid,mode,rptSent,rptRcvd,time,band;
  QString comments,eqslcomments,name;
  hisCall=ui->call->text();
  hisGrid=ui->grid->text();
  mode=ui->mode->text();
  rptSent=ui->sent->text();
  rptRcvd=ui->rcvd->text();
  m_dateTimeOn = ui->start_date_time->dateTime ().toUTC();
  m_dateTimeOff = ui->end_date_time->dateTime ().toUTC();
  band=ui->band->text();
  name=ui->name->text();
  m_txPower=ui->txPower->text();
  comments=ui->comments->text();
  m_comments=comments;
  eqslcomments=ui->eqslcomments->text();
  m_eqslcomments=eqslcomments;
  QString strDialFreq(QString::number(m_dialFreq / 1.e6,'f',6));
  QMap<QString,QString> propagation;
  if(!m_propagation->currentData().toString().isEmpty())propagation["PROP_MODE"]=m_propagation->currentData().toString();
  if(propagation.value("PROP_MODE")=="SAT") {
    propagation["SAT_NAME"]=m_satellite->text().trimmed().toUpper();propagation["SAT_MODE"]=m_satMode->text().trimmed().toUpper();
  }
  if(m_receiveFrequency->value()>0) {
    propagation["FREQ_RX"]=QString::number(m_receiveFrequency->value(),'f',6);
    propagation["BAND_RX"]=m_config->bands()->find(qRound64(m_receiveFrequency->value()*1e6));
  }

  //Log this QSO to ADIF file "wsjtx_log.adi"
  ADIF adifile;
  auto adifilePath = OperatorLog::quarterFile(m_myCall,m_dateTimeOn.toUTC().date());
  adifile.init(adifilePath);
  if (!adifile.addQSOToFile(hisCall,hisGrid,mode,rptSent,rptRcvd,m_dateTimeOn,m_dateTimeOff,band,comments,name,strDialFreq,m_myCall,m_myGrid,m_txPower,m_send_to_eqsl,propagation))
  {
      JTDXMessageBox::information_message(0,"","Cannot open file \"" + adifilePath + "\".");
      return; // Never report a QSO as saved when the write failed.
   }

//Clean up and finish logging
    QString myadif;
    QByteArray myadif2;
    myadif="<BAND:" + QString::number(band.length()) + ">" + band;
    myadif+=" <STATION_CALLSIGN:" + QString::number(m_myCall.length()) + ">" + m_myCall;
    if (m_myGrid.length() > 7) {
      myadif+=" <MY_GRIDSQUARE:8>" + m_myGrid.left(8);
    }
    else if (m_myGrid.length() > 3) {
      myadif+=" <MY_GRIDSQUARE:" + QString::number(m_myGrid.length()) + ">" + m_myGrid;
    }
    myadif+=" <CALL:" + QString::number(hisCall.length()) + ">" + hisCall;
    myadif+=" <FREQ:" + QString::number(strDialFreq.length()) + ">" + strDialFreq;
    if (mode == "FT4" || mode == "FT2" || mode == "JTTY" || mode == "Q65") myadif+=" <MODE:4>MFSK <SUBMODE:"  + QString::number(mode.length()) + ">" + mode;
    else myadif+=" <MODE:"  + QString::number(mode.length()) + ">" + mode;
    myadif+=" <QSO_DATE:8>" + m_dateTimeOn.date().toString("yyyyMMdd");
    myadif+=" <TIME_ON:6>" + m_dateTimeOn.time().toString("hhmmss");
    myadif+=" <QSO_DATE_OFF:8>" + m_dateTimeOff.date().toString("yyyyMMdd");
    myadif+=" <TIME_OFF:6>" + m_dateTimeOff.time().toString("hhmmss");
    myadif+=" <RST_SENT:" + QString::number(rptSent.length()) + ">" + rptSent;
    myadif+=" <RST_RCVD:" + QString::number(rptRcvd.length()) + ">" + rptRcvd;
    if (m_txPower.length() >0) {
       myadif+=" <TX_PWR:" + QString::number(m_txPower.length()) + ">" + m_txPower;
    }
    if (hisGrid.length() > 3) {
      myadif+=" <GRIDSQUARE:" + QString::number(hisGrid.length()) + ">" + hisGrid;
    }
    if (name.length() > 0) {
      myadif+=" <NAME:" + QString::number(name.length()) + ">" + name;
    }
    if (comments.length() > 0) {
      myadif+=" <COMMENT:" + QString::number(comments.length()) + ">" + comments;
    }
    if (m_send_to_eqsl) {
      myadif+=" <EQSL_QSL_SENT:1>Y <EQSL_QSLSDATE:8>" + m_jtdxtime->currentDateTimeUtc2().toString("yyyyMMdd");
    }
    for(auto it=propagation.cbegin();it!=propagation.cend();++it)if(!it.value().isEmpty())myadif+=" <"+it.key()+":"+QString::number(it.value().size())+">"+it.value();
    myadif+=" <EOR> ";
    myadif2 = myadif.trimmed().toUtf8();

  Q_EMIT acceptQSO (m_dateTimeOff, hisCall, hisGrid, m_dialFreq, mode, rptSent, rptRcvd, m_txPower, comments, name, m_dateTimeOn, eqslcomments, myadif2);
  
  myadif="<command:3>Log <parameters:" + QString::number(myadif.length()) + "> " + myadif;
  myadif2 = myadif.toUtf8();
  if(m_enable_tcp_connection) {
    auto sender=new LogForwarder(m_tcp_server_name,m_tcp_server_port,myadif2,
        m_debug?OperatorLog::directory(m_myCall).filePath("tcptrace.txt"):QString{},this);
    connect(sender,&LogForwarder::finished,this,[this](QString const& error){if(!error.isEmpty())emit forwardingError(error);});
  }
  QDialog::accept();
}

// closeEvent is only called from the system menu close widget for a
// modeless dialog so we use the hideEvent override to store the
// window settings
void LogQSO::hideEvent (QHideEvent * e)
{
  storeSettings ();
  QDialog::hideEvent (e);
}
