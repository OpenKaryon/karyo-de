// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/menu.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QContextMenuEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

namespace Ui = Karyo::Ui;

namespace {

bool isConda(const QString &source)
{
    return source == QLatin1String("bioconda") || source == QLatin1String("conda-forge");
}

void setInk(QLabel *label, const QColor &color)
{
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, color);
    label->setPalette(palette);
}

} // namespace

// ---------------------------------------------------------------- Pill

Pill::Pill(const QString &text, bool primary, QWidget *parent, const QString &glyph)
    : QAbstractButton(parent)
    , m_glyph(glyph)
    , m_primary(primary)
{
    setText(text);
    setAttribute(Qt::WA_Hover);
    setFocusPolicy(Qt::TabFocus);
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(34);
    setFixedWidth(sizeHint().width());
}

QFont Pill::labelFont() const { return m_primary ? Ui::headingFont(14) : Ui::bodyFont(13.5, QFont::DemiBold); }

QSize Pill::sizeHint() const
{
    int w = QFontMetrics(labelFont()).horizontalAdvance(text()) + 32;
    if (!m_glyph.isEmpty())
        w += 16 + 8;
    return {w, 34};
}

void Pill::setPrimary(bool primary)
{
    m_primary = primary;
    setFixedWidth(sizeHint().width());
    update();
}

void Pill::setProgress(int percent)
{
    m_progress = percent;
    update();
}

void Pill::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QColor ink = t.text;
    if (m_progress >= 0) {
        Ui::fillRounded(p, r, r.height() / 2, t.tint1);
        QPainterPath clip;
        clip.addRoundedRect(r, r.height() / 2, r.height() / 2);
        p.save();
        p.setClipPath(clip);
        p.fillRect(QRectF(r.left(), r.top(), r.width() * qMax(6, m_progress) / 100.0, r.height()), Ui::alpha(t.accent, 55));
        p.restore();
        p.setFont(Ui::bodyFont(12.5, QFont::DemiBold));
        p.setPen(t.ink1);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("%1%").arg(m_progress));
        return;
    }
    if (m_primary) {
        Ui::fillPill(p, r, isDown() ? Ui::accent(700) : underMouse() ? Ui::accent(600) : t.accent);
        ink = Ui::neutral(100);
    } else {
        Ui::fillRounded(p, r, r.height() / 2, isDown() ? t.press : underMouse() ? t.hover : Ui::alpha(t.win, 80),
                        Ui::alpha(t.text, 16));
    }
    int x = 16;
    if (!m_glyph.isEmpty()) {
        p.drawPixmap(x, (height() - 16) / 2, Ui::icon(m_glyph, 16, ink, devicePixelRatioF()));
        x += 16 + 8;
    }
    p.setFont(labelFont());
    p.setPen(ink);
    p.drawText(QRect(x, 0, width() - x - 14, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
}

// ---------------------------------------------------------------- SourceTag

SourceTag::SourceTag(const QString &source, QWidget *parent)
    : QWidget(parent)
    , m_source(source)
{
    setFixedSize(sizeHint());
}

QSize SourceTag::sizeHint() const
{
    return {QFontMetrics(Ui::monoFont(11.5)).horizontalAdvance(m_source) + 20, 24};
}

void SourceTag::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor fill = Ui::alpha(t.text, 8);
    QColor ink = t.muted;
    if (isConda(m_source)) {
        fill = t.tint2;
        ink = t.ink2;
    } else if (m_source == QLatin1String("apt") || m_source == QLatin1String("deb")) {
        fill = t.tint1;
        ink = t.ink1;
    }
    Ui::fillPill(p, rect(), fill);
    p.setFont(Ui::monoFont(11.5));
    p.setPen(ink);
    p.drawText(rect(), Qt::AlignCenter, m_source);
}

void paintAppTile(QPainter &painter, const QRectF &rect, const CatalogApp &app)
{
    const Ui::Theme &t = Ui::Theme::current();
    QColor fill = t.card;
    QColor ink = t.text;
    if (isConda(app.source)) {
        fill = Ui::mix(Ui::sage(200), t.win, t.scheme == Ui::ColorScheme::Light ? 100 : 30);
        ink = t.ink2;
    } else if (app.kind == QLatin1String("gui")) {
        fill = Ui::mix(Ui::neutral(300), t.win, t.scheme == Ui::ColorScheme::Light ? 100 : 30);
    } else {
        fill = Ui::mix(Ui::accent(100), t.win, t.scheme == Ui::ColorScheme::Light ? 100 : 25);
        ink = t.ink1;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    Ui::fillRounded(painter, rect, rect.width() * 14 / 46, fill);
    painter.setFont(Ui::monoFont(rect.width() * (app.tile.size() > 2 ? 0.3 : 0.36), QFont::DemiBold));
    painter.setPen(ink);
    painter.drawText(rect, Qt::AlignCenter, app.tile);
    painter.restore();
}

// ---------------------------------------------------------------- AppCard

AppCard::AppCard(const CatalogApp &app, PackageManager *packages, QWidget *parent)
    : QWidget(parent)
    , m_app(app)
    , m_packages(packages)
    , m_action(new Pill(tr("Install"), true, this))
{
    setFixedHeight(170);
    setMinimumWidth(240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
    auto *tag = new SourceTag(app.source, this);
    tag->move(18, height() - 18 - 24);
    connect(m_action, &QAbstractButton::clicked, this, [this] {
        if (m_packages->isInstalled(m_app))
            m_packages->open(m_app);
        else
            m_packages->install(m_app);
    });
    connect(m_packages, &PackageManager::changed, this, &AppCard::sync);
    connect(m_packages, &PackageManager::jobChanged, this, [this](const CatalogApp &changed) {
        if (changed.source == m_app.source && changed.package == m_app.package)
            sync();
    });
    sync();
}

void AppCard::sync()
{
    m_action->setEnabled(!m_packages->isBusy(m_app));
    if (m_packages->isBusy(m_app)) {
        m_action->setProgress(m_packages->progress(m_app));
        m_action->setFixedWidth(90);
    } else {
        m_action->setProgress(-1);
        const bool installed = m_packages->isInstalled(m_app);
        m_action->setText(installed ? tr("Open") : tr("Install"));
        m_action->setPrimary(!installed);
    }
    m_action->move(width() - 18 - m_action->width(), height() - 18 - m_action->height());
    update();
}

void AppCard::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    Ui::fillRounded(p, rect(), 8, m_hover ? Ui::mix(t.text, t.card, 4) : t.card);
    paintAppTile(p, QRectF(18, 18, 46, 46), m_app);
    const int x = 18 + 46 + 12;
    p.setFont(Ui::bodyFont(15, QFont::DemiBold));
    p.setPen(t.text);
    p.drawText(QRect(x, 20, width() - x - 18, 22), Qt::AlignVCenter | Qt::AlignLeft,
               QFontMetrics(p.font()).elidedText(m_app.name, Qt::ElideRight, width() - x - 18));
    p.setFont(Ui::bodyFont(12));
    p.setPen(t.muted);
    const QString version = m_packages->version(m_app);
    p.drawText(QRect(x, 42, width() - x - 18, 18), Qt::AlignVCenter | Qt::AlignLeft,
               version.isEmpty() ? m_app.package : version);
    p.setFont(Ui::bodyFont(13));
    p.setPen(t.text);
    const QRect summary(18, 76, width() - 36, 38);
    const QFontMetrics metrics(p.font());
    QString text = m_app.summary;
    if (metrics.boundingRect(summary, Qt::TextWordWrap, text).height() > summary.height())
        text = metrics.elidedText(text, Qt::ElideRight, summary.width() * 2 - 30);
    p.drawText(summary, Qt::TextWordWrap | Qt::AlignTop | Qt::AlignLeft, text);
}

void AppCard::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
        Q_EMIT detailsRequested(m_app);
}

void AppCard::contextMenuEvent(QContextMenuEvent *event)
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::styleMenu(menu);
    menu->addAction(tr("Details"), this, [this] { Q_EMIT detailsRequested(m_app); });
    if (m_packages->isInstalled(m_app) && !m_packages->isBusy(m_app)) {
        menu->addAction(tr("Open"), this, [this] { m_packages->open(m_app); });
        menu->addAction(tr("Remove"), this, [this] { m_packages->remove(m_app); });
    }
    menu->popup(event->globalPos());
}

void AppCard::enterEvent(QEnterEvent *)
{
    m_hover = true;
    update();
}

void AppCard::leaveEvent(QEvent *)
{
    m_hover = false;
    update();
}

// ---------------------------------------------------------------- CardGrid

CardGrid::CardGrid(PackageManager *packages, QWidget *parent)
    : QWidget(parent)
    , m_packages(packages)
    , m_grid(new QGridLayout(this))
{
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(14);
}

void CardGrid::setApps(const QList<CatalogApp> &apps)
{
    qDeleteAll(m_cards);
    m_cards.clear();
    for (const CatalogApp &app : apps) {
        auto *card = new AppCard(app, m_packages, this);
        connect(card, &AppCard::detailsRequested, this, &CardGrid::detailsRequested);
        m_cards.append(card);
    }
    m_columns = 0;
    place();
}

void CardGrid::resizeEvent(QResizeEvent *) { place(); }

void CardGrid::place()
{
    const int columns = qBound(1, (width() + 14) / (260 + 14), 4);
    if (columns == m_columns && m_grid->count() == m_cards.size())
        return;
    m_columns = columns;
    for (AppCard *card : std::as_const(m_cards))
        m_grid->removeWidget(card);
    for (int c = 0; c < 4; ++c)
        m_grid->setColumnStretch(c, c < columns ? 1 : 0);
    for (int i = 0; i < m_cards.size(); ++i) {
        m_grid->addWidget(m_cards.at(i), i / columns, i % columns);
        m_cards.at(i)->show();
    }
}

QLabel *pageTitle(const QString &text, QWidget *parent, qreal px)
{
    auto *label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setFont(Ui::headingFont(px));
    setInk(label, Ui::Theme::current().text);
    return label;
}

QLabel *mutedText(const QString &text, QWidget *parent, qreal px)
{
    auto *label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setFont(Ui::bodyFont(px));
    label->setWordWrap(true);
    setInk(label, Ui::Theme::current().muted);
    return label;
}
