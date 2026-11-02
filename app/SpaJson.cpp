#include "SpaJson.h"

#include <QStringList>

namespace SpaJson
{
namespace
{
class Parser
{
public:
  explicit Parser(const QString& text) : m_text(text) {}

  QVariantMap document()
  {
    QVariantMap result;
    skipSpace();
    // Some files wrap the whole document in braces.
    if (peek() == '{')
    {
      ++m_pos;
      parseMembers(result, '}');
      expect('}');
    }
    else
    {
      parseMembers(result, QChar());
    }
    skipSpace();
    if (!atEnd() && !failed())
      fail(QStringLiteral("Unexpected '%1'").arg(peek()));
    return result;
  }

  QVariant single()
  {
    skipSpace();
    QVariant v = value();
    skipSpace();
    if (!atEnd() && !failed())
      fail(QStringLiteral("Unexpected '%1'").arg(peek()));
    return v;
  }

  Error error() const { return m_error; }
  bool failed() const { return m_error.isError(); }

private:
  bool atEnd() const { return m_pos >= m_text.size(); }
  QChar peek() const { return atEnd() ? QChar() : m_text.at(m_pos); }

  void fail(const QString& message)
  {
    if (failed())
      return;
    m_error.message = message;
    m_error.line = m_text.left(m_pos).count('\n') + 1;
    m_pos = m_text.size();
  }

  void skipSpace()
  {
    while (!atEnd())
    {
      const QChar c = peek();
      if (c == '#')
      {
        while (!atEnd() && peek() != '\n')
          ++m_pos;
      }
      else if (c.isSpace() || c == ',')
      {
        ++m_pos;
      }
      else
      {
        break;
      }
    }
  }

  void expect(QChar c)
  {
    skipSpace();
    if (peek() != c)
    {
      fail(atEnd() ? QStringLiteral("Missing '%1'").arg(c) : QStringLiteral("Expected '%1' but found '%2'").arg(c, peek()));
      return;
    }
    ++m_pos;
  }

  static bool isBareTerminator(QChar c) { return c.isSpace() || c == ',' || c == '=' || c == ':' || c == '{' || c == '}' || c == '[' || c == ']' || c == '"'; }

  QString scalar()
  {
    if (peek() == '"')
      return quoted();
    const int start = m_pos;
    while (!atEnd() && !isBareTerminator(peek()))
      ++m_pos;
    if (m_pos == start)
      fail(atEnd() ? QStringLiteral("Unexpected end of file") : QStringLiteral("Unexpected '%1'").arg(peek()));
    return m_text.mid(start, m_pos - start);
  }

  QString quoted()
  {
    ++m_pos;  // opening quote
    QString out;
    while (!atEnd())
    {
      const QChar c = m_text.at(m_pos++);
      if (c == '"')
        return out;
      if (c != '\\' || atEnd())
      {
        out.append(c);
        continue;
      }
      const QChar e = m_text.at(m_pos++);
      switch (e.unicode())
      {
        case 'n':
          out.append('\n');
          break;
        case 't':
          out.append('\t');
          break;
        case 'r':
          out.append('\r');
          break;
        case 'b':
          out.append('\b');
          break;
        case 'f':
          out.append('\f');
          break;
        case 'u':
        {
          bool ok = false;
          const uint code = m_text.mid(m_pos, 4).toUInt(&ok, 16);
          if (ok)
          {
            out.append(QChar(code));
            m_pos += 4;
          }
          break;
        }
        default:
          out.append(e);
          break;
      }
    }
    fail(QStringLiteral("Unterminated string"));
    return out;
  }

  QVariant value()
  {
    skipSpace();
    const QChar c = peek();
    if (c == '{')
    {
      ++m_pos;
      QVariantMap obj;
      parseMembers(obj, '}');
      expect('}');
      return obj;
    }
    if (c == '[')
    {
      ++m_pos;
      QVariantList list;
      while (true)
      {
        skipSpace();
        if (atEnd() || failed())
        {
          fail(QStringLiteral("Missing ']'"));
          break;
        }
        if (peek() == ']')
        {
          ++m_pos;
          break;
        }
        list.append(value());
      }
      return list;
    }
    return scalar();
  }

  void parseMembers(QVariantMap& obj, QChar terminator)
  {
    while (true)
    {
      skipSpace();
      if (atEnd() || failed())
      {
        if (!terminator.isNull())
          fail(QStringLiteral("Missing '%1'").arg(terminator));
        return;
      }
      if (!terminator.isNull() && peek() == terminator)
        return;

      const QString key = scalar();
      skipSpace();
      if (peek() == '=' || peek() == ':')
        ++m_pos;
      QVariant v = value();
      if (failed())
        return;

      // Repeated array sections are appended, everything else is replaced (same as PipeWire's merge).
      auto it = obj.find(key);
      if (it != obj.end() && it->typeId() == QMetaType::QVariantList && v.typeId() == QMetaType::QVariantList)
      {
        QVariantList merged = it->toList();
        merged.append(v.toList());
        *it = merged;
      }
      else
      {
        obj.insert(key, v);
      }
    }
  }

  const QString& m_text;
  int m_pos = 0;
  Error m_error;
};

bool needsQuotes(const QString& s)
{
  if (s.isEmpty())
    return true;
  for (const QChar c : s)
  {
    if (c.isSpace() || c == ',' || c == '=' || c == ':' || c == '{' || c == '}' || c == '[' || c == ']' || c == '"' || c == '#' || c == '\\')
      return true;
  }
  return false;
}

bool isComplex(const QVariant& v)
{
  return v.typeId() == QMetaType::QVariantMap || v.typeId() == QMetaType::QVariantList || v.typeId() == QMetaType::QStringList;
}

}  // namespace

QVariantMap parseDocument(const QString& text, Error* error)
{
  Parser p(text);
  QVariantMap result = p.document();
  if (error)
    *error = p.error();
  return p.failed() ? QVariantMap() : result;
}

QVariant parseValue(const QString& text, Error* error)
{
  Parser p(text);
  QVariant result = p.single();
  if (error)
    *error = p.error();
  return p.failed() ? QVariant() : result;
}

QString formatScalar(const QString& value)
{
  if (!needsQuotes(value))
    return value;
  QString out = QStringLiteral("\"");
  for (const QChar c : value)
  {
    if (c == '"' || c == '\\')
      out += '\\';
    if (c == '\n')
    {
      out += QStringLiteral("\\n");
      continue;
    }
    out += c;
  }
  return out + '"';
}

QStringList toStringList(const QVariant& value)
{
  if (value.typeId() == QMetaType::QStringList)
    return value.toStringList();
  QStringList out;
  if (value.typeId() == QMetaType::QVariantList)
  {
    for (const QVariant& v : value.toList())
      out.append(v.toString());
    return out;
  }
  if (!value.isNull() && !value.toString().trimmed().isEmpty())
  {
    // A list given as text, e.g. "[ 44100 48000 ]".
    const QVariant parsed = parseValue(value.toString());
    if (parsed.typeId() == QMetaType::QVariantList)
      return toStringList(parsed);
    out.append(value.toString());
  }
  return out;
}

QString formatObjectBody(const QVariantMap& object, int indent, const QStringList& order)
{
  QStringList keys = order.isEmpty() ? object.keys() : order;
  const QString pad(indent, ' ');
  QString out;
  for (const QString& key : keys)
  {
    if (!object.contains(key))
      continue;
    out += pad + formatScalar(key) + " = " + formatValue(object.value(key), indent) + "\n";
  }
  return out;
}

QString formatValue(const QVariant& value, int indent)
{
  const QString pad(indent, ' ');
  switch (value.typeId())
  {
    case QMetaType::QVariantMap:
    {
      const QVariantMap map = value.toMap();
      if (map.isEmpty())
        return QStringLiteral("{ }");
      return "{\n" + formatObjectBody(map, indent + 4) + pad + "}";
    }
    case QMetaType::QStringList:
    case QMetaType::QVariantList:
    {
      const QVariantList list = value.typeId() == QMetaType::QStringList ? QVariant(value.toStringList()).toList() : value.toList();
      if (list.isEmpty())
        return QStringLiteral("[ ]");
      bool anyComplex = false;
      for (const QVariant& v : list)
        anyComplex |= isComplex(v);
      if (!anyComplex)
      {
        QStringList parts;
        for (const QVariant& v : list)
          parts.append(formatScalar(v.toString()));
        return "[ " + parts.join(' ') + " ]";
      }
      QString out = QStringLiteral("[\n");
      for (const QVariant& v : list)
        out += pad + "    " + formatValue(v, indent + 4) + "\n";
      return out + pad + "]";
    }
    default:
      return formatScalar(value.toString());
  }
}

}  // namespace SpaJson
