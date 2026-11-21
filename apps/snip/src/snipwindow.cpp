// SPDX-License-Identifier: GPL-3.0-or-later
#include "snipwindow.h"
#include "filemanager.h"
#include <karyo/ui/desktopentry.h>
#include <QDesktopServices>
#include <QUrl>

#include "canvas.h"
#include "capture.h"
#include "overlay.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/menu.h>
#include <karyo/ui/navigation.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QCloseEvent>
#include <QColorDialog>
#include <QSignalBlocker>
#include <QSettings>
#include <QSpinBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

namespace Ui = Karyo::Ui;

namespace {

QSettings snipSettings()
{
    return QSettings(QSettings::NativeFormat, QSettings::UserScope,
                     QStringLiteral("openkaryon"), QStringLiteral("openkaryon-snip"));
}


void setInk(QLabel *label, const QColor &color)
{
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, color);
    label->setPalette(palette);
}

// Scissors on sage, as on the taskbar and in Start.
class AppTile : public QWidget
{
public:
    explicit AppTile(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(30, 30);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        Ui::fillRounded(p, QRectF(rect()), 9, Ui::sage(600));
        p.drawPixmap(7, 7, Ui::icon(QStringLiteral("scissors"), 16, Ui::neutral(100), devicePixelRatioF()));
    }
};

// A color dot; the chosen one has a ring.
class Swatch : public QAbstractButton
{
public:
    Swatch(const QColor &color, QWidget *parent)
        : QAbstractButton(parent)
        , m_color(color)
    {
        setCheckable(true);
        setAutoExclusive(true);
        setFixedSize(30, 30);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
    }
    QColor color() const { return m_color; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (isChecked()) {
            p.setPen(QPen(t.text, 2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QRectF(rect()).adjusted(2, 2, -2, -2));
        }
        p.setPen(QPen(t.edge, 1));
        p.setBrush(m_color);
        p.drawEllipse(QRectF(rect()).adjusted(7, 7, -7, -7));
    }

private:
    QColor m_color;
};

// A thin upright rule between toolbar groups.
class Rule : public QWidget
{
public:
    explicit Rule(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(17, 28);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(QRect(8, 2, 1, height() - 4), Ui::Theme::current().edge);
    }
};

} // namespace

SnipWindow::SnipWindow(QWidget *parent)
    : KaryoWindow(parent)
    , m_pages(new QStackedWidget)
    , m_canvas(new Canvas)
{
    const Ui::Theme &t = Ui::Theme::current();
    setWindowTitle(tr("Snipping Tool"));
    setBodyColor(t.pane);
    titleLayout()->setContentsMargins(16, 0, 8, 0);
    titleLayout()->setSpacing(10);
    titleLayout()->addWidget(new AppTile);
    auto *appName = new QLabel(tr("Snipping Tool"));
    appName->setFont(Ui::bodyFont(14, QFont::DemiBold));
    titleLayout()->addWidget(appName);
    titleLayout()->addStretch(1);

    // Toolbar: New with its mode and delay, then the markup tools.
    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(14, 6, 14, 10);
    bar->setSpacing(4);
    auto *newButton = new Ui::PillButton(QStringLiteral("plus"), tr("New"));
    newButton->setKind(Ui::PillButton::Kind::Primary);
    newButton->setToolTip(tr("New screenshot (Ctrl+N)"));
    connect(newButton, &QAbstractButton::clicked, this, &SnipWindow::newSnip);
    bar->addWidget(newButton);
    m_mode = new Ui::PillButton(QStringLiteral("scan"), tr("Rectangle"));
    m_mode->setChevron(true);
    connect(m_mode, &QAbstractButton::clicked, this, [this] {
        auto *menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        Ui::styleMenu(menu);
        menu->addAction(tr("Rectangle"), this, [this] {
            m_captureMode = 0;
            m_mode->setText(tr("Rectangle"));
            m_mode->updateGeometry();
        });
        menu->addAction(tr("Full screen"), this, [this] {
            m_captureMode = 1;
            m_mode->setText(tr("Full screen"));
            m_mode->updateGeometry();
        });
        menu->addAction(tr("Window"), this, [this] {
            m_captureMode = 2;
            m_mode->setText(tr("Window"));
            m_mode->updateGeometry();
        });
        menu->popup(m_mode->mapToGlobal(QPoint(0, m_mode->height() + 4)));
    });
    bar->addWidget(m_mode);
    m_delay = new Ui::PillButton(QStringLiteral("timer"), tr("No delay"));
    m_delay->setChevron(true);
    connect(m_delay, &QAbstractButton::clicked, this, [this] {
        auto *menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        Ui::styleMenu(menu);
        for (const int seconds : {0, 3, 5, 10}) {
            const QString text = seconds ? tr("%1 seconds").arg(seconds) : tr("No delay");
            menu->addAction(text, this, [this, seconds, text] {
                m_delaySeconds = seconds;
                m_delay->setText(text);
                m_delay->updateGeometry();
            });
        }
        menu->popup(m_delay->mapToGlobal(QPoint(0, m_delay->height() + 4)));
    });
    bar->addWidget(m_delay);

    bar->addStretch(1);
    auto *keys = new QLabel(tr("Ctrl+Shift+1–4 · screenshots"));
    keys->setFont(Ui::bodyFont(12));
    setInk(keys, t.muted);
    keys->setToolTip(tr("1 — quick region\n2 — region + editor\n3 — full screen + editor\n4 — window + editor"));
    bar->addWidget(keys);
    m_editTools = new QWidget;
    auto *edit = new QHBoxLayout(m_editTools);
    edit->setContentsMargins(14, 0, 14, 8);
    edit->setSpacing(2);
    edit->addWidget(new Rule);
    struct ToolButton { Canvas::Tool tool; QString icon; QString hint; };
    const QList<ToolButton> tools = {
        {Canvas::Tool::Crop, QStringLiteral("crop"), tr("Crop (C)")},
        {Canvas::Tool::Select, QStringLiteral("mouse-pointer-2"), tr("Select / move (V)")},
        {Canvas::Tool::Pen, QStringLiteral("pencil"), tr("Pen (P)")},
        {Canvas::Tool::Highlighter, QStringLiteral("highlighter"), tr("Highlighter (H)")},
        {Canvas::Tool::Arrow, QStringLiteral("move-up-right"), tr("Arrow (A)")},
        {Canvas::Tool::Box, QStringLiteral("square"), tr("Box (B)")},
        {Canvas::Tool::Text, QStringLiteral("type"), tr("Text (T)")},
        {Canvas::Tool::Blur, QStringLiteral("droplet"), tr("Blur (R)")},
    };
    for (int i = 0; i < tools.size(); ++i) {
        auto *button = new Ui::IconButton(tools.at(i).icon, m_editTools, {38, 38}, 18);
        button->setObjectName(QStringLiteral("tool-%1").arg(int(tools.at(i).tool)));
        button->setHint(tools.at(i).hint);
        connect(button, &QAbstractButton::clicked, this, [this, i] { selectTool(i); });
        m_toolKinds.append(int(tools.at(i).tool));
        m_tools.append(button);
        edit->addWidget(button);
    }
    edit->addWidget(new Rule);
    // Fixed colors, whatever the accent: terracotta, sage, red, ink, white, yellow.
    for (const QColor &color : {QColor(0xc6, 0x71, 0x39), Ui::sage(600), QColor(0xd9, 0x3b, 0x3b), Ui::neutral(900),
                                Ui::neutral(100), QColor(0xf2, 0xc9, 0x4c)}) {
        auto *swatch = new Swatch(color, m_editTools);
        connect(swatch, &QAbstractButton::clicked, this, [this, color] { chooseColor(color); });
        m_swatches.append(swatch);
        edit->addWidget(swatch);
    }
    auto *customColor = new Ui::IconButton(QStringLiteral("palette"), m_editTools, {34, 34}, 18);
    customColor->setHint(tr("Choose color…"));
    connect(customColor, &QAbstractButton::clicked, this, [this] {
        const QColor current = m_canvas->color();
        const QColor color = QColorDialog::getColor(current, this, tr("Annotation color"));
        if (color.isValid())
            chooseColor(color);
    });
    edit->addWidget(customColor);
    auto *width = new QSpinBox(m_editTools);
    m_strokeWidth = width;
    width->setRange(1, 16);
    width->setValue(qBound(1, snipSettings().value(QStringLiteral("strokeWidth"), 4).toInt(), 16));
    width->setSuffix(tr(" px"));
    width->setToolTip(tr("Stroke width"));
    width->setMaximumWidth(82);
    m_canvas->setWidth(width->value());
    connect(width, &QSpinBox::valueChanged, this, [this](int value) {
        m_canvas->setWidth(value);
        snipSettings().setValue(QStringLiteral("strokeWidth"), value);
    });
    edit->addWidget(width);
    m_textSize = new QSpinBox(m_editTools);
    m_textSize->setRange(10, 144);
    m_textSize->setValue(qBound(10, snipSettings().value(QStringLiteral("textSize"), 28).toInt(), 144));
    m_textSize->setSuffix(tr(" px"));
    m_textSize->setToolTip(tr("Text size"));
    m_textSize->setMaximumWidth(90);
    m_canvas->setTextSize(m_textSize->value());
    connect(m_textSize, &QSpinBox::valueChanged, this, [this](int value) {
        m_canvas->setTextSize(value);
        snipSettings().setValue(QStringLiteral("textSize"), value);
    });
    edit->addWidget(m_textSize);
    edit->addWidget(new Rule);
    m_undo = new Ui::IconButton(QStringLiteral("undo-2"), m_editTools, {38, 38}, 18);
    m_undo->setHint(tr("Undo (Ctrl+Z)"));
    connect(m_undo, &QAbstractButton::clicked, m_canvas, &Canvas::undo);
    m_redo = new Ui::IconButton(QStringLiteral("redo-2"), m_editTools, {38, 38}, 18);
    m_redo->setHint(tr("Redo (Ctrl+Y)"));
    connect(m_redo, &QAbstractButton::clicked, m_canvas, &Canvas::redo);
    edit->addWidget(m_undo);
    edit->addWidget(m_redo);
    edit->addStretch(1);
    auto *copyButton = new Ui::PillButton(QStringLiteral("copy"), tr("Copy"), m_editTools);
    copyButton->setKind(Ui::PillButton::Kind::Secondary);
    connect(copyButton, &QAbstractButton::clicked, this, &SnipWindow::copy);
    auto *saveButton = new Ui::PillButton(QStringLiteral("save"), tr("Save"), m_editTools);
    saveButton->setKind(Ui::PillButton::Kind::Secondary);
    connect(saveButton, &QAbstractButton::clicked, this, &SnipWindow::save);
    auto *more = new Ui::IconButton(QStringLiteral("ellipsis"), m_editTools, {38, 38}, 18);
    more->setHint(tr("More"));
    connect(more, &QAbstractButton::clicked, this, [this, more] {
        auto *menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        Ui::styleMenu(menu);
        menu->addAction(tr("Save as…"), QKeySequence(QStringLiteral("Ctrl+Shift+S")), this, &SnipWindow::saveAs);
        menu->addAction(tr("Show in Files"), this, [this] {
            if (!m_file.isEmpty())
                Karyo::Apps::showInFiles(m_file, this);
        });
        menu->addAction(tr("Open in Photos"), this, [this] {
            if (save()) {
                const auto photos = DesktopEntry::find(QStringLiteral("karyo-photos.desktop"));
                if (!photos || !photos->launchWith({m_file}))
                    QDesktopServices::openUrl(QUrl::fromLocalFile(m_file));
            }
        });
        const QSize size = menu->sizeHint();
        menu->popup(more->mapToGlobal(QPoint(more->width() - size.width(), more->height() + 4)));
    });
    bar->addWidget(copyButton);
    bar->addWidget(saveButton);
    bar->addWidget(more);
    connect(m_canvas, &Canvas::changed, copyButton, [this, copyButton, saveButton] {
        copyButton->setEnabled(!m_canvas->isEmpty());
        saveButton->setEnabled(!m_canvas->isEmpty());
    });
    copyButton->setEnabled(false);
    saveButton->setEnabled(false);


    // The picture, or a hint while there is none.
    auto *empty = new QWidget;
    auto *emptyColumn = new QVBoxLayout(empty);
    emptyColumn->addStretch(1);
    auto *glyph = new QLabel;
    glyph->setPixmap(Ui::icon(QStringLiteral("scissors"), 56, t.muted, devicePixelRatioF()));
    glyph->setAlignment(Qt::AlignCenter);
    emptyColumn->addWidget(glyph);
    emptyColumn->addSpacing(12);
    auto *heading = new QLabel(tr("Take a screenshot"));
    heading->setFont(Ui::headingFont(26));
    heading->setAlignment(Qt::AlignCenter);
    emptyColumn->addWidget(heading);
    auto *hint = new QLabel(tr("Ctrl+Shift+1 — quick region · Ctrl+Shift+2 — region + editor\n"
                               "Ctrl+Shift+3 — screen + editor · Ctrl+Shift+4 — window + editor"));
    hint->setFont(Ui::bodyFont(14));
    hint->setAlignment(Qt::AlignCenter);
    setInk(hint, t.muted);
    emptyColumn->addWidget(hint);
    emptyColumn->addStretch(1);
    m_pages->addWidget(empty);
    m_pages->addWidget(m_canvas);

    auto *panel = new Ui::InsetPanel;
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->addWidget(m_pages);

    m_path = new QLabel;
    m_path->setFont(Ui::bodyFont(12.5));
    setInk(m_path, t.muted);
    m_path->setContentsMargins(6, 8, 6, 0);

    auto *body = new QWidget;
    auto *column = new QVBoxLayout(body);
    column->setContentsMargins(12, 0, 12, 12);
    column->setSpacing(0);
    column->addLayout(bar);
    column->addWidget(m_editTools);
    column->addWidget(panel, 1);
    column->addWidget(m_path);
    setCentralWidget(body);

    connect(m_canvas, &Canvas::changed, this, [this] {
        m_dirty = !m_canvas->isEmpty() && (m_savedImage.isNull() || m_canvas->image() != m_savedImage);
        updateState();
    });

    auto shortcut = [this](const QKeySequence &keys, auto slot) {
        auto *s = new QShortcut(keys, this);
        connect(s, &QShortcut::activated, this, slot);
    };
    shortcut(QKeySequence::New, [this] { newSnip(); });
    shortcut(QKeySequence::Copy, [this] { copy(); });
    shortcut(QKeySequence::Save, [this] { save(); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")), [this] { saveAs(); });
    shortcut(QKeySequence::Undo, [this] { m_canvas->undo(); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+Y")), [this] { m_canvas->redo(); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Z")), [this] { m_canvas->redo(); });
    const QList<QPair<Qt::Key, Canvas::Tool>> toolKeys = {
        {Qt::Key_P, Canvas::Tool::Pen}, {Qt::Key_H, Canvas::Tool::Highlighter},
        {Qt::Key_A, Canvas::Tool::Arrow}, {Qt::Key_B, Canvas::Tool::Box},
        {Qt::Key_C, Canvas::Tool::Crop}, {Qt::Key_T, Canvas::Tool::Text},
        {Qt::Key_R, Canvas::Tool::Blur}, {Qt::Key_V, Canvas::Tool::Select}};
    for (const auto &key : toolKeys)
        shortcut(QKeySequence(key.first), [this, tool = key.second] { selectTool(m_toolKinds.indexOf(int(tool))); });
    connect(m_canvas, &Canvas::selectionChanged, this, [this] {
        const QSignalBlocker widthBlocker(m_strokeWidth), sizeBlocker(m_textSize);
        m_strokeWidth->setValue(m_canvas->strokeWidth());
        m_textSize->setValue(m_canvas->textSize());
        m_textSize->setVisible(m_selectedTool == int(Canvas::Tool::Text) || m_canvas->textSelected());
        for (QAbstractButton *button : m_swatches) {
            button->setAutoExclusive(false);
            button->setChecked(static_cast<Swatch *>(button)->color() == m_canvas->color());
            button->setAutoExclusive(true);
        }
    });

    selectTool(m_toolKinds.indexOf(int(Canvas::Tool::Pen)));
    updateState();
    resizeToFit({1200, 820});
}

void SnipWindow::selectTool(int index)
{
    for (int i = 0; i < m_tools.size(); ++i)
        m_tools.at(i)->setActive(i == index);
    m_selectedTool = m_toolKinds.at(index);
    m_canvas->setTool(Canvas::Tool(m_selectedTool));
    chooseColor(snipSettings().value(QStringLiteral("colors/%1").arg(m_selectedTool), QColor(0xc6, 0x71, 0x39)).value<QColor>());
    m_textSize->setVisible(Canvas::Tool(m_selectedTool) == Canvas::Tool::Text);
}

void SnipWindow::chooseColor(const QColor &color)
{
    m_canvas->setColor(color);
    snipSettings().setValue(QStringLiteral("colors/%1").arg(m_selectedTool), color);
    for (QAbstractButton *button : m_swatches) {
        button->setAutoExclusive(false);
        button->setChecked(static_cast<Swatch *>(button)->color() == color);
        button->setAutoExclusive(true);
    }
}

void SnipWindow::updateState()
{
    const bool picture = !m_canvas->isEmpty();
    m_pages->setCurrentIndex(picture ? 1 : 0);
    m_editTools->setVisible(picture);
    m_undo->setEnabled(m_canvas->canUndo());
    m_redo->setEnabled(m_canvas->canRedo());
    m_path->setText(m_file.isEmpty() ? QString()
                    : m_dirty        ? tr("%1 · changed").arg(QDir::toNativeSeparators(m_file))
                                     : QDir::toNativeSeparators(m_file));
    setWindowTitle(m_file.isEmpty() ? tr("Snipping Tool")
                                    : QFileInfo(m_file).fileName() + QStringLiteral(" — ") + tr("Snipping Tool"));
}

void SnipWindow::showCapture(const QImage &image, const QString &path, bool saved)
{
    m_canvas->setImage(image);
    m_savedImage = saved ? m_canvas->image() : QImage();
    m_file = path;
    m_dirty = !saved;
    updateState();
}

bool SnipWindow::openFile(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull())
        return false;
    showCapture(image, QFileInfo(path).absoluteFilePath());
    return true;
}

void SnipWindow::newSnip()
{
    if (!confirmDiscard())
        return;
    // Out of the way first, so the window is not in its own picture.
    hide();
    QTimer::singleShot(qMax(350, m_delaySeconds * 1000), this, &SnipWindow::takeSnip);
}

void SnipWindow::takeSnip()
{
    auto finishWith = [this](const QImage &image) {
        showCapture(image, Capture::newPath(), false);
        show();
    };
    if (m_captureMode == 1) {
        QString error;
        const QImage image = Capture::grab(nullptr, &error);
        if (image.isNull()) {
            show();
            QMessageBox::warning(this, tr("Snipping Tool"), tr("The screen could not be captured: %1").arg(error));
            return;
        }
        finishWith(image);
        return;
    }
    auto *snipper = new Snipper(this);
    connect(snipper, &Snipper::captured, this, [snipper, finishWith](const QImage &image) {
        snipper->deleteLater();
        finishWith(image);
    });
    connect(snipper, &Snipper::cancelled, this, [this, snipper] {
        snipper->deleteLater();
        show();
    });
    if (!snipper->start(m_captureMode == 2)) {
        show();
        QMessageBox::warning(this, tr("Snipping Tool"), tr("The screen could not be captured: %1").arg(snipper->error()));
        snipper->deleteLater();
    }
}

void SnipWindow::copy()
{
    m_canvas->finishEditing();
    if (!m_canvas->isEmpty())
        Capture::copy(m_canvas->image());
}

bool SnipWindow::save()
{
    m_canvas->finishEditing();
    if (m_canvas->isEmpty())
        return false;
    if (m_file.isEmpty())
        return saveAs();
    QString error;
    if (!Capture::save(m_canvas->image(), m_file, &error)) {
        QMessageBox::warning(this, tr("Snipping Tool"), error);
        return false;
    }
    m_savedImage = m_canvas->image();
    m_dirty = false;
    updateState();
    return true;
}

bool SnipWindow::saveAs()
{
    m_canvas->finishEditing();
    if (m_canvas->isEmpty())
        return false;
    const QString suggested = m_file.isEmpty() ? Capture::newPath() : m_file;
    const QString path = QFileDialog::getSaveFileName(this, tr("Save as"), suggested, tr("Pictures (*.png *.jpg)"));
    if (path.isEmpty())
        return false;
    QString error;
    if (!Capture::save(m_canvas->image(), path, &error)) {
        QMessageBox::warning(this, tr("Snipping Tool"), error);
        return false;
    }
    m_file = path;
    m_savedImage = m_canvas->image();
    m_dirty = false;
    updateState();
    return true;
}

void SnipWindow::closeEvent(QCloseEvent *event)
{
    if (confirmDiscard())
        event->accept();
    else
        event->ignore();
}

bool SnipWindow::confirmDiscard()
{
    m_canvas->finishEditing();
    if (!m_dirty)
        return true;
    QMessageBox box(this);
    box.setWindowTitle(tr("Snipping Tool"));
    box.setText(tr("Save this screenshot?"));
    box.setInformativeText(tr("Unsaved changes will be lost."));
    QPushButton *saveButton = box.addButton(tr("Save"), QMessageBox::AcceptRole);
    QPushButton *discard = box.addButton(tr("Don't save"), QMessageBox::DestructiveRole);
    box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(saveButton);
    box.exec();
    return (box.clickedButton() == saveButton && save()) || box.clickedButton() == discard;
}
