#include "AudioBuffer.hpp"
#include "Ft8ThreadPolicy.hpp"
#include "RxAudioBufferPolicy.hpp"
#include "DebugFile.hpp"
#include "getfile.h"
#include "OperatorLog.hpp"
#include <QLockFile>
#include "StationProfiles.hpp"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
//-------------------------------------------------------- MainWindow

#include "mainwindow.h"
#include "JttyController.hpp"
#include <cinttypes>
#include <limits>
#include <fftw3.h>
#include <thread>

#include <QProcessEnvironment>
#include <QLineEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QDesktopServices>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QtConcurrent/QtConcurrentRun>
#include <QProgressDialog>
#include <QHostInfo>
#include <QVector>
#include <QCursor>
#include <QToolTip>
#include <QButtonGroup>
#include <QUdpSocket>
#include <QtMath>
#if QT_VERSION >= QT_VERSION_CHECK (5, 15, 0)
#include <QRandomGenerator>
#endif

#include "revision_utils.hpp"
#include "qt_helpers.hpp"
#include "soundout.h"
#include "soundin.h"
#include "Modulator.hpp"
#include "Detector.hpp"
#include "plotter.h"
#include "about.h"
#include "widegraph.h"
#include "sleep.h"
#include "logqso.h"
#include "decodedtext.h"
#include "Radio.hpp"
#include "Bands.hpp"
#include "TransceiverFactory.hpp"
#include "FrequencyList.hpp"
#include "StationList.hpp"
#include "LiveFrequencyValidator.hpp"
#include "MessageClient.hpp"
#include "JtAlertCompatibility.hpp"
#include "wsprnet.h"
#include "eqsl.h"
#include "signalmeter.h"
#include "HelpTextWindow.hpp"
#include "SampleDownloader.hpp"
#include "Audio/BWFFile.hpp"

#include "ui_mainwindow.h"
#include "moc_mainwindow.cpp"



extern "C" {
  void genft2_(char*,int*,char*,char*,int*,int,int);
  void gen_ft2wave_(int*,int*,int*,float*,float*,float*,float*,int*,int*);

  //----------------------------------------------------- C and Fortran routines
  void symspec_(struct dec_data *, int* k, int* ntrperiod, int* nsps,
                float* px, float s[], float* df3, int* nhsym, int* npts8);

  void four2a_(_Complex float *, int * nfft, int * ndim, int * isign, int * iform, fortran_charlen_t);
				
  void genft8_(char* msg, int* i3, int* n3, int* ntxhash, char* msgsent, char ft8msgbits[], int itone[], fortran_charlen_t, fortran_charlen_t);

  void genft4_(char* msg, int* ichk, int* ntxhash, char* msgsent, char ft4msgbits[], int itone[], fortran_charlen_t, fortran_charlen_t);

  void gen_ft8wave_(int itone[], int* nsym, int* nsps, float* bt, float* fsample, float* f0, float xjunk[], float wave[], int* icmplx, int* nwave);

  void gen_ft4wave_(int itone[], int* nsym, int* nsps, float* fsample, float* f0, float xjunk[], float wave[], int* icmplx, int* nwave);

  void gen9_(char* msg, int* ichk, char* msgsent, int itone[],
               int* itext, fortran_charlen_t, fortran_charlen_t);

  void gen10_(char* msg, int* ichk, char* msgsent, int itone[],
               int* itext, fortran_charlen_t, fortran_charlen_t);

  void gen65_(char* msg, int* ichk, char* msgsent, int itone[],
              int* itext, fortran_charlen_t, fortran_charlen_t);


  void genwspr_(char* msg, char* msgsent, int itone[], fortran_charlen_t, fortran_charlen_t);

  void azdist_(char* MyGrid, char* HisGrid, double* utch, int* nAz, int* nEl,
               int* nDmiles, int* nDkm, int* nHotAz, int* nHotABetter,
               fortran_charlen_t, fortran_charlen_t);

  void morse_(char* msg, int* icw, int* ncw, fortran_charlen_t);

  int ptt_(int nport, int ntx, int* iptt, int* nopen);

  void wspr_downsample_(short int d2[], int* k);
  int savec2_(char* fname, int* TR_seconds, double* dial_freq, fortran_charlen_t);

  void wav12_(short d2[], short d1[], int* nbytes, short* nbitsam2);

  void foxgen_();
}

int volatile itone[NUM_WSPR_SYMBOLS];	//Audio tones for all Tx symbols
int volatile icw[NUM_CW_SYMBOLS];	    //Dits for CW ID
dec_data_t dec_data;             // for sharing with Fortran

int rc;
qint32  g_iptt {0};
wchar_t buffer[256];
int   old_row {-1};
QVector<QColor> g_ColorTbl;

namespace
{
  Radio::Frequency constexpr default_frequency {14076000};
  QRegularExpression message_alphabet {"[- @A-Za-z0-9+./?#<>]*"};
  QRegularExpression messagespec_alphabet {"[- @A-Za-z0-9+./?#<>;]*"};
  QRegularExpression wcall_alphabet {"[A-Za-z0-9/,]*"};
  QRegularExpression wcountry_alphabet {"[A-Za-z0-9/,*]*"};
  QRegularExpression wgrid_alphabet {"([a-r]{2,2}[0-9]{2,2}[,]{1,1})*",QRegularExpression::CaseInsensitiveOption};
  QRegularExpression cqdir_alphabet {"[a-z]{0,2}",QRegularExpression::CaseInsensitiveOption};
  QRegularExpression dxCall_alphabet {"[A-Za-z0-9/]*"};
  QRegularExpression dxGrid_alphabet {"[A-Ra-r]{2,2}[0-9]{2,2}[A-Xa-x]{2,2}[0-9]{2,2}[A-Xa-x]{2,2}"};
  QRegularExpression words_re {R"(^(?:(?<word1>(?:CQ|DE|QRZ)(?:\s?DX|\s(?:[A-Z]{2}|\d{3}))|[A-Z0-9/]+)\s)(?:(?<word2>[A-Z0-9/]+)(?:\s(?<word3>[-+A-Z0-9]+)(?:\s(?<word4>(?:OOO|(?!RR73)[A-R]{2}[0-9]{2})))?)?)?)"};
  constexpr int default_tx_audio_buffer_frames {-1}; // lets Qt decide

  bool message_is_73 (int type, QStringList const& msg_parts)
  {
    return type >= 0
      && (((type < 6 || 7 == type)
           && (msg_parts.contains ("73") || msg_parts.contains ("RR73")))
          || (type == 6 && !msg_parts.filter ("73").isEmpty ()));
  }

  int ms_minute_error (JTDXDateTime * jtdxtime)
  {
    auto const& now = jtdxtime->currentDateTime2 ();
    auto const& time = now.time ();
    auto second = time.second ();
    return now.msecsTo (now.addSecs (second > 30 ? 60 - second : -second)) - time.msec ();
  }
}

//--------------------------------------------------- MainWindow constructor
MainWindow::MainWindow(bool multiple, QSettings * settings, QSharedMemory *shdmem,
                       unsigned downSampleFactor, QNetworkAccessManager * network_manager,
                       QProcessEnvironment const& env, QWidget *parent) :
  QMainWindow(parent),
  m_exitCode {0},
  m_jtdxtime {new JTDXDateTime()},

  m_env {env},
  m_dataDir {QStandardPaths::writableLocation (QStandardPaths::DataLocation)},
  m_valid {true},
  m_revision {revision ()},
  m_multiple {multiple},
  m_settings {settings},
  ui(new Ui::MainWindow),
//  m_olek {false},
//  m_olek2 {false},
  m_config {settings, this},

  m_WSPR_band_hopping {settings, &m_config, this},
  m_WSPR_tx_next {false},
  m_wideGraph (new WideGraph(settings, m_jtdxtime)),
  m_logDlg (new LogQSO (settings, &m_config, m_jtdxtime, this)),
  m_lastDialFreq {145000000},
  //m_dialFreq {std::numeric_limits<Radio::Frequency>::max ()},
  m_dialFreqRxWSPR {0},
  m_detector {new Detector {RX_SAMPLE_RATE, double(NTMAX), m_jtdxtime , downSampleFactor}},
  m_FFTSize {6912 / 2},         // conservative value to avoid buffer overruns
  m_soundInput {new SoundInput},
  m_modulator {new Modulator {TX_SAMPLE_RATE, NTMAX, m_jtdxtime}},
  m_soundOutput {new SoundOutput},
  m_rx_audio_buffer_frames {0},
  m_tx_audio_buffer_frames {0},
  m_TRperiod {60.0},
  m_msErase {0},
  m_secBandChanged {0},
  m_secTxStopped {0},
  m_msDecStarted {0},
  m_freqNominal {0},
  m_freqTxNominal {0},
  m_lastDisplayFreq {0},
  m_mslastTX {0},
  m_mslastMon {0},
//  m_msDecoderStarted {0},
  m_waterfallAvg {1},
  m_ntx {1},
  m_addtx {0},
  m_nlasttx {0},
  m_lapmyc {0},
  m_delay {0},
  m_XIT {0},
  m_ndepth {3},
  m_ncandthin {100},
  m_nFT8Cycles {1},
  m_nFT8SWLCycles {1},
  m_nFT8RXfSens {1},
  m_nFT4depth {3},
  m_sec0 {-1},
  m_RxLog {1},			//Write Date and Time to RxLog
  m_nutc0 {999999},
  m_ntr {0},
  m_tx {0},
  m_secID {0},
  m_pctx {0},
  m_nseq {0},
  m_nWSPRdecodes {0},
  m_used_freq {0},
  m_nguardfreq {51},
  m_idleMinutes {0},
  m_oldTx5Index {0},
  m_oldFreeMsgIndex {0},
  m_ft8threads {0},
  m_acceptUDP {1},
  m_lastCallingFreq {1500},
  m_saveWav {0},
  m_callMode {2},
  m_ft8Sensitivity {0},
  m_position {0},
  m_nsecBandChanged {0},
  m_nDecodes {0},
  m_ncand {0},
  m_btxok {false},
  m_diskData {false},
  m_loopall {false},
  m_txFirst {false},
  m_txGenerated {false},
  m_enableTx {false},
  m_restart {false},
  m_startAnother {false},
  m_showHarmonics {false},
  m_showMyCallMsgRxWindow {true},
  m_showWantedCallRxWindow {true},
  m_bypassRxfFilters {false},
  m_bypassAllFilters {false},
  m_windowPopup {false},
  m_autoErase {false},
  m_autoEraseBC {false},
  m_rprtPriority {false},
  m_dataAvailable {false},
  m_blankLine {false},
  m_notified {false}, 
  m_start {true},
  m_start2 {true},
  m_decodedText2 {false},
  m_freeText {false},
  m_sentFirst73 {false},
  m_reply_me {false},
  m_reply_other {false},
  m_reply_CQ73 {false},
  m_tci_mod_active {false},
  m_tci {false},
  m_counter {0},
  m_currentMessageType {-1},
  m_currentMessage {""},
  m_curMsgTx {""},
  m_lastMessageType {-1},
  m_lockTxFreq {false},
  m_skipTx1 {false},
  m_swl {false},
  m_filter {false},
  m_agcc {false},
  m_hint {true},
  m_disable_TX_on_73 {false},
  m_showTooltips {true},
  m_autoTx {false},
  m_autoseq {false},
  m_wasAutoSeq {false},
  m_Tx5setAutoSeqOff {false},
  m_FTsetAutoSeqOff {false},
  m_uploadSpots {false},
  m_uploading {false},
  m_txNext {false},
  m_grid6 {false},
  m_tuneup {false},
  m_bTxTime {false},
  m_rxDone {false},
  m_bSimplex {false},
  m_logqso73 {false},
  m_processAuto_done {false},
  m_haltTrans {false},
  m_crossbandOptionEnabled {true},
  m_crossbandHLOptionEnabled {true},
  m_repliedCQ {""},
  m_dxbcallTxHalted {""},
  m_currentQSOcallsign {""},
  m_callPrioCQ {false},
  m_callFirst73 {false},
  m_maxDistance {false},
  m_answerWorkedB4 {false},
  m_singleshot {false},
  m_autofilter {false},
  m_houndMode {false},
  m_commonFT8b {true},
  m_houndTXfreqJumps {false},
  m_spotDXsummit {false},
  m_FilterState {0},
  m_manualDecode {false},
  m_haltTxWritten {false},
  m_strictdirCQ {false},
  m_colorTxMsgButtons {false},
  m_txbColorSet {false},
  m_bMyCallStd {true},
  m_bHisCallStd {true},
  m_callNotif {false},
  m_gridNotif {false},
  m_qsoLogged {false},
  m_logInitNeeded {false},
  m_wantedchkd {false},
  m_menus {true},
  m_wasSkipTx1 {false},
  m_modeChanged {false},
  m_FT8WideDxCallSearch {false},
  m_multInst {false},
  m_myCallCompound {false},
  m_hisCallCompound {false},
  m_callToClipboard {true},
  m_rigOk {false},
  m_bandChanged {false},
  m_useDarkStyle {false},
  m_lostaudio {false},
  m_lasthint {false},
  m_monitoroff {false},
  m_savedRRR {false},
  m_lang {"en_US"},
  m_lastloggedcall {""},
  m_cqdir {""},
  m_lastMode {""},
  m_callsign {""},
  m_grid {""},
  m_name {""},
  m_timeFrom {""},
  m_m_prefix {""},
  m_m_continent {""},
  m_spotText {""},
  m_QSOProgress {CALLING},
  m_transmittedQSOProgress {CALLING},
  tx_status_label {new QLabel {"Receiving"}},
  mode_label {new QLabel {""}},
  last_tx_label {new QLabel {""}},
  txwatchdog_label {new QLabel {""}},
  progressBar {new QProgressBar},
  date_label {new QLabel {""}},
  lastlogged_label {new QLabel {""}},
  qso_count_label {new QLabel {""}},
  wsprNet {new WSPRNet {network_manager, this}},
  Eqsl {new EQSL {network_manager, this}},
  m_hisCall {""},
  m_hisGrid {""},
  m_wantedCall {""}, m_wantedCountry {""}, m_wantedPrefix {""}, m_wantedGrid {""},
  m_wantedCallList {}, m_wantedCountryList {}, m_wantedPrefixList {}, m_wantedGridList {},
  m_appDir {QApplication::applicationDirPath ()},
  m_palette {"Linrad"},
  m_mode {"FT8"},
  m_oldmode {""},
  m_modeTx {"FT8"},
  m_rpt {"-15"},
  m_rptSent {"-15"},
  m_rptRcvd {"-15"},
  m_pfx {
      "1A", "1S",
      "3A", "3B6", "3B8", "3B9", "3C", "3C0", "3D2", "3D2C",
      "3D2R", "3DA", "3V", "3W", "3X", "3Y", "3YB", "3YP",
      "4J", "4L", "4S", "4U1I", "4U1U", "4W", "4X",
      "5A", "5B", "5H", "5N", "5R", "5T", "5U", "5V", "5W", "5X", "5Z",
      "6W", "6Y",
      "7O", "7P", "7Q", "7X",
      "8P", "8Q", "8R",
      "9A", "9G", "9H", "9J", "9K", "9L", "9M2", "9M6", "9N",
      "9Q", "9U", "9V", "9X", "9Y",
      "A2", "A3", "A4", "A5", "A6", "A7", "A9", "AP",
      "BS7", "BV", "BV9", "BY",
      "C2", "C3", "C5", "C6", "C9", "CE", "CE0X", "CE0Y",
      "CE0Z", "CE9", "CM", "CN", "CP", "CT", "CT3", "CU",
      "CX", "CY0", "CY9",
      "D2", "D4", "D6", "DL", "DU",
      "E3", "E4", "E5", "EA", "EA6", "EA8", "EA9", "EI", "EK",
      "EL", "EP", "ER", "ES", "ET", "EU", "EX", "EY", "EZ",
      "F", "FG", "FH", "FJ", "FK", "FKC", "FM", "FO", "FOA",
      "FOC", "FOM", "FP", "FR", "FRG", "FRJ", "FRT", "FT5W",
      "FT5X", "FT5Z", "FW", "FY",
      "M", "MD", "MI", "MJ", "MM", "MU", "MW",
      "H4", "H40", "HA", "HB", "HB0", "HC", "HC8", "HH",
      "HI", "HK", "HK0", "HK0M", "HL", "HM", "HP", "HR",
      "HS", "HV", "HZ",
      "I", "IS", "IS0",
      "J2", "J3", "J5", "J6", "J7", "J8", "JA", "JDM",
      "JDO", "JT", "JW", "JX", "JY",
      "K", "KC4", "KG4", "KH0", "KH1", "KH2", "KH3", "KH4", "KH5",
      "KH5K", "KH6", "KH7", "KH8", "KH9", "KL", "KP1", "KP2",
      "KP4", "KP5",
      "LA", "LU", "LX", "LY", "LZ",
      "OA", "OD", "OE", "OH", "OH0", "OJ0", "OK", "OM", "ON",
      "OX", "OY", "OZ",
      "P2", "P4", "PA", "PJ2", "PJ7", "PY", "PY0F", "PT0S", "PY0T", "PZ",
      "R1F", "R1M",
      "S0", "S2", "S5", "S7", "S9", "SM", "SP", "ST", "SU",
      "SV", "SVA", "SV5", "SV9",
      "T2", "T30", "T31", "T32", "T33", "T5", "T7", "T8", "T9", "TA",
      "TF", "TG", "TI", "TI9", "TJ", "TK", "TL", "TN", "TR", "TT",
      "TU", "TY", "TZ",
      "UA", "UA2", "UA9", "UK", "UN", "UR",
      "V2", "V3", "V4", "V5", "V6", "V7", "V8", "VE", "VK", "VK0H",
      "VK0M", "VK9C", "VK9L", "VK9M", "VK9N", "VK9W", "VK9X", "VP2E",
      "VP2M", "VP2V", "VP5", "VP6", "VP6D", "VP8", "VP8G", "VP8H",
      "VP8O", "VP8S", "VP9", "VQ9", "VR", "VU", "VU4", "VU7",
      "XE", "XF4", "XT", "XU", "XW", "XX9", "XZ",
      "YA", "YB", "YI", "YJ", "YK", "YL", "YN", "YO", "YS", "YU", "YV", "YV0",
      "Z2", "Z3", "ZA", "ZB", "ZC4", "ZD7", "ZD8", "ZD9", "ZF", "ZK1N",
      "ZK1S", "ZK2", "ZK3", "ZL", "ZL7", "ZL8", "ZL9", "ZP", "ZS", "ZS8"
      },
  m_sfx {"P",  "0",  "1",  "2",  "3",  "4",  "5",  "6",  "7",  "8",  "9",  "A"},
  m_dateTimeQSOOn {m_jtdxtime->currentDateTimeUtc2()},
  m_status {QsoHistory::NONE},
  mem_jtdxjt9 {shdmem},
  m_downSampleFactor (downSampleFactor),
  m_audioThreadPriority (QThread::HighPriority),
//  m_audioThreadPriority (QThread::HighestPriority),
//  m_audioThreadPriority (QThread::TimeCriticalPriority),
  m_bandEdited {false},
  m_splitMode {false},
  m_monitoring {false},
  m_tx_when_ready {false},
  m_transmitting {false},
  m_tune {false},
  m_txwatchdog {false},
  m_block_pwr_tooltip {false},
  m_PwrBandSetOK {true},
  m_okToPost {false},
  m_lastMonitoredFrequency {default_frequency},
  m_toneSpacing {0.},
  m_geometry_restored {2},
  m_firstDecode {0},
  m_optimizingProgress {"Optimizing decoder FFTs for your CPU.\n"
      "Please be patient,\n"
      "this may take a few minutes", QString {}, 0, 1, this},
  m_messageClient {new MessageClient {JtAlertCompatibility::instanceId(QApplication::applicationName ()), QCoreApplication::applicationVersion (),
                   m_config.udp_server_name (), m_config.udp_server_port (),
                   this}},
  psk_Reporter {new PSK_Reporter {m_messageClient, this}},
  m_manual {network_manager}
{
  ui->setupUi(this);
  auto stationTab=new QWidget(ui->tabWidget);stationTab->setObjectName("stationSwitchTab");
  auto stationLayout=new QVBoxLayout(stationTab);
  auto stationForm=new QFormLayout;
  stationForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
  m_quickStationSelector=new QComboBox(stationTab);
  m_quickStationSelector->setObjectName("quickStationSelector");
  m_quickStationSelector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_quickStationSelector->setMinimumContentsLength(12);
  m_quickStationGrid=new QLabel(stationTab);m_quickStationGrid->setObjectName("quickStationGrid");
  auto stationSelection=new QHBoxLayout;
  stationSelection->addWidget(m_quickStationSelector,1);
  m_confirmStationSwitchButton=new QPushButton(tr("Confirm switch"),stationTab);
  m_confirmStationSwitchButton->setObjectName("confirmStationSwitch");
  stationSelection->addWidget(m_confirmStationSwitchButton);
  stationForm->addRow(tr("My callsign"),stationSelection);
  stationForm->addRow(tr("Grid"),m_quickStationGrid);
  stationLayout->addLayout(stationForm);
  m_blacklistButton=new QPushButton(tr("Blacklist management"),stationTab);
  m_blacklistButton->setObjectName("blacklist_management");stationLayout->addWidget(m_blacklistButton,0,Qt::AlignLeft);
  connect(m_blacklistButton,&QPushButton::clicked,this,&MainWindow::showBlacklistManager);
  stationLayout->addStretch();
  ui->tabWidget->addTab(stationTab,QStringLiteral("3"));
  connect(ui->tabWidget,&QTabWidget::currentChanged,this,[this](int index){
    if(index==0 || index==1)m_txMessageTab=index;
  });
  connect(m_quickStationSelector,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int){
    updateStationSelectorEnabled();
  });
  connect(m_confirmStationSwitchButton,&QPushButton::clicked,this,[this]{
    switchStation(m_quickStationSelector->currentText());
  });
  updateStationSelector();
  m_config.set_jtdxtime (m_jtdxtime);
  ui->decodedTextBrowser->setConfiguration (&m_config,true);
  ui->decodedTextBrowser2->setConfiguration (&m_config);
  m_qsoHistory.jtdxtime = m_jtdxtime;
  m_qsoHistory2.jtdxtime = m_jtdxtime; 
  m_baseCall = Radio::base_callsign (m_config.my_callsign ());
 
  m_optimizingProgress.setWindowModality (Qt::WindowModal);
  m_optimizingProgress.setAutoReset (false);
  m_optimizingProgress.setMinimumDuration (15000); // only show after 15s delay

  m_tci = m_config.tci_audio() && m_config.is_tci();
  // Closedown.
  connect (ui->actionExit, &QAction::triggered, this, &QMainWindow::close);

  // parts of the rig error message box that are fixed

  m_rigErrorMessageBox.setInformativeText (tr ("Do you want to reconfigure the radio interface?"));
  m_rigErrorMessageBox.setStandardButtons (JTDXMessageBox::Cancel | JTDXMessageBox::Ok | JTDXMessageBox::Retry);
  m_rigErrorMessageBox.setDefaultButton (JTDXMessageBox::Ok);
  m_rigErrorMessageBox.translate_buttons();
  m_rigErrorMessageBox.setIcon (JTDXMessageBox::Critical);

  // start audio thread and hook up slots & signals for shutdown management
  // these objects need to be in the audio thread so that invoking
  // their slots is done in a thread safe way
  m_soundOutput->moveToThread (&m_audioThread);
  m_modulator->moveToThread (&m_audioThread);
  m_soundInput->moveToThread (&m_audioThread);
  m_detector->moveToThread (&m_audioThread);
  bool ok;
  auto buffer_size = env.value ("JTDX_RX_AUDIO_BUFFER_FRAMES", "0").toInt (&ok);
  m_rx_audio_buffer_override=ok?buffer_size:0;
  m_rx_audio_buffer_frames=RxAudioBufferPolicy::select(m_rx_audio_buffer_override,m_config.full_duplex());
  buffer_size = env.value ("JTDX_TX_AUDIO_BUFFER_FRAMES", "0").toInt (&ok);
  m_tx_audio_buffer_frames = ok && buffer_size ? buffer_size : default_tx_audio_buffer_frames;
  // hook up sound output stream slots & signals and disposal
  connect (this, &MainWindow::initializeAudioOutputStream, m_soundOutput, &SoundOutput::setFormat);
  connect (m_soundOutput, &SoundOutput::error, this, &MainWindow::showSoundOutError);
  connect(m_soundOutput,&SoundOutput::playbackError,this,[this](quint64 generation,QString const& message){
    if(generation && generation==m_audioPlaybackGeneration && (m_transmitting || m_tune || g_iptt || m_tx_when_ready))
      showSoundOutError(message);
  });
  connect(m_modulator,&Modulator::waveformConsumed,this,[this](quint64 generation){
    if(!generation || generation!=m_audioPlaybackGeneration)return;
    if(m_mode=="JTTY")m_jttyPcmConsumed=true;
    else if(upstreamMode())m_upstreamPcmConsumed=true;
  });
  connect(m_soundOutput,&SoundOutput::playbackDrained,this,[this](quint64 generation){
    if(!generation || generation!=m_audioPlaybackGeneration || !m_transmitting)return;
    if(m_mode=="JTTY" && m_jttyPcmConsumed)finishJttyTransmit();
    else if(upstreamMode() && m_upstreamPcmConsumed)finishUpstreamTransmit();
  });
  // connect (m_soundOutput, &SoundOutput::status, this, &MainWindow::showStatusMessage);
  connect (this, &MainWindow::outAttenuationChanged, m_soundOutput, &SoundOutput::setAttenuation);
  connect (&m_audioThread, &QThread::finished, m_soundOutput, &QObject::deleteLater);

  // hook up Modulator slots and disposal
  connect (this, &MainWindow::transmitFrequency, m_modulator, &Modulator::setFrequency);
  connect (this, &MainWindow::endTransmitMessage, m_modulator, &Modulator::stop);
  connect (this, &MainWindow::tune, m_modulator, &Modulator::tune);
  connect (this, &MainWindow::sendMessage, m_modulator, &Modulator::start);
  connect(this,&MainWindow::audioPeriod,m_modulator,&Modulator::setPeriod);
  connect(this,&MainWindow::audioPeriod,m_detector,&Detector::setPeriod);
  connect(this,&MainWindow::audioSpread,m_modulator,&Modulator::setSpread);
  connect (&m_audioThread, &QThread::finished, m_modulator, &QObject::deleteLater);

  // hook up the audio input stream signals, slots and disposal
  connect (this, &MainWindow::startAudioInputStream, m_soundInput, &SoundInput::start);
  connect (this, &MainWindow::suspendAudioInputStream, m_soundInput, &SoundInput::suspend);
  connect (this, &MainWindow::resumeAudioInputStream, m_soundInput, &SoundInput::resume);
  connect (this, &MainWindow::finished, m_soundInput, &SoundInput::stop);
  connect(m_soundInput, &SoundInput::error, this, &MainWindow::showSoundInError);
  // connect(m_soundInput, &SoundInput::status, this, &MainWindow::showStatusMessage);
  connect (&m_audioThread, &QThread::finished, m_soundInput, &QObject::deleteLater);

  connect (this, &MainWindow::finished, this, &MainWindow::close);

  // hook up the detector signals, slots and disposal
  connect (this, &MainWindow::FFTSize, m_detector, &Detector::setBlockSize);
  
  connect(m_detector, &Detector::framesWritten, this, &MainWindow::dataSink,Qt::QueuedConnection);
  connect (&m_audioThread, &QThread::finished, m_detector, &QObject::deleteLater);

  // setup the waterfall
  connect(m_wideGraph.data (), SIGNAL(freezeDecode2(int)),this,SLOT(freezeDecode(int)));
  connect(m_wideGraph.data (), SIGNAL(f11f12(int)),this,SLOT(bumpFqso(int)));
  connect(m_wideGraph.data (), SIGNAL(setXIT2(int)),this,SLOT(setXIT(int)));

  connect (this, &MainWindow::finished, m_wideGraph.data (), &WideGraph::close);

  // setup the log QSO dialog
  connect (m_logDlg.data (), &LogQSO::acceptQSO, this, &MainWindow::acceptQSO2);
  connect(m_logDlg.data(),&QDialog::finished,this,[this] {
    m_config.set_station_locked(m_enableTx || m_tune || m_transmitting || logQSOTimer.isActive());
    updateStationSelectorEnabled();
  });
  connect(m_logDlg.data(),&LogQSO::forwardingError,this,[this](QString const& error){statusBar()->showMessage(error,30000);mcpEvent("log_forwarding_error",{{"message",error}});});
  connect (this, &MainWindow::finished, m_logDlg.data (), &LogQSO::close);

  // Network message handlers
  connect (m_messageClient, &MessageClient::reply, this, &MainWindow::replyToUDP);
  connect (m_messageClient, &MessageClient::replay, this, &MainWindow::replayDecodes);
  connect (m_messageClient, &MessageClient::halt_tx, [this] (bool enableTx_only) {
      if (m_config.accept_udp_requests ()) {
        if (enableTx_only) { if (ui->enableTxButton->isChecked ()) ui->enableTxButton->click(); }
//		else { ui->stopTxButton->click(); }
		else { haltTx("UDP request "); }
      }
    });
  connect (m_messageClient, &MessageClient::error, this, &MainWindow::networkError);
  connect (m_messageClient, &MessageClient::free_text, [this] (QString const& text, bool send) {
      if (m_config.accept_udp_requests ()) {
        // send + non-empty text means set and send the free text
        // message, !send + non-empty text means set the current free
        // text message, send + empty text means send the current free
        // text message without change, !send + empty text means clear
        // the current free text messageski
        txwatchdog (false);
        qDebug () << "Free text UDP message - text:" << text << "send:" << send << "text empty:" << text.isEmpty ();
        if (0 == txTabIndex()) {
          if (!text.isEmpty ()) {
            ui->tx5->setCurrentText (text);
          }
          if (send) {
            ui->txb5->click ();
          } else if (text.isEmpty ()) {
            ui->tx5->setCurrentText (text);
          }
        } else if (1 == txTabIndex()) {
          if (!text.isEmpty ()) {
            ui->freeTextMsg->setCurrentText (text);
          }
          if (send) {
            ui->rbFreeText->click ();
          } else if (text.isEmpty ()) {
            ui->freeTextMsg->setCurrentText (text);
          }
        }
      }
    });

  connect (m_messageClient, &MessageClient::set_tx_deltafreq, [this] (quint32 tx_delta_frequency) {
    if (m_config.accept_udp_requests ()) {
        txwatchdog (false);
        quint32 start_freq=m_wideGraph->nStartFreq();
        quint32 max_freq=m_wideGraph->Fmax();
        if(tx_delta_frequency > start_freq && tx_delta_frequency < max_freq) ui->TxFreqSpinBox->setValue(tx_delta_frequency);
    }
  });

  connect (m_messageClient, &MessageClient::trigger_CQ, [this] (QString const& direction, bool tx_period, bool send) {
    if (m_config.accept_udp_requests ()) {
        txwatchdog (false);
        if (0 == txTabIndex()) {
          if (direction != ui->direction1LineEdit->text()) ui->direction1LineEdit->setText(direction);
          if (send) ui->txb6->click ();
          if (tx_period != m_txFirst) ui->TxMinuteButton->click ();
          if (send && !m_enableTx) ui->enableTxButton->click ();
        }
        if (1 == txTabIndex()) {
          if (direction != ui->directionLineEdit->text()) ui->directionLineEdit->setText(direction);
          if (send) ui->pbCallCQ->click ();
          if (tx_period != m_txFirst) ui->TxMinuteButton->click ();
          if (send && !m_enableTx) ui->enableTxButton->click ();
        }
    }
  });

  // Hook up WSPR band hopping
  connect (ui->band_hopping_schedule_push_button, &QPushButton::clicked
           , &m_WSPR_band_hopping, &WSPRBandHopping::show_dialog);
  connect (ui->sbTxPercent, static_cast<void (QSpinBox::*) (int)> (&QSpinBox::valueChanged)
           , &m_WSPR_band_hopping, &WSPRBandHopping::set_tx_percent);

  on_EraseButton_clicked ();
  clearDX ("");

  QActionGroup* modeGroup = new QActionGroup(this);
  ui->actionFT4->setActionGroup(modeGroup);
  ui->actionFT2->setActionGroup(modeGroup);
  ui->actionJTTY->setActionGroup(modeGroup);
  ui->actionFT8->setActionGroup(modeGroup);
  ui->actionJT65->setActionGroup(modeGroup);
  ui->actionJT9_JT65->setActionGroup(modeGroup);
  ui->actionJT9->setActionGroup(modeGroup);
  ui->actionT10->setActionGroup(modeGroup);
  ui->actionWSPR_2->setActionGroup(modeGroup);

  QActionGroup* languageGroup = new QActionGroup(this);
  ui->actionEnglish->setActionGroup(languageGroup);
  ui->actionEstonian->setActionGroup(languageGroup);
  ui->actionRussian->setActionGroup(languageGroup);
  ui->actionCatalan->setActionGroup(languageGroup);
  ui->actionCroatian->setActionGroup(languageGroup);
  ui->actionDanish->setActionGroup(languageGroup);
  ui->actionDutch->setActionGroup(languageGroup);
  ui->actionHungarian->setActionGroup(languageGroup);
  ui->actionSpanish->setActionGroup(languageGroup);
  ui->actionSwedish->setActionGroup(languageGroup);
  ui->actionFrench->setActionGroup(languageGroup);
  ui->actionItalian->setActionGroup(languageGroup);
  ui->actionLatvian->setActionGroup(languageGroup);
  ui->actionPolish->setActionGroup(languageGroup);
  ui->actionPortuguese->setActionGroup(languageGroup);
  ui->actionPortuguese_BR->setActionGroup(languageGroup);
  ui->actionChinese_simplified->setActionGroup(languageGroup);
  ui->actionChinese_traditional->setActionGroup(languageGroup);
  ui->actionJapanese->setActionGroup(languageGroup);

  QActionGroup* saveGroup = new QActionGroup(this);
  ui->actionNone->setActionGroup(saveGroup);
  ui->actionSave_decoded->setActionGroup(saveGroup);
  ui->actionSave_all->setActionGroup(saveGroup);

  QActionGroup* DepthGroup = new QActionGroup(this);
  ui->actionQuickDecode->setActionGroup(DepthGroup);
  ui->actionMediumDecode->setActionGroup(DepthGroup);
  ui->actionDeepestDecode->setActionGroup(DepthGroup);

  QActionGroup* FT8CyclesGroup = new QActionGroup(this);
  ui->actionDecFT8cycles1->setActionGroup(FT8CyclesGroup);
  ui->actionDecFT8cycles2->setActionGroup(FT8CyclesGroup);
  ui->actionDecFT8cycles3->setActionGroup(FT8CyclesGroup);

  QActionGroup* FT8SWLCyclesGroup = new QActionGroup(this);
  ui->actionDecFT8SWLcycles1->setActionGroup(FT8SWLCyclesGroup);
  ui->actionDecFT8SWLcycles2->setActionGroup(FT8SWLCyclesGroup);
  ui->actionDecFT8SWLcycles3->setActionGroup(FT8SWLCyclesGroup);
  
  QActionGroup* FT8RXfreqSensitivityGroup = new QActionGroup(this);
  ui->actionRXfLow->setActionGroup(FT8RXfreqSensitivityGroup);
  ui->actionRXfMedium->setActionGroup(FT8RXfreqSensitivityGroup);
  ui->actionRXfHigh->setActionGroup(FT8RXfreqSensitivityGroup);
  
  QActionGroup* FT8DecoderSensitivityGroup = new QActionGroup(this);
  ui->actionFT8SensMin->setActionGroup(FT8DecoderSensitivityGroup);
  ui->actionlowFT8thresholds->setActionGroup(FT8DecoderSensitivityGroup);
  ui->actionFT8subpass->setActionGroup(FT8DecoderSensitivityGroup);

  QActionGroup* FT4DepthGroup = new QActionGroup(this);
  ui->actionFT4fast->setActionGroup(FT4DepthGroup);
  ui->actionFT4medium->setActionGroup(FT4DepthGroup);
  ui->actionFT4deep->setActionGroup(FT4DepthGroup);

  m_managedClock.start();
  m_managedMenu=new QMenu(tr("Managed mode"),this);m_managedMenu->setObjectName("menuManaged");
  ui->menuBar->insertMenu(ui->menuDXpedition->menuAction(),m_managedMenu);
  auto managedActions=new QActionGroup(this);managedActions->setExclusive(true);
  for(auto mode:{ManagedMode::DxccHunter,ManagedMode::QsoEnthusiast,ManagedMode::Off}) {
    auto action=m_managedMenu->addAction(mode==ManagedMode::DxccHunter?tr("DXCC hunter"):mode==ManagedMode::QsoEnthusiast?tr("QSO enthusiast"):tr("Off"));
    action->setObjectName("managed_mode_"+QString::number(int(mode)));action->setCheckable(true);action->setActionGroup(managedActions);
    action->setData(int(mode));if(mode==ManagedMode::Off){m_managedOff=action;action->setChecked(true);}
    connect(action,&QAction::triggered,this,[this,mode]{setManagedMode(mode);});
  }
  QActionGroup* AutoseqGroup = new QActionGroup(this);
  ui->actionCallNone->setActionGroup(AutoseqGroup);
  ui->actionCallFirst->setActionGroup(AutoseqGroup);
  ui->actionCallMid->setActionGroup(AutoseqGroup);
  ui->actionCallEnd->setActionGroup(AutoseqGroup);

  // These are two ordering choices, not independent enable/disable switches.
  auto autoPriorityGroup = new QActionGroup(this);
  autoPriorityGroup->setExclusive(true);
  ui->actionReport_message_priority->setActionGroup(autoPriorityGroup);
  ui->actionMaxDistance->setActionGroup(autoPriorityGroup);

  setupFt8Threads(QThread::idealThreadCount());

  QActionGroup* AcceptUDPGroup = new QActionGroup(this);
  ui->actionAcceptUDPCQ->setActionGroup(AcceptUDPGroup);
  ui->actionAcceptUDPCQ73->setActionGroup(AcceptUDPGroup);
  ui->actionAcceptUDPAny->setActionGroup(AcceptUDPGroup);

  connect (ui->download_samples_action, &QAction::triggered, [this, network_manager] () {
      if (!m_sampleDownloader) {
          m_sampleDownloader.reset (new SampleDownloader {m_settings, &m_config, network_manager, this});
      }
      m_sampleDownloader->show ();
    });

  QButtonGroup* txMsgButtonGroup = new QButtonGroup {this};
  txMsgButtonGroup->addButton(ui->txrb1,1);
  txMsgButtonGroup->addButton(ui->txrb2,2);
  txMsgButtonGroup->addButton(ui->txrb3,3);
  txMsgButtonGroup->addButton(ui->txrb4,4);
  txMsgButtonGroup->addButton(ui->txrb5,5);
  txMsgButtonGroup->addButton(ui->txrb6,6);
  connect(txMsgButtonGroup,SIGNAL(buttonClicked(int)),SLOT(set_ntx(int)));
  connect(ui->decodedTextBrowser2,SIGNAL(selectCallsign(bool,bool)),this,SLOT(doubleClickOnCall(bool,bool)));
  connect(ui->decodedTextBrowser,SIGNAL(selectCallsign(bool,bool)),this,SLOT(doubleClickOnCall2(bool,bool)));
  connect(ui->decodedTextBrowser->horizontalScrollBar(),SIGNAL(sliderMoved(int)),SLOT(ScrollBarPosition(int)));

  // initialise decoded text font and hook up change signal
  setDecodedTextFont (m_config.decoded_text_font ());
  connect (&m_config, &Configuration::decoded_text_font_changed, [this] (QFont const& font) {
      setDecodedTextFont (font);
    });

//  setWindowTitle (program_title ());
  setWindowTitle (program_title ());

  createStatusBar();

  connect(&proc_jtdxjt9, &QProcess::readyReadStandardOutput,this, &MainWindow::readFromStdout);
#if QT_VERSION < QT_VERSION_CHECK (5, 6, 0)
  connect(&proc_jtdxjt9, static_cast<void (QProcess::*) (QProcess::ProcessError)> (&QProcess::error),
          [this] (QProcess::ProcessError error) {
            subProcessError (&proc_jtdxjt9, error);
          });
#else
  connect(&proc_jtdxjt9, &QProcess::errorOccurred, [this] (QProcess::ProcessError error) {
                                                 subProcessError (&proc_jtdxjt9, error);
                                               });
#endif
  connect(&proc_jtdxjt9, static_cast<void (QProcess::*) (int, QProcess::ExitStatus)> (&QProcess::finished),
          [this] (int exitCode, QProcess::ExitStatus status) {
            if (m_valid && !m_decoderRecovering)
              recoverDecoder(tr("Decoder exited (code %1, crash %2)").arg(exitCode).arg(status == QProcess::CrashExit));
          });

  connect(&p1, &QProcess::readyReadStandardOutput,this, &MainWindow::p1ReadFromStdout);
#if QT_VERSION < QT_VERSION_CHECK (5, 6, 0)
  connect(&p1, static_cast<void (QProcess::*) (QProcess::ProcessError)> (&QProcess::error),
          [this] (QProcess::ProcessError error) {
            subProcessError (&p1, error);
          });
#else
  connect(&p1, &QProcess::errorOccurred, [this] (QProcess::ProcessError error) {
                                           subProcessError (&p1, error);
                                         });
#endif
  connect(&p1, static_cast<void (QProcess::*) (int, QProcess::ExitStatus)> (&QProcess::finished),
          [this] (int exitCode, QProcess::ExitStatus status) {
            if (subProcessFailed (&p1, exitCode, status))
              {
                m_valid = false;          // ensures exit if still
                                          // constructing
                QTimer::singleShot (0, this, SLOT (close ()));
              }
          });

#if QT_VERSION < QT_VERSION_CHECK (5, 6, 0)
  connect(&p3, static_cast<void (QProcess::*) (QProcess::ProcessError)> (&QProcess::error),
          [this] (QProcess::ProcessError error) {
#else
  connect(&p3, &QProcess::errorOccurred, [this] (QProcess::ProcessError error) {
#endif
#if !defined(Q_OS_WIN)
                                           if (QProcess::FailedToStart != error)
#else
                                           if (QProcess::Crashed != error)
#endif
                                             {
                                               subProcessError (&p3, error);
                                             }
                                         });
  connect(&p3, &QProcess::started, [this] () {
                                     showStatusMessage (QString {"Started: %1 \"%2\""}.arg (p3.program ()).arg (p3.arguments ().join ("\" \"")));
                                   });
  connect(&p3, static_cast<void (QProcess::*) (int, QProcess::ExitStatus)> (&QProcess::finished),
          [this] (int exitCode, QProcess::ExitStatus status) {
#if defined(Q_OS_WIN)
            // We forgo detecting user_hardware failures with exit
            // code 1 on Windows. This is because we use CMD.EXE to
            // run the executable. CMD.EXE returns exit code 1 when it
            // can't find the target executable.
            if (exitCode != 1)  // CMD.EXE couldn't find file to execute
#else
            // We forgo detecting user_hardware failures with exit
            // code 127 non-Windows. This is because we use /bin/sh to
            // run the executable. /bin/sh returns exit code 127 when it
            // can't find the target executable.
            if (exitCode != 127)  // /bin/sh couldn't find file to execute
#endif
              {
                subProcessFailed (&p3, exitCode, status);
              }
          });

  // hook up save WAV file exit handling
  connect (&m_saveWAVWatcher, &QFutureWatcher<QString>::finished, [this] {
      auto result=m_saveWAVWatcher.future().result();m_wavSaveBusy=false;
      auto args=m_wsprDecodeArgs;m_wsprDecodeArgs.clear();
      if(!result.isEmpty()) {
        if(!args.isEmpty())decodeBusy(false);
        JTDXMessageBox::critical_message(this,"",tr("Error Writing WAV File"),result);
      } else if(!args.isEmpty() && m_mode.startsWith("WSPR")) {
        p1.start(QDir::toNativeSeparators(m_appDir+"/wsprd_jtdx"),args);
      }
    });

  // Hook up working frequencies.
  ui->bandComboBox->setModel (m_config.frequencies ());
  ui->bandComboBox->setModelColumn (FrequencyList_v2::frequency_mhz_column);

  // combo box drop down width defaults to the line edit + decorator width,
  // here we change that to the column width size hint of the model column
  ui->bandComboBox->view ()->setMinimumWidth (ui->bandComboBox->view ()->sizeHintForColumn (FrequencyList_v2::frequency_mhz_column));

  // Enable live band combo box entry validation and action.
  auto band_validator = new LiveFrequencyValidator {ui->bandComboBox
                                                    , m_config.bands ()
                                                    , m_config.frequencies ()
                                                    , &m_freqNominal
                                                   , m_config.kHz_without_k()
                                                    , this};
  ui->bandComboBox->setValidator (band_validator);

  // Hook up signals.
  connect (band_validator, &LiveFrequencyValidator::valid, this, &MainWindow::band_changed);
  connect (ui->bandComboBox->lineEdit (), &QLineEdit::textEdited, [this] (QString const&) {m_bandEdited = true; if(m_config.write_decoded_debug()) writeToALLTXT("bandComboBox line edited to " + ui->bandComboBox->lineEdit()->text());});

  // hook up configuration signals
  connect (&m_config, &Configuration::transceiver_update, this, &MainWindow::handle_transceiver_update);
  connect (&m_config, &Configuration::transceiver_TCIframesWritten, this, &MainWindow::dataSink);
  connect (&m_config, &Configuration::transceiver_TCImodActive, this, &MainWindow::tci_mod_active);
  connect (&m_config, &Configuration::transceiver_failure, this, &MainWindow::handle_transceiver_failure);
  connect (&m_config, &Configuration::udp_server_changed, m_messageClient, &MessageClient::set_server);
  connect (&m_config, &Configuration::udp_server_port_changed, m_messageClient, &MessageClient::set_server_port);


  // set up message text validators
  ui->tx1->setValidator (new QRegularExpressionValidator {message_alphabet, this});
  ui->tx2->setValidator (new QRegularExpressionValidator {messagespec_alphabet, this});
  ui->tx3->setValidator (new QRegularExpressionValidator {message_alphabet, this});
  ui->tx4->setValidator (new QRegularExpressionValidator {message_alphabet, this});
  ui->tx5->setValidator (new QRegularExpressionValidator {message_alphabet, this});
  ui->tx6->setValidator (new QRegularExpressionValidator {message_alphabet, this});
  ui->freeTextMsg->setValidator (new QRegularExpressionValidator {message_alphabet, this});
  ui->wantedCall->setValidator (new QRegularExpressionValidator {wcall_alphabet, this});
  ui->wantedCountry->setValidator (new QRegularExpressionValidator {wcountry_alphabet, this});
  ui->wantedPrefix->setValidator (new QRegularExpressionValidator {wcall_alphabet, this});
  ui->wantedGrid->setValidator (new QRegularExpressionValidator {wgrid_alphabet, this});
  ui->dxCallEntry->setValidator (new QRegularExpressionValidator {dxCall_alphabet, this});
  ui->dxGridEntry->setValidator (new QRegularExpressionValidator {dxGrid_alphabet, this});
  ui->directionLineEdit->setValidator (new QRegularExpressionValidator {cqdir_alphabet, this});
  ui->direction1LineEdit->setValidator (new QRegularExpressionValidator {cqdir_alphabet, this});

  // Free text macros model to widget hook up.
  ui->tx5->setModel (m_config.macros ());
  connect (ui->tx5->lineEdit ()
           , &QLineEdit::editingFinished
           , [this] () {on_tx5_currentTextChanged (ui->tx5->lineEdit ()->text ());});
  ui->freeTextMsg->setModel (m_config.macros ());
  connect (ui->freeTextMsg->lineEdit ()
           , &QLineEdit::editingFinished
           , [this] () {on_freeTextMsg_currentTextChanged (ui->freeTextMsg->lineEdit ()->text ());});

  connect(&m_guiTimer, &QTimer::timeout, this, &MainWindow::guiUpdate);
  m_guiTimer.start(100);   //### Don't change the 100 ms! ###

  ptt0Timer.setSingleShot(true); connect(&ptt0Timer, &QTimer::timeout, this, &MainWindow::stopTx2);
  ptt1Timer.setSingleShot(true); connect(&ptt1Timer, &QTimer::timeout, this, &MainWindow::startTx2);
  logQSOTimer.setSingleShot(true); connect(&logQSOTimer, &QTimer::timeout, this, &MainWindow::on_logQSOButton_clicked);
  tuneButtonTimer.setSingleShot(true); connect(&tuneButtonTimer, &QTimer::timeout, this, &MainWindow::haltTxTuneTimer);
  cqButtonTimer.setSingleShot(true); connect(&cqButtonTimer, &QTimer::timeout, this, &MainWindow::on_pbCallCQ_clicked);
  enableTxButtonTimer.setSingleShot(true); connect(&enableTxButtonTimer, &QTimer::timeout, this, &MainWindow::enableTxButton_off);
  tx73ButtonTimer.setSingleShot(true); connect(&tx73ButtonTimer, &QTimer::timeout, this, &MainWindow::on_pbSend73_clicked);
  logClearDXTimer.setSingleShot(true); connect(&logClearDXTimer, &QTimer::timeout, this, &MainWindow::logClearDX);
  dxbcallTxHaltedClearTimer.setSingleShot(true); connect(&dxbcallTxHaltedClearTimer, &QTimer::timeout, this, &MainWindow::dxbcallTxHaltedClear);
  tuneATU_Timer.setSingleShot(true); connect(&tuneATU_Timer, &QTimer::timeout, this, &MainWindow::stopTuneATU);
  killFileTimer.setSingleShot(true); connect(&killFileTimer, &QTimer::timeout, this, &MainWindow::killFile);
  uploadTimer.setSingleShot(true); connect(&uploadTimer, SIGNAL(timeout()), this, SLOT(uploadSpots()));
  TxAgainTimer.setSingleShot(true); connect(&TxAgainTimer, SIGNAL(timeout()), this, SLOT(TxAgain()));
  StopTuneTimer.setSingleShot(true); connect(&StopTuneTimer, SIGNAL(timeout()), this, SLOT(stop_tuning()));
  RxQSYTimer.setSingleShot(true); connect(&RxQSYTimer, SIGNAL(timeout()), this, SLOT(RxQSY()));
  minuteTimer.setSingleShot(true); connect (&minuteTimer, &QTimer::timeout, this, &MainWindow::on_the_minute);

  connect(m_wideGraph.data (), SIGNAL(setFreq3(int,int)), this, SLOT(setFreq4(int,int)));
  connect(m_wideGraph.data (), SIGNAL(setRxFreq3(int)), this, SLOT(setRxFreq4(int)));
  connect(m_wideGraph.data (), SIGNAL(filter_on3()), this, SLOT(filter_on()));
  connect(m_wideGraph.data (), SIGNAL(toggle_filter3()), this, SLOT(toggle_filter()));
  connect(m_wideGraph.data (), SIGNAL(esc_key()), this, SLOT(escapeHalt()));

  fsWatcher = new QFileSystemWatcher(this);
  connect(fsWatcher,&QFileSystemWatcher::fileChanged,this,[this] {
    // A logger may replace bytes without changing file size or its timestamp.
    ++m_logInputRevision;m_logFilesStamp=operatorLogStamp();m_logRefreshTimer.start(400);
  });
  connect(fsWatcher, SIGNAL(directoryChanged(QString)), this, SLOT(logChanged()));
  m_logRefreshTimer.setSingleShot(true);
  connect(&m_logRefreshTimer,&QTimer::timeout,this,[this] { importOperatorLogs();refreshOperatorLogs(false); });
  // Cached content checks finish quickly and should not flash a modal dialog.
  m_logProgressTimer.setSingleShot(true);m_logProgressTimer.setInterval(150);
  connect(&m_logProgressTimer,&QTimer::timeout,this,[this] {
    if(!m_logProgressRequested || isMinimized() ||
       !(m_logLoading || m_logImportActive || m_logRefreshPending))return;
    if(!m_logProcessingDialog) {
      m_logProcessingDialog=new ProcessingDialog(this);
      connect(m_logProcessingDialog.data(),&ProcessingDialog::stopTxRequested,this,&MainWindow::on_stopTxButton_clicked);
    }
    m_logProcessingDialog->setStopVisible(m_enableTx || m_tune || m_transmitting);
    m_logProcessingDialog->show();
  });
  connect(&m_logImportWatcher,&QFutureWatcher<OperatorLog::ImportResult>::finished,this,[this] {
    m_logImportActive=false;++m_logInputRevision;auto result=m_logImportWatcher.result();m_logRefreshTimer.stop();refreshOperatorLogs(true);
    QString message=tr("Log import: %1 new, %2 duplicates, %3 originals archived, %4 records from other stations skipped.").arg(result.imported).arg(result.duplicates).arg(result.archived).arg(result.skipped);
    if(!result.errors.isEmpty()) message+=" "+result.errors.join("; ");
    statusBar()->showMessage(message,20000);
    mcpEvent("log_import",{{"imported",result.imported},{"duplicates",result.duplicates},{"skipped",result.skipped},{"errors",QJsonArray::fromStringList(result.errors)}});
  });
  connect(&m_logLoadWatcher,&QFutureWatcher<LogSnapshot>::finished,this,[this] {
    m_logLoading=false;auto snapshot=m_logLoadWatcher.result();
    bool current=snapshot.call==m_config.my_callsign() && snapshot.grid==(m_config.gridNotif()?m_config.my_grid():QString()) && snapshot.date==m_config.timeFrom()
      && m_logLoadRevision==m_logInputRevision;
    if(current && snapshot.error.isEmpty() && !m_logImportActive) {
      m_logBook=snapshot.book;m_logReady=true;m_logReadyContext=m_logLoadContext;countQSOs();updateOwnDxcc();
      if(m_config.write_decoded_debug()) writeToALLTXT(QString("Operator log loaded: cache=%1 elapsed=%2 ms").arg(snapshot.cacheHit).arg(snapshot.elapsedMs));
    } else if(current && !snapshot.error.isEmpty()) {
      m_logReady=false;
      statusBar()->showMessage(tr("Log loading failed: %1").arg(snapshot.error),20000);
    }
    // Atomic replacements detach individual file watches. Reattach them and
    // let any changes during the load receive a cached content check.
    watchOperatorLogs();logChanged();
    if(!current || m_logRefreshPending) {m_logRefreshPending=false;refreshOperatorLogs(m_logProgressRequested);}
    updateLogProcessingDialog();
  });
  setAcceptDrops(true);
  QTimer::singleShot(0,this,[this]{importOperatorLogs();refreshOperatorLogs(true);});
  
  m_rrr=false;
  m_killAll=false;
  m_sentFirst73=false;
  m_skipTx1=false;
  m_QSOText.clear();
  decodeBusy(false);
  m_callingFrequency=0;
  QString t1[28]={"1 uW","2 uW","5 uW","10 uW","20 uW","50 uW","100 uW","200 uW","500 uW",
                  "1 mW","2 mW","5 mW","10 mW","20 mW","50 mW","100 mW","200 mW","500 mW",
                  "1 W","2 W","5 W","10 W","20 W","50 W","100 W","200 W","500 W","1 kW"};

  for(int i=0; i<28; i++)  {                      //Initialize dBm values
    float dbm=(10.0*i)/3.0 - 30.0;
    int ndbm=0;
    if(dbm<0) ndbm=int(dbm-0.5);
    if(dbm>=0) ndbm=int(dbm+0.5);
    QString t;
    t = QString::asprintf("%d dBm  ",ndbm);
    t+=t1[i];
    ui->TxPowerComboBox->addItem(t);
  }


  ui->labAz->setStyleSheet("border: 0px;");
  ui->labDist->setStyleSheet("border: 0px;");

  m_useDarkStyle = m_config.useDarkStyle();
  readSettings();		         //Restore user's setup params
  applyGeneralSettings();

  QString t;
  if (m_mode.startsWith("FT")) t = "UTC     dB   DT "+tr("Freq   Message");
  else t = "UTC   dB   DT "+tr("Freq   Message");
  ui->decodedTextLabel->setText(t);
  ui->decodedTextLabel2->setText(t);

  ui->tuneButton->setMaximumSize(QWIDGETSIZE_MAX,45);
  ui->monitorButton->setMaximumSize(QWIDGETSIZE_MAX,45);
  ui->bypassButton->setMaximumSize(QWIDGETSIZE_MAX,45);
  ui->singleQSOButton->setMaximumSize(QWIDGETSIZE_MAX,45);
  ui->AnsB4Button->setMaximumSize(QWIDGETSIZE_MAX,45);
  ui->stopButton->setMaximumSize(QWIDGETSIZE_MAX,45);
  dynamicButtonsInit();

  m_audioThread.start (m_audioThreadPriority);

#ifdef WIN32
  if (!m_multiple)
    {
      while(true)
        {
          int iret=killbyname("jtdxjt9.exe");
          if(iret == 603) break;
            JTDXMessageBox::warning_message (this, "", tr ("Error Killing jtdxjt9.exe Process")
                                         , tr ("KillByName return code: %1")
                                         .arg (iret));
        }
    }
#endif

//  auto_tx_label->setText (m_autoTx ? "AutoTx Armed" : "AutoTx Disarmed");

  {
    //delete any .quit file that might have been left lying around
    //since its presence will cause jtdxjt9 to exit a soon as we start it
    //and decodes will hang
    QFile quitFile {m_config.temp_dir ().absoluteFilePath (".quit")};
    while (quitFile.exists ())
      {
        if (!quitFile.remove ())
          {
            JTDXMessageBox::query_message (this, "", tr ("Error removing \"%1\"").arg (quitFile.fileName ())
                                       , tr ("Click OK to retry"));
          }
      }
  }

  //Create .lock so jtdxjt9 will wait
  QFile {m_config.temp_dir ().absoluteFilePath (".lock")}.open(QIODevice::ReadWrite);

  QStringList jt9_args {
    "-s", QApplication::applicationName () // shared memory key,
                                           // includes rig
#ifdef NDEBUG
      , "-w", "1"               //FFTW patience - release
#else
      , "-w", "1"               //FFTW patience - debug builds for speed
#endif
      // The number  of threads for  FFTW specified here is  chosen as
      // three because  that gives  the best  throughput of  the large
      // FFTs used  in jt9.  The count  is the minimum of  (the number
      // available CPU threads less one) and 12 (was three).  This ensures that
      // there is always at least one free CPU thread to run the other
      // mode decoder in parallel.
      , "-m", QString::number (qMin (qMax (QThread::idealThreadCount () - 1, 1), 24)) //FFTW threads
//      , "-m", QString::number (qMin (qMax (QThread::idealThreadCount (), 1), 24)) //FFTW threads
      , "-e", QDir::toNativeSeparators (m_appDir)
      , "-a", QDir::toNativeSeparators (m_dataDir.absolutePath ())
      , "-t", QDir::toNativeSeparators (m_config.temp_dir ().absolutePath ())
      , "-r", QDir::toNativeSeparators (m_config.data_dir ().absolutePath ())
      };
  QProcessEnvironment new_env {m_env};
  new_env.insert  ("OMP_STACKSIZE", "10M");
  proc_jtdxjt9.setProcessEnvironment (new_env);
  proc_jtdxjt9.start(QDir::toNativeSeparators (m_appDir) + QDir::separator () +
          "jtdxjt9", jt9_args, QIODevice::ReadWrite | QIODevice::Unbuffered);

  QString fname {QDir::toNativeSeparators(m_dataDir.absoluteFilePath ("wsjtx_wisdom.dat"))};
  QByteArray cfname=fname.toLocal8Bit();
  fftwf_import_wisdom_from_filename(cfname);

//  QThread::currentThread()->setPriority(QThread::HighPriority);

  connect (&m_wav_future_watcher, &QFutureWatcher<void>::finished, this, &MainWindow::diskDat);

#if JTDX_DEBUG_TO_FILE
  FILE * pFile = fopen (QDir(QStandardPaths::writableLocation (QStandardPaths::DataLocation)).absoluteFilePath ("jtdx_debug.txt").toStdString().c_str(),"a");  
  debugFilePrintf(pFile,"%s(%0.1f) JTDX v%s start, performance %d threads\n",m_jtdxtime->currentDateTimeUtc2().toString("hh:mm:ss.zzz").toStdString().c_str(),m_jtdxtime->GetOffset(),
      (version() + (m_tci ? " tci " : " ") + revision()).toStdString().c_str(),QThread::idealThreadCount ());
  debugFileClose(pFile);
#endif
//  printf ("is_tci: %d downsampleFactor %d\n",m_tci,m_downSampleFactor);
  if (m_tci) {
    Q_EMIT m_config.transceiver_period(double(NTMAX));
  } else {
    if (!m_config.audio_input_device ().isNull ()) {
      startSoundcardInputStream();
    }
    if (!m_config.audio_output_device ().isNull ()) {
      Q_EMIT initializeAudioOutputStream (m_config.audio_output_device (), AudioDevice::Mono == m_config.audio_output_channel () ? 1 : 2, m_tx_audio_buffer_frames);
    }
  }
//  if (m_tci) Q_EMIT m_config.transceiver_trfrequency(ui->TxFreqSpinBox->value () - m_XIT); 
//  else Q_EMIT transmitFrequency (ui->TxFreqSpinBox->value () - m_XIT);

  enable_DXCC_entity ();  // updates station identity and text window proportions
  if(m_config.monitor_off_at_startup()) m_monitoroff=true;

  // this must be done before initializing the mode as some modes need
  // to turn off split on the rig e.g. WSPR
  m_config.transceiver_online ();

  ui->TxMinuteButton->setChecked(m_txFirst);
  setMinButton();
  morse_(const_cast<char *> (m_config.my_callsign ().toLatin1().constData()),
         const_cast<int *> (icw), &m_ncw, m_config.my_callsign ().length());
  on_actionWide_Waterfall_triggered();
  m_wideGraph->setTol(500);
  m_wideGraph->setLockTxFreq(m_lockTxFreq);
  m_wideGraph->setMode(m_mode);
  m_wideGraph->setTopJT65(m_config.ntopfreq65());
  m_wideGraph->setModeTx(m_modeTx);
  
  minuteTimer.start (ms_minute_error (m_jtdxtime) + 60 * 1000);
  
  setupJtty();
  setupBlacklist();
  setupUpstream();
  setupSignoff();
  if(m_mode=="FT8") on_actionFT8_triggered();
  else if(m_mode=="FT4") on_actionFT4_triggered();
  else if(m_mode=="FT2") on_actionFT2_triggered();
  else if(m_mode=="JTTY") on_actionJTTY_triggered();
  else if(m_mode=="JT9+JT65") on_actionJT9_JT65_triggered();
  else if(m_mode=="JT9") on_actionJT9_triggered();
  else if(m_mode=="JT65") on_actionJT65_triggered();
  else if(m_mode=="T10") on_actionT10_triggered();
  else if(m_mode=="WSPR-2") on_actionWSPR_2_triggered();
  else if(upstreamMode()) selectUpstreamMode(m_mode);

  if(m_mode!="FT8") { ui->actionEnable_hound_mode->setChecked(false); ui->actionEnable_hound_mode->setEnabled(false); }

  if (m_tci) Q_EMIT m_config.transceiver_trfrequency(ui->TxFreqSpinBox->value () - m_XIT);
  else Q_EMIT transmitFrequency (ui->TxFreqSpinBox->value () - m_XIT);
  ui->actionCallPriorityAndSearchCQ->setEnabled(m_callMode==2 || m_callMode==3);
  m_callPrioCQ=ui->actionCallPriorityAndSearchCQ->isChecked() && (m_callMode==2 || m_callMode==3);
  m_maxDistance=ui->actionMaxDistance->isChecked();
  m_answerWorkedB4=ui->actionAnswerWorkedB4->isChecked();
  m_callWorkedB4=ui->actionCallWorkedB4->isChecked();
  m_callHigherNewCall=ui->actionCallHigherNewCall->isChecked();
  m_singleshot=ui->actionSingleShot->isChecked();
  m_autofilter=ui->actionAutoFilter->isChecked();
  if(ui->actionEnable_hound_mode->isChecked()) on_actionEnable_hound_mode_toggled(true);
  m_wideGraph->setFilter(m_filter);
  ui->actionUse_TX_frequency_jumps->setEnabled(false);
  m_showHarmonics=ui->actionShow_messages_decoded_from_harmonics->isChecked();
  m_showMyCallMsgRxWindow=ui->actionMyCallRXFwindow->isChecked();
  m_showWantedCallRxWindow=ui->actionWantedCallRXFwindow->isChecked();
  m_FT8EarlyStart=ui->actionFT8EarlyStart->isChecked();
  m_bypassRxfFilters=ui->actionBypass_text_filters_on_RX_frequency->isChecked();
  m_bypassAllFilters=ui->actionBypass_all_text_filters->isChecked();
  m_windowPopup=ui->actionEnable_main_window_popup->isChecked();
  m_autoErase=ui->actionAutoErase->isChecked();
  m_autoEraseBC=ui->actionEraseWindowsAtBandChange->isChecked();

  m_rprtPriority=ui->actionReport_message_priority->isChecked();

  ui->sbTxPercent->setValue(m_pctx);
  ui->TxPowerComboBox->setCurrentIndex(int(0.3*(m_dBm + 30.0)+0.2));
  ui->cbUploadWSPR_Spots->setChecked(m_uploadSpots);
  
//  ui->pbTxLock->setChecked(m_lockTxFreq);
//  on_pbTxLock_clicked(m_lockTxFreq);

  if(m_mode.left(4)=="WSPR" and m_pctx>0)  {
    QPalette palette {ui->sbTxPercent->palette ()};
    palette.setColor(QPalette::Base,Qt::yellow);
    ui->sbTxPercent->setPalette(palette);
  }

  setStopHSym();
  
  progressBar->setMaximum(int(m_TRperiod));
  if (m_tci) Q_EMIT m_config.transceiver_period(m_TRperiod);
  else Q_EMIT audioPeriod(m_TRperiod);
  connect( wsprNet, SIGNAL(uploadStatus(QString)), this, SLOT(uploadResponse(QString)));

//  setting up CQ message on init
  m_myCallCompound=(!m_config.my_callsign().isEmpty() && m_config.my_callsign().contains("/")); // && !m_config.my_callsign().endsWith("/P") && !m_config.my_callsign().endsWith("/R"));
  genStdMsgs(m_rpt);
  ui->txrb6->setChecked(true); ui->genMsg->setText(ui->tx6->text());
  if (0 == txTabIndex()) { m_ntx=6; }
  else if (1 == txTabIndex()) { m_ntx=7; }
  m_nlasttx = 6; m_QSOProgress = CALLING;
  ui->rbGenMsg->setChecked(true);
//
  if (!m_mode.startsWith ("WSPR")) countQSOs ();
  if(!ui->cbMenus->isChecked()) { ui->cbMenus->setChecked(true); ui->cbMenus->setChecked(false); }
  if(!ui->cbShowWanted->isChecked()) { ui->cbShowWanted->setChecked(true); ui->cbShowWanted->setChecked(false); }
  m_oldmode=m_mode;
  mode_label->setText(m_mode);
  m_lastloggedtime=m_jtdxtime->currentDateTimeUtc2().addSecs(-7*int(m_TRperiod));
  QFile f0 {m_dataDir.absoluteFilePath ("CALL3.TXT")};
  if(!f0.exists()) { 
  QFile f1 {m_config.data_dir ().absoluteFilePath ("CALL3.TXT")};
  f1.copy(m_dataDir.absoluteFilePath ("CALL3.TXT"));
  }
  m_lastDisplayFreq=m_lastMonitoredFrequency;
  m_bMyCallStd=stdCall(m_config.my_callsign ());
  if(!m_config.my_callsign().isEmpty()) toggle_skipTx1();
  ui->spotMsgLabel->setVisible(false); ui->spotEditLabel->setVisible(false);  ui->spotLineEdit->setVisible(false); ui->propEditLabel->setVisible(false); ui->propLineEdit->setVisible(false);
  ui->genStdMsgsPushButton->click ();
  ui->spotMsgLabel->setTextFormat(Qt::PlainText);
  m_mslastTX = m_jtdxtime->currentMSecsSinceEpoch2();
  m_multInst=m_multiple;
  foxgen_();

// make sure TX hash tables are filled in at first message transmission
  char message[38]="CQ QQ1QQQ KO85                       ";char msgsent[38]; char ft8msgbits[77];
  int i3=0; int n3=0; int ntxhash=1;
  genft8_(message,&i3,&n3,&ntxhash,msgsent,const_cast<char *> (ft8msgbits),const_cast<int *> (itone),37,37);

  m_bHisCallStd=stdCall(m_hisCall); styleChanged();
  configureMcp();
  // this must be the last statement of constructor
  if (!m_valid) throw std::runtime_error {"Fatal initialization exception"};
}

//--------------------------------------------------- MainWindow destructor
MainWindow::~MainWindow()
{
  drainSignoffLogUpdates();
  m_logImportWatcher.waitForFinished();
  m_logLoadWatcher.waitForFinished();
  m_wav_future_watcher.waitForFinished();
  m_saveWAVWatcher.waitForFinished();
  QString fname {QDir::toNativeSeparators(m_dataDir.absoluteFilePath ("wsjtx_wisdom.dat"))};
  QByteArray cfname=fname.toLocal8Bit();
  fftwf_export_wisdom_to_filename(cfname);
  {
    int nfft {-1};
    int ndim {1};
    int isign {1};
    int iform {1};
    // free FFT plan resources
    four2a_ (nullptr, &nfft, &ndim, &isign, &iform, 0);
  }
  fftwf_forget_wisdom ();
  fftwf_cleanup ();
  m_audioThread.quit ();
  m_audioThread.wait ();
  remove_child_from_event_filter (this);
}

//-------------------------------------------------------- writeSettings()
void MainWindow::writeSettings()
{
  m_settings->beginGroup("MainWindow");
  m_settings->setValue("geometry",saveGeometry ());
  m_settings->setValue("state",saveState ());
  m_settings->setValue("vertSplitter",ui->splitter->saveState());
  m_settings->setValue("MRUdir",m_path);
  m_settings->setValue("TxFirst",m_txFirst);
  m_settings->setValue("RRR/RR73",m_rrr);
  m_settings->setValue("CQdirection",m_cqdir);
  m_settings->setValue("DXcall",ui->dxCallEntry->text());
  m_settings->setValue("DXgrid",ui->dxGridEntry->text());
  m_settings->setValue("WantedCallCommaList",ui->wantedCall->text());
  m_settings->setValue("WantedCountryCommaList",ui->wantedCountry->text());
  m_settings->setValue("WantedPrefixCommaList",ui->wantedPrefix->text());
  m_settings->setValue("WantedGridCommaList",ui->wantedGrid->text());
  m_settings->setValue("FreeText",ui->freeTextMsg->currentText ());
  m_settings->setValue("ShowMenus",ui->cbMenus->isChecked());
  m_settings->setValue("ShowWanted",ui->cbShowWanted->isChecked());
  m_settings->endGroup();

  m_settings->beginGroup("Common");
  m_settings->setValue("FT8threads",m_ft8threads);
  m_settings->setValue("AcceptUDPReplyMessages",m_acceptUDP);
  m_settings->setValue("Mode",m_mode);
  m_settings->setValue("ModeTx",m_modeTx);
  m_settings->setValue("SaveWav",m_saveWav);
  m_settings->setValue("Language",m_lang);
  m_settings->setValue("CallMode",m_callMode);
  m_settings->setValue("CallPriorityCQ",ui->actionCallPriorityAndSearchCQ->isChecked());
  m_settings->setValue("MaxDistance",ui->actionMaxDistance->isChecked());
  m_settings->setValue("AnswerWorkedB4",ui->actionAnswerWorkedB4->isChecked());
  m_settings->setValue("CallWorkedB4",ui->actionCallWorkedB4->isChecked());
  m_settings->setValue("CallHigherNewCall",ui->actionCallHigherNewCall->isChecked());
  m_settings->setValue("SingleShotQSO",ui->actionSingleShot->isChecked());
  m_settings->setValue("AutoFilter",ui->actionAutoFilter->isChecked());
  m_settings->setValue("EnableHoundMode",ui->actionEnable_hound_mode->isChecked());  
  m_settings->setValue("ShowHarmonics",ui->actionShow_messages_decoded_from_harmonics->isChecked());
  m_settings->setValue("HideFTContestMessages",ui->actionHide_FT_contest_messages->isChecked());
  m_settings->setValue("HideTelemetryMessages",ui->actionHide_telemetry_messages->isChecked());
  m_settings->setValue("HideFT8Dupes",ui->actionHide_FT8_dupe_messages->isChecked());
  m_settings->setValue("ShowMyCallMessagesRxWindow",ui->actionMyCallRXFwindow->isChecked());
  m_settings->setValue("ShowWantedCallMessagesRxWindow",ui->actionWantedCallRXFwindow->isChecked());
  m_settings->setValue("FT8Sensitivity",m_ft8Sensitivity);
  m_settings->setValue("ModernFT8Decode",ui->actionModernFT8Decode->isChecked());
  m_settings->setValue("ModernFT4Decode",ui->actionModernFT4Decode->isChecked());
  m_settings->setValue("FT8DecoderEarlyStart",ui->actionFT8EarlyStart->isChecked());
  m_settings->setValue("FT8WideDXCallSearch",m_FT8WideDxCallSearch);
  m_settings->setValue("BypassRXFreqTextFilters",ui->actionBypass_text_filters_on_RX_frequency->isChecked());
  m_settings->setValue("BypassAllTextFilters",ui->actionBypass_all_text_filters->isChecked());
  m_settings->setValue("EnableMainwindowPopup",ui->actionEnable_main_window_popup->isChecked());
  m_settings->setValue("AutoErase",ui->actionAutoErase->isChecked());
  m_settings->setValue("EraseWindowsAtBandChange",ui->actionEraseWindowsAtBandChange->isChecked());
  m_settings->setValue("ReportMessagePriority",ui->actionReport_message_priority->isChecked());
  m_settings->setValue("NDepth",m_ndepth);
  m_settings->setValue("NCandidateListThinning",m_ncandthin);
  qint32 nDTcenter=100 * ui->DTCenterSpinBox->value();
  m_settings->setValue("DTCenterInt",nDTcenter);
  m_settings->setValue("NFT8Cycles",m_nFT8Cycles);
  m_settings->setValue("NFT8SWLCycles",m_nFT8SWLCycles);
  m_settings->setValue("NFT8QSORXfreqSensitivity",m_nFT8RXfSens);
  m_settings->setValue("NFT4Depth",m_nFT4depth);
  m_settings->setValue("SwitchFilterOff",m_FilterState);
  m_settings->setValue("RxFreq",ui->RxFreqSpinBox->value());
  m_settings->setValue("TxFreq",ui->TxFreqSpinBox->value());
  m_settings->setValue("WSPRfreq",ui->WSPRfreqSpinBox->value());
  m_settings->setValue("DialFreq",QVariant::fromValue(m_lastMonitoredFrequency));
  m_settings->setValue("OutAttenuation",ui->outAttenuation->value ());
  m_settings->setValue("GUItab",ui->tabWidget->currentIndex());
  m_settings->setValue("TxMessageTab",m_txMessageTab);
  m_settings->setValue("LockTxFreq",m_lockTxFreq);
  m_settings->setValue("SkipTx1", m_skipTx1);
  m_settings->setValue("PctTx",m_pctx);
  m_settings->setValue("dBm",m_dBm);
  m_settings->setValue("UploadSpots",m_uploadSpots);
  m_settings->setValue("BandHopping",ui->band_hopping_group_box->isChecked ());
  m_settings->setValue("pwrBandTxMemory",m_pwrBandTxMemory);
  m_settings->setValue("pwrBandTuneMemory",m_pwrBandTuneMemory);
  m_settings->setValue("SWLMode",m_swl);
  m_settings->setValue("Filter",m_filter);
  m_settings->setValue("AGCcompensation",m_agcc);
  m_settings->setValue("Hint",m_hint);
  m_settings->setValue("73TxDisable",m_disable_TX_on_73);
  m_settings->setValue("ShowMainWindowTooltips",m_showTooltips);
  m_settings->setValue("ColorTxMessageButtons",m_colorTxMsgButtons);
  m_settings->setValue("CallsignToClipboard",m_callToClipboard);
  m_settings->setValue("Crossband160mJA",m_crossbandOptionEnabled);
  m_settings->setValue("Crossband160mHL",m_crossbandHLOptionEnabled);
  m_settings->setValue("QuickCall",m_autoTx);
  m_settings->setValue("SuperFox",m_superFoxMode);
  m_settings->setValue("SuperFoxTolerance",m_superFoxTolerance);
  m_settings->setValue("AutoSequence",m_autoseq);
  m_settings->setValue("SpotText",m_spotText);
  m_settings->setValue("ClearWCallAtLog",ui->cbClearCallsign->isChecked ());
  m_settings->setValue("ClearWGridAtLog",ui->cbClearGrid->isChecked ());
  m_settings->setValue("NoOwnCallWSPR",ui->cbNoOwnCall->isChecked());
  m_settings->setValue("SMeterUnits",ui->S_meter_button->isChecked());
  m_settings->endGroup();
}

//---------------------------------------------------------- readSettings()
void MainWindow::readSettings()
{
  QScopedValueRollback<bool> restoringSettings(m_restoringSettings,true);
  m_settings->beginGroup("MainWindow");
  
  m_geometry = m_settings->value ("geometry",saveGeometry()).toByteArray();
  restoreGeometry(m_geometry);
  restoreState (m_settings->value ("state",saveState ()).toByteArray ());
  ui->splitter->restoreState(m_settings->value("vertSplitter").toByteArray());
  m_path = m_settings->value("MRUdir",m_config.save_directory ().absolutePath ()).toString ();

  m_txFirst = m_settings->value("TxFirst",false).toBool();

  m_rrr = m_settings->value("RRR/RR73",false).toBool();
  ui->rrrCheckBox->setChecked(m_rrr);
  ui->rrr1CheckBox->setChecked(m_rrr);
  if(m_rrr) { ui->pbSendRRR->setText("RRR"); } 
  else { ui->pbSendRRR->setText("RR73"); }
  
  m_cqdir = m_settings->value("CQdirection","").toString();
  if(!m_cqdir.isEmpty()) {
    QRegularExpression cqdir_re("^[A-Za-z]{2,2}$"); QRegularExpressionMatch match = cqdir_re.match(m_cqdir);
    bool hasMatch = match.hasMatch(); if(!hasMatch) m_cqdir="";
  }
  ui->directionLineEdit->setText(m_cqdir);
  if(!m_cqdir.isEmpty ()) { ui->pbCallCQ->setText("CQ " + m_cqdir); }
  else { ui->pbCallCQ->setText("CQ"); }

  ui->dxCallEntry->setText(m_settings->value("DXcall","").toString());
  ui->dxGridEntry->setText(m_settings->value("DXgrid","").toString());
  ui->wantedCall->setText(m_settings->value("WantedCallCommaList","").toString());
  ui->wantedCountry->setText(m_settings->value("WantedCountryCommaList","").toString());
  ui->wantedPrefix->setText(m_settings->value("WantedPrefixCommaList","").toString());
  ui->wantedGrid->setText(m_settings->value("WantedGridCommaList","").toString());

  if(m_settings->contains ("FreeText")) ui->freeTextMsg->setCurrentText (m_settings->value ("FreeText").toString ());

  if(m_settings->value("ShowMenus").toString()=="false") { ui->cbMenus->setChecked(false); on_cbMenus_toggled(false); }
  else { ui->cbMenus->setChecked(true); on_cbMenus_toggled(true); }

  bool wanted=m_settings->value("ShowWanted",false).toBool(); m_wantedchkd=wanted; ui->cbShowWanted->setChecked(wanted);

  m_settings->endGroup();

  // do this outside of settings group because it uses groups internally
  m_settings->beginGroup("Common");

  setFt8Threads(m_settings->value("FT8threads",0).toInt());

  m_acceptUDP=m_settings->value("AcceptUDPReplyMessages",1).toInt(); if(!(m_acceptUDP>=1 && m_acceptUDP<=3)) m_acceptUDP=1;
  if(m_acceptUDP==1) ui->actionAcceptUDPCQ->setChecked(true);
  else if(m_acceptUDP==2) ui->actionAcceptUDPCQ73->setChecked(true);
  else if(m_acceptUDP==3) ui->actionAcceptUDPAny->setChecked(true);

  m_mode=m_settings->value("Mode","FT8").toString();
  m_modeTx=m_settings->value("ModeTx","FT8").toString();

  if(!m_mode.startsWith("FT") && !m_mode.startsWith("JT") && m_mode!="T10" && m_mode!="JTTY" && !m_mode.startsWith ("WSPR")) {
     m_mode="FT8"; m_modeTx="FT8";
  }

  if(!m_modeTx.startsWith("FT") && !m_modeTx.startsWith("JT") && m_modeTx!="T10" && m_modeTx!="JTTY" && !m_modeTx.startsWith ("WSPR")) {
    if(m_mode=="FT8") m_modeTx="FT8";
	else if(m_mode=="FT4") m_modeTx="FT4";
    else if(m_mode=="FT2") m_modeTx="FT2";
    else if(m_mode=="JTTY") m_modeTx="JTTY";
    else if(m_mode=="JT9+JT65") m_modeTx="JT65";
    else if(m_mode=="JT65") m_modeTx="JT65";
    else if(m_mode=="JT9") m_modeTx="JT9";
    else if(m_mode=="T10") m_modeTx="T10";
    else if(m_modeTx.startsWith ("WSPR")) m_modeTx="WSPR-2";
  }
  if(m_modeTx.startsWith("JT9")) ui->pbTxMode->setText("Tx JT9  @");
  if(m_modeTx=="JT65") ui->pbTxMode->setText("Tx JT65  #");

  m_saveWav=m_settings->value("SaveWav",0).toInt();
  if(!(m_saveWav>=0 && m_saveWav<=2)) m_saveWav=0;  
  if(m_saveWav==0) ui->actionNone->setChecked(true);
  else if(m_saveWav==1) ui->actionSave_decoded->setChecked(true);
  else if(m_saveWav==2) ui->actionSave_all->setChecked(true);

  m_lang=m_settings->value("Language","en_US").toString();
  ui->actionEnglish->setText("English");
  ui->actionEstonian->setText("Eesti");
  ui->actionRussian->setText("Русский");
  ui->actionCatalan->setText("Català");
  ui->actionCroatian->setText("Hrvatski");
  ui->actionDanish->setText("Dansk");
  ui->actionDutch->setText("Nederlands");
  ui->actionSpanish->setText("Español");
  ui->actionSwedish->setText("Svenska");
  ui->actionFrench->setText("Français");
  ui->actionItalian->setText("Italiano");
  ui->actionLatvian->setText("Latviski");
  ui->actionHungarian->setText("Magyar");
  ui->actionPolish->setText("Polski");
  ui->actionPortuguese->setText("Português");
  ui->actionPortuguese_BR->setText("Português BR");
  ui->actionChinese_simplified->setText("简体中文");
  ui->actionChinese_traditional->setText("繁體中文");
  ui->actionJapanese->setText("日本語");
  set_language (m_lang);
  
  m_callMode=m_settings->value("CallMode",3).toInt();
  if(!(m_callMode>=0 && m_callMode<=3)) m_callMode=2; 
  if(m_callMode==0) ui->actionCallNone->setChecked(true);
  else if(m_callMode==1) ui->actionCallFirst->setChecked(true);
  else if(m_callMode==2) ui->actionCallMid->setChecked(true);
  else if(m_callMode==3) ui->actionCallEnd->setChecked(true);

  if(m_settings->value("CallPriorityCQ").toString()!="false" && m_settings->value("CallPriorityCQ").toString()!="true") 
    ui->actionCallPriorityAndSearchCQ->setChecked(false);
  else ui->actionCallPriorityAndSearchCQ->setChecked(m_settings->value("CallPriorityCQ",false).toBool());

  // Retain an explicit distance preference. New installations and legacy
  // configurations with neither choice selected default to report priority.
  bool preferDistance=m_settings->value("MaxDistance",false).toBool();
  ui->actionMaxDistance->setChecked(preferDistance);
  ui->actionReport_message_priority->setChecked(!preferDistance);
  ui->actionAnswerWorkedB4->setChecked(m_settings->value("AnswerWorkedB4",false).toBool());
  ui->actionCallWorkedB4->setChecked(m_settings->value("CallWorkedB4",false).toBool());
  ui->actionCallHigherNewCall->setChecked(m_settings->value("CallHigherNewCall",false).toBool());
  ui->actionSingleShot->setChecked(m_settings->value("SingleShotQSO",false).toBool());
  ui->actionAutoFilter->setChecked(m_settings->value("AutoFilter",false).toBool());
  ui->actionEnable_hound_mode->setChecked(m_settings->value("EnableHoundMode",false).toBool());

  ui->actionShow_messages_decoded_from_harmonics->setChecked(m_settings->value("ShowHarmonics",false).toBool());

  if(m_settings->value("HideFTContestMessages").toString()!="false" && m_settings->value("HideFTContestMessages").toString()!="true") 
    ui->actionHide_FT_contest_messages->setChecked(false);
  else ui->actionHide_FT_contest_messages->setChecked(m_settings->value("HideFTContestMessages").toBool());

  if(m_settings->value("HideTelemetryMessages").toString()!="false" && m_settings->value("HideTelemetryMessages").toString()!="true") 
    ui->actionHide_telemetry_messages->setChecked(false);
  else ui->actionHide_telemetry_messages->setChecked(m_settings->value("HideTelemetryMessages").toBool());

  ui->actionHide_FT8_dupe_messages->setChecked(m_settings->value("HideFT8Dupes",true).toBool());
  ui->actionMyCallRXFwindow->setChecked(m_settings->value("ShowMyCallMessagesRxWindow",true).toBool());
  ui->actionWantedCallRXFwindow->setChecked(m_settings->value("ShowWantedCallMessagesRxWindow",true).toBool());

  m_ft8Sensitivity=m_settings->value("FT8Sensitivity",1).toInt();
  ui->actionModernFT8Decode->setChecked(m_settings->value("ModernFT8Decode",false).toBool());
  ui->actionModernFT4Decode->setChecked(m_settings->value("ModernFT4Decode",false).toBool());
  if(!(m_ft8Sensitivity>=0 && m_ft8Sensitivity<=2)) m_ft8Sensitivity=0;
  if(m_ft8Sensitivity==0) ui->actionFT8SensMin->setChecked(true);
  else if(m_ft8Sensitivity==1) ui->actionlowFT8thresholds->setChecked(true);
  else if(m_ft8Sensitivity==2) ui->actionFT8subpass->setChecked(true);

  if(m_settings->value("FT8DecoderEarlyStart").toString()!="false" && m_settings->value("FT8DecoderEarlyStart").toString()!="true") 
    ui->actionFT8EarlyStart->setChecked(true);
  else ui->actionFT8EarlyStart->setChecked(m_settings->value("FT8DecoderEarlyStart",false).toBool());

  m_FT8WideDxCallSearch=m_settings->value("FT8WideDXCallSearch",true).toBool();
  ui->actionFT8WidebandDXCallSearch->setChecked(m_FT8WideDxCallSearch);

  ui->actionBypass_text_filters_on_RX_frequency->setChecked(m_settings->value("BypassRXFreqTextFilters",true).toBool());
  ui->actionBypass_all_text_filters->setChecked(m_settings->value("BypassAllTextFilters",false).toBool());
  ui->actionEnable_main_window_popup->setChecked(m_settings->value("EnableMainwindowPopup",false).toBool());
  ui->actionAutoErase->setChecked(m_settings->value("AutoErase",false).toBool());
  ui->actionEraseWindowsAtBandChange->setChecked(m_settings->value("EraseWindowsAtBandChange",true).toBool());

  m_ndepth=m_settings->value("NDepth",3).toInt(); if(!(m_ndepth>=1 && m_ndepth<=3)) m_ndepth=3;
  if(m_ndepth==3) ui->actionDeepestDecode->setChecked(true);
  else if(m_ndepth==2) ui->actionMediumDecode->setChecked(true);
  else if(m_ndepth==1) ui->actionQuickDecode->setChecked(true);

  ui->candListSpinBox->setValue(5); // ensure a change is signaled
  if(m_settings->value("NCandidateListThinning").toInt()>=5 && m_settings->value("NCandidateListThinning").toInt()<=100)
    ui->candListSpinBox->setValue(m_settings->value("NCandidateListThinning",100).toInt());
  else ui->candListSpinBox->setValue(100);

  ui->DTCenterSpinBox->setValue(0.1); // ensure a change is signaled
  qint32 nDTcenter=m_settings->value("DTCenterInt",0).toInt();
  if(nDTcenter>-201 && nDTcenter<251) ui->DTCenterSpinBox->setValue(static_cast<double>(nDTcenter/100.));
  else ui->DTCenterSpinBox->setValue(0.0);

  m_nFT8Cycles=m_settings->value("NFT8Cycles",1).toInt(); if(!(m_nFT8Cycles>=1 && m_nFT8Cycles<=3)) m_nFT8Cycles=1;
  if(m_nFT8Cycles==1) ui->actionDecFT8cycles1->setChecked(true);
  else if(m_nFT8Cycles==2) ui->actionDecFT8cycles2->setChecked(true);
  else if(m_nFT8Cycles==3) ui->actionDecFT8cycles3->setChecked(true);

  m_nFT8SWLCycles=m_settings->value("NFT8SWLCycles",1).toInt(); if(!(m_nFT8SWLCycles>=1 && m_nFT8SWLCycles<=3)) m_nFT8SWLCycles=1;
  if(m_nFT8SWLCycles==1) ui->actionDecFT8SWLcycles1->setChecked(true);
  else if(m_nFT8SWLCycles==2) ui->actionDecFT8SWLcycles2->setChecked(true);
  else if(m_nFT8SWLCycles==3) ui->actionDecFT8SWLcycles3->setChecked(true);

  m_nFT8RXfSens=m_settings->value("NFT8QSORXfreqSensitivity",2).toInt(); if(!(m_nFT8RXfSens>=1 && m_nFT8RXfSens<=3)) m_nFT8RXfSens=1;
  if(m_nFT8RXfSens==1) ui->actionRXfLow->setChecked(true);
  else if(m_nFT8RXfSens==2) ui->actionRXfMedium->setChecked(true);
  else if(m_nFT8RXfSens==3) ui->actionRXfHigh->setChecked(true);
  
  m_nFT4depth=m_settings->value("NFT4Depth",3).toInt(); if(!(m_nFT4depth>=1 && m_nFT4depth<=3)) m_nFT4depth=3;
  if(m_nFT4depth==3) ui->actionFT4deep->setChecked(true);
  else if(m_nFT4depth==2) ui->actionFT4medium->setChecked(true);
  else if(m_nFT4depth==1) ui->actionFT4fast->setChecked(true);
  
  m_FilterState=m_settings->value("SwitchFilterOff",0).toInt(); if(!(m_FilterState>=0 && m_FilterState<=2)) m_FilterState=0;
  if(m_FilterState==0) on_actionAutoFilter_toggled(false);
  else if(m_FilterState==1) { ui->actionSwitch_Filter_OFF_at_sending_73->setChecked(true); on_actionSwitch_Filter_OFF_at_sending_73_triggered(true); }
  else if(m_FilterState==2) { ui->actionSwitch_Filter_OFF_at_getting_73->setChecked(true); on_actionSwitch_Filter_OFF_at_getting_73_triggered(true); }

  ui->RxFreqSpinBox->setValue(200); // ensure a change is signaled
  if(m_settings->value("RxFreq").toInt()>=0 && m_settings->value("RxFreq").toInt()<=5000)
    ui->RxFreqSpinBox->setValue(m_settings->value("RxFreq",1500).toInt());
  else ui->RxFreqSpinBox->setValue(1500);

  ui->WSPRfreqSpinBox->setValue(1400); // ensure a change is signaled
  if(m_settings->value("WSPRfreq").toInt()>=1400 && m_settings->value("WSPRfreq").toInt()<=1600)
    ui->WSPRfreqSpinBox->setValue(m_settings->value("WSPRfreq",1500).toInt());
  else ui->WSPRfreqSpinBox->setValue(1500);

  ui->TxFreqSpinBox->setValue(200); // ensure a change is signaled
  if(m_settings->value("TxFreq").toInt()>=200 && m_settings->value("TxFreq").toInt()<=5000)
    ui->TxFreqSpinBox->setValue(m_settings->value("TxFreq",1500).toInt());
  else ui->TxFreqSpinBox->setValue(1500);

  m_lastMonitoredFrequency = m_settings->value ("DialFreq",
     QVariant::fromValue<Frequency> (default_frequency)).value<Frequency> ();
  // setup initial value of tx attenuator, range 0...450 (0...45dB attenuation)
  if(m_settings->value("OutAttenuation").toInt()>=0 && m_settings->value("OutAttenuation").toInt()<=450)
    m_outAttenuation = m_settings->value ("OutAttenuation", 225).toInt ();

  int n=m_settings->value("GUItab",0).toInt(); if(!(n>=0 && n<=2)) n=0;
  m_txMessageTab=m_settings->value("TxMessageTab",n==1?1:0).toInt();
  if(m_txMessageTab!=0 && m_txMessageTab!=1)m_txMessageTab=0;
  ui->tabWidget->setCurrentIndex(n);

  // V3.1.0 accidentally saved the upstream locked default. Correct it once
  // on upgrade, then preserve the operator's subsequent explicit selection.
  if (!m_settings->value("TxRxSplitDefaultV311", false).toBool()) {
    m_settings->setValue("LockTxFreq", false);
    m_settings->setValue("TxRxSplitDefaultV311", true);
  }
  m_lockTxFreq=m_settings->value("LockTxFreq",false).toBool();

  m_skipTx1=m_settings->value("SkipTx1",false).toBool();
  ui->skipTx1->setChecked(m_skipTx1);
  ui->skipGrid->setChecked(m_skipTx1);

  m_pctx=m_settings->value("PctTx",20).toInt(); if(!(m_pctx>=0 && m_pctx<=100)) m_pctx=20;
  m_dBm=m_settings->value("dBm",37).toInt(); if(!(m_dBm>=-30 && m_dBm<=60)) m_dBm=37;

  m_uploadSpots=m_settings->value("UploadSpots",false).toBool();
  
  ui->band_hopping_group_box->setChecked (m_settings->value ("BandHopping", false).toBool());
  m_pwrBandTxMemory=m_settings->value("pwrBandTxMemory").toHash();
  m_pwrBandTuneMemory=m_settings->value("pwrBandTuneMemory").toHash();

  m_swl=m_settings->value("SWLMode",false).toBool();
  ui->swlButton->setChecked(m_swl);
  
  m_filter=m_settings->value("Filter",false).toBool();
  ui->filterButton->setChecked(m_filter);

  m_agcc=m_settings->value("AGCcompensation",false).toBool();
  ui->AGCcButton->setChecked(m_agcc);
  
  m_hint=m_settings->value("Hint",true).toBool();
  ui->hintButton->setChecked(m_hint);
  
  m_disable_TX_on_73=m_settings->value("73TxDisable",false).toBool();
  ui->actionDisableTx73->setChecked(m_disable_TX_on_73);

  m_showTooltips=m_settings->value("ShowMainWindowTooltips",true).toBool();
  ui->actionShow_tooltips_main_window->setChecked(m_showTooltips);

  m_colorTxMsgButtons=m_settings->value("ColorTxMessageButtons",false).toBool();
  ui->actionColor_Tx_message_buttons->setChecked(m_colorTxMsgButtons);

  m_callToClipboard=m_settings->value("CallsignToClipboard",true).toBool();
  ui->actionCallsign_to_clipboard->setChecked(m_callToClipboard);

  m_crossbandOptionEnabled=m_settings->value("Crossband160mJA",false).toBool();
  ui->actionCrossband_160m_JA->setChecked(m_crossbandOptionEnabled);

  m_crossbandHLOptionEnabled=m_settings->value("Crossband160mHL",true).toBool();
  ui->actionCrossband_160m_HL->setChecked(m_crossbandHLOptionEnabled);

  m_autoTx=m_settings->value("QuickCall",true).toBool();
  ui->AutoTxButton->setChecked(m_autoTx);

  m_superFoxTolerance=qBound(5,m_settings->value("SuperFoxTolerance",75).toInt(),200);
  ui->actionSuperFox->setChecked(m_settings->value("SuperFox",false).toBool());
  m_autoseq=m_settings->value("AutoSequence",false).toBool();
  if (m_autoseq) { clearDXfields(""); enableTab1TXRB(false); }
  ui->AutoSeqButton->setChecked(m_autoseq);
  setAutoSeqButtonStyle(m_autoseq);

  m_spotText=m_settings->value("SpotText","").toString();
  ui->spotLineEdit->setText(m_spotText);

  ui->cbClearCallsign->setChecked (m_settings->value ("ClearWCallAtLog",true).toBool());
  ui->cbClearGrid->setChecked (m_settings->value ("ClearWGridAtLog",true).toBool());
  ui->cbNoOwnCall->setChecked(m_settings->value("NoOwnCallWSPR",false).toBool());

  ui->S_meter_button->setChecked (m_settings->value ("SMeterUnits",true).toBool());

  m_settings->endGroup();
  
  if(m_config.do_snr()) {
    if(ui->S_meter_button->isChecked()) ui->S_meter_button->setText("S0");
    else ui->S_meter_button->setText("dBm");
  }
  else ui->S_meter_button->setText(tr("S meter"));
  ui->S_meter_button->setEnabled(m_config.do_snr());
  dec_data.params.nstophint=1;
  m_nlasttx=0;
  m_delay=0;
}

void MainWindow::setDecodedTextFont (QFont const& font)
{
  ui->decodedTextBrowser->setContentFont (font);
  ui->decodedTextBrowser2->setContentFont (font);
  QFontMetrics fm(font);
  auto style_sheet = "QLabel {" + font_as_stylesheet (font) + '}';
  
  ui->decodedTextLabel->setStyleSheet (ui->decodedTextLabel->styleSheet () + style_sheet);
  ui->decodedTextLabel->setMinimumHeight (fm.height());
  ui->decodedTextLabel->setMaximumHeight (fm.height());
  ui->decodedTextLabel2->setStyleSheet (ui->decodedTextLabel2->styleSheet () + style_sheet);
  ui->decodedTextLabel2->setMinimumHeight (fm.height());
  ui->decodedTextLabel2->setMaximumHeight (fm.height());
  updateGeometry ();
}

void MainWindow::setStopHSym()
{
  m_hsymStop=179;
  if(m_mode=="FT8") {
    if(m_swl) m_hsymStop = 51;
    else if(m_FT8EarlyStart) m_hsymStop = 48;
    else m_hsymStop=49;
  }
  else if(m_mode=="FT4") m_hsymStop=21;
  else if(m_mode=="FT2") m_hsymStop=11;
  else if(m_mode.startsWith("JT") or m_mode=="T10") { m_hsymStop=173; if(m_config.decode_at_52s()) m_hsymStop=179; }
  else if(m_mode.startsWith ("WSPR")) m_hsymStop=396;
}

// init labUTC clock stylesheet at SW start and operation
void MainWindow::setClockStyle(bool reset)
{
  QDateTime t = m_jtdxtime->currentDateTimeUtc2();
  QString minute = t.time().toString("mm");
  QString second = t.time().toString("ss");
  QString secms = t.time().toString("ss.zzz");
  secms.remove(2,1); int ft4int = secms.toInt()/(m_mode=="FT2"?3750:7500);

  if(m_start || reset) {
    if(m_mode.startsWith("FT")) {
      if(m_mode=="FT8") {
		int isecond = second.toInt();
		if((isecond >= 0 &&  isecond < 15) || (isecond >= 30 &&  isecond < 45)) ui->labUTC->setStyleSheet(QString("font-size: 18pt;background: %1;color : %2").arg(Radio::convert_dark("#96ffff",m_useDarkStyle),Radio::convert_dark("#1400b1",m_useDarkStyle)));
        else ui->labUTC->setStyleSheet(QString("font-size: 18pt;background: %1;color: %2").arg(Radio::convert_dark("#ffff96",m_useDarkStyle),Radio::convert_dark("#0000ff",m_useDarkStyle)));
      }
      else if(m_mode=="FT4" || m_mode=="FT2") {
        if(ft4int%2==0) ui->labUTC->setStyleSheet(QString("font-size: 18pt;background: %1;color : %2").arg(Radio::convert_dark("#96ffff",m_useDarkStyle),Radio::convert_dark("#1400b1",m_useDarkStyle)));
        else ui->labUTC->setStyleSheet(QString("font-size: 18pt;background: %1;color: %2").arg(Radio::convert_dark("#ffff96",m_useDarkStyle),Radio::convert_dark("#0000ff",m_useDarkStyle)));
      }
    }
    else if(!m_mode.startsWith ("WSPR")) {
      if((minute.toInt())%2==0)	ui->labUTC->setStyleSheet(QString("font-size: 18pt;background: %1;color : %2").arg(Radio::convert_dark("#96ffff",m_useDarkStyle),Radio::convert_dark("#1400b1",m_useDarkStyle)));
      else ui->labUTC->setStyleSheet(QString("font-size: 18pt;background: %1;color: %2").arg(Radio::convert_dark("#ffff96",m_useDarkStyle),Radio::convert_dark("#0000ff",m_useDarkStyle)));
    }

    if(m_mode.startsWith ("WSPR")) {
      ui->labUTC->setStyleSheet(QString("font-size: 18pt;background: %1;color: %2").arg(Radio::convert_dark("#ffff96",m_useDarkStyle),Radio::convert_dark("#0000ff",m_useDarkStyle)));
      ui->TxMinuteButton->setText("N/A");
      ui->TxMinuteButton->setStyleSheet(QString("background: %1").arg(Radio::convert_dark("#d2d2d2",m_useDarkStyle))); 
    }
    m_start=false;
  }
  else {
    if(m_mode=="FT8") {
      if(second=="00" || second=="30") ui->labUTC->setStyleSheet(QString("font-size: 18pt;background: %1;color : %2").arg(Radio::convert_dark("#96ffff",m_useDarkStyle),Radio::convert_dark("#1400b1",m_useDarkStyle)));
	  else if(second=="15" || second=="45") ui->labUTC->setStyleSheet(QString("font-size: 18pt;background: %1;color: %2").arg(Radio::convert_dark("#ffff96",m_useDarkStyle),Radio::convert_dark("#0000ff",m_useDarkStyle)));
	}
    else if(m_mode=="FT4" || m_mode=="FT2") {
        if(ft4int%2==0) ui->labUTC->setStyleSheet(QString("font-size: 18pt;background: %1;color : %2").arg(Radio::convert_dark("#96ffff",m_useDarkStyle),Radio::convert_dark("#1400b1",m_useDarkStyle)));
        else ui->labUTC->setStyleSheet(QString("font-size: 18pt;background: %1;color: %2").arg(Radio::convert_dark("#ffff96",m_useDarkStyle),Radio::convert_dark("#0000ff",m_useDarkStyle)));
    }
	else if((m_mode.startsWith("JT") || m_mode=="T10") && second=="00") {
		if((minute.toInt())%2==0) {
			ui->labUTC->setStyleSheet(QString("font-size: 18pt;background: %1;color : %2").arg(Radio::convert_dark("#96ffff",m_useDarkStyle),Radio::convert_dark("#1400b1",m_useDarkStyle)));
        } else {
			ui->labUTC->setStyleSheet(QString("font-size: 18pt;background: %1;color: %2").arg(Radio::convert_dark("#ffff96",m_useDarkStyle),Radio::convert_dark("#0000ff",m_useDarkStyle)));
        }
    }
  }
}

void MainWindow::setAutoSeqButtonStyle(bool checked) {
  if(checked) {
    if(m_singleshot) { ui->AutoSeqButton->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: solid;border-width: 1px;border-radius: 5px;border-color: %3;min-width: 5em;padding: 3px}").arg(Radio::convert_dark("#0000ff",m_useDarkStyle),Radio::convert_dark("#00ff00",m_useDarkStyle),Radio::convert_dark("#000000",m_useDarkStyle))); }
    else { ui->AutoSeqButton->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: solid;border-width: 1px;border-radius: 5px;border-color: %3;min-width: 5em;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#00ff00",m_useDarkStyle),Radio::convert_dark("#000000",m_useDarkStyle))); }
  } else {
    if(m_singleshot) {
      if(m_mode.startsWith("FT")) { ui->AutoSeqButton->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: solid;border-width: 1px;border-color: %3;min-width: 5em;padding: 3px}").arg(Radio::convert_dark("#0000ff",m_useDarkStyle),Radio::convert_dark("#ffbbbb",m_useDarkStyle),Radio::convert_dark("#808080",m_useDarkStyle))); }
      else { ui->AutoSeqButton->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: solid;border-width: 1px;border-color: %3;min-width: 5em;padding: 3px}").arg(Radio::convert_dark("#0000ff",m_useDarkStyle),Radio::convert_dark("#e0e0e0",m_useDarkStyle),Radio::convert_dark("#808080",m_useDarkStyle))); }}
    else { 
      if(m_mode.startsWith("FT")) { ui->AutoSeqButton->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: solid;border-width: 1px;border-color: %3;min-width: 5em;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#ffbbbb",m_useDarkStyle),Radio::convert_dark("#808080",m_useDarkStyle))); }
	  else { ui->AutoSeqButton->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: solid;border-width: 1px;border-color: %3;min-width: 5em;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#e0e0e0",m_useDarkStyle),Radio::convert_dark("#808080",m_useDarkStyle))); }
    }
  }
}

// set text on TX even/odd minute button
void MainWindow::setMinButton()
{
  if(!m_mode.startsWith ("WSPR")) {
	if(m_txFirst) {
	  if(m_mode.startsWith("FT")) {
		if(m_mode=="FT8") ui->TxMinuteButton->setText("TX 00/30");
		else ui->TxMinuteButton->setText("TX 00");
      }
      else ui->TxMinuteButton->setText(tr("TX Even"));
      ui->TxMinuteButton->setStyleSheet(QString("QPushButton {background: %1}").arg(Radio::convert_dark("#96ffff",m_useDarkStyle)));
    } else {
	  if(m_mode.startsWith("FT")) {
		if(m_mode=="FT8") ui->TxMinuteButton->setText("TX 15/45");
        else ui->TxMinuteButton->setText(m_mode=="FT2"?"TX 3.75":"TX 7.5");
      } 
      else ui->TxMinuteButton->setText(tr("TX Odd"));
      ui->TxMinuteButton->setStyleSheet(QString("QPushButton {background: %1}").arg(Radio::convert_dark("#ffff96",m_useDarkStyle)));
    }
  } 
  else {
	ui->TxMinuteButton->setText("N/A");
	ui->TxMinuteButton->setStyleSheet(QString("QPushButton {background: %1}").arg(Radio::convert_dark("#bebebe",m_useDarkStyle)));
  }
}

void MainWindow::autoStopTx(QString reason)
{
//prevent AF RX frequency jumps since QSO is finished and prevent unexpected Halt Tx in autologging mode
//  if(m_config.clear_DX () || m_config.autolog()) clearDX ();
  if(m_enableTx || m_transmitting || m_btxok || g_iptt==1) haltTx(reason);
//prevent any possible sequence breaking with this callsign at the next QSO attempt
  if(m_skipTx1 && reason.endsWith ("counter triggered ") && (m_ntx==2 || m_QSOProgress==REPORT)) m_qsoHistory.remove(m_hisCall);
//prevent AF RX frequency jumps since QSO is finished and prevent unexpected Halt Tx in autologging mode
  if((m_config.clear_DX () || m_config.autolog()) && !m_hisCall.isEmpty() && !m_houndMode) clearDX (" cleared from autoStopTx()");
}

void MainWindow::writeHaltTxEvent(QString reason)
{
  bool haltTrans=false;
  if(!m_transmitting && g_iptt==1) haltTrans=true;
  if(m_config.write_decoded_debug()) {
    QFile f {m_config.log_directory().absoluteFilePath (m_jtdxtime->currentDateTimeUtc2().toString("yyyyMM_")+"ALL.TXT")};
    if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
       QTextStream out(&f);
       if(m_transmitting) {
          out << m_jtdxtime->currentDateTimeUtc2().toString("yyyyMMdd_hhmmss.zzz") << "(" << m_jtdxtime->GetOffset() << ")"
              << "  Halt Tx triggered at TX: " << reason << qSetRealNumberPrecision (12) << (m_freqNominal / 1.e6)
              << " MHz  " << m_modeTx
              << ":  " << m_currentMessage <<
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                 endl;
#else
                 Qt::endl;
#endif

       } else {
          if(!haltTrans) {
             out << m_jtdxtime->currentDateTimeUtc2().toString("yyyyMMdd_hhmmss.zzz") << "(" << m_jtdxtime->GetOffset() << ")"
                 << "  Halt Tx triggered at RX: " << reason <<
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                 endl;
#else
                 Qt::endl;
#endif

          } else {			  
             out << m_jtdxtime->currentDateTimeUtc2().toString("yyyyMMdd_hhmmss.zzz") << "(" << m_jtdxtime->GetOffset() << ")"
                 << "  Halt Tx triggered at transition from RX to TX: " << reason <<
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                 endl;
#else
                 Qt::endl;
#endif

          }
       }
       f.close();
    } else {
       JTDXMessageBox::warning_message (this, "", tr ("File Open Error")
                                    , tr ("Cannot open \"%1\" for append: %2")
                                    .arg (f.fileName ()).arg (f.errorString ()));
    }
  }
}

//-------------------------------------------------------------- dataSink()
void MainWindow::dataSink(qint64 frames)
{
  static float s[NSMAX];
  static int ihsym=0;
  static int ihsymlast=52;
  static int nlostaudio=0;
  static int trmin;
  static int npts8;
  static float px=0.0;
  static float df3;
  static QDateTime last {m_jtdxtime->currentDateTimeUtc2().addSecs(-300)};
  static bool lastdelayed {false};

  if(m_diskData) dec_data.params.ndiskdat=1; else dec_data.params.ndiskdat=0;

// Get power, spectrum, and ihsym
  trmin=m_TRperiod/60.0;
  int k (frames);
  dec_data.params.nfa=m_wideGraph->nStartFreq();
  dec_data.params.nfb=m_wideGraph->Fmax();
  int nsps=m_nsps;
  {
    QMutexLocker lock(&captureBufferMutex());
    if(k<0 || k>dec_data.params.kin)return; // Obsolete queued notification after a period reset.
    symspec_(&dec_data,&k,&trmin,&nsps,&px,s,&df3,&ihsym,&npts8);
    if(m_mode=="WSPR-2")wspr_downsample_(dec_data.d2,&k);
  }
  if(ihsym <=0) return;
//  printf("%s(%0.1f) dataSink %s %d %d\n",m_jtdxtime->currentDateTimeUtc2().toString("hh:mm:ss.zzz").toStdString().c_str(),m_jtdxtime->GetOffset(),last.toString("hh:mm:ss.zzz").toStdString().c_str(),ihsym,k);
  QString t;
  t = QString::asprintf(" Rx noise: %5.1f ",px);
  ui->signal_meter_widget->setValue(px); // Update thermometer
  if(m_monitoring || m_diskData) {
    m_wideGraph->dataSink2(s,df3,ihsym,m_diskData);
  }
  if(m_mode=="JTTY" || upstreamMode())return; // Continuous audio is decoded by the isolated JTTY process.
  setStopHSym();
  if(ihsym==3*m_hsymStop/4) m_dialFreqRxWSPR=m_freqNominal;
  int nhsymEStopFT8 = m_hsymStop;
  if(m_diskData || m_mode.startsWith("WSPR")) m_delay=0;
  // Each WAV is an independent decode request. A delayed live period can
  // leave this guard armed, including across a mode change. Applying its
  // wall-clock timeout to the next file silently skips that file.
  if(m_diskData)lastdelayed=false;

// let Win users to decode some intervals if some frames were dropped in system audio
//#if defined(Q_OS_WIN)
  if(!m_diskData && m_mode=="FT8") {
    if(ihsym==1) nlostaudio=52-ihsymlast;
    ihsymlast=ihsym;
    if(nlostaudio > 0) {
//printf("%s lost audio blocks %d \n",m_jtdxtime->currentDateTimeUtc2().toString("hh:mm:ss").toStdString().c_str(),nlostaudio);
      quint64 timedelta = m_jtdxtime->currentMSecsSinceEpoch2() - m_mslastMon;
      if(timedelta > 14990) {
        if(nlostaudio < 3) ui->label_6->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#ffff00",m_useDarkStyle)));
        else ui->label_6->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#ff8000",m_useDarkStyle)));
        ui->label_6->setText(tr("lost audio ")+QString::number(nlostaudio));
        if(m_config.write_decoded_debug()) writeToALLTXT("Lost audio blocks: " + QString::number(nlostaudio));
      }
      else ui->label_6->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#fdedc5",m_useDarkStyle)));
      nlostaudio=0; m_lostaudio=true;
    }
    if(!m_diskData && m_mode=="FT8" && ihsym>45 && ihsym<nhsymEStopFT8 && m_delay==0) {
      QDateTime now1 {m_jtdxtime->currentDateTimeUtc2 ()};
      quint64 n1=now1.toMSecsSinceEpoch()%15000;
      if(n1>14735) {
        if(m_swl) ihsym=51; 
        else if(m_FT8EarlyStart) ihsym=48;
        else ihsym=49;
      } 
    }
  }
//#endif

  int ihsymdelay=0;
  if(m_delay > 0) {
  float fdelta=float(m_delay)*0.345; // 1/(0.29*10)
  if(fmod(fdelta,1.0)>0.49) ihsymdelay=qCeil(fdelta)+ihsym;
  else ihsymdelay=qFloor(fdelta)+ihsym;
  }
//cycling approximately once per 269..301 milliseconds
  if((m_mode=="FT8" && m_delay==0 && ihsym == nhsymEStopFT8)
     || (m_mode=="FT8" && m_delay > 0 && ihsymdelay >= nhsymEStopFT8)
     || (m_mode=="FT2" && ihsym == m_hsymStop)
     || (m_mode=="FT4" && m_delay==0 && ihsym == m_hsymStop)
     || (m_mode=="FT4" && m_delay > 0 && ihsymdelay >= m_hsymStop)
     || ((m_mode.startsWith("JT") || m_mode=="T10") && m_delay==0 && ihsym == m_hsymStop)
     || ((m_mode.startsWith("JT") || m_mode=="T10") && m_delay > 0 && ihsymdelay >= m_hsymStop)
     || (m_mode.startsWith("WSPR") && ihsym == m_hsymStop)) {
    QDateTime now = m_jtdxtime->currentDateTimeUtc2 ();
//prevent dupe decoding
    if(lastdelayed && !m_modeChanged) {
      if(m_mode=="FT8" && last.secsTo(now)<12) { lastdelayed=false; return; }
      else if(m_mode=="FT2" && last.msecsTo(now)<3000) { lastdelayed=false; return; }
      else if(m_mode=="FT4" && last.secsTo(now)<6) { lastdelayed=false; return; }
      else if(!m_mode.startsWith("FT") && !m_mode.startsWith("WSPR") && last.secsTo(now)<46) { lastdelayed=false; return; }
      lastdelayed=false;
    }
    if(m_delay>0) lastdelayed=true;

    if( m_dialFreqRxWSPR==0) m_dialFreqRxWSPR=m_freqNominal;
    m_dataAvailable=true;
    dec_data.params.npts8=(ihsym*m_nsps)/16;
    dec_data.params.newdat=1;
    dec_data.params.nagain=0;
    dec_data.params.nagainfil=0;	
    dec_data.params.nzhsym=m_hsymStop;
//    m_dateTime = now.toString ("yyyy-MMM-dd hh:mm");

    if(!m_decoderBusy && m_mode.left(4)!="WSPR") { //Start decoder
      last=now; decode(); 
      if(!m_lostaudio) { ui->label_6->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#fdedc5",m_useDarkStyle))); ui->label_6->setText(tr("Band Activity")); }
    }
    m_delay=0;

    if(!m_diskData && (m_saveWav!=0 || m_mode.mid (0,4) == "WSPR")) { //Save unless "Save None"
      if(m_mode.startsWith("FT")) {
        qint64 period=qRound64(m_TRperiod*1000);
        auto period_start=QDateTime::fromMSecsSinceEpoch(now.toMSecsSinceEpoch()-now.toMSecsSinceEpoch()%period,Qt::UTC);
        m_fnameWE=m_config.save_directory().absoluteFilePath (period_start.toString("yyMMdd_hhmmss"));
      } else {
        auto const& period_start = now.addSecs (-(now.time ().minute () % (int(m_TRperiod) / 60)) * 60);
        m_fnameWE=m_config.save_directory ().absoluteFilePath (period_start.toString ("yyMMdd_hhmm"));
      }
      m_fileToSave.clear ();
      int samples=m_TRperiod*12000;
      if(m_mode=="FT4") samples=21*3456;
      if ((m_saveWav==2 || m_saveWav==1 || m_mode.mid (0,4) == "WSPR") && !m_fnameWE.isEmpty ()) {
        if(m_wavSaveBusy) {
          statusBar()->showMessage(tr("Previous audio file is still being saved; this interval was skipped."),10000);
          if(m_mode.startsWith("WSPR")){m_rxDone=true;return;}
        }
        else {
          samples=qBound(0,samples,int(sizeof(dec_data.d2)/sizeof(dec_data.d2[0])));
          QVector<short> snapshot(samples);
          {QMutexLocker lock(&captureBufferMutex());std::copy_n(dec_data.d2,samples,snapshot.data());}
          m_wavSaveBusy=true;
          m_saveWAVWatcher.setFuture(QtConcurrent::run(std::bind(&MainWindow::save_wave_file,
              m_fnameWE,snapshot,m_config.my_callsign(),m_config.my_grid(),m_mode,
              m_freqNominal,m_hisCall,m_hisGrid,m_jtdxtime->currentDateTimeUtc2())));
        }
      }

      if (m_mode.mid (0,4) == "WSPR") {
        QString c2name_string {m_fnameWE + ".c2"};
        auto c2name=c2name_string.toUtf8();
        int nsec=120;
        int nbfo=1500;
        double f0m1500=m_freqNominal/1000000.0 + nbfo - 1500;
        int err = savec2_(c2name.data(),&nsec,&f0m1500,c2name.size());
        if (err!=0) JTDXMessageBox::warning_message (this, "", tr ("Error saving c2 file"), c2name_string);
      }
    }

    if(m_mode.left(4)=="WSPR" && !m_decoderBusy) {
      m_decoderBusy = true;
      QStringList args;
      if(m_ndepth==1)args << "-qB";
      else if(m_ndepth==2)args << "-B";
      else args << "-C" << "5000" << "-o" << "4";
      args << "-a" << QDir::toNativeSeparators(m_dataDir.absolutePath());
      if(!m_diskData)args << "-f" << QString::number(m_dialFreqRxWSPR/1000000.0,'f',6);
      args << QDir::toNativeSeparators(m_diskData?m_path:m_fnameWE+".wav");
      if(ui)ui->DecodeButton->setChecked(true);
      if(m_diskData)p1.start(QDir::toNativeSeparators(m_appDir+"/wsprd_jtdx"),args);
      else m_wsprDecodeArgs=args; // Start only when the WAV snapshot has finished saving.
      statusUpdate ();
    }
    m_rxDone=true;
  }
}

QString MainWindow::save_wave_file (QString const& name, QVector<short> const& data,
        QString const& my_callsign, QString const& my_grid, QString const& mode,
        Frequency frequency, QString const& his_call, QString const& his_grid,QDateTime const& utc)
{
  //
  // This member function runs in a thread and should not access
  // members that may be changed in the GUI thread or any other thread
  // without suitable synchronization.
  //
  QAudioFormat format;
  format.setCodec ("audio/pcm");
  format.setSampleRate (12000);
  format.setChannelCount (1);
  format.setSampleSize (16);
  format.setSampleType (QAudioFormat::SignedInt);
  auto source = QString {"%1, %2"}.arg (my_callsign).arg (my_grid);
  auto comment = QString {"Mode=%1%2, Freq=%3%4"}
     .arg (mode)
     .arg (QString {mode.contains ('J') && !mode.contains ('+')
           ? QString {", Sub Mode="} + QChar {'A' + 0}
         : QString {}})
        .arg (Radio::frequency_MHz_string (frequency))
     .arg (QString {!mode.startsWith ("WSPR") ? QString {", DXCall=%1, DXGrid=%2"}
         .arg (his_call)
         .arg (his_grid).toLocal8Bit () : ""});

  BWFFile::InfoDictionary list_info {
      {{{'I','S','R','C'}}, source.toLocal8Bit ()},
      {{{'I','S','F','T'}}, program_title (revision ()).simplified ().toLocal8Bit ()},
      {{{'I','C','R','D'}}, utc.toUTC()
                          .toString ("yyyy-MM-ddTHH:mm:ss.zzzZ").toLocal8Bit ()},
      {{{'I','C','M','T'}}, comment.toLocal8Bit ()},
  };
  BWFFile wav {format, name + ".wav", list_info};
  if (!wav.open (BWFFile::WriteOnly)
      || qint64(sizeof(short)*data.size()) != wav.write(reinterpret_cast<char const*>(data.constData()),sizeof(short)*data.size()))
    {
      return wav.errorString ();
    }
  return QString {};
}

void MainWindow::showSoundInError(const QString& errorMsg) { on_stopTxButton_clicked(); JTDXMessageBox::critical_message(this, "", tr("Error in SoundInput"), errorMsg); }
void MainWindow::applyRxAudioBufferSize()
{
  auto frames=RxAudioBufferPolicy::select(m_rx_audio_buffer_override,m_config.full_duplex());
  if(frames==m_rx_audio_buffer_frames || m_transmitting || m_tune || g_iptt || m_tx_when_ready
     || ptt0Timer.isActive() || ptt1Timer.isActive() || m_rigState.ptt())return;
  if(!m_tci && !m_config.audio_input_device().isNull())startSoundcardInputStream();
  else m_rx_audio_buffer_frames=frames;
}

void MainWindow::startSoundcardInputStream()
{
  m_rx_audio_buffer_frames=RxAudioBufferPolicy::select(m_rx_audio_buffer_override,m_config.full_duplex());
  Q_EMIT startAudioInputStream(m_config.audio_input_device(),m_rx_audio_buffer_frames,m_detector,m_downSampleFactor,m_config.audio_input_channel());
}

quint64 MainWindow::beginAudioPlayback()
{
  // Managed accounting and signoff must share the same waveform identity.
  if(!m_managedCallOnAir && !m_managedCqOnAir)++m_signoffGeneration;
  m_audioPlaybackGeneration=m_signoffGeneration;
  return m_audioPlaybackGeneration;
}

void MainWindow::showSoundOutError(const QString& errorMsg) { on_stopTxButton_clicked(); JTDXMessageBox::critical_message(this, "", tr("Error in SoundOutput"), errorMsg); }
void MainWindow::showStatusMessage(const QString& statusMsg) { statusBar()->showMessage(statusMsg); }

void MainWindow::on_actionSettings_triggered()               //Setup Dialog
{
  // things that might change that we need know about
  m_strictdirCQ = m_config.strictdirCQ ();
  m_callsign = m_config.my_callsign ();
  m_useDarkStyle = m_config.useDarkStyle();
  m_grid = m_config.my_grid();
  m_callNotif = m_config.callNotif();
  m_gridNotif = m_config.gridNotif();
  m_timeFrom = m_config.timeFrom();
  bool spot_to_dxsummit = m_config.spot_to_dxsummit();

  m_config.set_station_locked(m_enableTx || m_tune || m_transmitting || m_logDlg->isVisible() || logQSOTimer.isActive());
  if (QDialog::Accepted == m_config.exec ()) {
      configureMcp();
      applyGeneralSettings();
      bool const logContextChanged=m_config.my_callsign()!=m_callsign
        || (m_config.gridNotif()?m_config.my_grid():QString())!=(m_gridNotif?m_grid:QString())
        || m_config.timeFrom()!=m_timeFrom;
      importOperatorLogs();
      if(logContextChanged) refreshOperatorLogs(true);
      if(m_config.write_decoded_debug()) writeToALLTXT("Configuration settings change accepted");
      ui->decodedTextBrowser->setConfiguration (&m_config,true);
      ui->decodedTextBrowser2->setConfiguration (&m_config);
      if (m_config.useDarkStyle() != m_useDarkStyle) {
        m_useDarkStyle = m_config.useDarkStyle();
        styleChanged();
      }
      if(m_config.my_callsign()!=m_callsign) applyStationIdentity();
      updateStationSelector();

      on_dxGridEntry_textChanged (m_hisGrid); // recalculate distances in case of units change
      enable_DXCC_entity ();  // updates station identity and text window proportions

      if(m_config.spot_to_psk_reporter ()) {
        pskSetLocal ();
      }
      bool was_monitoring = m_monitoring;
      if (m_monitoring && (m_config.restart_tci () || m_tci != (m_config.tci_audio() && m_config.is_tci()))) on_monitorButton_clicked (false);
      if (!(m_config.tci_audio() && m_config.is_tci())) {
        //here computer soundcard
        if((m_config.restart_audio_input() || m_tci
            || RxAudioBufferPolicy::select(m_rx_audio_buffer_override,m_config.full_duplex())!=m_rx_audio_buffer_frames)
           && !m_config.audio_input_device().isNull() && !m_transmitting && !g_iptt && !m_tx_when_ready)
          startSoundcardInputStream();

        if((m_config.restart_audio_output () || m_tci) && !m_config.audio_output_device ().isNull ()) {
          Q_EMIT initializeAudioOutputStream (m_config.audio_output_device (),
             AudioDevice::Mono == m_config.audio_output_channel () ? 1 : 2, m_tx_audio_buffer_frames);
          if(m_transmitting || g_iptt==1) {
  //           ui->stopTxButton->click (); // halt any transmission
             haltTx("settings change is accepted ");
             enableTx_mode (false);       // switch off EnableTx button
             updateTxButtonAppearance();
             TxAgainTimer.start(2000);
           }
        }
        if (m_tci) {
          Q_EMIT transmitFrequency (ui->TxFreqSpinBox->value () - m_XIT);
          Q_EMIT audioPeriod(m_TRperiod);
        }
      } else {
        if(m_transmitting || g_iptt==1) {
//           ui->stopTxButton->click (); // halt any transmission
           haltTx("settings change is accepted ");
           enableTx_mode (false);       // switch off EnableTx button
           updateTxButtonAppearance();
           TxAgainTimer.start(2000);
         }

        Q_EMIT m_config.transceiver_period(double(NTMAX));
        Q_EMIT m_config.transceiver_trfrequency(ui->TxFreqSpinBox->value () - m_XIT);
        Q_EMIT m_config.transceiver_period(m_TRperiod);
      }

      if (was_monitoring && (m_config.restart_tci () || m_tci != (m_config.tci_audio() && m_config.is_tci())) && !m_monitoring && !m_transmitting && g_iptt!=1 ) {
        m_tci = m_config.tci_audio() && m_config.is_tci();
        on_monitorButton_clicked (true);
      } else m_tci = m_config.tci_audio() && m_config.is_tci();
 
      displayDialFrequency ();

      if(m_mode=="FT8") on_actionFT8_triggered();
      else if(m_mode=="FT4") on_actionFT4_triggered();
  else if(m_mode=="FT2") on_actionFT2_triggered();
  else if(m_mode=="JTTY") on_actionJTTY_triggered();
      else if(m_mode=="JT9+JT65") on_actionJT9_JT65_triggered();
      else if(m_mode=="JT9") on_actionJT9_triggered();
      else if(m_mode=="JT65") on_actionJT65_triggered();
      else if(m_mode=="T10") on_actionT10_triggered();
      else if(m_mode=="WSPR-2") on_actionWSPR_2_triggered();
  else if(upstreamMode()) selectUpstreamMode(m_mode);

	  m_config.transceiver_online ();
	  m_wideGraph->setTopJT65(m_config.ntopfreq65());
	  setXIT (ui->TxFreqSpinBox->value ());
      update_watchdog_label ();
      if(m_mode != "WSPR-2" && spot_to_dxsummit != m_config.spot_to_dxsummit()) {
         if(m_config.spot_to_dxsummit() ) { ui->pbSpotDXCall->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: outset;border-width: 1px;border-color: %3;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#c4c4ff",m_useDarkStyle),Radio::convert_dark("#808080",m_useDarkStyle))); }
         else { ui->pbSpotDXCall->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: outset;border-width: 1px;border-color: %3;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#aabec8",m_useDarkStyle),Radio::convert_dark("#808080",m_useDarkStyle))); }
      }
      if(!m_config.do_snr()) ui->S_meter_button->setText(tr("S meter"));
      ui->S_meter_button->setEnabled(m_config.do_snr());
      if(!m_config.do_pwr()) {ui->PWRlabel->setText(tr("Pwr")); ui->SWRlabel->setText("");}
      on_spotLineEdit_textChanged(ui->spotLineEdit->text());
      ui->bandComboBox->setCurrentText (m_config.bands ()->find (m_freqNominal));
  }
}

void MainWindow::updateStationSelectorEnabled() {
  bool allowed=m_config.station_switch_allowed() && !m_enableTx && !m_tune && !m_transmitting
    && !m_logLoading && !m_logImportActive && !m_logRefreshPending
    && !m_logDlg->isVisible() && !logQSOTimer.isActive();
  if(m_quickStationSelector)m_quickStationSelector->setEnabled(allowed);
  if(m_confirmStationSwitchButton)m_confirmStationSwitchButton->setEnabled(allowed
    && !m_quickStationSelector->currentText().isEmpty()
    && m_quickStationSelector->currentText()!=m_config.my_callsign());
}
void MainWindow::updateStationSelector() {
  QSignalBlocker block(m_quickStationSelector);
  m_quickStationSelector->clear();
  for(auto const& profile:m_config.station_profiles())m_quickStationSelector->addItem(profile.call);
  m_quickStationSelector->setCurrentIndex(m_quickStationSelector->findText(m_config.my_callsign()));
  m_quickStationGrid->setText(m_config.my_grid());
  m_blacklistButton->setEnabled(!m_config.my_callsign().isEmpty());
  updateStationSelectorEnabled();
}
void MainWindow::applyStationIdentity() {
  // A new operator must never inherit the previous operator's TX permission or QSO.
  on_stopTxButton_clicked();
  m_managed.clearCandidates();m_managed.cancelAttempt();m_managedTxReady=false;
  loadBlacklist();
  m_bMyCallStd=stdCall(m_config.my_callsign());
  if(!m_config.my_callsign().isEmpty())toggle_skipTx1();
  m_myCallCompound=!m_config.my_callsign().isEmpty() && m_config.my_callsign().contains('/');
  if(!m_config.my_callsign().isEmpty()) {
    if(m_bMyCallStd) {ui->skipTx1->setEnabled(true);ui->skipGrid->setEnabled(true);}
    else {
      if(m_skipTx1) {m_skipTx1=false;ui->skipTx1->setChecked(false);ui->skipGrid->setChecked(false);}
      ui->skipTx1->setEnabled(false);ui->skipGrid->setEnabled(false);
    }
  }
  m_baseCall=Radio::base_callsign(m_config.my_callsign());
  clearDXfields(QString());genStdMsgs(QString());
  m_QSOProgress=CALLING;m_nlasttx=6;
  if(txTabIndex()==1) {ui->genMsg->setText(ui->tx6->text());m_ntx=7;ui->rbGenMsg->setChecked(true);}
  else {m_ntx=6;ui->txrb6->setChecked(true);}
  m_lastloggedtime=m_jtdxtime->currentDateTimeUtc2().addSecs(-7*int(m_TRperiod));
  m_lastloggedcall.clear();setLastLogdLabel();
  morse_(const_cast<char*>(m_config.my_callsign().toLatin1().constData()),
         const_cast<int*>(icw),&m_ncw,m_config.my_callsign().length());
}
void MainWindow::switchStation(QString const& callsign) {
  if(callsign==m_config.my_callsign())return;
  if(m_enableTx || m_tune || m_transmitting || m_logLoading || m_logImportActive || m_logRefreshPending
     || m_logDlg->isVisible() || logQSOTimer.isActive()
     || !m_config.station_switch_allowed()) {updateStationSelector();return;}
  m_callsign=m_config.my_callsign();m_grid=m_config.my_grid();
  if(!m_config.select_station(callsign)) {updateStationSelector();return;}
  applyStationIdentity();
  enable_DXCC_entity();
  m_config.frequencies()->filter(m_config.region(),Modes::value(m_mode));
  ui->bandComboBox->setCurrentText(m_config.bands()->find(m_freqNominal));
  if(m_config.spot_to_psk_reporter())pskSetLocal();
  importOperatorLogs();refreshOperatorLogs(true);
  updateStationSelector();statusUpdate();
}

void MainWindow::on_filterButton_clicked (bool checked)
{
  if(checked) { m_filter=true; m_wideGraph->setFilter(m_filter); }
  else { dec_data.params.nagainfil=0; m_filter=false; m_wideGraph->setFilter(m_filter); }
  if(m_config.write_decoded_debug()) {
    QString filter = m_filter ? "  Filter button is switched on" : "  Filter button is switched off";
    writeToALLTXT(filter);
  }
}

void MainWindow::toggle_filter() { ui->filterButton->click(); }
void MainWindow::escapeHalt() { haltTx("TX halted via Escape button from widegraph "); }
void MainWindow::filter_on() { if(!m_filter) ui->filterButton->click(); }
void MainWindow::on_swlButton_clicked (bool checked) { if(checked) m_swl=true; else m_swl=false; }
void MainWindow::on_AGCcButton_clicked(bool checked) { if(checked) m_agcc=true; else m_agcc=false; }
void MainWindow::on_hintButton_clicked (bool checked) { m_hint=checked; }
void MainWindow::on_HoundButton_clicked (bool checked) { ui->actionEnable_hound_mode->setChecked(checked);}

void MainWindow::on_AutoTxButton_clicked (bool checked)
{
  m_autoTx = checked;
  if(checked) ui->AutoTxButton->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: solid;border-width: 1px;border-radius: 5px;border-color: %3;min-width: 5em;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#00ff00",m_useDarkStyle),Radio::convert_dark("#000000",m_useDarkStyle)));
  else if(m_autoseq) ui->AutoTxButton->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: solid;border-width: 1px;border-color: %3;min-width: 5em;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#ffbbbb",m_useDarkStyle),Radio::convert_dark("#808080",m_useDarkStyle)));
  else ui->AutoTxButton->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: solid;border-width: 1px;border-color: %3;min-width: 5em;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#e0e0e0",m_useDarkStyle),Radio::convert_dark("#808080",m_useDarkStyle)));
}

void MainWindow::on_AutoSeqButton_clicked (bool checked)
{
  m_autoseq = checked;
  if(!checked)resetOrdinaryBusy();
  if (checked) {
    m_wasAutoSeq=false; //in case of toggling AutoSeq button by user
//txrb button selection by user can brake AutoSeq, disable all txrb buttons
    enableTab1TXRB(false);
    if(txTabIndex()==1) {
      on_rbGenMsg_clicked(true);
      ui->rbGenMsg->setChecked(true);
    }
    if(!m_autoTx) ui->AutoTxButton->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: solid;border-width: 1px;border-color: %3;min-width: 5em;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#ffbbbb",m_useDarkStyle),Radio::convert_dark("#808080",m_useDarkStyle)));
  } else {
    enableTab1TXRB(true);
	if(!m_autoTx) ui->AutoTxButton->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: solid;border-width: 1px;border-color: %3;min-width: 5em;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#e0e0e0",m_useDarkStyle),Radio::convert_dark("#808080",m_useDarkStyle)));
  }
  setAutoSeqButtonStyle(m_autoseq);
}

void MainWindow::on_monitorButton_clicked (bool checked)
{
  if (!m_transmitting)
    {
      auto prior = m_monitoring;
      m_monitoroff = !checked;
      monitor (checked);

      if (checked && !prior)
        {
          if (m_config.monitor_last_used ())
            {
              // put rig back where it was when last in control
              m_freqNominal = m_lastMonitoredFrequency;
              m_freqTxNominal = m_freqNominal;
              setRig ();
              setXIT (ui->TxFreqSpinBox->value ());
            }
        }

      //Get Configuration in/out of strict split and mode checking
      Q_EMIT m_config.sync_transceiver (true, checked);
    }
  else
    {
      ui->monitorButton->setChecked (false); // disallow
    }
}

void MainWindow::monitor (bool state)
{
  if(upstreamMode() && state!=m_monitoring){m_periodAudio.reset();m_mskAudio.clear();}
  if(m_jtty && m_mode=="JTTY" && state!=m_monitoring){m_jtty->reset();m_jttyRxAnchor=0;}
  ui->monitorButton->setChecked (state);
  if (state) {
    m_diskData = false;	// no longer reading WAV files
    if (!m_monitoring) {
      m_mslastMon=m_jtdxtime->currentMSecsSinceEpoch2();
      if (m_tci) Q_EMIT m_config.transceiver_audio(true);
      else Q_EMIT resumeAudioInputStream ();
      QDateTime  currentTime = m_jtdxtime->currentDateTimeUtc2 (); // decode part of interval
      QString curtime=currentTime.toString("ss.zzz");
      curtime.remove(2,1); curtime.remove(3,2);
      int curdsec = curtime.toInt();
      if(m_addtx==-1) m_addtx=2; else if(m_addtx==-2) m_addtx=4; else m_addtx=0; // no delay for manual triggering Monitor button
      if(m_mode == "FT8") {
         curdsec=curdsec%150; if(curdsec > 0 && curdsec < 90) m_delay=curdsec+m_addtx; else m_delay = 0;
      }
      else if(m_mode=="JTTY" || m_mode=="FT2")m_delay=0;
      else if(m_mode == "FT4") {
         curdsec=curdsec%75; if(curdsec > 0 && curdsec < 40) m_delay=curdsec+m_addtx; else m_delay = 0;
      }
	  else if(!m_mode.startsWith("WSPR")) {
         if(curdsec > 0 && curdsec < 350) m_delay=curdsec+m_addtx; else m_delay = 0; // 2 second processing delay
      }
      m_addtx=0;
    }
  } else {
    if (m_tci) Q_EMIT m_config.transceiver_audio(false);
    else Q_EMIT suspendAudioInputStream ();
  }
  m_monitoring = state;
  if(m_monitoring && m_txbColorSet) { resetTxMsgBtnColor(); m_txbColorSet=false; }
}

void MainWindow::on_actionAbout_triggered() { CAboutDlg {this}.exec (); } //Display "About"

void MainWindow::on_enableTxButton_clicked (bool checked)
{
  // Only a physical UI activation may establish the TX session. Internal
  // clicks, UDP, MCP and delayed callbacks may never arm a stopped station.
  if(checked && (m_mode=="FT2" || m_mode=="JTTY") && m_tci) {ui->enableTxButton->setChecked(false);m_manualTxAuthorized=false;statusBar()->showMessage(tr("This mode requires local audio devices; TCI audio is unavailable."),15000);return;}
  if (checked && m_manualTxAuthorized) {
    StationProfile station;station.call=m_config.my_callsign();station.grid=m_config.my_grid();
    if(!StationProfiles::validate(station).isEmpty()) {
      m_manualTxAuthorized=false;
      statusBar()->showMessage(tr("Select a valid callsign and grid in station settings before enabling TX."),15000);
    }
    if(!m_logReady) {
      m_manualTxAuthorized=false;
      statusBar()->showMessage(tr("The current operator's log is not ready. Check the log files and try again."),15000);
    }
    if(permanentlyBlocked(m_hisCall) || messageBlacklisted(selectedTransmitText())) {
      m_manualTxAuthorized=false;
      statusBar()->showMessage(tr("This station is blocked, or the permanent blacklist is unavailable."),15000);
    }
  }
  if (checked && !m_manualTxAuthorized) {
    ui->enableTxButton->setChecked(false);
    return;
  }
  if (!checked) {
    m_ordinaryAutoPending=false;
    resetOrdinaryBusy();
    ++m_signoffGeneration;
    m_signoff.reset();
    flushSignoffLogRefresh();
    logQSOTimer.stop();
    // PCM modes bypass the legacy per-tick TX stop path. Cancel their active
    // or pending transmission immediately when Enable TX is unticked too.
    // The common stop handler revokes manual authority before calling back
    // through enableTx_mode(false), so this delegation cannot recurse.
    if(upstreamMode() && m_manualTxAuthorized) {on_stopTxButton_clicked();return;}
    if(m_mode=="JTTY" && m_jtty) {
      const bool resumeMonitoring=!m_monitoroff && !m_diskData
        && (m_transmitting || g_iptt!=0 || m_tx_when_ready || m_jttyPreparing);
      m_jtty->cancelTransmit();m_jttyPreparing=false;m_jttyPcm.clear();m_jttyPttTimeout.stop();
      Q_EMIT endTransmitMessage(true);Q_EMIT m_config.transceiver_ptt(false);
      m_transmitting=false;g_iptt=0;m_tx_when_ready=false;m_btxok=false;transmitDisplay(false);
      if(resumeMonitoring && !m_monitoring)monitor(true);
    }
    m_manualTxAuthorized = false;
    TxAgainTimer.stop(); enableTxButtonTimer.stop(); ptt1Timer.stop();
  }
  if(m_enableTx && !checked && m_curMsgTx.startsWith(m_hisCall+" ")) m_lasthint=true;
  if(checked && m_lasthint) m_lasthint=false;
  if (checked && !m_enableTx) {
    if(m_managed.active()){m_managed.restartSchedule(m_managedClock.elapsed());m_managedTxReady=false;clearDX(" cleared, managed TX session started");}
    m_txSession.start();
    m_txwatchdog = false;
    m_watchdogFinishing=false;m_watchdogCall.clear();m_watchdogContext.clear();
  } else if (!checked) {
    m_txSession.stop();
    m_watchdogFinishing=false;m_watchdogCall.clear();m_watchdogContext.clear();
  }
  m_enableTx = checked;
  updateJttyUi();
  m_config.set_station_locked(m_enableTx || m_tune || m_transmitting || m_logDlg->isVisible() || logQSOTimer.isActive());
  updateStationSelectorEnabled();
  update_watchdog_label();
  statusUpdate ();
  if(m_mode.left(4)=="WSPR")  {
    QPalette palette {ui->sbTxPercent->palette ()};
    if(m_enableTx or m_pctx==0) { palette.setColor(QPalette::Base,Qt::white); }
    else { palette.setColor(QPalette::Base,Qt::yellow); }
    ui->sbTxPercent->setPalette(palette);
  }
  if(!m_enableTx) {
// sync TX variables 
     if(!m_transmitting) { m_bTxTime=false; m_tx_when_ready=false; m_restart=false; m_txNext=false; }
  }
  updateTxButtonAppearance();
}

void MainWindow::enableTx_mode (bool state) { ui->enableTxButton->setChecked (state); on_enableTxButton_clicked (state); }
void MainWindow::enableTxButton_off () { enableTx_mode (false); }

void MainWindow::keyPressEvent( QKeyEvent *e )                //keyPressEvent
{
  if(handleGeneralKeyEvent(e))return;
  int n;
  switch(e->key())
    {
    case Qt::Key_D:
      if(!m_decoderBusy && m_mode != "WSPR-2") {
         if(e->modifiers() & Qt::ShiftModifier) {
            dec_data.params.newdat=0;
            dec_data.params.nagain=0;
            decode();
            return;
         }
         if(e->modifiers() & Qt::AltModifier) {
            dec_data.params.newdat=0;
            dec_data.params.nagainfil=1;
            decode();
            return;
         }
      }
      break;
    case Qt::Key_A:
      if((e->modifiers() & Qt::AltModifier) && (e->modifiers() & Qt::ControlModifier)) {
        ui->wantedCall->clear();
        return;
      }
      break;
    case Qt::Key_F2:	  
      if(!m_menus) QTimer::singleShot (0, this, SLOT (on_actionSettings_triggered ()));
      return;
    case Qt::Key_F4:
      on_ClearDxButton_clicked();
      ui->dxCallEntry->setFocus();
      return;
    case Qt::Key_F6:
      if(e->modifiers() & Qt::ShiftModifier) { on_actionDecode_remaining_files_in_directory_triggered(); return; }
      else { if(!m_menus) on_actionOpen_next_in_directory_triggered(); }
      return;
    case Qt::Key_F7:	  
      if(!m_menus) on_actionOpen_wsjtx_log_adi_triggered();
      return;
      break;
    case Qt::Key_F11:
      n=11;
      if(e->modifiers() & Qt::ControlModifier) n+=100;
      bumpFqso(n);
      return;
    case Qt::Key_F12:
      n=12;
      if(e->modifiers() & Qt::ControlModifier) n+=100;
      bumpFqso(n);
      return;
    case Qt::Key_E:
      if(e->modifiers() & Qt::ShiftModifier) {
        assignTxSlot(false,true);
        return;
      }
      if(e->modifiers() & Qt::ControlModifier) {
        assignTxSlot(true,true);
        return;
      }
      if(e->modifiers() & Qt::AltModifier && m_lang=="ru_RU") {
        ui->EraseButton->click();
        return;
      }
      break;
    case Qt::Key_F:
      if(e->modifiers() & Qt::ControlModifier) {
        if(txTabIndex()==0) { ui->tx5->clearEditText(); ui->tx5->setFocus(); }
        else { ui->freeTextMsg->clearEditText(); ui->freeTextMsg->setFocus(); }
        return;
      }
      if(e->modifiers() & Qt::AltModifier) {
        if(m_bypassAllFilters) { ui->actionBypass_all_text_filters->setChecked(false); m_bypassAllFilters=false; } 
        else { ui->actionBypass_all_text_filters->setChecked(true); m_bypassAllFilters=true; }
      }
      break;
    case Qt::Key_G:
      if(e->modifiers() & Qt::AltModifier) {
        genStdMsgs(m_rpt);
        return;
      }
      break;
    case Qt::Key_H:
      if(e->modifiers() & Qt::AltModifier) {
        on_stopTxButton_clicked();
        return;
      }
      break;
    case Qt::Key_L:
      if(e->modifiers() & Qt::ControlModifier) {
        lookup();
        genStdMsgs(m_rpt);
        return;
      }
      if(e->modifiers() & Qt::AltModifier && m_lang=="ru_RU") {
        lookup();
        return;
      }
      break;
    case Qt::Key_O:
      if(!m_menus && e->modifiers() & Qt::ControlModifier) {
        QTimer::singleShot (0, this, SLOT (on_actionOpen_triggered()));
        return;
      }
      if(e->modifiers() & Qt::AltModifier && m_lang=="ru_RU") {
        ui->DecodeButton->click();
        return;
      }
      break;
    case Qt::Key_V:
      if(e->modifiers() & Qt::AltModifier) {
        m_fileToSave=m_fnameWE;
        return;
      }
      break;
    case Qt::Key_Z:
      if(e->modifiers() & Qt::AltModifier) {
        ui->filterButton->click();
        return;
      }
      break;
    case Qt::Key_Escape:
      haltTx("TX halted via Escape button ");
      break;
    case Qt::Key_B:
      if(e->modifiers() & Qt::AltModifier) {
        on_actionFT8_triggered();
        return;
      }
      break;
    case Qt::Key_C:
      if(e->modifiers() & Qt::AltModifier) {
        on_actionFT4_triggered();
        return;
      }
      break;
    case Qt::Key_M:
      if(e->modifiers() & Qt::AltModifier && m_lang=="ru_RU") {
        ui->monitorButton->click();
        return;
      }
      break;
    case Qt::Key_N:
      if(e->modifiers() & Qt::AltModifier && m_lang=="ru_RU") {
        if(!ui->enableTxButton->isChecked()) m_manualTxAuthorized=true;
        ui->enableTxButton->click();
        return;
      }
      break;
    case Qt::Key_S:
      if(e->modifiers() & Qt::AltModifier && m_lang=="ru_RU") {
        ui->stopButton->click();
        return;
      }
      break;
    }

  QMainWindow::keyPressEvent (e);
}

void MainWindow::bumpFqso(int n)                                 //bumpFqso()
{
  int i;
  bool ctrl = (n>=100);
  n=n%100;
  i=ui->RxFreqSpinBox->value ();
  if(n==11) i--;
  if(n==12) i++;
  if (ui->RxFreqSpinBox->isEnabled ())
    {
      ui->RxFreqSpinBox->setValue (i);
    }
  if(ctrl and m_mode.left(4)=="WSPR") {
    ui->WSPRfreqSpinBox->setValue(i);
  } else {
    if(ctrl && ui->TxFreqSpinBox->isEnabled ()) {
      ui->TxFreqSpinBox->setValue (i);
    }
  }
}

void MainWindow::displayDialFrequency ()
{
  static bool startup=true;
  Frequency dial_frequency {m_rigState.ptt () && m_rigState.split () ?
      m_rigState.tx_frequency () : m_rigState.frequency ()};
  if(m_monitoroff && m_config.rig_name()=="None") dial_frequency=m_freqNominal;
  // The band selector and worked-before lookup describe the nominal receive
  // frequency. A split TX VFO (including satellite operation) must not change
  // that context while the frequency readout displays the transmitting VFO.
  auto const band_name = m_config.bands ()->find (m_freqNominal);
//  printf("last band %s curband %s band %s freq %lld\n",m_lastBand.toStdString().c_str(),ui->bandComboBox->currentText().toStdString().c_str(),band_name.toStdString().c_str(),dial_frequency);
  bool const changed=m_lastBand!=band_name;
  if(changed) {
    if(!m_lastBand.isEmpty() && !band_name.isEmpty()) { // handle loss of the poll response from transceiver where dial_frequency==0
      if(m_autoEraseBC && !startup) { // option: erase both windows if band is changed
        clearBandActivity(); ui->decodedTextBrowser2->clear();
        clearDX (" cleared, triggered by erase both windows option upon band change from transceiver");
      }
      m_qsoHistory.init();
      if(m_config.write_decoded_debug()) {
        QString text = startup ? "program startup, m_lastBand: " : "QSO history initialized by band change from transceiver, m_lastBand: ";
        writeToALLTXT(text + m_lastBand + ", current band: " + band_name + ", dial_frequency: " + QString::number(dial_frequency) + ", TX VFO frequency: " + 
        QString::number(m_rigState.tx_frequency ()) + ", RX VFO frequency: " + QString::number(m_rigState.frequency ()));
      }
      // Set the attenuation value if options are checked
      QString curBand;
      if (m_config.pwrBandTxMemory() && !m_tune) {
          if (m_mode == "JT9+JT65" && m_modeTx == "JT65") { curBand = band_name+m_modeTx; }
          else { curBand = band_name+m_mode; }
          if (m_pwrBandTxMemory.contains(curBand)) { m_PwrBandSetOK = false; ui->outAttenuation->setValue(m_pwrBandTxMemory[curBand].toInt()); m_PwrBandSetOK = true;/* printf("set power from freq %s %s %d\n",m_lastBand.toStdString().c_str(),curBand.toStdString().c_str(),m_pwrBandTxMemory[curBand].toInt());*/}
          else { m_pwrBandTxMemory[curBand] = ui->outAttenuation->value(); }
      }
      startup=false;
    }
    m_wideGraph->setRxBand (band_name);
  }
  m_lastBand = band_name;
  // Model refresh/currentIndexChanged can restore the old working-frequency
  // row after a CAT QSY. Repair the label even when the band is already cached,
  // without replaying band-change side effects or clobbering manual input.
  if((changed || !m_bandEdited) && ui->bandComboBox->currentText()!=band_name) {
    QSignalBlocker blocker(ui->bandComboBox);
    ui->bandComboBox->setCurrentText(band_name.isEmpty()?m_config.bands()->oob():band_name);
    ui->bandComboBox->lineEdit()->setStyleSheet(band_name.isEmpty()
      ?QString("QLineEdit {color: %1; background-color: %2;}").arg(Radio::convert_dark("#ffff00",m_useDarkStyle),Radio::convert_dark("#ff0000",m_useDarkStyle))
      :QString{});
  }
  // search working frequencies for one we are within 10kHz of (1 Mhz of on VHF and up)
  bool valid {false};
  quint64 min_offset {99999999};
  for (auto const& item : *m_config.frequencies ())
    {
      // we need to do specific checks for above and below here to
      // ensure that we can use unsigned Radio::Frequency since we
      // potentially use the full 64-bit unsigned range.
      auto const& working_frequency = item.frequency_;
      auto const& offset = dial_frequency > working_frequency ?
        dial_frequency - working_frequency :
        working_frequency - dial_frequency;
      if (offset < min_offset) {
        min_offset = offset;
      }
    }
  if (min_offset < 10000u) {
    valid = true;
  }
  if (valid) ui->labDialFreq->setStyleSheet(QString("QLabel {font-family: MS Shell Dlg 2;font-size: 18pt;background: %1;color: %2;}").arg(Radio::convert_dark("#e1e1e1",m_useDarkStyle),Radio::convert_dark("#0000ff",m_useDarkStyle)));
  else ui->labDialFreq->setStyleSheet(QString("QLabel {font-family: MS Shell Dlg 2;font-size: 18pt;background: %1;color: %2;}").arg(Radio::convert_dark("#ff0000",m_useDarkStyle),Radio::convert_dark("#0000ff",m_useDarkStyle)));
  ui->labDialFreq->setText (Radio::pretty_frequency_MHz_string (dial_frequency));

  static bool first_freq {true};
  static Frequency first_value {145000000};
  if(((first_freq && dial_frequency!=0 && dial_frequency!=145000000) || (first_value != m_lastDialFreq)) && m_mode=="FT8") {
    first_value = m_lastDialFreq;
    if(first_freq) dial_frequency = m_freqNominal;
    bool commonFT8b=false;
    for(long unsigned int i=0; i < sizeof (m_ft8Freq) / sizeof (m_ft8Freq[0]); i++) {
      int kHzdiff=dial_frequency/1000 - m_ft8Freq[i];
      if(kHzdiff > -2 && kHzdiff < 3) { commonFT8b=true; break; }
    }
    m_commonFT8b=commonFT8b; first_freq=false;
    if (m_houndMode) {
    // Don't allow Hound frequency control in common FT8 bands if VFO Split mode is switched off
      QString message = "";
      if(!m_superFoxMode && !m_config.split_mode() && !m_commonFT8b && m_config.rig_name() != "None") {
        message =  tr ("Hound mode TX frequency control requires"
                                                            " *Split* rig control (either *Rig* or *Fake It* set"
                                                            " in the *Settings | Radio* tab.)");
        JTDXMessageBox::warning_message (this, "", tr ("Hound TX frequency control warning"), message);
        ui->actionEnable_hound_mode->setChecked(false);
      } else {
        m_houndTXfreqJumps=!m_superFoxMode && !m_commonFT8b && m_config.split_mode() && m_config.rig_name() != "None";
        ui->actionUse_TX_frequency_jumps->setChecked(m_houndTXfreqJumps);
        if(m_superFoxMode || m_commonFT8b || m_config.rig_name() == "None") ui->actionUse_TX_frequency_jumps->setEnabled(false);
        else ui->actionUse_TX_frequency_jumps->setEnabled(true);
      }
    }
  }
}

void MainWindow::styleChanged()
{

  ui->pbTxLock->setChecked(m_lockTxFreq);
  on_pbTxLock_clicked(m_lockTxFreq);
  ui->tx5->setStyleSheet(QString("QComboBox {selection-background-color: %1;background: %1}").arg(Radio::convert_dark("#ffffff",m_useDarkStyle)));
  on_directionLineEdit_textChanged(m_cqdir);
  on_direction1LineEdit_textChanged(m_cqdir);
  if(!m_uploadSpots) ui->cbUploadWSPR_Spots->setStyleSheet(QString("QCheckBox{background: %1}").arg(Radio::convert_dark("#ffff00",m_useDarkStyle)));
  ui->label_4->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#aabec8",m_useDarkStyle)));
  ui->label_9->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#aabec8",m_useDarkStyle)));
  ui->label_10->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#aabec8",m_useDarkStyle)));
  if (!m_mode.startsWith ("WSPR")) {
	if (m_config.prompt_to_log ()) { qso_count_label->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#99ff99",m_useDarkStyle))); }
	else if (m_config.autolog ()) { qso_count_label->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#9999ff",m_useDarkStyle))); }
	else { qso_count_label->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#ffffff",m_useDarkStyle))); }
  }
ui->dxCallEntry->setStyleSheet(QString("QLineEdit {color: %1; background: %2}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#ffffff",m_useDarkStyle)));
  updateTxButtonAppearance();
  setLastLogdLabel();
  setAutoSeqButtonStyle(m_autoseq);
  if(m_config.spot_to_dxsummit()) {
    ui->pbSpotDXCall->setStyleSheet(QString("QPushButton{color: %1;background: %2;border-style: outset; border-width: 1px;border-color: %3;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),
      Radio::convert_dark("#c4c4ff",m_useDarkStyle),Radio::convert_dark("#808080",m_useDarkStyle))); }
  else {
    ui->pbSpotDXCall->setStyleSheet(QString("QPushButton{color: %1;background: %2;border-style: outset;border-width: 1px;border-color: %3;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),
      Radio::convert_dark("#aabec8",m_useDarkStyle),Radio::convert_dark("#808080",m_useDarkStyle))); }
//  ui->txrb1->setStyleSheet(QString("QRadioButton::indicator:checked:disabled{background: %1;width: 6px;height: 6px;border-radius: 3px;margin-left: 3px}").arg(Radio::convert_dark("#222222",m_useDarkStyle)));
//  ui->txrb2->setStyleSheet(QString("QRadioButton::indicator:checked:disabled{background: %1;width: 6px;height: 6px;border-radius: 3px;margin-left: 3px}").arg(Radio::convert_dark("#222222",m_useDarkStyle)));
//  ui->txrb3->setStyleSheet(QString("QRadioButton::indicator:checked:disabled{background: %1;width: 6px;height: 6px;border-radius: 3px;margin-left: 3px}").arg(Radio::convert_dark("#222222",m_useDarkStyle)));
//  ui->txrb4->setStyleSheet(QString("QRadioButton::indicator:checked:disabled{background: %1;width: 6px;height: 6px;border-radius: 3px;margin-left: 3px}").arg(Radio::convert_dark("#222222",m_useDarkStyle)));
//  ui->txrb5->setStyleSheet(QString("QRadioButton::indicator:checked:disabled{background: %1;width: 6px;height: 6px;border-radius: 3px;margin-left: 3px}").arg(Radio::convert_dark("#222222",m_useDarkStyle)));
//  ui->txrb6->setStyleSheet(QString("QRadioButton::indicator:checked:disabled{background: %1;width: 6px;height: 6px;border-radius: 3px;margin-left: 3px}").arg(Radio::convert_dark("#222222",m_useDarkStyle)));
  if(m_autoseq && !m_autoTx) ui->AutoTxButton->setStyleSheet(QString("QPushButton{color: %1;background: %2;border-style: solid;border-width: 1px;border-color: %3;min-width: 5em;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#ffbbbb",m_useDarkStyle),Radio::convert_dark("#808080",m_useDarkStyle)));
  else ui->AutoTxButton->setStyleSheet(QString("QPushButton:checked{background: %1}").arg(Radio::convert_dark("#00ff00",m_useDarkStyle)));
  ui->tuneButton->setStyleSheet(QString("QPushButton:checked{background: %1}").arg(Radio::convert_dark("#ff0000",m_useDarkStyle)));
  ui->monitorButton->setStyleSheet(QString("QPushButton:checked{background: %1}").arg(Radio::convert_dark("#00ff00",m_useDarkStyle)));
  ui->bypassButton->setStyleSheet(QString("QPushButton:checked{background: %1}").arg(Radio::convert_dark("#00ff00",m_useDarkStyle)));
  ui->singleQSOButton->setStyleSheet(QString("QPushButton:checked{background: %1}").arg(Radio::convert_dark("#00ff00",m_useDarkStyle)));
  ui->AnsB4Button->setStyleSheet(QString("QPushButton:checked{background: %1}").arg(Radio::convert_dark("#00ff00",m_useDarkStyle)));
  ui->swlButton->setStyleSheet(QString("QPushButton:checked{background: %1}").arg(Radio::convert_dark("#00ff00",m_useDarkStyle)));
  ui->filterButton->setStyleSheet(QString("QPushButton:checked{background: %1}").arg(Radio::convert_dark("#00ff00",m_useDarkStyle)));
  ui->AGCcButton->setStyleSheet(QString("QPushButton:checked{background: %1}").arg(Radio::convert_dark("#00ff00",m_useDarkStyle)));
  ui->hintButton->setStyleSheet(QString("QPushButton:checked{background: %1}").arg(Radio::convert_dark("#00ff00",m_useDarkStyle)));
  ui->syncButton->setStyleSheet(QString("QPushButton:checked{background: %1}").arg(Radio::convert_dark("#00ff00",m_useDarkStyle)));
  ui->DecodeButton->setStyleSheet(QString("QPushButton:checked{background: %1}").arg(Radio::convert_dark("#00ffff",m_useDarkStyle)));
  m_wideGraph->setDarkStyle(m_useDarkStyle);
  statusUpdate ();
}

void MainWindow::statusChanged()
{
  statusUpdate ();
  QFile f {m_config.temp_dir ().absoluteFilePath ("wsjtx_status.txt")};
  if(f.open(QFile::WriteOnly | QIODevice::Text)) {
    QTextStream out(&f);
    out << qSetRealNumberPrecision (12) << (m_freqNominal / 1.e6)
        << ";" << m_mode << ";" << m_hisCall << ";"
        << ui->rptSpinBox->value() << ";" << m_modeTx <<
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                 endl;
#else
                 Qt::endl;
#endif

    f.close();
  } else {
    JTDXMessageBox::warning_message (this, "", tr ("File Open Error")
                                 , tr ("Cannot open \"%1\" for append: %2")
                                 .arg (f.fileName ()).arg (f.errorString ()));
  }
}

bool MainWindow::eventFilter(QObject *object, QEvent *event)  //eventFilter()
{
  if(handleGeneralMouseEvent(object,event))return true;
  if(handleTxSlotEvent(object,event))return true;
  if (object == ui->enableTxButton || object == ui->tuneButton) {
    auto button = qobject_cast<QPushButton*>(object);
    bool manual = event->type() == QEvent::Shortcut;
    if (event->type() == QEvent::MouseButtonRelease) {
      auto mouse = static_cast<QMouseEvent*>(event);
      manual = mouse->button() == Qt::LeftButton && button->isDown() && button->rect().contains(mouse->pos());
    } else if (event->type() == QEvent::KeyRelease) {
      auto key = static_cast<QKeyEvent*>(event);
      manual = !key->isAutoRepeat() && key->key()==Qt::Key_Space && button->isDown();
    } else if (event->type() == QEvent::KeyPress) {
      auto key = static_cast<QKeyEvent*>(event);
      manual = !key->isAutoRepeat() && (key->key()==Qt::Key_Return || key->key()==Qt::Key_Enter);
    }
    if (manual && button->isEnabled()) {
      button->setDown(false);
      // The delayed progress window must not create a brief opportunity to
      // arm TX/Tune while the operator's log is still being processed.
      // Checked buttons remain usable so an existing transmission can stop.
      if(!button->isChecked() && (m_logLoading || m_logImportActive || m_logRefreshPending))return true;
      if (!button->isChecked()) {
        if (object == ui->enableTxButton) m_manualTxAuthorized = true;
        else { on_stopTxButton_clicked(); m_manualTuneAuthorized = true; }
      }
      // Consume the event and activate synchronously: authorization cannot linger
      // after a key event that Qt would otherwise decline to turn into a click.
      button->click();
      if (!m_enableTx) m_manualTxAuthorized=false;
      if (!m_tune) m_manualTuneAuthorized=false;
      return true;
    }
  }
  switch (event->type())
    {
    case QEvent::WindowActivate:
      txwatchdog (false);
      break;
    case QEvent::KeyPress:
      // fall through
    case QEvent::MouseButtonPress:
      txwatchdog (false);
      break;

    case QEvent::ChildAdded:
      // ensure our child widgets get added to our event filter
      add_child_to_event_filter (static_cast<QChildEvent *> (event)->child ());
      break;

    case QEvent::ChildRemoved:
      // ensure our child widgets get d=removed from our event filter
      remove_child_from_event_filter (static_cast<QChildEvent *> (event)->child ());
      break;

    case QEvent::ToolTip:
      if(!m_showTooltips) return true;
      break;

    default: break;
    }

  return QObject::eventFilter(object, event);
}

void MainWindow::createStatusBar()                           //createStatusBar
{
  statusBar()->setMinimumHeight (30);
  statusBar()->setContentsMargins(2,1,2,2);

  tx_status_label->setAlignment(Qt::AlignHCenter);
  tx_status_label->setAlignment(Qt::AlignVCenter);
  tx_status_label->setContentsMargins(1,1,1,1); //(int left, int top, int right, int bottom)
  tx_status_label->setMinimumSize(QSize(150,20));
  tx_status_label->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#00ff00",m_useDarkStyle)));
  tx_status_label->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget(tx_status_label);

  mode_label->setAlignment(Qt::AlignHCenter);
  mode_label->setAlignment(Qt::AlignVCenter);
  mode_label->setContentsMargins(1,1,1,1);
  mode_label->setMinimumSize(QSize(45,20));
  mode_label->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget(mode_label);

  last_tx_label->setAlignment(Qt::AlignHCenter);
  last_tx_label->setAlignment(Qt::AlignVCenter);
  last_tx_label->setContentsMargins(1,1,1,1);
  last_tx_label->setMinimumSize(QSize(100,20));
  last_tx_label->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget(last_tx_label);

  txwatchdog_label->setAlignment(Qt::AlignHCenter);
  txwatchdog_label->setAlignment(Qt::AlignVCenter);
  txwatchdog_label->setContentsMargins(1,1,1,1);
  txwatchdog_label->setMinimumSize(QSize(52,20));
  txwatchdog_label->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  txwatchdog_label->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget(txwatchdog_label);
  update_watchdog_label ();



  statusBar()->addWidget(progressBar,1);
  progressBar->setMinimumWidth(40);
  progressBar->setFormat("%v/"+QString::number(m_TRperiod));

  lastlogged_label->setAlignment(Qt::AlignHCenter);
  lastlogged_label->setAlignment(Qt::AlignVCenter);
  lastlogged_label->setContentsMargins(1,1,1,1);
  lastlogged_label->setMinimumSize(QSize(76,20));
  lastlogged_label->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget(lastlogged_label);

  date_label->setAlignment(Qt::AlignHCenter);
  date_label->setAlignment(Qt::AlignVCenter);
  date_label->setContentsMargins(1,1,1,1);
  date_label->setMinimumSize(QSize(76,20));
  date_label->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget(date_label);


  qso_count_label->setAlignment(Qt::AlignHCenter);
  qso_count_label->setAlignment(Qt::AlignVCenter);
  qso_count_label->setContentsMargins(1,1,1,1);
  qso_count_label->setMinimumSize(QSize(76,20));
  qso_count_label->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  statusBar()->addWidget(qso_count_label);
 }

bool MainWindow::subProcessFailed (QProcess * process, int exit_code, QProcess::ExitStatus status)
{
  if (m_valid && (exit_code || QProcess::NormalExit != status))
    {
      QStringList arguments;
      for (auto argument: process->arguments ())
        {
          if (argument.contains (' ')) argument = '"' + argument + '"';
          arguments << argument;
        }
      JTDXMessageBox::critical_message (this, "", tr ("Subprocess Error")
                                    , tr ("Subprocess failed with exit code %1")
                                    .arg (exit_code)
                                    , tr ("Running: %1\n%2")
                                    .arg (process->program () + ' ' + arguments.join (' '))
                                    .arg (QString {process->readAllStandardError()}));
      return true;          // ensures exit if still constructing
    }
  return false;  
}

void MainWindow::subProcessError (QProcess * process, QProcess::ProcessError)
{
  if (process == &proc_jtdxjt9) { recoverDecoder(process->errorString()); return; }
  if (m_valid)
    {
      QStringList arguments;
      for (auto argument: process->arguments ())
        {
          if (argument.contains (' ')) argument = '"' + argument + '"';
          arguments << argument;
        }
      JTDXMessageBox::critical_message (this, "", tr ("Subprocess error")
                                    , tr ("Running: %1\n%2")
                                    .arg (process->program () + ' ' + arguments.join (' '))
                                    .arg (process->errorString ()));
      QTimer::singleShot (0, this, SLOT (close ()));
      m_valid = false;              // ensures exit if still constructing
    }
}

// A decoder fault must not take the GUI, settings and unsaved QSO down with it.
void MainWindow::recoverDecoder(QString const& reason)
{
  if (!m_valid || m_decoderRecovering) return;
  m_decoderRecovering = true;
  ++m_decoderRestarts;
  TxAgainTimer.stop(); enableTxButtonTimer.stop(); tuneButtonTimer.stop();
  m_tuneup = false;
  haltTx("decoder failure ");
  on_monitorButton_clicked(false);
  m_loopall = false;
  m_manualDecode = false;
  m_startAnother = false;
  ui->DecodeButton->setChecked(false);
  QString message = tr("Decoder error: %1. TX stopped; restarting decoder.").arg(reason);
  statusBar()->showMessage(message, 30000);
  writeToALLTXT(message);
  QFile diagnostic(m_config.log_directory().absoluteFilePath("decoder-errors.log"));
  if (diagnostic.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    diagnostic.write((QDateTime::currentDateTimeUtc().toString(Qt::ISODate) + " " + message + "\n" + QString::fromUtf8(proc_jtdxjt9.readAllStandardError()) + "\n").toUtf8());
  mcpEvent("decoder_error", {{"reason", reason}, {"restart_attempt", m_decoderRestarts}});
  if (proc_jtdxjt9.state() != QProcess::NotRunning) proc_jtdxjt9.kill();
  QTimer::singleShot(200, this, &MainWindow::restartDecoder);
}

void MainWindow::restartDecoder()
{
  if (!m_valid) return;
  if (proc_jtdxjt9.state() != QProcess::NotRunning) {
    QTimer::singleShot(200, this, &MainWindow::restartDecoder);
    return;
  }
  decodeBusy(false);
  m_decoderRecovering = false;
  if (m_decoderRestarts > 3) {
    statusBar()->showMessage(tr("Decoder restart failed repeatedly. Check decoder-errors.log and restart JTDX-F."));
    return;
  }
  QFile {m_config.temp_dir().absoluteFilePath(".quit")}.remove();
  QFile {m_config.temp_dir().absoluteFilePath(".lock")}.open(QIODevice::ReadWrite);
  m_firstDecode = 0;
  m_modeChanged = true;
  proc_jtdxjt9.start(proc_jtdxjt9.program(), proc_jtdxjt9.arguments(), QIODevice::ReadWrite | QIODevice::Unbuffered);
  statusBar()->showMessage(tr("Decoder restarted. Enable Monitor to resume reception; TX remains stopped."), 30000);
}

void MainWindow::closeEvent(QCloseEvent * e)
{
  on_stopTxButton_clicked();
  drainSignoffLogUpdates();
  monitor(false);
  m_valid = false;              // suppresses subprocess errors
  if(m_config.clear_DX_exit())
    {
      clearDX ("");
    }
  m_config.transceiver_offline ();
  writeSettings ();
  m_guiTimer.stop ();
  m_prefixes.reset ();
  m_shortcuts.reset ();
  m_mouseCmnds.reset ();

  killFile ();
  m_killAll=true;
  mem_jtdxjt9->detach();
  QFile quitFile {m_config.temp_dir ().absoluteFilePath (".quit")};
  quitFile.open(QIODevice::ReadWrite);
  QFile {m_config.temp_dir ().absoluteFilePath (".lock")}.remove(); // Allow jtdxjt9 to terminate
  bool b=proc_jtdxjt9.waitForFinished(1000);
  if(!b) proc_jtdxjt9.close();
  quitFile.remove();
  Q_EMIT finished ();
  QMainWindow::closeEvent (e);
}

void MainWindow::on_stopButton_clicked() { m_monitoroff=true; monitor (false); m_loopall=false; }
void MainWindow::on_AnsB4Button_clicked (bool checked) { ui->actionAnswerWorkedB4->setChecked(checked); }
void MainWindow::on_singleQSOButton_clicked (bool checked) { ui->actionSingleShot->setChecked(checked); }
void MainWindow::on_bypassButton_clicked (bool checked) { ui->actionBypass_all_text_filters->setChecked(checked); }

void MainWindow::on_pbSpotDXCall_clicked ()
{
  if(m_config.spot_to_dxsummit() && !m_spotDXsummit && !m_hisCall.isEmpty() && !m_config.my_callsign().isEmpty ()) {
    QUrl url("http://www.dxsummit.fi/SendSpot.aspx");
    double frequency=(m_freqNominal + ui->RxFreqSpinBox->value())/1000.0;
    QUrlQuery query;
    query.addQueryItem("callSign", m_config.my_callsign());
    query.addQueryItem("dxCallSign", m_hisCall);
    query.addQueryItem("frequency", QString::number(frequency,'f',1));
    QString spotInfoText=ui->spotMsgLabel->text(); spotInfoText.remove(0,6);
    query.addQueryItem("info", spotInfoText);
    url.setQuery(query.query());
    QEventLoop eventLoop;
    QTimer timer;
    timer.setSingleShot(true);
    QNetworkAccessManager mgr;
    QNetworkRequest req( url );
    QNetworkReply *reply = mgr.get(req);
    QObject::connect(&timer, SIGNAL(timeout()), &eventLoop, SLOT(quit()));
    QObject::connect( reply, SIGNAL(finished()), &eventLoop, SLOT(quit()) );
    timer.start(10*1000);
    eventLoop.exec( QEventLoop::ExcludeUserInputEvents );
    if(timer.isActive()) {
      timer.stop();
//    if (reply->error() == QNetworkReply::NoError) {
//      printf("Success : %s\n",reply->readAll().toStdString().c_str());
//    } else {
//      printf("Failure : %s\n",reply->errorString().toStdString().c_str());
//    }
      ui->pbSpotDXCall->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: outset;border-width: 1px;border-color: %3;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#c4ffc4",m_useDarkStyle),Radio::convert_dark("#808080",m_useDarkStyle)));
      ui->pbSpotDXCall->setText(tr("Spotted"));
      m_spotDXsummit=true;
    } else {
   // timeout
     QObject::disconnect(reply, SIGNAL(finished()), &eventLoop, SLOT(quit()));

     reply->abort();
     JTDXMessageBox::critical_message(0, "", "Critical", tr("Can not establish/complete connection to dxsummit server"));
    }
    delete reply;
  }
}

void MainWindow::msgBox(QString t) { msgBox0.setText(t); msgBox0.translate_buttons(); msgBox0.exec(); }
void MainWindow::on_actionJTDX_Web_Site_triggered() { m_manual.display_html_url (QUrl {PROJECT_MANUAL_DIRECTORY_URL}, PROJECT_MANUAL); }
void MainWindow::on_actionJTDX_Forum_triggered() { m_manual.display_html_url (QUrl {"https://jtdx.freeforums.net/"}, ""); }

/*Display local copy of manual
void MainWindow::on_actionLocal_User_Guide_triggered()
{
#if defined (CMAKE_BUILD)
  m_manual.display_html_file (m_config.doc_dir (), PROJECT_MANUAL);
#endif
}*/

void MainWindow::on_actionWide_Waterfall_triggered() { m_wideGraph->show(); } //Display Waterfalls

void MainWindow::on_actionCopyright_Notice_triggered()
{
  JTDXMessageBox::information_message(this, "", tr("The algorithms, source code, look-and-feel of WSJT-X and related "
                           "programs, and protocol specifications for the modes FSK441, FT8, JT4, "
                           "JT6M, JT9, JT65, JTMS, QRA64, ISCAT, MSK144 are Copyright (C) "
                           "2001-2018 by one or more of the following authors: Joseph Taylor, "
                           "K1JT; Bill Somerville, G4WJS; Steven Franke, K9AN; Nico Palermo, "
                           "IV3NWV; Greg Beam, KI7MT; Michael Black, W9MDB; Edson Pereira, PY2SDR; "
                           "Philip Karn, KA9Q; and other members of the WSJT Development Group."));

}

void MainWindow::hideMenus(bool checked)
{
  ui->menuBar->setVisible(!checked);
  ui->label_6->setVisible(!checked);
  if(!m_mode.startsWith ("WSPR")) {
     ui->label_7->setVisible(!checked);
     ui->decodedTextLabel2->setVisible(!checked);
  }
//  ui->decodedTextLabel->setVisible(!checked);
  ui->gridLayout_14->layout()->setSpacing(0);
  ui->gridLayout_14->layout()->setContentsMargins(0,0,0,0);
  ui->horizontalLayout_10->layout()->setSpacing(0);
  ui->horizontalLayout_10->layout()->setContentsMargins(0,0,0,0);
  ui->verticalLayout->layout()->setSpacing(1);
  ui->verticalLayout->layout()->setContentsMargins(0,0,0,0);
  ui->verticalLayout_7->layout()->setSpacing(1);
  ui->verticalLayout_7->layout()->setContentsMargins(0,0,0,0);
}

void MainWindow::on_actionOpen_triggered()                     //Open File
{
  monitor (false);

  QString fname;
  fname=QFileDialog::getOpenFileName(this, "Open File", m_path,
                                     "WSJT Files (*.wav)");
  if(!fname.isEmpty ()) {
    m_path=fname;
    int i1=fname.lastIndexOf("/");
    QString baseName=fname.mid(i1+1);
    tx_status_label->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#99ffff",m_useDarkStyle)));
    tx_status_label->setText(" " + baseName + " ");
    on_stopButton_clicked();
    m_diskData=true;
    read_wav_file (fname);
  }
}

void MainWindow::read_wav_file (QString const& fname)
{
  // Keep logging/UI access on the GUI thread and share the validated WAV reader.
  // The old reader divided by zero for invalid formats and used a byte count
  // as a short-array offset when zero-filling, corrupting memory on short files.
  if (m_config.write_decoded() || m_config.write_decoded_debug())
    writeToALLTXT("Reading wav file " + QFileInfo(fname).fileName());
  monitor(false);
  // Drain the stop command before the worker reuses the shared audio buffer.
  if(m_audioThread.isRunning())QMetaObject::invokeMethod(m_soundInput,[]{},Qt::BlockingQueuedConnection);
  if(m_wav_future_watcher.isRunning())return;
  const double period = m_TRperiod;
  m_wav_future_watcher.setFuture(QtConcurrent::run([fname, period] { getfile(fname, period); }));
}

void MainWindow::on_actionOpen_next_in_directory_triggered()   //Open Next
{
  monitor (false);

  int i,len;
  QFileInfo fi(m_path);
  QStringList list;
  list= fi.dir().entryList().filter(".wav",Qt::CaseInsensitive);
  for (i = 0; i < list.size()-1; ++i) {
    if(i==list.size()-2) m_loopall=false;
    len=list.at(i).length();
    if(list.at(i)==m_path.right(len)) {
      int n=m_path.length();
      QString fname=m_path.replace(n-len,len,list.at(i+1));
      m_path=fname;
      int i1=fname.lastIndexOf("/");
      QString baseName=fname.mid(i1+1);
      tx_status_label->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#99ffff",m_useDarkStyle)));
      tx_status_label->setText(" " + baseName + " ");
      m_diskData=true;
      read_wav_file (fname);
      return;
    }
  }
}
//Open all remaining files
void MainWindow::on_actionDecode_remaining_files_in_directory_triggered() { m_loopall=true; on_actionOpen_next_in_directory_triggered(); }

void MainWindow::diskDat()                                   //diskDat()
{
  if(dec_data.params.kin>0) {
    int k;
    int kstep=m_FFTSize;
    m_diskData=true;
    for(int n=1; n<=m_hsymStop; n++) {                      // Do the waterfall spectra
      k=(n+1)*kstep;
//      if(k > dec_data.params.kin) break;
      dec_data.params.npts8=k/8;
      dataSink(k);
      qApp->processEvents();                                //Update the waterfall
    }
  } else {
    JTDXMessageBox::information_message(this, "", tr("No data read from disk. Wrong file format?"));
  }
}

//Delete ../save/*.wav
void MainWindow::on_actionDelete_all_wav_files_in_SaveDir_triggered()
{ /* Log and recording deletion is intentionally performed outside the application. */ }

void MainWindow::on_actionNone_triggered() { m_saveWav=0; ui->actionNone->setChecked(true); }
void MainWindow::on_actionSave_decoded_triggered() { m_saveWav=1; ui->actionSave_decoded->setChecked(true); }
void MainWindow::on_actionSave_all_triggered() { m_saveWav=2; ui->actionSave_all->setChecked(true); }

void MainWindow::on_actionEnglish_triggered() { ui->actionEnglish->setChecked(true); set_language("en_US"); }
void MainWindow::on_actionEstonian_triggered() { ui->actionEstonian->setChecked(true); set_language("et_EE"); }
void MainWindow::on_actionRussian_triggered() { ui->actionRussian->setChecked(true); set_language("ru_RU"); }
void MainWindow::on_actionCatalan_triggered() { ui->actionCatalan->setChecked(true); set_language("ca_ES"); }
void MainWindow::on_actionCroatian_triggered() { ui->actionCroatian->setChecked(true); set_language("hr_HR"); }
void MainWindow::on_actionDanish_triggered() { ui->actionDanish->setChecked(true); set_language("da_DK"); }
void MainWindow::on_actionDutch_triggered() { ui->actionDutch->setChecked(true); set_language("nl_NL"); }
void MainWindow::on_actionHungarian_triggered() { ui->actionHungarian->setChecked(true); set_language("hu_HU"); }
void MainWindow::on_actionSpanish_triggered() { ui->actionSpanish->setChecked(true); set_language("es_ES"); }
void MainWindow::on_actionSwedish_triggered() { ui->actionSwedish->setChecked(true); set_language("sv_SE"); }
void MainWindow::on_actionFrench_triggered() { ui->actionFrench->setChecked(true); set_language("fr_FR"); }
void MainWindow::on_actionItalian_triggered() { ui->actionItalian->setChecked(true); set_language("it_IT"); }
void MainWindow::on_actionLatvian_triggered() { ui->actionLatvian->setChecked(true); set_language("lv_LV"); }
void MainWindow::on_actionPolish_triggered() { ui->actionPolish->setChecked(true); set_language("pl_PL"); }
void MainWindow::on_actionPortuguese_triggered() { ui->actionPortuguese->setChecked(true); set_language("pt_PT"); }
void MainWindow::on_actionPortuguese_BR_triggered() { ui->actionPortuguese_BR->setChecked(true); set_language("pt_BR"); }
void MainWindow::on_actionChinese_simplified_triggered() { ui->actionChinese_simplified->setChecked(true); set_language("zh_CN"); }
void MainWindow::on_actionChinese_traditional_triggered() { ui->actionChinese_traditional->setChecked(true); set_language("zh_HK"); }
void MainWindow::on_actionJapanese_triggered() { ui->actionJapanese->setChecked(true); set_language("ja_JP"); }

void MainWindow::on_actionCallNone_toggled(bool checked)
{
  if (checked) {
    m_callMode=0;
    if (m_callPrioCQ) {
      ui->actionCallPriorityAndSearchCQ->setChecked(false);
      m_callPrioCQ=false;
    }
    ui->actionCallPriorityAndSearchCQ->setEnabled(false);
    ui->AutoSeqButton->setText(tr("AutoSeq0"));
  }
}

void MainWindow::on_actionCallFirst_toggled(bool checked)
{
  if (checked) {
    m_callMode=1;
    if (m_callPrioCQ) {
      ui->actionCallPriorityAndSearchCQ->setChecked(false);
      m_callPrioCQ=false;
    }
    ui->actionCallPriorityAndSearchCQ->setEnabled(false);
    ui->AutoSeqButton->setText(tr("AutoSeq1"));
  }
}

void MainWindow::on_actionCallMid_toggled(bool checked)
{
  if (checked) {
    m_callMode=2;
    ui->actionCallPriorityAndSearchCQ->setEnabled(true);
	if (!m_callPrioCQ) ui->AutoSeqButton->setText(tr("AutoSeq2"));
	else ui->AutoSeqButton->setText(tr("AutoSeq6"));
  }  
}

void MainWindow::on_actionCallEnd_toggled(bool checked)
{
  if (checked) {
    m_callMode=3;
    ui->actionCallPriorityAndSearchCQ->setEnabled(true);
	if (!m_callPrioCQ) ui->AutoSeqButton->setText(tr("AutoSeq3"));
	else ui->AutoSeqButton->setText(tr("AutoSeq7"));
  }
}

 void MainWindow::on_actionCallPriorityAndSearchCQ_toggled(bool checked)
 {
   m_callPrioCQ=checked;
   if (checked) {
     ui->actionAutoFilter->setChecked(false);
     ui->actionAutoFilter->setEnabled(false);
     m_autofilter=false;
     if (m_callMode==2) ui->AutoSeqButton->setText(tr("AutoSeq6"));
     else ui->AutoSeqButton->setText(tr("AutoSeq7"));
   } else {
      ui->actionAutoFilter->setEnabled(true);
      if (m_callMode==2) ui->AutoSeqButton->setText(tr("AutoSeq2"));
      else ui->AutoSeqButton->setText(tr("AutoSeq3"));
   }
 }

void MainWindow::on_actionMaxDistance_toggled(bool checked)
{
  m_maxDistance=checked;
}

void MainWindow::on_actionAnswerWorkedB4_toggled(bool checked) { m_answerWorkedB4=checked; ui->AnsB4Button->setChecked(checked); }
void MainWindow::on_actionCallWorkedB4_toggled(bool checked) { m_callWorkedB4=checked; }
void MainWindow::on_actionCallHigherNewCall_toggled(bool checked) { m_callHigherNewCall=checked; }

void MainWindow::on_actionSingleShot_toggled(bool checked)
{
  if(!m_restoringSettings)m_config.set_disable_TX_on_73(checked);
  m_disable_TX_on_73=checked;
  {QSignalBlocker block(ui->actionDisableTx73);ui->actionDisableTx73->setChecked(checked);}
  m_singleshot=checked;
  setAutoSeqButtonStyle(m_autoseq);
  ui->singleQSOButton->setChecked(checked);
}

void MainWindow::on_actionAutoFilter_toggled(bool checked)
{
  m_autofilter=checked;
  if(m_autofilter && !ui->actionSwitch_Filter_OFF_at_sending_73->isChecked() && !ui->actionSwitch_Filter_OFF_at_getting_73->isChecked()) {
     ui->actionSwitch_Filter_OFF_at_sending_73->setChecked(true); m_FilterState=1;
  }
}

void MainWindow::on_actionEnable_hound_mode_toggled(bool checked)
{
  if(checked && m_managed.active())setManagedMode(ManagedMode::Off);
// Don't allow Hound frequency control in common FT8 bands if VFO Split mode is switched off
  QString message = "";
  if(checked && !m_superFoxMode && !m_config.split_mode() && !m_commonFT8b && m_config.rig_name() != "None") {
    message =  tr ("Hound mode TX frequency control requires"
                                                        " *Split* rig control (either *Rig* or *Fake It* set"
                                                        " in the *Settings | Radio* tab.)");
    JTDXMessageBox::warning_message (this, "", tr ("Hound TX frequency control warning"), message);
    ui->actionEnable_hound_mode->setChecked(false);
    return;
  }
  m_houndTXfreqJumps=checked && !m_superFoxMode && !m_commonFT8b && m_config.split_mode() && m_config.rig_name() != "None";
  ui->actionUse_TX_frequency_jumps->setChecked(m_houndTXfreqJumps);
  m_houndMode=checked;
  if(!checked && m_superFoxMode) ui->actionSuperFox->setChecked(false);
  m_wideGraph->setHoundFilter(m_houndMode);
  ui->HoundButton->setChecked(m_houndMode);
  if(m_houndMode) {
    ui->HoundButton->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: solid;border-width: 1px;border-radius: 5px;border-color: %3;min-width: 5em;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#00ff00",m_useDarkStyle),Radio::convert_dark("#000000",m_useDarkStyle)));
    if(m_skipTx1) { m_skipTx1=false; ui->skipTx1->setChecked(false); ui->skipGrid->setChecked(false); on_txb1_clicked(); m_wasSkipTx1=true; }
    ui->skipTx1->setEnabled(false); ui->skipGrid->setEnabled(false);
    if(!m_superFoxMode && !m_commonFT8b && m_config.rig_name() != "None") ui->actionUse_TX_frequency_jumps->setEnabled(true); }
  else {
    ui->HoundButton->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: solid;border-width: 1px;border-color: %3;min-width: 5em;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#e1e1e1",m_useDarkStyle),Radio::convert_dark("#adadad",m_useDarkStyle)));
    ui->skipTx1->setEnabled(true); ui->skipGrid->setEnabled(true);
    if(m_wasSkipTx1) { 
      m_skipTx1=true; ui->skipTx1->setChecked(true); ui->skipGrid->setChecked(true);
      if(ui->txrb1->isChecked()) on_txb2_clicked();
      if(ui->genMsg->text() == ui->tx1->text()) ui->genMsg->setText(ui->tx2->text());
      m_wasSkipTx1=false;
	}
    ui->actionUse_TX_frequency_jumps->setEnabled(false);
  }
  setHoundAppearance(m_houndMode); if(!ui->spotLineEdit->text().isEmpty() && ui->spotLineEdit->text().contains("#H")) on_spotLineEdit_textChanged(ui->spotLineEdit->text());
}

void MainWindow::on_actionSuperFox_toggled(bool checked)
{
  on_stopTxButton_clicked();
  m_superFoxMode=checked;
  if(checked) {
    if(m_mode!="FT8") on_actionFT8_triggered();
    ui->actionEnable_hound_mode->setChecked(true);
    m_houndTXfreqJumps=false; ui->actionUse_TX_frequency_jumps->setChecked(false);
    ui->actionUse_TX_frequency_jumps->setEnabled(false);
    ui->RxFreqSpinBox->setValue(750);
    ui->TxMinuteButton->setChecked(false);m_txFirst=false;setMinButton();
  }
  if(!checked) on_actionEnable_hound_mode_toggled(m_houndMode);
  ui->HoundButton->setText(checked?tr("SuperHound"):tr("Hound"));
}
void MainWindow::on_actionSuperFoxTolerance_triggered()
{
  bool ok=false;
  int value=QInputDialog::getInt(this,tr("SuperFox frequency tolerance"),tr("Search around Rx frequency (Hz):"),m_superFoxTolerance,5,200,5,&ok);
  if(ok) m_superFoxTolerance=value;
}

void MainWindow::on_actionUse_TX_frequency_jumps_triggered (bool checked) {
  m_houndTXfreqJumps=checked && !m_superFoxMode;
  ui->actionUse_TX_frequency_jumps->setChecked(m_houndTXfreqJumps);
}
void MainWindow::setupFt8Threads(int available)
{
  m_ft8ThreadLimit=Ft8ThreadPolicy::maximum(available);
  auto group=ui->actionMTAuto->actionGroup();
  if(!group) {
    group=new QActionGroup(this);
    group->setExclusive(true);
    connect(group,&QActionGroup::triggered,this,[this](QAction* action){
      setFt8Threads(action->isVisible() && action->isEnabled()?action->data().toInt():m_ft8threads);
    });
  }
  for(int count=0;count<=Ft8ThreadPolicy::supportedMaximum;++count) {
    auto action=count?findChild<QAction*>(QString("actionMT%1").arg(count)):ui->actionMTAuto;
    Q_ASSERT(action);
    action->setActionGroup(group);action->setData(count);
    action->setVisible(count<=m_ft8ThreadLimit);
    action->setEnabled(count<=m_ft8ThreadLimit);
  }
  setFt8Threads(m_ft8threads);
}

void MainWindow::setFt8Threads(int requested)
{
  m_ft8threads=Ft8ThreadPolicy::normalize(requested,m_ft8ThreadLimit);
  auto action=m_ft8threads?findChild<QAction*>(QString("actionMT%1").arg(m_ft8threads)):ui->actionMTAuto;
  action->setChecked(true);
}
void MainWindow::on_actionAcceptUDPCQ_triggered() { m_acceptUDP=1; }
void MainWindow::on_actionAcceptUDPCQ73_triggered() { m_acceptUDP=2; }
void MainWindow::on_actionAcceptUDPAny_triggered() { m_acceptUDP=3; }
void MainWindow::on_actionDisableTx73_toggled(bool checked) {
  if(!m_restoringSettings)m_config.set_disable_TX_on_73(checked);
  m_disable_TX_on_73=checked;
  ui->actionSingleShot->setChecked(checked);on_actionSingleShot_toggled(checked);
}
void MainWindow::on_actionShow_tooltips_main_window_toggled(bool checked) { m_showTooltips = checked; }
void MainWindow::on_actionColor_Tx_message_buttons_toggled(bool checked) { m_colorTxMsgButtons = checked; }
void MainWindow::on_actionCallsign_to_clipboard_toggled(bool checked) { m_callToClipboard = checked; }
void MainWindow::on_actionCrossband_160m_JA_toggled(bool checked) { m_crossbandOptionEnabled = checked; }
void MainWindow::on_actionCrossband_160m_HL_toggled(bool checked) { m_crossbandHLOptionEnabled = checked; }

void MainWindow::on_actionShow_messages_decoded_from_harmonics_toggled(bool checked)
{
  if(checked) { dec_data.params.showharmonics=1; m_showHarmonics=true; }
  else { dec_data.params.showharmonics=0; m_showHarmonics=false; }
}

void MainWindow::on_actionMyCallRXFwindow_toggled(bool checked) { m_showMyCallMsgRxWindow=checked; }
void MainWindow::on_actionWantedCallRXFwindow_toggled(bool checked) { m_showWantedCallRxWindow=checked; }
void MainWindow::on_actionFT8SensMin_toggled(bool checked) { if(checked) m_ft8Sensitivity=0; }
void MainWindow::on_actionlowFT8thresholds_toggled(bool checked) { if(checked) m_ft8Sensitivity=1; }
void MainWindow::on_actionFT8subpass_toggled(bool checked) { if(checked) m_ft8Sensitivity=2; }
void MainWindow::on_actionFT8EarlyStart_toggled(bool checked) { m_FT8EarlyStart=checked; }
void MainWindow::on_actionFT8WidebandDXCallSearch_toggled(bool checked) { m_FT8WideDxCallSearch=checked; }
void MainWindow::on_actionBypass_text_filters_on_RX_frequency_toggled(bool checked) { m_bypassRxfFilters=checked; }
void MainWindow::on_actionBypass_all_text_filters_toggled(bool checked) { m_bypassAllFilters=checked; ui->bypassButton->setChecked(checked); }
void MainWindow::on_actionEnable_main_window_popup_toggled(bool checked) { m_windowPopup=checked; }
void MainWindow::on_actionAutoErase_toggled(bool checked) { m_autoErase=checked; }
void MainWindow::on_actionEraseWindowsAtBandChange_toggled(bool checked) { m_autoEraseBC=checked;if(!m_restoringSettings)m_config.set_erase_BandActivity(checked); }

void MainWindow::on_actionReport_message_priority_toggled(bool checked)
{
  m_rprtPriority=checked;
}

void MainWindow::on_actionKeyboard_shortcuts_triggered()
{
  if(!m_shortcuts) {
    QFont font;
    font.setPointSize (10);
    m_shortcuts.reset (new HelpTextWindow {tr ("Keyboard Shortcuts"),
                                               //: Keyboard shortcuts help window contents
                                               tr (R"(<table cellspacing=1>
  <tr><td><b>F1       </b></td><td>Online User's Guide</td><td><b>Ctrl+F1  </b></td><td>About JTDX</td></tr>
  <tr><td><b>F2       </b></td><td>Open configuration window</td></tr>
  <tr><td><b>F3       </b></td><td>Display keyboard shortcuts</td></tr>
  <tr><td><b>F4       </b></td><td>Clear DX Call/Grid and Tx messages</td><td><b>Alt+F4   </b></td><td>Exit program</td></tr>
  <tr><td><b>F5       </b></td><td>Display special mouse commands</td></tr>
  <tr><td><b>F6       </b></td><td>Open next file in directory</td><td><b>Shift+F6 </b></td><td>Decode all remaining files in directory</td></tr>
  <tr><td><b>F7       </b></td><td>Open log by assigned in the operating system viewer</td></tr>
  <tr><td><b>F11      </b></td><td>Move Rx frequency down 1 Hz</td><td><b>Ctrl+F11 </b></td><td>Move Rx and Tx frequencies down 1 Hz</td></tr>
  <tr><td><b>F12      </b></td><td>Move Rx frequency up 1 Hz</td><td><b>Ctrl+F12 </b></td><td>Move Rx and Tx frequencies up 1 Hz</td></tr>
  <tr><td><b>Alt+1-6  </b></td><td>Set now transmission to this number on Tab 1</td></tr>
  <tr><td><b>Ctl+1-6  </b></td><td>Set next transmission to this number on Tab 1</td></tr>
  <tr><td><b>Alt+Ctrl+A    </b></td><td>Clear wanted callsign list</td></tr>
  <tr><td><b>Alt+B/C  </b></td><td>Switch to FT8/FT4 mode</td></tr>
  <tr><td><b>Alt+D    </b></td><td>Decode again at QSO frequency</td><td><b>Shift+D  </b></td><td>Full decode (both windows)</td></tr>
  <tr><td><b>Alt+E    </b></td><td>Erase</td></tr>
  <tr><td><b>Alt+F    </b></td><td>Toggle bypass all text filters</td><td><b>Ctrl+F   </b></td><td>Edit the free text message box</td></tr>
  <tr><td><b>Alt+G    </b></td><td>Generate standard messages</td></tr>
  <tr><td><b>Alt+H    </b></td><td>Halt Tx</td></tr>
  <tr><td><b>Ctrl+L   </b></td><td>Lookup callsign in database, generate standard messages</td></tr>
  <tr><td><b>Alt+M    </b></td><td>Monitor</td></tr>
  <tr><td><b>Alt+N    </b></td><td>Enable Tx</td></tr>
  <tr><td><b>Alt+Q    </b></td><td>Log QSO</td></tr>
  <tr><td><b>Alt+S    </b></td><td>Stop monitoring</td></tr>
  <tr><td><b>Alt+T    </b></td><td>Tune</td></tr>
  <tr><td><b>Alt+V    </b></td><td>Save the most recently completed *.wav file</td></tr>
  <tr><td><b>Alt+Z    </b></td><td>Filter, this shortcut is being supported in main UI and widegraph UI</td></tr>
  <tr><td><b>Esc    </b></td><td>Halt Tx</td></tr>
</table>)"), font});
  }
  m_shortcuts->showNormal ();
  m_shortcuts->raise ();
}

void MainWindow::on_actionSpecial_mouse_commands_triggered()
{
  if(!m_mouseCmnds) {
    QFont font;
    font.setPointSize (10);
    m_mouseCmnds.reset (new HelpTextWindow {tr ("Special Mouse Commands"),tr(R"(<table cellpadding=5>
  <tr>
    <th align="right">Click on</th>
    <th align="left">Action</th>
  </tr>
  <tr>
    <td align="right">Waterfall:</td>
    <td>Set Rx frequency.<br/>
        Double-click to set Rx frequency and decode there.<br/>
        Ctrl-click to set Rx and Tx frequencies.<br/>
        Unlocked TX=RX:<br/>
        use left button to set RX frequency<br/>
        use ALT+left button to set RX frequency and switch on Filter<br/>
        use right button to set TX frequency
    </td>
  </tr>
  <tr>
    <td align="right">Decoded text:</td>
    <td>Double-click to copy second callsign to Dx Call,<br/>
        locator to Dx Grid; change Rx and Tx frequencies to<br/>
        decoded signal's frequency; generate standard messages.<br/>
        If first callsign is your own, Tx frequency is not<br/>
        changed unless CTRL is held down when double-clicking.<br/><br/>
        ALT+double-click will also halt Tx if Enable Tx button is active.<br/><br/>
        CTRL+ALT+double-click will only add second callsign from decoded<br/>
        message into wanted callsign list.
    </td>
  </tr>
  <tr>
    <td align="right">Erase button:</td>
    <td>Click right button to erase QSO window.<br/>
        Click left button to erase Band Activity window.<br/>
        Double-click left or right button to erase QSO <br/>
        and Band Activity windows.
    </td>
  </tr>
</table>
)"), font});
  }
  m_mouseCmnds->showNormal ();
  m_mouseCmnds->raise ();
}

void MainWindow::on_DecodeButton_clicked (bool /* checked */)	//Decode request
{
  if(!m_rxDone) { ui->DecodeButton->setChecked (false); return; }
  if(!m_decoderBusy && m_mode != "WSPR-2") {
    dec_data.params.newdat=0;
    dec_data.params.nagain=1;
    m_blankLine=false; // don't insert the separator again
	m_manualDecode=true;
    decode();
  }
}

void MainWindow::freezeDecode(int n)                          //freezeDecode()
{
  if(!m_decoderBusy) {
    if((n%100)==2) on_DecodeButton_clicked (true);
    dec_data.params.nagainfil=1;
  }
}

void MainWindow::decode()                                       //decode()
{
  if(m_mode=="JTTY")return;
  if (m_decoderRecovering || proc_jtdxjt9.state() != QProcess::Running) return;
  if(!m_dataAvailable or m_TRperiod==0.0) { m_manualDecode=false; return; }
  decodeBusy(true); // shall be second line
  if(m_autoErase) clearBandActivity();
  if(m_hint && !m_hisCall.isEmpty() && (m_enableTx || m_lasthint)) {
    if(m_curMsgTx.startsWith(m_hisCall+" ")) dec_data.params.nstophint = 0; // DxCall matched to last transmitted message
    m_lasthint=false;
  }
//  printf("%s(%0.1f) Timing decode start: %d\n",m_jtdxtime->currentDateTimeUtc2().toString("hh:mm:ss.zzz").toStdString().c_str(),m_jtdxtime->GetOffset(),dec_data.params.nstophint);
  m_nDecodes = 0;
  m_reply_me = false;
  m_reply_other = false;
  m_reply_CQ73 = false;
  ui->DecodeButton->setChecked (true);
  if(!m_manualDecode) { m_processAuto_done = false; m_callFirst73 = false; }
  m_used_freq = 0;
  if(m_diskData && !m_mode.startsWith("FT")) dec_data.params.nutc=dec_data.params.nutc/100;
  if(dec_data.params.newdat==1) {
    m_msDecStarted=m_jtdxtime->currentMSecsSinceEpoch2();
    if(!m_mode.startsWith("FT")) {
      qint64 ms = m_msDecStarted % 86400000;
      int imin=ms/60000;
      int ihr=imin/60;
      imin=imin % 60;
      if(m_TRperiod>=60.0) imin=imin - (imin % (int(m_TRperiod)/60));
      dec_data.params.nutc=100*ihr + imin;
    }
  }
  if(dec_data.params.newdat==1 && (!m_diskData) && m_mode.startsWith("FT")) {
    qint64 now=m_jtdxtime->currentMSecsSinceEpoch2();
    qint64 period=qRound64(m_TRperiod*1000.0);
    auto time=QDateTime::fromMSecsSinceEpoch(now-now%period,Qt::UTC).time();
    dec_data.params.nutc=time.hour()*10000+time.minute()*100+time.second();
  }

//FT8 block of parameters
  dec_data.params.nQSOProgress = m_QSOProgress;
  dec_data.params.nftx = ui->TxFreqSpinBox->value ();
  if(m_freqNominal < 30000000) dec_data.params.napwid=5; // FT8AP decoding bandwidth for 'mycall hiscall ???' and RRR,RR73,73 messages
  else if(m_freqNominal < 100000000) dec_data.params.napwid=15;
  else dec_data.params.napwid=50;
  setFt8Threads(m_ft8threads);
  dec_data.params.nmt=m_ft8threads;
  dec_data.params.ncandthin=m_ncandthin;
  dec_data.params.ndtcenter=100 * ui->DTCenterSpinBox->value();
  dec_data.params.nft8cycles=m_nFT8Cycles;
  dec_data.params.nft8swlcycles=m_nFT8SWLCycles;
  if(m_houndMode) { dec_data.params.nft8rxfsens=1; } else { dec_data.params.nft8rxfsens=m_nFT8RXfSens; }
  dec_data.params.nft4depth=m_nFT4depth;
  dec_data.params.nmodernft8=ui->actionModernFT8Decode->isChecked() ? 1 : 0;
  dec_data.params.nmodernft4=ui->actionModernFT4Decode->isChecked() ? 1 : 0;
  if(m_ft8Sensitivity==0) dec_data.params.lft8lowth=false;
  else  dec_data.params.lft8lowth=true;
  if(m_ft8Sensitivity==2) dec_data.params.lft8subpass=true;
  else dec_data.params.lft8subpass=false;
  dec_data.params.ltxing=(m_enableTx && (m_jtdxtime->currentMSecsSinceEpoch2()-m_mslastTX) < 26000) ? 1 : 0;
  dec_data.params.lhidetest=(ui->actionHide_FT_contest_messages->isChecked() && !m_bypassAllFilters) ? 1 : 0;
  dec_data.params.lhidetelemetry=(ui->actionHide_telemetry_messages->isChecked() && !m_bypassAllFilters) ? 1 : 0;
  dec_data.params.lhideft8dupes=ui->actionHide_FT8_dupe_messages->isChecked() ? 1 : 0;
  dec_data.params.lhound=m_houndMode ? 1 : 0;
  dec_data.params.lhidehash=m_config.hide2ndHash() && !m_bypassAllFilters;
  dec_data.params.lcommonft8b=m_commonFT8b;
  dec_data.params.lmycallstd=m_bMyCallStd; dec_data.params.lhiscallstd=m_bHisCallStd;
  dec_data.params.lapmyc=m_lapmyc;
  dec_data.params.lmodechanged=m_modeChanged ? 1 : 0; m_modeChanged=false;
  dec_data.params.lbandchanged=m_bandChanged ? 1 : 0; m_bandChanged=false;
  dec_data.params.lmultinst=m_multInst ? 1 : 0;
  dec_data.params.lskiptx1=m_skipTx1 ? 1 : 0;
  dec_data.params.nlasttx=m_nlasttx;
  dec_data.params.lforcesync=ui->syncButton->isChecked() && m_mode=="FT8";
  dec_data.params.learlystart=m_FT8EarlyStart ? 1 : 0;

  dec_data.params.nsecbandchanged=m_nsecBandChanged; m_nsecBandChanged=0;
  dec_data.params.nswl=m_swl ? 1 : 0;
  dec_data.params.nfilter=m_filter ? 1 : 0;
  dec_data.params.nagcc=m_agcc ? 1 : 0;
  dec_data.params.nhint=m_hint ? 1 : 0;
  dec_data.params.ndelay=m_delay;
  dec_data.params.nfqso=m_wideGraph->rxFreq();
  dec_data.params.ndepth=m_ndepth;
  dec_data.params.nranera=m_config.ntrials();
  dec_data.params.ntrials10=m_config.ntrials10();
  dec_data.params.ntrialsrxf10=m_config.ntrialsrxf10();
  dec_data.params.nprepass=m_config.npreampass();
  dec_data.params.naggressive=m_config.aggressive();
  dec_data.params.nharmonicsdepth=m_config.harmonicsdepth();
  dec_data.params.ntopfreq65=m_config.ntopfreq65();
  dec_data.params.nsdecatt=m_config.nsingdecatt();
  dec_data.params.fmaskact=m_config.fmaskact();
  dec_data.params.ndiskdat=0;
  if(m_diskData) dec_data.params.ndiskdat=1;
  dec_data.params.nfa=m_wideGraph->nStartFreq();
  dec_data.params.nfSplit=m_wideGraph->Fmin();
  dec_data.params.nfb=m_wideGraph->Fmax();
  if(m_config.single_decode() && (m_mode=="JT65" || m_mode=="JT9" || m_mode=="T10" || m_mode=="JT9+JT65")) {
    dec_data.params.nfa=qMax(0,dec_data.params.nfqso-50);
    dec_data.params.nfb=qMin(5000,dec_data.params.nfqso+50);
  }
  dec_data.params.ntol=50; // this value is not being used 
  if(m_mode=="JT9+JT65") {
    dec_data.params.ntol=20;
  }
  if(dec_data.params.nutc < m_nutc0) m_RxLog = 1;       //Date and Time to all.txt
  m_nutc0=dec_data.params.nutc;

  if(m_modeTx=="JT65") { dec_data.params.ntxmode=65; }
  else if(m_modeTx=="JT9") { dec_data.params.ntxmode=9; }
  
  if(m_mode=="FT8") dec_data.params.nmode=8;
  else if(m_mode=="FT4") dec_data.params.nmode=4;
  else if(m_mode=="FT2") dec_data.params.nmode=2;
  else if(m_mode=="JT9+JT65") dec_data.params.nmode=9+65;
  else if(m_mode=="JT9") dec_data.params.nmode=9;
  else if(m_mode=="JT65") dec_data.params.nmode=65;
  else if(m_mode=="T10") {
    dec_data.params.nmode=10;
    dec_data.params.ntxmode=10;
    dec_data.params.ntol=20; }
  
  dec_data.params.ntrperiod=int(m_TRperiod);

//  strncpy(dec_data.params.datetime, m_dateTime.toLatin1(), 20);
  strncpy(dec_data.params.mycall, (m_config.my_callsign()+"            ").toLatin1(),12);
  strncpy(dec_data.params.mybcall, (Radio::base_callsign(m_config.my_callsign())+"            ").toLatin1(),12);
//  strncpy(dec_data.params.mygrid, (m_config.my_grid()+"      ").toLatin1(),6);
  QString hisCall=m_hisCall;
  QString hisGrid=m_hisGrid;
  strncpy(dec_data.params.hiscall,(hisCall+"            ").toLatin1(),12);
  if(hisCall.length()<3) { hisCall.clear(); strncpy(dec_data.params.hisbcall,(hisCall+"            ").toLatin1(),12); }
  else strncpy(dec_data.params.hisbcall,(Radio::base_callsign(hisCall)+"            ").toLatin1(),12);
  strncpy(dec_data.params.hisgrid,(hisGrid+"      ").toLatin1(),6);

  if(!hisCall.isEmpty() && !m_enableTx && hisCall != m_lastloggedcall) dec_data.params.lenabledxcsearch=true;
  else dec_data.params.lenabledxcsearch=false;
  dec_data.params.nsftol=(m_superFoxMode && m_houndMode && m_mode=="FT8") ? m_superFoxTolerance : 0;
  dec_data.params.lwidedxcsearch=m_FT8WideDxCallSearch ? 1 : 0; 

  //newdat=1  ==> this is new data, must do the big FFT
  //nagain=1  ==> decode only at fQSO +/- Tol

  char *to = (char*)mem_jtdxjt9->data();
  char *from = reinterpret_cast<char*>(&dec_data);
  int size=sizeof(struct dec_data);
  if(dec_data.params.newdat==0) {
    int noffset {offsetof (struct dec_data, params.nutc)};
    to += noffset;
    from += noffset;
    size -= noffset;
  }
  {QMutexLocker lock(&captureBufferMutex());memcpy(to, from, qMin(mem_jtdxjt9->size(), size));}
  QFile {m_config.temp_dir ().absoluteFilePath (".lock")}.remove (); // Allow jtdxjt9 to start
  if(m_config.write_decoded_debug()) {
    QString swl{""}, cycles{""};
    if(m_mode=="FT8") {
      if(m_swl) cycles = "SWL On, cycles: " + QString::number(m_nFT8SWLCycles);
      else cycles = "SWL Off, cycles: " + QString::number(m_nFT8Cycles);
    }
    else swl = (m_swl ? "SWL On " : "SWL Off ");
    writeToALLTXT("Decoder started " + swl + cycles);
  }
//  m_msDecoderStarted = m_jtdxtime->currentMSecsSinceEpoch2();
}

void MainWindow::setManagedMode(ManagedMode mode)
{
  on_stopTxButton_clicked();
  if(mode!=ManagedMode::Off && (m_mode.startsWith("WSPR") || m_mode=="JTTY" || m_mode=="Echo"))mode=ManagedMode::Off;
  if(mode!=ManagedMode::Off && m_houndMode)ui->actionEnable_hound_mode->setChecked(false);
  m_managed.setMode(mode,m_managedClock.elapsed());m_managedTxReady=false;
  m_managedContext=m_config.my_callsign()+"|"+QString::number(m_freqNominal)+"|"+m_mode;
  clearDX(" cleared, managed mode changed");
  for(auto action:m_managedMenu->actions())action->setChecked(action->data().toInt()==int(mode));
  ui->menuAutoSeq->setEnabled(!m_managed.active());ui->menuDXpedition->setEnabled(!m_managed.active());
  ui->AutoSeqButton->setEnabled(!m_managed.active());ui->AutoTxButton->setEnabled(!m_managed.active());
  ui->HoundButton->setEnabled(!m_managed.active());
  statusBar()->showMessage(mode==ManagedMode::Off?tr("Managed mode is off."):tr("Managed mode is listening. Enable TX manually to allow calls and scheduled CQ."),15000);
}

void MainWindow::observeManagedDecode(DecodedText& decoded)
{
  if(messageBlacklisted(decoded.message()))return;
  if(!m_managed.active() || m_diskData || m_manualDecode || decoded.isDebug() || decoded.isWrong())return;
  ManagedCandidate c;QString type;
  auto cq=decoded.CQersCall(c.grid,type);
  decoded.deCallAndGrid(c.call,c.grid);
  if(!cq.isEmpty())c.call=cq;
  c.call.remove('<');c.call.remove('>');c.call=c.call.toUpper();
  if(!Radio::is_callsign(c.call) || Radio::base_callsign(c.call)==m_baseCall)return;
  auto words=decoded.message().split(' ',SkipEmptyParts);
  c.incoming=!m_baseCall.isEmpty() && Radio::base_callsign(decoded.call())==m_baseCall;
  if(c.incoming) {
    QString reportType;decoded.report(m_baseCall,Radio::base_callsign(c.call),c.receivedReport,reportType);
    if(words.last()=="73")c.responseStage=5;
    else if(words.last()=="RR73" || words.last()=="RRR")c.responseStage=4;
    else if(!c.receivedReport.isEmpty())c.responseStage=reportType=="R"?3:2;
    else if(gridOK(c.grid) || words.size()==2)c.responseStage=1;
  }
  c.direction=type;
  c.available=!cq.isEmpty() || (words.size()==3 && (words.last()=="73" || words.last()=="RR73" || words.last()=="RRR"));
  // Respect directed CQs independently of AutoSeq's optional filters.
  if(!cq.isEmpty() && !type.isEmpty() && type.size()<=2) {
    QString own,other;m_logBook.getDXCC(m_config.my_callsign(),own);m_logBook.getDXCC(c.call,other);
    auto ownParts=own.split(',');auto otherParts=other.split(',');
    bool addressed=m_config.my_callsign().startsWith(type) || ownParts.value(0).trimmed()==type || ownParts.value(1).trimmed()==type
      || (type=="DX" && ownParts.value(0)!=otherParts.value(0));
    if(!addressed)c.available=false;
  }
  c.snr=decoded.snr();c.frequency=decoded.frequencyOffset();c.utcSeconds=decoded.timeInSeconds();c.seen=m_managedClock.elapsed();
  c.mode=m_mode=="JT9+JT65"?(decoded.isJT65()?"JT65":"JT9"):m_mode;
  m_managed.observe(c);
}

ManagedCandidate MainWindow::chooseManagedCandidate(bool incomingOnly)
{
  if(!m_logReady)return {};
  auto candidates=m_managed.candidates(m_managedClock.elapsed(),qint64(m_TRperiod*2000+2000));
  candidates.erase(std::remove_if(candidates.begin(),candidates.end(),[this](ManagedCandidate const& c){
    return permanentlyBlocked(c.call) || !managedCandidateSlotAllowed(c)
      || (!c.incoming && !ManagedSession::currentInvitation(c.utcSeconds,
          m_jtdxtime->currentMSecsSinceEpoch2(),qRound64(m_TRperiod*1000)));
  }),candidates.end());
  for(auto& c:candidates) {
    QString country;bool worked=true,bandMode=true;
    m_logBook.matchCall(c.call,country,worked,bandMode,m_freqNominal,c.mode);
    c.workedBandMode=bandMode;c.novelty=0;
    if(!worked)c.novelty|=NewCall;if(!bandMode)c.novelty|=NewBandModeCall;
    worked=bandMode=true;m_logBook.matchCQZ(c.call,country,worked,bandMode,m_freqNominal,c.mode);if(!worked)c.novelty|=NewCqZone;
    worked=bandMode=true;m_logBook.matchITUZ(c.call,country,worked,bandMode,m_freqNominal,c.mode);if(!worked)c.novelty|=NewItuZone;
    worked=bandMode=true;m_logBook.matchDXCC(c.call,country,worked,bandMode,m_freqNominal,c.mode);if(!worked)c.novelty|=NewDxcc;if(!bandMode)c.novelty|=NewBandModeDxcc;
    if(gridOK(c.grid) && !gridRR73(c.grid)){worked=bandMode=true;m_logBook.matchGrid(c.grid,worked,bandMode,m_freqNominal,c.mode);if(!worked)c.novelty|=NewGrid;}
  }
  return m_managed.choose(candidates,m_config.managed_settings().profile(m_managed.mode()),m_managedClock.elapsed(),incomingOnly);
}

bool MainWindow::managedTxAllowed() const
{
  const bool continuingFinal=m_signoff.matches(m_hisCall,generalContext())
    && m_signoffSelectionRevision==m_generalSelectionRevision;
  const bool logsReady=m_logReady && !m_logLoading && !m_logImportActive && !m_logRefreshPending;
  const bool sameContext=m_managedContext==m_config.my_callsign()+"|"+QString::number(m_freqNominal)+"|"+m_mode;
  int limit=m_config.managed_settings().profile(m_managed.mode()).callLimit;
  const bool budgetReached=m_managed.budgetReached(limit,selectedTransmitText());
  return generalTxAllowed() && !permanentlyBlocked(m_hisCall) && !messageBlacklisted(selectedTransmitText())
    && watchdogTxAllowed() && (!m_managed.active() || ((logsReady || continuingFinal)
      && !budgetReached && m_managedTxReady && sameContext && m_managed.current()==m_hisCall));
}

bool MainWindow::watchdogTxAllowed() const
{
  if(!m_watchdogFinishing)return !m_txwatchdog;
  if(m_hisCall!=m_watchdogCall || m_watchdogContext!=m_config.my_callsign()+"|"+QString::number(m_freqNominal)+"|"+m_mode || m_ntx==6)return false;
  auto message=m_ntx==7?ui->genMsg->text():m_ntx==8?ui->freeTextMsg->currentText():QString();
  return !message.trimmed().startsWith("CQ ");
}

void MainWindow::prepareManagedTransmission()
{
  m_managedTxStarted=false;m_managedTxConsumed=false;m_managedTxNatural=false;m_managedTxCounted=false;
  m_managedCqOnAir=m_managed.active() && !m_tune && m_currentMessage.trimmed().startsWith("CQ ");
  m_managedCallOnAir=m_managed.active() && !m_tune && !m_managedCqOnAir && !m_managed.current().isEmpty();
  if(!m_managedCqOnAir && !m_managedCallOnAir)return;
  m_managedTxMessage=m_currentMessage;m_managedTxCall=m_managed.current();m_managedTxContext=generalContext();
  m_managedTxGeneration=++m_signoffGeneration;
  m_managedTxPeriod=qRound64(m_TRperiod*1000);
  m_managedTxSlot=m_managedTxPeriod>0?m_jtdxtime->currentMSecsSinceEpoch2()/m_managedTxPeriod*m_managedTxPeriod:-1;
}

void MainWindow::managedTransmissionFinished()
{
  if(m_managedTxCounted || !m_managedTxStarted || !m_managedTxConsumed || !m_managedTxNatural)return;
  if(!m_managed.active() || !m_enableTx || !m_manualTxAuthorized || m_tune
     || m_managedTxContext!=generalContext() || m_managedTxCall!=m_managed.current())return;
  m_managedTxCounted=true;
  if(m_managedCqOnAir)m_managed.cqTransmitted(m_managedClock.elapsed());
  if(m_managedCallOnAir)m_managed.transmitted(m_managedTxMessage,m_managedTxSlot,m_managedTxPeriod);
  m_managedCqOnAir=false;m_managedCallOnAir=false;
}

void MainWindow::queueOrdinaryAutoSelection()
{
  if(!m_managed.active() && m_autoseq && m_callMode>0 && m_enableTx && m_manualTxAuthorized
     && !m_houndMode && m_hisCall.isEmpty() && !m_diskData && !m_manualDecode) {
    m_ordinaryAutoPending=true;
    m_ordinaryAutoContext=generalContext();
    m_ordinaryAutoSelectionRevision=m_generalSelectionRevision;
  }
}

void MainWindow::process_Auto()
{
  finishSignoff();
  refreshOrdinaryBusy();
  if(m_ordinaryAutoPending) {
    if(!m_enableTx || !m_manualTxAuthorized || !m_autoseq || m_callMode==0 || m_managed.active()
       || m_houndMode || !m_hisCall.isEmpty()
       || m_ordinaryAutoContext!=generalContext()
       || m_ordinaryAutoSelectionRevision!=m_generalSelectionRevision) {
      m_ordinaryAutoPending=false;return;
    }
    if(m_decoderBusy || m_diskData || m_manualDecode || m_transmitting || g_iptt || m_tx_when_ready
       || ptt0Timer.isActive() || ptt1Timer.isActive() || m_rigState.ptt()
       || !m_logReady || m_logLoading || m_logImportActive || m_logRefreshPending)return;
    m_ordinaryAutoPending=false;
    m_processAuto_done=true;
  }
  if(m_generalState.waiting())return;
  if(m_config.full_duplex() && (m_transmitting || g_iptt))return;
  if(permanentlyBlocked(m_hisCall))return;
  if(m_watchdogFinishing || m_txSession.expired(m_config.watchdog()))txwatchdog(true);
  if(m_txwatchdog)return;
  if(ordinaryBusyWaiting()) {
    if(!ordinaryRetryDue())return;
    resetOrdinaryBusy(); // Continue into the normal count-exhaustion handoff.
  }
  if(upstreamMode() && !m_managed.active() && !m_watchdogFinishing
     && m_upstreamState.waitingFor73(m_config.repeat_Tx(),m_mode,m_upstream->context(),m_hisCall))return;
  const bool managed=m_managed.active();
  if(managed) {
    if(m_transmitting || g_iptt!=0 || m_tx_when_ready || ptt0Timer.isActive()
       || ptt1Timer.isActive() || m_rigState.ptt())return;
    m_managedTxReady=false;
    auto context=m_config.my_callsign()+"|"+QString::number(m_freqNominal)+"|"+m_mode;
    if(context!=m_managedContext){m_managed.clearCandidates();clearDX(" cleared, managed context changed");m_managedContext=context;}
    bool finalPending=m_signoff.matches(m_hisCall,generalContext()) && m_signoffSelectionRevision==m_generalSelectionRevision;
    if(!m_manualTxAuthorized || !m_enableTx || ((!finalPending) && (m_logLoading || m_logImportActive || m_logRefreshPending)) || m_diskData || m_manualDecode)return;
    if(m_mode.startsWith("WSPR") || m_houndMode){setManagedMode(ManagedMode::Off);return;}
    if(m_hisCall!=m_managed.current()) {m_managed.cancelAttempt();clearDX(" cleared, managed selection changed");}
  }
  int count = 0;
  int prio = 0;
  bool counters = true;
  m_status = QsoHistory::NONE;
  QString hisCall = m_hisCall;
  QString rpt = m_rpt;
  QString grid = m_hisGrid;
  QString mode = "";
  unsigned time = 0;
  int rx = ui->RxFreqSpinBox->value ();
  int tx = ui->TxFreqSpinBox->value ();
  QStringList StrStatus = {"NONE","RFIN","RCQ","SCQ","RCALL","SCALL","RREPORT","SREPORT","RRREPORT","SRREPORT","RRR","SRR","RRR73","SRR73","R73","S73","FIN"};
  if (!hisCall.isEmpty ()) {
    if (m_houndMode) count = -1; //marker for changing status to FIN when status is RRR73
    m_status = m_qsoHistory.autoseq(hisCall,grid,rpt,rx,tx,time,count,prio,mode);
    if(!managed && !m_houndMode)count=m_qsoHistory.sent_count(hisCall);
    // History records selected/sent final texts before physical and protocol
    // completion. Retain the partner until the independent handshake confirms.
    if(m_signoff.matches(hisCall,generalContext())) {
      m_status=m_signoff.message()==SignoffState::RR73?QsoHistory::SRR73:QsoHistory::S73;
      count=m_signoff.transmissions();
    }
    if (m_transmitting) count -=1;
    if(m_config.write_decoded_debug()) {
      QString StrDirection = "";
      if(m_status == QsoHistory::FIN) StrDirection = " auto sequence is finished;";
      else if(m_status == QsoHistory::NONE) StrDirection = " auto sequence is not started;";
      writeToALLTXT("hisCall:" + hisCall + " time:" + QString::number(time) + " autoseq: " + StrDirection + " status: " + StrStatus[m_status] + " count: " + QString::number(count)+ " prio: " + QString::number(prio));
    }
    if (!m_houndMode && (!m_singleshot || managed) && !m_transmitting && g_iptt == 0
        && !m_signoff.active() && m_status == QsoHistory::FIN) {
      if(m_watchdogFinishing){finishTxWatchdog();return;}
      if(managed)m_managed.complete(m_managedClock.elapsed());
      m_qsoHistory.complete(hisCall);
      clearDX(" cleared, continuous QSO completed");
      if(m_config.set_RXtoTX()) {
        rx=ui->TxFreqSpinBox->value();
        ui->RxFreqSpinBox->setValue(rx);
      }
      hisCall.clear();
      grid.clear();
      rpt.clear();
      count = 0;
      m_counter = 0;
      m_status = QsoHistory::NONE;
    }
    if(!managed) {
    if (m_houndMode ) { //WSJT-X Fox will drop QSO if R+Report from Hound is not decoded after three attempts 
      if (m_status == QsoHistory::SRREPORT || m_status == QsoHistory::RREPORT) {
        if(count > 3) {
          haltTx("DXpQSO failed after three TX of R+REPORT message ");
          count = m_qsoHistory.reset_count(hisCall,QsoHistory::RCQ);
          ui->TxFreqSpinBox->setValue (m_lastCallingFreq);
          m_status = QsoHistory::RCQ;
         } else if (m_houndTXfreqJumps && rx > 199 && rx < 1000) {
          if (count == 1) {
             ui->TxFreqSpinBox->setValue (rx);
           } else if (rx < 600) {
             ui->TxFreqSpinBox->setValue (rx+300);
           } else {
             ui->TxFreqSpinBox->setValue (rx-300);
           }
         }
      }
    } else if (!m_signoff.active() && !m_transmitting && g_iptt == 0 && m_status == QsoHistory::FIN && !m_singleshot && !m_config.autolog() && m_lastloggedcall == m_hisCall && !m_lockTxFreq &&
        (tx == 1 || abs(rx - ui->TxFreqSpinBox->value ()) > m_nguardfreq)) { 
      clearDX (" cleared, AutoSeq QSO finished");
      hisCall = m_hisCall;
      grid = m_hisGrid;
      m_status = QsoHistory::NONE;
    } else if (!m_transmitting && g_iptt == 0 && (m_status == QsoHistory::RFIN || m_status == QsoHistory::RCQ || m_status == QsoHistory::SCALL || (m_status == QsoHistory::SREPORT && m_skipTx1 && !m_houndMode)) && m_config.answerCQCount() &&
        (m_config.nAnswerCQCounter() <= count || (!m_autoseq && !m_watchdogFinishing && m_reply_other))) {
      if(m_config.nAnswerCQCounter()<=count)m_qsoHistory.deferOutgoingCall(hisCall);
      clearDX (" cleared, RCQ/SCALL/SREPORT count reached");
      if (m_reply_other)
          m_counter = 0;
      else {
          m_counter = m_config.nAnswerCQCounter(); 
          m_qsoHistory.calllist(hisCall,rpt.toInt(),time);
      }
      count = m_qsoHistory.reset_count(hisCall);
      hisCall = m_hisCall;
      grid = m_hisGrid;
      m_status = QsoHistory::NONE;
      if (m_singleshot)
        counters = false;
    } else if (!m_transmitting && g_iptt == 0 && (m_status == QsoHistory::RCALL || (m_status == QsoHistory::SREPORT && !m_skipTx1)) && m_config.answerInCallCount() &&
        (m_config.nAnswerInCallCounter() <= count || (!m_autoseq && !m_watchdogFinishing && m_reply_other))) {
      clearDX (" cleared, RCALL/SREPORT count reached");
      m_qsoHistory.calllist(hisCall,rpt.toInt(),time);
      count = m_qsoHistory.reset_count(hisCall);
      hisCall = m_hisCall;
      grid = m_hisGrid;
      m_status = QsoHistory::NONE;
      m_counter = 0;
      if (m_singleshot)
        counters = false;
    } else if (!m_transmitting && g_iptt == 0 && (m_status == QsoHistory::RREPORT || m_status == QsoHistory::SRREPORT) && m_config.sentRReportCount() &&
        m_config.nSentRReportCounter() <= count) {
      clearDX (" cleared, RREPORT/SRREPORT count reached");
      count = m_qsoHistory.reset_count(hisCall);
      hisCall = m_hisCall;
      grid = m_hisGrid;
      m_status = QsoHistory::NONE;
      m_counter = 0;
      if (m_singleshot)
        counters = false;
    } else if (!m_transmitting && g_iptt == 0 && (m_status == QsoHistory::RRREPORT || m_status == QsoHistory::RRR || m_status == QsoHistory::SRR || m_status == QsoHistory::RRR73 || m_status == QsoHistory::R73 || m_status == QsoHistory::SRR73 || m_status == QsoHistory::S73) &&
        m_config.sentRR7373Count() && m_config.nSentRR7373Counter() <= count
        && (!m_signoff.active() || m_signoff.hadReceiveOpportunity())) {
      clearDX (" cleared, RRR|RR73|R73 count reached");
      count = m_qsoHistory.reset_count(hisCall);
      hisCall = m_hisCall;
      grid = m_hisGrid;
      m_status = QsoHistory::NONE;
      m_counter = 0;
      if (m_singleshot)
        counters = false;
    }
    }
  }

  // A managed QSO may have answered earlier but fail at the final exchange.
  // Keep its own profile limit; ordinary AutoSeq counters do not govern it.
  int managedLimit=managed?m_config.managed_settings().profile(m_managed.mode()).callLimit:0;
  if(managed && m_signoff.active() && !m_transmitting && !g_iptt && !m_decoderBusy
     && m_signoff.hadReceiveOpportunity() && managedLimit>0
     && m_signoff.transmissions()>=managedLimit) {
    m_managed.failed(m_managedClock.elapsed());m_qsoHistory.remove(hisCall);
    clearDX(" cleared, final-message retry limit reached");
    hisCall.clear();grid.clear();rpt.clear();m_status=QsoHistory::NONE;
  }
  if(managed && !m_signoff.active() && !hisCall.isEmpty() && !m_decoderBusy && m_managed.exhausted(m_config.managed_settings().profile(m_managed.mode()).callLimit)) {
    m_managed.failed(m_managedClock.elapsed());m_qsoHistory.remove(hisCall);clearDX(" cleared, managed unanswered call limit");
    hisCall.clear();grid.clear();rpt.clear();m_status=QsoHistory::NONE;
  }
  // An expired session may finish only the captured QSO. Count exhaustion or
  // a cleared selection ends it before incoming/outgoing candidates or CQ.
  if(m_watchdogFinishing && (hisCall.isEmpty() || hisCall!=m_watchdogCall)) {finishTxWatchdog();return;}
  if (hisCall.isEmpty () && counters && !m_houndMode && (m_callMode!=0 || managed) && !m_transmitting && g_iptt == 0) {
    if(managed) {
      // Use the candidates already decoded while the selected TX slot is
      // open. Continuing decoder work must not postpone an available reply.
      if(m_decoderBusy && !m_bTxTime)return;
      QElapsedTimer selectionClock;selectionClock.start();
      auto candidate=chooseManagedCandidate(true);
      bool cq=!m_decoderBusy && m_managed.cqDue(m_managedClock.elapsed());
      if(candidate.call.isEmpty() && !cq)candidate=chooseManagedCandidate();
      if(selectionClock.elapsed()>1000 || m_config.write_decoded_debug())
        writeTxTiming(QString("Managed selection timing: elapsed_ms=%1 decoder_busy=%2 candidate=%3")
          .arg(selectionClock.elapsed()).arg(m_decoderBusy).arg(candidate.call));
      if(!candidate.call.isEmpty()) {
        if(!applyManagedCandidateSlot(candidate))return;
        hisCall=candidate.call;grid=candidate.grid;rpt=QString::number(candidate.snr);rx=candidate.frequency;time=candidate.utcSeconds;mode=candidate.mode;
        m_managed.begin(hisCall);
        // A newly selected QSO starts from the latest received invitation.
        // In particular an earlier FIN/S73 must not complete it immediately.
        m_qsoHistory.remove(hisCall);
        QString country;m_logBook.getDXCC(hisCall,country);auto countryParts=country.split(',');
        auto received=candidate.incoming?(candidate.receivedReport.isEmpty()?QsoHistory::RCALL:QsoHistory::RREPORT):QsoHistory::RCQ;
        m_qsoHistory.message(hisCall,received,0,candidate.receivedReport.isEmpty()?grid:candidate.receivedReport,
          candidate.direction,countryParts.value(0),countryParts.value(1),time,rpt,rx,mode);
        m_status=m_qsoHistory.autoseq(hisCall,grid,rpt,rx,tx,time,count,prio,mode);
        if(m_status==QsoHistory::NONE || m_status==QsoHistory::FIN)m_status=candidate.incoming?QsoHistory::RCALL:QsoHistory::RCQ;
        // Scheduling follows the most recent receive slot, not a prior QSO's start.
        time=candidate.utcSeconds;
      } else if(cq) {m_managedTxReady=true;on_txb6_clicked();if(txTabIndex()==1)ui->genMsg->setText(ui->tx6->text());}
    } else {
    if(m_callPrioCQ || m_callWorkedB4) { time=1; }    //highiest priority, evaluating response to CQ first, then searching CQ decoded messages
    else { if (m_counter > 0) m_counter -= 1; time=0; } //highiest priority, evaluating response to CQ only
    // Highlight preferences affect ranking, never permission to call a worked
    // station. DisplayText gives unworked band/mode calls a baseline rank.
    if (m_answerWorkedB4) time |= 128;
    if (m_callWorkedB4) time |= 64;
    else if (m_callHigherNewCall) time |= 256;
    if (m_rprtPriority) time |= 16;
    if (m_maxDistance) time |= 32;
    m_status = m_qsoHistory.autoseq(hisCall,grid,rpt,rx,tx,time,count,prio,mode);
    if(m_config.write_decoded_debug()) {
      QString StrDirection = "";
      if(m_status == QsoHistory::FIN) StrDirection = " auto sequence is finished;";
      else if(m_status == QsoHistory::NONE) StrDirection = " auto sequence is not started;";
      QString StrPriority = "";
      if (!hisCall.isEmpty ()) {
        if (prio > 27) StrPriority = " New CQZ ";
        else if (prio > 23) StrPriority = " New ITUZ ";
        else if (prio > 19) StrPriority = " New DXCC ";
        else if (prio == 19 ||  prio == 4) StrPriority = " Wanted Call ";
        else if (prio == 18 ||  prio == 3) StrPriority = " Wanted Prefix ";
        else if (prio == 17 ||  prio == 2) StrPriority = " Wanted Country ";
        if (m_status > QsoHistory::RREPORT) StrPriority += " Resume interrupted QSO ";
      }
      writeToALLTXT("hisCall:" + hisCall + "mode:" + mode + StrPriority + " time:" + QString::number(time) +  " autoselect: " + StrDirection + " status: " + StrStatus[m_status] + " count: " + QString::number(count)+ " prio: " + QString::number(prio));
    }
    }
    if(permanentlyBlocked(hisCall)) {m_qsoHistory.discardDecodedCall(hisCall);return;}
    if (!hisCall.isEmpty ()) {
      // Managed selection already checked and applied its slot policy before
      // beginning the attempt. Ordinary AutoSeq keeps its existing behavior.
      if(!managed)assignTxSlot(ManagedSlotPolicy::replyTxFirst(time,m_TRperiod));
      if (m_callToClipboard) clipboard->setText(hisCall);
      ui->dxCallEntry->setText(hisCall);
      if(m_mode=="JT9+JT65" && m_modeTx != mode) {
      m_modeTx = mode;
      if (m_modeTx == "JT9") ui->pbTxMode->setText("Tx JT9  @");
      else ui->pbTxMode->setText("Tx JT65  #");
      m_wideGraph->setModeTx(m_modeTx);
      ui->TxFreqSpinBox->setValue (rx);
      }
      if (!rpt.isEmpty () && rpt == m_rpt) m_rpt = "-60";
    } else if (!managed && m_transmittedQSOProgress != CALLING){
        on_txb6_clicked();
        if(txTabIndex()==1) ui->genMsg->setText(ui->tx6->text());
    }
  }
  if(managed && !hisCall.isEmpty())m_managedTxReady=true;
//  printf("%s(%0.1f) process_Auto: %s,%s,%s,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",m_jtdxtime->currentDateTimeUtc2().toString("hh:mm:ss.zzz").toStdString().c_str(),m_jtdxtime->GetOffset(),m_hisCall.toStdString().c_str(),hisCall.toStdString().c_str(),m_lastloggedcall.toStdString().c_str(),mode.toStdString().c_str(),m_status,prio,ui->TxFreqSpinBox->value (),m_used_freq,m_callMode,m_callPrioCQ,m_reply_other,m_reply_me,counters2);

  if (rx > 0 && rx != ui->RxFreqSpinBox->value ()) ui->RxFreqSpinBox->setValue (rx);
  //if (tx > 0 && tx != ui->TxFreqSpinBox->value ()) ui->TxFreqSpinBox->setValue (tx);
  if (m_status > QsoHistory::NONE){
    if(!grid.isEmpty() && m_hisGrid.left(4) != grid) {
       ui->dxGridEntry->setText(grid);
    }
    if (time < 86400 && m_status < QsoHistory::R73) {
      m_dateTimeQSOOn = m_jtdxtime->currentDateTimeUtc2();
      m_dateTimeQSOOn.setTime(QTime::fromMSecsSinceStartOfDay(time*1000));
      if (m_jtdxtime->currentDateTimeUtc2() < m_dateTimeQSOOn) m_dateTimeQSOOn = m_dateTimeQSOOn.addSecs(-86400);
    }
    if (!rpt.isEmpty () && rpt != m_rpt) {
      ui->rptSpinBox->setValue(rpt.toInt());
      if (m_status == QsoHistory::SREPORT) {
        if (m_skipTx1 && !m_houndMode) { m_status=QsoHistory::RCQ; }
        else { m_status=QsoHistory::RREPORT; }
      }
      genStdMsgs(rpt);
    }
    switch (m_status) {
      case QsoHistory::RFIN:
      case QsoHistory::RCQ: {
        if (m_skipTx1 && !m_houndMode) {
          on_txb2_clicked();
          if(txTabIndex()==1) { ui->genMsg->setText(ui->tx2->text()); m_ntx=7; } }
        else {
          on_txb1_clicked();
          if(txTabIndex()==1) { ui->genMsg->setText(ui->tx1->text()); m_ntx=7; } }
        break;
      }
      case QsoHistory::RCALL: {
        if(m_enableTx && m_autoseq && m_callMode>0) txwatchdog (false);
        on_txb2_clicked();
        if(txTabIndex()==1) ui->genMsg->setText(ui->tx2->text());
        if(!managed && m_autofilter && m_enableTx && !m_filter) autoFilter (true);
        break;
      }
      case QsoHistory::RREPORT: {
        if(m_enableTx && m_autoseq && m_callMode>0) txwatchdog (false);
        if(!managed && m_autofilter && m_enableTx && !m_filter) autoFilter (true);
        on_txb3_clicked();
        if(txTabIndex()==1) ui->genMsg->setText(ui->tx3->text());
        break;
      }
      case QsoHistory::RRREPORT: {
        on_txb4_clicked();
        if(txTabIndex()==1) ui->genMsg->setText(ui->tx4->text());
        break;
      }
      case QsoHistory::RRR: {
        on_txb5_clicked();
        if(txTabIndex()==1) ui->genMsg->setText(ui->tx5->currentText());
        break;
      }
      case QsoHistory::RRR73: {
        if(!m_houndMode) { on_txb5_clicked(); if(txTabIndex()==1) ui->genMsg->setText(ui->tx5->currentText()); }
        else { 
          auto curtime=m_jtdxtime->currentDateTimeUtc2();
          if(m_lastloggedcall!=m_hisCall || qAbs(curtime.toMSecsSinceEpoch()-m_lastloggedtime.toMSecsSinceEpoch()) > int(m_TRperiod) * 7000) {
            m_logqso73=true;
            logQSOTimer.start (0);
          }
          if(m_enableTx || m_transmitting || m_btxok || g_iptt==1) haltTx("end of DXpedition QSO ");
        }
        break;
      }
      case QsoHistory::SRR73: {
        if (!managed && !m_signoff.active() && m_singleshot && !m_transmitting && g_iptt == 0)
          autoStopTx("SRR73, single QSO completed ");
        break;
      }
      case QsoHistory::R73: {
        on_txb5_clicked();
        if(txTabIndex()==1) ui->genMsg->setText(ui->tx5->currentText());
        break;
      }
      case QsoHistory::S73:
      case QsoHistory::FIN: {
        if (!managed && !m_signoff.active() && (m_singleshot || m_houndMode) && !m_transmitting && g_iptt == 0)
          autoStopTx("end of single QSO ");
        break;
      }
      default: {
        break;
      }
    }
  } else {
    if (m_enableTx && m_hisCall.isEmpty()) ui->RxFreqSpinBox->setValue (ui->TxFreqSpinBox->value ());
    if (!counters) {
       if(m_singleshot) { autoStopTx("m_singleshot, counter triggered "); }
       else if(m_houndMode) { autoStopTx("m_houndMode, counter triggered "); }
    }
  }
}

void MainWindow::readFromStdout()
{
  if (m_decoderRecovering) { proc_jtdxjt9.readAllStandardOutput(); return; }
  while(proc_jtdxjt9.canReadLine()) {
    QByteArray text=proc_jtdxjt9.readLine();
    bool finished=text.startsWith("<DecodeFinished>");
    processDecodedOutput(text);
    if(finished)return;
  }
}

void MainWindow::processDecodedOutput(QByteArray t)
{
    if(t.contains("$VERIFY$")) {
      mcpEvent("superfox_signature",{{"verified",false},{"message",QString::fromLatin1(t).trimmed()}});
      return;
    }
    if(t.startsWith("<DecodeFinished>")) {
      bool managedCycle=m_managed.active() && !m_manualDecode && !m_diskData;
      const bool signoffCycle=!m_manualDecode && !m_diskData && m_monitoring && m_dataAvailable;
      // The waterfall's lost-audio label describes the previous period's
      // frame notifications, which are normally incomplete just after TX.
      // Final-message confirmation instead intersects this decode with the
      // actual audio coverage of the same RX slot in signoffReceiveCycleFinished.
      const bool managedReceiveCycle=signoffCycle && !m_lostaudio;
      const qint64 signoffPeriod=qRound64(m_TRperiod*1000);
      const qint64 signoffSlot=signoffPeriod>0?m_msDecStarted/signoffPeriod*signoffPeriod:-1;
      dec_data.params.nstophint=1;
      m_bDecoded = t.mid (20).trimmed ().toInt () > 0;
      int mswait=750.0*m_TRperiod;
      if(!m_diskData) killFileTimer.start (mswait); //Kill in 3/4 of period
    // autoseq guard frequency band
      if(m_modeTx == "FT8") m_nguardfreq = 51;
      else if(m_modeTx == "FT4") m_nguardfreq = 84;
      else if(m_modeTx == "FT2") m_nguardfreq = 168;
      else if(m_modeTx == "JT65") m_nguardfreq = 176;
      else if(m_modeTx == "JT9") m_nguardfreq = 16;
      else if(m_modeTx == "T10") m_nguardfreq = 67;
      int indxncand=t.indexOf("<ncand>")+7; m_ncand=t.mid(indxncand,5).toInt();
      int indxdt=t.indexOf("<avexdt>")+8; QString avexdt = t.mid(indxdt,6).trimmed();
//  printf("%s(%0.1f) Timing decode stop, %d candidates\n",m_jtdxtime->currentDateTimeUtc2().toString("hh:mm:ss.zzz").toStdString().c_str(),m_jtdxtime->GetOffset(),m_ncand);
      // Late results can arrive after CQ has already started. Keep this
      // decision pending until PTT is released instead of spending the only
      // AutoSeq pass while selection is prohibited by the transmit state.
      queueOrdinaryAutoSelection();
      if (m_autoseq && !m_managed.active() && !m_processAuto_done && !m_manualDecode) { m_processAuto_done = true; process_Auto(); }
      m_okToPost=true;
      m_RxLog=0;
      m_startAnother=m_loopall;
      m_blankLine=true;
      m_notified=false;
      if(m_config.write_decoded_debug()) writeToALLTXT("Decoding finished");
      QString slag="";
      qint64 msDecFin=m_jtdxtime->currentMSecsSinceEpoch2();
      if(!m_manualDecode && !m_diskData) {
        qint64 periodms=m_TRperiod*1000;
        qint64 lagms=msDecFin - periodms*((m_msDecStarted / periodms)+1); // rounding to base int
        float lag=lagms/1000.0; slag.setNum(lag,'f',2); if(lag > 0.0) slag="+"+slag;
      }
      if(m_diskData && m_mode.startsWith("FT")) { // estimated lag for FT4||FT8 wav file
        qint64 lagms=msDecFin-m_msDecStarted;
        if(m_mode=="FT8") {
          if(m_swl) lagms-=300; 
          else if(m_FT8EarlyStart) lagms-=1200;
          else lagms-=900;
        } else {
          lagms-=1430;	
        }
        float lag=lagms/1000.0; slag.setNum(lag,'f',2); if(lag > 0.0) slag="+"+slag;
      }
      int navexdt=qAbs(100.*avexdt.toFloat());
      if(m_mode.startsWith("FT")) {
        ui->decodedTextLabel->setText("UTC     dB   DT "+tr("Freq  ")+" "+tr("Avg=")+avexdt+" "+tr("Lag=")+slag+"/"+QString::number(m_nDecodes));
        if(m_mode=="FT8") {
          if(!m_lostaudio) {
            if(navexdt<76) ui->label_6->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#fdedc5",m_useDarkStyle)));
            else if(navexdt>75 && navexdt<151) ui->label_6->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#ffff00",m_useDarkStyle)));
            else if(navexdt>150) ui->label_6->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#ff8000",m_useDarkStyle)));
            if(navexdt>75) ui->label_6->setText(tr("check time"));
            else  ui->label_6->setText(tr("Band Activity"));
          }
          else m_lostaudio=false;
          if (ui->syncButton->isChecked() && !m_config.ntp_enabled()) {
            if (navexdt > 19) m_jtdxtime->SetOffset(m_jtdxtime->GetOffset() - avexdt.toFloat());
            ui->syncButton->setChecked(false);
          } else if (!ui->syncButton->isEnabled()) ui->syncButton->setEnabled(true);
        }
        else if (m_mode=="FT4") {
          if(navexdt<41) ui->label_6->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#fdedc5",m_useDarkStyle)));
          else if(navexdt>40 && navexdt<81) ui->label_6->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#ffff00",m_useDarkStyle)));
          else if(navexdt>80) ui->label_6->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#ff8000",m_useDarkStyle)));
          if(navexdt>40) ui->label_6->setText(tr("check time"));
          else  ui->label_6->setText(tr("Band Activity"));
        }
      }
      else ui->decodedTextLabel->setText("UTC     dB   DT "+tr("Freq  ")+" "+tr("Lag=")+slag);
      dec_data.params.nagain=0; dec_data.params.nagainfil=0; dec_data.params.ndiskdat=0;
      m_manualDecode=false; ui->DecodeButton->setChecked (false);
      QFile {m_config.temp_dir ().absoluteFilePath (".lock")}.open(QIODevice::ReadWrite);
      decodeBusy(false);
      if(managedCycle && managedReceiveCycle && !m_decoderRecovering)
        m_managed.receiveCycleFinished(signoffSlot,signoffPeriod);
      if(signoffCycle)signoffReceiveCycleFinished(signoffSlot);
      // Rows are collected while the decoder is busy. Release an ordinary
      // target wait now, before another decode can occupy the next GUI tick.
      ordinaryBusyTick();
      if(managedCycle || m_ordinaryAutoPending){m_processAuto_done=true;process_Auto();}
      return;
    } else {
      if(messageBlacklisted(QString::fromUtf8(t)))return;
      if(t.indexOf(m_baseCall.toLatin1()) >= 0 || m_config.write_decoded() || m_config.write_decoded_debug()) {
        QFile f {m_config.log_directory().absoluteFilePath (m_jtdxtime->currentDateTimeUtc2().toString("yyyyMM_")+"ALL.TXT")};
        if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
          QTextStream out(&f);
          if (m_RxLog==1) {
            out << m_jtdxtime->currentDateTimeUtc2().toString("yyyyMMdd_hhmmss")
                << "  " << qSetRealNumberPrecision (12) << (m_freqNominal / 1.e6) << " MHz  "
                << m_mode << 
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                 endl;
#else
                 Qt::endl;
#endif

            m_RxLog=0;
          }
          out << m_jtdxtime->currentDateTimeUtc2().toString("yyyyMMdd_") << t.trimmed() << 
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                 endl;
#else
                 Qt::endl;
#endif

          f.close();
        } else {
          JTDXMessageBox::warning_message (this, "", tr ("File Open Error")
                                       , tr ("Cannot open \"%1\" for append: %2")
                                       .arg (f.fileName ()).arg (f.errorString ()));
        }
      }

      DecodedText decodedtext {QString::fromUtf8(t).remove(QRegularExpression{"\r|\n"}),this,upstreamMode()};
      if(m_blankLine) {
        ui->decodedTextBrowser->new_period();ui->decodedTextBrowser2->new_period();
        if(m_config.insert_blank()) {
          auto utc=m_jtdxtime->currentDateTimeUtc2();
          auto currentUtc=utc;
          utc.setTime(QTime::fromMSecsSinceStartOfDay(decodedtext.timeInSeconds()*1000));
          if(currentUtc.secsTo(utc)>43200)utc=utc.addDays(-1);
          ui->decodedTextBrowser->insertPeriodSpacer(utc,m_config.bands()->find(m_freqNominal),m_mode,m_diskData);
        }
        m_blankLine=false;
      }

       if (!m_notified && m_config.beepOnFirstMsg () && !m_diskData) {
          if (m_windowPopup) {
			 this->showNormal();
			 this->raise();
			 QApplication::setActiveWindow(this);
          }
          QApplication::beep();
          m_notified=true;
       }
	   
      if (!decodedtext.isDebug() && !decodedtext.isWrong()) {
        m_mcpDecodes.append(QJsonObject{{"id", ++m_mcpDecodeId}, {"utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
          {"raw", decodedtext.string()}, {"message", decodedtext.message()}, {"snr", decodedtext.snr()}, {"dt", decodedtext.dt()},
          {"audio_hz", decodedtext.frequencyOffset()}, {"mode", m_mode}, {"dial_hz", static_cast<double>(m_freqNominal)}, {"disk", m_diskData}});
        if (m_mcpDecodes.size() > 500) m_mcpDecodes.removeFirst();
      }
//      DecodedText decodedtext {"161545  -4  0.1 1939 & CQ RT9K/4    ",this};
	  QString tcut = t.replace("\n","");
	  if (!m_mode.startsWith("FT") && !upstreamMode()) {
		tcut = tcut.remove(0,21);
	  } else {
		tcut = tcut.remove(0,23);
	  }
      if (!decodedtext.isDebug()) m_nDecodes ++;
      observeManagedDecode(decodedtext);
      processGeneralDecode(decodedtext.string());
      auto decodedtextmsg = decodedtext.message();
      bool mycallinmsg = false;
      if (!m_baseCall.isEmpty () && Radio::base_callsign (decodedtext.call()) == m_baseCall) mycallinmsg = true;

      // extract de
      QString deCall="";
      QString grid="";
      decodedtext.deCallAndGrid(/*out*/deCall,grid);
      if (!m_transmitting && g_iptt==0 && !m_hisCall.isEmpty() && !deCall.isEmpty() && Radio::base_callsign (m_hisCall) == Radio::base_callsign (deCall)) ui->RxFreqSpinBox->setValue (decodedtext.frequencyOffset());
      if (!m_transmitting && g_iptt==0 && !deCall.isEmpty() && !m_reply_me && Radio::base_callsign (deCall) == Radio::base_callsign (m_hisCall)) {
          if (mycallinmsg) {
             m_reply_me = true;
             m_reply_other = false;
          } else if (decodedtextmsg.contains ("73") || decodedtextmsg.left(2) == "CQ") {
             m_reply_other = false;
             m_reply_CQ73 = true;
          } else if (!m_reply_CQ73) m_reply_other = true;
      }
      //Left (Band activity) window
      bool bypassRxfFilters = false;
      int notified = 2;
      notified = ui->decodedTextBrowser->displayDecodedText (&decodedtext
                                                    , m_baseCall
                                                    , Radio::base_callsign (m_hisCall)
                                                    , m_hisGrid.left(4)
                                                    , m_notified
                                                    , m_logBook
                                                    , m_qsoHistory
                                                    , m_qsoHistory
                                                    , m_freqNominal
                                                    , m_mode
                                                    , bypassRxfFilters
                                                    , m_bypassAllFilters
                                                    , m_wideGraph->rxFreq()
                                                    , m_wantedCallList
                                                    , m_wantedPrefixList
                                                    , m_wantedGridList
                                                    , m_wantedCountryList
                                                    , m_windowPopup
                                                    , this
                                                    , m_managed.active()
                                                    );
      if(m_position != 0) ui->decodedTextBrowser->horizontalScrollBar()->setValue(m_position);
	  if (notified & 1) m_notified = true;

      //Right (Rx Frequency) window
	  
	  // content free messages to (Rx Frequency) window functionality
	  bool bcontent = false;
	  if (m_config.enableContent ()) {
		if (tcut.contains ("/")) {
			QStringList slcontent = m_config.content ().split(",");
			foreach (QString item, slcontent) {
				if (item.length() > 2 && item.length() < 7) {
					if (tcut.contains ("/" + item)) {
						bcontent = true;
						break;
					}
				}
			}
		}
	  }
	  
	  // DXCall 73/RR73 message to RX frequency window in scenario where Enable TX is turned off
	  bool bdxcall73 = false;
	  QStringList message_words = words_re.match (tcut).capturedTexts ();
	  if(message_words.length () == 4 && !m_enableTx) {
	    QString dxbasecall = Radio::base_callsign (m_hisCall);
	    if (!dxbasecall.isEmpty () && message_words.at (2).contains (dxbasecall) && message_words.at (3).contains ("73")) bdxcall73 = true;
	  }
	  
      if (qAbs(decodedtext.frequencyOffset() - m_wideGraph->rxFreq()) <= 10 || (m_showMyCallMsgRxWindow && mycallinmsg) || bcontent || bdxcall73 || (m_showWantedCallRxWindow && notified & 8)) {

        if(m_bypassRxfFilters || dec_data.params.nagain==1 || dec_data.params.nagainfil==1) 
			bypassRxfFilters = true;

        // This msg is within 10 hertz of our tuned frequency



        ui->decodedTextBrowser2->displayDecodedText(&decodedtext
                                                    , m_baseCall
                                                    , Radio::base_callsign (m_hisCall)
                                                    , m_hisGrid.left(4)
                                                    , m_notified
                                                    , m_logBook
                                                    , m_qsoHistory2
                                                    , m_qsoHistory
                                                    , m_freqNominal
                                                    , m_mode
                                                    , bypassRxfFilters
                                                    , m_bypassAllFilters
                                                    , m_wideGraph->rxFreq());


        m_QSOText=decodedtext.string();
		bcontent = false;
      }

      observeOrdinaryBusy(decodedtext);
      if (mycallinmsg && !m_manualDecode) {
         if (!deCall.isEmpty() && Radio::base_callsign (deCall) == Radio::base_callsign (m_hisCall)) {
           if (!m_processAuto_done && (m_autoseq || m_managed.active()) && ((!decodedtextmsg.contains(" 73") && !decodedtextmsg.contains("RR73"))
           || (decodedtextmsg.contains(" 73") && m_status==QsoHistory::RRREPORT && m_rrr) || (decodedtextmsg.contains("RR73") && m_status==QsoHistory::RREPORT) || m_callMode==0 || m_singleshot || m_houndMode)) {
             m_processAuto_done = true;
             process_Auto();
           } else if ((decodedtextmsg.contains(" 73") || decodedtextmsg.contains("RR73")) && m_callMode<=1) m_callFirst73 = true;
         } else if (!m_managed.active() && m_callMode==1 && !m_processAuto_done && m_autoseq && m_hisCall.isEmpty()) {
           m_processAuto_done = true;
           process_Auto();
         }
      } else if (!deCall.isEmpty() && Radio::base_callsign (deCall) == Radio::base_callsign (m_hisCall) && decodedtextmsg.left(3) != "CQ " && decodedtextmsg.left(3) != "DE " && decodedtextmsg.left(4) != "QRZ " && !decodedtextmsg.contains(" 73") && !decodedtextmsg.contains(" RR73") && !decodedtextmsg.contains(" RRR")) {
        m_used_freq = decodedtext.frequencyOffset();
         if (!m_signoff.active() && !m_managed.active() && m_enableTx && !m_reply_me && !m_houndMode) {
           if(m_autoseq) {
             if(m_config.halttxreplyother())beginOrdinaryBusy(decodedtext);
           } else if(!m_transmitting && g_iptt==0 &&
                     (abs(m_used_freq-ui->TxFreqSpinBox->value())<m_nguardfreq || m_config.halttxreplyother())) {
             haltTx("readFromStdout, not owner of the frequency or reply to other ");
           }
         }
      }
      ordinaryBusyTick();

      if((!m_config.prevent_spotting_false () || (m_config.prevent_spotting_false () && !decodedtext.isWrong ()))
         && (!m_config.filterUDP () || (m_config.filterUDP () && notified & 2))) {
         postDecode (true, decodedtext.string ());
      }
      // find and extract any report for myCall
      QString rpt_type;
      bool stdMsg = decodedtext.report(m_baseCall,
          Radio::base_callsign(m_hisCall), m_rptRcvd,rpt_type);
      // extract details and send to PSKreporter
      if(m_okToPost and m_config.spot_to_psk_reporter () and stdMsg and !m_diskData) {
        QString msgmode="FT8";
        if (m_mode=="FT4" || m_mode=="FT2" || upstreamMode()) msgmode=m_mode;
        else if (decodedtext.isJT65()) msgmode="JT65";
        else if (m_mode.startsWith("JT9")) msgmode="JT9";
        else if (m_mode=="T10") msgmode="T10";

        int audioFrequency = decodedtext.frequencyOffset();
        int snr = decodedtext.snr();
        Frequency frequency = m_freqNominal + audioFrequency;
        pskSetLocal ();
        if(gridOK(grid) && !gridRR73(grid) && !decodedtext.isHint() && !decodedtext.isWrong())
          {
            // qDebug() << "To PSKreporter:" << deCall << grid << frequency << msgmode << snr;
            psk_Reporter->addRemoteStation(deCall,grid,QString::number(frequency),msgmode,
                                           QString::number(snr),
                                           QString::number(m_jtdxtime->currentDateTime2().toTime_t()));
          }
      }
    }
}

void MainWindow::killFile ()
{
  if (m_fnameWE.size () &&
      !(m_saveWav==2 || (m_saveWav==1 && m_bDecoded) || m_fnameWE == m_fileToSave)) {
    QFile f1 {m_fnameWE + ".wav"};
    if(f1.exists()) f1.remove();
    if(m_mode.startsWith ("WSPR")) {
      QFile f2 {m_fnameWE + ".c2"};
      if(f2.exists()) f2.remove();
    }
  }
  if(m_logInitNeeded) {
    printf("%s(%0.1f) Timing Log_init_needed\n",m_jtdxtime->currentDateTimeUtc2().toString("hh:mm:ss.zzz").toStdString().c_str(),m_jtdxtime->GetOffset());
    if(m_config.write_decoded_debug()) writeToALLTXT("Log initialization is started: wsjtx_log.adi file was changed");
    refreshOperatorLogs();
    countQSOs ();
    m_logInitNeeded=false;
  }
}

void MainWindow::set_language (QString const& lang)
{
  if (m_lang != lang) {
    bool olek;
    QString tolge;
    QTranslator translator;
    olek = translator.load (QLocale(lang),"jtdx","_",":/Translations");
    if (!olek) olek = translator.load (QString {"jtdx_"} + lang);
    JTDXMessageBox msgbox;
    msgbox.setWindowTitle(tr("Confirm change Language"));
    msgbox.setIcon(JTDXMessageBox::Question);
    msgbox.setText(tr("Are You sure to change UI Language to English, JTDX will restart?"));
    msgbox.setStandardButtons(JTDXMessageBox::Yes | JTDXMessageBox::No);
    msgbox.setDefaultButton(JTDXMessageBox::No);
    if (olek) {
      tolge = translator.translate("MainWindow","Confirm change Language");
      if (!tolge.isEmpty()) msgbox.setWindowTitle(tolge);
      else msgbox.setWindowTitle("Confirm change Language");
      tolge = translator.translate("MainWindow","Are You sure to change UI Language to English, JTDX will restart?");
      if (!tolge.isEmpty()) msgbox.setText(tolge);
      else msgbox.setText("Are You sure to change UI Language to English, JTDX will restart?");
      tolge = translator.translate("JTDXMessageBox","&Yes");
      if (!tolge.isEmpty()) msgbox.button(JTDXMessageBox::Yes)->setText(tolge);
      else msgbox.button(JTDXMessageBox::Yes)->setText("&Yes");
      tolge = translator.translate("JTDXMessageBox","&No");
      if (!tolge.isEmpty()) msgbox.button(JTDXMessageBox::No)->setText(tolge);
      else msgbox.button(JTDXMessageBox::No)->setText("&No");
    }
    if(msgbox.exec() == JTDXMessageBox::Yes) {
            m_lang = lang;
            m_exitCode = 1337;
            QMainWindow::close();
    }
  }
  if(m_lang=="et_EE") ui->actionEstonian->setChecked(true);
  else if(m_lang=="ru_RU") ui->actionRussian->setChecked(true);
  else if(m_lang=="ca_ES") ui->actionCatalan->setChecked(true);
  else if(m_lang=="hr_HR") ui->actionCroatian->setChecked(true);
  else if(m_lang=="da_DK") ui->actionDanish->setChecked(true);
  else if(m_lang=="nl_NL") ui->actionDutch->setChecked(true);
  else if(m_lang=="es_ES") ui->actionSpanish->setChecked(true);
  else if(m_lang=="fr_FR") ui->actionFrench->setChecked(true);
  else if(m_lang=="hu_HU") ui->actionHungarian->setChecked(true);
  else if(m_lang=="it_IT") ui->actionItalian->setChecked(true);
  else if(m_lang=="lv_LV") ui->actionLatvian->setChecked(true);
  else if(m_lang=="pl_PL") ui->actionPolish->setChecked(true);
  else if(m_lang=="pt_PT") ui->actionPortuguese->setChecked(true);
  else if(m_lang=="pt_BR") ui->actionPortuguese_BR->setChecked(true);
  else if(m_lang=="sv_SE") ui->actionSwedish->setChecked(true);
  else if(m_lang=="zh_CN") ui->actionChinese_simplified->setChecked(true);
  else if(m_lang=="zh_HK") ui->actionChinese_traditional->setChecked(true);
  else if(m_lang=="ja_JP") ui->actionJapanese->setChecked(true);
  else ui->actionEnglish->setChecked(true);
}

void MainWindow::on_EraseButton_clicked()                          //Erase
{
  eraseGeneralMessages();
}

void MainWindow::on_ClearDxButton_clicked()
{
  m_generalPostQsoCall.clear();
  clearDX(" is cleared by ClearDxButton, user action");
  ui->RxFreqSpinBox->setValue(ui->TxFreqSpinBox->value());
}

void MainWindow::decodeBusy(bool b)                             //decodeBusy()
{
  if (b && !m_decoderBusy) m_decoderElapsed.start();
  m_decoderBusy=b;
  if (b && m_firstDecode < 65 && ("JT65" == m_mode || "JT9+JT65" == m_mode)) {
      m_firstDecode += 65;
      if ("JT9+JT65" == m_mode) m_firstDecode = 65 + 9;
  }
  if (b && m_firstDecode != 9 && m_firstDecode != 65 + 9 && ("JT9" == m_mode)) m_firstDecode += 9;
  if (!b)  m_optimizingProgress.reset ();
  ui->DecodeButton->setEnabled(!b);
  ui->actionOpen->setEnabled(!b);
  ui->actionOpen_next_in_directory->setEnabled(!b);
  ui->actionDecode_remaining_files_in_directory->setEnabled(!b);
  statusUpdate ();
}

//------------------------------------------------------------- //guiUpdate()
void MainWindow::guiUpdate()
{
  applyRxAudioBufferSize();
  generalTick();
  updateAstronomy();
  if(upstreamMode() && !m_tune){upstreamGuiUpdate();return;}
  if(m_mode=="JTTY") {
    if(m_watchdogFinishing || m_txSession.expired(m_config.watchdog()))txwatchdog(true);
    auto now=m_jtdxtime->currentDateTimeUtc2();ui->labUTC->setText(now.toString("hh:mm:ss"));date_label->setText(now.toString("dd.MM.yyyy"));
    update_watchdog_label();updateJttyUi();return;
  }
  if (!m_mode.startsWith("WSPR") && m_decoderBusy && !m_decoderRecovering && m_decoderElapsed.isValid()
      && m_decoderElapsed.elapsed() > qMax(120000, int(m_TRperiod * 4000)))
    recoverDecoder(tr("No decoder completion within the timeout"));
  if (!m_mode.startsWith("WSPR") && (m_watchdogFinishing || m_txSession.expired(m_config.watchdog())))
    txwatchdog(true);
  static int iptt0=0;
  static bool btxok0=false;
  static char message[38];
  static char msgsent[38];
  static double onAirFreq0=0.0;
  double txDuration;

  if(m_TRperiod==0.0) m_TRperiod=60.0;
  txDuration=0.0;
  if(m_modeTx=="FT8") txDuration=13.64; //1.0 + 79*1920/12000.0;
  else if(m_modeTx=="FT2") txDuration=3.02;
  else if(m_modeTx=="FT4")  txDuration=6.04; //1.0 + 105*576/12000.0;
  else if(m_modeTx=="JT65") txDuration=47.81142857142857; //1.0 + 126*4096/11025.0;
  else if(m_modeTx=="JT9") txDuration=49.96; //1.0 + 85.0*m_nsps/12000.0;
  else if(m_modeTx=="T10") txDuration=49.96; //1.0 + 85.0*m_nsps/12000.0;
  else if(m_mode=="WSPR-2") txDuration=112.592; // 2.0 + 162*8192/12000.0;

  double tx1=0.0;
  double tx2=txDuration;
  if(m_mode.startsWith("FT")) icw[0]=0;                                   //No CW ID in FT8 mode
  if(icw[0]>0) tx2 += icw[0]*2560.0/48000.0;  //Full length including CW ID
  if(tx2>m_TRperiod) tx2=m_TRperiod;
  
  if(!m_txFirst and m_mode.left(4)!="WSPR") {
    tx1 += m_TRperiod;
    tx2 += m_TRperiod;
  }

  qint64 ms = m_jtdxtime->currentMSecsSinceEpoch2() % 86400000;
  int nsec=ms/1000;
  double tsec=0.001*ms;
  double t2p=fmod(tsec,2.0*m_TRperiod);
  m_nseq = fmod(double(nsec),m_TRperiod);

  if(m_mode.left(4)=="WSPR") {
    if(m_nseq==0 and m_ntr==0) {                   //Decide whether to Tx or Rx
      m_tuneup=false;                              //This is not an ATU tuneup
      if(m_pctx==0) m_WSPR_tx_next = false; //Don't transmit if m_pctx=0
      bool btx = m_enableTx && m_WSPR_tx_next; // To Tx, we need m_enableTx and
                                // scheduled transmit
      if(m_enableTx and m_txNext) btx=true;            //TxNext button overrides
      if(m_enableTx and m_pctx==100) btx=true;         //Always transmit

      if(btx) {
        m_ntr=-1;                          //This says we will have transmitted
        m_txNext=false;
        ui->pbTxNext->setChecked(false);
        m_bTxTime=true;                      //Start a WSPR Tx sequence
      } else {
// This will be a WSPR Rx sequence.
        m_ntr=1;                           //This says we will have received
        m_bTxTime=false;                     //Start a WSPR Rx sequence
      }
    }

  } else {
 // For all modes other than WSPR
    m_bTxTime = (t2p >= tx1) and (t2p < tx2);
  }
  if(m_tune) m_bTxTime=true;                 //"Tune" takes precedence

  if(m_transmitting or m_enableTx or m_tune) {
// Check for "txboth" (testing purposes only)
    QFile f(m_appDir + "/txboth");
    if(f.exists() and fmod(tsec,m_TRperiod)<49.96) m_bTxTime=true; //<(1.0 + 85.0*m_nsps/12000.0)

// Don't transmit another mode in the WSPR sub-band
    Frequency onAirFreq = m_freqNominal + ui->TxFreqSpinBox->value();
    auto mhz = onAirFreq / 1000000;
    Frequency f_from = 0;
    Frequency f_to = 0;
    switch (mhz)
    {
        case 1: {f_from = 1837930; f_to = 1838220; break;}
        case 3: {f_from = 3569930; f_to = 3570220; break;}
        case 5: {f_from = 5288530; f_to = 5288820; break;}
        case 7: {f_from = 7039930; f_to = 7040220; break;}
        case 10: {
            if (m_mode.contains("JT65")) f_from = 10139900; else f_from = 10140030;
            f_to = 10140320;
            break;}
        case 14: {f_from = 14096930; f_to = 14097220; break;}
        case 18: {f_from = 18105930; f_to = 18106220; break;}
        case 21: {f_from = 21095930; f_to = 21096220; break;}
        case 24: {f_from = 24925930; f_to = 24926220; break;}
        case 28: {f_from = 28125930; f_to = 28126220; break;}
        case 50: {f_from = 50294370; f_to = 50294620; break;}
        case 70: {f_from = 70092370; f_to = 70092620; break;}
        default: {f_from = 0;  f_to = 0; break;}
    }
    if ((f_from >0 and (onAirFreq > f_from and onAirFreq < f_to) and m_mode.left(4)!="WSPR") || (m_houndTXfreqJumps &&  ui->TxFreqSpinBox->value() < 1000 && ui->txrb1->isChecked())) {
      m_bTxTime=false;
//      if (m_tune) stop_tuning ();
      if (m_enableTx) enableTx_mode (false);
      if(onAirFreq!=onAirFreq0) {
        onAirFreq0=onAirFreq;
        if (onAirFreq > f_from and onAirFreq < f_to) {
          auto const& message1 = tr ("Please choose another Tx frequency."
                                  " JTDX will not knowingly transmit another"
                                  " mode in the WSPR sub-band.");
#if QT_VERSION >= 0x050400
          QTimer::singleShot (0, [=] { // don't block guiUpdate
            JTDXMessageBox::warning_message (this, "", tr ("WSPR Guard Band"), message1);
          });
#else
          JTDXMessageBox::warning_message (this, "", tr ("WSPR Guard Band"), message1);
#endif
        } else {
          auto const& message1 = tr ("Please choose another Tx frequency."
                                  " JTDX will not allow to Call below 1000 Hz"
                                  " in DXped mode.");
#if QT_VERSION >= 0x050400
          QTimer::singleShot (0, [=] { // don't block guiUpdate
            JTDXMessageBox::warning_message (this, "", tr ("FT8 F/H Tx Guard"), message1);
          });
#else
          JTDXMessageBox::warning_message (this, "", tr ("FT8 F/H Tx Guard"), message1);
#endif
        }
      }
    }

    float fTR=float((ms%int(1000.0*m_TRperiod)))/(1000.0*m_TRperiod);

    if (m_bTxTime && iptt0==0 && fTR<99.0 && m_autoseq && !m_managed.active() && !m_processAuto_done && (m_callMode==2 || (m_callMode<=1 && (m_callFirst73 || m_status >= QsoHistory::RRR73)))) {
      m_processAuto_done = true;
      bool previousSlot=m_txFirst;
      process_Auto();
      if(previousSlot!=m_txFirst)return; // Recompute the new slot before asserting PTT.
    }
    if(m_managed.active() && g_iptt==0 && m_bTxTime && !m_tune) {
      process_Auto();
      double start=m_txFirst?0.0:m_TRperiod;
      m_bTxTime=t2p>=start && t2p<start+txDuration;
    }
    // Check the selected body before asserting PTT and before regenerating a
    // message mid-transmission; the DX field can name a different station.
    if(!m_tune && (permanentlyBlocked(m_hisCall) || messageBlacklisted(selectedTransmitText()))) {
      stopBlacklistedTransmit();return;
    }
    // The configured message budget also supervises a manually selected call
    // when AutoSeq is off. Run the existing handoff policy only at exhaustion.
    if(g_iptt==0 && m_bTxTime && !m_tune && ordinaryRetryDue()) {process_Auto();return;}
    if(g_iptt==0 && (!m_managed.active() || (!m_tx_when_ready && !ptt0Timer.isActive()
       && !ptt1Timer.isActive() && !m_rigState.ptt()))
       && (m_tune || managedTxAllowed()) && ((m_enableTx && m_manualTxAuthorized) || (m_tune && (m_manualTuneAuthorized || m_manualTxAuthorized)))
       && ((m_bTxTime && fTR<99.0) || m_tune)) {
      icw[0]=0;
      m_txStartClock.start();m_txPttAckMs=-1;m_txAudioRequestedMs=-1;m_txStartReported=false;m_txStartAudioGeneration=0;
      g_iptt = 1;
      setRig ();
      setXIT (ui->TxFreqSpinBox->value ());
      Q_EMIT m_config.transceiver_ptt (true);       //Assert the PTT
      m_tx_when_ready = true;
    }
    if(!m_bTxTime and !m_tune) m_btxok=false;       //Time to stop transmitting
  }

  if(m_mode.left(4)=="WSPR" and
     ((m_ntr==1 and m_rxDone) or (m_ntr==-1 and m_nseq>tx2))) {
    if(m_monitoring) m_rxDone=false;
    if(m_transmitting) {
      WSPR_history(m_freqNominal,-1);
      m_bTxTime=false;                        //Time to stop a WSPR transmission
      m_btxok=false;
    }
    else if (m_ntr != -1) {
      WSPR_scheduling ();
      m_ntr=0;                                //This WSPR Rx sequence is complete
    }
  }

  bool haltedEmpty=false;
  // Calculate Tx tones when needed
  if((g_iptt==1 && iptt0==0) || m_restart) {
//----------------------------------------------------------------------
//    printf("%s(%0.1f) Timing transmission start %d %d\n",m_jtdxtime->currentDateTimeUtc2().toString("hh:mm:ss.zzz").toStdString().c_str(),m_jtdxtime->GetOffset(),g_iptt,iptt0);
    QByteArray ba;
    QByteArray ba0;

    if(m_mode.left(4)=="WSPR") {
      QString sdBm,msg0,msg1,msg2;
      sdBm = QString::asprintf(" %d",m_dBm);
      m_tx=1-m_tx;
      int i2=m_config.my_callsign().indexOf("/");
      if(i2>0 or m_grid6) {
        if(i2<0) {                                                 // "Type 2" WSPR message
          msg1=m_config.my_callsign() + " " + m_config.my_grid().left(4) + sdBm;
        } else {
          msg1=m_config.my_callsign() + sdBm;
        }
        msg0="<" + m_config.my_callsign() + "> " + m_config.my_grid()+ sdBm;
        if(m_tx==0) msg2=msg0;
        if(m_tx==1) msg2=msg1;
      } else {
        msg2=m_config.my_callsign() + " " + m_config.my_grid().left(4) + sdBm; // Normal WSPR message
      }
      ba=msg2.toLatin1();
    } else {
      QString txMsg=selectedTransmitText();
      int msgLength=txMsg.trimmed().length();
      if(!m_tune && (msgLength==0 || m_config.my_callsign().isEmpty() || (m_houndMode && (m_QSOProgress == CALLING || m_ntx == 6)))) { 
        haltedEmpty=true;
        if(m_houndMode) m_currentMessage=txMsg; else m_currentMessage.clear();
        if(msgLength==0) haltTx("Transmission of the empty message is not allowed ");
        else if(m_config.my_callsign().isEmpty()) haltTx("Transmission halted: user callsign is not configured ");
        else haltTx("Transmission of CQ message is not allowed in the Hound mode ");
      }
	  else { ba=txMsg.toLocal8Bit(); }
    }

    ba2msg(ba,message);
    int ichk=0;
    if (m_lastMessageSent != m_currentMessage
        || m_lastMessageType != m_currentMessageType)
      {
        m_lastMessageSent = m_currentMessage;
        m_lastMessageType = m_currentMessageType;
      }
    m_currentMessageType = 0;
    if(m_tune) { itone[0]=0; }
    else {
      int len1=22;
      if(m_modeTx=="FT8") {
        int i3=0; int n3=0; char ft8msgbits[77]; int ntxhash=1;
        genft8_(message,&i3,&n3,&ntxhash,msgsent,const_cast<char *> (ft8msgbits),const_cast<int *> (itone),37,37);
        int nsym=79; int nsps=4*1920; float fsample=48000.0; float bt=2.0; float f0=ui->TxFreqSpinBox->value() - m_XIT; int icmplx=0; int nwave=nsym*nsps;
        gen_ft8wave_(const_cast<int *>(itone),&nsym,&nsps,&bt,&fsample,&f0,foxcom_.wave,foxcom_.wave,&icmplx,&nwave);
      }
      else if(m_modeTx=="FT2") {
        int check=0;char bits[77];genft2_(message,&check,msgsent,bits,const_cast<int*>(itone),37,37);
        int nsym=103,nsps=4*288,nwave=105*nsps,icmplx=0;float rate=48000.0f,f0=ui->TxFreqSpinBox->value()-m_XIT;
        gen_ft2wave_(const_cast<int*>(itone),&nsym,&nsps,&rate,&f0,foxcom_.wave,foxcom_.wave,&icmplx,&nwave);
      }
      else if(m_modeTx=="FT4") {
        int ichk=0; char ft4msgbits[77]; int ntxhash=1;
        genft4_(message, &ichk, &ntxhash, msgsent, const_cast<char *> (ft4msgbits),const_cast<int *>(itone),37,37);
        int nsym=103; int nsps=4*576; float fsample=48000.0; float f0=ui->TxFreqSpinBox->value() - m_XIT; int nwave=(nsym+2)*nsps; int icmplx=0;
        gen_ft4wave_(const_cast<int *>(itone),&nsym,&nsps,&fsample,&f0,foxcom_.wave,foxcom_.wave,&icmplx,&nwave);
      }
      else if(m_modeTx=="JT65") { gen65_(message, &ichk, msgsent, const_cast<int *> (itone), &m_currentMessageType, len1, len1); }
      else if(m_modeTx=="JT9") { gen9_(message, &ichk, msgsent, const_cast<int *> (itone), &m_currentMessageType, len1, len1); }
      else if(m_modeTx=="T10") { gen10_(message, &ichk, msgsent, const_cast<int *> (itone), &m_currentMessageType, len1, len1); }							  
      else if(m_mode.left(4)=="WSPR") { genwspr_(message, msgsent, const_cast<int *> (itone), len1, len1); }

      if(m_colorTxMsgButtons && m_mode.left(4)!="WSPR") { if(m_restart) resetTxMsgBtnColor(); setTxMsgBtnColor(); m_txbColorSet=true; }
      msgsent[37]=0;
    }
    m_curMsgTx = QString::fromLatin1 (msgsent);
    if(m_mode.startsWith("FT")) { m_curMsgTx.remove (QChar {'<'}); m_curMsgTx.remove (QChar {'>'}); }
    m_currentMessage = m_curMsgTx;
    if(m_tune) { m_currentMessage = tr("TUNE"); m_currentMessageType = -1; m_nlasttx=0; }
    last_tx_label->setText(tr("LastTx: ") + m_currentMessage.trimmed());
  signoffPttReleased();
	
    if(m_restart && !haltedEmpty) {
      QFile f {m_config.log_directory().absoluteFilePath (m_jtdxtime->currentDateTimeUtc2().toString("yyyyMM_")+"ALL.TXT")};
      if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append))
        {
          QTextStream out(&f);
          out << m_jtdxtime->currentDateTimeUtc2().toString("yyyyMMdd_hhmmss.zzz") << "(" << m_jtdxtime->GetOffset() << ")"
              << "  Retransmitting " << qSetRealNumberPrecision (12) << (m_freqNominal / 1.e6)
              << " MHz +" << ui->TxFreqSpinBox->value () <<"Hz  " << m_modeTx
              << ":  " << m_currentMessage << 
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                 endl;
#else
                 Qt::endl;
#endif

          if(m_config.write_decoded_debug()) {
            QString autoseqa=ui->AutoSeqButton->text(); int indx=autoseqa.length()-1; QString autoseq="AutoSeq"+autoseqa[indx];
            out << m_jtdxtime->currentDateTimeUtc2().toString("yyyyMMdd_hhmmss.zzz") << "(" << m_jtdxtime->GetOffset() << ")"
                << "  AF TX/RX " << ui->TxFreqSpinBox->value () << "/" << ui->RxFreqSpinBox->value ()
                << "Hz " << autoseq << (m_autoseq ? "-On" : "-Off") << " AutoTx" 
                << (m_autoTx ? "-On" : "-Off") << " SShotQSO" << (m_singleshot ? "-On" : "-Off")
                << " Hound mode" << (m_houndMode ? "-On" : "-Off") << " Skip Tx1" << (m_skipTx1 ? "-On" : "-Off") <<
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                 endl;
#else
                 Qt::endl;
#endif

          }
          f.close();
        }
      else
        {
          JTDXMessageBox::warning_message (this, "", tr ("File Open Error")
                                       , tr ("Cannot open \"%1\" for append: %2")
                                       .arg (f.fileName ()).arg (f.errorString ()));
        }
      bool showTx=m_config.TX_messages() || m_autoseq || m_managed.active();
      if (!m_tune)
        {
        if (0 == txTabIndex()) {
          if (ui->txrb5->isChecked() && m_autoseq && !m_managed.active() && m_Tx5setAutoSeqOff) { m_wasAutoSeq=true; on_AutoSeqButton_clicked(false); }
        }
        else if (1 == txTabIndex()) {
          if (ui->rbFreeText->isChecked() && m_autoseq && !m_managed.active() && m_FTsetAutoSeqOff) { m_wasAutoSeq=true; on_AutoSeqButton_clicked(false); }
        }
          ui->decodedTextBrowser2->displayTransmittedText(m_currentMessage,m_baseCall,Radio::base_callsign (m_hisCall),(m_skipTx1 && !m_houndMode ? "S" : ""),m_modeTx,
                     ui->TxFreqSpinBox->value(),m_config.color_TxMsg(),m_qsoHistory,showTx);
        }
    }

    icw[0] = 0;
    auto msg_parts = m_currentMessage.split (' ', SkipEmptyParts);
/*    if (msg_parts.size () > 2) {
      // clean up short code forms
      msg_parts[0].remove (QChar {'<'});
      msg_parts[1].remove (QChar {'>'});
    } */

// m_currentMessageType m_lastMessageType are always 0 in FT8 mode
    auto is_73 = (m_QSOProgress >= ROGER_REPORT || m_ntx==4 || m_ntx==5) && message_is_73 (m_currentMessageType, msg_parts);
	auto lastmsg_is73 = message_is_73 (m_lastMessageType, m_lastMessageSent.split (' ', SkipEmptyParts));
    m_sentFirst73 = is_73 && m_lastloggedcall!=m_hisCall
      && (!lastmsg_is73 || (lastmsg_is73 && !m_lastMessageSent.contains(m_hisCall)));

    if(is_73 && !m_tune)prepareSignoff(m_currentMessage);
    // Actual first samples trigger logging. Single-QSO stop and selection of
    // the next partner wait for finishSignoff() to confirm the final exchange.

    if(!m_mode.startsWith("FT") && m_config.id_interval () >0) {
      int nmin=(m_sec0-m_secID)/60;
      if(nmin >= m_config.id_interval ()) {
        icw[0]=0;
        m_secID=m_sec0;
      }
    }

    if (m_currentMessageType < 6 && msg_parts.length() >= 3
        && (msg_parts[1] == m_config.my_callsign () ||
            msg_parts[1] == m_baseCall))
    {
      int i1;
      bool ok;
      i1 = msg_parts[2].toInt(&ok);
      if(ok and i1>=-50 and i1<50)
      {
        m_rptSent = msg_parts[2];
      } else {
        if (msg_parts[2].mid (0, 1) == "R")
        {
          i1 = msg_parts[2].mid (1).toInt (&ok);
          if (ok and i1 >= -50 and i1 < 50)
          {
            m_rptSent = msg_parts[2].mid (1);
          }
        }
      }
    }
    m_restart=false;
//----------------------------------------------------------------------
  } else {
    if (!m_enableTx && m_sentFirst73)
    {
      m_sentFirst73 = false;
      if (1 == txTabIndex())
      {
        ui->genMsg->setText(ui->tx6->text());
        m_ntx=7;
        ui->rbGenMsg->setChecked(true);
      }
    }
  }

  if (g_iptt == 1 && iptt0 == 0) {
    if(m_config.watchdog () && !m_mode.startsWith ("WSPR")
      && m_curMsgTx != m_msgSent0) {
      txwatchdog (false);  // in case we are auto sequencing
      m_msgSent0 = m_curMsgTx;
    }
// DX Call window must be empty if CQ msg being transmitted under AutoSeq
    if ((m_autoseq || m_managed.active()) && (m_QSOProgress == CALLING || m_ntx == 6) && !m_hisCall.isEmpty()) clearDXfields(" field cleared, CQ message triggered by AutoSeq");
    if (m_houndMode && (m_QSOProgress == REPLYING || m_ntx == 1)) m_lastCallingFreq = ui->TxFreqSpinBox->value ();
    if(m_curMsgTx!=m_msgSent0) m_msgSent0=m_curMsgTx;
    if(!m_tune && !haltedEmpty) {
      QFile f {m_config.log_directory().absoluteFilePath (m_jtdxtime->currentDateTimeUtc2().toString("yyyyMM_")+"ALL.TXT")};
      if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        QTextStream out(&f);
        if(m_config.write_decoded_debug()) {
          QString autoseqa=ui->AutoSeqButton->text(); int indx=autoseqa.length()-1; QString autoseq="AutoSeq"+autoseqa[indx];
          out << m_jtdxtime->currentDateTimeUtc2().toString("yyyyMMdd_hhmmss.zzz") << "(" << m_jtdxtime->GetOffset() << ")"
              << "  JTDX v" << QCoreApplication::applicationVersion () << revision () <<" Transmitting " << qSetRealNumberPrecision (12)
              << (m_freqNominal / 1.e6) << " MHz + " << ui->TxFreqSpinBox->value () <<"Hz  " << m_modeTx << ":  " << m_currentMessage <<
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                 endl
#else
                 Qt::endl
#endif
 << "                   "
              << "  AF TX/RX " << ui->TxFreqSpinBox->value () << "/" << ui->RxFreqSpinBox->value ()
              << "Hz " << autoseq << (m_autoseq ? "-On" : "-Off") << " AutoTx" 
              << (m_autoTx ? "-On" : "-Off") << " SShotQSO" << (m_singleshot ? "-On" : "-Off")
              << " Hound mode" << (m_houndMode ? "-On" : "-Off") <<
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                 endl
#else
                 Qt::endl
#endif
 << " Skip Tx1" << (m_skipTx1 ? "-On" : "-Off")
              << " HaltTxReplyOther" << (m_config.halttxreplyother () ? "-On" : "-Off") <<
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                 endl;
#else
                 Qt::endl;
#endif
 }
        else {
          out << m_jtdxtime->currentDateTimeUtc2().toString("yyyyMMdd_hhmmss.zzz") << "(" << m_jtdxtime->GetOffset() << ")"
              << "  Transmitting " << qSetRealNumberPrecision (12)
              << (m_freqNominal / 1.e6) << " MHz + " << ui->TxFreqSpinBox->value () <<"Hz  " << m_modeTx << ":  " << m_currentMessage <<
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                 endl;
#else
                 Qt::endl;
#endif
 }
        f.close();
      } else {
        JTDXMessageBox::warning_message (this, "", tr ("File Open Error")
                                     , tr ("Cannot open \"%1\" for append: %2")
                                     .arg (f.fileName ()).arg (f.errorString ()));
      }
    }

    if (!m_tune) {
      bool showTx=m_config.TX_messages() || m_autoseq || m_managed.active();
      if (0 == txTabIndex()) {
        if (ui->txrb5->isChecked() && m_autoseq && !m_managed.active() && m_Tx5setAutoSeqOff) { m_wasAutoSeq=true; on_AutoSeqButton_clicked(false); }
      }
      else if (1 == txTabIndex()) {
        if (ui->rbFreeText->isChecked() && m_autoseq && !m_managed.active() && m_FTsetAutoSeqOff) { m_wasAutoSeq=true; on_AutoSeqButton_clicked(false); }
      }
      if(!haltedEmpty) ui->decodedTextBrowser2->displayTransmittedText(m_curMsgTx,m_baseCall,Radio::base_callsign (m_hisCall),(m_skipTx1 && !m_houndMode ? "S" : ""),m_modeTx,
            ui->TxFreqSpinBox->value(),m_config.color_TxMsg(),m_qsoHistory,showTx);
    }
    switch (m_ntx)
      {
      case 1: m_QSOProgress = REPLYING; break;
      case 2: m_QSOProgress = REPORT; break;
      case 3: m_QSOProgress = ROGER_REPORT; break;
      case 4: m_QSOProgress = ROGERS; break;
      case 5: m_QSOProgress = SIGNOFF; break;
      case 6: m_QSOProgress = CALLING; break;
      default: break;             // determined elsewhere
      } 

    m_transmitting = true;
    if (!m_config.tx_QSY_allowed ()) ui->TxFreqSpinBox->setDisabled(true);
    m_transmittedQSOProgress = m_QSOProgress;
    transmitDisplay (true);
    if(m_filter) {
      if(m_FilterState==2) { if(m_QSOProgress == CALLING) autoFilter (false); } // switch filter off at getting 73
      else if(m_FilterState==1) { if (m_QSOProgress == SIGNOFF || (!m_rrr && m_QSOProgress == ROGERS)) autoFilter (false); } // switch filter off at sending 73
    }
    statusUpdate ();
  }

  if((!m_btxok && btxok0 && g_iptt==1) || m_haltTrans) { stopTx(); if(m_haltTrans) m_haltTrans=false; }

  if(m_startAnother) {
    m_startAnother=false;
    on_actionOpen_next_in_directory_triggered();
  }

//Once per second:
  if(nsec != m_sec0) {
    update_watchdog_label();
    if(m_tune && m_config.tunetimer() && !m_tuneup) { //shall not count at WSPR band hopping
      QString remtime;
      remtime = QString::asprintf("%.0f s",StopTuneTimer.remainingTime()/1000.0);
      ui->tuneButton->setText(remtime);
    }
    if(m_monitoring or m_transmitting) {
        int isec=int(fmod(tsec,m_TRperiod));
        progressBar->setValue(isec);
    } else {
        progressBar->setValue(0);
    }

    QString cssSafe = QString("QProgressBar { border: 2px solid %1; border-radius: 5px; background: %2; text-align: center; } QProgressBar::chunk { background: %3; width: 1px; }").arg(Radio::convert_dark("#808080",m_useDarkStyle),Radio::convert_dark("#ffffff",m_useDarkStyle),Radio::convert_dark("#00ff00",m_useDarkStyle));
    QString cssTransmit = QString("QProgressBar { border: 2px solid %1; border-radius: 5px; background: %2; text-align: center; } QProgressBar::chunk { background: %3; width: 1px; }").arg(Radio::convert_dark("#808080",m_useDarkStyle),Radio::convert_dark("#ffffff",m_useDarkStyle),Radio::convert_dark("#ff0000",m_useDarkStyle));

    if(m_transmitting) {
      tx_status_label->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#ffff33",m_useDarkStyle)));
      if(m_tune) tx_status_label->setText(tr("Tx: TUNE"));
      else tx_status_label->setText(tr("Tx: ") + m_curMsgTx.trimmed());
	  progressBar->setStyleSheet(m_config.progressBar_red()?cssTransmit:cssSafe);
    } else if(m_monitoring) {
	  if (!m_txwatchdog) {
		tx_status_label->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#00ff00",m_useDarkStyle)));
		QString t=tr("Receiving ");
		tx_status_label->setText(t);
	  }
      transmitDisplay(false);
      progressBar->setStyleSheet(cssSafe);
    } else if (!m_diskData && !m_txwatchdog) {
      tx_status_label->setStyleSheet("");
      tx_status_label->setText("");
      progressBar->setStyleSheet(cssSafe);
    }
    if(m_transmitting && !m_tune && (m_nseq==10 || m_nseq==11)) { m_lapmyc=1; m_mslastTX = m_jtdxtime->currentMSecsSinceEpoch2(); } //setting twice: make sure it is not skipped
    QDateTime tme = m_jtdxtime->currentDateTimeUtc2();
    QString currentDate = tme.date().toString("dd.MM.yyyy");
    QString utc = tme.time().toString();
    QString hour = tme.time().toString("hh");
    QString minute = tme.time().toString("mm");
    QString second = tme.time().toString("ss");
    int isecond=second.toInt();
    ui->labUTC->setText(utc);
    date_label->setText(currentDate);
	// setting labUTC clock style at start
	if(m_start) setClockStyle(true);
	// setting labUTC clock style at operation
	if ((m_mode=="FT8" && isecond%15==0) || 
        (m_mode=="FT2") || (m_mode=="FT4" && (isecond%15==0 || isecond==8 || isecond==23 || isecond==38 || isecond==53)) ||
        (!m_mode.startsWith("FT") && second=="00")) setClockStyle(false);
	// setting band scheduler
	if((minute.toInt())%5==0 && second == "01" && m_config.usesched() && !m_enableTx) {
        if (m_config.sched_hh_1() == hour && m_config.sched_mm_1() == minute) {
          set_scheduler(m_config.sched_band_1(),m_config.sched_mix_1());
        } else if (!m_config.sched_band_2().isEmpty ()) {
          if (m_config.sched_hh_2() == hour && m_config.sched_mm_2() == minute) {
            set_scheduler(m_config.sched_band_2(),m_config.sched_mix_2());
          } else if (!m_config.sched_band_3().isEmpty ()) {
            if (m_config.sched_hh_3() == hour && m_config.sched_mm_3() == minute) {
              set_scheduler(m_config.sched_band_3(),m_config.sched_mix_3());
            } else if (!m_config.sched_band_4().isEmpty ()) {
              if (m_config.sched_hh_4() == hour && m_config.sched_mm_4() == minute) {
                set_scheduler(m_config.sched_band_4(),m_config.sched_mix_4());
              } else if (!m_config.sched_band_5().isEmpty () && m_config.sched_hh_4() == hour && m_config.sched_mm_4() == minute) {
                set_scheduler(m_config.sched_band_5(),m_config.sched_mix_5());
              }
            }
          }
        }
    }
    m_sec0=nsec;
    if(!m_monitoring and !m_diskData) ui->signal_meter_widget->setValue(0);
    displayDialFrequency ();
    if (m_geometry_restored > 0) { m_geometry_restored -=1; if (m_geometry_restored == 0) restoreGeometry (m_geometry);}

  }
  iptt0=g_iptt;
  btxok0=m_btxok;
}               //End of GUIupdate

void MainWindow::set_scheduler(QString const& setto,bool mixed)
{
  QString newband;
  Frequency frq = 0;

  if (setto.contains(",")) { frq = setto.left(setto.indexOf(" ")).replace("*","").replace(",","").toInt(); }
  else { frq = setto.left(setto.indexOf(" ")).replace("*","").replace(".","").toInt(); }

  if (mixed) {
    newband="JT9+JT65";
    on_actionJT9_JT65_triggered();
  } else {
    newband=setto.mid(setto.indexOf(" ")+1,4);
    if (newband == "FT8") { on_actionFT8_triggered(); }
    else if (newband == "FT4") { on_actionFT4_triggered(); }
    else if (newband == "FT2") { on_actionFT2_triggered(); }
    else if (newband == "JT65") { on_actionJT65_triggered(); }
	else if (newband == "JT9") { on_actionJT9_triggered(); }
	else if (newband == "T10") { on_actionT10_triggered(); }
	else if (newband == "WSPR") { on_actionWSPR_2_triggered(); } 
  }

  m_bandEdited = true;
  band_changed (frq); if(m_config.write_decoded_debug()) writeToALLTXT("Band changed from scheduler, frequency: " + QString::number(frq));
  m_wideGraph->setRxBand (m_config.bands ()->find (frq));
}

void MainWindow::haltTx(QString reason) { m_haltTxWritten=true; writeHaltTxEvent(reason); on_stopTxButton_clicked(); }

void MainWindow::haltTxTuneTimer()
{
  if(m_config.write_decoded_debug()) { writeToALLTXT("Halt Tx triggered: tuning stopped"); m_haltTxWritten=true; }
  on_stopTxButton_clicked();
}

QString MainWindow::operatorLogStamp() const {
  QString stamp=m_config.my_callsign();
  for(auto const& path:LogBook::databaseFiles(OperatorLog::root())) {
    if(path.startsWith(':'))continue;
    QFileInfo info(path);stamp+='|'+path+':'+QString::number(info.exists())+':'+QString::number(info.size())+':'+QString::number(info.lastModified().toMSecsSinceEpoch());
  }
  for(auto const& info:m_config.log_directory().entryInfoList(QDir::Files,QDir::Name)) {
    auto suffix=info.suffix().toLower();
    if(suffix!="adi" && suffix!="adif" && (suffix!="log" || info.fileName()=="decoder-errors.log"))continue;
    stamp+='|'+info.fileName()+':'+QString::number(info.size())+':'+QString::number(info.lastModified().toMSecsSinceEpoch());
  }
  return stamp;
}
void MainWindow::logChanged() {
  // Directory notifications also include our lock and temporary files. They
  // must not recursively trigger a reload which creates another lock file.
  auto stamp=operatorLogStamp();if(stamp==m_logFilesStamp)return;
  ++m_logInputRevision;m_logFilesStamp=stamp;
  if(m_signoff.active() || m_signoffLogWriting || !m_signoffLogUpdates.isEmpty()) {
    m_signoffLogRefreshDeferred=true;m_logRefreshPending=true;return;
  }
  m_logRefreshTimer.start(400);
}

void MainWindow::updateLogProcessingDialog() {
  updateStationSelectorEnabled();
  bool running=m_logLoading || m_logImportActive || m_logRefreshPending;
  if(!running) {
    m_logProgressRequested=false;m_logProgressTimer.stop();
    if(m_logProcessingDialog) {
      // A completed task must not leave a reusable native owned dialog that
      // a window manager can restore with the main window later.
      m_logProcessingDialog->hide();m_logProcessingDialog->deleteLater();
      m_logProcessingDialog.clear();
    }
  } else if(!m_logProgressRequested || isMinimized()) {
    m_logProgressTimer.stop();
    if(m_logProcessingDialog)m_logProcessingDialog->hide();
  } else if(m_logProcessingDialog) {
    m_logProcessingDialog->setStopVisible(m_enableTx || m_tune || m_transmitting);
    m_logProcessingDialog->show();
  } else if(!m_logProgressTimer.isActive())m_logProgressTimer.start();
}
void MainWindow::changeEvent(QEvent *event) {
  QMainWindow::changeEvent(event);
  // Restoring a window changes only presentation, never the log load state.
  if(event->type()==QEvent::WindowStateChange)updateLogProcessingDialog();
}
void MainWindow::watchOperatorLogs() {
  if(!fsWatcher->files().isEmpty()) fsWatcher->removePaths(fsWatcher->files());
  if(!fsWatcher->directories().isEmpty()) fsWatcher->removePaths(fsWatcher->directories());
  fsWatcher->addPath(OperatorLog::root().absolutePath());
  for(auto const& path:LogBook::databaseFiles(OperatorLog::root()))
    if(!path.startsWith(':') && QFile::exists(path))fsWatcher->addPath(path);
  if(!m_config.my_callsign().isEmpty()) {
    fsWatcher->addPath(m_config.log_directory().absolutePath());
    auto paths=OperatorLog::files(m_config.my_callsign());
    auto full=OperatorLog::compatibilityFile(m_config.my_callsign());if(QFile::exists(full))paths.append(full);
    if(!paths.isEmpty()) fsWatcher->addPaths(paths);
  }
}
void MainWindow::refreshOperatorLogs(bool showProgress) {
  m_logProgressRequested=m_logProgressRequested || showProgress;
  m_logInitNeeded=false;m_qsoLogged=false;
  QString call=m_config.my_callsign(),grid=m_config.gridNotif()?m_config.my_grid():QString(),date=m_config.timeFrom();
  QString context=call+'|'+grid+'|'+date,stamp=operatorLogStamp();
  if(context!=m_logReadyContext)m_logReady=false;
  if(m_logLoading || m_logImportActive) {
    // Repeated requests for the same unchanged input share the running load.
    // An import or an actual input/context change must still queue a refresh.
    if(m_logImportActive || context!=m_logLoadContext || stamp!=m_logLoadStamp || m_logLoadRevision!=m_logInputRevision)
      m_logRefreshPending=true;
    updateLogProcessingDialog();return;
  }
  m_logRefreshTimer.stop();
  m_logRefreshPending=false;
  watchOperatorLogs();
  m_logFilesStamp=stamp;m_logLoadStamp=stamp;m_logLoadContext=context;
  m_logLoadRevision=m_logInputRevision;
  m_logLoading=true;
  m_logLoadWatcher.setFuture(QtConcurrent::run([this,call,grid,date] { return m_logLoader.load(call,grid,date); }));
  updateLogProcessingDialog();
}
void MainWindow::importOperatorLogs(QStringList paths) {
  if(m_config.my_callsign().isEmpty() || m_logImportActive) return;
  bool automatic=paths.isEmpty();
  if(automatic) {
    for(auto const& f:m_config.log_directory().entryInfoList(QDir::Files)) {
      QString suffix=f.suffix().toLower();
      if((suffix=="adi" || suffix=="adif" || suffix=="log") && !OperatorLog::isQuarterFile(f.fileName()) && f.fileName()!="decoder-errors.log" && f.fileName().compare("wsjtx_log.adi",Qt::CaseInsensitive)!=0) paths.append(f.absoluteFilePath());
    }
    for(auto name:{"wsjtx_log.adi","wsjtx.log"}) {
      auto path=m_dataDir.absoluteFilePath(name);if(QFile::exists(path)) paths.append(path);
    }
  }
  QStringList selected;
  for(auto const& path:paths) {
    QFileInfo info(path);QString stamp=m_config.my_callsign()+'|'+info.absoluteFilePath()+QString::number(info.size())+info.lastModified().toString(Qt::ISODateWithMs);
    if(!automatic || !m_importAttempts.contains(stamp)) {m_importAttempts.insert(stamp);selected.append(path);}
  }
  if(selected.isEmpty()) return;
  auto call=m_config.my_callsign();auto base=OperatorLog::root();
  m_logProgressRequested=true;
  m_logImportActive=true;
  m_logImportWatcher.setFuture(QtConcurrent::run([call,selected,base]{
    try {return OperatorLog::importFiles(call,selected,base);}
    catch(std::exception const& error) {OperatorLog::ImportResult result;result.errors.append(QString::fromLocal8Bit(error.what()));return result;}
    catch(...) {OperatorLog::ImportResult result;result.errors.append(QObject::tr("Unable to import the log files."));return result;}
  }));
  updateLogProcessingDialog();
}
void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
  if(event->mimeData()->hasUrls()) {
    for(auto const& url:event->mimeData()->urls()) {
      auto suffix=QFileInfo(url.toLocalFile()).suffix().toLower();
      if(!url.isLocalFile() || (suffix!="adi" && suffix!="adif" && suffix!="log")) return;
    }
    event->acceptProposedAction();
  }
}
void MainWindow::dropEvent(QDropEvent *event) {
  if(m_config.my_callsign().isEmpty()) {statusBar()->showMessage(tr("Select a callsign before importing logs."),10000);return;}
  if(m_logImportActive) {statusBar()->showMessage(tr("A log import is already running."),10000);return;}
  QStringList paths;for(auto const& url:event->mimeData()->urls()) if(url.isLocalFile()) paths.append(url.toLocalFile());
  importOperatorLogs(paths);event->acceptProposedAction();
}

void MainWindow::startTx2()
{
  if(m_mode=="JTTY"){startJttyAudio();return;}
  if(upstreamMode() && !m_tune){startUpstreamAudio();return;}
  if(!m_tune && (permanentlyBlocked(m_hisCall) || messageBlacklisted(m_currentMessage)
      || messageBlacklisted(selectedTransmitText()))) {
    stopBlacklistedTransmit();return;
  }
  if ((!m_tune && !managedTxAllowed()) || !((m_enableTx && m_manualTxAuthorized) || (m_tune && (m_manualTuneAuthorized || m_manualTxAuthorized)))) {
    m_tx_when_ready=false;
    Q_EMIT m_config.transceiver_ptt(false);
    return;
  }
//  for(int i=0; i<15; i++) { // workaround to modulator start failure
//    if(i==1) QThread::currentThread()->msleep(5);
//    else if(i>1) QThread::currentThread()->msleep(10);
//    printf("%s(%0.1f) Timing modulator",m_jtdxtime->currentDateTimeUtc2().toString("hh:mm:ss.zzz").toStdString().c_str(),m_jtdxtime->GetOffset());
    bool modulator_active;
    bool tci_active = m_tci;
    if (tci_active) modulator_active=m_tci_mod_active;
    else modulator_active=m_modulator->isActive ();
    if (!modulator_active) { // TODO - not thread safe
      double fSpread=0.0;
      double snr=99.0;
      QString t=ui->tx5->currentText();
      if(t.left(1)=="#") fSpread=t.mid(1,5).toDouble();
      if (tci_active) Q_EMIT m_config.transceiver_spread(fSpread);
      else Q_EMIT audioSpread(fSpread);
      t=ui->tx6->text();
      if(t.left(1)=="#") snr=t.mid(1,5).toDouble();
      if(snr>0.0 or snr < -50.0) snr=99.0;
      prepareManagedTransmission();
      // Every source request needs its own identity, including ordinary
      // non-final messages, so delayed callbacks cannot time a later round.
      m_txStartAudioGeneration=beginAudioPlayback();
      if(m_txStartClock.isValid()) {
        m_txAudioRequestedMs=m_txStartClock.elapsed();
        if(m_txAudioRequestedMs>1000 || m_config.write_decoded_debug())
          writeTxTiming(QString("TX start timing: phase=audio_request mode=%1 call=%2 ptt_ack_ms=%3 audio_request_ms=%4")
            .arg(m_modeTx,m_hisCall).arg(m_txPttAckMs).arg(m_txAudioRequestedMs));
      }
      transmit (snr);
      updateTxButtonAppearance();
//      printf(" started %s\n",m_jtdxtime->currentDateTimeUtc2().toString("hh:mm:ss.zzz").toStdString().c_str());
      if(m_config.write_decoded_debug()) writeToALLTXT("Modulator started");
//      QThread::currentThread()->setPriority(QThread::HighPriority);
      ui->signal_meter_widget->setValue(0);

      if(m_mode.left(4)=="WSPR" and !m_tune) {
        if (m_config.TX_messages ()) {
          t = " Transmitting " + m_mode + " ----------------------- " + m_config.bands ()->find (m_freqNominal);
          t=WSPR_hhmm(0) + ' ' + t.rightJustified (66, '-');
          ui->decodedTextBrowser->displayOwnText(t);
        }

        QFile f {m_config.log_directory().absoluteFilePath ("ALL_WSPR.TXT")};
        if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
          QTextStream out(&f);
          out << m_jtdxtime->currentDateTimeUtc2().toString("yyMMdd hhmm")
              << "  Transmitting " << qSetRealNumberPrecision (12) << (m_freqNominal / 1.e6) << " MHz:  "
              << m_currentMessage << "  " + m_mode <<
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                 endl;
#else
                 Qt::endl;
#endif

          f.close();
        } else {
          JTDXMessageBox::warning_message (this, "", tr ("File Open Error")
                                       , tr ("Cannot open \"%1\" for append: %2")
                                       .arg (f.fileName ()).arg (f.errorString ()));
        }
      }
      return;
    }
  if(m_config.write_decoded_debug()) writeToALLTXT("MainWindow::startTx2() failed to start modulator: m_modulator is active");
}

void MainWindow::stopTx()
{
  m_managedTxNatural=!m_haltTrans && !m_bTxTime && m_enableTx && m_manualTxAuthorized;
  if(!m_managedTxNatural){m_managedCqOnAir=false;m_managedCallOnAir=false;}
  if(m_signoff.active() && !m_haltTrans && !m_bTxTime && m_enableTx && m_manualTxAuthorized)
    m_signoff.naturalEnd();
  if (m_tci) Q_EMIT m_config.transceiver_modulator_stop();
  else Q_EMIT endTransmitMessage ();
  managedTransmissionFinished();
  m_btxok = false;
  m_transmitting = false;
  ui->TxFreqSpinBox->setDisabled(false);
  g_iptt=0;
  if (!m_txwatchdog) {
	  tx_status_label->setStyleSheet("");
	  tx_status_label->setText("");
  }
  if (m_tci) ptt0Timer.start(0); else {
    ptt0Timer.start(200);                //Sequencer delay
    if(!m_monitoroff) monitor (true);
    statusUpdate ();
    m_secTxStopped=m_jtdxtime->currentMSecsSinceEpoch2()/1000;
  }
}

void MainWindow::stopTx2()
{
  Q_EMIT m_config.transceiver_ptt (false);      //Lower PTT
  if (m_tci) {
    if(!m_monitoroff) monitor (true);
    statusUpdate ();
    m_secTxStopped=m_jtdxtime->currentMSecsSinceEpoch2()/1000;
  }
//  QThread::currentThread()->setPriority(QThread::HighPriority);
  if(m_mode.left(4)=="WSPR" and m_ntr==-1 and !m_tuneup) {
    m_wideGraph->setWSPRtransmitted();
    WSPR_scheduling ();
    m_ntr=0;
  }
  last_tx_label->setText(tr("LastTx: ") + m_currentMessage.trimmed());
}

void MainWindow::RxQSY()
{
  // this appears to be a null directive
  if (m_config.is_transceiver_online ()) {
    Q_EMIT m_config.transceiver_frequency(m_freqNominal);
  }
}

void MainWindow::ba2msg(QByteArray ba, char message[])             //ba2msg()
{
  int iz=ba.length();
  for(int i=0; i<37; i++) {
    if(i<iz) {
//      if(int(ba[i])>=97 and int(ba[i])<=122) ba[i]=int(ba[i])-32;
      message[i]=ba[i];
    } else {
      message[i]=32;
    }
  }
  message[37]=0;
}

void MainWindow::on_TxMinuteButton_clicked(bool checked)        //TxFirst
{
  assignTxSlot(checked,m_manualTxSlotChange);
  if(m_transmitting && m_config.write_decoded_debug()) writeToALLTXT("Tx halted: period changed via TX period button");
  if (m_txGenerated != checked && m_enableTx && m_autoseq)  clearDX (" cleared, Tx Minute button clicked");
  setMinButton();
}

void MainWindow::on_rrrCheckBox_stateChanged(int nrrr)        //RRR or RR73
{
  m_rrr = (nrrr==2);
  if(!m_rrr) { ui->pbSendRRR->setText("RR73"); }
  else { ui->pbSendRRR->setText("RRR"); }
  ui->rrr1CheckBox->setChecked(m_rrr);
  if(ui->genMsg->text().contains (" RRR") || ui->genMsg->text().contains (" RR73")) on_pbSendRRR_clicked();
}

void MainWindow::on_rrr1CheckBox_stateChanged(int nrrr)        //RRR or RR73
{
  m_rrr = (nrrr==2);
  ui->rrrCheckBox->setChecked(m_rrr);
  if(!ui->tx4->text().isEmpty ()) genStdMsgs(m_rpt); // can change message for non tx4 transmission?
}

void MainWindow::set_ntx(int n) { m_ntx=n; m_nlasttx=n; } //set_ntx()

void MainWindow::on_txb1_clicked()                                //txb1
{
  m_ntx=1;
  m_QSOProgress = REPLYING;
  m_nlasttx=1;
  ui->txrb1->setChecked(true);
  if (m_transmitting) m_restart=true;
}

void MainWindow::on_txb2_clicked()                                //txb2
{
  m_ntx=2;
  m_QSOProgress = REPORT;
  m_nlasttx=2;
  ui->txrb2->setChecked(true);
  if (m_transmitting) m_restart=true;
}

void MainWindow::on_txb3_clicked()                                //txb3
{
  m_ntx=3;
  m_QSOProgress = ROGER_REPORT;
  m_nlasttx=3;
  ui->txrb3->setChecked(true);
  if (m_transmitting) m_restart=true;
}

void MainWindow::on_txb4_clicked()                                //txb4
{
  m_ntx=4;
  m_QSOProgress = ROGERS;
  m_nlasttx=4;
  ui->txrb4->setChecked(true);
  if (m_transmitting) m_restart=true;
}

void MainWindow::on_txb5_clicked()                                //txb5
{
  m_ntx=5;
  m_QSOProgress = SIGNOFF;
  m_nlasttx=5;
  ui->txrb5->setChecked(true);
  if (m_transmitting) m_restart=true;
}

void MainWindow::on_txb6_clicked()                                //txb6
{
  if(!m_hisCall.isEmpty()) clearDXfields(" field cleared, SLOT on_txb6_clicked()");
//added on 20180907 step .92_33 needs in testing
  ui->labAz->setText("");
  ui->labDist->setText("");
  ui->tx1->setText("");
  ui->tx2->setText("");
  ui->tx3->setText("");
  ui->tx4->setText("");
  ui->tx5->setCurrentText("");
  ui->genMsg->setText("");
//
  if(!m_autoseq && m_wasAutoSeq) { m_wasAutoSeq=false; on_AutoSeqButton_clicked(true); }
  if (m_spotDXsummit){
     ui->pbSpotDXCall->setText(tr("DX Call"));
     if(m_config.spot_to_dxsummit()) { ui->pbSpotDXCall->setStyleSheet(QString("QPushButton {color: 51;background: %2;border-style: outset;border-width: 1px;border-color: %3;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#c4c4ff",m_useDarkStyle),Radio::convert_dark("#808080",m_useDarkStyle))); }
     else { ui->pbSpotDXCall->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: outset;border-width: 1px;border-color: %3;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#aabec8",m_useDarkStyle),Radio::convert_dark("#808080",m_useDarkStyle))); }
     m_spotDXsummit=false;
  }
  m_ntx=6;
  m_QSOProgress = CALLING;
  m_nlasttx=6;
  ui->txrb6->setChecked(true);
  ui->genMsg->setText(ui->tx6->text()); // directional CQ tab1/tab2 sync
  if (m_transmitting) m_restart=true;
}

void MainWindow::doubleClickOnCall(bool alt, bool ctrl)
{
  // Keep a managed on-air send and its retry accounting attached to the
  // existing QSO. Select the next partner once that transmission has ended.
  if(managedSelectionBlocked())return;
  txwatchdog (false);
  QTextCursor cursor;
  if(m_decodedText2) {
    cursor=ui->decodedTextBrowser->textCursor();
  } else {
    cursor=ui->decodedTextBrowser2->textCursor();
  }
  cursor.select(QTextCursor::LineUnderCursor);

  auto selected=cursor.selectedText();
  if(processMessage(selected,selected.size()-1,alt,ctrl)) {
    resetOrdinaryBusy();
    quickCallFromSelection(selected,alt,ctrl);
  }
}

void MainWindow::doubleClickOnCall2(bool alt, bool ctrl)
{
  m_decodedText2=true;
  doubleClickOnCall(alt,ctrl);
  m_decodedText2=false;
}

bool MainWindow::processMessage(QString const& messages, int position, bool alt, bool ctrl)
{
//QString result;
//QTextStream(&result) << position;
//msgBox (result);
  QString t1 = messages.left(position);              //contents up to \n on selected line
  int i1=t1.lastIndexOf("\n") + 1;       //points to first char of line
  QString t2 = messages.mid(i1,position-i1+2).replace("\n","   ");         //selected line
  QString t2disp;
  bool period_changed = false;
  bool tx_message = false;
  auto tcut = t2;
  
  if (!m_mode.startsWith("FT") && !upstreamMode()) {
    t2disp=t2.left(40)+"     "+t2.mid(40,1);
    tcut.remove(0,21);
    tx_message = t2.mid(6,2) == "Tx";
  } else {
    t2disp=t2.left(upstreamMode()?60:50);
    tcut.remove(0,23);
    tx_message = t2.mid(8,2) == "Tx";
  }
  
  QString t2a;
  t2a = t2;
  DecodedText decodedtext {t2a,this,upstreamMode()};
  if(messageBlacklisted(decodedtext.message()))return false;
  QString sender,senderGrid;
  decodedtext.deCallAndGrid(sender,senderGrid);
  sender.remove('<').remove('>');
  // Full-duplex reception can decode our own RF. Reject it before changing
  // the selected partner, slot or frequencies, including on a double click.
  if(!decodedtext.isTX() && !sender.isEmpty()
     && Radio::base_callsign(sender)==Radio::base_callsign(m_config.my_callsign()))return false;

  bool addWanted = (alt && ctrl);
  if(!addWanted) {
//    int nmod=decodedtext.timeInSeconds () % (2*int(m_TRperiod));
    int nmod = qRound(decodedtext.timeInSeconds()/m_TRperiod)%2;
//    printf ("periods %d,%f,%d,%s,%d\n",decodedtext.timeInSeconds (),m_TRperiod,nmod,t2.mid(6,4).toStdString().c_str(),tx_message);
    if (m_txFirst != (nmod!=0) && !tx_message) {
      if(ui->decodedTextBrowser->callSelectionInProgress() || ui->decodedTextBrowser2->callSelectionInProgress())
        m_managedSlotPolicy.changed(m_managedClock.elapsed());
      m_txFirst=(nmod!=0);
//      ui->TxMinuteButton->setChecked(m_txFirst);
//      an attempt to provide stable UDP Reply operation:
      if(m_txFirst && !ui->TxMinuteButton->isChecked()) ui->TxMinuteButton->click();
      else if(!m_txFirst && ui->TxMinuteButton->isChecked()) ui->TxMinuteButton->click();
//      setMinButton();
      period_changed = true;
      if(m_transmitting && m_config.write_decoded_debug()) writeToALLTXT("Tx halted: period changed via message click");
    }
  }

  auto t3 = decodedtext.string ().left(upstreamMode()?60:47);
  auto t4 = t3.replace (QRegularExpression {" CQ ([A-Z]{1,2}|[0-9]{3,3}) "}, " CQ_\\1 ").split (" ", SkipEmptyParts);
  if(t4.size () < 6) return false;             //Skip the rest if no decoded text


  QString hiscall;
  QString hisgrid;
  decodedtext.deCallAndGrid(/*out*/hiscall,hisgrid);

  if(addWanted) {
    if(!hiscall.isEmpty() && hiscall != m_hisCall && hiscall != m_config.my_callsign()) {
      QString t5{""}; if(m_mode.startsWith("FT")) t5=hiscall; else t5=Radio::base_callsign(hiscall);
      QString t6{""};
      if(m_wantedCall.isEmpty())  ui->wantedCall->setText(t5);
      else if(m_wantedCallList.length()<20 && !m_wantedCall.startsWith(t5+",") && !m_wantedCall.endsWith(","+t5) 
              && !m_wantedCall.contains(","+t5+",")) { t6 = m_wantedCall; ui->wantedCall->setText(t6.append(","+t5)); }
    }
    return false;
  }

  if (!Radio::is_callsign (hiscall) // not interested if not from QSO partner
      && !(t4.size () == 7          // unless it is of the form
           && (t4.at (5) == m_baseCall // "<our-call> 73"
               || t4.at (5).startsWith (m_baseCall + '/')
               || t4.at (5).endsWith ('/' + m_baseCall))
           && t4.at (6) == "73"))
    {
      qDebug () << "Not processing message - hiscall:" << hiscall << "hisgrid:" << hisgrid;
//SNR from free messages to report for operation with special callsigns in the manual mode
	  if (!m_autoseq) {
         QString rpt = decodedtext.report();
         ui->rptSpinBox->setValue(rpt.toInt());
         genStdMsgs(rpt);
      }
      return false;
    }
  QString curBand;
  QString prevmodeTx = m_modeTx;
  // only allow automatic mode changes between JT9 and JT65, and when not transmitting
  if (!m_transmitting and m_mode == "JT9+JT65") {
      if (decodedtext.isJT9())
        {
          if (m_modeTx != "JT9" && m_config.pwrBandTxMemory() && !m_tune)
            {
              curBand = ui->bandComboBox->currentText()+m_mode;
              if (m_pwrBandTxMemory.contains(curBand))
                {
                  ui->outAttenuation->setValue(m_pwrBandTxMemory[curBand].toInt());
                }
              else
                {
                  m_pwrBandTxMemory[curBand] = ui->outAttenuation->value();
                }
            }
          m_modeTx="JT9";
          ui->pbTxMode->setText("Tx JT9  @");
          m_wideGraph->setModeTx(m_modeTx);
        } 
      else if (decodedtext.isJT65())
        {
          if (m_modeTx != "JT65" && m_config.pwrBandTxMemory() && !m_tune)
            {
              curBand = ui->bandComboBox->currentText()+"JT65";
              if (m_pwrBandTxMemory.contains(curBand))
                {
//                  printf("auto mode changed %s JT65 %s:%d\n",m_mode.toStdString().c_str(),curBand.toStdString().c_str(),m_pwrBandTxMemory[curBand].toInt());
                  ui->outAttenuation->setValue(m_pwrBandTxMemory[curBand].toInt());
                }
              else
                {
                  m_pwrBandTxMemory[curBand] = ui->outAttenuation->value();
                }
            }
          m_modeTx="JT65";
          ui->pbTxMode->setText("Tx JT65  #");
          m_wideGraph->setModeTx(m_modeTx);
        }
    } else if ((decodedtext.isJT9 () and m_modeTx != "JT9") or
               (decodedtext.isJT65 () and m_modeTx != "JT65")) {
      // if we are not allowing mode change then don't process decode
      return false;
    }

  int frequency = decodedtext.frequencyOffset();
  QString firstcall = decodedtext.call();
  bool TxModeChanged = false;
  if (m_mode == "JT9+JT65") {
	if((decodedtext.isJT9 () and prevmodeTx != "JT9") or (decodedtext.isJT65 () and prevmodeTx != "JT65")) {
      TxModeChanged = true; on_spotLineEdit_textChanged(ui->spotLineEdit->text()); 
    }
  }

  // Don't change Tx freq if in a fast mode;
  // unless m_lockTxFreq is true or CTRL is held down
  if (m_lockTxFreq or ctrl or TxModeChanged) {
     if (ui->TxFreqSpinBox->isEnabled ()) {
        ui->TxFreqSpinBox->setValue(frequency);
     } else {
        return false;
     }
  }

  int i9=m_QSOText.indexOf(decodedtext.string());
  if (i9<0 and !decodedtext.isTX() and m_decodedText2) {
    DecodedText decodedtext {t2disp,this,upstreamMode()};
	if (!t2.contains (m_baseCall) || !m_showMyCallMsgRxWindow) {
		ui->decodedTextBrowser2->displayDecodedText(&decodedtext
                                                  ,m_baseCall
                                                  ,Radio::base_callsign (m_hisCall)
                                                  ,m_hisGrid.left(4)
                                                  ,m_notified
                                                  ,m_logBook
                                                  ,m_qsoHistory2
                                                  ,m_qsoHistory
                                                  ,m_freqNominal
                                                  ,m_mode
                                                  ,m_bypassRxfFilters
                                                  ,m_bypassAllFilters
                                                  ,m_wideGraph->rxFreq());
	}
      m_QSOText=decodedtext.string();
  }

  if (ui->RxFreqSpinBox->isEnabled ())
    {
      ui->RxFreqSpinBox->setValue (frequency); //Set Rx freq
    }
  if (decodedtext.isTX())
    {
      if (ctrl && ui->TxFreqSpinBox->isEnabled ())
        {
          ui->TxFreqSpinBox->setValue(frequency); //Set Tx freq
        }
      return false;
    }

  // prior DX call (possible QSO partner)
  auto qso_partner_base_call = Radio::base_callsign (m_hisCall);
  bool call_changed = false;
  auto base_call = Radio::base_callsign (hiscall);
  if (base_call != Radio::base_callsign(m_hisCall)
      || (base_call != hiscall && hiscall.trimmed().toUpper() != m_hisCall))
    {
  // his base call different or his call more qualified
  // i.e. compound version of same base call
      if (!m_hisGrid.isEmpty()) ui->dxGridEntry->clear();
      i1=m_qsoHistory.reset_count(hiscall);
      if (m_callToClipboard) clipboard->setText(hiscall);
      ui->dxCallEntry->setText(hiscall); ui->dxCallEntry->setStyleSheet(QString("QLineEdit {color: %1; background: %2}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#ffffff",m_useDarkStyle)));
      call_changed = true;
    }
  if (gridOK(hisgrid)) {
    if(m_hisGrid.left(4) != hisgrid) ui->dxGridEntry->setText(hisgrid);
  }
  if (m_hisGrid.isEmpty ())
    lookup();

  QString rpt = decodedtext.report();
  ui->rptSpinBox->setValue(rpt.toInt());
  genStdMsgs(rpt);

// Determine appropriate response to received message
  auto dtext = " " + decodedtext.string () + " ";
  if(dtext.contains (" " + m_baseCall + " ")
     || dtext.contains ("/" + m_baseCall + " ")
     || dtext.contains (" " + m_baseCall + "/")
     || (firstcall == "DE" && ((t4.size () > 7 && t4.at(7) != "73") || t4.size () <= 7)))
    {



      if (t4.size () > 7   // enough fields for a normal msg
          and !gridOK (t4.at (7))) // but no grid on end of msg
        {
          QString r=t4.at (7);
          if(r.left(3)=="RRR" || (r.toInt()==73) || r.left(4)=="RR73") {
            m_ntx=5;
			m_QSOProgress = SIGNOFF;
            m_nlasttx=5;
            ui->txrb5->setChecked(true);
            if(txTabIndex()==1) {
              ui->genMsg->setText(ui->tx5->currentText());
              m_ntx=7;
              ui->rbGenMsg->setChecked(true);
            }
          } else if(r.left(1)=="R") {
            m_ntx=4;
			m_QSOProgress = ROGERS;
            m_nlasttx=4;
            ui->txrb4->setChecked(true);
            if(txTabIndex()==1) {
              ui->genMsg->setText(ui->tx4->text());
              m_ntx=7;
              ui->rbGenMsg->setChecked(true);
            }
          } else if(r.toInt()>=-50 and r.toInt()<=49) {
            m_ntx=3;
			m_QSOProgress = ROGER_REPORT;
            m_nlasttx=3;
            ui->txrb3->setChecked(true);
            if(txTabIndex()==1) {
              ui->genMsg->setText(ui->tx3->text());
              m_ntx=7;
              ui->rbGenMsg->setChecked(true);
            }
          }
        }
      else if (t4.size () == 7 && t4.at (6) == "73") {
        // 73 back to compound call holder
        m_ntx=5;
		m_QSOProgress = SIGNOFF;
        m_nlasttx=5;
        ui->txrb5->setChecked(true);
        if(txTabIndex()==1) {
          ui->genMsg->setText(ui->tx5->currentText());
          m_ntx=7;
          ui->rbGenMsg->setChecked(true);
        }
      }
      else {
        m_ntx=2;
		m_QSOProgress = REPORT;
        m_nlasttx=2;
        ui->txrb2->setChecked(true);
        if(txTabIndex()==1) {
          ui->genMsg->setText(ui->tx2->text());
          m_ntx=7;
          ui->rbGenMsg->setChecked(true);
        }
      }
    QString rpt_type;
    decodedtext.report(m_baseCall,
      Radio::base_callsign(m_hisCall), m_rptRcvd, rpt_type);
    }
  else if (firstcall == "DE" && t4.size () == 8 && t4.at (7) == "73") {
    if (base_call == qso_partner_base_call) {
      // 73 back to compound call holder
      m_ntx=5;
      m_QSOProgress = SIGNOFF;
      m_nlasttx=5;
      ui->txrb5->setChecked(true);
      if(txTabIndex()==1) {
        ui->genMsg->setText(ui->tx5->currentText());
        m_ntx=7;
        ui->rbGenMsg->setChecked(true);
      }
    }
    else {
      // treat like a CQ/QRZ
      if (m_skipTx1 && !m_houndMode) {
         if (0 == txTabIndex()) m_ntx = 2;
            m_QSOProgress = REPORT;
            m_nlasttx = 2;
            ui->txrb2->setChecked(true); }
      else {
        if(0 == txTabIndex()) m_ntx = 1;
        m_QSOProgress = REPLYING;
        m_nlasttx = 1;
        ui->txrb1->setChecked(true); }
      if(1 == txTabIndex()) {
        if (m_skipTx1 && !m_houndMode) { ui->genMsg->setText(ui->tx2->text()); }
        else { ui->genMsg->setText(ui->tx1->text()); }
        m_ntx=7;
        ui->rbGenMsg->setChecked(true);
      }
    }
  }
  else // myCall not in msg
    {
      if (m_skipTx1 && !m_houndMode) {
        if(0 == txTabIndex()) m_ntx=2;
        if (call_changed) m_qsoHistory.remove(hiscall); //prevent braking Auto Sequence by QSO history values 
        m_QSOProgress = REPORT;
        m_nlasttx=2;
        ui->txrb2->setChecked(true); }
      else {
        if(0 == txTabIndex()) m_ntx=1;
        m_QSOProgress = REPLYING;
        m_nlasttx=1;
        ui->txrb1->setChecked(true); }
      if(1 == txTabIndex()) {
        if (m_skipTx1 && !m_houndMode) { ui->genMsg->setText(ui->tx2->text()); }
        else { ui->genMsg->setText(ui->tx1->text()); }
        m_ntx=7;
        ui->rbGenMsg->setChecked(true);
      }
    }
  if(!alt && !ctrl)seedOrdinarySelection(decodedtext,call_changed);
  //RX3ASP request for manually toggling Enable Tx under AutoSeq while AutoTx is switched off
  if(m_autofilter && m_autoseq && !m_filter && (m_QSOProgress == REPORT || m_QSOProgress == REPLYING)) autoFilter (true);
  if(m_transmitting && !period_changed) m_restart=true;
//  if(m_autoTx && !alt) enableTx_mode(true);
// an attempt to provide stable UDP Reply operation:
  if(m_autoTx && !alt) { if(!ui->enableTxButton->isChecked()) ui->enableTxButton->click(); }
  if(alt && m_enableTx) haltTx("TX halted: ALT modifier is used at double click on message ");
  if(m_config.write_decoded_debug()) {
    QString EnTXstate = m_enableTx ? "On" : "OFF"; QString AuTXstate = m_autoTx ? "On" : "OFF";
    writeToALLTXT("double click on call processed: " + hiscall + "; EnableTx " + EnTXstate + ", AutoTx " + AuTXstate);
  }
  if(!ui->spotLineEdit->text().isEmpty() && ui->spotLineEdit->text().contains("#")) on_spotLineEdit_textChanged(ui->spotLineEdit->text());
  return true;
}

bool MainWindow::stdCall(QString const& w)
{
  static QRegularExpression standard_call_re {
    R"(
        ^\s*				# optional leading spaces
        ( [A-Z]{0,2} | [A-Z][0-9] | [0-9][A-Z] )  # part 1
        ( [0-9][A-Z]{0,3} )                       # part 2
        (/R | /P)?			# optional suffix
        \s*$				# optional trailing spaces
    )", QRegularExpression::CaseInsensitiveOption | QRegularExpression::ExtendedPatternSyntaxOption};
  return standard_call_re.match (w).hasMatch ();
}

void MainWindow::ScrollBarPosition(int n) { m_position=n; }

void MainWindow::on_S_meter_button_clicked(bool checked)
{
  ui->S_meter_button->setText(Radio::convert_Smeter(m_rigState.level(),checked));
}

void MainWindow::genCQMsg ()
{
  if(m_config.my_callsign().size () && m_config.my_grid().size ()) {
    QString grid{m_config.my_grid()};
    if(stdCall(m_config.my_callsign())) {
//      msgtype (QString {"%1 %2 %3"}.arg(m_CQtype).arg(m_config.my_callsign())
//               .arg(grid.left(4)),ui->tx6);
      msgtype (QString {"CQ %1 %2"}.arg(m_config.my_callsign())
               .arg(grid.left(4)),ui->tx6);
    } else {
//      msgtype (QString {"%1 %2"}.arg(m_CQtype).arg(m_config.my_callsign()),ui->tx6);
      msgtype (QString {"CQ %1"}.arg(m_config.my_callsign()),ui->tx6);
    }


/*    QString t=ui->tx6->text();
    if(m_mode=="FT8" and SpecOp::NONE != m_config.special_op_id() and
       t.split(" ").at(1)==m_config.my_callsign() and stdCall(m_config.my_callsign())) {
      if(SpecOp::NA_VHF == m_config.special_op_id())    t="CQ TEST" + t.mid(2,-1);
      if(SpecOp::EU_VHF == m_config.special_op_id())    t="CQ TEST" + t.mid(2,-1);
      if(SpecOp::FIELD_DAY == m_config.special_op_id()) t="CQ FD" + t.mid(2,-1);
      if(SpecOp::RTTY == m_config.special_op_id())      t="CQ RU" + t.mid(2,-1);
      ui->tx6->setText(t);
    } */
// code below shall be combined into m_CQtype or CQ CONTEST shall be combined into m_cqdir
    if(!m_cqdir.isEmpty () && stdCall(m_config.my_callsign())) { QString t="CQ " + m_cqdir + " " + m_config.my_callsign() + " " + m_config.my_grid().left(4);
      msgtype(t, ui->tx6);
    }  
// end of the code area to be reworked
  } else {
    ui->tx6->clear ();
  }
}

void MainWindow::genStdMsgs(QString rpt)                       //genStdMsgs()
{
  if(!m_autoseq && m_wasAutoSeq) { m_wasAutoSeq=false; on_AutoSeqButton_clicked(true); }
  QString myrpt,t;
  m_txGenerated = m_txFirst;

  genCQMsg ();

  QString hisCall=m_hisCall;
  ui->dxCallEntry->setStyleSheet(QString("color: %1; background: %2").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#ffffff",m_useDarkStyle)));

  if(hisCall.isEmpty ()) {
    ui->labAz->setText("");
    ui->labDist->setText("");
    ui->tx1->setText("");
    ui->tx2->setText("");
    ui->tx3->setText("");
    ui->tx4->setText("");
    ui->tx5->setCurrentText("");
    ui->genMsg->setText("");
    return;
  }

  auto const& my_callsign = m_config.my_callsign ();
//  auto is_compound = my_callsign != m_baseCall;
//  auto is_type_one = is_compound && shortList (my_callsign);
  auto const& my_grid = m_config.my_grid ().left (4);
  auto const& hisBase = Radio::base_callsign (hisCall);
  m_bMyCallStd=stdCall(my_callsign);
  m_bHisCallStd=stdCall(hisCall);
  bool bothCallsP=(my_callsign.endsWith("/P") and hisCall.endsWith("/P")); bool bothCallsR=(my_callsign.endsWith("/R") and hisCall.endsWith("/R"));
  bool mixedPR=((my_callsign.endsWith("/R") and hisCall.endsWith("/P")) or (my_callsign.endsWith("/P") and hisCall.endsWith("/R")));
  bool bothCallsCompound=(m_myCallCompound and m_hisCallCompound);
// longCompoundMix being not supported by message packing on the long callsign side
// an attempt to simplify such QSO from compound callsign side makes confusion with manipulation of the SkipTx1 option
//  bool longCompoundMix=((m_myCallCompound and !m_hisCallCompound and !m_bHisCallStd) or (m_hisCallCompound and !m_myCallCompound and !m_bMyCallStd));
  QString t0=hisBase + " " + m_baseCall + " ";

  if(m_mode.startsWith("FT") || m_mode=="Q65" || m_mode=="MSK144") {
    QString t0a,t0b;
    if(m_bHisCallStd and m_bMyCallStd) t0=hisCall + " " + my_callsign + " ";
    t0a="<"+hisCall + "> " + my_callsign + " ";
    t0b=hisCall + " <" + my_callsign + "> ";
    QString t {t0 + my_grid};
    if(!m_bMyCallStd) t=t0a;
    msgtype(t, ui->tx1);
    int n=rpt.toInt();
    myrpt = QString::asprintf("%+2.2d",n);
    QString t2,t3;
    QString sent=myrpt;
    QString rs,rst;
    int nn=(n+36)/6;
    if(nn<2) nn=2;
    if(nn>9) nn=9;
    rst = QString::asprintf("5%1d9 ",nn);
    rs=rst.mid(0,2);
    t=t0;
    if(!mixedPR) {
      if(!m_bMyCallStd) { t=t0b; msgtype(t0a, ui->tx1); }
      if(!m_bHisCallStd) { 
        t=t0a; 
        if(m_bMyCallStd && !my_callsign.endsWith("/P")) msgtype(t0a + my_grid, ui->tx1); 
        else msgtype(t0a, ui->tx1);
      }
    }
    else msgtype("<"+hisCall + "> " + my_callsign, ui->tx1);
//    if((bothCallsCompound and !bothCallsP and !bothCallsR) or longCompoundMix) t="<"+hisCall + "> " + Radio::base_callsign(my_callsign) + " ";
    if(bothCallsCompound and !bothCallsP and !bothCallsR) t="<"+hisCall + "> " + Radio::base_callsign(my_callsign) + " ";
    msgtype(t + sent, ui->tx2);
//    if((m_houndMode and !m_bHisCallStd and !m_bMyCallStd) or (!m_houndMode and ((bothCallsCompound and !bothCallsP and !bothCallsR) or longCompoundMix)))
    if((m_houndMode and !m_bHisCallStd and !m_bMyCallStd) or (!m_houndMode and bothCallsCompound and !bothCallsP and !bothCallsR))
      t="<"+hisCall + "> " + Radio::base_callsign(my_callsign) + " ";
    if(sent==myrpt) msgtype(t + "R" + sent, ui->tx3);
    if(sent!=myrpt) msgtype(t + "R " + sent, ui->tx3);

    t=t0 + (m_rrr ? "RRR" : "RR73");
//    if((!m_bHisCallStd and m_bMyCallStd) or (bothCallsCompound and !bothCallsP and !bothCallsR) or longCompoundMix)
    if((!m_bHisCallStd and m_bMyCallStd) or (bothCallsCompound and !bothCallsP and !bothCallsR))
      t=hisCall + " <" + my_callsign + "> " + (m_rrr ? "RRR" : "RR73");
    else if((m_bHisCallStd and !m_bMyCallStd)) t="<" + hisCall + "> " + my_callsign + " " + (m_rrr ? "RRR" : "RR73");
    msgtype(t, ui->tx4);

    t=t0 + "73";
//    if((!m_bHisCallStd and m_bMyCallStd) or (bothCallsCompound and !bothCallsP and !bothCallsR) or longCompoundMix)
    if((!m_bHisCallStd and m_bMyCallStd) or (bothCallsCompound and !bothCallsP and !bothCallsR))
      t=hisCall + " <" + my_callsign + "> 73";
    else if((m_bHisCallStd and !m_bMyCallStd)) t="<" + hisCall + "> " + my_callsign + " 73";

/*    if(unconditional || hisBase != m_lastCallsign || !m_lastCallsign.size ()) {
      // only update tx5 when (forced-unconditional or ) callsign changes */
      msgtype(t, ui->tx5->lineEdit());
/*      m_lastCallsign = hisBase;
    } */

    if (m_skipTx1 && !m_houndMode) {
      m_QSOProgress = REPORT;  
      m_nlasttx=2;
      ui->txrb2->setChecked(true);
      if (0 == txTabIndex()) { m_ntx=2; } else if (1 == txTabIndex()) { m_ntx=7; } }
    else {
      m_QSOProgress = REPLYING;
      m_nlasttx=1;
      ui->txrb1->setChecked(true);
      if (0 == txTabIndex()) { m_ntx=1; } else if (1 == txTabIndex()) { m_ntx=7; } }
      m_rpt=myrpt;

    return;
  }

  t0=hisBase + " " + m_baseCall + " ";
  t=t0 + m_config.my_grid ().left(4);
  msgtype(t, ui->tx1);
//this part is VHF one and being not supported, shall we delete it? 
  if(rpt.isEmpty ()) {
    t=t+" OOO";
    msgtype(t, ui->tx2);
    msgtype("RO", ui->tx3);
    if(!m_rrr) {
      msgtype("RR73", ui->tx4);
    } else {
      msgtype("RRR", ui->tx4);
    }
    msgtype("73", ui->tx5->lineEdit ());
//end of VHF functionality
  } else {
    int n=rpt.toInt();
    myrpt = QString::asprintf("%+2.2d",n);
    t=t0 + myrpt;
    msgtype(t, ui->tx2);
    t=t0 + "R" + myrpt;
    msgtype(t, ui->tx3);
    if(!m_rrr) {
      t=t0 + "RR73";
    } else {
      t=t0 + "RRR";
    }
    msgtype(t, ui->tx4);
    t=t0 + "73";
    msgtype(t, ui->tx5->lineEdit ());
  }

  if(m_config.my_callsign () != m_baseCall) {
    if(shortList(m_config.my_callsign ())) {
      t=hisBase + " " + m_config.my_callsign ();
      msgtype(t, ui->tx1);
      t="CQ " + m_config.my_callsign ();
      msgtype(t, ui->tx6);
    } else {
      switch (m_config.type_2_msg_gen ())
        {
        case Configuration::type_2_msg_1_full:
          t="DE " + m_config.my_callsign () + " " + m_config.my_grid ().left(4);
          msgtype(t, ui->tx1);
          t=t0 + "R" + myrpt;
          msgtype(t, ui->tx3);
          break;

        case Configuration::type_2_msg_3_full:
          t = t0 + m_config.my_grid ().left(4);
          msgtype(t, ui->tx1);
          t="DE " + m_config.my_callsign () + " R" + myrpt;
          msgtype(t, ui->tx3);
          break;

        case Configuration::type_2_msg_5_only:
          t = t0 + m_config.my_grid ().left(4);
          msgtype(t, ui->tx1);
          t=t0 + "R" + myrpt;
          msgtype(t, ui->tx3);
          break;
        }
      t="DE " + m_config.my_callsign () + " 73";
      msgtype(t, ui->tx5->lineEdit ());
    }
    if (hisCall != hisBase
        && m_config.type_2_msg_gen () != Configuration::type_2_msg_5_only) {
      // cfm we have his full call copied as we could not do this earlier
      t = hisCall + " 73";
      msgtype(t, ui->tx5->lineEdit ());
    }
  } else {
    if(hisCall!=hisBase) {
      if(shortList(hisCall)) {
        // cfm we know his full call with a type 1 tx1 message
        t=hisCall + " " + m_config.my_callsign ();
        msgtype(t, ui->tx1);
      }
      else {
        t=hisCall + " 73";
        msgtype(t, ui->tx5->lineEdit());
      }
    }
  }
  
  if (m_skipTx1 && !m_houndMode) {
	m_QSOProgress = REPORT;  
    m_nlasttx=2;
    ui->txrb2->setChecked(true);
    if (0 == txTabIndex()) { m_ntx=2; } else if (1 == txTabIndex()) { m_ntx=7; } }
  else {
	m_QSOProgress = REPLYING;
    m_nlasttx=1;
    ui->txrb1->setChecked(true);
    if (0 == txTabIndex()) { m_ntx=1; } else if (1 == txTabIndex()) { m_ntx=7; } }
  m_rpt=myrpt;
  if(!ui->spotLineEdit->text().isEmpty() && ui->spotLineEdit->text().contains("#")) on_spotLineEdit_textChanged(ui->spotLineEdit->text());
}

void MainWindow::TxAgain() { enableTx_mode(true); }

void MainWindow::clearDX (QString reason)
{
  QString dxcallclr=m_hisCall;
  clearDXfields("");
  genStdMsgs(QString {});
  if (1 == txTabIndex())
    {
      ui->genMsg->setText(ui->tx6->text());
      m_ntx=7;
      ui->rbGenMsg->setChecked(true);
    }
  else if (0 == txTabIndex())
    {
      m_ntx=6;
      ui->txrb6->setChecked(true);
    }
  m_QSOProgress = CALLING;
  m_nlasttx=6;
  if (m_spotDXsummit) {
     ui->pbSpotDXCall->setText(tr("DX Call"));
     m_spotDXsummit=false;
  }    
  if(m_config.spot_to_dxsummit()) { ui->pbSpotDXCall->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: outset;border-width: 1px;border-color: %3;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#c4c4ff",m_useDarkStyle),Radio::convert_dark("#808080",m_useDarkStyle))); }
  else { ui->pbSpotDXCall->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: outset;border-width: 1px;border-color: %3;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#aabec8",m_useDarkStyle),Radio::convert_dark("#808080",m_useDarkStyle))); }

  if(!reason.isEmpty() && m_config.write_decoded_debug()) writeToALLTXT("DX Call " + dxcallclr + reason);
}

void MainWindow::clearDXfields (QString reason)
{
  resetOrdinaryBusy();
  m_signoff.reset();
  flushSignoffLogRefresh();
  m_name = "";
  QString dxcallclr=m_hisCall;
  ui->dxCallEntry->clear();
  ui->dxGridEntry->clear();
  ui->dxCallEntry->setStyleSheet(QString("QLineEdit {color: %1; background: %2}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#ffffff",m_useDarkStyle)));
  if(!reason.isEmpty() && m_config.write_decoded_debug()) writeToALLTXT("DX Call " + dxcallclr + reason);
}

void MainWindow::logClearDX ()
{
  if(m_watchdogFinishing || m_signoff.active())return; // Wait for the final exchange.
  clearDX (" cleared by logClearDXTimer");
  m_QSOProgress = CALLING;  // guiUpdate() every second checking m_QSOProgress based on m_ntx value for tab1, in tab2 m_ntx=7
}

void MainWindow::countQSOs ()
{
  //printing QSO count
  char c_txt [64];
  if (m_mode == "FT8") { sprintf(c_txt,"FT8  %d",m_logBook.get_qso_count("FT8")); }
  else if (m_mode == "FT4" || m_mode=="FT2" || m_mode=="JTTY" || upstreamMode()) { snprintf(c_txt,sizeof(c_txt),"%s  %d",qPrintable(m_mode),m_logBook.get_qso_count(m_mode)); }
  else if (m_mode == "JT9+JT65") { sprintf(c_txt,"JT65/9 %d/%d",m_logBook.get_qso_count("JT65"),m_logBook.get_qso_count("JT9")); }
  else if (m_mode == "JT9") { sprintf(c_txt,"JT9  %d",m_logBook.get_qso_count("JT9")); }
  else if (m_mode == "JT65") { sprintf(c_txt,"JT65  %d",m_logBook.get_qso_count("JT65")); }
  else if (m_mode == "T10") { sprintf(c_txt,"T10  %d",m_logBook.get_qso_count("T10")); }
  else { sprintf(c_txt," "); }
  qso_count_label->setText(c_txt);
}

void MainWindow::autoFilter (bool action) { ui->filterButton->setChecked(action); on_filterButton_clicked (action); }

void MainWindow::enableTab1TXRB(bool state)
{
  ui->txrb1->setEnabled(state);
  ui->txrb2->setEnabled(state);
  ui->txrb3->setEnabled(state);
  ui->txrb4->setEnabled(state);
  ui->txrb5->setEnabled(state);
  ui->txrb6->setEnabled(state);
}

void MainWindow::dxbcallTxHaltedClear () { m_dxbcallTxHalted = ""; }

void MainWindow::lookup()                                       //lookup()
{
  QString hisCall=m_hisCall;
  if (hisCall.isEmpty () || hisCall.endsWith("/P") || hisCall.endsWith("/MM") || hisCall.endsWith("/A") || hisCall.endsWith("/M")) return;
  QFile f {m_dataDir.absoluteFilePath ("CALL3.TXT")};
  if (f.open (QIODevice::ReadOnly | QIODevice::Text))
    {
      char c[132];
      qint64 n=0;
      for(int i=0; i<999999; i++) {
        n=f.readLine(c,sizeof(c));
        if(n <= 0) {
          if (!m_hisGrid.isEmpty()) ui->dxGridEntry->clear();
          break;
        }
        QString t=QString(c);
        if(t.indexOf(hisCall)==0) {
          int i1=t.indexOf(",");
          QString hisgrid=t.mid(i1+1,6);
          i1=hisgrid.indexOf(",");
          if(i1>0) {
            hisgrid=hisgrid.left(4);
          } else {
            hisgrid=hisgrid.left(4) + hisgrid.mid(4,2).toLower();
          }
          ui->dxGridEntry->setText(hisgrid);
          break;
        }
      }
      f.close();
    }
}

void MainWindow::on_lookupButton_clicked() { lookup(); }

void MainWindow::on_addButton_clicked()
{
  if(m_hisGrid.isEmpty()) {
    JTDXMessageBox::warning_message (this, "", tr ("Add to CALL3.TXT")
                                 , tr ("Please enter a valid grid locator"));
    return;
  }
  m_call3Modified=false;
  QString hisCall=m_hisCall;
  QString hisgrid=m_hisGrid;
  QString newEntry=hisCall + "," + hisgrid;
  newEntry += ",,,";
  
  QFile f1 {m_dataDir.absoluteFilePath ("CALL3.TXT")};
  if(!f1.open(QIODevice::ReadWrite | QIODevice::Text)) {
    JTDXMessageBox::warning_message (this, "", tr ("Add to CALL3.TXT")
                                 , tr ("Cannot open \"%1\" for read/write: %2")
                                 .arg (f1.fileName ()).arg (f1.errorString ()));
    return;
  }
  if(f1.size()==0) {
    QTextStream out(&f1);
    out << "ZZZZZZ" <<
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                 endl;
#else
                 Qt::endl;
#endif

    f1.close();
    f1.open(QIODevice::ReadOnly | QIODevice::Text);
  }
  QFile f2 {m_dataDir.absoluteFilePath ("CALL3.TMP")};
  if(!f2.open(QIODevice::WriteOnly | QIODevice::Text)) {
    JTDXMessageBox::warning_message (this, "", tr ("Add to CALL3.TXT")
                                 , tr ("Cannot open \"%1\" for writing: %2")
                                 .arg (f2.fileName ()).arg (f2.errorString ()));
    return;
  }
  QTextStream in(&f1);          //Read from CALL3.TXT
  QTextStream out(&f2);         //Copy into CALL3.TMP
  QString hc=hisCall;
  QString hc1="";
  QString hc2="000000";
  QString s;
  do {
    s=in.readLine();
    hc1=hc2;
    if(s.left(2)=="//") {
      out << s + QChar::LineFeed;          //Copy all comment lines
    } else {
      int i1=s.indexOf(",");
      hc2=s.left(i1);
      if(hc>hc1 && hc<hc2) {
        out << newEntry + QChar::LineFeed;
        out << s + QChar::LineFeed;
        m_call3Modified=true;
      } else if(hc==hc2) {
        QString t {tr ("%1\nis already in CALL3.TXT"
                       ", do you wish to replace it?").arg (s)};
        int ret = JTDXMessageBox::query_message (this, "", tr ("Add to CALL3.TXT"), t);
        if(ret==JTDXMessageBox::Yes) {
          out << newEntry + QChar::LineFeed;
          m_call3Modified=true;
        }
      } else {
        if(!s.isEmpty ()) out << s + QChar::LineFeed;
      }
    }
  } while(!s.isNull());

  f1.close();
  if(hc>hc1 && !m_call3Modified) out << newEntry + QChar::LineFeed;
  if(m_call3Modified) {
    QFile f0 {m_dataDir.absoluteFilePath ("CALL3.OLD")};
    if(f0.exists()) f0.remove();
    QFile f1 {m_dataDir.absoluteFilePath ("CALL3.TXT")};
    f1.rename(m_dataDir.absoluteFilePath ("CALL3.OLD"));
    f2.rename(m_dataDir.absoluteFilePath ("CALL3.TXT"));
    f2.close();
  }
}

void MainWindow::msgtype(QString t, QLineEdit* tx)               //msgtype()
{
  char message[38];
  char msgsent[38];
  int len1=22;
  QByteArray s=t.toUpper().toLocal8Bit();
  ba2msg(s,message);
  int ichk=1,itype=0;
  gen9_(message,&ichk,msgsent,const_cast<int *>(itone),&itype,len1,len1);
  msgsent[22]=0;
  bool text=false;
  if(itype==6) text=true;
  QPalette p(tx->palette());
  if(text) {
    p.setColor(QPalette::Base,Radio::convert_dark("#ffccff",m_useDarkStyle));
  } else {
    p.setColor(QPalette::Base,Radio::convert_dark("#ffffff",m_useDarkStyle));
  }
  tx->setPalette(p);
  auto pos  = tx->cursorPosition ();
  tx->setText(t.toUpper());
  tx->setCursorPosition (pos);
}

void MainWindow::on_tx1_editingFinished() { QString t=ui->tx1->text(); msgtype(t, ui->tx1); }
void MainWindow::on_tx2_editingFinished() { QString t=ui->tx2->text(); msgtype(t, ui->tx2); }
void MainWindow::on_tx3_editingFinished() { QString t=ui->tx3->text(); msgtype(t, ui->tx3); }
void MainWindow::on_tx4_editingFinished() { QString t=ui->tx4->text(); msgtype(t, ui->tx4); }

void MainWindow::on_tx5_currentTextChanged (QString const& text) //tx5 edited
{
  bool isAllowedAuto73=isAutoSeq73(text);
  if(!m_Tx5setAutoSeqOff && !isAllowedAuto73) m_Tx5setAutoSeqOff=true;
  if(isAllowedAuto73) m_Tx5setAutoSeqOff=false;
  if(!text.contains(QRegularExpression {R"([@#&^])"}) && !text.isEmpty()) {
    QString t="161545  -4  0.1 1939 & " + text;
    DecodedText decodedtext {t,this,upstreamMode()};
//      DecodedText decodedtext {"161545  -4  0.1 1939 & CQ RT9K/4    "};
    bool stdfreemsg = decodedtext.isStandardMessage();
    if(stdfreemsg) {
      ui->tx5->setStyleSheet(QString("QComboBox {background: %1}").arg(Radio::convert_dark("#7bff7b",m_useDarkStyle)));
    } else {
      if(!text.isEmpty () && text.length() < 14) { ui->tx5->setStyleSheet(QString("QComboBox {background: %1}").arg(Radio::convert_dark("#7bff7b",m_useDarkStyle))); }
      else if(text.length() >= 14) { ui->tx5->setStyleSheet(QString("QComboBox {background: %1}").arg(Radio::convert_dark("#ffff00",m_useDarkStyle))); }
      msgtype(text, ui->tx5->lineEdit ());
    }
  } else ui->tx5->setStyleSheet(QString("QComboBox {background: %1}").arg(Radio::convert_dark("#ffffff",m_useDarkStyle)));
}

void MainWindow::on_tx5_currentIndexChanged(int index)
{
//it is dedicated to change free message during Tx if new message is selected from the list
  if(m_transmitting && ui->txrb5->isChecked() && index != m_oldTx5Index) m_restart=true;
  m_oldTx5Index=index;
}

void MainWindow::on_tx6_editingFinished() { QString t=ui->tx6->text(); msgtype(t, ui->tx6); }

void MainWindow::on_wantedCall_textChanged(const QString &wcall)
{
  m_wantedCall = wcall.trimmed().toUpper();
  if (ui->wantedCall->text() != m_wantedCall) {
    auto pos = ui->wantedCall->cursorPosition ();
    ui->wantedCall->setText(m_wantedCall);
    ui->wantedCall->setCursorPosition (pos);
  } else if (m_wantedCall.size() < 3){ 
    m_wantedCallList.clear();
  } else { 
    m_wantedCallList = m_wantedCall.split(',');
  }
}

void MainWindow::on_wantedCountry_textChanged(const QString &wcountry)
{
  m_wantedCountry = wcountry.trimmed().toUpper();
  if (ui->wantedCountry->text() != m_wantedCountry) {
    auto pos = ui->wantedCountry->cursorPosition ();
    ui->wantedCountry->setText(m_wantedCountry);
    ui->wantedCountry->setCursorPosition (pos);
  } else if (m_wantedCountry.size() < 1){ 
    m_wantedCountryList.clear();
  } else { 
    m_wantedCountryList = m_wantedCountry.split(',');
  }
}

void MainWindow::on_wantedPrefix_textChanged(const QString &wprefix)
{
  m_wantedPrefix = wprefix.trimmed().toUpper();
  if (ui->wantedPrefix->text() != m_wantedPrefix) {
    auto pos = ui->wantedPrefix->cursorPosition ();
    ui->wantedPrefix->setText(m_wantedPrefix);
    ui->wantedPrefix->setCursorPosition (pos);
  } else if (m_wantedPrefix.size() < 2) {
    m_wantedPrefixList.clear();
  } else {
    m_wantedPrefixList = m_wantedPrefix.split(',');
  }
}

void MainWindow::on_wantedGrid_textChanged(const QString &wgrid)
{
  m_wantedGrid = wgrid.trimmed().toUpper();
  if (ui->wantedGrid->text() != m_wantedGrid) {
    auto pos = ui->wantedGrid->cursorPosition ();
    ui->wantedGrid->setText(m_wantedGrid);
    ui->wantedGrid->setCursorPosition (pos);
  } else if (m_wantedGrid.size() < 4) {
    m_wantedGridList.clear();
  } else {
    m_wantedGridList = m_wantedGrid.split(',');
  }
}

void MainWindow::on_directionLineEdit_textChanged(const QString &dir) //CQ direction changed
{
  QString cqdirection = dir.toUpper();
  if(cqdirection.isEmpty ()) { ui->directionLineEdit->setStyleSheet(QString("QLineEdit {background: %1}").arg(Radio::convert_dark("#ffffff",m_useDarkStyle))); }
  else if(cqdirection.length() == 1 || cqdirection.length() > 2) { ui->directionLineEdit->setStyleSheet(QString("QLineEdit {background: %1}").arg(Radio::convert_dark("#fffa82",m_useDarkStyle))); }
  else if(cqdirection.length() == 2) { ui->directionLineEdit->setStyleSheet(QString("QLineEdit {background: %1}").arg(Radio::convert_dark("#82ff8c",m_useDarkStyle))); }
  m_cqdir = cqdirection;
  if (cqdirection.isEmpty ()) { ui->pbCallCQ->setText("CQ"); }
  else if (cqdirection.length() == 2) { ui->pbCallCQ->setText("CQ " + m_cqdir); }
  auto curpos = ui->directionLineEdit->cursorPosition(); ui->directionLineEdit->setText(m_cqdir);
  ui->direction1LineEdit->setText(m_cqdir); ui->directionLineEdit->setCursorPosition(curpos);
  if (cqdirection.isEmpty () || cqdirection.length() == 2) {
      if(ui->genMsg->text().startsWith ("CQ ") && ui->txrb6->isChecked()) {
         ui->pbCallCQ->click ();
      }
  }
}

void MainWindow::on_direction1LineEdit_textChanged(const QString &dir) //CQ direction changed
{
  QString cqdirection = dir.toUpper();
  if(cqdirection.isEmpty ()) { ui->direction1LineEdit->setStyleSheet(QString("QLineEdit {background: %1}").arg(Radio::convert_dark("#ffffff",m_useDarkStyle))); }
  else if(cqdirection.length() == 1 || cqdirection.length() > 2) { ui->direction1LineEdit->setStyleSheet(QString("QLineEdit {background: %1}").arg(Radio::convert_dark("#fffa82",m_useDarkStyle))); }
  else if(cqdirection.length() == 2) { ui->direction1LineEdit->setStyleSheet(QString("QLineEdit {background: %1}").arg(Radio::convert_dark("#82ff8c",m_useDarkStyle))); }
  m_cqdir = cqdirection;
  if (cqdirection.isEmpty ()) { ui->pbCallCQ->setText("CQ"); }
  else if (cqdirection.length() == 2) { ui->pbCallCQ->setText("CQ " + m_cqdir); }
  auto curpos = ui->direction1LineEdit->cursorPosition(); ui->direction1LineEdit->setText(m_cqdir);
  ui->directionLineEdit->setText(m_cqdir); ui->direction1LineEdit->setCursorPosition(curpos);
  if (cqdirection.isEmpty () || cqdirection.length() == 2) {
      if(ui->tx1->text().isEmpty()) { if(ui->txrb6->isChecked()) ui->txb6->click (); }
      else { 
        if(cqdirection.length() == 2) ui->tx6->setText("CQ " + m_cqdir + " " + m_config.my_callsign() + " " + m_config.my_grid().left(4));
        else ui->tx6->setText("CQ " + m_config.my_callsign() + " " + m_config.my_grid().left(4));
      }
  }
}

void MainWindow::on_spotLineEdit_textChanged(const QString &text)
{
  m_spotText=text;
  QString spotTextTmp=text;
  spotTextTmp.replace("#G",m_config.my_grid() + "<" + ui->propLineEdit->text() + ">" + ui->dxGridEntry->text(), Qt::CaseInsensitive);
  spotTextTmp.replace("#D", ui->labDist->text().replace(" ",""), Qt::CaseInsensitive);
  spotTextTmp.replace("#R", m_rpt+"dB", Qt::CaseInsensitive);
  spotTextTmp.replace("#H", m_houndMode ? "F/H" : "");
  QString spotText;
  if(ui->spotLineEdit->text().isEmpty() && ui->propLineEdit->text().isEmpty()) { spotText="info: " + m_modeTx; }
  else if(ui->spotLineEdit->text().isEmpty() && !ui->propLineEdit->text().isEmpty()) { spotText="info: " + m_modeTx + " " + ui->propLineEdit->text(); }
  else if(!m_spotText.contains("#G", Qt::CaseInsensitive)) { spotText="info: " + m_modeTx + " " + ui->propLineEdit->text() + " " + spotTextTmp; }
  else { spotText="info: " + m_modeTx + " " + spotTextTmp; }
  ui->spotMsgLabel->setText(spotText);
}

void MainWindow::on_propLineEdit_textChanged(const QString &text) {
  if(text != text.toUpper()) ui->propLineEdit->setText(text.toUpper());
  on_spotLineEdit_textChanged(ui->spotLineEdit->text());
}

void MainWindow::on_dxCallEntry_textChanged(const QString &t) //dxCall changed
{
  if(t.trimmed().toUpper()!=m_hisCall) {++m_generalSelectionRevision;resetOrdinaryBusy();}
  int n=t.length();
  if (n < 3 ) {
      if (t != t.toUpper().trimmed()) ui->dxCallEntry->setText(t.toUpper().trimmed());
      if (m_hisCall.isEmpty()) { m_hisCallCompound=false; return; }
      else m_hisCall.clear();
  } else m_hisCall=t.toUpper().trimmed();
  m_hisCallCompound=(!m_hisCall.isEmpty() && m_hisCall.contains("/")); // && !m_hisCall.endsWith("/P") && !m_hisCall.endsWith("/R"));
  if(m_myCallCompound && m_hisCallCompound) {
    if(m_skipTx1) { m_skipTx1=false; ui->skipTx1->setChecked(false); ui->skipGrid->setChecked(false); on_txb1_clicked(); }
    ui->skipTx1->setEnabled(false); ui->skipGrid->setEnabled(false);
  }
  else { if(!m_houndMode) {ui->skipTx1->setEnabled(true); ui->skipGrid->setEnabled(true); }}
  auto pos = ui->dxCallEntry->cursorPosition (); 
  if (t != m_hisCall && !m_hisCall.isEmpty()) { ui->dxCallEntry->setText(m_hisCall); ui->dxCallEntry->setCursorPosition (pos); }
  else {
      QString grid;
      if (m_logBook.getData(m_hisCall,grid,m_name)) {
//            printf("DXCall log_data %s %s %s\n",m_hisCall.toStdString().c_str(),grid.toStdString().c_str(),m_name.toStdString().c_str());
            if (!grid.isEmpty()) {
              ui->dxGridEntry->setText(grid);
            }
          } else {
             m_name = "";
          }
      ui->dxCallEntry->setStyleSheet(QString("QLineEdit {color: %1; background: %2}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#ffffff",m_useDarkStyle)));
      if (logClearDXTimer.isActive()) logClearDXTimer.stop();
      // Refresh Tx macros
      QStringListModel* model1 = m_config.macros();
      QStringList list1 = model1->stringList();
      QStringList list2 = list1.replaceInStrings("@", m_hisCall);
      list2 = list2.replaceInStrings("&", m_baseCall);
      list2 = list2.replaceInStrings("#", m_rpt);
      QString name=m_name.trimmed().split(" ").at(0);
      if(!name.isEmpty() && name.length()<=7) { 
        name = name.toUpper();
        QRegularExpression name_re("^[A-Z]{2,7}$"); QRegularExpressionMatch match = name_re.match(name);
        bool hasMatch = match.hasMatch();
        if(hasMatch) list2 = list2.replaceInStrings("^", name); 
      }
      if (!m_generatedMacros) m_generatedMacros = new QStringListModel(this);
      m_generatedMacros->setStringList(list2);
      ui->tx5->setModel(m_generatedMacros);
      ui->freeTextMsg->setModel(m_generatedMacros);
      if (m_spotDXsummit){
         ui->pbSpotDXCall->setText(tr("DX Call"));
         m_spotDXsummit=false;
      }
      if(m_config.spot_to_dxsummit()) { ui->pbSpotDXCall->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: outset;border-width: 1px;border-color: %3;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#c4c4ff",m_useDarkStyle),Radio::convert_dark("#808080",m_useDarkStyle))); }
      else { ui->pbSpotDXCall->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: outset;border-width: 1px;border-color: %3;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#aabec8",m_useDarkStyle),Radio::convert_dark("#808080",m_useDarkStyle))); }
      m_bHisCallStd=stdCall(m_hisCall);
      statusChanged();
  }
}

void MainWindow::on_dxGridEntry_textChanged(const QString &t) //dxGrid changed
{
  int n=t.length();
  if(n!=4 and n!=6 and n!=8 and n!=10) {
    if (n < 4 || n==5) {
        if (t != t.left(2).toUpper() + t.mid(2,2) + t.mid(4,1).toLower()) ui->dxGridEntry->setText(t.left(2).toUpper() + t.mid(2,2) + t.mid(4,1).toLower());
        if (n < 4 && !m_hisGrid.isEmpty()) { ui->labAz->clear(); ui->labDist->clear(); m_hisGrid.clear(); statusUpdate (); }
    } else if (n==7){
        if (t != t.left(2).toUpper() + t.mid(2,2) + t.mid(4,2).toLower() + t.mid(6,1)) ui->dxGridEntry->setText(t.left(2).toUpper() + t.mid(2,2) + t.mid(4,2).toLower() + t.mid(6,1));
    } else if (n==9){
        if (t != t.left(2).toUpper() + t.mid(2,2) + t.mid(4,2).toLower() + t.mid(6,2) + t.mid(8,1).toLower()) ui->dxGridEntry->setText(t.left(2).toUpper() + t.mid(2,2) + t.mid(4,2).toLower() + t.mid(6,2) + t.mid(8,1).toLower());
    }  
 
    return;
  }
  m_hisGrid=t.left(2).toUpper() + t.mid(2,2) + t.mid(4,2).toLower() + t.mid(6,2) + t.mid(8,2).toLower();
  auto pos = ui->dxGridEntry->cursorPosition ();
  if (t != m_hisGrid) { ui->dxGridEntry->setText(m_hisGrid); ui->dxGridEntry->setCursorPosition (pos); }
  else {
        statusUpdate ();
        qint64 nsec = (m_jtdxtime->currentMSecsSinceEpoch2() / 1000) % 86400;
        double utch=nsec/3600.0;
        int nAz=0,nEl=0,nDmiles=0,nDkm=0,nHotAz,nHotABetter;
        if (!m_hisGrid.isEmpty() && !m_config.my_grid ().isEmpty())
          azdist_(const_cast <char *> ((m_config.my_grid () + "        ").left (8).toLatin1().constData()),
                const_cast <char *> ((m_hisGrid + "        ").left (8).toLatin1().constData()),&utch,
                &nAz,&nEl,&nDmiles,&nDkm,&nHotAz,&nHotABetter,8,8);
        QString t;
        t = QString::asprintf("Az: %d",nAz);
        ui->labAz->setText(t);
        if (m_config.miles ()) { t = QString::asprintf ("%d mi", nDmiles); }
        else { t = QString::asprintf ("%d km", nDkm); }
        ui->labDist->setText(t);
  }
  if(!ui->spotLineEdit->text().isEmpty() && ui->spotLineEdit->text().contains("#")) on_spotLineEdit_textChanged(ui->spotLineEdit->text());
}

void MainWindow::on_genStdMsgsPushButton_clicked() { genStdMsgs(m_rpt); }

void MainWindow::on_logQSOButton_clicked()
{
  if (m_hisCall.isEmpty()) return;
  auto currenttime = m_jtdxtime->currentDateTimeUtc2();
  auto dateTimeQSOOff = currenttime;
  QString rrep,srep,distance;
  unsigned time = 0;

  if (!m_loggingCompletedSignoff && !m_houndMode && (m_config.prompt_to_log() || m_config.autolog())) {
    if(m_mode == "FT8") dateTimeQSOOff = currenttime.addSecs (14);
    else if(m_mode == "FT4") dateTimeQSOOff = currenttime.addSecs (7);
    else if(m_mode == "FT2") dateTimeQSOOff = currenttime.addSecs (3);
    else if(m_mode!="JTTY" && !upstreamMode()) dateTimeQSOOff = currenttime.addSecs (50);
  }
  if (dateTimeQSOOff < m_dateTimeQSOOn) m_dateTimeQSOOn = dateTimeQSOOff;
  // 75 for FT4, 150 seconds delta for FT8, 600 seconds delta for other modes
  if (qAbs(dateTimeQSOOff.toMSecsSinceEpoch() - m_dateTimeQSOOn.toMSecsSinceEpoch()) > int(m_TRperiod) * 10000) m_dateTimeQSOOn = dateTimeQSOOff;
  auto txHz=correctedFrequency(m_config.split_mode()?m_freqTxNominal:m_freqNominal,m_astroCorrection.tx)+ui->TxFreqSpinBox->value()-(m_config.split_mode()?m_XIT:0);
  auto rxHz=correctedFrequency(m_freqNominal,m_astroCorrection.rx)+ui->RxFreqSpinBox->value();
  bool autolog = false;
  if(m_logqso73) autolog = m_config.autolog();
  distance=ui->labDist->text();
  // A later confirmation belongs to this selection, even if CAT or AutoSeq
  // changes the band, mode or partner while the log dialog is open.
  m_generalLogContext=generalContext();
  m_generalLogSelectionRevision=m_generalSelectionRevision;
  if (m_qsoHistory.log_data(m_hisCall,time,rrep,srep) > QsoHistory::SREPORT) {
      if (time < 86400) {
          currenttime.setTime(QTime::fromMSecsSinceStartOfDay(time*1000));
          if (dateTimeQSOOff < currenttime) currenttime = currenttime.addSecs(-86400);
      }
      if (rrep.isEmpty()) rrep=m_rptRcvd;
      if (srep.isEmpty()) srep=m_rptSent;
      if(m_loggingCompletedSignoff) {
        if(!m_signoffLogOn.isValid())m_signoffLogOn=currenttime;
        currenttime=m_signoffLogOn;dateTimeQSOOff=m_signoffLogOff;
      }
      m_logDlg->initLogQSO (m_hisCall, m_hisGrid, m_modeTx, srep, rrep, distance, m_name,
                        currenttime, dateTimeQSOOff, txHz,autolog,rxHz);
  } else { 
      auto on=m_dateTimeQSOOn;
      if(m_loggingCompletedSignoff) {
        if(!m_signoffLogOn.isValid())m_signoffLogOn=on;
        on=m_signoffLogOn;dateTimeQSOOff=m_signoffLogOff;
      }
      m_logDlg->initLogQSO (m_hisCall, m_hisGrid, m_modeTx, m_rptSent, m_rptRcvd, distance, m_name,
                        on, dateTimeQSOOff, txHz,autolog,rxHz);
  }
  m_logqso73=false;
  m_config.set_station_locked(m_enableTx || m_tune || m_transmitting || m_logDlg->isVisible());
  updateStationSelectorEnabled();
}

void MainWindow::acceptQSO2(QDateTime const& QSO_date_off, QString const& call, QString const& grid
                            , Frequency dial_freq, QString const& mode
                            , QString const& rpt_sent, QString const& rpt_received
                            , QString const& tx_power, QString const& comments
                            , QString const& name, QDateTime const& QSO_date_on
                            , QString const& eqslcomments, QByteArray const& myadif2)
{
  QString date = QSO_date_on.toString("yyyyMMdd");
  m_qsoLogged=true;
  if(m_signoff.matches(call,generalContext()) && m_generalLogContext==generalContext()
     && m_signoffSelectionRevision==m_generalLogSelectionRevision) {
    auto records=OperatorLog::parse(QString::fromUtf8(myadif2));
    if(records.size()==1) {m_signoffLogOriginal=records.first();m_signoffLogOwner=m_config.my_callsign();}
  }
  mcpEvent("qso_logged", {{"call", call}, {"grid", grid}, {"mode", mode}, {"dial_hz", static_cast<double>(dial_freq)}, {"report_sent", rpt_sent}, {"report_received", rpt_received}});
  m_logBook.addAsWorked (call, m_config.bands ()->find (dial_freq), mode, date, grid, name);
  QString operator_call = m_config.my_callsign(); QString my_call = m_config.my_callsign(); QString my_grid = m_config.my_grid();
  m_messageClient->qso_logged (QSO_date_off, call, grid, dial_freq, mode, rpt_sent, rpt_received, tx_power, comments, name, QSO_date_on, operator_call, my_call, my_grid);
  if(m_config.enable_udp1_adif_sending()) m_messageClient->logged_ADIF(myadif2);
  if(m_config.enable_udp2_broadcast() && m_config.valid_udp2()) {
    QUdpSocket sock;
    if(-1 == sock.writeDatagram (myadif2, QHostAddress {m_config.udp2_server_name()}, m_config.udp2_server_port()))
      { JTDXMessageBox::warning_message (this, "", tr ("Error sending QSO ADIF data to secondary UDP server"), tr ("Write returned \"%1\"").arg (sock.errorString ())); }
  }
  if (m_config.send_to_eqsl())
      Eqsl->upload(m_config.eqsl_username(),m_config.eqsl_passwd(),m_config.eqsl_nickname(),call,mode,QSO_date_on,rpt_sent,m_config.bands ()->find (dial_freq),eqslcomments);
  ui->dxCallEntry->setStyleSheet(QString("QLineEdit {color: %1; background: %2}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#7fff7f",m_useDarkStyle)));
  m_lastloggedcall=call;
  m_lastloggedtime=m_jtdxtime->currentDateTimeUtc2();
  bool currentQso=m_generalLogContext==generalContext()
    && m_generalLogSelectionRevision==m_generalSelectionRevision;
  if(currentQso)applyGeneralPostQso(call);
  countQSOs ();
  writeToALLTXT("QSO logged: " + m_lastloggedcall);
  setLastLogdLabel();
//clean up wanted call from window/list if QSO with this call is logged in
  if(ui->cbClearCallsign->isChecked ()) {
    int wcallidx=-1;
    if(m_mode.startsWith("FT")) wcallidx=m_wantedCallList.indexOf(call);
    else wcallidx=m_wantedCallList.indexOf(Radio::base_callsign (call));
    if(wcallidx >= 0) {
      m_wantedCallList.removeAt(wcallidx); ui->wantedCall->setText(m_wantedCallList.join(","));
    }
  }
//clean up wanted grid from window/list if QSO with this grid is logged in
  if(ui->cbClearGrid->isChecked () && grid.trimmed().length()==4) {
    int wgrididx=-1;
    wgrididx=m_wantedGridList.indexOf(grid);
    if(wgrididx >= 0) {
      m_wantedGridList.removeAt(wgrididx); ui->wantedGrid->setText(m_wantedGridList.join(","));
    }
  }
  if (currentQso && m_houndMode && call==m_hisCall) { clearDX (" cleared: QSO logged in DXpedition mode"); ui->dxCallEntry->setStyleSheet(QString("QLineEdit {color: %1; background: %2}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#ffffff",m_useDarkStyle))); }
}

void MainWindow::on_actionJT9_triggered()
{
  if (m_mode=="WSPR-2") killFile();
  m_mode="JT9";
  WSPR_config(false);
  switch_mode (Modes::JT9);
  if(m_modeTx!="JT9") on_pbTxMode_clicked();
  m_hsymStop=173; if(m_config.decode_at_52s()) m_hsymStop=179;
  mode_label->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#ff99cc",m_useDarkStyle)));
  ui->actionJT9->setChecked(true);
  ui->pbTxMode->setText("Tx JT9  @");
  ui->pbTxMode->setEnabled(false);
  m_TRperiod=60.0;
  commonActions();
  enableHoundAccess(false);
}

void MainWindow::on_actionT10_triggered()
{
  if (m_mode=="WSPR-2") killFile();
  m_mode="T10";
  WSPR_config(false);
  switch_mode (Modes::T10);
  m_modeTx="T10";
  m_hsymStop=173; if(m_config.decode_at_52s()) m_hsymStop=179;
  mode_label->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#aaffff",m_useDarkStyle)));
  ui->actionT10->setChecked(true);
  ui->pbTxMode->setText("Tx T10  +");
  ui->pbTxMode->setEnabled(false);
  m_TRperiod=60.0;
  commonActions();
  enableHoundAccess(false);
}

void MainWindow::on_actionFT2_triggered()
{
  if(m_mode=="WSPR-2")killFile();
  m_mode=m_modeTx="FT2";WSPR_config(false);switch_mode(Modes::FT2);
  m_hsymStop=11;m_TRperiod=3.75;
  ui->actionFT2->setChecked(true);ui->pbTxMode->setText("Tx FT2 +");ui->pbTxMode->setEnabled(false);
  on_AutoSeqButton_clicked(true);commonActions();enableHoundAccess(false);
}

void MainWindow::on_actionFT4_triggered()
{
  if (m_mode=="WSPR-2") killFile();
  m_mode="FT4";
  WSPR_config(false);
  switch_mode (Modes::FT4);
  m_modeTx="FT4";
  m_hsymStop=21;
  mode_label->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#a99ee2",m_useDarkStyle))); //to be changed
  ui->actionFT4->setChecked(true);
  ui->pbTxMode->setText("Tx FT4 :");
  ui->pbTxMode->setEnabled(false);
  on_AutoSeqButton_clicked(true);
  m_TRperiod=7.5;
  if(!m_hint) ui->hintButton->click();
  commonActions();
  enableHoundAccess(false);
}

void MainWindow::on_actionFT8_triggered()
{
  if (m_mode=="WSPR-2") killFile();
  m_mode="FT8";
  WSPR_config(false);
  switch_mode (Modes::FT8);
  m_modeTx="FT8";
  m_hsymStop=50;
  mode_label->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#6699ff",m_useDarkStyle)));
  ui->actionFT8->setChecked(true);
  ui->pbTxMode->setText("Tx FT8 ~");
  ui->pbTxMode->setEnabled(false);
  on_AutoSeqButton_clicked(true);
  m_TRperiod=15.0;
  commonActions();
  if(!m_hint) ui->hintButton->click();
  ui->hintButton->setEnabled(false); ui->hintButton->setVisible(false);
  ui->candListSpinBox->setEnabled(true); ui->DTCenterSpinBox->setEnabled(true);
  ui->DTCenterSpinBox->setVisible(true); ui->pbTxMode->setVisible(false);
  enableHoundAccess(true);
}

void MainWindow::on_actionJT65_triggered()
{
  if (m_mode=="WSPR-2") killFile();
  if(m_mode.startsWith("FT") or m_mode=="T10" or m_mode.left(4)=="WSPR") {
// If coming from FT,T10 or WSPR mode, pretend temporarily that we're coming
// from JT9 and click the pbTxMode button
    m_modeTx="JT9";
    on_pbTxMode_clicked();
  }
  m_mode="JT65";
  WSPR_config(false);
  switch_mode (Modes::JT65);
  if(m_modeTx!="JT65") on_pbTxMode_clicked();
  m_TRperiod=60.0;
  m_hsymStop=173; if(m_config.decode_at_52s()) m_hsymStop=179;
  mode_label->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#66ff66",m_useDarkStyle)));
  ui->actionJT65->setChecked(true);
  ui->pbTxMode->setText("Tx JT65  #");
  ui->pbTxMode->setEnabled(false);
  commonActions();
  enableHoundAccess(false);
}

void MainWindow::on_actionJT9_JT65_triggered()
{
  if (m_mode=="WSPR-2") killFile();
  m_mode="JT9+JT65";
  WSPR_config(false);
  switch_mode (Modes::JT65);
  ui->pbTxMode->setText("Tx JT65  #"); ui->pbTxMode->setEnabled(true);
  m_modeTx="JT65";
  m_TRperiod=60.0;
  m_hsymStop=173; if(m_config.decode_at_52s()) m_hsymStop=179;
  mode_label->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#ffff66",m_useDarkStyle)));
  ui->actionJT9_JT65->setChecked(true);
  commonActions();
  enableHoundAccess(false);
}

void MainWindow::on_actionWSPR_2_triggered()
{
  m_mode="WSPR-2";
  WSPR_config(true);
  switch_mode (Modes::WSPR);
  m_modeTx="WSPR-2";                                    //### not needed ?? ###
  m_TRperiod=120.0;
  if (m_tci) Q_EMIT m_config.transceiver_period(m_TRperiod); // TODO - not thread safe
  else Q_EMIT audioPeriod(m_TRperiod);
  m_nsps=6912;                   //For symspec only
  m_FFTSize = m_nsps / 2;
  if (m_tci) Q_EMIT m_config.transceiver_blocksize (m_FFTSize);
  else Q_EMIT FFTSize (m_FFTSize);
  m_hsymStop=396;
  m_toneSpacing=12000.0/8192.0;
  mode_label->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#ff66ff",m_useDarkStyle)));
  mode_label->setText(m_mode);
  ui->actionWSPR_2->setChecked(true);
  m_wideGraph->setPeriod(m_TRperiod,m_nsps);
  m_wideGraph->setMode(m_mode);
  m_wideGraph->setModeTx(m_modeTx);
  ui->TxFreqSpinBox->setValue(ui->WSPRfreqSpinBox->value());
  ui->pbTxMode->setText(tr("Tx WSPR"));
  ui->pbTxMode->setEnabled(false);
  setMinButton();
  ui->TxMinuteButton->setEnabled(false); ui->candListSpinBox->setEnabled(false);
  ui->DTCenterSpinBox->setEnabled(false); ui->DTCenterSpinBox->setVisible(false);
  setClockStyle(true);
  progressBar->setMaximum(int(m_TRperiod));
  progressBar->setFormat("%v/%m");
  statusChanged();
  on_spotLineEdit_textChanged(ui->spotLineEdit->text());
}

void MainWindow::switch_mode (Mode mode)
{
  if(!m_wsprDecodeArgs.isEmpty() && !m_mode.startsWith("WSPR")){m_wsprDecodeArgs.clear();decodeBusy(false);}
  if(m_lastMode!=m_mode && (m_enableTx || m_transmitting || g_iptt || m_tune))on_stopTxButton_clicked();
  if(m_jtty && m_mode!="JTTY") {
    m_jtty->setActive(false);Q_EMIT continuousAudio(upstreamMode());
    ui->tuneButton->setEnabled(true);m_managedMenu->setEnabled(true);ui->menuAutoSeq->setEnabled(true);
    ui->actionOpen->setEnabled(!m_decoderBusy);ui->actionOpen_next_in_directory->setEnabled(!m_decoderBusy);ui->actionDecode_remaining_files_in_directory->setEnabled(!m_decoderBusy);
  }
// m_lastMode value is deliberately not assigned in constructor to let qsohistory init at SW startup 
  if(m_upstream && !upstreamMode()){m_upstream->stop();m_periodAudio.reset();m_upstreamTxTimeout.stop();m_emeTools->setVisible(m_config.enable_VHF_features());}
  if(m_lastMode!=m_mode) {
     if (m_lastMode == "FT4") Q_EMIT m_config.transceiver_ft4_mode (false);
     else if (m_mode == "FT4") Q_EMIT m_config.transceiver_ft4_mode (true);
     m_qsoHistory.init(); 
     m_lastMode=m_mode; 
     if(m_config.write_decoded_debug()) writeToALLTXT("QSO history initialized by switch_mode");
  } 
  m_okToPost = false;
  m_config.frequencies ()->filter (m_config.region (),mode);
  auto const& row = m_config.frequencies ()->best_working_frequency (m_freqNominal);
  if (row >= 0) {
    ui->bandComboBox->setCurrentIndex (row);
    on_bandComboBox_activated (row);
  }
  if (!m_mode.startsWith ("WSPR")) {
	countQSOs ();
	if (m_config.prompt_to_log ()) { qso_count_label->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#99ff99",m_useDarkStyle))); }
	else if (m_config.autolog ()) { qso_count_label->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#9999ff",m_useDarkStyle))); }
	else { qso_count_label->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#ffffff",m_useDarkStyle))); }
  }
}

void MainWindow::commonActions ()
{
//  m_modulator->setPeriod(m_TRperiod); // TODO - not thread safe
//  m_detector->setPeriod(m_TRperiod);   // TODO - not thread safe
  m_nsps=6912;                   //For symspec only
  m_FFTSize = m_nsps / 2;
  if (m_tci) Q_EMIT m_config.transceiver_blocksize(m_FFTSize);
  else Q_EMIT FFTSize (m_FFTSize);
  m_toneSpacing=0.0;
  m_wideGraph->setMode(m_mode);
  m_wideGraph->setModeTx(m_modeTx);
  m_wideGraph->show();
  mode_label->setText(m_mode);
  QString t;
  if (m_mode.startsWith("FT")) t = "UTC     dB   DT "+tr("Freq   Message");
  else t = "UTC   dB   DT "+tr("Freq   Message");
  ui->decodedTextLabel->setText(t);
  ui->label_6->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#fdedc5",m_useDarkStyle)));
  ui->label_6->setText(tr("Band Activity"));
  ui->decodedTextLabel2->setText(t);
  m_wideGraph->setPeriod(m_TRperiod,m_nsps);
  if (m_tci) Q_EMIT m_config.transceiver_period(m_TRperiod); // TODO - not thread safe
  else Q_EMIT audioPeriod(m_TRperiod);
  ui->label_6->setText(tr("Band Activity"));
  ui->label_7->setText(tr("Rx Frequency"));
  ui->TxMinuteButton->setEnabled(true);
  setMinButton();
  setClockStyle(true);
  progressBar->setMaximum(int(m_TRperiod));
  progressBar->setFormat("%v/"+QString::number(m_TRperiod));
  statusChanged();
  on_spotLineEdit_textChanged(ui->spotLineEdit->text());
  if(m_mode=="FT4" || m_mode=="FT2") {
    if(m_rrr) { m_savedRRR=m_rrr; ui->rrrCheckBox->click(); }
    ui->rrrCheckBox->setEnabled(false); ui->rrr1CheckBox->setEnabled(false);
    if(!m_hint) ui->hintButton->click();
    ui->hintButton->setEnabled(false);
  }
  else {
    ui->rrrCheckBox->setEnabled(true); ui->rrr1CheckBox->setEnabled(true); if(m_savedRRR) ui->rrrCheckBox->click();
    if(m_mode!="FT8") ui->hintButton->setEnabled(true);
  }
  if(m_mode!="FT8") {
    ui->candListSpinBox->setEnabled(false); ui->DTCenterSpinBox->setEnabled(false);
    ui->DTCenterSpinBox->setVisible(false); ui->pbTxMode->setVisible(true);
    ui->hintButton->setVisible(true); ui->syncButton->setVisible(false);
  } else {
    ui->syncButton->setVisible(true);
  }
  updateGeneralWindows();
  if(m_upstreamPeriod && !upstreamMode()) {
    m_upstreamPeriod->setEnabled(false);m_upstreamSubmode->setEnabled(false);
    m_upstreamTolerance->setEnabled(false);m_upstreamAverage->setEnabled(false);
  }
  m_modeChanged=true;
}

void MainWindow::WSPR_config(bool b)
{
  ui->decodedTextBrowser2->setVisible(!b);
  ui->decodedTextLabel2->setVisible(!b);
  ui->controls_stack_widget->setCurrentIndex (b ? 1 : 0);
  ui->QSO_controls_widget->setVisible (!b);
  ui->DX_controls_widget->setEnabled (!b);
  ui->WSPR_controls_widget->setVisible (b);
  ui->label_6->setVisible(!b and ui->cbMenus->isChecked()); ui->label_7->setVisible(!b); ui->HoundButton->setEnabled(!b);
  ui->logQSOButton->setEnabled(!b); ui->DecodeButton->setEnabled(!b); ui->filterButton->setEnabled(!b);
  ui->AGCcButton->setEnabled(!b); ui->swlButton->setEnabled(!b); ui->ClearDxButton->setEnabled(!b);
  ui->hintButton->setEnabled(!b); ui->AutoTxButton->setEnabled(!b); ui->AutoSeqButton->setEnabled(!b);
  ui->bypassButton->setEnabled(!b); ui->singleQSOButton->setEnabled(!b); ui->AnsB4Button->setEnabled(!b);
  ui->syncButton->setEnabled(!b); ui->syncButton->setVisible(!b);
  if(b) {
    ui->decodedTextLabel->setText("UTC    dB   DT "+tr("    Freq     Drift  Call          Grid    dBm   Dist"));
    ui->label_6->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#fdedc5",m_useDarkStyle)));
    ui->label_6->setText(tr("Band Activity"));
    if (m_config.is_transceiver_online ()) {
      Q_EMIT m_config.transceiver_tx_frequency (0); // turn off split
    }
    m_bSimplex = true;
  } else {
    QString t;
    if (m_mode.startsWith("FT")) t = "UTC     dB   DT "+tr("Freq   Message");
    else t = "UTC   dB   DT "+tr("Freq   Message");
    ui->decodedTextLabel->setText(t);
    ui->label_6->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#fdedc5",m_useDarkStyle)));
    ui->label_6->setText(tr("Band Activity"));
    m_bSimplex = false;
  }
  enable_DXCC_entity ();  // updates station identity and text window proportions
}

void MainWindow::on_TxFreqSpinBox_valueChanged(int n)
{
//  if(n<200 && (!m_rigOk || ui->readFreq->text()!="S")) n=200;
  m_wideGraph->setTxFreq(n);
  if(m_lockTxFreq) ui->RxFreqSpinBox->setValue(n);
  if (m_tci) Q_EMIT m_config.transceiver_trfrequency(n - m_XIT);
  else Q_EMIT transmitFrequency (n - m_XIT);
  statusUpdate ();
}

void MainWindow::on_RxFreqSpinBox_valueChanged(int n)
{
  m_wideGraph->setRxFreq(n);
  if (m_lockTxFreq && ui->TxFreqSpinBox->isEnabled ()) ui->TxFreqSpinBox->setValue (n);
  statusUpdate ();
}

void MainWindow::on_candListSpinBox_valueChanged(int n) { m_ncandthin=n; }

void MainWindow::on_actionQuickDecode_triggered() { m_ndepth=1; ui->actionQuickDecode->setChecked(true); }
void MainWindow::on_actionMediumDecode_triggered() { m_ndepth=2; ui->actionMediumDecode->setChecked(true); }
void MainWindow::on_actionDeepestDecode_triggered() { m_ndepth=3; ui->actionDeepestDecode->setChecked(true); }

void MainWindow::on_actionDecFT8cycles1_triggered() { m_nFT8Cycles=1; ui->actionDecFT8cycles1->setChecked(true); }
void MainWindow::on_actionDecFT8cycles2_triggered() { m_nFT8Cycles=2; ui->actionDecFT8cycles2->setChecked(true); }
void MainWindow::on_actionDecFT8cycles3_triggered() { m_nFT8Cycles=3; ui->actionDecFT8cycles3->setChecked(true); }

void MainWindow::on_actionDecFT8SWLcycles1_triggered() { m_nFT8SWLCycles=1; ui->actionDecFT8SWLcycles1->setChecked(true); }
void MainWindow::on_actionDecFT8SWLcycles2_triggered() { m_nFT8SWLCycles=2; ui->actionDecFT8SWLcycles2->setChecked(true); }
void MainWindow::on_actionDecFT8SWLcycles3_triggered() { m_nFT8SWLCycles=3; ui->actionDecFT8SWLcycles3->setChecked(true); }

void MainWindow::on_actionRXfLow_triggered() { m_nFT8RXfSens=1; ui->actionRXfLow->setChecked(true); }
void MainWindow::on_actionRXfMedium_triggered() { m_nFT8RXfSens=2; ui->actionRXfMedium->setChecked(true); }
void MainWindow::on_actionRXfHigh_triggered() { m_nFT8RXfSens=3; ui->actionRXfHigh->setChecked(true); }

void MainWindow::on_actionFT4fast_triggered() { m_nFT4depth=1; ui->actionFT4fast->setChecked(true); }
void MainWindow::on_actionFT4medium_triggered() { m_nFT4depth=2; ui->actionFT4medium->setChecked(true); }
void MainWindow::on_actionFT4deep_triggered() { m_nFT4depth=3; ui->actionFT4deep->setChecked(true); }

void MainWindow::on_actionSwitch_Filter_OFF_at_sending_73_triggered(bool checked)
{
  if(checked) {
    if(ui->actionSwitch_Filter_OFF_at_getting_73->isChecked()) ui->actionSwitch_Filter_OFF_at_getting_73->setChecked(false);
    m_FilterState=1;
    ui->actionSwitch_Filter_OFF_at_sending_73->setChecked(true);
  } else {
    ui->actionSwitch_Filter_OFF_at_sending_73->setChecked(false);
    if(!ui->actionSwitch_Filter_OFF_at_getting_73->isChecked()) {
      m_FilterState=0;
      on_actionAutoFilter_toggled(false); ui->actionAutoFilter->setChecked(false);
    } else {
      m_FilterState=2;
    }
  }
}

void MainWindow::on_actionSwitch_Filter_OFF_at_getting_73_triggered(bool checked)
{
  if(checked) {
    if(ui->actionSwitch_Filter_OFF_at_sending_73->isChecked()) ui->actionSwitch_Filter_OFF_at_sending_73->setChecked(false);
    m_FilterState=2;
    ui->actionSwitch_Filter_OFF_at_getting_73->setChecked(true);
  } else {
    ui->actionSwitch_Filter_OFF_at_getting_73->setChecked(false);
    if(!ui->actionSwitch_Filter_OFF_at_sending_73->isChecked()) {
      m_FilterState=0;
      on_actionAutoFilter_toggled(false); ui->actionAutoFilter->setChecked(false);
    } else {
      m_FilterState=1;
    }
  }
}

void MainWindow::on_actionErase_ALL_TXT_triggered()          //Erase ALL.TXT
{ /* Log and recording deletion is intentionally performed outside the application. */ }

void MainWindow::on_actionErase_wsjtx_log_adi_triggered()
{ /* Log and recording deletion is intentionally performed outside the application. */ }

void MainWindow::on_actionOpen_wsjtx_log_adi_triggered() { on_actionOpen_log_directory_triggered(); }
void MainWindow::on_actionOpen_log_directory_triggered() {
  QDesktopServices::openUrl(QUrl::fromLocalFile(m_config.log_directory().absolutePath()));
}

bool MainWindow::gridOK(QString g)
{
  bool b=false;
  if(g.length()>=4) {
    b=g.left(1).compare("A")>=0 and
        g.left(1).compare("R")<=0 and
        g.mid(1,1).compare("A")>=0 and
        g.mid(1,1).compare("R")<=0 and
        g.mid(2,1).compare("0")>=0 and
        g.mid(2,1).compare("9")<=0 and
        g.mid(3,1).compare("0")>=0 and
        g.mid(3,1).compare("9")<=0;
		
    if(g.length()==4 and g=="RR73") b=false;
  }
  return b;
}

bool MainWindow::gridRR73(QString g)
{
  bool RR73 = false;
  if (g.length()==4 and g=="RR73") RR73 = true;
  return RR73;
}

bool MainWindow::reportRCVD(QStringList msg)
{
  bool rprt_rcvd = false;
  if (msg.length() == 4) { // three words(max index 3) in the message where "CQ DX" is counted as single word
	if (msg.at (1).contains (m_baseCall) && msg.at (2).contains (Radio::base_callsign (m_hisCall))
		&& (msg.at (3).contains ("-") || msg.at (3).contains ("+"))) rprt_rcvd = true;
  }
  return rprt_rcvd;
}

bool MainWindow::rReportRCVD(QStringList msg)
{
  bool rogerrprt_rcvd = false;
  if (msg.length() == 4) { // three words(max index 3) in the message where "CQ DX" is counted as single word
	if (msg.at (1).contains (m_baseCall) && msg.at (2).contains (Radio::base_callsign (m_hisCall))
		&& (msg.at (3).contains ("R-") || msg.at (3).contains ("R+"))) rogerrprt_rcvd = true;
	}
  return rogerrprt_rcvd;
}

void MainWindow::on_bandComboBox_currentIndexChanged (int index)
{
  auto const& frequencies = m_config.frequencies ();
  auto const& source_index = frequencies->mapToSource (frequencies->index (index, FrequencyList_v2::frequency_column));
  Frequency frequency {m_freqNominal};
  if (source_index.isValid ())
    {
      frequency = frequencies->frequency_list ()[source_index.row ()].frequency_;
      m_callingFrequency = frequency;
    }

  // Lookup band
  auto const& band  = m_config.bands ()->find (frequency);
  if (!band.isEmpty ())
    {
      ui->bandComboBox->lineEdit ()->setStyleSheet ({});
      ui->bandComboBox->setCurrentText (band);
    }
  else
    {
      ui->bandComboBox->lineEdit ()->setStyleSheet (QString("QLineEdit {color: %1; background-color : %2}").arg(Radio::convert_dark("#ffff00",m_useDarkStyle),Radio::convert_dark("#ff0000",m_useDarkStyle)));
      ui->bandComboBox->setCurrentText (m_config.bands ()->oob ());
    }
  displayDialFrequency ();
}

void MainWindow::on_bandComboBox_activated (int index)
{
  auto const& frequencies = m_config.frequencies ();
  auto const& source_index = frequencies->mapToSource (frequencies->index (index, FrequencyList_v2::frequency_column));
  Frequency frequency {m_freqNominal};
  if (source_index.isValid ()) frequency = frequencies->frequency_list ()[source_index.row ()].frequency_;
  m_bandEdited = true;
  band_changed (frequency); if(m_config.write_decoded_debug()) writeToALLTXT("Band changed from bandComboBox, frequency: " + QString::number(frequency));
  m_wideGraph->setRxBand (m_config.bands ()->find (frequency));
}

void MainWindow::band_changed (Frequency f)
{
  if (m_bandEdited) {
    if (!m_mode.startsWith ("WSPR")) { // band hopping preserves auto Tx
      if (f + m_wideGraph->nStartFreq () > m_freqNominal + ui->TxFreqSpinBox->value ()
          || f + m_wideGraph->nStartFreq () + m_wideGraph->fSpan () <=
          m_freqNominal + ui->TxFreqSpinBox->value ()) {
        qDebug () << "start f:" << m_wideGraph->nStartFreq () << "span:" << m_wideGraph->fSpan () << "DF:" << ui->TxFreqSpinBox->value ();
        // disable auto Tx if "blind" QSY outside of waterfall
//        ui->stopTxButton->click (); // halt any transmission
        if(m_transmitting || g_iptt==1) haltTx("band changed ");
        enableTx_mode (false);       // switch off Enable Tx button
      }
    }
    auto const& newband = m_config.bands ()->find (f);
    auto const& oldband = m_config.bands ()->find (m_lastMonitoredFrequency);
    bool cleared=false;
    if (oldband != newband || m_oldmode != m_mode) {
      clearDX (" cleared, triggered by erase both windows option upon band change, new band/mode"); // Request from Boris UX8IW
      if (m_autoEraseBC) { // option: erase both windows if band is changed
          clearBandActivity();
          ui->decodedTextBrowser2->clear();
          cleared=true;
      }
    }
//    m_lastBand.clear ();
    m_bandEdited = false;
    psk_Reporter->sendReport();      // Upload any queued spots before changing band
    m_okToPost = false;
    if(!m_transmitting && !m_start2 && m_monitoroff) { monitor (true); m_monitoroff=false; }

    m_nsecBandChanged=0;
    if(!m_transmitting && (oldband != newband || m_oldmode != m_mode) && m_rigOk && !m_config.rig_name().startsWith("None")) {
      m_bandChanged=true;
      qint64 ms = m_jtdxtime->currentMSecsSinceEpoch2() % 86400000; int nsec=ms/1000;
      double TRperiod=60.0; // TR period is the only reliable way in this point of code at the mode change 
      if(m_mode=="FT8") TRperiod=15.0;
      else if(m_mode=="FT4") TRperiod=7.5;
      else if(m_mode=="FT2") TRperiod=3.75;
      int nseqmod = fmod(double(nsec),TRperiod);
      m_nsecBandChanged=nseqmod;
    }

    m_freqNominal = f;
    m_freqTxNominal = m_freqNominal;
    setRig ();
    setXIT (ui->TxFreqSpinBox->value ());
    qint64 fDelta = m_lastDisplayFreq - m_freqNominal;
    if (ui->outAttenuation->value() == 1 && m_outAttenuation != 1 ) {ui->outAttenuation->setValue (m_outAttenuation); m_outAttenuation = 1;}
    if (qAbs(fDelta)>1000000) {
        m_qsoHistory.init(); if(m_config.write_decoded_debug()) writeToALLTXT("QSO history initialized by band_changed");
        clearDX (" cleared, triggered by erase both windows option upon band change, delta frequency"); // Request from Boris UX8IW
        if (m_autoEraseBC && !cleared) { // option: erase both windows if band is changed
            clearBandActivity();
            ui->decodedTextBrowser2->clear();
        }
    // Set the attenuation value if options are checked
        QString curBand;
        if (m_config.pwrBandTxMemory() && !m_tune) {
            if (m_mode == "JT9+JT65" && m_modeTx == "JT65") { curBand = m_config.bands ()->find (m_freqNominal)+m_modeTx; }
            else { curBand = m_config.bands ()->find (m_freqNominal)+m_mode; }
            if (m_pwrBandTxMemory.contains(curBand)) { m_PwrBandSetOK = false; ui->outAttenuation->setValue(m_pwrBandTxMemory[curBand].toInt()); m_PwrBandSetOK = true;/* printf("set power from bandchange % lld %lld %s %d\n",m_lastDisplayFreq,m_freqNominal,curBand.toStdString().c_str(),m_pwrBandTxMemory[curBand].toInt());*/}
            else { m_pwrBandTxMemory[curBand] = ui->outAttenuation->value(); }
        }
        ui->bandComboBox->setCurrentText (m_config.bands ()->find (m_freqNominal));
        m_wideGraph->setRxBand (m_config.bands ()->find (m_freqNominal));
    }
    m_lastDisplayFreq=m_freqNominal;
    m_oldmode=m_mode;
    bool commonFT8b=false;
//    Switch off Hound mode if coming to the regular FT8 band
    if(m_mode!="FT8") {
      if(m_houndMode) ui->actionEnable_hound_mode->setChecked(false);
    } else {
      for(long unsigned int i=0; i < sizeof (m_ft8Freq) / sizeof (m_ft8Freq[0]); i++) {
        int kHzdiff=m_freqNominal/1000 - m_ft8Freq[i];
        if(kHzdiff > -2 && kHzdiff < 3) { if(m_houndMode) ui->actionEnable_hound_mode->setChecked(false); commonFT8b=true; break; }
      }
      m_commonFT8b=commonFT8b;
      if (m_houndMode) {
      // Don't allow Hound frequency control in common FT8 bands if VFO Split mode is switched off
        QString message = "";
        if(!m_superFoxMode && !m_config.split_mode() && !m_commonFT8b && m_config.rig_name() != "None") {
          message =  tr ("Hound mode TX frequency control requires"
                                                              " *Split* rig control (either *Rig* or *Fake It* set"
                                                              " in the *Settings | Radio* tab.)");
          JTDXMessageBox::warning_message (this, "", tr ("Hound TX frequency control warning"), message);
          ui->actionEnable_hound_mode->setChecked(false);
        } else {
          m_houndTXfreqJumps=!m_superFoxMode && !m_commonFT8b && m_config.split_mode() && m_config.rig_name() != "None";
          ui->actionUse_TX_frequency_jumps->setChecked(m_houndTXfreqJumps);
          if(m_superFoxMode || m_commonFT8b || m_config.rig_name() == "None") ui->actionUse_TX_frequency_jumps->setEnabled(false);
          else ui->actionUse_TX_frequency_jumps->setEnabled(true);
        }
      }
    }
    m_lastloggedtime=m_lastloggedtime.addSecs(-7*int(m_TRperiod));
    m_lastloggedcall.clear(); setLastLogdLabel();
  }
}

void MainWindow::enable_DXCC_entity ()
{
  if (m_mode.left(4)!="WSPR" && (m_callNotif != m_config.callNotif() || m_callsign != m_config.my_callsign() || m_gridNotif != m_config.gridNotif() || m_grid != m_config.my_grid() || m_timeFrom != m_config.timeFrom() || m_strictdirCQ != m_config.strictdirCQ())) {
    if (m_callNotif != m_config.callNotif() || m_callsign != m_config.my_callsign() || m_gridNotif != m_config.gridNotif() || m_grid != m_config.my_grid() || m_timeFrom != m_config.timeFrom()) {
      m_qsoHistory.init(); if(m_config.write_decoded_debug()) writeToALLTXT("QSO history initialized by enable_DXCC_entity");
      m_callsign = m_config.my_callsign();
      m_grid = m_config.my_grid();
      m_callNotif = m_config.callNotif();
      m_gridNotif = m_config.gridNotif();
      m_timeFrom = m_config.timeFrom();
    }
    updateOwnDxcc();
    m_strictdirCQ = m_config.strictdirCQ();
  }
  updateGeometry ();
 }

void MainWindow::updateOwnDxcc() {
  QString country;m_logBook.getDXCC(m_config.my_callsign(),country);
  auto items=country.split(',');m_m_continent=items.value(0);m_m_prefix=items.value(1);
  ui->decodedTextBrowser->setMyContinent(m_m_continent);
  ui->decodedTextBrowser2->setMyContinent(m_m_continent);
  m_qsoHistory.owndata(m_m_continent,m_m_prefix,m_config.my_grid(),m_config.strictdirCQ());
}

void MainWindow::on_pbCallCQ_clicked()
{
//  clearDXfields(" field cleared, SLOT on_pbCallCQ_clicked()"); // this line is duplicated in SLOT on_txb6_clicked()
//need to sync CQ direction, instead of  ui->txrb6->setChecked(true); :
  ui->txb6->click (); // check if there is any dependency
  genStdMsgs(m_rpt);
  ui->genMsg->setText(ui->tx6->text());
  m_ntx=7;
  m_QSOProgress = CALLING;
  m_nlasttx=6;
  ui->rbGenMsg->setChecked(true);
  if(m_transmitting) m_restart=true;
}

void MainWindow::on_pbAnswerCaller_clicked()
{
  genStdMsgs(m_rpt);
  QString t=ui->tx3->text();
  int i0=t.indexOf(" R-");
  if(i0<0) i0=t.indexOf(" R+");
  t=t.left(i0+1)+t.mid(i0+2,3);
  ui->genMsg->setText(t);
  m_ntx=7;
  m_QSOProgress = REPORT;
  m_nlasttx=2;
  ui->rbGenMsg->setChecked(true);
  if(m_transmitting) m_restart=true;
}

void MainWindow::on_pbSendRRR_clicked()
{
  genStdMsgs(m_rpt);
  ui->genMsg->setText(ui->tx4->text());
  m_ntx=7;
  m_QSOProgress = ROGERS;
  m_nlasttx=4;
  ui->rbGenMsg->setChecked(true);
  if(m_transmitting) m_restart=true;
}

void MainWindow::resizeEvent(QResizeEvent *event) { 
  if(event->size().height() != event->oldSize().height()) dynamicButtonsInit(); 
}

void MainWindow::mousePressEvent(QMouseEvent *event)             //mousePressEvent
{
  if(ui->ClearDxButton->hasFocus() && (event->button() & Qt::RightButton)) {
    if (!m_hisCall.isEmpty()) {
      if (event->modifiers() & Qt::ControlModifier)
          m_qsoHistory.blacklist(m_hisCall);    
      else
          m_qsoHistory.remove(m_hisCall);    
      clearDX (" cleared and erased from QSO history by right mouse button click, user action");
    }   
  }
  
  if(ui->syncButton->hasFocus() && (event->button() & Qt::RightButton) && m_jtdxtime->GetOffset() != 0.0) {
    m_jtdxtime->SetOffset(0);
    ui->syncButton->setEnabled(false);
  }

  if(ui->EraseButton->hasFocus() && (event->button() & Qt::RightButton)) {
    qint64 ms=m_jtdxtime->currentMSecsSinceEpoch2();
    ui->decodedTextBrowser2->clear();
    if(m_mode.left(4)=="WSPR") { clearBandActivity(); }
    else {
      m_QSOText.clear();
      if((ms-m_msErase)<500) {
      clearBandActivity();

//     QFile f(m_config.temp_dir ().absoluteFilePath ("decoded.txt"));
//     if(f.exists()) f.remove();
      }
    }
    m_msErase=ms;
  }

  if(ui->pbSpotDXCall->hasFocus() && (event->button() & Qt::RightButton)) {
    QString basecall = Radio::base_callsign (m_hisCall);
    if(basecall.length () > 2) {
        m_config.add_callsign_hideFilter (basecall);
        ui->pbSpotDXCall->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: outset;border-width: 1px;border-color: %3;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#00ff00",m_useDarkStyle),Radio::convert_dark("#808080",m_useDarkStyle)));
    }
  }
}

void MainWindow::on_pbAnswerCQ_clicked()
{
  genStdMsgs(m_rpt);
  ui->genMsg->setText(ui->tx1->text());
  if(!m_mode.startsWith("FT")) { QString t=ui->tx2->text(); int i0=t.indexOf("/"); int i1=t.indexOf(" "); if(i0>0 and i0<i1) ui->genMsg->setText(t); }
  m_ntx=7;
  m_QSOProgress = REPLYING;
  m_nlasttx=1;
  ui->rbGenMsg->setChecked(true);
  if(m_transmitting) m_restart=true;
}

void MainWindow::on_pbSendReport_clicked()
{
  genStdMsgs(m_rpt);
  ui->genMsg->setText(ui->tx3->text());
  m_ntx=7;
  m_QSOProgress = ROGER_REPORT;
  m_nlasttx=3;
  ui->rbGenMsg->setChecked(true);
  if(m_transmitting) m_restart=true;
}

void MainWindow::on_pbSend73_clicked()
{
  genStdMsgs(m_rpt);
  ui->genMsg->setText(ui->tx5->currentText());
  m_ntx=7;
  m_QSOProgress = SIGNOFF;
  m_nlasttx=5;
  ui->rbGenMsg->setChecked(true);
  if(m_transmitting) m_restart=true;
}

void MainWindow::on_rbGenMsg_clicked(bool checked)
{
  m_freeText=!checked;
  if(!m_freeText) {
    if(m_ntx != 7 && m_transmitting) m_restart=true;
    m_ntx=7;
  }
}

void MainWindow::on_rbFreeText_clicked(bool checked)
{
  m_freeText=checked;
  if(m_freeText) {
    m_ntx=8;
    m_QSOProgress = SIGNOFF; // is one more value required for free text?
    m_nlasttx=8;
    if (m_transmitting) m_restart=true;
  }
}

void MainWindow::on_freeTextMsg_currentTextChanged (QString const& text)
{
  bool isAllowedAuto73=isAutoSeq73(text);
  if(!m_FTsetAutoSeqOff && !isAutoSeq73(text)) m_FTsetAutoSeqOff=true;
  if(isAllowedAuto73) m_FTsetAutoSeqOff=false;
  if(!text.contains(QRegularExpression {R"([@#&^])"}) && !text.isEmpty()) {
    QString t="161545  -4  0.1 1939 & " + text;
    DecodedText decodedtext {t,this,upstreamMode()};
//      DecodedText decodedtext {"161545  -4  0.1 1939 & CQ RT9K/4    ",this};
    bool stdfreemsg = decodedtext.isStandardMessage();
    if(stdfreemsg) {
      ui->freeTextMsg->setStyleSheet(QString("background: %1;color: %2").arg(Radio::convert_dark("#7bff7b",m_useDarkStyle),Radio::convert_dark("#000000",m_useDarkStyle)));
    } else {
      if(text.length() < 14) { ui->freeTextMsg->setStyleSheet(QString("background: %1;color: %2").arg(Radio::convert_dark("#7bff7b",m_useDarkStyle),Radio::convert_dark("#000000",m_useDarkStyle))); }
      else if(text.length() >= 14) { ui->freeTextMsg->setStyleSheet(QString("background: %1;color: %2").arg(Radio::convert_dark("#ffff00",m_useDarkStyle),Radio::convert_dark("#000000",m_useDarkStyle))); }
      msgtype(text, ui->freeTextMsg->lineEdit ());
    }
  }
}

void MainWindow::on_freeTextMsg_currentIndexChanged(int index)
{
//this functionality can affect AutoSeq operation
//it is dedicated to change free message during Tx if new message is selected from the list
  if(m_transmitting && ui->rbFreeText->isChecked() && index != m_oldFreeMsgIndex) m_restart=true;	
  m_oldFreeMsgIndex=index;
}

void MainWindow::on_rptSpinBox_valueChanged(int n)
{
  m_rpt=QString::number(n);
  int ntx0=m_ntx;
  QString t=ui->tx5->currentText();
  genStdMsgs(m_rpt);
  ui->tx5->setCurrentText(t);
  m_ntx=ntx0;
  if(m_ntx==1) { ui->txrb1->setChecked(true); }
  else if(m_ntx==2) { ui->txrb2->setChecked(true); }
  else if(m_ntx==3) { ui->txrb3->setChecked(true); }
  else if(m_ntx==4) { ui->txrb4->setChecked(true); }
  else if(m_ntx==5) { ui->txrb5->setChecked(true); }
  else if(m_ntx==6) { ui->txrb6->setChecked(true); }
  statusChanged();
}

void MainWindow::on_tuneButton_clicked (bool checked)
{
  if(checked && (m_mode=="JTTY" || (m_mode=="FT2" && m_tci))) {ui->tuneButton->setChecked(false);return;}
  if (checked && !m_manualTuneAuthorized && !m_manualTxAuthorized) {
    ui->tuneButton->setChecked(false);
    return;
  }
  if (!checked) m_manualTuneAuthorized=false;
  m_config.set_station_locked(checked || m_enableTx || m_transmitting || m_logDlg->isVisible() || logQSOTimer.isActive());
  updateStationSelectorEnabled();
  static bool lastChecked = false;
  if (lastChecked == checked) return;
  lastChecked = checked;
  if(!checked) m_addtx = -2;
  QString curBand;
  if (m_mode == "JT9+JT65" && m_modeTx == "JT65") { curBand = ui->bandComboBox->currentText()+m_modeTx; }
  else { curBand = ui->bandComboBox->currentText()+m_mode; }
  if (checked && m_tune==false) { // we're starting tuning so remember Tx and change pwr to Tune value
    if (m_config.pwrBandTuneMemory ()) {
      m_pwrBandTxMemory[curBand] = ui->outAttenuation->value(); // remember our Tx pwr
      if (m_pwrBandTuneMemory.contains(curBand)) {
        m_PwrBandSetOK = false;
        ui->outAttenuation->setValue(m_pwrBandTuneMemory[curBand].toInt()); // set to Tune pwr
        m_PwrBandSetOK = true;
      }
    }
  } else { // we're turning off so remember our Tune pwr setting and reset to Tx pwr
	if (m_config.pwrBandTuneMemory() || m_config.pwrBandTxMemory()) {
		m_pwrBandTuneMemory[curBand] = ui->outAttenuation->value(); // remember our Tune pwr
		m_PwrBandSetOK = false;
		ui->outAttenuation->setValue(m_pwrBandTxMemory[curBand].toInt()); // set to Tx pwr
		m_PwrBandSetOK = true;
    }
  }
  if (m_tune) {
	if (!tuneButtonTimer.isActive())
		tuneButtonTimer.start(250);
  } else {
    m_sentFirst73=false;
    itone[0]=0;
//    on_monitorButton_clicked (true);
    m_tune=true;
  }
  if (m_tci) Q_EMIT m_config.transceiver_tune(checked);
  else Q_EMIT tune (checked);
  if(checked && m_config.tunetimer() && !m_tuneup) StopTuneTimer.start(m_config.tunetimer()*1000); // shall not start at WSPR band hopping
}

void MainWindow::stop_tuning ()
{
  if(StopTuneTimer.isActive()) StopTuneTimer.stop();
  on_tuneButton_clicked(false);
  ui->tuneButton->setChecked (false);
  ui->tuneButton->setText(tr("&Tune"));
  m_bTxTime=false;
  m_tune=false;
}

void MainWindow::stopTuneATU() { on_tuneButton_clicked(false); m_bTxTime=false; }

void MainWindow::on_stopTxButton_clicked()                    //Stop Tx
{
  resetOrdinaryBusy();
  if(m_txStartClock.isValid() && !m_txStartReported && (g_iptt || m_tx_when_ready || m_transmitting)
     && (m_txStartClock.elapsed()>1000 || m_config.write_decoded_debug()))
    writeTxTiming(QString("TX start timing: phase=cancelled mode=%1 call=%2 elapsed_ms=%3 ptt_ack_ms=%4 audio_request_ms=%5")
      .arg(m_modeTx,m_hisCall).arg(m_txStartClock.elapsed()).arg(m_txPttAckMs).arg(m_txAudioRequestedMs));
  m_txStartClock.invalidate();
  m_audioPlaybackGeneration=0;
  ++m_signoffGeneration;
  m_signoff.reset();
  flushSignoffLogRefresh();
  logQSOTimer.stop();
  resetGeneralWait();
  m_upstreamState.cancelTransmit();
  if(m_emeTools)m_emeTools->setEnabled(true);
  if(m_fullDuplexAction)m_fullDuplexAction->setEnabled(true);
  if(m_tci)Q_EMIT m_config.transceiver_tx_pcm(QByteArray());
  // Transmitting suspends capture without changing the operator's Monitor
  // preference. Aborting TX clears g_iptt below, so guiUpdate will no longer
  // enter stopTx() to resume capture; restore it here after lowering PTT.
  const bool resumeMonitoring=!m_monitoroff && !m_diskData
    && (m_transmitting || m_tune || g_iptt!=0 || m_tx_when_ready || ptt0Timer.isActive());
  if(m_upstream){m_upstream->cancelTransmit();m_upstreamPcm.clear();m_upstreamPcmConsumed=false;m_upstreamTxTimeout.stop();++m_upstreamTxRevision;}
  if(m_jtty){m_jtty->cancelTransmit();m_jttyPreparing=false;m_jttyPcm.clear();m_jttyPcmConsumed=false;m_jttyPttTimeout.stop();}
  m_managedTxReady=false;m_managedCqOnAir=false;m_managedCallOnAir=false;m_managed.cancelAttempt();
  m_manualTxAuthorized=false; m_manualTuneAuthorized=false;
  if(m_logProcessingDialog)m_logProcessingDialog->setStopVisible(false);
  TxAgainTimer.stop(); enableTxButtonTimer.stop(); ptt1Timer.stop(); ptt0Timer.stop();
  StopTuneTimer.stop(); tuneButtonTimer.stop();
  m_tuneup=false;
  if (m_transmitting || m_tune) m_addtx = -1;
  if (m_tune) stop_tuning ();
  enableTx_mode(false);
  if (m_tci) Q_EMIT m_config.transceiver_modulator_stop();
  else Q_EMIT endTransmitMessage();
  Q_EMIT m_config.transceiver_ptt(false);
  g_iptt=0;
  m_btxok=false;
  m_nlasttx=0;
//  if(m_autofilter && m_autoseq && m_filter) autoFilter (false);
  if(m_filter && (m_FilterState==1 || m_FilterState==2)) autoFilter (false);
  if(!m_transmitting && g_iptt==1) m_haltTrans=true;
  if (m_haltTxWritten) m_haltTxWritten=false;
  else writeHaltTxEvent("Halt Tx button clicked ");
  if(m_transmitting && m_skipTx1 && !m_hisCall.isEmpty() && (m_ntx==2 || m_QSOProgress==REPORT)) m_qsoHistory.remove(m_hisCall);
// sync TX variables, RX scenario is covered in on_enableTxButton_clicked ()
  m_bTxTime=false; m_tx_when_ready=false; m_transmitting=false; m_restart=false; m_txNext=false;
  transmitDisplay(false);
  if(resumeMonitoring && !m_monitoring)monitor(true);
  if(upstreamMode()) {
    tx_status_label->setText(m_monitoring ? tr("Receiving") : tr("Stopped"));
    progressBar->setStyleSheet(QString());
  }
  m_config.set_station_locked(m_logDlg->isVisible() || logQSOTimer.isActive());
  updateStationSelectorEnabled();
  updateJttyUi();
}

void MainWindow::rigOpen ()
{
  ui->readFreq->setStyleSheet(ui->readFreq->styleSheet().left(230)+QString("background: %1;\n color: %2;\n}").arg(Radio::convert_dark("#ffa500",m_useDarkStyle),Radio::convert_dark("#000000",m_useDarkStyle)));
  m_rigOk=false;
  ui->readFreq->setText ("");
  ui->readFreq->setEnabled (true);
  m_config.transceiver_online ();
  Q_EMIT m_config.sync_transceiver (true, true);
}

void MainWindow::on_pbR2T_clicked()
{
  if (ui->TxFreqSpinBox->isEnabled ()) ui->TxFreqSpinBox->setValue(ui->RxFreqSpinBox->value ());
}

void MainWindow::on_pbT2R_clicked()
{
  if (ui->RxFreqSpinBox->isEnabled ()) ui->RxFreqSpinBox->setValue (ui->TxFreqSpinBox->value ());
}

void MainWindow::on_readFreq_clicked()
{
  if (m_transmitting) return;
  if (m_config.transceiver_online ()) Q_EMIT m_config.sync_transceiver (true, true);
}

void MainWindow::on_pbTxMode_clicked()
{
  QString curBand;
  if(m_modeTx=="JT9") {
    m_modeTx="JT65";
    ui->pbTxMode->setText("Tx JT65  #");
  } else {
    m_modeTx="JT9";
    ui->pbTxMode->setText("Tx JT9  @");
  }
  if (m_config.pwrBandTxMemory() && !m_tune) {
      if (m_mode == "JT9+JT65" && m_modeTx == "JT65") { curBand = ui->bandComboBox->currentText()+m_modeTx; }
      else { curBand = ui->bandComboBox->currentText()+m_mode; }
      if (m_pwrBandTxMemory.contains(curBand)) { ui->outAttenuation->setValue(m_pwrBandTxMemory[curBand].toInt()); }
      else { m_pwrBandTxMemory[curBand] = ui->outAttenuation->value(); }
  }
  m_wideGraph->setModeTx(m_modeTx);
  statusChanged();
  on_spotLineEdit_textChanged(ui->spotLineEdit->text());
}

void MainWindow::setXIT(int n, Frequency base)
{
  
  if(m_mode=="JT9+JT65") {
	if(m_wideGraph->Fmin() <= m_config.ntopfreq65()) {
		if(m_modeTx == "JT65" && n >= m_config.ntopfreq65()) {
			on_pbTxMode_clicked();
			on_pbT2R_clicked();
		} else {
			if(m_modeTx == "JT9" && n <= m_wideGraph->Fmin()) {
				on_pbTxMode_clicked();
				on_pbT2R_clicked();
			}
		}
	} else {
		if(m_modeTx == "JT65" && n >= m_wideGraph->Fmin()) {
			on_pbTxMode_clicked();
			on_pbT2R_clicked();
		} else {
			if(m_modeTx == "JT9" && n <= m_config.ntopfreq65()) {
				on_pbTxMode_clicked();
				on_pbT2R_clicked();
			}
		}
	}
  }
  
  if (!base) base = m_freqNominal;
  m_XIT = 0;
  if (!m_bSimplex && base) {
    // m_bSimplex is false, so we can use split mode if requested
    if (m_config.split_mode ()) {if (m_tci) m_XIT = n - 1500; else m_XIT = (n/500)*500 - 1500;}
    if ((m_monitoring || m_transmitting) && m_config.is_transceiver_online () && m_config.split_mode ()) {
        // All conditions are met, reset the transceiver Tx dial
        // frequency
        m_freqTxNominal = base + m_XIT;
        if (base == 1908000) {
          if (m_crossbandOptionEnabled && m_m_prefix != "JA") m_freqTxNominal -= 68000;
        } else if (base == 1810000) {
          if (m_crossbandHLOptionEnabled && m_m_prefix != "HL") m_freqTxNominal += 30000;
        } else if (base == 1840000) {
          if (m_crossbandOptionEnabled && m_m_prefix == "JA") m_freqTxNominal += 68000;
          if (m_crossbandHLOptionEnabled && m_m_prefix == "HL") m_freqTxNominal -= 30000;
        }
        Q_EMIT m_config.transceiver_tx_frequency (correctedFrequency(m_freqTxNominal,m_astroCorrection.tx));
	}
  }
  //Now set the audio Tx freq
  if (m_tci) Q_EMIT m_config.transceiver_trfrequency(ui->TxFreqSpinBox->value () - m_XIT);
  else Q_EMIT transmitFrequency (ui->TxFreqSpinBox->value () - m_XIT);
  if(m_transmitting) m_restart=true;
}

void MainWindow::setRxFreq4(int rxFreq)
{
    txwatchdog (false);
	if (m_mode=="JT9+JT65" && !m_transmitting && !m_lockTxFreq) {
		if(m_wideGraph->Fmin() <= m_config.ntopfreq65()) {
			if(m_modeTx == "JT65" && rxFreq >= m_config.ntopfreq65()) {
				on_pbTxMode_clicked();
				on_pbR2T_clicked();
			} else {
				if(m_modeTx == "JT9" && rxFreq <= m_wideGraph->Fmin()) {
					on_pbTxMode_clicked();
					on_pbR2T_clicked();
				}
			}
		} else {
			if(m_modeTx == "JT65" && rxFreq >= m_wideGraph->Fmin()) {
				on_pbTxMode_clicked();
				on_pbR2T_clicked();
			}
			if(m_modeTx == "JT65" && rxFreq > m_config.ntopfreq65() && rxFreq < m_wideGraph->Fmin()) {
				ui->RxFreqSpinBox->setValue (m_config.ntopfreq65());
			}
			if(m_modeTx == "JT9" && rxFreq <= m_config.ntopfreq65()) {
				on_pbTxMode_clicked();
				on_pbR2T_clicked();
			}
			if(m_modeTx == "JT9" && rxFreq < m_wideGraph->Fmin() && rxFreq > m_config.ntopfreq65()) {
				ui->RxFreqSpinBox->setValue (m_wideGraph->Fmin());
			}
		}
	}
}

void MainWindow::setFreq4(int rxFreq, int txFreq)
{
  txwatchdog (false);
  if (ui->RxFreqSpinBox->isEnabled ()) ui->RxFreqSpinBox->setValue(rxFreq);
  if(m_mode.left(4)=="WSPR") {
    ui->WSPRfreqSpinBox->setValue(txFreq);
  } else {
    if (ui->TxFreqSpinBox->isEnabled ()) {
      ui->TxFreqSpinBox->setValue(txFreq);
    }
  }
}

void MainWindow::on_pbTxLock_clicked(bool checked)
{
  m_lockTxFreq=checked;
  if(checked) {
     ui->pbTxLock->setText(tr("Lockd Tx=Rx"));
	 ui->pbTxLock->setToolTip(tr("<html><head/><body><p>Push button to allow Tx/Rx AF frequencies split operation.</p></body></html>"));
     ui->pbTxLock->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: solid;border-width: 1px;border-radius: 5px;border-color: %3;min-width: 5em;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#ffff88",m_useDarkStyle),Radio::convert_dark("#000000",m_useDarkStyle)));
  } else {
  	 ui->pbTxLock->setText(tr("Tx/Rx Split"));
     ui->pbTxLock->setToolTip(tr("<html><head/><body><p>Push button to lock Tx frequency to the Rx AF frequency.</p></body></html>"));
     ui->pbTxLock->setStyleSheet(QString("QPushButton {color: %1;background: %2;border-style: solid;border-width: 1px;border-color: %3;min-width: 5em;padding: 3px}").arg(Radio::convert_dark("#000000",m_useDarkStyle),Radio::convert_dark("#00ff00",m_useDarkStyle),Radio::convert_dark("#808080",m_useDarkStyle)));
  }
  m_wideGraph->setLockTxFreq(m_lockTxFreq);
  if(m_lockTxFreq) on_pbR2T_clicked();
}

void MainWindow::on_skipTx1_clicked(bool checked)
{
  m_skipTx1=checked;
  ui->skipGrid->setChecked(checked);
  if(checked) { if(ui->txrb1->isChecked()) on_txb2_clicked(); if(ui->genMsg->text() == ui->tx1->text()) ui->genMsg->setText(ui->tx2->text()); }
  else { if(ui->txrb2->isChecked()) on_txb1_clicked(); if(ui->genMsg->text() == ui->tx2->text()) ui->genMsg->setText(ui->tx1->text()); }
}

void MainWindow::on_skipGrid_clicked(bool checked)
{
  m_skipTx1=checked;
  ui->skipTx1->setChecked(checked);
  if(checked) { if(ui->txrb1->isChecked()) { ui->txrb2->setChecked(true); m_QSOProgress = REPORT; m_nlasttx=2; m_ntx=7; if(m_transmitting) m_restart=true;
                                             if(ui->genMsg->text() == ui->tx1->text()) ui->genMsg->setText(ui->tx2->text()); }}
  else { if(ui->txrb2->isChecked()) { ui->txrb1->setChecked(true); m_QSOProgress = REPLYING; m_nlasttx=1; m_ntx=7; if(m_transmitting) m_restart=true;
                                      if(ui->genMsg->text() == ui->tx2->text()) ui->genMsg->setText(ui->tx1->text()); }}	  
}

void MainWindow::handle_transceiver_update (Transceiver::TransceiverState const& s)
{
  updateStationSelectorEnabled();
  // qDebug () << "MainWindow::handle_transceiver_update:" << s;
  Transceiver::TransceiverState old_state {m_rigState};

  if(m_config.write_decoded_debug()) {
    QString curPttState = m_rigState.ptt () ? "PTT On" : "PTT Off";
    QString reqPttState = s.ptt () ? "PTT On" : "PTT Off";
    QString tx_when_ready = m_tx_when_ready ? "true" : "false";
    writeToALLTXT("handle_transceiver_update started, current rig state: " + curPttState + 
      ", requested state: " + reqPttState + ", m_tx_when_ready: " + tx_when_ready + ", g_iptt=" + QString::number(g_iptt));
   }
//   printf("%s(%0.1f) tranceiver update %d %d old %d new %d\n",m_jtdxtime->currentDateTimeUtc2().toString("hh:mm:ss.zzz").toStdString().c_str(),
//     m_jtdxtime->GetOffset(),m_tx_when_ready,g_iptt,m_rigState.ptt (),s.ptt ());
  if (!s.ptt() && m_rigState.ptt () && (m_transmitting || m_tune)) haltTx("Halt Tx from rig detected ");
  if (s.ptt () && !m_rigState.ptt ()) // safe to start audio
                                      // (caveat - DX Lab Suite Commander)
    {
 // waiting to Tx and still needed
 //Start-of-transmission sequencer delay
      if (m_tx_when_ready && g_iptt) {
          if(m_txStartClock.isValid()) {
            m_txPttAckMs=m_txStartClock.elapsed();
            if(m_txPttAckMs>1000 || m_config.write_decoded_debug())
              writeTxTiming(QString("TX start timing: phase=ptt_ack mode=%1 call=%2 elapsed_ms=%3")
                .arg(m_modeTx,m_hisCall).arg(m_txPttAckMs));
          }
//          QThread::currentThread()->setPriority(QThread::HighestPriority);
          int ms_delay=1000*m_config.txDelay();
          if(m_mode=="FT4" || m_mode=="FT2") ms_delay=20;
          ptt1Timer.start(ms_delay);
//          printf("ptt1Timer started\n");
          if(m_config.write_decoded_debug()) writeToALLTXT("ptt1Timer started");
      }
      m_tx_when_ready = false;
    }
  if(m_config.do_snr() && m_rigState.level() != s.level()) {
      ui->S_meter_button->setText(Radio::convert_Smeter(s.level(),ui->S_meter_button->isChecked()));
  }
  if(m_config.check_SWR() && s.online() && s.ptt() && s.swr()>250 && (m_transmitting || g_iptt || m_tune)) {
    haltTx("SWR exceeds 2.5");statusBar()->showMessage(tr("SWR %1 exceeds 2.5. TX stopped; enable TX manually after checking the antenna.").arg(s.swr()/100.0,0,'f',2),20000);
  }
  if(m_config.do_pwr()) {
    if (m_rigState.power() != s.power()) {
      ui->PWRlabel->setText(QString {tr("Pwr<br>%1 W")}.arg (round(s.power()/1000.)));
    }
    if (m_rigState.swr() != s.swr()) {
      if (s.swr() > 0) {
        if (s.swr()<1000) ui->SWRlabel->setText(QString {"swr%1"}.arg (s.swr()/100.,0,'f',2)); else ui->SWRlabel->setText(QString {"swr%1"}.arg (s.swr()/100.,0,'f',1));}
      else
        ui->SWRlabel->setText("");
    }
  }    
  m_rigState = s;
  if(!s.ptt())signoffPttReleased();
  auto old_freqNominal = m_freqNominal;
  m_freqNominal = m_upstreamState.nominalFromReport(s.frequency(),old_state.frequency(),m_freqNominal,m_astroCorrection.rx);
  // initializing
  if (!old_state.online () && s.online () && !m_diskData && !m_wav_future_watcher.isRunning()
      && !m_monitoroff && !(m_start2 && m_config.monitor_off_at_startup())) {
      // A late CAT connection must not override Stop or interrupt a WAV decode.
      on_monitorButton_clicked(true);
      if(m_config.write_decoded_debug()) writeToALLTXT("handle_transceiver_update: transceiver state transition from offline to online");
      if(m_mode=="FT8") on_actionFT8_triggered();
      else if(m_mode=="FT4") on_actionFT4_triggered();
  else if(m_mode=="FT2") on_actionFT2_triggered();
  else if(m_mode=="JTTY") on_actionJTTY_triggered();
      else if(m_mode=="JT9+JT65") on_actionJT9_JT65_triggered();
      else if(m_mode=="JT9") on_actionJT9_triggered();
      else if(m_mode=="JT65") on_actionJT65_triggered();
      else if(m_mode=="T10") on_actionT10_triggered();
      else if(m_mode=="WSPR-2") on_actionWSPR_2_triggered();
  else if(upstreamMode()) selectUpstreamMode(m_mode);
  }
  if (s.frequency () != old_state.frequency () || s.split () != m_splitMode) {
      m_splitMode = s.split ();
      if (!s.ptt ()) {
          if (old_freqNominal != m_freqNominal) m_freqTxNominal = m_freqNominal;
          if (m_monitoring) m_lastMonitoredFrequency = m_freqNominal;
          if (m_lastDialFreq != m_freqNominal)
            {
              m_lastDialFreq = m_freqNominal;
              m_secBandChanged=m_jtdxtime->currentMSecsSinceEpoch2()/1000;
//              if((s.frequency () < 30000000u || (s.frequency () > 30000000u && !m_config.tx_QSY_allowed ()))  && m_mode.left(4)!="WSPR") {
                // Write freq changes to ALL.TXT.
                QFile f2 {m_config.log_directory().absoluteFilePath (m_jtdxtime->currentDateTimeUtc2().toString("yyyyMM_")+"ALL.TXT")};
                if (f2.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
                  QTextStream out(&f2);
                  out << m_jtdxtime->currentDateTimeUtc2().toString("yyyyMMdd_hhmmss")
                      << "  " << qSetRealNumberPrecision (12) << (m_freqNominal / 1.e6) << " MHz  "
                      << m_mode << " JTDX v" << QCoreApplication::applicationVersion () << revision () <<
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                 endl;
#else
                 Qt::endl;
#endif

                  f2.close();
                } else {
                  JTDXMessageBox::warning_message (this, "", tr ("File Open Error")
                                               , tr ("Cannot open \"%1\" for append: %2")
                                               .arg (f2.fileName ()).arg (f2.errorString ()));
                }
//              }

              if (m_config.spot_to_psk_reporter ()) {
                pskSetLocal ();
              }
              statusChanged();
              m_wideGraph->setDialFreq(m_freqNominal / 1.e6);
            }
      } else {
          if(!m_astro || !m_astro->tracking())m_freqTxNominal = s.split () ? correctedFrequency(s.tx_frequency(),-m_astroCorrection.tx): correctedFrequency(s.frequency(),-m_astroCorrection.rx);
      }
      if (s.split ()) {
        setXIT (ui->TxFreqSpinBox->value ());
      }
  }

  displayDialFrequency ();
  ui->readFreq->setStyleSheet(ui->readFreq->styleSheet().left(230)+QString("background: %1;\n color: %2;\n}").arg(Radio::convert_dark("#00ff00",m_useDarkStyle),Radio::convert_dark("#000000",m_useDarkStyle)));
  m_rigOk=true;
  ui->readFreq->setEnabled (false);
  ui->readFreq->setText (s.split () ? "S" : "");
  if(m_config.write_decoded_debug()) {
    QString pttstate = s.ptt () ? "PTT On" : "PTT Off";
    QString splitstate = s.split () ? " Split On" : " Split Off";
    writeToALLTXT("handle_transceiver_update " + pttstate + splitstate  + " s.frequency:" + QString::number(s.frequency(),10) + " s.tx_frequency:"  + QString::number(s.tx_frequency(),10));
  }
  if(m_start2) { if(m_config.monitor_off_at_startup ()) on_monitorButton_clicked(false); m_start2=false; }
}

void MainWindow::handle_transceiver_failure (QString const& reason)
{
  ui->readFreq->setStyleSheet(ui->readFreq->styleSheet().left(230)+QString("background: %1;\n color: %2;\n}").arg(Radio::convert_dark("#ff0000",m_useDarkStyle),Radio::convert_dark("#000000",m_useDarkStyle)));
  m_rigOk=false;
  ui->readFreq->setEnabled (true);
  haltTx("Rig control error: " + reason + " ");
  rigFailure (tr("Rig Control Error"), reason);
}

void MainWindow::rigFailure (QString const& reason, QString const& detail)
{
  static bool first_error {true};
  if (first_error) {
      // one automatic retry
      QTimer::singleShot (0, this, SLOT (rigOpen ()));
      first_error = false;
  } else {
      m_rigErrorMessageBox.setText (reason);
      m_rigErrorMessageBox.setDetailedText (detail);

      // don't call slot functions directly to avoid recursion
      switch (m_rigErrorMessageBox.exec ())
        {
        case JTDXMessageBox::Ok:
          QTimer::singleShot (0, this, SLOT (on_actionSettings_triggered ()));
          break;

        case JTDXMessageBox::Retry:
          QTimer::singleShot (0, this, SLOT (rigOpen ()));
          break;

        case JTDXMessageBox::Cancel:
          QTimer::singleShot (0, this, SLOT (close ()));
          break;
        }
      first_error = true;       // reset
  }
}

void MainWindow::transmit (double snr)
{
  double toneSpacing=0.0;
  if (m_modeTx == "FT8") {
//    toneSpacing=12000.0/1920.0;
    toneSpacing=-3.0; //GFSK wave
    if (m_tci) Q_EMIT m_config.transceiver_modulator_start(NUM_FT8_SYMBOLS,1920.0,ui->TxFreqSpinBox->value()-m_XIT,toneSpacing,true,snr,m_TRperiod,m_signoffGeneration);
    else Q_EMIT sendMessage (NUM_FT8_SYMBOLS,1920.0,ui->TxFreqSpinBox->value()-m_XIT,toneSpacing,m_soundOutput,
                        m_config.audio_output_channel(),true,snr,m_TRperiod,m_signoffGeneration);
  }
  else if (m_modeTx == "FT2") {
    if(m_tci) {haltTx("FT2 requires a local audio device ");return;}
    Q_EMIT sendMessage(NUM_FT4_SYMBOLS,288.0,ui->TxFreqSpinBox->value()-m_XIT,-2.0,m_soundOutput,
                      m_config.audio_output_channel(),true,snr,m_TRperiod,m_signoffGeneration);
  }
  else if (m_modeTx == "FT4") {
    toneSpacing=-2.0;                     //Transmit a pre-computed, filtered waveform.
    if (m_tci) Q_EMIT m_config.transceiver_modulator_start(NUM_FT4_SYMBOLS,576.0,ui->TxFreqSpinBox->value()-m_XIT,toneSpacing,true,snr,m_TRperiod,m_signoffGeneration);
    else Q_EMIT sendMessage (NUM_FT4_SYMBOLS,576.0,ui->TxFreqSpinBox->value()-m_XIT,toneSpacing,m_soundOutput,
                        m_config.audio_output_channel(),true,snr,m_TRperiod,m_signoffGeneration);
  }
  else if (m_modeTx == "JT65") {
    toneSpacing=11025.0/4096.0;
    if (m_tci) Q_EMIT m_config.transceiver_modulator_start(NUM_JT65_SYMBOLS,4096.0*12000.0/11025.0,ui->TxFreqSpinBox->value()-m_XIT,toneSpacing,true,snr,m_TRperiod,m_signoffGeneration);
    else Q_EMIT sendMessage (NUM_JT65_SYMBOLS,4096.0*12000.0/11025.0,ui->TxFreqSpinBox->value()-m_XIT,toneSpacing,m_soundOutput,
                        m_config.audio_output_channel(),true,snr,m_TRperiod,m_signoffGeneration);
  }
  else if (m_modeTx == "JT9") {
    double sps=m_nsps; m_toneSpacing=12000.0/6912.0;
    if (m_tci) Q_EMIT m_config.transceiver_modulator_start(NUM_JT9_SYMBOLS,sps,ui->TxFreqSpinBox->value()-m_XIT, m_toneSpacing,true,snr,m_TRperiod,m_signoffGeneration);
    else Q_EMIT sendMessage (NUM_JT9_SYMBOLS,sps,ui->TxFreqSpinBox->value()-m_XIT, m_toneSpacing,m_soundOutput,
                        m_config.audio_output_channel(),true,snr,m_TRperiod,m_signoffGeneration);
  }
  else if (m_modeTx == "T10") {
    double sps=m_nsps; m_toneSpacing=4.0*12000.0/6912.0;
    if (m_tci) Q_EMIT m_config.transceiver_modulator_start(NUM_T10_SYMBOLS,sps,ui->TxFreqSpinBox->value()-m_XIT,m_toneSpacing,true,snr,m_TRperiod,m_signoffGeneration);
    else Q_EMIT sendMessage (NUM_T10_SYMBOLS,sps,ui->TxFreqSpinBox->value()-m_XIT,m_toneSpacing,m_soundOutput,
                        m_config.audio_output_channel(),true,snr,m_TRperiod,m_signoffGeneration);
  }
  else if (m_mode=="WSPR-2") {
    if (m_tci) Q_EMIT m_config.transceiver_modulator_start(NUM_WSPR_SYMBOLS,8192.0,ui->TxFreqSpinBox->value()-1.5*12000/8192,m_toneSpacing,true,snr,m_TRperiod,m_signoffGeneration);
    else Q_EMIT sendMessage (NUM_WSPR_SYMBOLS,8192.0,ui->TxFreqSpinBox->value()-1.5*12000/8192,m_toneSpacing,m_soundOutput,
                        m_config.audio_output_channel(),true,snr,m_TRperiod,m_signoffGeneration);
  }
  if(m_nlasttx==0 && !m_tune) m_nlasttx=m_ntx;
}

void MainWindow::on_outAttenuation_valueChanged (int a)
{
  QString tt_str; int areversed=450-a;
  qreal dBAttn {areversed / 10.};       // slider interpreted as dB / 100
  QString curBand;
  if (m_tune && m_config.pwrBandTuneMemory()) { tt_str = tr ("Tune digital gain"); }
  else { tt_str = tr ("Transmit digital gain"); }
  tt_str += (areversed ? QString::number (-dBAttn, 'f', 1) : " 0") + "dB";
  if (!m_block_pwr_tooltip) QToolTip::showText (QCursor::pos (), tt_str, ui->outAttenuation);
  if (m_mode == "JT9+JT65" && m_modeTx == "JT65") { curBand = ui->bandComboBox->currentText()+m_modeTx; }
  else { curBand = ui->bandComboBox->currentText()+m_mode; }
  if (m_PwrBandSetOK && !m_tune && m_config.pwrBandTxMemory ()) {
      m_pwrBandTxMemory[curBand] = a; // remember our Tx pwr
      qDebug () << "Tx=" << QString::number(a);
  }
  if (m_PwrBandSetOK && m_tune && m_config.pwrBandTuneMemory()) {
      m_pwrBandTuneMemory[curBand] = a; // remember our Tune pwr
      qDebug () << "Tune=" << QString::number(a);
  }
  // Updating attenuation for tuning is done in stop_tuning
  if (m_tci) Q_EMIT m_config.transceiver_txvolume(dBAttn);
  else Q_EMIT outAttenuationChanged (dBAttn);
}

void MainWindow::on_actionShort_list_of_add_on_prefixes_and_suffixes_triggered()
{
  if (!m_prefixes) m_prefixes.reset (new HelpTextWindow {tr ("Prefixes"), R"(Short-list of Add-On DXCC Prefixes:

 1A    1S    3A    3B6   3B8   3B9   3C    3C0   3D2   3D2C  3D2R  3DA   3V    3W    3X   
 3Y    3YB   3YP   4J    4L    4S    4U1I  4U1U  4W    4X    5A    5B    5H    5N    5R   
 5T    5U    5V    5W    5X    5Z    6W    6Y    7O    7P    7Q    7X    8P    8Q    8R   
 9A    9G    9H    9J    9K    9L    9M2   9M6   9N    9Q    9U    9V    9X    9Y    A2   
 A3    A4    A5    A6    A7    A9    AP    BS7   BV    BV9   BY    C2    C3    C5    C6   
 C9    CE    CE0X  CE0Y  CE0Z  CE9   CM    CN    CP    CT    CT3   CU    CX    CY0   CY9  
 D2    D4    D6    DL    DU    E3    E4    EA    EA6   EA8   EA9   EI    EK    EL    EP   
 ER    ES    ET    EU    EX    EY    EZ    F     FG    FH    FJ    FK    FKC   FM    FO   
 FOA   FOC   FOM   FP    FR    FRG   FRJ   FRT   FT5W  FT5X  FT5Z  FW    FY    M     MD   
 MI    MJ    MM    MU    MW    H4    H40   HA    HB    HB0   HC    HC8   HH    HI    HK   
 HK0A  HK0M  HL    HM    HP    HR    HS    HV    HZ    I     IS    IS0   J2    J3    J5   
 J6    J7    J8    JA    JDM   JDO   JT    JW    JX    JY    K     KG4   KH0   KH1   KH2  
 KH3   KH4   KH5   KH5K  KH6   KH7   KH8   KH9   KL    KP1   KP2   KP4   KP5   LA    LU   
 LX    LY    LZ    OA    OD    OE    OH    OH0   OJ0   OK    OM    ON    OX    OY    OZ   
 P2    P4    PA    PJ2   PJ7   PY    PY0F  PT0S  PY0T  PZ    R1F   R1M   S0    S2    S5   
 S7    S9    SM    SP    ST    SU    SV    SVA   SV5   SV9   T2    T30   T31   T32   T33  
 T5    T7    T8    T9    TA    TF    TG    TI    TI9   TJ    TK    TL    TN    TR    TT   
 TU    TY    TZ    UA    UA2   UA9   UK    UN    UR    V2    V3    V4    V5    V6    V7   
 V8    VE    VK    VK0H  VK0M  VK9C  VK9L  VK9M  VK9N  VK9W  VK9X  VP2E  VP2M  VP2V  VP5  
 VP6   VP6D  VP8   VP8G  VP8H  VP8O  VP8S  VP9   VQ9   VR    VU    VU4   VU7   XE    XF4  
 XT    XU    XW    XX9   XZ    YA    YB    YI    YJ    YK    YL    YN    YO    YS    YU   
 YV    YV0   Z2    Z3    ZA    ZB    ZC4   ZD7   ZD8   ZD9   ZF    ZK1N  ZK1S  ZK2   ZK3
 ZL    ZL7   ZL8   ZL9   ZP    ZS    ZS8   KC4   E5

Short-list of Add-on Suffixes:    /0 /1 /2 /3 /4 /5 /6 /7 /8 /9 /A /P
)", {"Courier", 10}});
  m_prefixes->showNormal();
  m_prefixes->raise ();
}

bool MainWindow::shortList(QString callsign)
{
  int n=callsign.length();
  int i1=callsign.indexOf("/");
  Q_ASSERT(i1>0 and i1<n);
  QString t1=callsign.left(i1);
  QString t2=callsign.mid(i1+1,n-i1-1);
  bool b=(m_pfx.contains(t1) or m_sfx.contains(t2));
  return b;
}

bool MainWindow::isAutoSeq73(QString const& text)
{
  QStringList parts = text.split (' ', SkipEmptyParts);
  auto partsSize = parts.size ();
  bool b=((partsSize > 0 && (parts[0] == "73" || parts[0] == "TNX" || parts[0] == "TKS" || parts[0] == "TU"))
       || (partsSize > 1 && (parts[1] == "73" || parts[1] == "TNX" || parts[1] == "TKS" || parts[1] == "TU"))
       || (partsSize > 2 && (parts[2] == "73" || parts[2] == "TNX" || parts[2] == "TKS" || parts[2] == "TU"))
       || (partsSize > 3 && (parts[3] == "73" || parts[3] == "TNX" || parts[3] == "TKS" || parts[3] == "TU")));
  return b;
}

void MainWindow::enableHoundAccess(bool b)
{
  if(b) { ui->actionEnable_hound_mode->setEnabled(true); ui->HoundButton->setEnabled(true); }
  else { ui->actionEnable_hound_mode->setChecked(false); ui->actionEnable_hound_mode->setEnabled(false); ui->HoundButton->setEnabled(false);
         ui->actionUse_TX_frequency_jumps->setChecked(false);
  }
}

void MainWindow::setHoundAppearance(bool hound)
{
  bool b = !hound;
  ui->tx2->setEnabled(b); ui->txb2->setEnabled(b);
  ui->tx4->setEnabled(b); ui->txb4->setEnabled(b);
  ui->tx5->setEnabled(b); ui->txb5->setEnabled(b);
  ui->tx6->setEnabled(b); ui->txb6->setEnabled(b);
  ui->pbCallCQ->setEnabled(b); ui->pbAnswerCaller->setEnabled(b);
  ui->pbSendRRR->setEnabled(b); ui->pbSend73->setEnabled(b);
  ui->freeTextMsg->setEnabled(b); ui->rbFreeText->setEnabled(b);
}

void MainWindow::setLastLogdLabel()
{
  if(m_lastloggedcall.isEmpty()) { lastlogged_label->setText(tr("Logd ")); lastlogged_label->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#ffffff",m_useDarkStyle))); }
  else { lastlogged_label->setText(tr("Logd ") + m_lastloggedcall); lastlogged_label->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#7fff7f",m_useDarkStyle))); }
}

void MainWindow::writeTxTiming(QString const& text)
{
  // Diagnostics must never hold up CAT/audio or display a modal file error.
  // A separate, serial writer avoids racing the ordinary ALL.TXT writer.
  auto pending=m_txTimingPending;
  if(pending->fetch_add(1)>=64) {pending->fetch_sub(1);return;}
  auto utc=m_jtdxtime->currentDateTimeUtc2();
  auto path=m_config.log_directory().absoluteFilePath(utc.toString("yyyyMM_")+"TX_TIMING.TXT");
  auto record=(utc.toString("yyyyMMdd_hhmmss.zzz")+"  "+text+'\n').toUtf8();
  QtConcurrent::run(&m_txTimingPool,[path,record,pending] {
    QFile file(path);
    if(file.open(QIODevice::WriteOnly|QIODevice::Append))file.write(record);
    pending->fetch_sub(1);
  });
}

void MainWindow::writeToALLTXT(QString const& text)
{
  QFile f {m_config.log_directory().absoluteFilePath (m_jtdxtime->currentDateTimeUtc2().toString("yyyyMM_")+"ALL.TXT")};
  if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
     QTextStream out(&f);
     out << m_jtdxtime->currentDateTimeUtc2().toString("yyyyMMdd_hhmmss.zzz")  << "(" << m_jtdxtime->GetOffset() << ")" << "  " << text <<
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                 endl;
#else
                 Qt::endl;
#endif

     if(text.endsWith("count reached")) out << "Counters: "
       << "answerCQ" << (m_config.answerCQCount() ? "-On value=" : "-Off value=") << m_config.nAnswerCQCounter()
       << "; answerInCall" << (m_config.answerInCallCount() ? "-On value=" : "-Off value=") << m_config.nAnswerInCallCounter()
       << "; sentRReport" << (m_config.sentRReportCount() ? "-On value=" : "-Off value=") << m_config.nSentRReportCounter()
       << "; sentRR7373" << (m_config.sentRR7373Count() ? "-On value=" : "-Off value=") << m_config.nSentRR7373Counter()
       <<
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                 endl;
#else
                 Qt::endl;
#endif

     f.close();
  } else {
     JTDXMessageBox::warning_message (this, "", tr ("File Open Error")
                                  , tr ("Cannot open \"%1\" for append: %2")
                                  .arg (f.fileName ()).arg (f.errorString ()));
  }
}

void MainWindow::setTxMsgBtnColor()
{
  if(0 == txTabIndex()) {
    if(m_ntx==1) ui->txb1->setStyleSheet(QString("QPushButton{background: %1}").arg(Radio::convert_dark("#88ff88",m_useDarkStyle)));
    else if(m_ntx==2) ui->txb2->setStyleSheet(QString("QPushButton{background: %1}").arg(Radio::convert_dark("#88ff88",m_useDarkStyle)));
    else if(m_ntx==3) ui->txb3->setStyleSheet(QString("QPushButton{background: %1}").arg(Radio::convert_dark("#88ff88",m_useDarkStyle)));
    else if(m_ntx==4) ui->txb4->setStyleSheet(QString("QPushButton{background: %1}").arg(Radio::convert_dark("#88ff88",m_useDarkStyle)));
    else if(m_ntx==5) ui->txb5->setStyleSheet(QString("QPushButton{background: %1}").arg(Radio::convert_dark("#88ff88",m_useDarkStyle)));
    else if(m_ntx==6) ui->txb6->setStyleSheet(QString("QPushButton{background: %1}").arg(Radio::convert_dark("#88ff88",m_useDarkStyle)));
  }
  else if(1 == txTabIndex()) {
    if(m_QSOProgress == CALLING) ui->pbCallCQ->setStyleSheet(QString("QPushButton{background: %1}").arg(Radio::convert_dark("#88ff88",m_useDarkStyle)));
    else if(m_QSOProgress == REPLYING) ui->pbAnswerCQ->setStyleSheet(QString("QPushButton{background: %1}").arg(Radio::convert_dark("#88ff88",m_useDarkStyle)));
    else if(m_QSOProgress == REPORT) ui->pbAnswerCaller->setStyleSheet(QString("QPushButton{background: %1}").arg(Radio::convert_dark("#88ff88",m_useDarkStyle)));
    else if(m_QSOProgress == ROGER_REPORT) ui->pbSendReport->setStyleSheet(QString("QPushButton{background: %1}").arg(Radio::convert_dark("#88ff88",m_useDarkStyle)));
    else if(m_QSOProgress == ROGERS) ui->pbSendRRR->setStyleSheet(QString("QPushButton{background: %1}").arg(Radio::convert_dark("#88ff88",m_useDarkStyle)));
    else if(m_QSOProgress == SIGNOFF) ui->pbSend73->setStyleSheet(QString("QPushButton{background: %1}").arg(Radio::convert_dark("#88ff88",m_useDarkStyle)));
  }
}

void MainWindow::resetTxMsgBtnColor()
{
  if(0 == txTabIndex()) {
    ui->txb1->setStyleSheet(""); ui->txb2->setStyleSheet(""); ui->txb3->setStyleSheet("");
    ui->txb4->setStyleSheet(""); ui->txb5->setStyleSheet(""); ui->txb6->setStyleSheet("");
  }
  else if(1 == txTabIndex()) {
    ui->pbCallCQ->setStyleSheet(""); ui->pbAnswerCQ->setStyleSheet(""); ui->pbAnswerCaller->setStyleSheet("");
    ui->pbSendReport->setStyleSheet(""); ui->pbSendRRR->setStyleSheet(""); ui->pbSend73->setStyleSheet("");
  }
}

void MainWindow::pskSetLocal ()
{
  // find the station row, if any, that matches the band we are on
  auto stations = m_config.stations ();
  auto matches = stations->match (stations->index (0, StationList::band_column)
                                  , Qt::DisplayRole
                                  , ui->bandComboBox->currentText ()
                                  , 1
                                  , Qt::MatchExactly);
  QString antenna_description;
  if (!matches.isEmpty ()) antenna_description = stations->index (matches.first ().row (), StationList::description_column).data ().toString ();
  // qDebug() << "To PSKreporter: local station details";
  psk_Reporter->setLocalStation(m_config.my_callsign (), m_config.my_grid (), antenna_description, QString {"JTDX v" + version() + (m_tci ? " tci " : " ") + revision()}.simplified ());
}

void MainWindow::transmitDisplay (bool transmitting)
{
  updateTxButtonAppearance();
  if (transmitting == m_transmitting) {
    if (transmitting) {
      ui->signal_meter_widget->setValue(0);
      if (m_monitoring && !m_config.full_duplex()) monitor (false);
      m_btxok=true;
    }
    auto QSY_allowed = !transmitting or m_config.tx_QSY_allowed () or !m_config.split_mode ();
    if (ui->pbTxLock->isChecked ()) {
      ui->RxFreqSpinBox->setEnabled (QSY_allowed);
      ui->pbT2R->setEnabled (QSY_allowed);
    }
    if(m_mode!="WSPR") {
      ui->TxFreqSpinBox->setEnabled (QSY_allowed && m_mode!="MSK144" && m_mode!="Echo");
      ui->pbR2T->setEnabled (QSY_allowed);
      ui->pbTxLock->setEnabled (QSY_allowed);
    }
    // the following are always disallowed in transmit
    ui->menuMode->setEnabled (!transmitting);
    if (!transmitting) {
      // allow mode switch in Rx when in dual mode
      if ("JT9+JT65" == m_mode) ui->pbTxMode->setEnabled (true);
    } else {
      ui->pbTxMode->setEnabled (false);
    }
  }
}

void MainWindow::networkError (QString const& e)
{
  if (JTDXMessageBox::Retry == JTDXMessageBox::warning_message (this, "", tr ("Network Error")
                                                  , tr ("Error: %1\nUDP server %2:%3")
                                                  .arg (e)
                                                  .arg (m_config.udp_server_name ())
                                                  .arg (m_config.udp_server_port ())
                                                  , "", JTDXMessageBox::Cancel | JTDXMessageBox::Retry, JTDXMessageBox::Cancel))
    {
      // retry server lookup
      m_messageClient->set_server (m_config.udp_server_name ());
    }
}

void MainWindow::p1ReadFromStdout()                        //p1readFromStdout
{
  while(p1.canReadLine()) {
    QString t = QString(p1.readLine());
    if(ui->cbNoOwnCall->isChecked()) {
      if(t.contains(" " + m_config.my_callsign() + " ")) continue;
      if(t.contains(" <" + m_config.my_callsign() + "> ")) continue;
    }
    QString t1;
    if(t.indexOf("<DecodeFinished>") >= 0) {
      m_bDecoded = m_nWSPRdecodes > 0;
      if(!m_diskData) {
        WSPR_history(m_dialFreqRxWSPR, m_nWSPRdecodes);
        if(m_nWSPRdecodes==0 and ui->band_hopping_group_box->isChecked()) {
          t = " Receiving " + m_mode + " ----------------------- " +
              m_config.bands ()->find (m_dialFreqRxWSPR);
          t=WSPR_hhmm(-60) + ' ' + t.rightJustified (66, '-');
          ui->decodedTextBrowser->appendText(t,Radio::convert_dark("#ffffff",m_useDarkStyle),Radio::convert_dark("#000000",m_useDarkStyle),0," ",Radio::convert_dark("#000000",m_useDarkStyle));
        }
        killFileTimer.start (int(750.0*m_TRperiod)); //Kill 3/4 period from now
      }
      m_nWSPRdecodes=0;
      ui->DecodeButton->setChecked (false);
      if(m_uploadSpots
         && m_config.is_transceiver_online ()) { // need working rig control
#if QT_VERSION >= QT_VERSION_CHECK (5, 15, 0)
        uploadTimer.start(QRandomGenerator::global ()->bounded (0, 20000)); // Upload delay
#else
        uploadTimer.start(20000 * qrand()/((double)RAND_MAX + 1.0)); // Upload delay
#endif
      } else {
        QFile f(QDir::toNativeSeparators(m_dataDir.absolutePath()) + "/wspr_spots.txt");
        if(f.exists()) f.remove();
      }
      m_RxLog=0;
      m_startAnother=m_loopall;
      m_blankLine=true;
      m_decoderBusy = false;
      statusUpdate ();
    } else {

      int n=t.length();
      t=t.left(n-2) + "                                                  ";
      t.remove(QRegularExpression("\\s+$"));
      QStringList rxFields = t.split(QRegularExpression("\\s+"));
      if(messageBlacklisted(t))continue;
      QString rxLine;
      QString grid="";
      if ( rxFields.count() == 8 ) {
          rxLine = QString("%1 %2 %3 %4 %5   %6  %7  %8")
                  .arg(rxFields.at(0), 4)
                  .arg(rxFields.at(1), 4)
                  .arg(rxFields.at(2), 5)
                  .arg(rxFields.at(3), 11)
                  .arg(rxFields.at(4), 4)
                  .arg(rxFields.at(5).leftJustified (12))
                  .arg(rxFields.at(6), -6)
                  .arg(rxFields.at(7), 3);
          postWSPRDecode (true, rxFields);
          grid = rxFields.at(6);
      } else if ( rxFields.count() == 7 ) { // Type 2 message
          rxLine = QString("%1 %2 %3 %4 %5   %6  %7  %8")
                  .arg(rxFields.at(0), 4)
                  .arg(rxFields.at(1), 4)
                  .arg(rxFields.at(2), 5)
                  .arg(rxFields.at(3), 11)
                  .arg(rxFields.at(4), 4)
                  .arg(rxFields.at(5).leftJustified (12))
                  .arg("", -6)
                  .arg(rxFields.at(6), 3);
          postWSPRDecode (true, rxFields);
      } else {
          rxLine = t;
      }
      if(!grid.isEmpty ()) {
        double utch=0.0;
        int nAz=0,nEl=0,nDmiles=0,nDkm=0,nHotAz,nHotABetter;
        if (!grid.isEmpty() && !m_config.my_grid ().isEmpty())
          azdist_(const_cast <char *> ((m_config.my_grid () + "        ").left (8).toLatin1().constData()),
                const_cast <char *> ((grid + "        ").left (8).toLatin1().constData()),&utch,
                &nAz,&nEl,&nDmiles,&nDkm,&nHotAz,&nHotABetter,8,8);
        if(m_config.miles()) {
          t1 = QString::asprintf("%7d",nDmiles);
        } else {
          t1 = QString::asprintf("%7d",nDkm);
        }
        rxLine += t1;
      }

      if (m_config.insert_blank () && m_blankLine) {
        QString band;
        Frequency f=1000000.0*rxFields.at(3).toDouble()+0.5;
        band = ' ' + m_config.bands ()->find (f);
        ui->decodedTextBrowser->appendText(band.rightJustified (71, '-'),Radio::convert_dark("#ffffff",m_useDarkStyle),Radio::convert_dark("#000000",m_useDarkStyle),0," ",Radio::convert_dark("#000000",m_useDarkStyle));
        m_blankLine = false;
      }
      m_nWSPRdecodes += 1;
      if(!m_baseCall.isEmpty() && Radio::base_callsign(rxFields.value(5))==m_baseCall)
        ui->decodedTextBrowser->displayOwnText(rxLine);
      else ui->decodedTextBrowser->appendText(rxLine,Radio::convert_dark("#ffffff",m_useDarkStyle),Radio::convert_dark("#000000",m_useDarkStyle),0," ",Radio::convert_dark("#000000",m_useDarkStyle));
    }
  }
}

QString MainWindow::WSPR_hhmm(int n)
{
  QDateTime t=m_jtdxtime->currentDateTimeUtc2().addSecs(n);
  int m=t.toString("hhmm").toInt()/2;
  QString t1;
  t1 = QString::asprintf("%04d",2*m);
  return t1;
}

void MainWindow::WSPR_history(Frequency dialFreq, int ndecodes)
{
  QDateTime t=m_jtdxtime->currentDateTimeUtc2().addSecs(-60);
  QString t1=t.toString("yyMMdd");
  QString t2=WSPR_hhmm(-60);
  QString t3;
  t3 = QString::asprintf("%13.6f",0.000001*dialFreq);
  if(ndecodes<0) {
    t1=t1 + " " + t2 + t3 + "  T";
  } else {
    QString t4;
    t4 = QString::asprintf("%4d",ndecodes);
    t1=t1 + " " + t2 + t3 + "  R" + t4;
  }
  QFile f {m_config.log_directory().absoluteFilePath ("WSPR_history.txt")};
  if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
    QTextStream out(&f);
    out << t1 <<
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                 endl;
#else
                 Qt::endl;
#endif

    f.close();
  } else {
    JTDXMessageBox::warning_message (this, "", tr ("File Error")
                                 , tr ("Cannot open \"%1\" for append: %2")
                                 .arg (f.fileName ()).arg (f.errorString ()));
  }
}

void MainWindow::uploadSpots()
{
  // do not spot replays or if rig control not working
  if(m_diskData || !m_config.is_transceiver_online ()) return;
  if(m_uploading) {
    qDebug() << "Previous upload has not completed, spots were lost";
    wsprNet->abortOutstandingRequests ();
    m_uploading = false;
  }
  QString rfreq = QString("%1").arg(0.000001*(m_dialFreqRxWSPR + 1500), 0, 'f', 6);
  QString tfreq = QString("%1").arg(0.000001*(m_dialFreqRxWSPR +
                        ui->TxFreqSpinBox->value()), 0, 'f', 6);
  wsprNet->upload(m_config.my_callsign(), m_config.my_grid(), rfreq, tfreq,
                  m_mode, QString::number(ui->enableTxButton->isChecked() ? m_pctx : 0),
                  QString::number(m_dBm), version(),
                  QDir::toNativeSeparators(m_dataDir.absolutePath()) + "/wspr_spots.txt");
  m_uploading = true;
}

void MainWindow::uploadResponse(QString response)
{
  if (response == "done") {
    m_uploading=false;
  } else {
    if (response.startsWith ("Upload Failed")) m_uploading=false;
    qDebug () << "WSPRnet.org status:" << response;
  }
}

void MainWindow::on_TxPowerComboBox_currentIndexChanged(const QString &arg1)
{
  int i1=arg1.indexOf(" ");
  m_dBm=arg1.left(i1).toInt();
}

void MainWindow::on_sbTxPercent_valueChanged(int n)
{
  m_pctx=n;
  if(m_pctx>0) {
    ui->pbTxNext->setEnabled(true);
  } else {
    m_txNext=false;
    ui->pbTxNext->setChecked(false);
    ui->pbTxNext->setEnabled(false);
  }
}

void MainWindow::on_cbUploadWSPR_Spots_toggled(bool b)
{
  m_uploadSpots=b;
  if(m_uploadSpots) ui->cbUploadWSPR_Spots->setStyleSheet("");
  if(!m_uploadSpots) ui->cbUploadWSPR_Spots->setStyleSheet(QString("QCheckBox{background: %1}").arg(Radio::convert_dark("#ffff00",m_useDarkStyle)));
}

void MainWindow::on_WSPRfreqSpinBox_valueChanged(int n) { ui->TxFreqSpinBox->setValue(n); }
void MainWindow::on_pbTxNext_clicked(bool b) { m_txNext=b; }

void MainWindow::WSPR_scheduling ()
{
  m_WSPR_tx_next = false;
  if (m_config.is_transceiver_online () // need working rig control for hopping
      && !m_config.is_dummy_rig ()
      && ui->band_hopping_group_box->isChecked ()) {
    auto hop_data = m_WSPR_band_hopping.next_hop (m_enableTx);
    qDebug () << "hop data: period:" << hop_data.period_name_
              << "frequencies index:" << hop_data.frequencies_index_
              << "tune:" << hop_data.tune_required_
              << "tx:" << hop_data.tx_next_;
    m_WSPR_tx_next = hop_data.tx_next_;
    if (hop_data.frequencies_index_ >= 0) { // new band
      ui->bandComboBox->setCurrentIndex (hop_data.frequencies_index_);
      on_bandComboBox_activated (hop_data.frequencies_index_);
      m_cmnd.clear ();
      QStringList prefixes {".bat", ".cmd", ".exe", ""};
      for (auto const& prefix : prefixes)
        {
          auto const& path = m_appDir + "/user_hardware" + prefix;
          QFile f {path};
          if (f.exists ()) {
            m_cmnd = QDir::toNativeSeparators (f.fileName ());
          }
        }
      if(!m_cmnd.isEmpty ()) p3.start(m_cmnd, {m_config.bands ()->find (m_freqNominal).remove ('m')});

      // Produce a short tuneup signal
      m_tuneup = false;
      if (hop_data.tune_required_) {
        m_tuneup = true;
        on_tuneButton_clicked (true);
        tuneATU_Timer.start (2500);
      }
    }
  }
  else {
    m_WSPR_tx_next = m_WSPR_band_hopping.next_is_tx ();
  }
}

void MainWindow::setRig ()
{
  if(m_transmitting && !m_config.tx_QSY_allowed ()) return;
  if ((m_monitoring || m_transmitting) && m_config.transceiver_online ()) {
      if(m_transmitting && m_config.split_mode ()) { Q_EMIT m_config.transceiver_tx_frequency (correctedFrequency(m_freqTxNominal,m_astroCorrection.tx)); }
      else { m_upstreamState.expectReceiveFrequency(m_freqNominal,m_astroCorrection.rx);Q_EMIT m_config.transceiver_frequency (correctedFrequency(m_freqNominal,m_astroCorrection.rx)); }
      Q_EMIT m_config.transceiver_ft4_mode (m_mode == "FT4");
  }
}

void MainWindow::on_the_minute ()
{
  if(minuteTimer.isSingleShot ()) { minuteTimer.setSingleShot (false); minuteTimer.start (60 * 1000); } // run free
  else {
    auto const& ms_error = ms_minute_error (m_jtdxtime);
    // keep drift within +-1s
    if (qAbs (ms_error) > 1000) { minuteTimer.setSingleShot (true); minuteTimer.start (ms_error + 60 * 1000); }
    }
  update_watchdog_label();
  //3...4 minutes to stop AP decoding
  if(!m_transmitting && m_mode=="FT8" && (m_jtdxtime->currentMSecsSinceEpoch2()-m_mslastTX) > 120000) m_lapmyc=0;
}

void MainWindow::toggle_skipTx1 ()
{
  if(m_bMyCallStd) {
    if(!ui->skipTx1->isEnabled()) { if(!m_houndMode) ui->skipTx1->setEnabled(true); ui->skipGrid->setEnabled(true); }
  } else {
    if(m_skipTx1) { m_skipTx1=false; ui->skipTx1->setChecked(false); ui->skipGrid->setChecked(false); }
    ui->skipTx1->setEnabled(false); ui->skipGrid->setEnabled(false);
  }
}

void MainWindow::statusUpdate () const
{
  if (!ui) return;
  QChar submode {0};
  m_messageClient->status_update (m_freqNominal, m_mode, m_hisCall,
                                  QString::number (ui->rptSpinBox->value ()),
                                  m_modeTx, ui->enableTxButton->isChecked (),
                                  m_transmitting, m_decoderBusy,
                                  ui->RxFreqSpinBox->value (), ui->TxFreqSpinBox->value (),
                                  m_config.my_callsign (), m_config.my_grid (),
                                  m_hisGrid, m_txwatchdog, submode != QChar::Null ? QString {submode} : QString {},
                                  false, m_txFirst, false);
}

void MainWindow::childEvent (QChildEvent * e)
{
  if (e->child ()->isWidgetType ())
    {
      switch (e->type ())
        {
        case QEvent::ChildAdded: add_child_to_event_filter (e->child ()); break;
        case QEvent::ChildRemoved: remove_child_from_event_filter (e->child ()); break;
        default: break;
        }
    }
  QMainWindow::childEvent (e);
}

// add widget and any child widgets to our event filter so that we can
// take action on key press and mouse press events anywhere in the main window
void MainWindow::add_child_to_event_filter (QObject * target)
{
  if (target && target->isWidgetType ())
    {
      target->installEventFilter (this);
    }
  if (!target) return;
  auto const& children = target->children ();
  for (auto iter = children.begin (); iter != children.end (); ++iter)
    {
      add_child_to_event_filter (*iter);
    }
}

// recursively remove widget and any child widgets from our event filter
void MainWindow::remove_child_from_event_filter (QObject * target)
{
  if (!target) return;
  auto const& children = target->children ();
  for (auto iter = children.begin (); iter != children.end (); ++iter)
    {
      remove_child_from_event_filter (*iter);
    }
  if (target && target->isWidgetType ())
    {
      target->removeEventFilter (this);
    }
}


void MainWindow::txwatchdog (bool triggered)
{
  // Legacy activity callbacks never restart a session or clear an expiry.
  if (!triggered) { update_watchdog_label(); return; }
  if (m_txwatchdog) return;
  if(m_mode=="JTTY") {
    if(m_transmitting){m_watchdogFinishing=true;update_watchdog_label();return;}
    finishTxWatchdog();return;
  }
  QString grid;auto state=m_qsoHistory.status(m_hisCall,grid);
  bool finished=!m_signoff.active() && (state==QsoHistory::FIN || (m_houndMode && state==QsoHistory::RRR73));
  if(!m_watchdogFinishing && m_enableTx && !m_tune && !m_hisCall.isEmpty() &&
     state>=QsoHistory::RCALL && (!finished || m_transmitting || g_iptt!=0)) {
    m_watchdogFinishing=true;m_watchdogCall=m_hisCall;
    m_watchdogContext=m_config.my_callsign()+"|"+QString::number(m_freqNominal)+"|"+m_mode;
    statusBar()->showMessage(tr("TX watchdog expired. Finishing the current QSO before stopping."),15000);
    statusUpdate();
  }
  if(m_watchdogFinishing && m_hisCall==m_watchdogCall &&
     m_watchdogContext==m_config.my_callsign()+"|"+QString::number(m_freqNominal)+"|"+m_mode &&
     (!finished || m_transmitting || g_iptt!=0)) {update_watchdog_label();return;}
  finishTxWatchdog();
}

void MainWindow::finishTxWatchdog()
{
  m_txwatchdog = true;
  TxAgainTimer.stop();
  enableTxButtonTimer.stop();
  m_tuneup = false;
  haltTx("TX session watchdog expired ");
  m_txSession.stop();
  tx_status_label->setStyleSheet(QString("QLabel{background: %1}").arg(Radio::convert_dark("#ff8080",m_useDarkStyle)));
  tx_status_label->setText(tr("Tx watchdog expired"));
  update_watchdog_label();
  statusUpdate();
}

void MainWindow::update_watchdog_label ()
{
  if (m_mode.startsWith("WSPR")) { txwatchdog_label->hide(); return; }
  txwatchdog_label->setToolTip(m_watchdogFinishing
    ? tr("TX watchdog expired. Finishing the current QSO before stopping.")
    : QCoreApplication::translate("configuration_dialog","<html><head/><body><p>Minutes from enabling TX, across QSOs; 0 = unlimited, maximum 999. Activity does not reset the timer.</p></body></html>"));
  if (m_watchdogFinishing)txwatchdog_label->setText(tr("WD finishing QSO"));
  else if (!m_config.watchdog()) txwatchdog_label->setText(tr("WD unlimited"));
  else {
    int seconds = m_txwatchdog ? 0 : m_txSession.remainingSeconds(m_config.watchdog());
    txwatchdog_label->setText(tr("WD %1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0')));
  }
  txwatchdog_label->setMinimumWidth(qMax(52, txwatchdog_label->sizeHint().width()));
  // QStatusBar temporarily hides its normal widgets while showing a message.
  // A timer must not show this label over the managed-mode explanation.
  txwatchdog_label->setVisible(statusBar()->currentMessage().isEmpty());
}

void MainWindow::on_cbMenus_toggled(bool b)
{
  m_menus=b;
  hideMenus(!b);
  minimumSize().setHeight(422); 
  minimumSize().setWidth(733);
  dynamicButtonsInit();
}

void MainWindow::on_cbShowWanted_toggled(bool b)
{
  m_wantedchkd=b;
  ui->labWantCall->setVisible(b); ui->wantedCall->setVisible(b); ui->labWantCountry->setVisible(b); ui->wantedCountry->setVisible(b);
  ui->labWantPfx->setVisible(b); ui->wantedPrefix->setVisible(b); ui->labWantGrid->setVisible(b); ui->wantedGrid->setVisible(b);
  ui->cbClearCallsign->setVisible(b); ui->cbClearGrid->setVisible(b); dynamicButtonsInit();
}

void MainWindow::on_cbShowSpot_toggled(bool b) 
{
  ui->spotMsgLabel->setVisible(b); ui->spotEditLabel->setVisible(b);  ui->spotLineEdit->setVisible(b); ui->propEditLabel->setVisible(b); ui->propLineEdit->setVisible(b);
}

void MainWindow::dynamicButtonsInit()
{
  QSize size=this->size();
  int height=size.height();
  if(m_menus) {
    if(!m_wantedchkd) {
      if(height <= 435) { ui->bypassButton->hide(); ui->singleQSOButton->hide(); ui->AnsB4Button->hide(); ui->stopButton->hide(); }
      else if(height > 435 && height <= 475) { ui->bypassButton->show(); ui->singleQSOButton->hide(); ui->AnsB4Button->hide(); ui->stopButton->hide(); }
      else if(height > 475 && height <= 500) { ui->bypassButton->show(); ui->singleQSOButton->show(); ui->AnsB4Button->hide(); ui->stopButton->hide(); }
      else if(height > 500 && height <= 525) { ui->bypassButton->show(); ui->singleQSOButton->show(); ui->AnsB4Button->show(); ui->stopButton->hide(); }
      else if(height > 525) { ui->bypassButton->show(); ui->singleQSOButton->show(); ui->AnsB4Button->show(); ui->stopButton->show(); }
    } else {
      if(height <= 415) { ui->bypassButton->hide(); ui->singleQSOButton->hide(); ui->AnsB4Button->hide(); ui->stopButton->hide(); }
      else if(height > 415 && height <= 450) { ui->bypassButton->show(); ui->singleQSOButton->hide(); ui->AnsB4Button->hide(); ui->stopButton->hide(); }
      else if(height > 450 && height <= 485) { ui->bypassButton->show(); ui->singleQSOButton->show(); ui->AnsB4Button->hide(); ui->stopButton->hide(); }
      else if(height > 485 && height <= 520) { ui->bypassButton->show(); ui->singleQSOButton->show(); ui->AnsB4Button->show(); ui->stopButton->hide(); }
      else if(height > 520) { ui->bypassButton->show(); ui->singleQSOButton->show(); ui->AnsB4Button->show(); ui->stopButton->show(); }
    }
  } else {
    if(height <= 425) { ui->bypassButton->hide(); ui->singleQSOButton->hide(); ui->AnsB4Button->hide(); ui->stopButton->hide(); }
    else if(height > 425 && height <= 450) { ui->bypassButton->show(); ui->singleQSOButton->hide(); ui->AnsB4Button->hide(); ui->stopButton->hide(); }
    else if(height > 450 && height <= 475) { ui->bypassButton->show(); ui->singleQSOButton->show(); ui->AnsB4Button->hide(); ui->stopButton->hide(); }
    else if(height > 475 && height <= 500) { ui->bypassButton->show(); ui->singleQSOButton->show(); ui->AnsB4Button->show(); ui->stopButton->hide(); }
    else if(height > 500) { ui->bypassButton->show(); ui->singleQSOButton->show(); ui->AnsB4Button->show(); ui->stopButton->show(); }
  }
}
