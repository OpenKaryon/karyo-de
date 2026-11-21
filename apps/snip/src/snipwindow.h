// SPDX-License-Identifier: GPL-3.0-or-later
// Snipping Tool's window, after Windows': New (rectangle or full screen,
// now or after a few seconds), the markup tools and colors, Undo and Redo,
// Copy and Save. Editor captures stay in memory until explicitly copied or
// saved, so an image awaiting redaction is not published prematurely.
#pragma once

#include <karyo/ui/window.h>

class Canvas;
class QLabel;
class QStackedWidget;
class QSpinBox;

namespace Karyo::Ui {
class IconButton;
class PillButton;
}

class SnipWindow : public Karyo::Ui::KaryoWindow
{
    Q_OBJECT
public:
    explicit SnipWindow(QWidget *parent = nullptr);

    // A screenshot just taken and saved at path.
    void showCapture(const QImage &image, const QString &path, bool saved = true);
    bool openFile(const QString &path);
    void newSnip();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void takeSnip();
    void selectTool(int index);
    void copy();
    bool save();
    bool saveAs();
    void updateState();
    bool confirmDiscard();
    void chooseColor(const QColor &color);

    QStackedWidget *m_pages;
    Canvas *m_canvas;
    QLabel *m_path;
    Karyo::Ui::PillButton *m_mode;
    Karyo::Ui::PillButton *m_delay;
    QList<Karyo::Ui::IconButton *> m_tools;
    QList<QAbstractButton *> m_swatches;
    Karyo::Ui::IconButton *m_undo;
    Karyo::Ui::IconButton *m_redo;
    QWidget *m_editTools;
    QSpinBox *m_textSize;
    QSpinBox *m_strokeWidth;
    QList<int> m_toolKinds;
    int m_selectedTool = 0;

    QString m_file;
    int m_captureMode = 0;
    int m_delaySeconds = 0;
    bool m_dirty = false;
    QImage m_savedImage;
};
