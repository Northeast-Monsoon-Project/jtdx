
// -*- Mode: C++ -*-
#ifndef DISPLAYTEXT_H
#define DISPLAYTEXT_H

#include <QTextEdit>
#include "logbook/logbook.h"
#include "decodedtext.h"
#include "qsohistory.h"
#include "Radio.hpp"
#include "DecodedPresentation.hpp"

class Configuration;

class DisplayText : public QTextEdit
{
    Q_OBJECT;
public:
    explicit DisplayText(QWidget *parent = 0);
    void setConfiguration(Configuration const *, bool highVolume = false);
    void new_period();
    void insertPeriodSpacer(QDateTime const& utcStart, QString const& band, QString const& mode, bool fromFile = false);
    void setDxHighlightTarget(QString const& call, QString const& grid);
    void setMyContinent (QString const&);
    void setContentFont (QFont const&);
    void insertLineSpacer(QString const&);
    int displayDecodedText(DecodedText* decodedText, QString myCall, QString hisCall, QString hisGrid,
                           bool once_notified, LogBook logBook, QsoHistory& qsoHistory,
                           QsoHistory& qsoHistory2, double dialFreq = 0, const QString app_mode = "",
                           bool bypassRxfFilters = false, bool bypassAllFilters = false, int rx_frq = 0,
                           QStringList wantedCallList = QStringList(), QStringList wantedPrefixList = QStringList(), QStringList wantedGridList = QStringList(),
                           QStringList wantedCountryList = QStringList(), bool windowPopup = false, QWidget* window = NULL, bool recordFiltered = false);
    void displayTransmittedText(QString text, QString myCall, QString hisCall, QString skip_tx1, QString modeTx, qint32 txFreq,
                                QColor color_TxMsg, QsoHistory& qsoHistory, bool display = true);
    void displayOwnText(QString const& text, bool overwrite = false);
    void displayQSY(QString text);
    bool callSelectionInProgress() const {return callSelectionInProgress_;}
signals:
    void selectCallsign(bool alt, bool ctrl);

public slots:
  void appendText(QString const& text, QString const& bg = "#ffffff", QString const& color = "#000000", int std_type = 0, QString const& servis = " ", QString const& servis_color = "#000000", QString const& cntry = " ", bool forceBold = false, bool strikethrough = false, bool underline = false, bool DXped = false, bool overwrite = false, bool wanted = false);

protected:
    void mouseDoubleClickEvent(QMouseEvent *e);
    void resizeEvent(QResizeEvent*) override;

private:
    friend class DisplayPresentationTests;
    friend class RegressionTest;
    bool callSelectionInProgress_ = false;
    void updatePeriodMargin();
    void highlightLastMessage(int messageLength, QString const& call, QString const& grid);
    bool highVolume_ = false;
    bool decodesFromTop_ = false;
    bool detailedBlank_ = true;
    bool insertBlank_ = true;
    bool displayDxcc_ = true;
    bool highlightDxCall_ = true;
    bool highlightDxGrid_ = true;
    bool showDistance_ = false;
    bool showAzimuth_ = false;
    bool miles_ = false;
    bool gridMapEnabled_ = false;
    bool gridMapAll_ = false;
    bool alignColumns_ = true;
    int alignSteps_ = 2;
    int alignSteps2_ = 0;
    int lastDecodedPeriod_ = -1;
    bool periodAnchorPending_ = false;
    bool explicitPeriod_ = false;
    QTextCursor lastAppendedLine_;
    QString stationGrid_;
    QString highlightCall_;
    QString highlightGrid_;
    GridStateMap gridStates_;

    bool scroll_ = false;
    bool bold_ = false;
    bool wastx_ = false;
    bool useDarkStyle_ = false;
    bool displayCountryName_ = false;
    bool displayCountryPrefix_ = false;
    bool displayNewCQZ_ = false;
    bool displayNewCQZBand_ = false;
    bool displayNewCQZBandMode_ = false;
    bool displayNewITUZ_ = false;
    bool displayNewITUZBand_ = false;
    bool displayNewITUZBandMode_ = false;
    bool displayNewDXCC_ = false;
    bool displayNewDXCCBand_ = false;
    bool displayNewDXCCBandMode_ = false;
    bool displayNewGrid_ = false;
    bool displayNewGridBand_ = false;
    bool displayNewGridBandMode_ = false;
    bool displayNewPx_ = false;
    bool displayNewPxBand_ = false;
    bool displayNewPxBandMode_ = false;
    bool displayNewCall_ = false;
    bool displayNewCallBand_ = false;
    bool displayNewCallBandMode_ = false;
    bool displayPotential_ = false;
    bool displayTxtColor_ = false;
    bool displayWorkedColor_ = false;
    bool displayWorkedStriked_ = false;
    bool displayWorkedUnderlined_ = false;
    bool displayWorkedDontShow_ = false;
    bool beepOnNewCQZ_ = false;
    bool beepOnNewITUZ_ = false;
    bool beepOnNewDXCC_ = false;
    bool beepOnNewGrid_ = false;
    bool beepOnNewPx_ = false;
    bool beepOnNewCall_ = false;
    bool beepOnMyCall_ = false;
    bool RR73Marker_ = false;
    bool otherMessagesMarker_ = false;
    bool enableCountryFilter_ = false;
    bool enableCallsignFilter_ = false;
    bool enableMyConinentFilter_ = false;
    bool hidefree_ = false;
    bool showcq_ = false;
    bool showcqrrr73_ = false;
    bool showcq73_ = false;
    bool redMarker_ = false;
    bool blueMarker_ = false;
    bool hidehintMarker_ = false;
    bool hide_TX_messages_ = false;
    QString ownTransmissionBackground_ {"#7AD6CA"};
    QsoHistory::Status mystatus_ = QsoHistory::NONE;
    unsigned max_r_time = 0;
    QTextCharFormat m_charFormat;
    unsigned last_tx = 0;
    QString mygrid_ = "";
    QString myhisCall_ = "";
    QString myCall_ = "";
    QString myContinent_ = "";
    QString color_MyCall_;
    QString color_CQ_;
    QString color_StandardCall_;
    QString color_WorkedCall_;
    QString color_NewCQZ_;
    QString color_NewCQZBand_;
    QString color_NewITUZ_;
    QString color_NewITUZBand_;
    QString color_NewDXCC_;
    QString color_NewDXCCBand_;
    QString color_NewGrid_;
    QString color_NewGridBand_;
    QString color_NewPx_;
    QString color_NewPxBand_;
    QString color_NewCall_;
    QString color_NewCallBand_;
    QString hideContinents_;
    QString countries_;
    QString callsigns_;
    
    
};

#endif // DISPLAYTEXT_H
