// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FM_PERIOD_AUDIO_BUFFER_HPP
#define FM_PERIOD_AUDIO_BUFFER_HPP
#include <QByteArray>
#include <QList>
#include <QtGlobal>
#include <limits>

// Capture for long WSJT-X modes, independent of the JTDX shared-memory ABI.
// UTC timestamps identify frames; sample counts preserve continuity within them.
// Detector timestamps are callback arrival times, not hardware sample clocks.
// Up to 500 ms of callback jitter is tolerated; larger steps re-anchor and drop
// the incomplete frame. Detecting actual dropped samples inside that tolerance
// requires a capture-device sequence counter, which this interface lacks.
class PeriodAudioBuffer {
public:
  struct Frame {qint64 utcMs=0;QByteArray pcm;bool partial=false;int missingPrefixSamples=0;};
  void setPeriod(int seconds){period_=qBound(5,seconds,1800);reset();}
  void reset(){data_.clear();start_=0;nextSample_=0;partial_=false;anchored_=false;missingPrefixSamples_=0;published_=false;}
  // Publish a complete active payload before the period boundary. The helper
  // zero-pads the unused tail; do not decode the same period again on rollover.
  bool snapshot(int minimumSamples,Frame& frame) {
    if(!anchored_ || partial_ || published_ || minimumSamples<=0 || data_.size()/2<minimumSamples)return false;
    frame.utcMs=start_;frame.pcm=data_;frame.partial=false;frame.missingPrefixSamples=0;published_=true;return true;
  }
  QList<Frame> append(QByteArray const& pcm,qint64 endUtcMs) {
    QList<Frame> frames;
    if(pcm.isEmpty())return frames;
    if(pcm.size()%2 || pcm.size()>24000*5 || endUtcMs<0 ||
       endUtcMs>std::numeric_limits<qint64>::max()/12-1800000) {
      reset();return frames;
    }
    qint64 samples=pcm.size()/2,beginSample=endUtcMs*12-samples;
    if(beginSample<0){reset();return frames;}
    if(!anchored_ || qAbs(beginSample-nextSample_)>6000) {
      reset();anchored_=true;start_=(beginSample/(period_*12000LL))*(period_*1000LL);
      nextSample_=beginSample;data_=QByteArray(int((beginSample-start_*12)*2),0);partial_=!data_.isEmpty();
      missingPrefixSamples_=data_.size()/2;
    }
    int offset=0;
    while(offset<pcm.size()) {
      int n=qMin(pcm.size()-offset,period_*24000-data_.size());
      data_.append(pcm.constData()+offset,n);offset+=n;nextSample_+=n/2;
      if(data_.size()==period_*24000) {
        if(!published_){Frame frame;frame.utcMs=start_;frame.pcm=data_;frame.partial=partial_;frame.missingPrefixSamples=missingPrefixSamples_;frames.append(frame);}
        data_.clear();start_+=period_*1000LL;partial_=false;missingPrefixSamples_=0;published_=false;
      }
    }
    return frames;
  }
  int samples() const {return data_.size()/2;}
private:
  int period_=60;
  QByteArray data_;
  qint64 start_=0,nextSample_=0;
  bool partial_=false,anchored_=false;
  int missingPrefixSamples_=0;
  bool published_=false;
};
#endif
