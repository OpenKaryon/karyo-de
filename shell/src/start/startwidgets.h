// SPDX-License-Identifier: GPL-3.0-or-later
// Building blocks of Start (KaryonStart.dc.html): pills and chips, the pinned
// grid, recent files, the app list of a category, search results and the
// preview card. They paint themselves with the Karyo tokens.
#pragma once

#include "appindex.h"

#include <QAbstractButton>
#include <QDateTime>
#include <QWidget>

#include <functional>

class QLabel;
class QVBoxLayout;

// Outlined pill: "All apps ›", "Back".
class LinkButton : public QAbstractButton
{
    Q_OBJECT
public:
    LinkButton(const QString &text, const QString &leadingGlyph, bool chevron, QWidget *parent);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_glyph;
    bool m_chevron;
};

// Scope chip of the search layout; the checked chip is filled with the accent.
class ChipButton : public QAbstractButton
{
    Q_OBJECT
public:
    ChipButton(const QString &text, QWidget *parent);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
};

// 6-column grid of 46px app tiles with labels; dragging a tile reorders them.
class PinnedGrid : public QWidget
{
    Q_OBJECT
public:
    explicit PinnedGrid(QWidget *parent);
    void setApps(const QList<AppInfo> &apps);
    QSize sizeHint() const override;
    int heightForWidth(int width) const override;
    bool hasHeightForWidth() const override { return true; }

Q_SIGNALS:
    void activated(const AppInfo &app);
    void contextMenuRequested(const AppInfo &app, const QPoint &globalPos);
    // The tiles' desktop ids in their new order, after a drag.
    void reordered(const QStringList &ids);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QRect cellRect(int index) const;
    int cellAt(const QPoint &pos) const;
    // The slot a dragged tile goes to: the cell under pos, else the nearest.
    int dropIndexAt(const QPoint &pos) const;
    void paintTile(QPainter &painter, const AppInfo &app, const QRect &cell) const;

    QList<AppInfo> m_apps;
    int m_hover = -1;
    int m_pressed = -1;
    QPoint m_pressPos;
    int m_dragged = -1;  // index of the dragged tile while a drag is on
    QPoint m_dragPos;
};

// Two columns of recent files: type tile, name and when.
class RecentList : public QWidget
{
    Q_OBJECT
public:
    explicit RecentList(QWidget *parent);
    void setFiles(const QList<FileHit> &files);
    QSize sizeHint() const override;

Q_SIGNALS:
    void activated(const QString &path);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRect cellRect(int index) const;
    int cellAt(const QPoint &pos) const;

    QList<FileHit> m_files;
    int m_hover = -1;
};

// Apps of one category: tile, name, description and a GUI/CLI tag.
class AppList : public QWidget
{
    Q_OBJECT
public:
    explicit AppList(QWidget *parent);
    void setApps(const QList<AppInfo> &apps);
    QSize sizeHint() const override;

Q_SIGNALS:
    void activated(const AppInfo &app);
    void contextMenuRequested(const AppInfo &app, const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    int rowAt(const QPoint &pos) const;

    QList<AppInfo> m_apps;
    int m_hover = -1;
};

struct SearchResult
{
    enum Kind { App, Setting, Command, Run, File, Symbol };
    Kind kind = App;
    QString title;
    QString trailing;  // muted text at the right: category, folder, command dir
    AppInfo app;       // App, Setting
    QString command;   // Command (name), Run (the whole line), Symbol (the symbol)
    FileHit file;      // File
};

// Results under headings ("Best match", "Commands", "Files"...). The current
// row is filled with --k-sel and follows the arrow keys.
class ResultList : public QWidget
{
    Q_OBJECT
public:
    explicit ResultList(QWidget *parent);
    // sections: heading and its results; the first section's first row is current.
    void setResults(const QList<QPair<QString, QList<SearchResult>>> &sections, const QString &query, const QString &emptyText = QString());
    const SearchResult *current() const;
    void moveCurrent(int delta);
    bool isEmpty() const { return m_rows.isEmpty(); }
    QSize sizeHint() const override;

Q_SIGNALS:
    void currentChanged();
    void activated(const SearchResult &result);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Row
    {
        bool heading = false;
        bool best = false;
        QString text;  // heading text
        SearchResult result;
        int y = 0;
        int height = 0;
    };
    int rowAt(const QPoint &pos) const;

    QList<Row> m_rows;
    QString m_query;
    QString m_emptyText;
    int m_current = -1;
    int m_hover = -1;
};

// Right column of the search layout: big tile, name, facts and the actions.
class PreviewCard : public QWidget
{
    Q_OBJECT
public:
    PreviewCard(AppIndex *index, QWidget *parent);
    void setResult(const SearchResult *result);

Q_SIGNALS:
    // An action ran; Start closes.
    void done();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void addAction(const QString &glyph, const QString &text, bool primary,
                   const std::function<void()> &run);

    AppIndex *m_index;
    SearchResult m_result;
    bool m_valid = false;
    int m_headerHeight = 0;
    QVBoxLayout *m_layout;
    QList<QWidget *> m_parts;
};

// What Start's rows and actions do.
namespace StartActions {
// Apps start; installed tools open in a terminal, others in App Center.
void launch(const AppInfo &app);
// "samtools view", in the tool's conda environment when it lives only there.
QString toolCommandLine(const AppInfo &tool, const QString &arguments = QString());
void showInAppCenter(const QString &toolId);
void openFile(const QString &path);
void openLocation(const QString &path);
void runInTerminal(const QString &commandLine);
void readManual(const QString &command);
bool hasManual(const QString &command);
void copyPath(const QString &path);
// A symbol in its tile: µ, °C.
void drawSymbolTile(QPainter &painter, const QRectF &tile, const QString &symbol);
QString fileKind(const FileHit &file);
QString fileGlyph(const FileHit &file);
QString relativeTime(const QDateTime &when);
// Type tile colors for a file: fill and glyph ink.
QPair<QColor, QColor> fileTile(const FileHit &file);
} // namespace StartActions
