#include "getfile.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
#include <QRandomGenerator>
#include <random>
#endif
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#ifdef WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <unistd.h>
#include <err.h>
#endif

#include "commons.h"

extern dec_data dec_data;

void getfile(QString fname, double ntrperiod)
{
  // QFile preserves Unicode Windows paths; all exits close the file.
  dec_data.params.newdat = 0;
  dec_data.params.kin = 0;
  dec_data.params.nutc = 0;
  int npts = int(qBound(0.0, ntrperiod, 120.0) * 12000);
  memset(dec_data.d2, 0, sizeof(dec_data.d2));
  QString base = QFileInfo(fname).completeBaseName();
  QString stamp = base.mid(base.lastIndexOf('_') + 1);
  if (stamp.size() == 6) dec_data.params.nutc = stamp.toInt();
  else if (stamp.size() == 4) dec_data.params.nutc = 100 * stamp.toInt();
  QFile file(fname);
  if (!file.open(QIODevice::ReadOnly)) return;
  QByteArray header = file.read(12);
  if (header.size() != 12 || header.left(4) != "RIFF" || header.mid(8, 4) != "WAVE") return;
  bool haveFormat = false;
  quint32 rate = 0;
  quint16 bits = 0;
  while (file.bytesAvailable() >= 8) {
    QByteArray chunk = file.read(8);
    quint32 size = qFromLittleEndian<quint32>(reinterpret_cast<uchar const*>(chunk.constData() + 4));
    if (size > static_cast<quint64>(file.bytesAvailable())) return;
    qint64 next = file.pos() + size + (size & 1);
    if (chunk.left(4) == "fmt ") {
      if (size < 16) return;
      QByteArray fmt = file.read(16);
      auto u16 = [&fmt](int i) { return qFromLittleEndian<quint16>(reinterpret_cast<uchar const*>(fmt.constData() + i)); };
      rate = qFromLittleEndian<quint32>(reinterpret_cast<uchar const*>(fmt.constData() + 4));
      bits = u16(14);
      if (u16(0) != 1 || u16(2) != 1 || (rate != 12000 && rate != 11025)
          || (bits != 16 && !(rate == 11025 && bits == 8)) || u16(12) != bits / 8) return;
      haveFormat = true;
    } else if (chunk.left(4) == "data") {
      if (!haveFormat) return;
      qint64 bytes = qMin<qint64>(size, 2LL * npts);
      QByteArray samples = file.read(bytes);
      int n = samples.size() / 2;
      memcpy(dec_data.d2, samples.constData(), n * 2);
      if (rate == 11025) {
        short sampleBits = bits;
        wav12_(dec_data.d2, dec_data.d2, &n, &sampleBits);
      }
      dec_data.params.kin = qMin(n, npts);
      dec_data.params.newdat = n > 0;
      return;
    }
    if (!file.seek(next)) return;
  }
}

void savewav(QString fname, int ntrperiod)
{
  struct {
    char ariff[4];         //ChunkID:    "RIFF"
    int nchunk;            //ChunkSize: 36+SubChunk2Size
    char awave[4];         //Format: "WAVE"
    char afmt[4];          //Subchunk1ID: "fmt "
    int lenfmt;            //Subchunk1Size: 16
    short int nfmt2;       //AudioFormat: 1
    short int nchan2;      //NumChannels: 1
    int nsamrate;          //SampleRate: 12000
    int nbytesec;          //ByteRate: SampleRate*NumChannels*BitsPerSample/8
    short int nbytesam2;   //BlockAlign: NumChannels*BitsPerSample/8
    short int nbitsam2;    //BitsPerSample: 16
    char adata[4];         //Subchunk2ID: "data"
    int ndata;             //Subchunk2Size: numSamples*NumChannels*BitsPerSample/8
  } hdr;

  int npts=qBound(0, ntrperiod, 120)*12000;
//  qint16* buf=(qint16*)malloc(2*npts);
  QFile file(fname);
  if (file.open(QIODevice::WriteOnly)) {
// Write a WAV header
    hdr.ariff[0]='R';
    hdr.ariff[1]='I';
    hdr.ariff[2]='F';
    hdr.ariff[3]='F';
    hdr.nchunk=36 + 2*npts;
    hdr.awave[0]='W';
    hdr.awave[1]='A';
    hdr.awave[2]='V';
    hdr.awave[3]='E';
    hdr.afmt[0]='f';
    hdr.afmt[1]='m';
    hdr.afmt[2]='t';
    hdr.afmt[3]=' ';
    hdr.lenfmt=16;
    hdr.nfmt2=1;
    hdr.nchan2=1;
    hdr.nsamrate=12000;
    hdr.nbytesec=2*12000;
    hdr.nbytesam2=2;
    hdr.nbitsam2=16;
    hdr.adata[0]='d';
    hdr.adata[1]='a';
    hdr.adata[2]='t';
    hdr.adata[3]='a';
    hdr.ndata=2*npts;

    file.write(reinterpret_cast<char const*>(&hdr), sizeof(hdr));
//    memcpy(dec_data.d2,buf,2*npts);
//    fwrite(buf,2,npts,fp);
    file.write(reinterpret_cast<char const*>(dec_data.d2), 2LL * npts);
  }
//  free(buf);
}

//#define	MAX_RANDOM	0x7fffffff
/* Generate gaussian random float with mean=0 and std_dev=1 */
float gran()
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
  static std::normal_distribution<float> d;
  return d (*QRandomGenerator::global ());
#else
  float fac,rsq,v1,v2;
  static float gset;
  static int iset;

  if(iset){
    /* Already got one */
    iset = 0;
    return gset;
  }
  /* Generate two evenly distributed numbers between -1 and +1
   * that are inside the unit circle
   */
  do {
    v1 = 2.0 * (float)qrand() / RAND_MAX - 1;
    v2 = 2.0 * (float)qrand() / RAND_MAX - 1;
    rsq = v1*v1 + v2*v2;
  } while(rsq >= 1.0 || rsq == 0.0);
  fac = sqrt(-2.0*log(rsq)/rsq);
  gset = v1*fac;
  iset++;
  return v2*fac;
#endif
}

int ptt(int nport, int ntx, int* iptt, int* nopen)
{
#ifdef WIN32
  static HANDLE hFile;
  char s[32];
  int i3=1,i4=1,i5=1,i6=1,i9=1,i00=1;  //Defs to silence compiler warning

  if(nport==0) {
    *iptt=ntx;
    return 0;
  }

  if(ntx && (!(*nopen))) {
    snprintf(s,sizeof(s),"\\\\.\\COM%d",nport);
    hFile=CreateFile(TEXT(s),GENERIC_WRITE,0,NULL,OPEN_EXISTING,
                     FILE_ATTRIBUTE_NORMAL,NULL);
    if(hFile==INVALID_HANDLE_VALUE) {
      QString t;
      t = t.asprintf("Cannot open COM port %d for PTT\n",nport);
      return 1;
    }
    *nopen=1;
  }

  if(ntx && *nopen) {
    i3=EscapeCommFunction(hFile,SETRTS);
    i5=EscapeCommFunction(hFile,SETDTR);
    *iptt=1;
  }

  else {
    i4=EscapeCommFunction(hFile,CLRRTS);
    i6=EscapeCommFunction(hFile,CLRDTR);
    i9=EscapeCommFunction(hFile,CLRBREAK);
    i00=CloseHandle(hFile);
    *iptt=0;
    *nopen=0;
  }
  if((i3+i4+i5+i6+i9+i00)==-999) return 1;    //Silence compiler warning
  return 0;
#else
  int control=TIOCM_RTS | TIOCM_DTR;
//  int control = TIOCM_RTS;
  static int fd;

  if(*nopen==0) {
    fd=open("/dev/ttyUSB0",O_RDWR | O_NONBLOCK);
    if(fd<0) {
      return -1;
    }
    *nopen=1;
  }

  if(ntx) {
    ioctl(fd, TIOCMBIS, &control);
    *iptt=1;
    *nopen=1;
  } else {
    ioctl(fd, TIOCMBIC, &control);
    close(fd);
    *iptt=0;
    *nopen=0;
  }
  return 0;
#endif
  if((nport+ntx+(*iptt)==-99999)) *nopen=0;   //Silence compiler warning
  return 0;
}
