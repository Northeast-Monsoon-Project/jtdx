#include "DecodedPresentation.hpp"

#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <cmath>

namespace {
double const pi = 3.14159265358979323846;
bool validGrid(QString const& grid) {
  static QRegularExpression const expression("\\A[A-R]{2}[0-9]{2}(?:[A-X]{2}(?:[0-9]{2}(?:[A-X]{2})?)?)?\\z");
  return grid != "RR73" && expression.match(grid).hasMatch();
}
}

bool DecodedPresentation::gridCoordinates(QString const& input, double& latitude, double& longitude)
{
  auto grid = input.trimmed().toUpper();
  if (!validGrid(grid)) return false;
  double lon = -180., lat = -90., width = 360., height = 180.;
  for (int pair = 0; pair < grid.size()/2; ++pair) {
    bool letters = pair % 2 == 0;
    int base = pair == 0 ? 18 : (letters ? 24 : 10);
    width /= base; height /= base;
    int zero = letters ? 'A' : '0';
    lon += (grid.at(pair*2).unicode() - zero) * width;
    lat += (grid.at(pair*2+1).unicode() - zero) * height;
  }
  longitude = lon + width/2.; latitude = lat + height/2.;
  return true;
}

QString DecodedPresentation::distanceAzimuth(QString const& ownGrid, QString const& receivedGrid,
                                            bool distance, bool azimuth, bool miles)
{
  if (!distance && !azimuth) return {};
  double lat1,lon1,lat2,lon2;
  if (!gridCoordinates(ownGrid,lat1,lon1) || !gridCoordinates(receivedGrid,lat2,lon2)) return {};
  lat1 *= pi/180.; lat2 *= pi/180.;
  double dlon = (lon2-lon1)*pi/180., dlat = lat2-lat1;
  double hav = std::pow(std::sin(dlat/2.),2) + std::cos(lat1)*std::cos(lat2)*std::pow(std::sin(dlon/2.),2);
  double km = 6371. * 2. * std::asin(std::sqrt(qBound(0.,hav,1.)));
  QStringList result;
  if (distance) result << QString::number(qRound(km * (miles ? 0.621371192237334 : 1.))) + (miles ? " mi" : " km");
  if (azimuth) {
    double bearing = km < 0.000001 ? 0. : std::atan2(std::sin(dlon)*std::cos(lat2),
                      std::cos(lat1)*std::sin(lat2)-std::sin(lat1)*std::cos(lat2)*std::cos(dlon))*180./pi;
    result << QString::number((qRound(bearing)+360)%360) + QChar(0x00b0);
  }
  return result.join(" / ");
}

QString DecodedPresentation::appendage(int messageColumns, QString const& country, QString const& state,
                                      QString const& geometry, bool align, int firstSpacing, int secondSpacing)
{
  QString entity = country.trimmed();
  if (!state.isEmpty()) entity += (entity.isEmpty() ? "" : " ") + state;
  QString result;
  if (!entity.isEmpty()) {
    int spaces = align ? qMax(1,50+qBound(-2,firstSpacing,10)-messageColumns) : 1;
    result = QString(spaces,' ') + entity;
  }
  if (!geometry.isEmpty()) {
    int columns = messageColumns + result.size();
    int spaces = align ? qMax(1,70+qBound(-2,firstSpacing,10)+qBound(-8,secondSpacing,5)-columns) : 1;
    result += QString(spaces,' ') + '[' + geometry + ']';
  }
  // DecodedText strips at this boundary when an operator selects a rendered
  // row. Labels must never be reparsed as part of an on-air message.
  if (!result.isEmpty()) result[0]=QChar::Nbsp;
  return result;
}

QString DecodedPresentation::periodSeparator(QDateTime const& start, QString const& band, QString const& mode,
                                            bool detailed, bool fromFile)
{
  QStringList pieces;
  if (detailed && start.isValid()) pieces << start.toUTC().toString("yyyy-MM-dd hh:mm:ss 'UTC'");
  if (!band.trimmed().isEmpty()) pieces << band.trimmed();
  if (!mode.trimmed().isEmpty()) pieces << mode.trimmed();
  if (fromFile) pieces << "WAV";
  QString text = pieces.join(" - ");
  return text.isEmpty() ? QString(40,'-') : "----- " + text + " -----";
}

QVector<QPair<int,int>> DecodedPresentation::tokenRanges(QString const& text, QString const& input, bool grid)
{
  QVector<QPair<int,int>> result;
  auto token = input.trimmed().toUpper();
  if (token.isEmpty()) return result;
  if (grid && !validGrid(token)) return result;
  if (grid) token = token.left(4);
  static QRegularExpression const words("[A-Za-z0-9/]+");
  auto matches = words.globalMatch(text);
  while (matches.hasNext()) {
    auto match = matches.next(); auto word = match.captured().toUpper();
    if ((!grid && word == token) || (grid && validGrid(word) && word.left(4) == token))
      result.append(qMakePair(match.capturedStart(),match.capturedLength()));
  }
  return result;
}

bool GridStateMap::load(QString const& path)
{
  states_.clear();
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 128*1024) return false;
  auto contents = QString::fromUtf8(file.readAll());
  QHash<QString,QString> parsed;
  QString prefix; bool opened = false;
  static QRegularExpression const heading("\\A([A-R]{2})<\\z");
  static QRegularExpression const entry("\\A([0-9]{2}):([A-Z]{2}(?:-[A-Z]{2})*)[,>]\\z");
  for (auto const& raw : contents.split('\n')) {
    auto line = raw.trimmed();
    if (line.isEmpty()) continue;
    auto group = heading.match(line);
    if (group.hasMatch()) {
      if (opened) return false;
      prefix = group.captured(1); opened = true;
      continue;
    }
    auto value = entry.match(line);
    if (!opened || !value.hasMatch()) return false;
    auto key = prefix + value.captured(1);
    if (parsed.contains(key)) return false;
    parsed.insert(key,value.captured(2));
    if (line.endsWith('>')) opened = false;
  }
  if (opened || parsed.isEmpty()) return false;
  states_.swap(parsed);
  return true;
}

QString GridStateMap::state(QString const& grid) const
{
  auto normalized = grid.trimmed().toUpper();
  return validGrid(normalized) ? states_.value(normalized.left(4)) : QString();
}
