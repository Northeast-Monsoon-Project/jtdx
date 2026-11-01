// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FM_UPSTREAM_OPERATING_STATE_HPP
#define FM_UPSTREAM_OPERATING_STATE_HPP
#include <QList>
#include <QMap>
#include <QString>
#include <limits>

// Small deterministic state shared by the GUI glue and its offline tests.
class UpstreamOperatingState {
public:
  static int q65MaximumSubmode(int period) {
    return period==15?2:period==30?3:period==120?5:4;
  }
  struct Echo {qint64 soundStartMs=0;int dither=0;double selfDopplerHz=0,txCorrectionHz=0,delaySeconds=2.5;};
  void cancelTransmit(){repeatKey_.clear();rr73Count_=0;echoes_.clear();}
  void resetCapture(){lastMskEndMs_=0;echoes_.clear();observedQ65_=-1;completedQ65_=-1;}
  void observeQ65(qint64 slotMs){
    // A UTC correction starts a new capture timeline. Completion from a
    // formerly later slot must not authorize TX before the new RX is decoded.
    if(slotMs<observedQ65_)completedQ65_=-1;
    observedQ65_=slotMs;
  }
  void completeQ65(qint64 slotMs){
    if(slotMs<=observedQ65_)completedQ65_=qMax(completedQ65_,slotMs);
  }
  bool waitingForQ65(qint64 previousSlotMs)const{return observedQ65_>=previousSlotMs && completedQ65_<previousSlotMs;}
  bool continuousMsk(qint64 endMs,int samples) {
    const bool continuous=!lastMskEndMs_ || qAbs(endMs-lastMskEndMs_-samples/12)<=500;
    lastMskEndMs_=endMs;return continuous;
  }
  bool finishedMessage(bool repeat,QString const& mode,QString const& context,QString const& target,QString const& message) {
    if(!repeat || (mode!="Q65" && mode!="MSK144")){repeatKey_.clear();rr73Count_=0;return false;}
    const auto key=context+"|"+target;
    if(key!=repeatKey_){repeatKey_=key;rr73Count_=0;}
    const auto words=message.simplified().toUpper().split(' ');
    if(words.isEmpty())return false;
    if(words.last()=="73"){rr73Count_=0;return true;}
    if(words.last()=="RR73" && !target.isEmpty())return ++rr73Count_>=10;
    rr73Count_=0;return false;
  }
  bool waitingFor73(bool repeat,QString const& mode,QString const& context,QString const& target) const {
    return repeat && (mode=="Q65" || mode=="MSK144") && !target.isEmpty() &&
      repeatKey_==context+"|"+target && rr73Count_>0 && rr73Count_<10;
  }
  void rememberEcho(qint64 slotMs,Echo value){echoes_.insert(slotMs,value);while(echoes_.size()>4)echoes_.erase(echoes_.begin());}
  bool takeEcho(qint64 slotMs,Echo& value){auto it=echoes_.find(slotMs);if(it==echoes_.end())return false;value=it.value();echoes_.erase(it);return true;}
  void expectReceiveFrequency(quint64 nominal,qint64 correction) {
    const quint64 physical=shifted(nominal,correction);
    if(!commands_.isEmpty() && commands_.last().physical==physical && commands_.last().nominal==nominal)return;
    Command command;command.physical=physical;command.nominal=nominal;command.correction=correction;commands_.append(command);
    while(commands_.size()>32)commands_.removeFirst();
  }
  quint64 nominalFromReport(quint64 physical,quint64 previous,quint64 nominal,qint64 desiredCorrection) {
    // PTT/SWR reports carry unchanged dial frequency while a new CAT command
    // may still be in flight. They must not reinterpret it with a new offset.
    if(physical==previous)return nominal;
    // Only commands for the current nominal frequency can be delayed Doppler
    // acknowledgements. Revisiting a previously used band on the rig is a
    // real QSY, even if its frequency remains in the command history.
    for(int i=commands_.size()-1;i>=0;--i)if(commands_[i].physical==physical && commands_[i].nominal==nominal) {
      appliedCorrection_=commands_[i].correction;haveApplied_=true;return nominal;
    }
    const qint64 correction=haveApplied_?appliedCorrection_:desiredCorrection;
    return correction==std::numeric_limits<qint64>::min()?physical:shifted(physical,-correction);
  }
  static quint64 shifted(quint64 base,qint64 correction) {
    // Avoid signed overflow even for a malicious/minimum qint64 correction.
    const quint64 magnitude=correction<0?quint64(-(correction+1))+1:quint64(correction);
    if(correction<0)return magnitude>base?base:base-magnitude;
    return base>std::numeric_limits<quint64>::max()-magnitude?base:base+magnitude;
  }
private:
  struct Command{quint64 physical=0,nominal=0;qint64 correction=0;};
  QList<Command> commands_;
  QMap<qint64,Echo> echoes_;
  QString repeatKey_;
  int rr73Count_=0;
  qint64 appliedCorrection_=0;
  bool haveApplied_=false;
  qint64 lastMskEndMs_=0;
  qint64 observedQ65_=-1,completedQ65_=-1;
};
#endif
