#ifndef JTDXF_WSJTX_GENERAL_SETTINGS_HPP
#define JTDXF_WSJTX_GENERAL_SETTINGS_HPP

#include <QVariant>

class QSettings;
class QWidget;

// Settings added while aligning the General tab with WSJT-X 3.0.2.
// Existing JTDX settings (fonts, watchdog, NTP, etc.) remain authoritative.
struct WSJTXGeneralSettings
{
  bool dxcc = true;
  bool highlightDxCall = true;
  bool highlightDxGrid = true;
  bool decodesFromTop = false;
  bool detailedBlank = true;
  bool enableVhf = false;
  bool alternateBindings = false;
  bool quickCall = true; // A physical double-click authorizes TX; saved user choices take precedence.
  bool disableTxAfter73 = false;
  bool autoAstro = false;
  bool forceCallFirst = false;
  bool repeatTx = false;
  bool khzWithoutK = false;
  bool singleDecode = false;
  bool tuneWatchdog = true;
  bool progressBarRed = true;
  bool rxToTxAfterQso = true;
  bool showDistance = false;
  bool eraseBandActivity = true;
  bool clearDxGrid = true;
  bool gridMapAll = true;
  bool gridMap = true;
  bool align = true;
  bool alternateErase = true;
  bool clearDxCall = true;
  bool waitFeatures = true;
  bool showAzimuth = false;
  bool lessButtonColoring = false;
  bool fullDuplex = false;
  bool checkSwr = false;
  int alignSteps = 2;
  int alignSteps2 = 0;

  // The caller enters Configuration; readRoot also migrates older Common keys.
  void read(QSettings&, QVariant const& legacySingleShot = QVariant(),
            QVariant const& legacyErase = QVariant(), QVariant const& legacyFullDuplex = QVariant());
  void readRoot(QSettings&);
  // Run once at startup before readRoot; later user choices remain authoritative.
  static void migrateDefaultsV361(QSettings&);
  void write(QSettings&) const;
  void initializeControls(QWidget*) const;
  void acceptControls(QWidget*);
  static void connectControls(QWidget*);
};

#endif
