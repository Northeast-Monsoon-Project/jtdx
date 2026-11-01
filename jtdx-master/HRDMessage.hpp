// Ported from WSJT-X v3.0.2, ccdfaf3c1c109010d15399674ce278167cfde848 (GPL-3.0).
#ifndef HRD_MESSAGE_HPP__
#define HRD_MESSAGE_HPP__

#include <QByteArray>
#include <QString>

namespace HRDMessage
{
  int header_size ();
  int max_message_size ();

  struct ParseResult
  {
    bool valid;
    bool complete;
    int required_size;
    QString payload;
    QString error;
  };

  QByteArray make (QString const& payload);
  ParseResult parse (QByteArray const& message);
}

#endif
