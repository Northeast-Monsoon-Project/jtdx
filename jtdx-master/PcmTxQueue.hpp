#ifndef PCM_TX_QUEUE_HPP
#define PCM_TX_QUEUE_HPP
#include <QByteArray>
#include <QtEndian>
#include <algorithm>
// Access only from the CAT thread. Stereo float output is the TCI wire format.
class PcmTxQueue {
  QByteArray pcm_;
  int position_=0;
public:
  bool start(QByteArray const& pcm) {
    clear();
    if(pcm.isEmpty() || pcm.size()%2 || pcm.size()>48000*2*600)return false;
    pcm_=pcm;return true;
  }
  void clear(){pcm_.clear();position_=0;}
  bool active()const{return !pcm_.isEmpty();}
  // Returns true exactly when an active payload has been completely consumed.
  bool read(float* samples,int count,bool ptt,float gain=1.f) {
    if(!samples||count<=0)return false;
    std::fill(samples,samples+count,0.f);
    if(!ptt || !active() || count%2)return false;
    for(int i=0;i<count&&position_<pcm_.size();i+=2,position_+=2){
      auto sample=qFromLittleEndian<qint16>(reinterpret_cast<uchar const*>(pcm_.constData()+position_));
      samples[i]=samples[i+1]=(sample/32768.f)*gain;
    }
    if(position_==pcm_.size()){clear();return true;}
    return false;
  }
};
#endif
