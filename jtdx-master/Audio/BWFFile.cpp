#include "BWFFile.hpp"

#include <cstring>
#include <numeric>
#include <algorithm>

#include <qendian.h>
#include <QAudioFormat>
#include <QDateTime>
#include <QDate>
#include <QTime>
#include <QString>
#include <QUuid>

#include "pimpl_impl.hpp"

#include "moc_BWFFile.cpp"

namespace
{
  // chunk descriptor
  struct Desc
  {
    Desc ()
      : id_ {{'J','U','N','K'}}, size_ {0}
    {
    }

    explicit Desc (char const * id, quint32 size = 0)
      : size_ {size}
    {
      set (id);
    }

    void set (char const * id = nullptr)
    {
      if (id)
        {
          auto len = std::min (size_t (4), strlen (id));
          memcpy (id_.data (), id, len);
          memset (id_.data () + len, ' ', 4u - len);
        }
      else
        {
          memcpy (id_.data (), "JUNK", 4);
        }
    }

    void set (char const * id, quint32 size)
    {
      set (id);
      size_ = size;
    }

    char * operator & () {return reinterpret_cast<char *> (this);}
    char const * operator & () const {return &*this;}

    std::array<char, 4> id_;
    quint32 size_;
  };

  // "fmt " chunk contents
  struct FormatChunk
  {
    quint16 audio_format;
    quint16 num_channels;
    quint32 sample_rate;
    quint32 byte_rate;
    quint16 block_align;
    quint16 bits_per_sample;
  };

  // "bext" chunk contents
  #pragma pack(push, 1)
  struct BroadcastAudioExtension
  {
    using Version = BWFFile::BextVersion;
    using UMID = BWFFile::UMID;

    BroadcastAudioExtension (Version version = Version::v_0)

    {
      std::memset(this, 0, sizeof *this);
      version_ = static_cast<quint16>(version);
      // set some sensible defaults for the "bext" fields
      auto now = QDateTime::currentDateTimeUtc ();
      std::strncpy (origination_date_,
                    now.date ().toString ("yyyy-MM-dd").toLocal8Bit ().constData (),
                    sizeof origination_date_);
      std::strncpy (origination_time_,
                    now.time ().toString ("hh-mm-ss").toLocal8Bit ().constData (),
                    sizeof origination_time_);
      auto uuid = QUuid::createUuid ().toRfc4122 ();
      std::copy (uuid.cbegin (), uuid.cend (), umid_.data () + 16);
    }

    char description_[256];
    char originator_[32];
    char originator_reference_[32];
    char origination_date_[10];
    char origination_time_[8];
    quint32 time_reference_low_;
    quint32 time_reference_high_;
    quint16 version_;
    UMID umid_;                       // V1 zero for V0
    quint16 loudness_value_;          // V2
    quint16 loudness_range_;          // V2
    quint16 max_true_peak_level_;     // V2
    quint16 max_momentary_loudness_;  // V2
    quint16 max_short_term_loudness_; // V2
    quint8 reserved_[180];
    char coding_history_[];
  };
  #pragma pack(pop)
  static_assert(sizeof(BroadcastAudioExtension)==602,"EBU Tech 3285 bext size");

  // Fixed-width strings on disk are not necessarily NUL-terminated.
  template<size_t N> QByteArray boundedText(char const (&value)[N]) {
    return QByteArray(value, int(std::find(value,value+N,'\0')-value));
  }
  void convertBext(BroadcastAudioExtension *p, bool bigEndian) {
    auto u16=[bigEndian](quint16 v){return bigEndian?qFromBigEndian(v):qFromLittleEndian(v);};
    auto u32=[bigEndian](quint32 v){return bigEndian?qFromBigEndian(v):qFromLittleEndian(v);};
    p->time_reference_low_=u32(p->time_reference_low_);
    p->time_reference_high_=u32(p->time_reference_high_);
    p->version_=u16(p->version_);
    p->loudness_value_=u16(p->loudness_value_);
    p->loudness_range_=u16(p->loudness_range_);
    p->max_true_peak_level_=u16(p->max_true_peak_level_);
    p->max_momentary_loudness_=u16(p->max_momentary_loudness_);
    p->max_short_term_loudness_=u16(p->max_short_term_loudness_);
  }
}

class BWFFile::impl final
{
public:
  impl (QAudioFormat const& format)
    : header_dirty_ {true}
    , format_ {format}
    , header_length_ {-1}
    , data_size_ {-1}
  {
  }

  impl (QAudioFormat const& format, QString const& name)
    : header_dirty_ {true}
    , format_ {format}
    , file_ {name}
    , header_length_ {-1}
    , data_size_ {-1}
  {
  }

  impl (QAudioFormat const& format, QString const& name, InfoDictionary const& dictionary)
    : header_dirty_ {true}
    , format_ {format}
    , file_ {name}
    , info_dictionary_ {dictionary}
    , header_length_ {-1}
    , data_size_ {-1}
  {
  }

  ~impl ()
  {
    file_.close ();
  }

  bool initialize (BWFFile * self, OpenMode mode);
  bool read_header ();
  bool write_header (QAudioFormat);
  bool update_header ();

  BroadcastAudioExtension const * bext () const
  {
    return bext_.isEmpty () ? nullptr : reinterpret_cast<BroadcastAudioExtension const *> (bext_.constData ());
  }

  BroadcastAudioExtension * bext ()
  {
    if (bext_.isEmpty ())       // create a "bext" chunk in place
      {
        bext_.fill ('\0', sizeof (BroadcastAudioExtension));
        new (bext_.data ()) BroadcastAudioExtension {};
      }
    return reinterpret_cast<BroadcastAudioExtension *> (bext_.data ());
  }

  bool header_dirty_;
  QAudioFormat format_;
  QFile file_;
  QByteArray bext_;
  InfoDictionary info_dictionary_;
  qint64 header_length_;
  qint64 data_size_;
};

bool BWFFile::impl::initialize (BWFFile * self, OpenMode mode)
{
  bool result {false};
  if (mode & Append)
    {
      result = file_.seek (file_.size ());
      if (result) result = self->seek (file_.size () - header_length_);
    }
  else
    {
      result = self->seek (0);
    }
  return result;
}

bool BWFFile::impl::read_header ()
{
  header_length_=-1;data_size_=-1;
  if(!(file_.openMode()&ReadOnly) || !file_.seek(0))return false;
  Desc outer;
  if(file_.read(&outer,sizeof outer)!=sizeof outer)return false;
  bool be=!memcmp(outer.id_.data(),"RIFX",4);
  if(!be && memcmp(outer.id_.data(),"RIFF",4))return false;
  auto read32=[be](quint32 v){return be?qFromBigEndian(v):qFromLittleEndian(v);};
  auto read16=[be](quint16 v){return be?qFromBigEndian(v):qFromLittleEndian(v);};
  qint64 end=8+qint64(read32(outer.size_));
  if(end<12 || end>file_.size() || file_.read(4)!="WAVE")return false;
  InfoDictionary parsedInfo=info_dictionary_;QByteArray parsedBext;
  bool haveFormat=false;
  qint64 metadata=0;
  while(file_.pos()<end) {
    if(end-file_.pos()<qint64(sizeof(Desc)))return false;
    Desc chunk;if(file_.read(&chunk,sizeof chunk)!=sizeof chunk)return false;
    qint64 size=read32(chunk.size_),start=file_.pos();
    if(size>end-start)return false;
    if(!memcmp(chunk.id_.data(),"fmt ",4)) {
      FormatChunk fmt{};
      if(size<qint64(sizeof fmt) || file_.read(reinterpret_cast<char*>(&fmt),sizeof fmt)!=sizeof fmt)return false;
      auto encoding=read16(fmt.audio_format);auto channels=read16(fmt.num_channels);
      auto rate=read32(fmt.sample_rate);auto bits=read16(fmt.bits_per_sample);
      if((encoding!=0 && encoding!=1) || channels<1 || channels>32 || rate<1 || rate>768000 ||
         (bits!=8 && bits!=16 && bits!=24 && bits!=32) || read16(fmt.block_align)!=channels*(bits/8))return false;
      format_.setByteOrder(be?QAudioFormat::BigEndian:QAudioFormat::LittleEndian);
      format_.setChannelCount(channels);format_.setCodec("audio/pcm");format_.setSampleRate(rate);
      format_.setSampleSize(bits);format_.setSampleType(bits==8?QAudioFormat::UnSignedInt:QAudioFormat::SignedInt);
      haveFormat=true;
    } else if(!memcmp(chunk.id_.data(),"data",4)) {
      if(data_size_>=0)return false;
      data_size_=size;header_length_=start;
    } else if(!memcmp(chunk.id_.data(),"bext",4)) {
      metadata+=size;
      if(size<qint64(sizeof(BroadcastAudioExtension)) || metadata>16*1024*1024)return false;
      parsedBext=file_.read(size);if(parsedBext.size()!=size)return false;
    } else if(!memcmp(chunk.id_.data(),"LIST",4)) {
      if(size<4)return false;
      if(file_.read(4)=="INFO") {
        qint64 limit=start+size;
        while(file_.pos()<limit) {
          Desc info;
          if(limit-file_.pos()<qint64(sizeof info) || file_.read(&info,sizeof info)!=sizeof info)return false;
          qint64 count=read32(info.size_),at=file_.pos();metadata+=count;
          if(count>limit-at || metadata>16*1024*1024)return false;
          auto value=file_.read(count);if(value.size()!=count)return false;
          parsedInfo[info.id_]=value;
          if(!file_.seek(qMin(limit,at+count+(count&1))))return false;
        }
      }
    }
    // RIFF padding is external to the chunk length. Permit a missing final pad.
    if(!file_.seek(qMin(end,start+size+(size&1))))return false;
  }
  if(!haveFormat || data_size_<0 || !file_.seek(header_length_))return false;
  if(!parsedBext.isEmpty())convertBext(reinterpret_cast<BroadcastAudioExtension*>(parsedBext.data()),be);
  bext_=parsedBext;info_dictionary_=parsedInfo;header_dirty_=false;return true;
}

bool BWFFile::impl::write_header (QAudioFormat format)
{
  data_size_ = -1;
  if ("audio/pcm" != format.codec ()) return false;
  if (!(file_.openMode () & WriteOnly)) return false;
  if (!file_.seek (0)) return false;
  header_length_ = 0;
  bool be {QAudioFormat::BigEndian == format_.byteOrder ()};
  Desc desc {be ? "RIFX" : "RIFF"};
  if (file_.write (&desc, sizeof desc) != sizeof desc) return false;
  header_dirty_ = true;
  if (file_.write ("WAVE", 4) != 4) return false;

  FormatChunk fmt;
  if (be)
    {
      fmt.audio_format = qToBigEndian<quint16> (1); // PCM
      fmt.num_channels = qToBigEndian<quint16> (format.channelCount ());
      fmt.sample_rate = qToBigEndian<quint32> (format.sampleRate ());
      fmt.byte_rate = qToBigEndian<quint32> (format.bytesForDuration (1000000));
      fmt.block_align = qToBigEndian<quint16> (format.bytesPerFrame ());
      fmt.bits_per_sample = qToBigEndian<quint16> (format.sampleSize ());
      desc.set ("fmt", qToBigEndian<quint32> (sizeof fmt));
    }
  else
    {
      fmt.audio_format = qToLittleEndian<quint16> (1); // PCM
      fmt.num_channels = qToLittleEndian<quint16> (format.channelCount ());
      fmt.sample_rate = qToLittleEndian<quint32> (format.sampleRate ());
      fmt.byte_rate = qToLittleEndian<quint32> (format.bytesForDuration (1000000));
      fmt.block_align = qToLittleEndian<quint16> (format.bytesPerFrame ());
      fmt.bits_per_sample = qToLittleEndian<quint16> (format.sampleSize ());
      desc.set ("fmt", qToLittleEndian<quint32> (sizeof fmt));
    }
  if (file_.write (&desc, sizeof desc) != sizeof desc) return false;
  if (file_.write (reinterpret_cast<char const *> (&fmt), sizeof fmt) != sizeof fmt) return false;

  desc.set ("data");
  if (file_.write (&desc, sizeof desc) != sizeof desc) return false;
  header_length_ = file_.pos ();
  return true;
}

bool BWFFile::impl::update_header ()
{
  if (header_length_ < 0 || !(file_.openMode () & WriteOnly)) return false;
  auto position = file_.pos ();
  bool be {QAudioFormat::BigEndian == format_.byteOrder ()};
  Desc desc;
  auto size = data_size_ < 0 ? file_.size () - header_length_ : data_size_;
  if (!file_.seek (header_length_ - sizeof desc)) return false;
  desc.set ("data", be ? qToBigEndian<quint32> (size) : qToLittleEndian<quint32> (size));
  if (file_.write (&desc, sizeof desc) != sizeof desc) return false;

  if (!bext_.isEmpty ())
    {
      if (!file_.seek (file_.size ())) return false;
      auto size = bext_.size ();
      desc.set ("bext", be ? qToBigEndian<quint32> (size) : qToLittleEndian<quint32> (size));
      if ((file_.size () % 2) && file_.write ("\0", 1) != 1) return false;
      if (file_.write (&desc, sizeof desc) != sizeof desc) return false;
      auto wire = bext_;
      convertBext(reinterpret_cast<BroadcastAudioExtension*>(wire.data()),be);
      if (file_.write (wire) != size) return false;
    }

  if (info_dictionary_.size ())
    {
      if (!file_.seek (file_.size ())) return false;
      desc.set ("LIST");
      if ((file_.size () % 2) && file_.write ("\0", 1) != 1) return false;
      if (file_.write (&desc, sizeof desc) != sizeof desc) return false;
      auto list_start = file_.pos ();
      if (file_.write ("INFO", 4) != 4) return false;
      for (auto iter = info_dictionary_.constBegin ()
             ; iter != info_dictionary_.constEnd (); ++iter)
        {
          auto value = iter.value ();
          auto len = value.size () + 1; // include terminating null char
          desc.set (iter.key ().data (), be ? qToBigEndian<quint32> (len) : qToLittleEndian<quint32> (len));
          if ((file_.size () % 2) && file_.write ("\0", 1) != 1) return false;
          if (file_.write (&desc, sizeof desc) != sizeof desc) return false;
          if (file_.write (value.constData (), len) != len) return false;
        }
      auto size = file_.pos () - list_start;
      if (!file_.seek (list_start - sizeof desc)) return false;
      desc.set ("LIST", be ? qToBigEndian<quint32> (size) : qToLittleEndian<quint32> (size));
      if (file_.write (&desc, sizeof desc) != sizeof desc) return false;
    }

  size = file_.size () - sizeof desc;
  if ((file_.size () % 2) && file_.seek (file_.size ()) && file_.write ("\0", 1) != 1) return false;
  if (!file_.seek (0)) return false;
  desc.set (be ? "RIFX" : "RIFF", be ? qToBigEndian<quint32> (size) : qToLittleEndian<quint32> (size));
  if (file_.write (&desc, sizeof desc) != sizeof desc) return false;
  return file_.seek (position);
}

//
// BWFFile implementation
//
BWFFile::BWFFile (QAudioFormat const& format, QObject * parent)
  : QIODevice {parent}
  , m_ {format}
{
}

BWFFile::BWFFile (QAudioFormat const& format, QString const& name, QObject * parent)
  : QIODevice {parent}
  , m_ {format, name}
{
}

BWFFile::BWFFile (QAudioFormat const& format, QString const& name
                  , InfoDictionary const& dictionary, QObject * parent)
  : QIODevice {parent}
  , m_ {format, name, dictionary}
{
}

BWFFile::~BWFFile ()
{
  if (isOpen ()) close ();
}

bool BWFFile::open (OpenMode mode)
{
  bool result {false};
  if (!(mode & WriteOnly))
    {
      result = m_->file_.open (mode & ~Text) && m_->read_header ();
    }
  else
    {
      if ((result = m_->file_.open (mode & ~Text)))
        {
          if (!(result = m_->read_header ()
                || m_->write_header (m_->format_)
                || m_->file_.resize (m_->header_length_)))
            {
              m_->file_.close ();
              return false;
            }
        }
    }
  if (result && (result = QIODevice::open (mode | Unbuffered)))
    {
      return m_->initialize (this, mode);
    }
  if (!result) close ();
  return result;
}

bool BWFFile::open(FILE * fh, OpenMode mode, FileHandleFlags flags)
{
  bool result {false};
  if (!(mode & ReadOnly)) return result;
  if (!(mode & WriteOnly))
    {
      result = m_->file_.open (fh, mode & ~Text, flags) && m_->read_header ();
    }
  else
    {
      if ((result = m_->file_.open (fh, mode & ~Text, flags)))
        {
          if (!(result = m_->read_header ()
                || m_->write_header (m_->format_)
                || m_->file_.resize (m_->header_length_)))
            {
              m_->file_.close ();
              return false;
            }
        }
    }
  if (result && (result = QIODevice::open (mode | Unbuffered)))
    {
      return m_->initialize (this, mode);
    }
  if (!result) close ();
  return result;
}

bool BWFFile::open (int fd, OpenMode mode, FileHandleFlags flags)
{
  bool result {false};
  if (!(mode & ReadOnly)) return result;
  if (!(mode & WriteOnly))
    {
      result = m_->file_.open (fd, mode & ~Text, flags) && m_->read_header ();
    }
  else
    {
      if ((result = m_->file_.open (fd, mode & ~Text, flags)))
        {
          if (!(result = m_->read_header ()
                || m_->write_header (m_->format_)
                || m_->file_.resize (m_->header_length_)))
            {
              m_->file_.close ();
              return false;
            }
        }
    }
  if (result && (result = QIODevice::open (mode | Unbuffered)))
    {
      return m_->initialize (this, mode);
    }
  if (!result) close ();
  return result;
}

QAudioFormat const& BWFFile::format () const {return m_->format_;}

auto BWFFile::list_info () -> InfoDictionary&
{
  m_->header_dirty_ = true;
  return m_->info_dictionary_;
}


// Broadcast Audio Extension fields
auto BWFFile::bext_version () const -> BextVersion
{
  return static_cast<BextVersion> (m_->bext () ? m_->bext ()->version_ : 0);
}

void BWFFile::bext_version (BextVersion version)
{
  m_->header_dirty_ = true;
  m_->bext ()->version_ = static_cast<quint16> (version);
}

QByteArray BWFFile::bext_description () const
{
  if (!m_->bext ()) return {};
  return boundedText(m_->bext ()->description_);
}

void BWFFile::bext_description (QByteArray const& description)
{
  m_->header_dirty_ = true;
  std::strncpy (m_->bext ()->description_, description.constData (), sizeof (BroadcastAudioExtension::description_));
}

QByteArray BWFFile::bext_originator () const
{
  if (!m_->bext ()) return {};
  return boundedText(m_->bext ()->originator_);
}

void BWFFile::bext_originator (QByteArray const& originator)
{
  m_->header_dirty_ = true;
  std::strncpy (m_->bext ()->originator_, originator.constData (), sizeof (BroadcastAudioExtension::originator_));
}

QByteArray BWFFile::bext_originator_reference () const
{
  if (!m_->bext ()) return {};
  return boundedText(m_->bext ()->originator_reference_);
}

void BWFFile::bext_originator_reference (QByteArray const& reference)
{
  m_->header_dirty_ = true;
  std::strncpy (m_->bext ()->originator_reference_, reference.constData (), sizeof (BroadcastAudioExtension::originator_reference_));
}

QDateTime BWFFile::bext_origination_date_time () const
{
  if (!m_->bext ()) return {};
  return {QDate::fromString (QString::fromLatin1(boundedText(m_->bext ()->origination_date_)), "yyyy-MM-dd"),
      QTime::fromString (QString::fromLatin1(boundedText(m_->bext ()->origination_time_)), "hh-mm-ss"), Qt::UTC};
}

void BWFFile::bext_origination_date_time (QDateTime const& dt)
{
  m_->header_dirty_ = true;
  std::strncpy (m_->bext ()->origination_date_,
                dt.toUTC().date ().toString ("yyyy-MM-dd").toLocal8Bit ().constData (),
                sizeof (BroadcastAudioExtension::origination_date_));
  std::strncpy (m_->bext ()->origination_time_,
                dt.toUTC().time ().toString ("hh-mm-ss").toLocal8Bit ().constData (),
                sizeof (BroadcastAudioExtension::origination_time_));
}

quint64 BWFFile::bext_time_reference () const
{
  if (!m_->bext ()) return 0;
  return (quint64 (m_->bext ()->time_reference_high_) << 32) + m_->bext ()->time_reference_low_;
}

void BWFFile::bext_time_reference (quint64 time_code)
{
  m_->header_dirty_ = true;
  m_->bext ()->time_reference_low_ = time_code & 0x00000000ffffffffll;
  m_->bext ()->time_reference_high_ = time_code >> 32;
}

auto BWFFile::bext_umid () const -> UMID
{
  UMID umid {{'\0'}};
  if (m_->bext ())
    {
      umid = m_->bext ()->umid_;
    }
  return umid;
}

void BWFFile::bext_umid (UMID const& umid)
{
  m_->header_dirty_ = true;
  m_->bext ()->umid_ = umid;
}

quint16 BWFFile::bext_loudness_value () const
{
  if (!m_->bext ()) return 0;
  return m_->bext ()->loudness_value_;
}

void BWFFile::bext_loudness_value (quint16 value)
{
  m_->header_dirty_ = true;
  m_->bext ()->loudness_value_ = value;
}

quint16 BWFFile::bext_loudness_range () const
{
  if (!m_->bext ()) return 0;
  return m_->bext ()->loudness_range_;
}

void BWFFile::bext_loudness_range (quint16 range)
{
  m_->header_dirty_ = true;
  m_->bext ()->loudness_range_ = range;
}

quint16 BWFFile::bext_max_true_peak_level () const
{
  if (!m_->bext ()) return 0;
  return m_->bext ()->max_true_peak_level_;
}

void BWFFile::bext_max_true_peak_level (quint16 level)
{
  m_->header_dirty_ = true;
  m_->bext ()->max_true_peak_level_ = level;
}

quint16 BWFFile::bext_max_momentary_loudness () const
{
  if (!m_->bext ()) return 0;
  return m_->bext ()->max_momentary_loudness_;
}

void BWFFile::bext_max_momentary_loudness (quint16 loudness)
{
  m_->header_dirty_ = true;
  m_->bext ()->max_momentary_loudness_ = loudness;
}

quint16 BWFFile::bext_max_short_term_loudness () const
{
  if (!m_->bext ()) return 0;
  return m_->bext ()->max_short_term_loudness_;
}

void BWFFile::bext_max_short_term_loudness (quint16 loudness)
{
  m_->header_dirty_ = true;
  m_->bext ()->max_short_term_loudness_ = loudness;
}

QByteArray BWFFile::bext_coding_history () const
{
  if (size_t (m_->bext_.size ()) <= sizeof (BroadcastAudioExtension)) return {};
  return QByteArray::fromRawData (m_->bext ()->coding_history_,
                                  m_->bext_.size () - sizeof (BroadcastAudioExtension));
}

void BWFFile::bext_coding_history (QByteArray const& text)
{
  m_->header_dirty_ = true;
  m_->bext ();                  // ensure we have a correctly
                                // initialized m_->bext_
  auto length = std::min (strlen (text.constData ()), size_t (text.size ()));
  m_->bext_.resize (sizeof (BroadcastAudioExtension) + length);
  std::strncpy (m_->bext ()->coding_history_, text.constData (), length);
}


bool BWFFile::reset ()
{
  if (m_->file_.isOpen ())
    {
      if(!(m_->file_.openMode() & WriteOnly)) return QIODevice::reset();
      m_->info_dictionary_.clear ();
      m_->bext_.clear ();
      auto size = m_->data_size_ < 0 ? m_->file_.size () - m_->header_length_ : m_->data_size_;
      m_->data_size_ = size;
      if (m_->header_length_ > qint64 (3 * sizeof (Desc) + 4 + sizeof (FormatChunk)))
        {
          // we need to move the data down
          auto old_pos = m_->header_length_;
          m_->write_header (m_->format_);
          auto new_pos = m_->header_length_;
          QByteArray buffer;
          while (size)
            {
              if(!m_->file_.seek (old_pos))return false;
              buffer = m_->file_.read (std::min (size, qint64 (32768)));
              if(buffer.isEmpty() || !m_->file_.seek (new_pos) || m_->file_.write(buffer)!=buffer.size())return false;
              new_pos += buffer.size ();
              old_pos += buffer.size ();
              size -= buffer.size ();
            }
        }
      if(!m_->file_.resize (m_->header_length_ + m_->data_size_))return false;
      m_->header_dirty_ = true;
    }
  return QIODevice::reset ();
}

qint64 BWFFile::size () const
{
  return m_->data_size_ < 0 ? m_->file_.size () - m_->header_length_ : m_->data_size_;
}

bool BWFFile::isSequential () const
{
  return m_->file_.isSequential ();
}

void BWFFile::close ()
{
  QIODevice::close ();
  if (m_->header_dirty_ || m_->data_size_ < 0) m_->update_header ();
  m_->file_.close ();
}

bool BWFFile::seek (qint64 pos)
{
  if (pos < 0) return false;
  QIODevice::seek (pos);
  return m_->file_.seek (pos + m_->header_length_);
}

qint64 BWFFile::readData (char * data, qint64 max_size)
{
  auto remaining=qMax(qint64(0),size()-(m_->file_.pos()-m_->header_length_));
  return m_->file_.read (data, qMin(max_size,remaining));
}

qint64 BWFFile::writeData (char const* data, qint64 max_size)
{
  auto bytes = m_->file_.write (data, max_size);
  if (bytes > 0 && atEnd ())
    {
      m_->header_dirty_ = true;
      m_->data_size_ = -1;
    }
  return bytes;
}

// forward to QFile
bool BWFFile::copy (QString const& new_name)
{
  close ();
  return m_->file_.copy (new_name);
}

bool BWFFile::exists () const {return m_->file_.exists ();}

bool BWFFile::link (QString const& link_name) {return m_->file_.link (link_name);}

bool BWFFile::remove ()
{
  close ();
  return m_->file_.remove ();
}

bool BWFFile::rename (QString const& new_name)
{
  close ();
  return m_->file_.rename (new_name);
}

void BWFFile::setFileName (QString const& name) {m_->file_.setFileName (name);}

QString BWFFile::symLinkTarget () const {return m_->file_.symLinkTarget ();}

QString BWFFile::fileName () const {return m_->file_.fileName ();}

auto BWFFile::permissions () const -> Permissions {return m_->file_.permissions ();}

bool BWFFile::resize (qint64 new_size)
{
  if(new_size<0 || m_->header_length_<0)return false;
  auto size = m_->file_.size ();
  if (pos () > new_size) seek (new_size);
  auto result = m_->file_.resize (m_->header_length_ + new_size);
  if (m_->data_size_ >= 0)
    {
      // set any fresh bytes to zero
      auto end_of_data = m_->header_length_ + m_->data_size_;
      auto length = std::min (size - end_of_data, m_->file_.size () - end_of_data);
      if (length > 0)
        {
          auto position = m_->file_.pos ();
          m_->file_.seek (m_->header_length_ + m_->data_size_);
          m_->file_.write (QByteArray {int (length), '\0'});
          m_->file_.seek (position);
        }
      m_->data_size_ = -1;
    }
  m_->header_dirty_ = true;
  return result;
}

bool BWFFile::setPermissions (Permissions permissions) {return m_->file_.setPermissions (permissions);}

auto BWFFile::error () const -> FileError {return m_->file_.error ();}

bool BWFFile::flush () {return m_->file_.flush ();}

int BWFFile::handle () const {return m_->file_.handle ();}

uchar * BWFFile::map (qint64 offset, qint64 size, MemoryMapFlags flags)
{
  if(offset<0 || size<0 || offset>this->size() || size>this->size()-offset)return nullptr;
  return m_->file_.map (offset + m_->header_length_, size, flags);
}

bool BWFFile::unmap (uchar * address) {return m_->file_.unmap (address);}

void BWFFile::unsetError () {m_->file_.unsetError ();}
