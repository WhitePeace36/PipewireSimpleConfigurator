#pragma once

#include <QString>
#include <QStringList>
#include <functional>

// Consistency checks shown as warnings at the top of a page.
// |value| returns the effective value of a key on the page, |graph| the effective value of a
// graph setting (default.clock.*).
using ValueGetter = std::function<QString(const QString& key)>;

QStringList runChecks(const QString& scope, const ValueGetter& value, const ValueGetter& graph);

// Parses "[ 44100 48000 ]" style lists into integers.
QList<int> parseIntList(const QString& text);
// Parses "512/48000" into milliseconds, or -1.
double fractionToMs(const QString& text);
