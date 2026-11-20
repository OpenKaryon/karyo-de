// SPDX-License-Identifier: GPL-3.0-or-later
#include "calcpage.h"

#include "keypad.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/navigation.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>
#include <karyo/ui/window.h>

#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QSettings>
#include <QVBoxLayout>

namespace Ui = Karyo::Ui;

namespace {

const QChar Minus(0x2212);
const QChar Times(0x00d7);
const QChar Divide(0x00f7);
const QChar Squared(0x00b2);
const QChar Root(0x221a);
const QChar PiSign(0x03c0);

QSettings calcSettings()
{
    return QSettings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                     QStringLiteral("calculator"));
}

bool isOperandChar(QChar c)
{
    return c.isDigit() || c.isLetter() || c == QLatin1Char('.') || c == QLatin1Char(',') || c == QLatin1Char('!')
           || c == QLatin1Char('%') || c == Squared;
}

// A number or a constant needs no parentheses around it.
bool isPlain(const QString &text)
{
    if (text == PiSign || text == QLatin1String("e"))
        return true;
    for (QChar c : text) {
        if (!c.isDigit() && c != QLatin1Char('.') && c != QLatin1Char(','))
            return false;
    }
    return !text.isEmpty();
}

// True when the text is one parenthesized group: (1+2), not (1)+(2).
bool isWrapped(const QString &text)
{
    if (!text.startsWith(QLatin1Char('(')) || !text.endsWith(QLatin1Char(')')))
        return false;
    int depth = 0;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('('))
            ++depth;
        else if (text.at(i) == QLatin1Char(')') && --depth == 0 && i != text.size() - 1)
            return false;
    }
    return true;
}

QString parenthesize(const QString &text)
{
    return isPlain(text) || isWrapped(text) ? text : QLatin1Char('(') + text + QLatin1Char(')');
}

} // namespace

void History::add(const QString &expression, const QString &result)
{
    m_entries.prepend({expression, result});
    if (m_entries.size() > 200)
        m_entries.removeLast();
    Q_EMIT changed();
}

void History::clear()
{
    m_entries.clear();
    Q_EMIT changed();
}

// The history column: each calculation as its expression over its result.
class HistoryList : public QWidget
{
    Q_OBJECT
public:
    explicit HistoryList(History *history, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_history(history)
    {
        setMouseTracking(true);
        connect(history, &History::changed, this, [this] {
            setFixedHeight(qMax(200, int(m_history->entries().size()) * RowHeight + 8));
            update();
        });
        setFixedHeight(200);
    }

Q_SIGNALS:
    void activated(int index);

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QList<History::Entry> &entries = m_history->entries();
        if (entries.isEmpty()) {
            p.setFont(Ui::bodyFont(13));
            p.setPen(t.muted);
            p.drawText(rect().adjusted(16, 16, -16, 0), Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap,
                       tr("There's no history yet."));
            return;
        }
        for (int i = 0; i < entries.size(); ++i) {
            const QRect row(0, 4 + i * RowHeight, width(), RowHeight);
            if (i == m_hover)
                Ui::fillRounded(p, QRectF(row).adjusted(6, 2, -6, -2), 14, t.hover);
            const QRect text = row.adjusted(18, 8, -18, -8);
            p.setFont(Ui::bodyFont(13));
            p.setPen(t.muted);
            p.drawText(text, Qt::AlignTop | Qt::AlignRight,
                       QFontMetrics(p.font()).elidedText(entries.at(i).expression + QStringLiteral(" ="), Qt::ElideLeft,
                                                         text.width()));
            p.setFont(Ui::bodyFont(20, QFont::DemiBold));
            p.setPen(t.text);
            p.drawText(text, Qt::AlignBottom | Qt::AlignRight,
                       QFontMetrics(p.font()).elidedText(entries.at(i).result, Qt::ElideRight, text.width()));
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const int row = rowAt(event->position().toPoint());
        if (row != m_hover) {
            m_hover = row;
            setCursor(row >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    }

    void leaveEvent(QEvent *) override
    {
        m_hover = -1;
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const int row = rowAt(event->position().toPoint());
        if (row >= 0)
            Q_EMIT activated(row);
    }

private:
    static constexpr int RowHeight = 66;
    int rowAt(const QPoint &pos) const
    {
        const int row = (pos.y() - 4) / RowHeight;
        return pos.y() >= 4 && row < m_history->entries().size() ? row : -1;
    }

    History *m_history;
    int m_hover = -1;
};

CalcPage::CalcPage(bool scientific, History *history, QWidget *parent)
    : QWidget(parent)
    , m_scientific(scientific)
    , m_history(history)
    , m_previous(new QLabel)
    , m_edit(new QLineEdit)
    , m_preview(new QLabel)
    , m_keys(new QWidget)
    , m_historyList(new HistoryList(history))
    , m_historyScroll(new QScrollArea)
{
    const Ui::Theme &t = Ui::Theme::current();
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(20, 18, 18, 18);
    row->setSpacing(18);

    // The display: the last calculation, the expression, its result as you type.
    auto *column = new QVBoxLayout;
    column->setSpacing(4);
    QPalette muted = palette();
    muted.setColor(QPalette::WindowText, t.muted);
    m_previous->setFont(Ui::bodyFont(15));
    m_previous->setPalette(muted);
    m_previous->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_previous->setMinimumHeight(22);
    m_previous->setTextInteractionFlags(Qt::TextSelectableByMouse);
    column->addWidget(m_previous);

    m_edit->setFrame(false);
    m_edit->setAlignment(Qt::AlignRight);
    m_edit->setFont(Ui::bodyFont(40, QFont::DemiBold));
    m_edit->setMinimumHeight(60);
    m_edit->setPlaceholderText(QStringLiteral("0"));
    QPalette editPalette = m_edit->palette();
    editPalette.setColor(QPalette::Base, Qt::transparent);
    editPalette.setColor(QPalette::Text, t.text);
    m_edit->setPalette(editPalette);
    m_edit->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; border: none; }"));
    m_edit->installEventFilter(this);
    connect(m_edit, &QLineEdit::textEdited, this, [this](const QString &text) {
        // Typed * / - read as × ÷ −; the lengths match, so the cursor stays.
        const QString shown = Calc::pretty(text);
        if (shown != text) {
            const int cursor = m_edit->cursorPosition();
            m_edit->setText(shown);
            m_edit->setCursorPosition(cursor);
        }
        m_evaluated = false;
        updatePreview();
    });
    column->addWidget(m_edit);

    m_preview->setFont(Ui::bodyFont(17));
    m_preview->setPalette(muted);
    m_preview->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_preview->setMinimumHeight(26);
    m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    column->addWidget(m_preview);
    column->addSpacing(10);

    // The keypad; Scientific puts two columns of functions in front.
    auto *grid = new QGridLayout(m_keys);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(4);
    const int c = scientific ? 2 : 0;
    using Kind = CalcKey::Kind;
    auto key = [this](const QString &label, Kind kind, auto action) {
        auto *k = new CalcKey(label, kind, m_keys);
        connect(k, &QAbstractButton::clicked, this, action);
        return k;
    };
    auto text = [this, key](const QString &label, Kind kind, const QString &inserted) {
        return key(label, kind, [this, inserted] { insert(inserted); });
    };
    addKey(0, c + 0, text(QStringLiteral("%"), Kind::Function, QStringLiteral("%")));
    addKey(0, c + 1, key(QStringLiteral("CE"), Kind::Function, [this] { clearEntry(); }));
    addKey(0, c + 2, key(QStringLiteral("C"), Kind::Function, [this] { clearAll(); }));
    auto *back = key(QString(), Kind::Function, [this] { backspace(); });
    back->setGlyph(QStringLiteral("delete"));
    back->setToolTip(tr("Backspace"));
    addKey(0, c + 3, back);
    addKey(1, c + 0, key(QStringLiteral("¹⁄ₓ"), Kind::Function, [this] { wrapOperand(Wrap::Reciprocal); }));
    addKey(1, c + 1, key(QStringLiteral("x²"), Kind::Function, [this] { wrapOperand(Wrap::Square); }));
    addKey(1, c + 2, key(QStringLiteral("√x"), Kind::Function, [this] { wrapOperand(Wrap::Root); }));
    addKey(1, c + 3, text(QString(Divide), Kind::Operator, QString(Divide)));
    const QString digits = QStringLiteral("789456123");
    for (int i = 0; i < 9; ++i)
        addKey(2 + i / 3, c + i % 3, text(digits.at(i), Kind::Digit, digits.at(i)));
    addKey(2, c + 3, text(QString(Times), Kind::Operator, QString(Times)));
    addKey(3, c + 3, text(QString(Minus), Kind::Operator, QString(Minus)));
    addKey(4, c + 3, text(QStringLiteral("+"), Kind::Operator, QStringLiteral("+")));
    addKey(5, c + 0, key(QStringLiteral("±"), Kind::Digit, [this] { wrapOperand(Wrap::Negate); }));
    addKey(5, c + 1, text(QStringLiteral("0"), Kind::Digit, QStringLiteral("0")));
    addKey(5, c + 2, text(QStringLiteral("."), Kind::Digit, QStringLiteral(".")));
    addKey(5, c + 3, key(QStringLiteral("="), Kind::Equals, [this] { evaluate(); }));
    if (scientific) {
        m_angleKey = key(QString(), Kind::Function, [this] {
            setAngle(m_angle == Calc::Angle::Degrees ? Calc::Angle::Radians : Calc::Angle::Degrees);
        });
        m_angleKey->setToolTip(tr("Degrees or radians"));
        addKey(0, 0, m_angleKey);
        addKey(0, 1, text(QString(PiSign), Kind::Function, QString(PiSign)));
        addKey(1, 0, text(QStringLiteral("sin"), Kind::Function, QStringLiteral("sin(")));
        addKey(1, 1, text(QStringLiteral("cos"), Kind::Function, QStringLiteral("cos(")));
        addKey(2, 0, text(QStringLiteral("tan"), Kind::Function, QStringLiteral("tan(")));
        addKey(2, 1, text(QStringLiteral("e"), Kind::Function, QStringLiteral("e")));
        addKey(3, 0, text(QStringLiteral("ln"), Kind::Function, QStringLiteral("ln(")));
        addKey(3, 1, text(QStringLiteral("log"), Kind::Function, QStringLiteral("log(")));
        addKey(4, 0, text(QStringLiteral("xʸ"), Kind::Function, QStringLiteral("^")));
        addKey(4, 1, text(QStringLiteral("n!"), Kind::Function, QStringLiteral("!")));
        addKey(5, 0, text(QStringLiteral("("), Kind::Function, QStringLiteral("(")));
        addKey(5, 1, text(QStringLiteral(")"), Kind::Function, QStringLiteral(")")));
        setAngle(calcSettings().value(QStringLiteral("angle")).toString() == QLatin1String("rad")
                     ? Calc::Angle::Radians
                     : Calc::Angle::Degrees);
    }
    column->addWidget(m_keys, 1);
    row->addLayout(column, 1);

    // History, on a pane-colored card.
    auto *card = new Ui::InsetPanel;
    card->setFill(t.pane);
    card->setRadius(22);
    card->setFixedWidth(270);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(4, 10, 4, 6);
    cardLayout->setSpacing(4);
    auto *header = new QHBoxLayout;
    header->setContentsMargins(14, 0, 6, 0);
    auto *title = new QLabel(tr("History"));
    title->setFont(Ui::bodyFont(14, QFont::DemiBold));
    header->addWidget(title);
    header->addStretch(1);
    auto *clear = new Ui::IconButton(QStringLiteral("trash-2"), card, {32, 32}, 16);
    clear->setHint(tr("Clear history"));
    connect(clear, &QAbstractButton::clicked, history, &History::clear);
    header->addWidget(clear);
    cardLayout->addLayout(header);
    m_historyScroll->setFrameShape(QFrame::NoFrame);
    m_historyScroll->setWidgetResizable(true);
    m_historyScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_historyScroll->setWidget(m_historyList);
    m_historyScroll->viewport()->setAutoFillBackground(false);
    m_historyList->setAutoFillBackground(false);
    cardLayout->addWidget(m_historyScroll, 1);
    connect(m_historyList, &HistoryList::activated, this, &CalcPage::recall);
    row->addWidget(card);
}

void CalcPage::addKey(int row, int column, CalcKey *key)
{
    static_cast<QGridLayout *>(m_keys->layout())->addWidget(key, row, column);
}

void CalcPage::focusDisplay()
{
    m_edit->setFocus();
    m_edit->end(false);
}

void CalcPage::insert(const QString &text)
{
    if (m_evaluated) {
        // An operator goes on with the result; anything else starts anew.
        static const QString continuing = QStringLiteral("+−×÷^%!²");
        if (!continuing.contains(text.at(0))) {
            m_edit->clear();
            m_previous->clear();
        }
        m_evaluated = false;
        m_edit->end(false);
    }
    m_edit->insert(text);
    focusDisplay();
    updatePreview();
}

int CalcPage::operandStart(const QString &text) const
{
    int i = int(text.size());
    if (i == 0)
        return -1;
    if (text.endsWith(QLatin1Char(')'))) {
        int depth = 0;
        for (--i; i >= 0; --i) {
            if (text.at(i) == QLatin1Char(')'))
                ++depth;
            else if (text.at(i) == QLatin1Char('(') && --depth == 0)
                break;
        }
        if (i < 0)
            return -1;
    } else {
        while (i > 0 && isOperandChar(text.at(i - 1)))
            --i;
        if (i == text.size())
            return -1;
    }
    // A function name or √ in front belongs to the operand.
    while (i > 0 && (text.at(i - 1).isLetter() || text.at(i - 1) == Root))
        --i;
    return i;
}

void CalcPage::wrapOperand(Wrap wrap)
{
    const QString text = m_edit->text();
    const int start = m_evaluated ? 0 : operandStart(text);
    if (start < 0)
        return;
    const QString head = text.left(start);
    const QString operand = text.mid(start);
    const QString p = parenthesize(operand);
    QString replaced;
    switch (wrap) {
    case Wrap::Reciprocal:
        replaced = QStringLiteral("(1") + Divide + p + QLatin1Char(')');
        break;
    case Wrap::Square:
        replaced = p + Squared;
        break;
    case Wrap::Root:
        replaced = Root + p;
        break;
    case Wrap::Negate:
        if (operand.startsWith(QStringLiteral("(") + Minus) && isWrapped(operand))
            replaced = operand.mid(2, operand.size() - 3);
        else if (start == 0 && operand.startsWith(Minus) && isPlain(operand.mid(1)))
            replaced = operand.mid(1);
        else if (start == 0 && isPlain(operand))
            replaced = Minus + operand;
        else
            replaced = QStringLiteral("(") + Minus + operand + QLatin1Char(')');
        break;
    }
    m_edit->setText(head + replaced);
    m_evaluated = false;
    focusDisplay();
    updatePreview();
}

void CalcPage::evaluate()
{
    const QString expression = m_edit->text().trimmed();
    if (expression.isEmpty())
        return;
    const Calc::Result result = Calc::evaluate(expression, m_angle, m_ans);
    if (!result.ok) {
        m_preview->setText(result.error);
        return;
    }
    const QString shown = Calc::format(result.value);
    if (shown != expression) {
        m_history->add(expression, shown);
        m_previous->setText(expression + QStringLiteral(" ="));
    }
    m_edit->setText(shown);
    m_ans = result.value;
    m_evaluated = true;
    m_preview->clear();
    fitFont();
    focusDisplay();
}

void CalcPage::clearAll()
{
    m_edit->clear();
    m_previous->clear();
    m_evaluated = false;
    updatePreview();
    focusDisplay();
}

void CalcPage::clearEntry()
{
    if (m_evaluated) {
        clearAll();
        return;
    }
    const QString text = m_edit->text();
    const int start = operandStart(text);
    if (start >= 0)
        m_edit->setText(text.left(start));
    updatePreview();
    focusDisplay();
}

void CalcPage::backspace()
{
    m_evaluated = false;
    if (!m_edit->hasSelectedText())
        m_edit->end(false);
    m_edit->backspace();
    updatePreview();
    focusDisplay();
}

void CalcPage::recall(int index)
{
    const History::Entry entry = m_history->entries().value(index);
    m_edit->setText(entry.result);
    m_previous->setText(entry.expression + QStringLiteral(" ="));
    m_ans = Calc::evaluate(entry.result).value;
    m_evaluated = true;
    m_preview->clear();
    fitFont();
    focusDisplay();
}

void CalcPage::setAngle(Calc::Angle angle)
{
    m_angle = angle;
    if (m_angleKey)
        m_angleKey->setText(angle == Calc::Angle::Degrees ? tr("DEG") : tr("RAD"));
    calcSettings().setValue(QStringLiteral("angle"),
                            angle == Calc::Angle::Degrees ? QStringLiteral("deg") : QStringLiteral("rad"));
    updatePreview();
}

void CalcPage::updatePreview()
{
    fitFont();
    const QString text = m_edit->text().trimmed();
    if (m_evaluated || text.isEmpty()) {
        m_preview->clear();
        return;
    }
    // Only a result that says something new; nothing for "1+" half typed.
    const Calc::Result result = Calc::evaluate(text, m_angle, m_ans);
    const QString shown = result.ok ? Calc::format(result.value) : QString();
    m_preview->setText(result.ok && shown != text ? QStringLiteral("= ") + shown : QString());
}

void CalcPage::fitFont()
{
    // Long expressions get smaller, down to a readable size.
    qreal size = 40;
    const int room = m_edit->width() - 24;
    while (size > 20 && QFontMetrics(Ui::bodyFont(size, QFont::DemiBold)).horizontalAdvance(m_edit->text()) > room)
        size -= 2;
    if (m_edit->font().pixelSize() != qRound(size))
        m_edit->setFont(Ui::bodyFont(size, QFont::DemiBold));
}

bool CalcPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_edit && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->text() == QLatin1String("=")) {
            evaluate();
            return true;
        }
        if (key->key() == Qt::Key_Escape) {
            clearAll();
            return true;
        }
        const QString typed = key->text();
        if (m_evaluated && !typed.isEmpty() && !(key->modifiers() & Qt::ControlModifier)) {
            const QChar c = typed.at(0);
            if (c.isDigit() || c.isLetter() || c == QLatin1Char('.') || c == QLatin1Char('(') || c == Root
                || c == PiSign) {
                m_edit->clear();
                m_previous->clear();
            } else {
                m_edit->end(false);
            }
            m_evaluated = false;
        }
    } else if (watched == m_edit && event->type() == QEvent::Resize) {
        fitFont();
    }
    return QWidget::eventFilter(watched, event);
}

#include "calcpage.moc"
