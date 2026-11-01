#ifndef JTDXF_SIGNOFF_STATE_HPP
#define JTDXF_SIGNOFF_STATE_HPP

#include <QString>
#include <QtGlobal>
#include <QList>

// QSO completion is a protocol event, not the moment a final waveform is
// selected. Inputs are explicit so neither wall-clock sleeps nor RF are needed
// to verify the state machine.
class SignoffState {
public:
  enum Message { None, RR73, SeventyThree };
  void reset() {*this=SignoffState{};}
  bool active() const {return message_!=None;}
  bool matches(QString const& call,QString const& context) const {
    return active() && call_==call && context_==context;
  }
  QString call() const {return call_;}
  Message message() const {return message_;}
  bool sending() const {return active() && !released_;}
  bool waveformFinished() const {return active() && started_ && consumed_ && naturalEnd_ && released_;}
  bool complete() const {
    return waveformFinished()
      && (message_==RR73?ack73_:quietCycle_);
  }
  bool allowsTransmit() const {
    if(!active() || sending())return true;
    if(complete())return false;
    return message_==RR73?!ack73_:repeat73_;
  }
  int transmissions() const {return message_==RR73?rr73Transmissions_:seventyThreeTransmissions_;}
  bool hadReceiveOpportunity() const {return receiveOpportunity_;}
  qint64 txSlot() const {return slot_;}
  qint64 period() const {return period_;}

  void prepare(QString const& call,QString const& context,Message message,
               qint64 slot,qint64 period) {
    if(call.isEmpty() || message==None || period<=0){reset();return;}
    bool same=matches(call,context) && message_==message;
    bool ack=same && ack73_;
    int rr73Count=matches(call,context)?rr73Transmissions_:0;
    int seventyThreeCount=matches(call,context)?seventyThreeTransmissions_:0;
    reset();call_=call;context_=context;message_=message;slot_=slot;period_=period;
    ack73_=ack;rr73Transmissions_=rr73Count;seventyThreeTransmissions_=seventyThreeCount;
  }
  void started() {if(active())started_=true;}
  void consumed() {if(started_)consumed_=true;countFinishedTransmission();}
  // Natural timing completion is separate from handing samples to the sink.
  // Cancel, audio error, manual Stop and rig faults never call this method.
  void naturalEnd() {if(active())naturalEnd_=true;countFinishedTransmission();}
  void released() {
    if(!active() || released_)return;
    released_=true;
    countFinishedTransmission();
  }
  void receive(QString const& call,QString const& context,QString const& ending,qint64 slot) {
    if(!matches(call,context) || slot<slot_)return;
    if(message_==RR73 && ending=="73")ack73_=true;
    if(message_==SeventyThree && ending=="RR73") {repeat73_=true;quietCycle_=false;}
  }
  void receiveCycleFinished(qint64 slot) {
    // Require an actual completed opposite-slot receive cycle after this
    // transmission; replay/manual/failed/incomplete decodes are filtered by
    // the caller. A later valid RX cycle is allowed after an audio gap.
    if(!active() || slot<slot_+period_)return;
    if(((slot-slot_)/period_)%2!=1)return;
    receiveOpportunity_=true;
    if(message_!=SeventyThree || repeat73_)return;
    quietCycle_=true;
  }
private:
  void countFinishedTransmission() {
    if(started_ && consumed_ && naturalEnd_ && released_ && !counted_) {
      if(message_==RR73)++rr73Transmissions_;else ++seventyThreeTransmissions_;
      counted_=true;
    }
  }
  QString call_,context_;
  Message message_=None;
  qint64 slot_=0,period_=0;
  int rr73Transmissions_=0,seventyThreeTransmissions_=0;
  bool started_=false,consumed_=false,naturalEnd_=false,released_=false;
  bool ack73_=false,repeat73_=false,quietCycle_=false;
  bool receiveOpportunity_=false,counted_=false;
};

// Sample coverage is independent of successful decoding. Neither an empty
// decoder result nor a clock timer proves that a complete RX slot was heard.
class SignoffAudioCoverage {
public:
  void reset() {period_=next_=start_=covered_=0;anchored_=full_=false;}
  QList<qint64> append(qint64 endMs,int samples,qint64 periodMs) {
    QList<qint64> result;
    if(samples<=0 || samples>60000 || endMs<0 || periodMs<=0)return result;
    const qint64 periodSamples=periodMs*12,end=endMs*12,begin=end-samples;
    if(begin<0)return result;
    // Delayed/duplicate chunks cannot advance time or count the same samples
    // twice. A new period starts a new capture context.
    if(period_==periodMs && anchored_ && end<=next_)return result;
    if(period_!=periodMs || !anchored_ || begin-next_>6000) {
      period_=periodMs;next_=begin;start_=begin/periodSamples*periodSamples;
      covered_=0;anchored_=true;full_=begin==start_;
    }
    auto advanceSlot=[&] {
      // Keep the existing 500 ms timestamp/capture tolerance, also bounding
      // the total missing coverage within one slot. Sparse short chunks must
      // never turn elapsed wall time into evidence of a complete reception.
      if(full_ && covered_>=qMax<qint64>(1,periodSamples-6000))result.append(start_/12);
      start_+=periodSamples;covered_=0;full_=true;
    };
    qint64 cursor=qMax(begin,next_);
    while(cursor>=start_+periodSamples)advanceSlot();
    while(cursor<end) {
      qint64 n=qMin(end-cursor,start_+periodSamples-cursor);
      covered_+=n;cursor+=n;
      if(cursor==start_+periodSamples)advanceSlot();
    }
    // Detector's legacy period buffer can discard a partial FFT block at
    // each boundary. Accumulating only sample counts drifts behind UTC until
    // a healthy receive cycle is incorrectly re-anchored as incomplete.
    next_=end;
    return result;
  }
private:
  qint64 period_=0,next_=0,start_=0,covered_=0;
  bool anchored_=false,full_=false;
};

#endif
