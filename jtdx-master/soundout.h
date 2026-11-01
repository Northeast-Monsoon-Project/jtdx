// -*- Mode: C++ -*-
#ifndef SOUNDOUT_H__
#define SOUNDOUT_H__

#include <QObject>
#include <QString>
#include <QAudioOutput>
#include <QAudioDeviceInfo>
#include <QPointer>

// All playback state belongs to the audio thread. The GUI only receives
// generation-tagged notifications; it never reads this object's state.
class SoundOutput : public QObject
{
  Q_OBJECT;

public:
  qreal attenuation () const;

public Q_SLOTS:
  void setFormat (QAudioDeviceInfo const& device, unsigned channels, int frames_buffered = 0);
  void restart (QIODevice *, quint64 generation = 0);
  void sourceExhausted (quint64 generation);
  void suspend ();
  void resume ();
  void reset ();
  void stop ();
  void setAttenuation (qreal);
  void resetAttenuation ();

Q_SIGNALS:
  // Configuration validation is independent of a transmit request.
  void error (QString message) const;
  // The backend is already stopped and the source closed before emission.
  void playbackError (quint64 generation, QString message) const;
  void playbackDrained (quint64 generation) const;
  void status (QString message) const;

private:
  friend class RegressionTest;
  QString errorText (QAudio::Error) const;
  void halt (bool quick, bool close_source = false);
  void fail (QString const& message);
  void checkError ();
  void handleStateChanged (QAudio::State, QAudio::Error, quint64 serial);

  QScopedPointer<QAudioOutput> m_stream;
  QPointer<QIODevice> m_source;
  QMetaObject::Connection m_stateConnection;
  int m_framesBuffered = 0;
  qreal m_volume = 1.0;
  quint64 m_generation = 0;
  quint64 m_serial = 0;
  bool m_playing = false;
  bool m_sourceExhausted = false;
  bool m_drained = false;
};

#endif
