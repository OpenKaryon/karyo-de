// SPDX-License-Identifier: GPL-3.0-or-later
#include "karyo/ui/style.h"

#include "karyo/ui/clientframe.h"
#include "karyo/ui/icons.h"
#include "karyo/ui/paint.h"
#include "karyo/ui/theme.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSlider>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTabBar>
#include <QToolButton>

namespace Karyo::Ui {

namespace {

constexpr int ControlHeight = 32;
constexpr qreal ItemRadius = 6;
constexpr qreal CardRadius = 8;
constexpr int ScrollBarExtent = 12;

bool isEnabled(const QStyleOption *option) { return option->state & QStyle::State_Enabled; }
bool isHovered(const QStyleOption *option) { return option->state & QStyle::State_MouseOver; }
bool isPressed(const QStyleOption *option)
{
    return option->state & (QStyle::State_Sunken | QStyle::State_On);
}

QColor disabled(QColor color, const QStyleOption *option)
{
    return isEnabled(option) ? color : alpha(color, 45);
}

qreal pillRadius(const QRectF &rect) { return qMin(rect.width(), rect.height()) / 2.0; }

// Lucide glyph centered in rect, tinted with color.
void drawGlyph(QPainter *painter, const QRectF &rect, const QString &name, int size,
               const QColor &color)
{
    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    const QPixmap pixmap = icon(name, size, color, dpr);
    painter->drawPixmap(QPointF(rect.center().x() - size / 2.0, rect.center().y() - size / 2.0),
                        pixmap);
}

void drawCheckMark(QPainter *painter, const QRectF &box, const QColor &color)
{
    QPainterPath path;
    path.moveTo(box.left() + box.width() * 0.25, box.top() + box.height() * 0.53);
    path.lineTo(box.left() + box.width() * 0.43, box.top() + box.height() * 0.70);
    path.lineTo(box.left() + box.width() * 0.76, box.top() + box.height() * 0.32);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(color, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path);
    painter->restore();
}

// Rounds only the outer corners of a row split across several columns.
void fillRowSegment(QPainter *painter, const QRectF &rect, qreal radius, const QColor &color,
                    QStyleOptionViewItem::ViewItemPosition position)
{
    using P = QStyleOptionViewItem;
    if (position == P::OnlyOne || position == P::Invalid) {
        fillRounded(*painter, rect, radius, color);
        return;
    }
    QPainterPath path;
    const bool roundLeft = position == P::Beginning;
    const bool roundRight = position == P::End;
    QRectF r = rect;
    path.addRoundedRect(r, radius, radius);
    QPainterPath square;
    if (!roundLeft)
        square.addRect(QRectF(r.left(), r.top(), r.width() / 2, r.height()));
    if (!roundRight)
        square.addRect(QRectF(r.center().x(), r.top(), r.width() / 2, r.height()));
    path = path.united(square);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawPath(path);
    painter->restore();
}

bool insideComposite(const QWidget *widget)
{
    if (!widget)
        return false;
    const QWidget *parent = widget->parentWidget();
    return qobject_cast<const QComboBox *>(parent) || qobject_cast<const QAbstractSpinBox *>(parent);
}

} // namespace

QPalette palette()
{
    const Theme &t = Theme::current();
    const bool dark = t.scheme == ColorScheme::Dark;
    QPalette p;
    auto set = [&p](QPalette::ColorRole role, const QColor &color) {
        p.setColor(QPalette::Active, role, color);
        p.setColor(QPalette::Inactive, role, color);
        p.setColor(QPalette::Disabled, role, alpha(color, color.alpha() == 255 ? 45 : 100));
    };
    const QColor highlight = mix(t.accent, t.win, dark ? 24 : 16);
    set(QPalette::Window, t.win);
    set(QPalette::WindowText, t.text);
    set(QPalette::Base, t.win);
    set(QPalette::AlternateBase, t.card);
    set(QPalette::Text, t.text);
    set(QPalette::PlaceholderText, t.muted);
    set(QPalette::Button, t.card);
    set(QPalette::ButtonText, t.text);
    set(QPalette::BrightText, neutral(100));
    set(QPalette::Highlight, highlight);
    set(QPalette::HighlightedText, t.selInk);
    set(QPalette::ToolTipBase, neutral(900));
    set(QPalette::ToolTipText, neutral(100));
    set(QPalette::Link, t.ink1);
    set(QPalette::LinkVisited, t.ink2);
    set(QPalette::Accent, t.accent);
    set(QPalette::Light, dark ? neutral(700) : neutral(100));
    set(QPalette::Midlight, dark ? neutral(800) : neutral(200));
    set(QPalette::Mid, dark ? neutral(700) : neutral(300));
    set(QPalette::Dark, dark ? neutral(900) : neutral(500));
    set(QPalette::Shadow, neutral(900));
    // Disabled text stays legible at the design's 45% opacity.
    p.setColor(QPalette::Disabled, QPalette::WindowText, alpha(t.text, 45));
    p.setColor(QPalette::Disabled, QPalette::Text, alpha(t.text, 45));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, alpha(t.text, 45));
    return p;
}

KaryoStyle::KaryoStyle()
    : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion")))
{
    setObjectName(QStringLiteral("Karyo"));
}

QPalette KaryoStyle::standardPalette() const { return palette(); }

void KaryoStyle::polish(QApplication *app)
{
    QProxyStyle::polish(app);
    app->setPalette(palette());
    QFont font = app->font();
    font.setFamilies({QStringLiteral("IBM Plex Sans"), QStringLiteral("Noto Sans"),
                      QStringLiteral("DejaVu Sans")});
    if (font.pointSizeF() <= 0 || font.pointSizeF() < 9.5)
        font.setPointSizeF(10);
    app->setFont(font);
    if ((qEnvironmentVariableIsSet("KARYO_CLIENT_FRAME")
             ? qEnvironmentVariableIntValue("KARYO_CLIENT_FRAME")
             : qEnvironmentVariableIntValue("OPENKARYON_CLIENT_FRAME")) > 0)
        installClientFrames(app);
}

void KaryoStyle::polish(QWidget *widget)
{
    QProxyStyle::polish(widget);
    if (qobject_cast<QAbstractButton *>(widget) || qobject_cast<QComboBox *>(widget)
        || qobject_cast<QTabBar *>(widget) || qobject_cast<QScrollBar *>(widget)
        || qobject_cast<QSlider *>(widget) || qobject_cast<QAbstractSpinBox *>(widget)
        || qobject_cast<QLineEdit *>(widget) || qobject_cast<QHeaderView *>(widget)) {
        widget->setAttribute(Qt::WA_Hover);
    }
    if (auto *view = qobject_cast<QAbstractItemView *>(widget))
        view->viewport()->setAttribute(Qt::WA_Hover);
    if (auto *header = qobject_cast<QHeaderView *>(widget)) {
        // Column headers sit on the view's ground, without a band.
        header->viewport()->setAutoFillBackground(false);
        QPalette p = header->palette();
        p.setColor(QPalette::Button, Qt::transparent);
        p.setColor(QPalette::Window, Qt::transparent);
        header->setPalette(p);
        header->setFont(bodyFont(12.5, QFont::DemiBold));
    }
    if (qobject_cast<QMenu *>(widget) && !widget->testAttribute(Qt::WA_WState_Created)) {
        // Rounded menus need an alpha channel around their corners.
        widget->setAttribute(Qt::WA_TranslucentBackground);
        widget->setWindowFlag(Qt::NoDropShadowWindowHint);
    }
}

void KaryoStyle::unpolish(QWidget *widget) { QProxyStyle::unpolish(widget); }

int KaryoStyle::pixelMetric(PixelMetric metric, const QStyleOption *option,
                            const QWidget *widget) const
{
    switch (metric) {
    case PM_ScrollBarExtent:
        return ScrollBarExtent;
    case PM_ScrollBarSliderMin:
        return 40;
    case PM_DefaultFrameWidth:
        return 1;
    case PM_MenuHMargin:
    case PM_MenuVMargin:
        return 6;
    case PM_MenuPanelWidth:
        return 1;
    case PM_SubMenuOverlap:
        return -4;
    case PM_IndicatorWidth:
    case PM_IndicatorHeight:
    case PM_ExclusiveIndicatorWidth:
    case PM_ExclusiveIndicatorHeight:
        return 18;
    case PM_CheckBoxLabelSpacing:
    case PM_RadioButtonLabelSpacing:
        return 10;
    case PM_TabBarTabHSpace:
        return 28;
    case PM_TabBarTabVSpace:
        return 14;
    case PM_HeaderMargin:
        return 8;
    case PM_SliderThickness:
    case PM_SliderControlThickness:
    case PM_SliderLength:
        return 22;
    case PM_SplitterWidth:
        return 6;
    case PM_ToolBarItemSpacing:
        return 4;
    case PM_ToolBarFrameWidth:
    case PM_ToolBarItemMargin:
        return 0;
    case PM_ButtonShiftHorizontal:
    case PM_ButtonShiftVertical:
        return 0;
    default:
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
}

int KaryoStyle::styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                          QStyleHintReturn *returnData) const
{
    switch (hint) {
    case SH_ItemView_ShowDecorationSelected:
    case SH_ScrollBar_LeftClickAbsolutePosition:
    case SH_Menu_Scrollable:
    case SH_ComboBox_AllowWheelScrolling:
        return 1;
    case SH_ComboBox_Popup:
    case SH_EtchDisabledText:
    case SH_DitherDisabledText:
    case SH_DialogButtonBox_ButtonsHaveIcons:
    case SH_ScrollView_FrameOnlyAroundContents:
        return 0;
    case SH_Widget_Animation_Duration:
        return 150;
    case SH_ToolTipLabel_Opacity:
        return 255;
    default:
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
}

QSize KaryoStyle::sizeFromContents(ContentsType type, const QStyleOption *option,
                                   const QSize &size, const QWidget *widget) const
{
    QSize s = QProxyStyle::sizeFromContents(type, option, size, widget);
    switch (type) {
    case CT_PushButton:
        s.setWidth(s.width() + 12);
        s.setHeight(qMax(s.height(), ControlHeight));
        break;
    case CT_LineEdit:
        if (!insideComposite(widget)) {
            s.setWidth(s.width() + 20);
            s.setHeight(qMax(s.height(), ControlHeight));
        }
        break;
    case CT_ComboBox:
        s.setWidth(s.width() + 20);
        s.setHeight(qMax(s.height(), ControlHeight));
        break;
    case CT_SpinBox:
        s.setWidth(s.width() + 16);
        s.setHeight(qMax(s.height(), ControlHeight));
        break;
    case CT_ItemViewItem:
        s.setHeight(qMax(s.height(), 32));
        break;
    case CT_MenuItem:
        if (const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
            item && item->menuItemType == QStyleOptionMenuItem::Separator) {
            s.setHeight(9);
        } else {
            s.setHeight(qMax(s.height(), 32));
            s.setWidth(s.width() + 16);
        }
        break;
    case CT_TabBarTab:
        s.setHeight(qMax(s.height(), ControlHeight));
        break;
    case CT_HeaderSection:
        s.setHeight(qMax(s.height(), 28));
        break;
    default:
        break;
    }
    return s;
}

QRect KaryoStyle::subElementRect(SubElement element, const QStyleOption *option,
                                 const QWidget *widget) const
{
    if (element == SE_LineEditContents && !insideComposite(widget)) {
        const QRect base = QProxyStyle::subElementRect(element, option, widget);
        return base.adjusted(10, 0, -10, 0);
    }
    return QProxyStyle::subElementRect(element, option, widget);
}

QRect KaryoStyle::subControlRect(ComplexControl control, const QStyleOptionComplex *option,
                                 SubControl subControl, const QWidget *widget) const
{
    if (control == CC_ScrollBar) {
        // Arrow-less scrollbars: the groove is the whole bar.
        const auto *bar = qstyleoption_cast<const QStyleOptionSlider *>(option);
        if (!bar)
            return QProxyStyle::subControlRect(control, option, subControl, widget);
        const QRect r = bar->rect;
        const bool horizontal = bar->orientation == Qt::Horizontal;
        const int length = horizontal ? r.width() : r.height();
        const int range = bar->maximum - bar->minimum;
        const int minSlider = pixelMetric(PM_ScrollBarSliderMin, option, widget);
        int sliderLength = range <= 0 ? length
                                      : int(qint64(length) * bar->pageStep
                                            / (qint64(range) + bar->pageStep));
        sliderLength = qBound(qMin(minSlider, length), sliderLength, length);
        const int start = sliderPositionFromValue(bar->minimum, bar->maximum, bar->sliderPosition,
                                                  length - sliderLength, bar->upsideDown);
        auto span = [&](int from, int size) {
            return horizontal ? QRect(r.left() + from, r.top(), size, r.height())
                              : QRect(r.left(), r.top() + from, r.width(), size);
        };
        switch (subControl) {
        case SC_ScrollBarGroove:
            return r;
        case SC_ScrollBarSlider:
            return span(start, sliderLength);
        case SC_ScrollBarSubPage:
            return span(0, start);
        case SC_ScrollBarAddPage:
            return span(start + sliderLength, length - start - sliderLength);
        case SC_ScrollBarAddLine:
        case SC_ScrollBarSubLine:
        case SC_ScrollBarFirst:
        case SC_ScrollBarLast:
            return {};
        default:
            break;
        }
    }
    if (control == CC_ComboBox) {
        const QRect r = option->rect;
        switch (subControl) {
        case SC_ComboBoxArrow:
            return QRect(r.right() - 34, r.top(), 34, r.height());
        case SC_ComboBoxEditField:
            return r.adjusted(14, 2, -36, -2);
        case SC_ComboBoxFrame:
            return r;
        default:
            break;
        }
    }
    if (control == CC_Slider && subControl == SC_SliderHandle) {
        const QRect handle = QProxyStyle::subControlRect(control, option, subControl, widget);
        const auto *slider = qstyleoption_cast<const QStyleOptionSlider *>(option);
        if (!slider)
            return handle;
        const QRect area = option->rect;
        const int extent = qMin(22, qMin(area.width(), area.height()));
        const bool horizontal = slider->orientation == Qt::Horizontal;
        const int length = horizontal ? area.width() : area.height();
        const int offset = sliderPositionFromValue(slider->minimum, slider->maximum, slider->sliderPosition,
                                                   length - extent, slider->upsideDown);
        return horizontal ? QRect(area.x() + offset, area.y() + (area.height() - extent) / 2, extent, extent)
                          : QRect(area.x() + (area.width() - extent) / 2, area.y() + offset, extent, extent);
    }
    return QProxyStyle::subControlRect(control, option, subControl, widget);
}

void KaryoStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                               QPainter *painter, const QWidget *widget) const
{
    const Theme &t = Theme::current();
    const QRectF r = option->rect;
    switch (element) {
    case PE_PanelButtonCommand: {
        const auto *button = qstyleoption_cast<const QStyleOptionButton *>(option);
        const bool primary = button && (button->features & QStyleOptionButton::DefaultButton);
        const bool flat = button && (button->features & QStyleOptionButton::Flat);
        QColor fill;
        QColor border = Qt::transparent;
        if (primary) {
            fill = isPressed(option) ? accent(700) : isHovered(option) ? accent(600) : t.accent;
        } else if (flat) {
            fill = isPressed(option) ? t.press : isHovered(option) ? t.hover : Qt::transparent;
        } else {
            fill = isPressed(option) ? mix(t.text, t.card, 13)
                                     : isHovered(option) ? mix(t.text, t.card, 6) : t.card;
            border = t.edge;
        }
        fillRounded(*painter, r, pillRadius(r), disabled(fill, option), disabled(border, option));
        return;
    }
    case PE_PanelButtonTool:
    case PE_PanelButtonBevel: {
        QColor fill = Qt::transparent;
        if (isPressed(option))
            fill = t.press;
        else if (isHovered(option) && isEnabled(option))
            fill = t.hover;
        fillRounded(*painter, r, qMin(pillRadius(r), 14.0), fill);
        return;
    }
    case PE_FrameDefaultButton:
    case PE_FrameFocusRect:
    case PE_FrameButtonTool:
    case PE_FrameStatusBarItem:
    case PE_PanelMenuBar:
    case PE_PanelToolBar:
    case PE_FrameDockWidget:
    case PE_IndicatorDockWidgetResizeHandle:
        return;
    case PE_PanelLineEdit: {
        if (insideComposite(widget))
            return;
        const auto *frame = qstyleoption_cast<const QStyleOptionFrame *>(option);
        if (frame && frame->lineWidth <= 0) {
            painter->fillRect(r, option->palette.base());
            return;
        }
        const bool focus = option->state & State_HasFocus;
        fillRounded(*painter, r, pillRadius(r), disabled(t.card, option),
                    focus ? Qt::transparent : t.edge);
        if (focus) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(QPen(t.accent, 2));
            painter->setBrush(Qt::NoBrush);
            const QRectF ring = r.adjusted(1, 1, -1, -1);
            painter->drawRoundedRect(ring, pillRadius(ring), pillRadius(ring));
            painter->restore();
        }
        return;
    }
    case PE_FrameLineEdit:
        return;
    case PE_PanelItemViewItem: {
        const auto *item = qstyleoption_cast<const QStyleOptionViewItem *>(option);
        const auto position = item ? item->viewItemPosition : QStyleOptionViewItem::OnlyOne;
        QColor fill = Qt::transparent;
        if (option->state & State_Selected)
            fill = t.sel;
        else if (isHovered(option) && isEnabled(option))
            fill = t.hover;
        if (fill.alpha())
            fillRowSegment(painter, r.adjusted(0, 1, 0, -1), ItemRadius, fill, position);
        return;
    }
    case PE_PanelItemViewRow: {
        const auto *item = qstyleoption_cast<const QStyleOptionViewItem *>(option);
        if (item && (item->features & QStyleOptionViewItem::Alternate))
            painter->fillRect(r, alpha(t.text, 3));
        return;
    }
    case PE_IndicatorCheckBox:
    case PE_IndicatorItemViewItemCheck: {
        const QRectF box = QRectF(r.center().x() - 9, r.center().y() - 9, 18, 18);
        const bool on = option->state & State_On;
        const bool partial = option->state & State_NoChange;
        if (on || partial) {
            fillRounded(*painter, box, 6, disabled(isHovered(option) ? accent(600) : t.accent, option));
            if (on) {
                drawCheckMark(painter, box, neutral(100));
            } else {
                painter->fillRect(QRectF(box.left() + 4.5, box.center().y() - 1, 9, 2),
                                  neutral(100));
            }
        } else {
            fillRounded(*painter, box, 6, disabled(t.win, option),
                        disabled(isHovered(option) ? t.accent : alpha(t.text, 35), option));
        }
        return;
    }
    case PE_IndicatorRadioButton: {
        const QRectF box = QRectF(r.center().x() - 9, r.center().y() - 9, 18, 18);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        if (option->state & State_On) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(disabled(t.accent, option));
            painter->drawEllipse(box);
            painter->setBrush(neutral(100));
            painter->drawEllipse(box.adjusted(5.5, 5.5, -5.5, -5.5));
        } else {
            painter->setPen(QPen(disabled(isHovered(option) ? t.accent : alpha(t.text, 35), option), 1));
            painter->setBrush(disabled(t.win, option));
            painter->drawEllipse(box.adjusted(0.5, 0.5, -0.5, -0.5));
        }
        painter->restore();
        return;
    }
    case PE_FrameMenu:
        return;
    case PE_PanelMenu: {
        QColor fill = t.glassStrong;
        fill.setAlpha(255);
        painter->save();
        painter->setCompositionMode(QPainter::CompositionMode_Source);
        painter->fillRect(r, Qt::transparent);
        painter->restore();
        fillRounded(*painter, r, CardRadius, fill, t.edge);
        return;
    }
    case PE_FrameTabWidget:
    case PE_FrameGroupBox:
        fillRounded(*painter, r, CardRadius, Qt::transparent, t.edge);
        return;
    case PE_Frame:
    case PE_FrameWindow:
        fillRounded(*painter, r, ItemRadius, Qt::transparent, t.edge);
        return;
    case PE_IndicatorArrowDown:
    case PE_IndicatorArrowUp:
    case PE_IndicatorArrowLeft:
    case PE_IndicatorArrowRight: {
        const char *name = element == PE_IndicatorArrowDown ? "chevron-down"
                           : element == PE_IndicatorArrowUp ? "chevron-up"
                           : element == PE_IndicatorArrowLeft ? "arrow-left"
                                                             : "chevron-right";
        const int size = qBound(10, int(qMin(r.width(), r.height())), 16);
        drawGlyph(painter, r, QString::fromLatin1(name), size,
                  disabled(option->palette.color(QPalette::ButtonText), option));
        return;
    }
    case PE_IndicatorBranch: {
        if (!(option->state & State_Children))
            return;
        const bool open = option->state & State_Open;
        drawGlyph(painter, r, open ? QStringLiteral("chevron-down") : QStringLiteral("chevron-right"),
                  14, t.muted);
        return;
    }
    case PE_IndicatorHeaderArrow: {
        const auto *header = qstyleoption_cast<const QStyleOptionHeader *>(option);
        if (!header || header->sortIndicator == QStyleOptionHeader::None)
            return;
        drawGlyph(painter, r,
                  header->sortIndicator == QStyleOptionHeader::SortUp ? QStringLiteral("chevron-up")
                                                                      : QStringLiteral("chevron-down"),
                  12, t.muted);
        return;
    }
    case PE_PanelTipLabel:
        painter->fillRect(r, neutral(900));
        return;
    case PE_IndicatorToolBarSeparator: {
        const bool horizontal = option->state & State_Horizontal;
        painter->fillRect(horizontal ? QRectF(r.center().x(), r.top() + 6, 1, r.height() - 12)
                                     : QRectF(r.left() + 6, r.center().y(), r.width() - 12, 1),
                          t.edge);
        return;
    }
    case PE_PanelStatusBar:
        return;
    default:
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
}

void KaryoStyle::drawControl(ControlElement element, const QStyleOption *option,
                             QPainter *painter, const QWidget *widget) const
{
    const Theme &t = Theme::current();
    const QRectF r = option->rect;
    switch (element) {
    case CE_PushButtonLabel: {
        const auto *button = qstyleoption_cast<const QStyleOptionButton *>(option);
        if (button && (button->features & QStyleOptionButton::DefaultButton)) {
            QStyleOptionButton copy = *button;
            copy.palette.setColor(QPalette::ButtonText, neutral(100));
            QProxyStyle::drawControl(element, &copy, painter, widget);
            return;
        }
        break;
    }
    case CE_HeaderSection: {
        if (isHovered(option) && isEnabled(option))
            fillRounded(*painter, r.adjusted(1, 3, -1, -3), 10, t.hover);
        return;
    }
    case CE_HeaderEmptyArea:
        return;
    case CE_HeaderLabel: {
        const auto *header = qstyleoption_cast<const QStyleOptionHeader *>(option);
        if (!header)
            break;
        QStyleOptionHeader copy = *header;
        copy.palette.setColor(QPalette::ButtonText, t.muted);
        const QFont font = bodyFont(12.5, QFont::DemiBold);
        painter->save();
        painter->setFont(font);
        QProxyStyle::drawControl(element, &copy, painter, widget);
        painter->restore();
        return;
    }
    case CE_MenuItem: {
        const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
        if (!item)
            break;
        if (item->menuItemType == QStyleOptionMenuItem::Separator) {
            painter->fillRect(QRectF(r.left() + 10, r.center().y(), r.width() - 20, 1), t.edge);
            return;
        }
        QStyleOptionMenuItem copy = *item;
        if ((item->state & State_Selected) && isEnabled(option)) {
            fillRounded(*painter, r.adjusted(0, 1, 0, -1), 10, t.hover);
            copy.state &= ~State_Selected;
        }
        copy.palette.setBrush(QPalette::Window, Qt::transparent);
        copy.palette.setBrush(QPalette::Button, Qt::transparent);
        QProxyStyle::drawControl(element, &copy, painter, widget);
        return;
    }
    case CE_MenuBarItem: {
        const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
        if (!item)
            break;
        if ((option->state & (State_Selected | State_Sunken)) && isEnabled(option))
            fillRounded(*painter, r.adjusted(1, 3, -1, -3), pillRadius(r.adjusted(1, 3, -1, -3)),
                        (option->state & State_Sunken) ? t.press : t.hover);
        QStyleOptionMenuItem copy = *item;
        copy.state &= ~(State_Selected | State_Sunken);
        copy.palette.setBrush(QPalette::Window, Qt::transparent);
        copy.palette.setBrush(QPalette::Button, Qt::transparent);
        QProxyStyle::drawControl(element, &copy, painter, widget);
        return;
    }
    case CE_MenuBarEmptyArea:
    case CE_MenuEmptyArea:
    case CE_ToolBar:
    case CE_SizeGrip:
        return;
    case CE_TabBarTabShape: {
        const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option);
        if (!tab)
            break;
        QColor fill = Qt::transparent;
        if (tab->state & State_Selected)
            fill = t.sel;
        else if (isHovered(option))
            fill = t.hover;
        fillRounded(*painter, r.adjusted(2, 3, -2, -3), pillRadius(r.adjusted(2, 3, -2, -3)), fill);
        return;
    }
    case CE_TabBarTabLabel: {
        const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option);
        if (tab && (tab->state & State_Selected)) {
            QStyleOptionTab copy = *tab;
            copy.palette.setColor(QPalette::WindowText, t.selInk);
            copy.palette.setColor(QPalette::ButtonText, t.selInk);
            QProxyStyle::drawControl(element, &copy, painter, widget);
            return;
        }
        break;
    }
    case CE_ProgressBarGroove:
        fillRounded(*painter, r, pillRadius(r), t.press);
        return;
    case CE_ProgressBarContents: {
        const auto *bar = qstyleoption_cast<const QStyleOptionProgressBar *>(option);
        if (!bar)
            break;
        const qint64 range = qint64(bar->maximum) - bar->minimum;
        qreal fraction = range > 0 ? qreal(bar->progress - bar->minimum) / range : 0.3;
        fraction = qBound(0.0, fraction, 1.0);
        QRectF fill = r;
        if (bar->state & State_Horizontal)
            fill.setWidth(qMax(r.height(), r.width() * fraction));
        else
            fill.setTop(r.bottom() - qMax(r.width(), r.height() * fraction));
        if (fraction > 0 || range <= 0)
            fillRounded(*painter, fill, pillRadius(fill), disabled(t.accent, option));
        return;
    }
    case CE_RubberBand:
        fillRounded(*painter, r, 6, t.tint1, alpha(t.accent, 60));
        return;
    case CE_Splitter:
        if (isHovered(option))
            painter->fillRect(r, t.hover);
        return;
    default:
        break;
    }
    QProxyStyle::drawControl(element, option, painter, widget);
}

void KaryoStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                                    QPainter *painter, const QWidget *widget) const
{
    const Theme &t = Theme::current();
    switch (control) {
    case CC_ScrollBar: {
        const auto *bar = qstyleoption_cast<const QStyleOptionSlider *>(option);
        if (!bar)
            break;
        const QRect slider = subControlRect(control, option, SC_ScrollBarSlider, widget);
        if (slider.isEmpty() || bar->maximum <= bar->minimum)
            return;
        const bool active = isHovered(option) || (bar->state & State_Sunken);
        // A thin pill that widens while the pointer is over the bar.
        const qreal inset = active ? 2.5 : 4;
        const QRectF handle = bar->orientation == Qt::Horizontal
                                  ? QRectF(slider).adjusted(2, inset, -2, -inset)
                                  : QRectF(slider).adjusted(inset, 2, -inset, -2);
        // The widget's ink, so dark surfaces (Terminal) get a light handle.
        const QColor ink = option->palette.color(QPalette::WindowText);
        const QColor color = (bar->activeSubControls & SC_ScrollBarSlider) && (bar->state & State_Sunken)
                                 ? alpha(ink, 45)
                                 : active ? alpha(ink, 32) : alpha(ink, 22);
        fillRounded(*painter, handle, pillRadius(handle), color);
        return;
    }
    case CC_ComboBox: {
        const auto *combo = qstyleoption_cast<const QStyleOptionComboBox *>(option);
        if (!combo)
            break;
        const QRectF r = option->rect;
        const bool focus = option->state & State_HasFocus;
        QColor fill = t.card;
        if (!combo->editable) {
            if (isPressed(option))
                fill = mix(t.text, t.card, 13);
            else if (isHovered(option))
                fill = mix(t.text, t.card, 6);
        }
        if (combo->frame)
            fillRounded(*painter, r, pillRadius(r), disabled(fill, option),
                        focus && combo->editable ? Qt::transparent : t.edge);
        if (focus && combo->editable) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(QPen(t.accent, 2));
            const QRectF ring = r.adjusted(1, 1, -1, -1);
            painter->drawRoundedRect(ring, pillRadius(ring), pillRadius(ring));
            painter->restore();
        }
        const QRect arrow = subControlRect(control, option, SC_ComboBoxArrow, widget);
        drawGlyph(painter, arrow, QStringLiteral("chevron-down"), 16,
                  disabled(option->palette.color(QPalette::ButtonText), option));
        return;
    }
    case CC_SpinBox: {
        const auto *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option);
        if (!spin)
            break;
        const QRectF r = option->rect;
        const bool focus = option->state & State_HasFocus;
        if (spin->frame)
            fillRounded(*painter, r, qMin(pillRadius(r), 18.0), disabled(t.card, option),
                        focus ? Qt::transparent : t.edge);
        if (focus) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(QPen(t.accent, 2));
            const QRectF ring = r.adjusted(1, 1, -1, -1);
            painter->drawRoundedRect(ring, qMin(pillRadius(ring), 17.0), qMin(pillRadius(ring), 17.0));
            painter->restore();
        }
        if (spin->buttonSymbols != QAbstractSpinBox::NoButtons) {
            const QRect up = subControlRect(control, option, SC_SpinBoxUp, widget);
            const QRect down = subControlRect(control, option, SC_SpinBoxDown, widget);
            const QColor ink = option->palette.color(QPalette::ButtonText);
            drawGlyph(painter, up, QStringLiteral("chevron-up"), 12,
                      (spin->stepEnabled & QAbstractSpinBox::StepUpEnabled) ? ink : alpha(ink, 35));
            drawGlyph(painter, down, QStringLiteral("chevron-down"), 12,
                      (spin->stepEnabled & QAbstractSpinBox::StepDownEnabled) ? ink : alpha(ink, 35));
        }
        return;
    }
    case CC_Slider: {
        const auto *slider = qstyleoption_cast<const QStyleOptionSlider *>(option);
        if (!slider)
            break;
        const QRect handle = subControlRect(control, option, SC_SliderHandle, widget);
        const QRectF r = option->rect;
        const bool horizontal = slider->orientation == Qt::Horizontal;
        // 8px track, accent fill up to the handle, 22px knob with a 5px accent ring.
        const QRectF track = horizontal ? QRectF(r.left() + 2, r.center().y() - 4, r.width() - 4, 8)
                                        : QRectF(r.center().x() - 4, r.top() + 2, 8, r.height() - 4);
        fillRounded(*painter, track, 4, t.press);
        QRectF filled = track;
        if (horizontal)
            filled.setRight(handle.center().x());
        else
            filled.setTop(handle.center().y());
        fillRounded(*painter, filled, 4, disabled(t.accent, option));
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(disabled(t.accent, option), 5));
        painter->setBrush(neutral(100));
        painter->drawEllipse(QRectF(handle).adjusted(2.5, 2.5, -2.5, -2.5));
        painter->restore();
        return;
    }
    default:
        break;
    }
    QProxyStyle::drawComplexControl(control, option, painter, widget);
}

void applyStyle()
{
    QApplication::setStyle(new KaryoStyle);
    QApplication::setPalette(palette());
    // Our main windows draw their own frame; labwc leaves karyo-* and legacy openkaryon-* app
    // ids undecorated, so their dialogs get the Karyo frame too.
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        installClientFrames(qApp);
    // Every Karyo app follows the theme live: the palette, colors set on
    // widgets' own palettes (the same tokens in the new look), and a repaint.
    // Karyo widgets that keep colors follow them themselves (Theme::follow).
    static bool connected = false;
    if (!connected) {
        connected = true;
        Theme::followConfiguration();
        QObject::connect(ThemeNotifier::instance(), &ThemeNotifier::changed, qApp, [] {
            QApplication::setPalette(palette());
            const QList<QWidget *> widgets = QApplication::allWidgets();
            for (QWidget *widget : widgets) {
                if (widget->testAttribute(Qt::WA_SetPalette)) {
                    QPalette p = widget->palette();
                    for (int group = 0; group < QPalette::NColorGroups; ++group) {
                        for (int role = 0; role < QPalette::NColorRoles; ++role) {
                            const auto g = QPalette::ColorGroup(group);
                            const auto r = QPalette::ColorRole(role);
                            p.setColor(g, r, Theme::follow(p.color(g, r)));
                        }
                    }
                    widget->setPalette(p);
                }
                // Colors written into style sheets (#rrggbb, #aarrggbb) too.
                const QString sheet = widget->styleSheet();
                if (sheet.contains(QLatin1Char('#'))) {
                    static const QRegularExpression hex(QStringLiteral("#(?:[0-9a-fA-F]{8}|[0-9a-fA-F]{6})\\b"));
                    QString followed;
                    qsizetype last = 0;
                    for (const QRegularExpressionMatch &match : hex.globalMatch(sheet)) {
                        const QColor color = Theme::follow(QColor::fromString(match.captured()));
                        followed += sheet.mid(last, match.capturedStart() - last)
                                    + color.name(match.capturedLength() == 9 ? QColor::HexArgb : QColor::HexRgb);
                        last = match.capturedEnd();
                    }
                    followed += sheet.mid(last);
                    if (followed != sheet)
                        widget->setStyleSheet(followed);
                }
                widget->update();
            }
        });
    }
}

} // namespace Karyo::Ui
