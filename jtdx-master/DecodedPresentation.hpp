#ifndef JTDXF_DECODED_PRESENTATION_HPP
#define JTDXF_DECODED_PRESENTATION_HPP

#include <QDateTime>
#include <QHash>
#include <QPair>
#include <QString>
#include <QVector>

namespace DecodedPresentation {
bool gridCoordinates(QString const& grid, double& latitude, double& longitude);
QString distanceAzimuth(QString const& ownGrid, QString const& receivedGrid,
                        bool distance, bool azimuth, bool miles);
QString appendage(int messageColumns, QString const& country, QString const& state,
                  QString const& geometry, bool align, int firstSpacing, int secondSpacing);
QString periodSeparator(QDateTime const& start, QString const& band, QString const& mode,
                        bool detailed, bool fromFile = false);
QVector<QPair<int,int>> tokenRanges(QString const& text, QString const& token, bool grid = false);
}

// Bounds-checked reader for the upstream grid.dat table. A locator can cover
// several states; preserve the list instead of claiming a unique state.
class GridStateMap {
public:
  bool load(QString const& path);
  QString state(QString const& grid) const;
  int size() const { return states_.size(); }
private:
  QHash<QString,QString> states_;
};

#endif
