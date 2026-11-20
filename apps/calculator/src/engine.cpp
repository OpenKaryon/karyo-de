// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine.h"

#include <QCoreApplication>

#include <cmath>

namespace Calc {

namespace {

class Text
{
    Q_DECLARE_TR_FUNCTIONS(Calc)
};

constexpr double Pi = 3.14159265358979323846;

bool isMinus(QChar c) { return c == QLatin1Char('-') || c == QChar(0x2212); }
bool isTimes(QChar c) { return c == QLatin1Char('*') || c == QChar(0x00d7) || c == QChar(0x00b7) || c == QChar(0x22c5); }
bool isDivide(QChar c) { return c == QLatin1Char('/') || c == QChar(0x00f7) || c == QChar(0x2215); }

class Parser
{
public:
    Parser(const QString &text, Angle angle, double ans)
        : m_text(text)
        , m_angle(angle)
        , m_ans(ans)
    {
    }

    Result run()
    {
        Result result;
        if (peek().isNull()) {
            result.error = Text::tr("Invalid input");
            return result;
        }
        bool percent = false;
        double value = expression(&percent);
        if (!m_failed && !peek().isNull())
            fail();
        if (m_failed) {
            result.error = m_divideByZero ? Text::tr("Cannot divide by zero") : Text::tr("Invalid input");
            return result;
        }
        if (std::isnan(value)) {
            result.error = Text::tr("Result is undefined");
            return result;
        }
        if (std::isinf(value)) {
            result.error = Text::tr("Overflow");
            return result;
        }
        result.ok = true;
        result.value = value == 0 ? 0 : value;  // no −0
        return result;
    }

private:
    QChar peek()
    {
        while (m_pos < m_text.size() && m_text.at(m_pos).isSpace())
            ++m_pos;
        return m_pos < m_text.size() ? m_text.at(m_pos) : QChar();
    }

    void fail()
    {
        m_failed = true;
        m_pos = m_text.size();
    }

    static bool startsOperand(QChar c)
    {
        return c.isDigit() || c.isLetter() || c == QLatin1Char('.') || c == QLatin1Char('(') || c == QChar(0x03c0)
               || c == QChar(0x221a);
    }

    // a + b% is a + a·b/100; elsewhere b% is b/100.
    double expression(bool *percent)
    {
        double left = term(percent);
        for (;;) {
            const QChar c = peek();
            const bool plus = c == QLatin1Char('+');
            if (!plus && !isMinus(c))
                return left;
            ++m_pos;
            bool rightPercent = false;
            double right = term(&rightPercent);
            if (rightPercent)
                right *= left;
            left = plus ? left + right : left - right;
            *percent = false;
        }
    }

    double term(bool *percent)
    {
        double left = unary(percent);
        for (;;) {
            const QChar c = peek();
            bool ignored = false;
            if (isTimes(c)) {
                ++m_pos;
                left *= unary(&ignored);
            } else if (isDivide(c)) {
                ++m_pos;
                const double right = unary(&ignored);
                if (right == 0 && !m_failed) {
                    m_divideByZero = true;
                    fail();
                    return 0;
                }
                left /= right;
            } else if (startsOperand(c) && !m_failed) {
                left *= unary(&ignored);  // 2π, 3(1+2)
            } else {
                return left;
            }
            *percent = false;
        }
    }

    double unary(bool *percent)
    {
        const QChar c = peek();
        if (isMinus(c)) {
            ++m_pos;
            return -unary(percent);
        }
        if (c == QLatin1Char('+')) {
            ++m_pos;
            return unary(percent);
        }
        return power(percent);
    }

    double power(bool *percent)
    {
        const double base = postfix(percent);
        if (peek() != QLatin1Char('^'))
            return base;
        ++m_pos;
        *percent = false;
        bool ignored = false;
        return std::pow(base, unary(&ignored));  // right-associative: 2^3^2 is 2^9
    }

    double postfix(bool *percent)
    {
        double value = primary();
        for (;;) {
            const QChar c = peek();
            if (c == QLatin1Char('!')) {
                ++m_pos;
                value = factorial(value);
            } else if (c == QLatin1Char('%')) {
                ++m_pos;
                value /= 100;
                *percent = true;
            } else if (c == QChar(0x00b2)) {
                ++m_pos;
                value *= value;
            } else if (c == QChar(0x00b3)) {
                ++m_pos;
                value = value * value * value;
            } else {
                return value;
            }
        }
    }

    static double factorial(double n)
    {
        if (n < 0 && n == std::floor(n))
            return NAN;
        if (n > 170)
            return INFINITY;
        if (n == std::floor(n)) {
            double product = 1;
            for (int i = 2; i <= int(n); ++i)
                product *= i;
            return product;
        }
        return std::tgamma(n + 1);
    }

    double primary()
    {
        const QChar c = peek();
        if (c.isNull()) {
            fail();
            return 0;
        }
        if (c == QLatin1Char('(')) {
            ++m_pos;
            bool ignored = false;
            const double value = expression(&ignored);
            if (peek() == QLatin1Char(')'))
                ++m_pos;
            else if (!peek().isNull())  // closing parentheses at the end may be left out
                fail();
            return value;
        }
        if (c.isDigit() || c == QLatin1Char('.') || c == QLatin1Char(','))
            return number();
        if (c == QChar(0x03c0)) {
            ++m_pos;
            return Pi;
        }
        if (c == QChar(0x221a)) {
            ++m_pos;
            bool ignored = false;
            return std::sqrt(postfix(&ignored));
        }
        if (c.isLetter())
            return identifier();
        fail();
        return 0;
    }

    double number()
    {
        QString digits;
        while (m_pos < m_text.size()) {
            const QChar c = m_text.at(m_pos);
            if (c.isDigit()) {
                digits += c;
            } else if (c == QLatin1Char('.') || c == QLatin1Char(',')) {
                if (digits.contains(QLatin1Char('.'))) {
                    fail();
                    return 0;
                }
                digits += QLatin1Char('.');
            } else {
                break;
            }
            ++m_pos;
        }
        // An exponent only when digits follow: 2e is 2·e.
        if (m_pos < m_text.size() && (m_text.at(m_pos) == QLatin1Char('e') || m_text.at(m_pos) == QLatin1Char('E'))) {
            int next = m_pos + 1;
            QString exponent;
            if (next < m_text.size() && (m_text.at(next) == QLatin1Char('+') || isMinus(m_text.at(next)))) {
                exponent += isMinus(m_text.at(next)) ? QLatin1Char('-') : QLatin1Char('+');
                ++next;
            }
            if (next < m_text.size() && m_text.at(next).isDigit()) {
                while (next < m_text.size() && m_text.at(next).isDigit())
                    exponent += m_text.at(next++);
                digits += QLatin1Char('e') + exponent;
                m_pos = next;
            }
        }
        bool ok = false;
        const double value = digits.toDouble(&ok);
        if (!ok)
            fail();
        return value;
    }

    double identifier()
    {
        QString name;
        while (m_pos < m_text.size() && m_text.at(m_pos).isLetter())
            name += m_text.at(m_pos++).toLower();
        if (name == QLatin1String("pi"))
            return Pi;
        if (name == QLatin1String("e"))
            return std::exp(1.0);
        if (name == QLatin1String("ans"))
            return m_ans;

        static const QStringList functions = {
            QStringLiteral("sin"),  QStringLiteral("cos"),  QStringLiteral("tan"), QStringLiteral("asin"),
            QStringLiteral("acos"), QStringLiteral("atan"), QStringLiteral("ln"),  QStringLiteral("log"),
            QStringLiteral("sqrt"), QStringLiteral("abs"),  QStringLiteral("exp"),
        };
        if (!functions.contains(name)) {
            fail();
            return 0;
        }
        // The argument: a parenthesized expression, or the next operand (sin30).
        bool ignored = false;
        const double x = postfix(&ignored);
        const bool degrees = m_angle == Angle::Degrees;
        const double toRadians = degrees ? Pi / 180 : 1;
        auto tidy = [](double value) { return std::abs(value) < 1e-12 ? 0.0 : value; };
        if (name == QLatin1String("sin"))
            return tidy(std::sin(x * toRadians));
        if (name == QLatin1String("cos"))
            return tidy(std::cos(x * toRadians));
        if (name == QLatin1String("tan")) {
            if (degrees && std::fmod(std::abs(x), 180.0) == 90)
                return NAN;
            return tidy(std::tan(x * toRadians));
        }
        if (name == QLatin1String("asin"))
            return std::asin(x) / toRadians;
        if (name == QLatin1String("acos"))
            return std::acos(x) / toRadians;
        if (name == QLatin1String("atan"))
            return std::atan(x) / toRadians;
        if (name == QLatin1String("ln"))
            return x > 0 ? std::log(x) : NAN;
        if (name == QLatin1String("log"))
            return x > 0 ? std::log10(x) : NAN;
        if (name == QLatin1String("sqrt"))
            return std::sqrt(x);
        if (name == QLatin1String("abs"))
            return std::abs(x);
        return std::exp(x);
    }

    QString m_text;
    Angle m_angle;
    double m_ans;
    int m_pos = 0;
    bool m_failed = false;
    bool m_divideByZero = false;
};

} // namespace

Result evaluate(const QString &expression, Angle angle, double ans)
{
    return Parser(expression, angle, ans).run();
}

QString format(double value, int significant)
{
    if (value == 0 || !std::isfinite(value))
        return QStringLiteral("0");
    QString text = QString::number(value, 'g', significant);
    const int e = text.indexOf(QLatin1Char('e'));
    if (e >= 0) {
        // 1.5e-07 -> 1.5e-7, 2e+15 -> 2e15
        QString exponent = text.mid(e + 1);
        const bool negative = exponent.startsWith(QLatin1Char('-'));
        exponent.remove(0, exponent.startsWith(QLatin1Char('+')) || negative ? 1 : 0);
        while (exponent.size() > 1 && exponent.startsWith(QLatin1Char('0')))
            exponent.remove(0, 1);
        text = text.left(e) + QLatin1Char('e') + (negative ? QStringLiteral("-") : QString()) + exponent;
    }
    return pretty(text);
}

QString pretty(const QString &expression)
{
    QString text = expression;
    text.replace(QLatin1Char('*'), QChar(0x00d7));
    text.replace(QLatin1Char('/'), QChar(0x00f7));
    text.replace(QLatin1Char('-'), QChar(0x2212));
    return text;
}

} // namespace Calc
