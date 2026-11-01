#include "ManagedSettings.hpp"
#include <QCheckBox>
#include <QSpinBox>
#include <QGroupBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QLabel>
ManagedSettingsWidget::ManagedSettingsWidget(QWidget* parent):QWidget(parent) {
  setObjectName("managed_tab");auto layout=new QVBoxLayout(this);
  auto add=[&](QString title,QString name,Controls& controls,QList<unsigned> flags) {
    auto group=new QGroupBox(title,this);group->setObjectName(name);auto form=new QFormLayout(group);
    for(auto flag:flags) {
      QString text;
      switch(flag) {
        case NewCqZone:text=tr("New CQ zone");break;case NewItuZone:text=tr("New ITU zone");break;
        case NewDxcc:text=tr("New DXCC");break;case NewBandModeDxcc:text=tr("New band && mode DXCC");break;
        case NewGrid:text=tr("New grid");break;case NewCall:text=tr("New callsign");break;
        case NewBandModeCall:text=tr("New band && mode callsign");break;
      }
      auto box=new QCheckBox(text,group);box->setObjectName(name+"_criterion_"+QString::number(flag));
      controls.criteria.insert(flag,box);form->addRow(box);
    }
    controls.answer=new QCheckBox(tr("Answer previously worked callsigns"),group);controls.answer->setObjectName(name+"_answer_worked");form->addRow(controls.answer);
    controls.freeTxSlot=new QCheckBox(tr("Allow TX slot changes"),group);controls.freeTxSlot->setObjectName(name+"_free_tx_slot");
    controls.freeTxSlot->setToolTip(tr("Allow automatic TX slot changes between completed QSOs. After a slot change, wait 5 minutes before another automatic change. Manual changes are always allowed and restart this timer. Off: stay in the selected TX slot."));
    form->addRow(controls.freeTxSlot);
    auto cq=new QCheckBox(tr("Send three CQ rounds every 15 minutes"),group);cq->setObjectName(name+"_periodic_cq");cq->setChecked(true);cq->setEnabled(false);form->addRow(cq);
    controls.limit=new QSpinBox(group);controls.limit->setObjectName(name+"_call_limit");controls.limit->setRange(0,99);controls.limit->setMaximumWidth(180);controls.limit->setSpecialValueText(tr("0 — unlimited"));
    form->addRow(tr("Unanswered call limit:"),controls.limit);layout->addWidget(group);
  };
  add(tr("DXCC hunter"),"managed_hunter",hunter_,{NewCqZone,NewItuZone,NewDxcc,NewBandModeDxcc,NewGrid});
  add(tr("QSO enthusiast (also prioritizes new DXCC entities and zones)"),"managed_enthusiast",enthusiast_,{NewCall,NewBandModeCall});
  layout->addStretch();setSettings(ManagedSettings{});
}
void ManagedSettingsWidget::setSettings(ManagedSettings const& s) {
  auto set=[](Controls& c,ManagedProfile const& p) {for(auto it=c.criteria.begin();it!=c.criteria.end();++it)it.value()->setChecked(p.criteria&it.key());c.answer->setChecked(p.answerWorked);c.limit->setValue(p.callLimit);c.freeTxSlot->setChecked(p.freeTxSlot);};
  set(hunter_,s.hunter);set(enthusiast_,s.enthusiast);
}
ManagedSettings ManagedSettingsWidget::settings() const {
  ManagedSettings s;
  auto get=[](Controls const& c,ManagedProfile& p) {p.criteria=0;for(auto it=c.criteria.begin();it!=c.criteria.end();++it)if(it.value()->isChecked())p.criteria|=it.key();p.answerWorked=c.answer->isChecked();p.callLimit=c.limit->value();p.freeTxSlot=c.freeTxSlot->isChecked();};
  get(hunter_,s.hunter);get(enthusiast_,s.enthusiast);return s;
}
