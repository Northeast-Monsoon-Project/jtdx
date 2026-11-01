// last time modified by Igor UA3DJY on 20200128

#ifndef MODULATOR_HPP__
#define MODULATOR_HPP__

#include <QAudio>
#include <atomic>
#include <QPointer>

#include "JTDXDateTime.h"
#include "AudioDevice.hpp"

class SoundOutput;

//
// Input device that generates PCM audio frames that encode a message
// and an optional CW ID.
//
// Output can be muted while underway, preserving waveform timing when
// transmission is resumed.
//
class Modulator
  : public AudioDevice
{
  Q_OBJECT;

public:
  enum ModulatorState {Synchronizing, Active, Idle};

  Modulator (unsigned frameRate, double periodLengthInSeconds, JTDXDateTime * jtdxtime, QObject * parent = nullptr);

  void close () override;

  bool isTuning () const {return m_tuning;}
  double frequency () const {return m_frequency;}
  bool isActive () const {return m_state != Idle;}
  Q_SLOT void setSpread(double s) {m_fSpread=s;}
  Q_SLOT void setPeriod(double p) {m_period=qBound(1.0,p,120.0);}
  void set_nsym(int n) {m_symbolsLength=n;}
  void set_ms0(qint64 ms) {m_ms0=ms;}

  Q_SLOT void start (unsigned symbolsLength, double framesPerSymbol, double frequency,
                     double toneSpacing, SoundOutput *, Channel = Mono,
                     bool synchronize = true, double dBSNR = 99., double TRperiod=60.0, quint64 generation=0);
  Q_SLOT void stop (bool quick = false);
  Q_SLOT void startPcm(QByteArray pcm,SoundOutput*,Channel=Mono,quint64 generation=0);
  Q_SIGNAL void pcmConsumed() const;
  Q_SIGNAL void waveformStarted(quint64 generation) const;
  // All waveform samples (legacy oscillator or finite PCM) have been supplied to the audio sink.
  // This is not a hardware playback/PTT completion notification.
  Q_SIGNAL void waveformConsumed(quint64 generation) const;
  Q_SLOT void tune (bool newState = true);
  Q_SLOT void setFrequency (double newFrequency) {m_frequency = newFrequency;}
  Q_SIGNAL void stateChanged (ModulatorState) const;

protected:
  qint64 readData (char * data, qint64 maxSize) override;
  qint64 writeData (char const * /* data */, qint64 /* maxSize */) override
  {
    return -1;			// we don't consume data
  }

private:
  friend class RegressionTest;
  qint16 postProcessSample (qint16 sample) const;

  QPointer<SoundOutput> m_stream;
  QByteArray m_pcm;
  int m_pcmOffset=0;
  bool m_pcmMode=false;
  bool m_waveformConsumed=false;
  bool m_waveformStarted=false;
  quint64 m_waveformGeneration=0;
  bool m_quickClose = false;

  unsigned m_symbolsLength = 0;

  static double constexpr m_twoPi = 2.0 * 3.141592653589793238462;
  unsigned m_nspd = 2048 + 512; // CW ID WPM factor = 22.5 WPM

  double m_phi = 0;
  double m_dphi = 0;
  double m_amp = 0;
  double m_nsps = 0;
  double m_frequency = 1500.0;
  double m_frequency0 = 0;
  double m_snr = 0;
  double m_fac = 0;
  double m_toneSpacing = 0;
  double m_fSpread = 0;
  double m_TRperiod = 0;
  double m_period = 0;

  qint64 m_silentFrames = 0;
  qint64 m_ms0 = 0;
  qint16 m_ramp = 0;

  unsigned m_frameRate = 0;
  std::atomic<ModulatorState> m_state;

  bool volatile m_tuning;
  bool m_addNoise = false;
  bool m_bFastMode = false;

  bool m_cwLevel = false;
  unsigned m_ic = 0;
  unsigned m_isym0 = 0;
  int m_j0 = 0;
  double m_toneFrequency0 = 0;
  JTDXDateTime * m_jtdxtime;
  std::string debug_file_;
};

#endif
