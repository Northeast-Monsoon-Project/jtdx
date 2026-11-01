// SPDX-License-Identifier: GPL-3.0-or-later
// RF corrections adapted from WSJT-X v3.0.2, Copyright the WSJT development team.
#include "AstroDialog.hpp"
#include <QSettings>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QDateTime>
#include <QHideEvent>
#include <cmath>
#include "moc_AstroDialog.cpp"

AstroDialog::AstroDialog(QSettings *settings,QWidget *parent):QDialog(parent),settings_(settings) {
  setObjectName("astronomical_data");setWindowTitle(tr("Astronomical Data"));
  auto layout=new QVBoxLayout(this);
  data_=new QLabel;data_->setTextFormat(Qt::PlainText);data_->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(data_);
  tracking_=new QCheckBox(tr("Doppler tracking"));tracking_->setObjectName("doppler_tracking");layout->addWidget(tracking_);
  auto form=new QFormLayout;layout->addLayout(form);
  method_=new QComboBox;method_->setObjectName("doppler_method");
  for(auto name:{QT_TR_NOOP("No Doppler correction"),QT_TR_NOOP("Full Doppler to DX grid"),QT_TR_NOOP("Constant frequency on the Moon"),QT_TR_NOOP("Own echo"),QT_TR_NOOP("On DX echo"),QT_TR_NOOP("Call DX")})method_->addItem(tr(name));
  form->addRow(tr("Method"),method_);
  shiftEnabled_=new QCheckBox(tr("TX frequency offset (MHz)"));shift_=new QSpinBox;shift_->setRange(-100000,100000);shift_->setObjectName("doppler_tx_shift_mhz");form->addRow(shiftEnabled_,shift_);
  rit_=new QSpinBox;rit_->setRange(-10000,10000);rit_->setSuffix(" Hz");form->addRow(tr("Echo RIT"),rit_);
  dither_=new QCheckBox(tr("Echo frequency dither (±10 Hz)"));form->addRow(dither_);
  frequency_=new QDoubleSpinBox;frequency_->setRange(0.001,1000000);frequency_->setDecimals(6);frequency_->setSuffix(" MHz");frequency_->setObjectName("eme_sked_frequency");
  auto set=new QPushButton(tr("Set frequency"));form->addRow(frequency_,set);
  auto close=new QPushButton(tr("Close"));layout->addWidget(close);connect(close,&QPushButton::clicked,this,&QWidget::hide);
  settings_->beginGroup("Astro");tracking_->setChecked(settings_->value("DopplerTracking",false).toBool());method_->setCurrentIndex(qBound(0,settings_->value("DopplerMethod",0).toInt(),5));
  shiftEnabled_->setChecked(settings_->value("EnableShift",false).toBool());shift_->setValue(settings_->value("ShiftValue",0).toInt());rit_->setValue(settings_->value("RIT",0).toInt());dither_->setChecked(settings_->value("Dither",false).toBool());
  restoreGeometry(settings_->value("geometry").toByteArray());settings_->endGroup();
  auto changed=[this]{method_->setEnabled(tracking());shift_->setEnabled(tracking() && shiftEnabled_->isChecked());shiftEnabled_->setEnabled(tracking());save();emit trackingChanged();};
  connect(tracking_,&QCheckBox::toggled,this,changed);connect(shiftEnabled_,&QCheckBox::toggled,this,changed);connect(method_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,changed);connect(shift_,QOverload<int>::of(&QSpinBox::valueChanged),this,changed);
  connect(rit_,QOverload<int>::of(&QSpinBox::valueChanged),this,changed);connect(dither_,&QCheckBox::toggled,this,changed);
  connect(set,&QPushButton::clicked,this,[this]{emit frequencyRequested(quint64(qRound64(frequency_->value()*1.e6)));});
  method_->setEnabled(tracking());shift_->setEnabled(tracking() && shiftEnabled_->isChecked());shiftEnabled_->setEnabled(tracking());
  showError(tr("Waiting for astronomical data…"));
}
void AstroDialog::save(){settings_->beginGroup("Astro");settings_->setValue("DopplerTracking",tracking());settings_->setValue("DopplerMethod",method_->currentIndex());settings_->setValue("EnableShift",shiftEnabled_->isChecked());settings_->setValue("ShiftValue",shift_->value());settings_->setValue("RIT",rit_->value());settings_->setValue("Dither",dither());settings_->setValue("geometry",saveGeometry());settings_->endGroup();}
void AstroDialog::hideEvent(QHideEvent *event){save();QDialog::hideEvent(event);emit trackingChanged();}
bool AstroDialog::tracking() const{return tracking_->isChecked();}
bool AstroDialog::dither() const{return dither_->isChecked();}
int AstroDialog::rit() const{return rit_->value();}
void AstroDialog::setNominalFrequency(quint64 hz){if(!frequency_->hasFocus())frequency_->setValue(hz/1.e6);}
void AstroDialog::showError(QString const& error){data_->setText(error);}
void AstroDialog::showData(QJsonObject const& value){
  auto n=[&](char const* key,int precision=1){auto number=value.value(key);return number.isDouble() && std::isfinite(number.toDouble())?QString::number(number.toDouble(),'f',precision):QStringLiteral("—");};
  auto utc=QDateTime::fromMSecsSinceEpoch(qint64(value.value("utc_ms").toDouble()),Qt::UTC);
  data_->setText(utc.toString("yyyy-MM-dd hh:mm:ss 'UTC'")+"\n"+
    tr("Moon Az / El: %1° / %2°").arg(n("moon_az_deg"),n("moon_el_deg"))+"\n"+
    tr("DX Moon Az / El: %1° / %2°").arg(n("dx_moon_az_deg"),n("dx_moon_el_deg"))+"\n"+
    tr("Sun Az / El: %1° / %2°").arg(n("sun_az_deg"),n("sun_el_deg"))+"\n"+
    tr("Own / DX Doppler: %1 / %2 Hz").arg(n("self_doppler_hz",0),n("dx_doppler_hz",0))+"\n"+
    tr("Echo delay: %1 s; spread: %2 / %3 Hz").arg(n("echo_delay_seconds",3),n("width_self_hz"),n("width_dx_hz"))+"\n"+
    tr("Sky: %1 K; degradation: %2 dB; polarization: %3°").arg(n("sky_temperature_k",0),n("degradation_db"),n("polarization_deg")));
}
AstroDialog::Correction AstroDialog::dopplerCorrection(int method,double self,double dx,qint64 shift,bool echo){
  Correction result;if(!std::isfinite(self) || !std::isfinite(dx) || std::abs(self)>1e7 || std::abs(dx)>1e7)return result;
  qint64 own=qRound64(self),other=qRound64(dx);
  switch(method) {
  case 1:result.rx=echo?0:other;result.tx=-other+shift;break;
  case 2:result.rx=own/2;result.tx=-result.rx+shift;break;
  case 3:result.rx=own;result.tx=0;break;
  case 4:result.rx=other;result.tx=other-own+shift;break;
  case 5:result.tx=-own+shift;break;
  default:result.tx=shift;break;
  }return result;
}
AstroDialog::Correction AstroDialog::correction(QJsonObject const& data,bool echo,bool fixedTransmit) const {
  if(!tracking())return {};
  qint64 shift=shiftEnabled_->isChecked()?qint64(shift_->value())*1000000:0;
  auto result=dopplerCorrection(method_->currentIndex(),data.value("self_doppler_hz").toDouble(),data.value("dx_doppler_hz").toDouble(),shift,echo);
  if(fixedTransmit && data.contains("tx_self_doppler_hz") && data.contains("tx_dx_doppler_hz") && method_->currentIndex()!=3)
    result.tx=dopplerCorrection(method_->currentIndex(),data.value("tx_self_doppler_hz").toDouble(),data.value("tx_dx_doppler_hz").toDouble(),shift,echo).tx;
  return result;
}
