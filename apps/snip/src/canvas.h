// SPDX-License-Identifier: GPL-3.0-or-later
// Annotations in image pixels. Text, strokes, crop and raster blur share the
// same ordered undo/redo history. image() flattens the cropped result.
#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QPolygonF>
#include <QWidget>

class QPlainTextEdit;

class Canvas : public QWidget
{
    Q_OBJECT
public:
    enum class Tool { Pen, Highlighter, Arrow, Box, Crop, Text, Blur, Select };

    explicit Canvas(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    bool isEmpty() const { return m_base.isNull(); }
    // The picture with the markup, cropped.
    QImage image() const;

    void setTool(Tool tool);
    void setColor(const QColor &color);
    void setWidth(int width);
    void setTextSize(int size);
    QColor color() const { return m_color; }
    int strokeWidth() const { return m_width; }
    int textSize() const { return m_textSize; }
    bool textSelected() const;
    // Commit inline text before Copy, Save or closing. Crop requires Enter.
    void finishEditing();
    void addText(const QPointF &at, const QString &text);
    bool canUndo() const { return !m_history.isEmpty(); }
    bool canRedo() const { return !m_undone.isEmpty(); }
    void undo();
    void redo();

Q_SIGNALS:
    void changed();
    void selectionChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    struct Step
    {
        Tool tool;
        QColor color;
        QPolygonF points;  // the stroke, or the two corners
        int width = 4;
        int textSize = 28;
        QString text;
        bool operator==(const Step &other) const {
            return tool == other.tool && color == other.color && points == other.points
                && width == other.width && textSize == other.textSize && text == other.text;
        }
    };

    QRectF cropRect() const;
    QRectF target() const;
    QPointF toImage(const QPointF &widget) const;
    QPointF fromImage(const QPointF &image) const;
    QPointF bounded(const QPointF &image) const;
    static void paintStep(QPainter &painter, const Step &step, qreal scale);
    QImage rendered() const;
    void commit(const Step &step);
    void record(const QList<Step> &before);
    void select(int index);
    int hitObject(const QPointF &widget) const;
    int hitHandle(const QPointF &widget, const QPolygonF &handles) const;
    QPolygonF handles(const QRectF &rect) const;
    QPolygonF selectedHandles() const;
    void drawHandles(QPainter &painter, const QPolygonF &points) const;
    void beginText(const QPointF &at, int index = -1);
    void finishText(bool accept);
    void positionTextEditor();

    QImage m_base;
    QList<Step> m_steps;
    QList<QList<Step>> m_history;
    QList<QList<Step>> m_undone;
    Step m_current;
    bool m_drawing = false;
    Tool m_tool = Tool::Pen;
    QColor m_color;
    int m_width = 4;
    int m_textSize = 28;
    mutable QImage m_rendered;
    int m_selected = -1;
    int m_handle = -1;
    enum class Drag { None, Object, Crop };
    Drag m_drag = Drag::None;
    QPointF m_dragFrom;
    QList<Step> m_dragBefore;
    QRectF m_pendingCrop;
    QRectF m_cropBefore;
    QPlainTextEdit *m_textEditor;
    Step m_textDraft;
    int m_textIndex = -1;
    bool m_finishingText = false;
    bool m_preedit = false;
};
