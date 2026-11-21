// SPDX-License-Identifier: GPL-3.0-or-later
// The sidebar's page thumbnails: rendered when they come into view, the
// current page outlined in the accent; a click goes to the page.
#pragma once

#include <QHash>
#include <QImage>
#include <QWidget>

class QPdfDocument;

class Thumbnails : public QWidget
{
    Q_OBJECT
public:
    Thumbnails(QPdfDocument *document, QWidget *parent = nullptr);
    void reset();
    void setCurrent(int page);
    QSize sizeHint() const override;
    // The vertical position of a page, for scrolling it into view.
    int pageTop(int page) const;

Q_SIGNALS:
    void activated(int page);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QRect pageRect(int page) const;
    int pageHeight(int page) const;

    QPdfDocument *m_document;
    QHash<int, QImage> m_cache;
    QList<int> m_tops;
    int m_current = 0;
};
