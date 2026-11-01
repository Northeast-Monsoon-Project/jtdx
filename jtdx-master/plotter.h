// -*- Mode: C++ -*-
///////////////////////////////////////////////////////////////////////////
// Some code in this file and accompanying files is based on work by
// Moe Wheatley, AE4Y, released under the "Simplified BSD License".
// For more details see the accompanying file LICENSE_WHEATLEY.TXT
///////////////////////////////////////////////////////////////////////////

#ifndef PLOTTER_H
#define PLOTTER_H

#ifdef QT5
#include <QtWidgets>
#else
#include <QtGui>
#endif
#include <QFrame>
#include <QImage>
#include <QVector>
#include <cstring>
#include "JTDXDateTime.h"
#define VERT_DIVS 7	//specify grid screen divisions
#define HORZ_DIVS 20

class CPlotter : public QFrame
{
  Q_OBJECT;

public:
  explicit CPlotter(QWidget *parent = 0);
  ~CPlotter();

  JTDXDateTime * m_jtdxtime;
  QSize minimumSizeHint() const;
  QSize sizeHint() const;
  bool m_bScaleOK = false;

  void draw(float swide[], bool bScroll);		//Update the waterfall
  void SetRunningState(bool running);
  void setPlotZero(int plotZero);
  int  plotZero();
  void setPlotGain(int plotGain);
  int  plotGain();
  int  plot2dGain();
  void setPlot2dGain(int n);
  int  plot2dZero();
  void setPlot2dZero(int plot2dZero);
  void setStartFreq(int f);
  int startFreq();
  int  plotWidth();
  void UpdateOverlay();
  void setDataFromDisk(bool b);
  void setRxRange(int fMin);
  void setBinsPerPixel(int n);
  int  binsPerPixel();
  void setWaterfallAvg(int n);
  void setRxFreq(int n);
  void DrawOverlay();
  int  rxFreq();
  void setFsample(int n);
  void setNsps(double trperiod, int nsps);
  void setTxFreq(int n);
  void setMode(QString mode);
  void setTopJT65(int n);
  void setModeTx(QString modeTx);
  void SetPercent2DScreen(int percent);
  int  Fmax();
  void setDialFreq(double d);
  void setCurrent(bool b) {m_bCurrent = b;}
  bool current() const {return m_bCurrent;}
  void setCumulative(bool b) {m_bCumulative = b;}
  bool cumulative() const {return m_bCumulative;}
  void setBreadth(qint32 w) {m_w = w;}
  qint32 breadth() const {return m_w;}
  float fSpan() const {return m_fSpan;}
  void setLockTxFreq(bool b) {m_lockTxFreq = b;}
  void setFilter(bool b);
  void setHoundFilter(bool b);
  void setDarkStyle(bool b);
  void setColours(QVector<QColor> const& cl);
  void setTimestamp(int n);
  void setScale(bool b);
  void setBars(bool b);
  void showFreq(bool b);
  void setFlatten(bool b);
  void setTol(int n);
  void setRxBand(QString band);

signals:
  void freezeDecode1(int n);
  void setFreq1(int rxFreq, int txFreq);
  void setRxFreq1(int rxFreq);
  void filter_on1();
  void toggle_filter1();

protected:
  //re-implemented widget event handlers
  void paintEvent(QPaintEvent *event);
  void resizeEvent(QResizeEvent* event);
  virtual void keyPressEvent( QKeyEvent *e );

private:

  void MakeFrequencyStrs();
  int XfromFreq(float f);
  float FreqfromX(int x);

  bool m_bCurrent = false;
  bool m_bCumulative = false;
  bool m_lockTxFreq = false;
  bool m_filter = false;
  bool m_houndFilter = false;
  bool m_useDarkStyle = false;
  bool m_bars = false;
  bool m_freq = false;
  float m_fSpan = 0;

  qint32 m_plotZero = 0;
  qint32 m_plotGain = 0;
  qint32 m_plot2dGain = 0;
  qint32 m_plot2dZero = 0;
  qint32 m_binsPerPixel = 1;
  qint32 m_waterfallAvg = 1;
  qint32 m_w = 0;
  qint32 m_Flatten = 0;
  qint32 m_nSubMode = 0;
  qint32 m_topJT65 = 0;
  qint32 m_timestamp = 0;

  QPixmap m_DialOverlayPixmap;
  QPixmap m_HoverOverlayPixmap;
  QPixmap m_WaterfallPixmap;
  QPixmap m_2DPixmap;
  QPixmap m_ScalePixmap;
  QPixmap m_OverlayPixmap;
  QPoint m_pos;
  QSize   m_Size;
  QString m_Str;
  QString m_HDivText[483];
  QString m_mode;
  QString m_modeTx;
  QString m_rxBand;

  bool m_Scale = false;
  bool m_Running = false;
  bool m_paintEventBusy = false;
  bool m_dataFromDisk = false;

  double m_fftBinWidth = 0;
  double m_dialFreq = 0;
  double m_xOffset = 0;
  double m_TRperiod = 1;

  float   m_sum[2048];

  qint32 m_dBStepSize = 0;
  qint32 m_FreqUnits = 0;
  qint32 m_hdivs = 0;
  qint32 m_line = 0;
  qint32 m_fSample = 0;
  qint32 m_xClick = 0;
  qint32 m_freqPerDiv = 1;
  qint32 m_nsps = 0;
  qint32 m_Percent2DScreen = 0;
  qint32 m_Percent2DScreen0 = 0;
  qint32 m_h = 0;
  qint32 m_h1 = 0;
  qint32 m_h2 = 0;
  qint32 m_rxFreq = 0;
  qint32 m_txFreq = 0;
  qint32 m_fMin = 0;
  qint32 m_fMax = 0;
  qint32 m_startFreq = 0;
  qint32 m_tol = 0;
  qint32 m_lastMouseX = 0;
  qint32 m_lastPaintedX = 0;
  char    m_sutc[6]{};

private slots:
  void leaveEvent(QEvent *event);
  void mouseMoveEvent(QMouseEvent * event);
  void mousePressEvent(QMouseEvent *event);
  void mouseDoubleClickEvent(QMouseEvent *event);
};

extern QVector<QColor> g_ColorTbl;

#endif // PLOTTER_H
