#ifndef JTDXF_GENERAL_OPERATING_STATE_HPP
#define JTDXF_GENERAL_OPERATING_STATE_HPP

#include <QString>
#include <QtGlobal>

// Waiting never creates a TX authorization. The caller must recheck its live
// manual-session, log, blacklist and watchdog gates for every candidate.
class GeneralOperatingState {
public:
  enum WaitMode { Off, Reply, Call, Pounce };
  enum AstroWindowAction { KeepAstroWindow, ShowAstroWindow, HideAstroWindow };
  static AstroWindowAction astroWindowAction(QString const& mode,bool automatic,bool emeDelay) {
    // WSJT-X 3.0.2 opens Echo's astronomy window even without AutoAstro.
    if(mode=="Echo")return ShowAstroWindow;
    if(!automatic)return KeepAstroWindow;
    return (mode=="Q65" || mode=="JT65") && emeDelay ? ShowAstroWindow : HideAstroWindow;
  }
  void arm(WaitMode mode,QString const& target,QString const& context) {
    reset(); mode_=mode;target_=normalized(target);context_=context;
  }
  void reset() {mode_=Off;target_.clear();context_.clear();triggered_=false;deadline_=-1;}
  WaitMode mode() const {return mode_;}
  bool waiting() const {return mode_!=Off && !triggered_;}
  bool allowsTx() const {return !waiting();}
  bool contextMatches(QString const& context) const {return mode_==Off || context==context_;}
  bool expired(qint64 monotonicMs) const {return deadline_>=0 && monotonicMs>=deadline_;}
  QString target() const {return target_;}
  bool consider(QString const& sender, bool addressedToUs, bool cq, bool endMessage,
                bool safeSession,QString const& context,qint64 monotonicMs,double periodSeconds) {
    if(mode_==Off || !safeSession || context!=context_)return false;
    auto call=normalized(sender);
    if(call.isEmpty())return false;
    if(triggered_) {
      if(mode_!=Reply && call==target_ && addressedToUs)deadline_=-1;
      return false;
    }
    bool selected=mode_==Pounce ? cq : call==target_ && (mode_==Reply ? addressedToUs&&!endMessage : cq||endMessage);
    if(!selected)return false;
    target_=call;triggered_=true;
    deadline_=monotonicMs+qMax<qint64>(1000,qRound64(periodSeconds*8000.));
    return true;
  }
private:
  static QString normalized(QString call) {return call.remove('<').remove('>').trimmed().toUpper();}
  WaitMode mode_=Off;
  QString target_,context_;
  bool triggered_=false;
  qint64 deadline_=-1;
};
#endif
