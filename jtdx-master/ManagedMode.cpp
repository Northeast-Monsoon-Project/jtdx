#include "ManagedMode.hpp"
#include "Radio.hpp"
#include <QSettings>
#include <QRegularExpression>
#include <limits>
#include <cmath>

void ManagedSettings::read(QSettings& settings) {
  auto read=[&](QString name,ManagedProfile& p,unsigned allowed) {
    settings.beginGroup("Managed/"+name);
    p.criteria=settings.value("Criteria",p.criteria).toUInt()&allowed;
    p.answerWorked=settings.value("AnswerWorked",false).toBool();
    p.callLimit=qBound(0,settings.value("CallLimit",3).toInt(),99);
    p.freeTxSlot=settings.value("FreeTxSlot",false).toBool();
    settings.endGroup();
  };
  read("Hunter",hunter,NewCqZone|NewItuZone|NewDxcc|NewBandModeDxcc|NewGrid);
  read("Enthusiast",enthusiast,NewCall|NewBandModeCall);
}
void ManagedSettings::write(QSettings& settings) const {
  auto write=[&](QString name,ManagedProfile const& p) {
    settings.beginGroup("Managed/"+name);
    settings.setValue("Criteria",p.criteria);settings.setValue("AnswerWorked",p.answerWorked);
    settings.setValue("CallLimit",qBound(0,p.callLimit,99));settings.setValue("FreeTxSlot",p.freeTxSlot);settings.endGroup();
  };
  write("Hunter",hunter);write("Enthusiast",enthusiast);
}
bool ManagedSlotPolicy::replyTxFirst(unsigned utcSeconds,double period) {
  return std::isfinite(period) && period>0 && (qRound(utcSeconds/period)%2)!=0;
}
qint64 ManagedSlotPolicy::remaining(qint64 now) const {
  if(!changed_)return 0;
  if(now<lastChange_)return 300000;
  return qMax(qint64(0),qint64(300000)-(now-lastChange_));
}
bool ManagedSlotPolicy::canSelect(bool currentFirst,bool requestedFirst,bool freeSwitch,bool handoffReady,qint64 now) const {
  if(currentFirst==requestedFirst)return true;
  return freeSwitch && handoffReady && remaining(now)==0;
}
void ManagedSession::setMode(ManagedMode mode,qint64 now) {mode_=mode;restartSchedule(now);}
void ManagedSession::restartSchedule(qint64 now) {clearCandidates();cqRemaining_=0;nextCq_=now+15*60000;}
void ManagedSession::cancelAttempt() {
  current_.clear();phaseTransmissions_.clear();transmittedPhase_.clear();responseStage_=0;
  transmittedSlot_=-1;transmittedPeriod_=0;receiveOpportunity_=false;progressSinceTransmit_=false;
}
void ManagedSession::clearCandidates() {candidates_.clear();cancelAttempt();}
void ManagedSession::observe(ManagedCandidate const& candidate) {
  if(!active() || candidate.call.isEmpty())return;
  auto key=Radio::base_callsign(candidate.call);
  auto previous=candidates_.constFind(key);
  if(previous!=candidates_.cend()
     && candidate.seen>=previous->seen && candidate.seen-previous->seen<=120000
     && (candidate.utcSeconds+86400-previous->utcSeconds)%86400>43200)return;
  if(key==Radio::base_callsign(current_) && candidate.incoming && candidate.responseStage>responseStage_) {
    // Progress is not a permanent exemption from the retry budget. Repeated
    // grids/reports and delayed messages from before our last TX do not reset
    // the current stage. A newly advanced handshake gives its reply a budget.
    bool fresh=transmittedSlot_<0 ||
      (candidate.utcSeconds+86400-unsigned((transmittedSlot_/1000)%86400))%86400<43200;
    if(fresh) {responseStage_=candidate.responseStage;progressSinceTransmit_=true;}
  }
  auto value=candidate;
  // A queued caller may subsequently send CQ. A message to someone else
  // cancels that pending invitation, rather than allowing an obsolete reply.
  if(value.grid.isEmpty())value.grid=candidates_.value(key).grid;
  candidates_.insert(key,value);
  if(candidates_.size()>1000) {
    for(auto it=candidates_.begin();it!=candidates_.end();)
      if(candidate.seen-it->seen>120000)it=candidates_.erase(it);else ++it;
  }
}
QList<ManagedCandidate> ManagedSession::candidates(qint64 now,qint64 outgoingAge) const {
  QList<ManagedCandidate> result;
  for(auto const& c:candidates_)
    if(now>=c.seen && now-c.seen<=(c.incoming?120000:outgoingAge))result.append(c);
  return result;
}
bool ManagedSession::currentInvitation(unsigned utcSeconds,qint64 nowUtcMs,qint64 periodMs) {
  if(utcSeconds>=86400 || nowUtcMs<0 || periodMs<=0)return false;
  constexpr qint64 day=86400000;
  // FT4/FT2 text timestamps omit fractional seconds. Recover their slot
  // boundary before comparing it with the current UTC slot, not arrival time.
  qint64 slot=nowUtcMs/day*day+qRound64(utcSeconds*1000.0/periodMs)*periodMs;
  if(slot-nowUtcMs>day/2)slot-=day;
  else if(nowUtcMs-slot>day/2)slot+=day;
  qint64 current=nowUtcMs/periodMs*periodMs;
  return slot>=current-periodMs && slot<=current;
}
int ManagedSession::rank(unsigned novelty) {
  for(int i=0;i<7;++i)if(novelty&(1u<<i))return i;
  return 7;
}
bool ManagedSession::blocked(QString const& call,qint64 now) const {return retries_.value(Radio::base_callsign(call)).until>now;}
ManagedCandidate ManagedSession::choose(QList<ManagedCandidate> const& candidates,ManagedProfile const& p,qint64 now,bool incomingOnly) const {
  ManagedCandidate best;
  if(!active())return best;
  for(auto const& c:candidates) {
    auto key=Radio::base_callsign(c.call);
    if(c.call.isEmpty() || completed_.value(key,-1)>=c.seen)continue;
    // A station that now calls us back has responded: the outgoing retry ban
    // must not prevent that incoming QSO.
    if(c.incoming) {if(c.workedBandMode && !p.answerWorked)continue;}
    else if(incomingOnly || !c.available || !(c.novelty&p.criteria) || blocked(c.call,now))continue;
    if(best.call.isEmpty() || (c.incoming && !best.incoming) ||
       (c.incoming==best.incoming && (rank(c.novelty)<rank(best.novelty) ||
        (rank(c.novelty)==rank(best.novelty) && (c.snr>best.snr || (c.snr==best.snr && c.call<best.call))))))best=c;
  }
  return best;
}
void ManagedSession::begin(QString const& call) {
  cancelAttempt();current_=call;
  responseStage_=candidates_.value(Radio::base_callsign(call)).responseStage;
}
QString ManagedSession::transmissionPhase(QString const& message) {
  auto text=message.simplified().toUpper();auto ending=text.section(' ',-1);
  if(ending=="73" || ending=="RR73" || ending=="RRR")return ending;
  if(QRegularExpression("^R[+-][0-9]{1,2}$").match(ending).hasMatch())return "RREPORT";
  if(QRegularExpression("^[+-][0-9]{1,2}$").match(ending).hasMatch())return "REPORT";
  return "CALL";
}
void ManagedSession::transmitted(QString const& message,qint64 slot,qint64 period) {
  if(current_.isEmpty())return;
  transmittedPhase_=transmissionPhase(message);++phaseTransmissions_[transmittedPhase_];
  progressSinceTransmit_=false;transmittedSlot_=slot;transmittedPeriod_=period;
  receiveOpportunity_=slot<0 || period<=0; // Explicit no-clock unit-test input.
}
bool ManagedSession::budgetReached(int limit,QString const& nextMessage) const {
  return limit>0 && phaseTransmissions_.value(transmissionPhase(nextMessage))>=limit;
}
void ManagedSession::receiveCycleFinished(qint64 slot,qint64 period) {
  if(transmittedSlot_<0 || transmittedPeriod_<=0 || period!=transmittedPeriod_
     || slot<transmittedSlot_+period || ((slot-transmittedSlot_)/period)%2!=1)return;
  receiveOpportunity_=true;
}
bool ManagedSession::exhausted(int limit) const {
  return budgetReached(limit) && receiveOpportunity_;
}
void ManagedSession::failed(qint64 now) {
  if(current_.isEmpty())return;
  auto key=Radio::base_callsign(current_);auto& r=retries_[key];
  if(mode_==ManagedMode::QsoEnthusiast) {
    ++r.failures;r.until=qMax(r.until,r.failures>=3?std::numeric_limits<qint64>::max():now+(r.failures==1?10:30)*60000);
  } else r.until=qMax(r.until,now+10*60000);
  candidates_.remove(key);cancelAttempt();
}
void ManagedSession::complete(qint64 now) {
  if(!current_.isEmpty())completed_.insert(Radio::base_callsign(current_),now);
  cancelAttempt();
}
bool ManagedSession::cqDue(qint64 now) {
  if(!active())return false;
  if(now>=nextCq_ && cqRemaining_==0)cqRemaining_=3;
  return cqRemaining_>0;
}
void ManagedSession::cqTransmitted(qint64 now) {
  if(cqRemaining_>0 && --cqRemaining_==0)nextCq_=now+15*60000;
}
