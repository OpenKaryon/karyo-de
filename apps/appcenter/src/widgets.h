// SPDX-License-Identifier: GPL-3.0-or-later
// App Center's pieces (KaryonAppCenter.dc.html): app cards with their
// install/open button and progress, pill buttons, tags and section headings.
#pragma once

#include "packages.h"

#include <QAbstractButton>
#include <QLabel>
#include <QWidget>

class QGridLayout;

// Primary (accent, IBM Plex Sans) or outlined pill; shows progress when set.
class Pill : public QAbstractButton
{
    Q_OBJECT
public:
    Pill(const QString &text, bool primary, QWidget *parent = nullptr, const QString &glyph = {});
    void setPrimary(bool primary);
    // 0-100 fills the pill as a progress bar; -1 is a plain button.
    void setProgress(int percent);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QFont labelFont() const;

    QString m_glyph;
    bool m_primary;
    int m_progress = -1;
};

// The source of a package as a small mono tag: bioconda, apt, flatpak...
class SourceTag : public QWidget
{
public:
    explicit SourceTag(const QString &source, QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_source;
};

// The 46px mono tile of an app: "sal", "IGV".
void paintAppTile(QPainter &painter, const QRectF &rect, const CatalogApp &app);

// A card of the grids: tile, name and version, two lines of summary, the
// source tag and the action. A click elsewhere on the card opens details.
class AppCard : public QWidget
{
    Q_OBJECT
public:
    AppCard(const CatalogApp &app, PackageManager *packages, QWidget *parent = nullptr);
    const CatalogApp &app() const { return m_app; }

Q_SIGNALS:
    void detailsRequested(const CatalogApp &app);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *) override { sync(); }

private:
    void sync();

    CatalogApp m_app;
    PackageManager *m_packages;
    Pill *m_action;
    bool m_hover = false;
};

// Cards in as many columns as fit (at least 260px each).
class CardGrid : public QWidget
{
    Q_OBJECT
public:
    CardGrid(PackageManager *packages, QWidget *parent = nullptr);
    void setApps(const QList<CatalogApp> &apps);

Q_SIGNALS:
    void detailsRequested(const CatalogApp &app);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void place();

    PackageManager *m_packages;
    QGridLayout *m_grid;
    QList<AppCard *> m_cards;
    int m_columns = 0;
};

QLabel *pageTitle(const QString &text, QWidget *parent = nullptr, qreal px = 30);
QLabel *mutedText(const QString &text, QWidget *parent = nullptr, qreal px = 13);
