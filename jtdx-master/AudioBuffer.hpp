// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef JTDXF_AUDIO_BUFFER_HPP
#define JTDXF_AUDIO_BUFFER_HPP
#include <QMutex>
#include <QMutexLocker>
// Protect the shared capture samples and capture cursor across audio/GUI threads.
// Hold only while computing/copying samples, never across GUI or device waits.
inline QMutex& captureBufferMutex() { static QMutex mutex; return mutex; }
#endif
