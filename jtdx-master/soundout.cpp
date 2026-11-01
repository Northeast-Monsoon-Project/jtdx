#include "soundout.h"

#include <QSysInfo>
#include <qmath.h>

#include "moc_soundout.cpp"

QString SoundOutput::errorText (QAudio::Error error) const
{
  switch (error)
    {
    case QAudio::OpenError: return tr ("An error opening the audio output device has occurred.");
    case QAudio::IOError: return tr ("An error occurred during write to the audio output device.");
    case QAudio::UnderrunError: return tr ("Audio data not being fed to the audio output device fast enough.");
    case QAudio::FatalError: return tr ("Non-recoverable error, audio output device not usable at this time.");
    case QAudio::NoError: return {};
    }
  return {};
}

void SoundOutput::halt (bool quick, bool close_source)
{
  // Invalidate before touching Qt: reset/stop can synchronously emit a state
  // change, and a queued callback from an earlier start may still be pending.
  ++m_serial;
  m_playing = false;
  m_sourceExhausted = false;
  m_drained = false;
  disconnect (m_stateConnection);
  auto source = m_source;
  m_source.clear ();
  if (m_stream)
    {
      if (quick) m_stream->reset ();
      else m_stream->stop ();
    }
  // Modulator::close calls back into stop/reset. State is already invalidated
  // and the source detached, so that path cannot report another failure.
  if (close_source && source) source->close ();
}

void SoundOutput::fail (QString const& message)
{
  if (!m_playing) return;
  auto const generation = m_generation;
  halt (true, true);
  Q_EMIT status (tr ("Error"));
  Q_EMIT playbackError (generation, message);
}

void SoundOutput::checkError ()
{
  if (m_playing && m_stream && m_stream->error () != QAudio::NoError)
    {
      // Qt can report an underrun when a finite PCM source reaches EOF.
      // Only the source's explicit end marker makes that a successful drain.
      if (m_stream->error () == QAudio::UnderrunError && m_sourceExhausted) return;
      fail (errorText (m_stream->error ()));
    }
}

void SoundOutput::setFormat (QAudioDeviceInfo const& device, unsigned channels, int frames_buffered)
{
  halt (true, true);
  m_stream.reset ();
  QAudioFormat format (device.preferredFormat ());
  format.setChannelCount (channels);
  format.setCodec ("audio/pcm");
  format.setSampleRate (48000);
  format.setSampleType (QAudioFormat::SignedInt);
  format.setSampleSize (16);
  format.setByteOrder (QAudioFormat::Endian (QSysInfo::ByteOrder));
  if (!format.isValid () || channels < 1 || channels > 2)
    {
      Q_EMIT error (tr ("Requested output audio format is not valid."));
      return;
    }
  if (!device.isFormatSupported (format))
    {
      Q_EMIT error (tr ("Requested output audio format is not supported on device."));
      return;
    }
  m_framesBuffered = frames_buffered;
  m_stream.reset (new QAudioOutput (device, format));
  m_stream->setVolume (m_volume);
  m_stream->setNotifyInterval (100);
  if (m_stream->error () != QAudio::NoError)
    {
      auto const message = errorText (m_stream->error ());
      m_stream.reset ();
      Q_EMIT error (message);
    }
}

void SoundOutput::restart (QIODevice * source, quint64 generation)
{
  halt (true);
  m_source = source;
  m_generation = generation;
  m_playing = true;
  if (!m_stream || !source || !source->isReadable ())
    {
      fail (tr ("An error opening the audio output device has occurred."));
      return;
    }
  auto const serial = m_serial;
  m_stateConnection = connect (m_stream.data (), &QAudioOutput::stateChanged, this,
                              [this, serial] (QAudio::State state)
    {
      if (serial == m_serial && m_stream)
        handleStateChanged (state, m_stream->error (), serial);
    });
  // Windows forgets the chosen buffer size after stop. Restore each start.
  if (m_framesBuffered > 0)
    m_stream->setBufferSize (m_stream->format ().bytesForFrames (m_framesBuffered));
  m_stream->setCategory ("production");
  m_stream->start (source);
  checkError ();
}

void SoundOutput::sourceExhausted (quint64 generation)
{
  if (m_playing && generation == m_generation) m_sourceExhausted = true;
}

void SoundOutput::suspend ()
{
  if (m_playing && m_stream && m_stream->state () == QAudio::ActiveState)
    { m_stream->suspend (); checkError (); }
}

void SoundOutput::resume ()
{
  if (m_playing && m_stream && m_stream->state () == QAudio::SuspendedState)
    { m_stream->resume (); checkError (); }
}

void SoundOutput::reset () { halt (true); }
void SoundOutput::stop () { halt (false); }

qreal SoundOutput::attenuation () const
{ return -(20. * qLn (m_volume) / qLn (10.)); }

void SoundOutput::setAttenuation (qreal a)
{
  Q_ASSERT (0.0 <= a && a <= 450.1);
  // VAC needs a little headroom to avoid stream distortion.
  m_volume = qPow (10.0, -a / 20.0) * 0.9;
  if (m_stream) m_stream->setVolume (m_volume);
}

void SoundOutput::resetAttenuation ()
{ m_volume = 1.; if (m_stream) m_stream->setVolume (m_volume); }

void SoundOutput::handleStateChanged (QAudio::State state, QAudio::Error error, quint64 serial)
{
  if (!m_playing || serial != m_serial) return;
  if (error != QAudio::NoError && !(state == QAudio::IdleState
                                  && error == QAudio::UnderrunError && m_sourceExhausted))
    {
      fail (errorText (error));
      return;
    }
  switch (state)
    {
    case QAudio::IdleState:
      if (!m_sourceExhausted)
        {
          fail (errorText (QAudio::UnderrunError));
          return;
        }
      Q_EMIT status (tr ("Idle"));
      if (!m_drained) { m_drained = true; Q_EMIT playbackDrained (m_generation); }
      break;
    case QAudio::ActiveState: Q_EMIT status (tr ("Sending")); break;
    case QAudio::SuspendedState: Q_EMIT status (tr ("Suspended")); break;
#if QT_VERSION >= QT_VERSION_CHECK (5, 10, 0)
    case QAudio::InterruptedState: Q_EMIT status (tr ("Interrupted")); break;
#endif
    case QAudio::StoppedState:
      // A backend stopping by itself before EOF must cancel this request.
      // Intentional stop/reset already invalidated its callback above.
      fail (errorText (QAudio::IOError));
      break;
    }
}
