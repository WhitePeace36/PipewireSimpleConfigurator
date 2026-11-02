#pragma once

#include <QString>
#include <QVariant>

// Reader and writer for SPA-JSON, the relaxed JSON dialect used by PipeWire and WirePlumber config files:
// keys and strings may be unquoted, "=" may be used instead of ":", commas are optional and "#" starts a comment.
//
// Parsed values are represented as:
//   QString      for every scalar (numbers, booleans, bare words and quoted strings, without quotes)
//   QVariantList for arrays
//   QVariantMap  for objects
namespace SpaJson
{
struct Error
{
  QString message;
  int line = 0;
  bool isError() const { return !message.isEmpty(); }
};

// Parses a whole config file. The top level is an object without braces ("section = value ...").
// Sections that appear more than once keep the last value, except arrays, which are appended.
QVariantMap parseDocument(const QString& text, Error* error = nullptr);

// Parses a single value such as "[ 44100 48000 ]" or "512/48000".
QVariant parseValue(const QString& text, Error* error = nullptr);

// Writes a scalar, quoting it when it cannot be written as a bare word.
QString formatScalar(const QString& value);

// Writes any value. Objects and arrays of objects are written over several lines, using |indent| spaces
// for the current nesting level; short arrays of scalars stay on one line.
QString formatValue(const QVariant& value, int indent = 0);

// Writes "key = value" lines for every entry of |object|, in the given key order (or sorted when empty).
QString formatObjectBody(const QVariantMap& object, int indent, const QStringList& order = {});

// Converts a QStringList or QVariantList of scalars to a QStringList.
QStringList toStringList(const QVariant& value);
}  // namespace SpaJson
