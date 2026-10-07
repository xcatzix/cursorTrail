/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * cursorTrailOn — based on WindTrail by Wesley (ProxyX).
 * Maintained by xcatzix.
 */

#include "cursortrailon_config.h"

#include <KConfigGroup>
#include <KPluginFactory>
#include <KSharedConfig>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPolygonF>
#include <QPushButton>
#include <QRadialGradient>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <utility>

K_PLUGIN_CLASS(KWin::CursorTrailOnConfig)

namespace KWin
{

namespace
{

constexpr int MaxTrailWidth = 80;
constexpr int MinTrailWidth = 2;
constexpr int MaxCaretWidth = 60;
constexpr int MinCaretWidth = 1;
constexpr int MaxCaretHeight = 120;
constexpr int MinCaretHeight = 4;
constexpr int MaxGradientStops = 6;
constexpr int MinGradientStops = 2;

// Default size of the settings page; the user can resize the window freely.
constexpr int DefaultPageWidth = 560;
constexpr int DefaultPageHeight = 400;

QColor mixColors(const QColor &a, const QColor &b, qreal amount)
{
    const qreal t = std::clamp(amount, qreal(0.0), qreal(1.0));

    const auto interpolate = [t](qreal first, qreal second) {
        return first * (1.0 - t) + second * t;
    };

    return QColor::fromRgbF(
        interpolate(a.redF(), b.redF()),
        interpolate(a.greenF(), b.greenF()),
        interpolate(a.blueF(), b.blueF()),
        interpolate(a.alphaF(), b.alphaF()));
}

QString colorStyle(const QColor &color)
{
    const qreal luminance = 0.2126 * color.redF()
        + 0.7152 * color.greenF()
        + 0.0722 * color.blueF();

    const QString textColor = luminance > 0.58
        ? QStringLiteral("#151515")
        : QStringLiteral("#ffffff");

    // The :disabled rule matters: without it a disabled button keeps its
    // color and looks exactly like a clickable one.
    return QStringLiteral(
        "QPushButton {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid rgba(127,127,127,0.7);"
        "  border-radius: 6px;"
        "  padding: 4px 12px;"
        "}"
        "QPushButton:disabled {"
        "  background-color: rgba(127,127,127,0.18);"
        "  color: rgba(127,127,127,0.8);"
        "  border: 1px dashed rgba(127,127,127,0.5);"
        "}")
        .arg(color.name(QColor::HexRgb), textColor);
}

QString stopsToString(const QList<TrailGradientStop> &stops)
{
    QStringList parts;
    for (const TrailGradientStop &stop : stops) {
        parts << QStringLiteral("%1@%2")
                     .arg(stop.color.name(QColor::HexRgb))
                     .arg(stop.percent / 100.0, 0, 'f', 3);
    }
    return parts.join(QLatin1Char(';'));
}

QList<TrailGradientStop> stopsFromString(const QString &text)
{
    QList<TrailGradientStop> stops;
    const QStringList parts = text.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QStringList pair = part.trimmed().split(QLatin1Char('@'));
        const QColor color(pair.value(0).trimmed());
        if (!color.isValid()) {
            continue;
        }
        bool ok = false;
        const double position = pair.value(1).toDouble(&ok);

        TrailGradientStop stop;
        stop.color = color;
        stop.color.setAlpha(255);
        stop.percent = std::clamp(int(std::lround((ok ? position : 0.0) * 100.0)),
                                  0, 100);
        stops.push_back(stop);
        if (stops.size() >= MaxGradientStops) {
            break;
        }
    }
    std::stable_sort(stops.begin(), stops.end(),
                     [](const TrailGradientStop &a, const TrailGradientStop &b) {
                         return a.percent < b.percent;
                     });
    return stops;
}

// Scroll area that asks for a fixed default size, so the dialog opens at a
// compact height but can still be dragged larger or smaller.
class PageScrollArea final : public QScrollArea
{
public:
    using QScrollArea::QScrollArea;

    QSize sizeHint() const override
    {
        int width = DefaultPageWidth;
        if (widget()) {
            width = std::max(width,
                             widget()->sizeHint().width()
                                 + verticalScrollBar()->sizeHint().width()
                                 + 2 * frameWidth());
        }
        return QSize(width, DefaultPageHeight);
    }

    QSize minimumSizeHint() const override
    {
        return QSize(320, 160);
    }
};

} // namespace

class CursorTrailOnPreview final : public QFrame
{
public:
    struct Look {
        QColor main = QColor(255, 6, 12);
        QColor core = QColor(255, 185, 187);
        QColor glow = QColor(184, 4, 9);
        bool glowEnabled = true;
        qreal intensity = 1.0;

        bool gradientEnabled = false;
        bool gradientSmooth = true;
        QList<TrailGradientStop> stops;

        bool lightEnabled = false;
        qreal lightStrength = 1.0;
        qreal lightRadius = 1.0;
        bool headLight = false;
    };

    struct Appearance {
        Look pointer;
        Look caret;
        int trailWidth = 18;
        int caretWidth = 8;
        int caretHeight = 40;
        bool mouseEnabled = true;
        bool caretEnabled = true;
    };

    explicit CursorTrailOnPreview(QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setMinimumHeight(130);
        setFrameShape(QFrame::StyledPanel);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setAppearance(const Appearance &appearance)
    {
        m_appearance = appearance;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QFrame::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setClipRect(rect().adjusted(1, 1, -1, -1));

        const QRectF area = rect().adjusted(22, 18, -22, -18);
        if (area.width() <= 10 || area.height() <= 10) {
            return;
        }

        if (!m_appearance.mouseEnabled && !m_appearance.caretEnabled) {
            painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));
            painter.drawText(rect(), Qt::AlignCenter,
                             QStringLiteral("Both trails are turned off"));
            return;
        }

        QRectF mouseArea = area;
        QRectF caretArea = area;
        if (m_appearance.mouseEnabled && m_appearance.caretEnabled) {
            mouseArea.setWidth(area.width() * 0.62);
            caretArea = QRectF(area.left() + area.width() * 0.68,
                               area.top(),
                               area.width() * 0.32,
                               area.height());
        }

        if (m_appearance.mouseEnabled) {
            paintMouse(painter, mouseArea);
        }
        if (m_appearance.caretEnabled) {
            paintCaret(painter, caretArea);
        }
    }

private:
    static QColor withAlpha(const QColor &source, qreal alpha, qreal intensity)
    {
        QColor color = source;
        color.setAlphaF(std::clamp(alpha * intensity,
                                   qreal(0.0),
                                   qreal(1.0)));
        return color;
    }

    // Gradient position t: 0 = head (cursor), 1 = tail.
    static QColor gradientAt(const Look &look, qreal t)
    {
        const QList<TrailGradientStop> &stops = look.stops;
        if (stops.isEmpty()) {
            return look.main;
        }
        t = std::clamp(t, qreal(0.0), qreal(1.0));
        if (t <= stops.first().percent / 100.0) {
            return stops.first().color;
        }
        if (t >= stops.last().percent / 100.0) {
            return stops.last().color;
        }
        for (int i = 0; i + 1 < stops.size(); ++i) {
            const qreal a = stops[i].percent / 100.0;
            const qreal b = stops[i + 1].percent / 100.0;
            if (t >= a && t <= b) {
                if (!look.gradientSmooth) {
                    return stops[i].color;
                }
                return mixColors(stops[i].color, stops[i + 1].color,
                                 b > a ? (t - a) / (b - a) : 0.0);
            }
        }
        return stops.last().color;
    }

    static QColor mainAt(const Look &look, qreal t)
    {
        return look.gradientEnabled ? gradientAt(look, t) : look.main;
    }

    static QColor glowAt(const Look &look, qreal t)
    {
        return look.gradientEnabled
            ? mixColors(gradientAt(look, t), QColor(0, 0, 0), 0.28)
            : look.glow;
    }

    static QColor lightAt(const Look &look, qreal t)
    {
        return mixColors(mainAt(look, t), QColor(255, 255, 255), 0.35);
    }

    // Horizontal brush for a stroke that runs from the tail (left) to the head
    // (right), colored by `colorAt(t)` and with a constant alpha.
    template<typename ColorAt>
    static QBrush strokeBrush(const QRectF &area, ColorAt colorAt, qreal alpha)
    {
        QLinearGradient gradient(area.left(), 0, area.right(), 0);
        constexpr int Samples = 24;
        for (int i = 0; i <= Samples; ++i) {
            const qreal pos = qreal(i) / Samples;       // 0 = tail
            QColor color = colorAt(1.0 - pos);          // t = 1 - pos
            color.setAlphaF(alpha);
            gradient.setColorAt(pos, color);
        }
        return QBrush(gradient);
    }

    void paintMouse(QPainter &painter, const QRectF &area) const
    {
        const Look &look = m_appearance.pointer;

        QPainterPath path;
        path.moveTo(area.left(), area.center().y() + area.height() * 0.22);
        path.cubicTo(area.left() + area.width() * 0.22,
                     area.top() - area.height() * 0.02,
                     area.left() + area.width() * 0.56,
                     area.bottom() + area.height() * 0.08,
                     area.right(),
                     area.center().y() - area.height() * 0.18);

        const qreal body = m_appearance.trailWidth * 0.5;
        const qreal k = look.intensity;
        const qreal lk = look.lightStrength * look.intensity;
        const qreal lr = look.lightRadius;

        const auto drawBrush = [&](const QBrush &brush, qreal width) {
            QPen pen(brush, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(pen);
            painter.drawPath(path);
        };
        const auto drawLayer = [&](const QColor &color, qreal width) {
            drawBrush(QBrush(color), width);
        };

        // Light effect: wide additive-looking halos under the trail.
        if (look.lightEnabled) {
            const auto lightAtFn = [&](qreal t) { return lightAt(look, t); };
            painter.setCompositionMode(QPainter::CompositionMode_Plus);
            drawBrush(strokeBrush(area, lightAtFn, std::min(1.0, 0.020 * lk * 3.0)),
                      body * 6.0 * lr);
            drawBrush(strokeBrush(area, lightAtFn, std::min(1.0, 0.040 * lk * 3.0)),
                      body * 3.8 * lr);
            drawBrush(strokeBrush(area, lightAtFn, std::min(1.0, 0.075 * lk * 3.0)),
                      body * 2.2 * lr);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }

        if (look.glowEnabled) {
            drawBrush(strokeBrush(area,
                                  [&](qreal t) { return glowAt(look, t); },
                                  std::clamp(0.12 * k, 0.0, 1.0)),
                      body * 2.05);
        }
        drawBrush(strokeBrush(area,
                              [&](qreal t) { return mainAt(look, t); },
                              std::clamp(0.42 * k, 0.0, 1.0)),
                  body);
        drawLayer(withAlpha(look.core, 0.82, k),
                  std::max<qreal>(1.2, body * 0.27));

        if (look.lightEnabled && look.headLight) {
            const QPointF head = path.pointAtPercent(1.0);
            const qreal radius = std::max<qreal>(8.0, body * 1.8 * lr);
            QRadialGradient radial(head, radius);
            QColor c = lightAt(look, 0.0);
            c.setAlphaF(std::clamp(0.55 * lk, 0.0, 1.0));
            radial.setColorAt(0.0, c);
            c.setAlphaF(0.0);
            radial.setColorAt(1.0, c);
            painter.setPen(Qt::NoPen);
            painter.setBrush(radial);
            painter.setCompositionMode(QPainter::CompositionMode_Plus);
            painter.drawEllipse(head, radius, radius);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
    }

    // Horizontal caret move: an isosceles triangle whose base is the caret's
    // left edge, so the trail never covers the caret itself.
    void paintCaret(QPainter &painter, const QRectF &area) const
    {
        const Look &look = m_appearance.caret;

        const qreal cy = area.center().y();
        const QColor textColor = palette().color(QPalette::Text);

        painter.setPen(Qt::NoPen);
        QColor lineColor = textColor;
        lineColor.setAlpha(40);
        painter.setBrush(lineColor);
        const qreal lineHeight = 4.0;
        painter.drawRoundedRect(
            QRectF(area.left(), cy - 34, area.width() * 0.9, lineHeight), 2, 2);
        painter.drawRoundedRect(
            QRectF(area.left(), cy + 30, area.width() * 0.7, lineHeight), 2, 2);

        const qreal caretHeight = std::min<qreal>(area.height() * 0.5, 24.0);
        const qreal headX = area.left() + area.width() * 0.82;
        const qreal tailX = area.left() + area.width() * 0.10;
        const qreal k = look.intensity;
        const qreal lk = look.lightStrength * look.intensity;
        const qreal lr = look.lightRadius;
        const qreal baseHeight = std::min<qreal>(
            std::min<qreal>(caretHeight, m_appearance.caretHeight),
            area.height() - 8.0);

        const auto drawLayer = [&](const std::function<QColor(qreal)> &colorAt,
                                   qreal alpha,
                                   qreal factor) {
            const qreal half = 0.5 * baseHeight * factor;

            QPolygonF triangle;
            triangle << QPointF(headX, cy - half)
                     << QPointF(headX, cy + half)
                     << QPointF(tailX, cy);

            // Fades from transparent at the tail to full at the head.
            QLinearGradient gradient(tailX, 0, headX, 0);
            constexpr int Samples = 16;
            for (int i = 0; i <= Samples; ++i) {
                const qreal pos = qreal(i) / Samples;
                QColor color = colorAt(1.0 - pos);
                color.setAlphaF(std::clamp(alpha * pos, 0.0, 1.0));
                gradient.setColorAt(pos, color);
            }

            painter.setPen(Qt::NoPen);
            painter.setBrush(gradient);
            painter.drawPolygon(triangle);
        };

        if (look.lightEnabled) {
            const auto lightFn = [&](qreal t) { return lightAt(look, t); };
            painter.setCompositionMode(QPainter::CompositionMode_Plus);
            drawLayer(lightFn, std::min(1.0, 0.020 * lk * 3.0), 4.2 * lr);
            drawLayer(lightFn, std::min(1.0, 0.040 * lk * 3.0), 2.8 * lr);
            drawLayer(lightFn, std::min(1.0, 0.075 * lk * 3.0), 1.9 * lr);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
        if (look.glowEnabled) {
            drawLayer([&](qreal t) { return glowAt(look, t); },
                      std::clamp(0.30 * k, 0.0, 1.0), 1.6);
        }
        drawLayer([&](qreal t) { return mainAt(look, t); },
                  std::clamp(0.75 * k, 0.0, 1.0), 1.0);
        drawLayer([&](qreal) { return look.core; },
                  std::clamp(0.95 * k, 0.0, 1.0), 0.40);

        if (look.lightEnabled && look.headLight) {
            const QPointF head(headX, cy);
            const qreal radius = std::max<qreal>(8.0, baseHeight * 0.9 * lr);
            QRadialGradient radial(head, radius);
            QColor c = lightAt(look, 0.0);
            c.setAlphaF(std::clamp(0.55 * lk, 0.0, 1.0));
            radial.setColorAt(0.0, c);
            c.setAlphaF(0.0);
            radial.setColorAt(1.0, c);
            painter.setPen(Qt::NoPen);
            painter.setBrush(radial);
            painter.setCompositionMode(QPainter::CompositionMode_Plus);
            painter.drawEllipse(head, radius, radius);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }

        // The caret itself, drawn on top of (not under) the trail.
        painter.setPen(Qt::NoPen);
        painter.setBrush(textColor);
        painter.drawRect(QRectF(headX, cy - caretHeight / 2, 2.0, caretHeight));
    }

    Appearance m_appearance;
};

CursorTrailOnConfig::CursorTrailOnConfig(QObject *parent,
                                         const KPluginMetaData &data)
    : KCModule(parent, data)
{
    // Everything lives in a scroll area whose default size is 400 px high;
    // the window can still be resized freely.
    auto *outer = new QVBoxLayout(widget());
    outer->setContentsMargins(0, 0, 0, 0);

    auto *scroll = new PageScrollArea(widget());
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll);

    auto *content = new QWidget(scroll);
    scroll->setWidget(content);
    auto *rootLayout = new QVBoxLayout(content);

    auto *description = new QLabel(
        QStringLiteral(
            "Customize the trail that follows the mouse pointer and the "
            "text caret. Each has its own colors, gradient, light effect "
            "and timing. Changes are applied when you click Apply."),
        content);
    description->setWordWrap(true);
    rootLayout->addWidget(description);

    m_preview = new CursorTrailOnPreview(content);
    rootLayout->addWidget(m_preview);

    // --- Mouse pointer ---------------------------------------------------
    auto *mouseBox = new QGroupBox(QStringLiteral("Mouse pointer trail"), content);
    auto *mouseForm = new QFormLayout(mouseBox);

    m_mouseEnabled = new QCheckBox(
        QStringLiteral("Show trail behind the mouse pointer"), mouseBox);
    mouseForm->addRow(m_mouseEnabled);

    mouseForm->addRow(
        QStringLiteral("Trail width:"),
        makeSliderRow(mouseBox, m_trailWidthSlider, m_trailWidth,
                      MinTrailWidth, MaxTrailWidth));

    m_activationSpeed = new QSpinBox(mouseBox);
    m_activationSpeed->setRange(100, 1600);
    m_activationSpeed->setSingleStep(20);
    m_activationSpeed->setSuffix(QStringLiteral(" px/s"));
    mouseForm->addRow(QStringLiteral("Minimum speed:"), m_activationSpeed);

    m_smoothness = new QSpinBox(mouseBox);
    m_smoothness->setRange(0, 4);
    m_smoothness->setSingleStep(1);
    m_smoothness->setSpecialValueText(QStringLiteral("No smoothing"));
    mouseForm->addRow(QStringLiteral("Curve smoothness:"), m_smoothness);

    m_trailDuration = new QSpinBox(mouseBox);
    m_trailDuration->setRange(120, 650);
    m_trailDuration->setSingleStep(10);
    m_trailDuration->setSuffix(QStringLiteral(" ms"));
    mouseForm->addRow(QStringLiteral("Trail length / duration:"),
                      m_trailDuration);

    buildLookRows(mouseForm, mouseBox, Target::Pointer);
    rootLayout->addWidget(mouseBox);

    // --- Text caret ------------------------------------------------------
    auto *caretBox = new QGroupBox(QStringLiteral("Text caret trail"), content);
    auto *caretForm = new QFormLayout(caretBox);

    m_caretEnabled = new QCheckBox(
        QStringLiteral("Show trail when the text caret moves"), caretBox);
    m_caretEnabled->setToolTip(QStringLiteral(
        "Works with Wayland applications that report their caret position "
        "to the compositor (GTK and Qt text fields, most Electron/Chromium "
        "apps running on Wayland). XWayland applications and terminals "
        "that do not use the text-input protocol are not tracked."));
    caretForm->addRow(m_caretEnabled);

    auto *widthRow = makeSliderRow(caretBox, m_caretWidthSlider, m_caretWidth,
                                   MinCaretWidth, MaxCaretWidth);
    widthRow->setToolTip(QStringLiteral(
        "Width of the trail when the caret moves up or down. The trail "
        "starts at the edge of the caret and never covers it."));
    caretForm->addRow(QStringLiteral("Caret trail width:"), widthRow);

    auto *heightRow = makeSliderRow(caretBox, m_caretHeightSlider, m_caretHeight,
                                    MinCaretHeight, MaxCaretHeight);
    heightRow->setToolTip(QStringLiteral(
        "Maximum height of the trail when the caret moves left or right "
        "(the trail is an isosceles triangle as tall as the caret, up to "
        "this limit)."));
    caretForm->addRow(QStringLiteral("Caret trail max height:"), heightRow);

    m_caretMinSpeed = new QSpinBox(caretBox);
    m_caretMinSpeed->setRange(0, 3000);
    m_caretMinSpeed->setSingleStep(20);
    m_caretMinSpeed->setSuffix(QStringLiteral(" px/s"));
    m_caretMinSpeed->setSpecialValueText(QStringLiteral("No limit"));
    m_caretMinSpeed->setToolTip(QStringLiteral(
        "Caret moves slower than this draw no trail, like the mouse "
        "trail's minimum speed."));
    caretForm->addRow(QStringLiteral("Minimum speed:"), m_caretMinSpeed);

    m_caretDuration = new QSpinBox(caretBox);
    m_caretDuration->setRange(80, 1000);
    m_caretDuration->setSingleStep(10);
    m_caretDuration->setSuffix(QStringLiteral(" ms"));
    caretForm->addRow(QStringLiteral("Trail length / duration:"),
                      m_caretDuration);

    m_caretPollInterval = new QSpinBox(caretBox);
    m_caretPollInterval->setRange(4, 200);
    m_caretPollInterval->setSingleStep(2);
    m_caretPollInterval->setSuffix(QStringLiteral(" ms"));
    m_caretPollInterval->setToolTip(QStringLiteral(
        "How often the caret position is queried. Lower values follow "
        "fast typing more closely but use slightly more CPU."));
    caretForm->addRow(QStringLiteral("Caret query interval:"),
                      m_caretPollInterval);

    buildLookRows(caretForm, caretBox, Target::Caret);
    rootLayout->addWidget(caretBox);

    // --- General ---------------------------------------------------------
    m_disableInFullscreen = new QCheckBox(
        QStringLiteral("Disable in full-screen apps and games"),
        content);
    rootLayout->addWidget(m_disableInFullscreen);

    auto *about = new QLabel(content);
    about->setTextFormat(Qt::RichText);
    about->setWordWrap(true);
    about->setOpenExternalLinks(true);
    about->setTextInteractionFlags(Qt::TextBrowserInteraction);
    about->setText(QStringLiteral(
        "<b>cursorTrailOn - Animated Pointer And Cursor Trail</b><br>"
        "A smooth, customizable trail for the mouse pointer and the text "
        "cursor on KDE Plasma and KWin Wayland.<br>"
        "Author: xcatzix &nbsp;&nbsp; License: GPL-3.0-or-later<br>"
        "mailto: <a href=\"mailto:3949745980@qq.com\">3949745980@qq.com</a>"
        " &nbsp;&nbsp; web: <a href=\"https://github.com/xcatzix\">"
        "github.com/xcatzix</a>"));
    rootLayout->addWidget(about);
    rootLayout->addStretch(1);

    for (LookWidgets *w : {&m_pointerWidgets, &m_caretWidgets}) {
        for (QCheckBox *box : {w->glowEnabled, w->gradientEnabled,
                               w->gradientSmooth, w->lightEnabled,
                               w->headLight}) {
            connect(box, &QCheckBox::toggled, this,
                    &CursorTrailOnConfig::settingsChanged);
        }
        for (QDoubleSpinBox *spin : {w->intensity, w->lightStrength}) {
            connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                    this, &CursorTrailOnConfig::settingsChanged);
        }
        connect(w->lightRadius, qOverload<int>(&QSpinBox::valueChanged), this,
                &CursorTrailOnConfig::settingsChanged);
    }
    for (QCheckBox *box : {m_mouseEnabled, m_caretEnabled,
                           m_disableInFullscreen}) {
        connect(box, &QCheckBox::toggled, this,
                &CursorTrailOnConfig::settingsChanged);
    }
    for (QSpinBox *spin : {m_trailWidth, m_trailDuration, m_activationSpeed,
                           m_smoothness, m_caretWidth, m_caretHeight,
                           m_caretMinSpeed, m_caretDuration,
                           m_caretPollInterval}) {
        connect(spin, qOverload<int>(&QSpinBox::valueChanged), this,
                &CursorTrailOnConfig::settingsChanged);
    }

    load();
}

CursorTrailOnConfig::~CursorTrailOnConfig() = default;

QWidget *CursorTrailOnConfig::makeSliderRow(QWidget *parent,
                                            QSlider *&slider,
                                            QSpinBox *&spin,
                                            int minimum,
                                            int maximum)
{
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);

    slider = new QSlider(Qt::Horizontal, row);
    slider->setRange(minimum, maximum);

    spin = new QSpinBox(row);
    spin->setRange(minimum, maximum);
    spin->setSuffix(QStringLiteral(" px"));

    layout->addWidget(slider, 1);
    layout->addWidget(spin);

    // Keep both controls in sync. The spin box is the one wired to
    // settingsChanged(), so the slider just forwards its value to it.
    QSlider *s = slider;
    QSpinBox *b = spin;
    connect(s, &QSlider::valueChanged, b, &QSpinBox::setValue);
    connect(b, qOverload<int>(&QSpinBox::valueChanged), s,
            [s](int value) {
                const QSignalBlocker blocker(s);
                s->setValue(value);
            });

    return row;
}

// Colors, gradient and light effect: identical rows for the pointer trail and
// the caret trail, stored independently.
void CursorTrailOnConfig::buildLookRows(QFormLayout *form,
                                        QWidget *parent,
                                        Target target)
{
    LookWidgets &w = widgetsFor(target);

    w.preset = new QComboBox(parent);
    w.preset->addItem(QStringLiteral("Crimson Slash"));
    w.preset->addItem(QStringLiteral("Wind White"));
    w.preset->addItem(QStringLiteral("Ice Blue"));
    w.preset->addItem(QStringLiteral("Custom"));
    form->addRow(target == Target::Pointer
                     ? QStringLiteral("Pointer trail preset:")
                     : QStringLiteral("Caret trail preset:"),
                 w.preset);

    w.mainButton = new QPushButton(parent);
    w.mainButton->setToolTip(QStringLiteral(
        "Body color of the trail. Replaced by the gradient while "
        "\"Use gradient\" is on."));
    form->addRow(QStringLiteral("Main color:"), w.mainButton);

    w.coreButton = new QPushButton(parent);
    form->addRow(QStringLiteral("Center color:"), w.coreButton);

    w.glowEnabled = new QCheckBox(
        QStringLiteral("Outer glow / shadow around the trail"), parent);
    form->addRow(w.glowEnabled);

    w.glowButton = new QPushButton(parent);
    w.glowButton->setToolTip(QStringLiteral(
        "Color of the outer glow. With \"Use gradient\" on, the glow is "
        "tinted from the gradient instead. Clicking this turns the glow on."));
    form->addRow(QStringLiteral("Glow color:"), w.glowButton);

    w.intensity = new QDoubleSpinBox(parent);
    w.intensity->setRange(0.15, 2.0);
    w.intensity->setSingleStep(0.05);
    w.intensity->setDecimals(2);
    w.intensity->setSuffix(QStringLiteral("×"));
    form->addRow(QStringLiteral("Intensity (opacity):"), w.intensity);

    // --- gradient -------------------------------------------------------
    w.gradientEnabled = new QCheckBox(
        QStringLiteral("Use gradient (mix several colors along the trail)"),
        parent);
    form->addRow(w.gradientEnabled);

    w.gradientPreset = new QComboBox(parent);
    for (const char *name : {"Custom", "Sunset", "Aurora", "Ocean",
                             "Rainbow", "Fire", "Neon"}) {
        w.gradientPreset->addItem(QLatin1String(name));
    }
    form->addRow(QStringLiteral("Gradient preset:"), w.gradientPreset);

    w.gradientSmooth = new QCheckBox(
        QStringLiteral("Smooth blend (off = hard color bands)"), parent);
    form->addRow(w.gradientSmooth);

    w.stopsHost = new QWidget(parent);
    w.stopsLayout = new QVBoxLayout(w.stopsHost);
    w.stopsLayout->setContentsMargins(0, 0, 0, 0);
    form->addRow(QStringLiteral("Gradient colors:"), w.stopsHost);

    w.addStop = new QPushButton(QStringLiteral("Add color"), parent);
    form->addRow(QString(), w.addStop);

    // --- light effect -----------------------------------------------------
    w.lightEnabled = new QCheckBox(
        QStringLiteral("Light effect (soft bloom around the trail)"), parent);
    form->addRow(w.lightEnabled);

    w.lightStrength = new QDoubleSpinBox(parent);
    w.lightStrength->setRange(0.1, 3.0);
    w.lightStrength->setSingleStep(0.1);
    w.lightStrength->setDecimals(1);
    w.lightStrength->setSuffix(QStringLiteral("×"));
    form->addRow(QStringLiteral("Light strength:"), w.lightStrength);

    w.lightRadius = new QSpinBox(parent);
    w.lightRadius->setRange(50, 400);
    w.lightRadius->setSingleStep(10);
    w.lightRadius->setSuffix(QStringLiteral(" %"));
    form->addRow(QStringLiteral("Light radius:"), w.lightRadius);

    w.headLight = new QCheckBox(
        QStringLiteral("Light spot at the head of the trail"), parent);
    form->addRow(w.headLight);

    connect(w.mainButton, &QPushButton::clicked, this,
            [this, target] { chooseColor(target, ColorRole::Main); });
    connect(w.coreButton, &QPushButton::clicked, this,
            [this, target] { chooseColor(target, ColorRole::Core); });
    connect(w.glowButton, &QPushButton::clicked, this,
            [this, target] { chooseColor(target, ColorRole::Glow); });
    connect(w.preset, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, target](int index) { presetChanged(target, index); });
    connect(w.gradientPreset,
            qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, target](int index) { gradientPresetChanged(target, index); });
    connect(w.addStop, &QPushButton::clicked, this, [this, target] {
        QList<TrailGradientStop> &stops = stopsFor(target);
        if (stops.size() >= MaxGradientStops) {
            return;
        }
        // New color goes halfway between the last two stops.
        TrailGradientStop stop = stops.isEmpty() ? TrailGradientStop{}
                                                  : stops.last();
        if (stops.size() >= 2) {
            const TrailGradientStop &prev = stops[stops.size() - 2];
            stop.percent = (prev.percent + stops.last().percent) / 2;
            stop.color = mixColors(prev.color, stops.last().color, 0.5);
        }
        stops.insert(stops.size() - 1 < 0 ? 0 : stops.size() - 1, stop);
        std::stable_sort(stops.begin(), stops.end(),
                         [](const TrailGradientStop &a,
                            const TrailGradientStop &b) {
                             return a.percent < b.percent;
                         });
        rebuildStopRows(target);
        settingsChanged();
    });
}

CursorTrailOnConfig::LookWidgets &CursorTrailOnConfig::widgetsFor(Target target)
{
    return target == Target::Pointer ? m_pointerWidgets : m_caretWidgets;
}

CursorTrailOnConfig::Palette &CursorTrailOnConfig::paletteFor(Target target)
{
    return target == Target::Pointer ? m_pointerPalette : m_caretPalette;
}

QList<TrailGradientStop> &CursorTrailOnConfig::stopsFor(Target target)
{
    return target == Target::Pointer ? m_pointerStops : m_caretStops;
}

bool CursorTrailOnConfig::Palette::operator==(const Palette &other) const
{
    return main.rgb() == other.main.rgb()
        && core.rgb() == other.core.rgb()
        && glow.rgb() == other.glow.rgb();
}

bool CursorTrailOnConfig::Look::operator==(const Look &other) const
{
    return palette == other.palette
        && glowEnabled == other.glowEnabled
        && qFuzzyCompare(intensity + 1.0, other.intensity + 1.0)
        && gradientEnabled == other.gradientEnabled
        && gradientSmooth == other.gradientSmooth
        && stops == other.stops
        && lightEnabled == other.lightEnabled
        && qFuzzyCompare(lightStrength + 1.0, other.lightStrength + 1.0)
        && lightRadius == other.lightRadius
        && headLight == other.headLight;
}

bool CursorTrailOnConfig::Settings::operator==(const Settings &other) const
{
    return mouseEnabled == other.mouseEnabled
        && caretEnabled == other.caretEnabled
        && pointer == other.pointer
        && trailWidth == other.trailWidth
        && trailDuration == other.trailDuration
        && activationSpeed == other.activationSpeed
        && smoothness == other.smoothness
        && caret == other.caret
        && caretWidth == other.caretWidth
        && caretHeight == other.caretHeight
        && caretMinSpeed == other.caretMinSpeed
        && caretDuration == other.caretDuration
        && caretPollInterval == other.caretPollInterval
        && disableInFullscreen == other.disableInFullscreen;
}

bool CursorTrailOnConfig::Settings::operator!=(const Settings &other) const
{
    return !(*this == other);
}

CursorTrailOnConfig::Palette CursorTrailOnConfig::derivedPalette(const QColor &main)
{
    // Same derivation the effect uses when no explicit center/glow color is
    // stored: a pale center and a darkened glow.
    return Palette{
        .main = main,
        .core = mixColors(main, QColor(255, 255, 255), 0.72),
        .glow = mixColors(main, QColor(0, 0, 0), 0.28),
    };
}

CursorTrailOnConfig::Palette CursorTrailOnConfig::presetPalette(int index)
{
    switch (index) {
    case 1:
        return derivedPalette(QColor(238, 242, 247)); // Wind White
    case 2:
        return derivedPalette(QColor(67, 184, 255)); // Ice Blue
    case 0:
    default:
        return derivedPalette(QColor(255, 6, 12)); // Crimson Slash
    }
}

int CursorTrailOnConfig::presetForPalette(const Palette &palette)
{
    for (int index = 0; index < 3; ++index) {
        if (palette == presetPalette(index)) {
            return index;
        }
    }
    return 3;
}

QList<TrailGradientStop> CursorTrailOnConfig::gradientPresetStops(int index)
{
    const auto make = [](std::initializer_list<std::pair<const char *, int>> l) {
        QList<TrailGradientStop> stops;
        for (const auto &item : l) {
            TrailGradientStop stop;
            stop.color = QColor(QLatin1String(item.first));
            stop.percent = item.second;
            stops.push_back(stop);
        }
        return stops;
    };

    switch (index) {
    case 2: // Aurora
        return make({{"#00ffa3", 0}, {"#00c2ff", 50}, {"#7a5cff", 100}});
    case 3: // Ocean
        return make({{"#00e5ff", 0}, {"#1e6bff", 55}, {"#0a1a6b", 100}});
    case 4: // Rainbow
        return make({{"#ff0000", 0}, {"#ffd400", 25}, {"#00e050", 50},
                     {"#0090ff", 75}, {"#a000ff", 100}});
    case 5: // Fire
        return make({{"#ffee58", 0}, {"#ff7a00", 45}, {"#c1121f", 100}});
    case 6: // Neon
        return make({{"#ff00e5", 0}, {"#00f0ff", 100}});
    case 1: // Sunset
    default:
        return make({{"#ff3d00", 0}, {"#ff0099", 50}, {"#6a00ff", 100}});
    }
}

int CursorTrailOnConfig::gradientPresetFor(const QList<TrailGradientStop> &stops)
{
    for (int index = 1; index <= 6; ++index) {
        if (stops == gradientPresetStops(index)) {
            return index;
        }
    }
    return 0; // Custom
}

CursorTrailOnConfig::Look CursorTrailOnConfig::defaultLook()
{
    Look look;
    look.palette = presetPalette(0);
    look.stops = gradientPresetStops(1);
    return look;
}

CursorTrailOnConfig::Settings CursorTrailOnConfig::defaultSettings()
{
    Settings settings;
    settings.pointer = defaultLook();
    settings.caret = defaultLook();
    return settings;
}

CursorTrailOnConfig::Settings CursorTrailOnConfig::readSettings() const
{
    const KSharedConfigPtr config = KSharedConfig::openConfig(
        QStringLiteral("kwinrc"));
    const KConfigGroup group(config, QStringLiteral("Effect-cursortrailon"));

    Settings settings = defaultSettings();

    settings.mouseEnabled = group.readEntry(
        "MouseTrailEnabled", settings.mouseEnabled);
    settings.caretEnabled = group.readEntry(
        "TextCaretEnabled", settings.caretEnabled);

    // Reads main/center/glow stored under the given keys; missing center and
    // glow colors are derived from the main color.
    const auto readPalette = [&group](const char *mainKey,
                                      const char *coreKey,
                                      const char *glowKey,
                                      const Palette &fallback) {
        const QColor main = group.readEntry(mainKey, fallback.main);
        const Palette derived = group.hasKey(mainKey)
            ? derivedPalette(main.isValid() ? main : fallback.main)
            : fallback;
        const QColor core = group.readEntry(coreKey, derived.core);
        const QColor glow = group.readEntry(glowKey, derived.glow);
        return Palette{
            .main = derived.main,
            .core = core.isValid() ? core : derived.core,
            .glow = glow.isValid() ? glow : derived.glow,
        };
    };

    // Gradient and light keys; `prefix` is "" (pointer) or "Caret".
    const auto readExtras = [&group](Look &look,
                                     const QString &prefix,
                                     const Look &fallback) {
        look.gradientEnabled = group.readEntry(
            prefix + QStringLiteral("GradientEnabled"), fallback.gradientEnabled);
        look.gradientSmooth = group.readEntry(
            prefix + QStringLiteral("GradientSmooth"), fallback.gradientSmooth);
        QList<TrailGradientStop> stops = stopsFromString(group.readEntry(
            prefix + QStringLiteral("GradientStops"), QString()));
        look.stops = stops.size() >= MinGradientStops ? stops : fallback.stops;

        look.lightEnabled = group.readEntry(
            prefix + QStringLiteral("LightEnabled"), fallback.lightEnabled);
        look.lightStrength = std::clamp(group.readEntry(
            prefix + QStringLiteral("LightStrength"), fallback.lightStrength),
            0.1, 3.0);
        look.lightRadius = std::clamp(group.readEntry(
            prefix + QStringLiteral("LightRadius"), fallback.lightRadius),
            50, 400);
        look.headLight = group.readEntry(
            prefix + QStringLiteral("HeadLight"), fallback.headLight);
    };

    // --- pointer trail
    settings.pointer.palette = readPalette("Color", "CoreColor", "GlowColor",
                                           settings.pointer.palette);
    settings.pointer.glowEnabled = group.readEntry(
        "GlowEnabled", settings.pointer.glowEnabled);
    settings.pointer.intensity = group.readEntry(
        "Intensity", settings.pointer.intensity);
    readExtras(settings.pointer, QString(), settings.pointer);

    qreal width = settings.trailWidth;
    if (group.hasKey("TrailWidth")) {
        width = group.readEntry("TrailWidth", width);
    } else if (group.hasKey("Thickness")) {
        width = group.readEntry("Thickness", 1.0) * 18.0; // legacy multiplier
    }
    settings.trailWidth = std::clamp(int(std::lround(width)),
                                     MinTrailWidth, MaxTrailWidth);

    settings.trailDuration = group.readEntry(
        "TrailDuration", settings.trailDuration);
    settings.activationSpeed = group.readEntry(
        "ActivationSpeed", settings.activationSpeed);
    settings.smoothness = group.readEntry("Smoothness", settings.smoothness);

    // --- caret trail: settings saved by 1.1.x had a single shared color set,
    // glow switch, intensity and duration; use those as the starting point.
    settings.caret = settings.pointer;
    if (group.hasKey("CaretColor")) {
        settings.caret.palette = readPalette(
            "CaretColor", "CaretCoreColor", "CaretGlowColor",
            settings.pointer.palette);
    }
    settings.caret.glowEnabled = group.readEntry(
        "CaretGlowEnabled", settings.pointer.glowEnabled);
    settings.caret.intensity = group.readEntry(
        "CaretIntensity", settings.pointer.intensity);
    readExtras(settings.caret, QStringLiteral("Caret"), settings.pointer);

    settings.caretWidth = std::clamp(
        int(std::lround(group.readEntry("CaretTrailWidth",
                                        qreal(settings.caretWidth)))),
        MinCaretWidth, MaxCaretWidth);
    settings.caretHeight = std::clamp(
        group.readEntry("CaretTrailHeight", settings.caretHeight),
        MinCaretHeight, MaxCaretHeight);
    settings.caretMinSpeed = std::clamp(
        group.readEntry("CaretMinSpeed", settings.caretMinSpeed), 0, 3000);
    settings.caretDuration = std::clamp(
        group.readEntry("CaretTrailDuration", settings.trailDuration),
        80, 1000);
    settings.caretPollInterval = std::clamp(
        group.readEntry("CaretPollInterval", settings.caretPollInterval),
        4, 200);

    settings.disableInFullscreen = group.readEntry(
        "DisableInFullscreen", settings.disableInFullscreen);

    return settings;
}

CursorTrailOnConfig::Settings CursorTrailOnConfig::currentSettings() const
{
    const auto lookOf = [](const LookWidgets &w,
                           const Palette &palette,
                           const QList<TrailGradientStop> &stops) {
        Look look;
        look.palette = palette;
        look.glowEnabled = w.glowEnabled->isChecked();
        look.intensity = w.intensity->value();
        look.gradientEnabled = w.gradientEnabled->isChecked();
        look.gradientSmooth = w.gradientSmooth->isChecked();
        look.stops = stops;
        look.lightEnabled = w.lightEnabled->isChecked();
        look.lightStrength = w.lightStrength->value();
        look.lightRadius = w.lightRadius->value();
        look.headLight = w.headLight->isChecked();
        return look;
    };

    Settings settings;
    settings.mouseEnabled = m_mouseEnabled->isChecked();
    settings.caretEnabled = m_caretEnabled->isChecked();

    settings.pointer = lookOf(m_pointerWidgets, m_pointerPalette, m_pointerStops);
    settings.trailWidth = m_trailWidth->value();
    settings.trailDuration = m_trailDuration->value();
    settings.activationSpeed = m_activationSpeed->value();
    settings.smoothness = m_smoothness->value();

    settings.caret = lookOf(m_caretWidgets, m_caretPalette, m_caretStops);
    settings.caretWidth = m_caretWidth->value();
    settings.caretHeight = m_caretHeight->value();
    settings.caretMinSpeed = m_caretMinSpeed->value();
    settings.caretDuration = m_caretDuration->value();
    settings.caretPollInterval = m_caretPollInterval->value();

    settings.disableInFullscreen = m_disableInFullscreen->isChecked();
    return settings;
}

void CursorTrailOnConfig::applySettings(const Settings &settings)
{
    m_loading = true;

    m_pointerPalette = settings.pointer.palette;
    m_caretPalette = settings.caret.palette;
    m_pointerStops = settings.pointer.stops;
    m_caretStops = settings.caret.stops;

    m_mouseEnabled->setChecked(settings.mouseEnabled);
    m_caretEnabled->setChecked(settings.caretEnabled);

    const auto applyLook = [this](LookWidgets &w, const Look &look) {
        w.preset->setCurrentIndex(presetForPalette(look.palette));
        w.glowEnabled->setChecked(look.glowEnabled);
        w.intensity->setValue(look.intensity);
        w.gradientEnabled->setChecked(look.gradientEnabled);
        w.gradientSmooth->setChecked(look.gradientSmooth);
        w.gradientPreset->setCurrentIndex(gradientPresetFor(look.stops));
        w.lightEnabled->setChecked(look.lightEnabled);
        w.lightStrength->setValue(look.lightStrength);
        w.lightRadius->setValue(look.lightRadius);
        w.headLight->setChecked(look.headLight);
    };
    applyLook(m_pointerWidgets, settings.pointer);
    applyLook(m_caretWidgets, settings.caret);

    m_trailWidth->setValue(settings.trailWidth);
    m_trailDuration->setValue(settings.trailDuration);
    m_activationSpeed->setValue(settings.activationSpeed);
    m_smoothness->setValue(settings.smoothness);

    m_caretWidth->setValue(settings.caretWidth);
    m_caretHeight->setValue(settings.caretHeight);
    m_caretMinSpeed->setValue(settings.caretMinSpeed);
    m_caretDuration->setValue(settings.caretDuration);
    m_caretPollInterval->setValue(settings.caretPollInterval);

    m_disableInFullscreen->setChecked(settings.disableInFullscreen);

    for (Target target : {Target::Pointer, Target::Caret}) {
        updateColorButtons(target);
        rebuildStopRows(target);
    }
    updateEnabledStates();
    updatePreview();

    m_loading = false;
}

void CursorTrailOnConfig::load()
{
    applySettings(readSettings());
    m_savedSettings = currentSettings();

    setNeedsSave(false);
    setRepresentsDefaults(m_savedSettings == defaultSettings());
}

void CursorTrailOnConfig::save()
{
    const Settings settings = currentSettings();

    const KSharedConfigPtr config = KSharedConfig::openConfig(
        QStringLiteral("kwinrc"));
    KConfigGroup group(config, QStringLiteral("Effect-cursortrailon"));

    group.writeEntry("MouseTrailEnabled", settings.mouseEnabled);
    group.writeEntry("TextCaretEnabled", settings.caretEnabled);

    // pointer trail
    group.writeEntry("Color", settings.pointer.palette.main);
    group.writeEntry("CoreColor", settings.pointer.palette.core);
    group.writeEntry("GlowColor", settings.pointer.palette.glow);
    group.writeEntry("GlowEnabled", settings.pointer.glowEnabled);
    group.writeEntry("Intensity", settings.pointer.intensity);
    group.writeEntry("GradientEnabled", settings.pointer.gradientEnabled);
    group.writeEntry("GradientSmooth", settings.pointer.gradientSmooth);
    group.writeEntry("GradientStops", stopsToString(settings.pointer.stops));
    group.writeEntry("LightEnabled", settings.pointer.lightEnabled);
    group.writeEntry("LightStrength", settings.pointer.lightStrength);
    group.writeEntry("LightRadius", settings.pointer.lightRadius);
    group.writeEntry("HeadLight", settings.pointer.headLight);
    group.writeEntry("TrailWidth", qreal(settings.trailWidth));
    group.writeEntry("TrailDuration", settings.trailDuration);
    group.writeEntry("ActivationSpeed", settings.activationSpeed);
    group.writeEntry("Smoothness", settings.smoothness);

    // caret trail
    group.writeEntry("CaretColor", settings.caret.palette.main);
    group.writeEntry("CaretCoreColor", settings.caret.palette.core);
    group.writeEntry("CaretGlowColor", settings.caret.palette.glow);
    group.writeEntry("CaretGlowEnabled", settings.caret.glowEnabled);
    group.writeEntry("CaretIntensity", settings.caret.intensity);
    group.writeEntry("CaretGradientEnabled", settings.caret.gradientEnabled);
    group.writeEntry("CaretGradientSmooth", settings.caret.gradientSmooth);
    group.writeEntry("CaretGradientStops", stopsToString(settings.caret.stops));
    group.writeEntry("CaretLightEnabled", settings.caret.lightEnabled);
    group.writeEntry("CaretLightStrength", settings.caret.lightStrength);
    group.writeEntry("CaretLightRadius", settings.caret.lightRadius);
    group.writeEntry("CaretHeadLight", settings.caret.headLight);
    group.writeEntry("CaretTrailWidth", qreal(settings.caretWidth));
    group.writeEntry("CaretTrailHeight", settings.caretHeight);
    group.writeEntry("CaretMinSpeed", settings.caretMinSpeed);
    group.writeEntry("CaretTrailDuration", settings.caretDuration);
    group.writeEntry("CaretPollInterval", settings.caretPollInterval);

    group.writeEntry("DisableInFullscreen", settings.disableInFullscreen);
    group.deleteEntry("Thickness"); // superseded by TrailWidth
    group.sync();
    config->sync();

    QDBusInterface interface(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/Effects"),
        QStringLiteral("org.kde.kwin.Effects"),
        QDBusConnection::sessionBus());

    interface.call(
        QDBus::NoBlock,
        QStringLiteral("reconfigureEffect"),
        QStringLiteral("cursortrailon"));

    m_savedSettings = settings;
    setNeedsSave(false);
    setRepresentsDefaults(settings == defaultSettings());
}

void CursorTrailOnConfig::defaults()
{
    applySettings(defaultSettings());
    updateState();
}

void CursorTrailOnConfig::chooseColor(Target target, ColorRole role)
{
    Palette &palette = paletteFor(target);

    QColor *color = nullptr;
    QString title;
    const QString what = target == Target::Pointer
        ? QStringLiteral("pointer trail")
        : QStringLiteral("caret trail");

    switch (role) {
    case ColorRole::Main:
        color = &palette.main;
        title = QStringLiteral("Choose %1 main color").arg(what);
        break;
    case ColorRole::Core:
        color = &palette.core;
        title = QStringLiteral("Choose %1 center color").arg(what);
        break;
    case ColorRole::Glow:
        color = &palette.glow;
        title = QStringLiteral("Choose %1 glow color").arg(what);
        break;
    }

    // Parent the dialog to the button's top-level window, not to the
    // KCModule's inner widget, and use the Qt dialog so it always opens.
    LookWidgets &w = widgetsFor(target);
    QWidget *anchor = w.mainButton ? w.mainButton->window() : widget();

    const QColor selected = QColorDialog::getColor(
        *color, anchor, title, QColorDialog::DontUseNativeDialog);
    if (!selected.isValid()) {
        return;
    }

    QColor opaque = selected;
    opaque.setAlpha(255); // opacity is controlled by Intensity
    *color = opaque;

    {
        const QSignalBlocker blocker(w.preset);
        w.preset->setCurrentIndex(presetForPalette(palette));
    }

    // Picking a glow color means the user wants a glow.
    if (role == ColorRole::Glow && !w.glowEnabled->isChecked()) {
        const QSignalBlocker blocker(w.glowEnabled);
        w.glowEnabled->setChecked(true);
    }

    updateColorButtons(target);
    settingsChanged();
}

void CursorTrailOnConfig::chooseStopColor(Target target, int index)
{
    QList<TrailGradientStop> &stops = stopsFor(target);
    if (index < 0 || index >= stops.size()) {
        return;
    }

    LookWidgets &w = widgetsFor(target);
    const QColor selected = QColorDialog::getColor(
        stops[index].color, w.mainButton->window(),
        QStringLiteral("Choose gradient color %1").arg(index + 1),
        QColorDialog::DontUseNativeDialog);
    if (!selected.isValid()) {
        return;
    }

    stops[index].color = selected;
    stops[index].color.setAlpha(255);

    {
        const QSignalBlocker blocker(w.gradientPreset);
        w.gradientPreset->setCurrentIndex(gradientPresetFor(stops));
    }
    rebuildStopRows(target);
    settingsChanged();
}

void CursorTrailOnConfig::presetChanged(Target target, int index)
{
    if (m_loading) {
        return;
    }

    if (index < 0 || index > 2) {
        updateState(); // "Custom": keep whatever colors are set now
        return;
    }

    paletteFor(target) = presetPalette(index);
    updateColorButtons(target);
    settingsChanged();
}

void CursorTrailOnConfig::gradientPresetChanged(Target target, int index)
{
    if (m_loading) {
        return;
    }

    if (index <= 0) {
        updateState(); // "Custom": keep the current stops
        return;
    }

    stopsFor(target) = gradientPresetStops(index);
    rebuildStopRows(target);
    settingsChanged();
}

// (Re)creates one row per gradient stop: color button, position, remove.
void CursorTrailOnConfig::rebuildStopRows(Target target)
{
    LookWidgets &w = widgetsFor(target);
    QList<TrailGradientStop> &stops = stopsFor(target);

    while (QLayoutItem *item = w.stopsLayout->takeAt(0)) {
        if (QWidget *child = item->widget()) {
            child->hide();
            child->deleteLater();
        }
        delete item;
    }

    for (int i = 0; i < stops.size(); ++i) {
        auto *row = new QWidget(w.stopsHost);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);

        auto *button = new QPushButton(
            stops[i].color.name(QColor::HexRgb).toUpper(), row);
        button->setStyleSheet(colorStyle(stops[i].color));
        connect(button, &QPushButton::clicked, this,
                [this, target, i] { chooseStopColor(target, i); });

        auto *position = new QSpinBox(row);
        position->setRange(0, 100);
        position->setSuffix(QStringLiteral(" %"));
        position->setValue(stops[i].percent);
        position->setToolTip(QStringLiteral(
            "Position along the trail: 0 % at the cursor, 100 % at the end "
            "of the trail."));
        connect(position, qOverload<int>(&QSpinBox::valueChanged), this,
                [this, target, i](int value) {
                    QList<TrailGradientStop> &list = stopsFor(target);
                    if (i < 0 || i >= list.size()) {
                        return;
                    }
                    list[i].percent = value;
                    LookWidgets &lw = widgetsFor(target);
                    {
                        const QSignalBlocker blocker(lw.gradientPreset);
                        lw.gradientPreset->setCurrentIndex(
                            gradientPresetFor(list));
                    }
                    settingsChanged();
                });

        auto *remove = new QPushButton(QStringLiteral("✕"), row);
        remove->setToolTip(QStringLiteral("Remove this color"));
        remove->setEnabled(stops.size() > MinGradientStops);
        remove->setMaximumWidth(32);
        connect(remove, &QPushButton::clicked, this,
                [this, target, i] {
                    QList<TrailGradientStop> &list = stopsFor(target);
                    if (list.size() <= MinGradientStops
                        || i < 0 || i >= list.size()) {
                        return;
                    }
                    list.removeAt(i);
                    LookWidgets &lw = widgetsFor(target);
                    {
                        const QSignalBlocker blocker(lw.gradientPreset);
                        lw.gradientPreset->setCurrentIndex(
                            gradientPresetFor(list));
                    }
                    rebuildStopRows(target);
                    settingsChanged();
                });

        layout->addWidget(button, 1);
        layout->addWidget(position);
        layout->addWidget(remove);
        w.stopsLayout->addWidget(row);
    }

    w.addStop->setEnabled(stops.size() < MaxGradientStops
                          && w.gradientEnabled->isChecked());
}

void CursorTrailOnConfig::settingsChanged()
{
    if (m_loading) {
        return;
    }

    updateEnabledStates();
    updatePreview();
    updateState();
}

void CursorTrailOnConfig::updateColorButtons(Target target)
{
    LookWidgets &w = widgetsFor(target);
    const Palette &palette = paletteFor(target);

    const auto apply = [](QPushButton *button, const QColor &color) {
        button->setText(color.name(QColor::HexRgb).toUpper());
        button->setStyleSheet(colorStyle(color));
    };

    apply(w.mainButton, palette.main);
    apply(w.coreButton, palette.core);
    apply(w.glowButton, palette.glow);
}

void CursorTrailOnConfig::updateEnabledStates()
{
    const bool mouse = m_mouseEnabled->isChecked();
    const bool caret = m_caretEnabled->isChecked();

    for (QWidget *w : std::initializer_list<QWidget *>{
             m_trailWidthSlider, m_trailWidth, m_activationSpeed,
             m_smoothness, m_trailDuration}) {
        w->setEnabled(mouse);
    }
    for (QWidget *w : std::initializer_list<QWidget *>{
             m_caretWidthSlider, m_caretWidth, m_caretHeightSlider,
             m_caretHeight, m_caretMinSpeed, m_caretDuration,
             m_caretPollInterval}) {
        w->setEnabled(caret);
    }

    // The color, gradient and light rows follow only the on/off switch of
    // their own trail. The glow color button is deliberately NOT tied to the
    // "outer glow" checkbox: it always opens the color dialog (and turns the
    // glow on), so it can never look clickable while being dead.
    for (Target target : {Target::Pointer, Target::Caret}) {
        LookWidgets &w = widgetsFor(target);
        const bool on = target == Target::Pointer ? mouse : caret;
        const bool gradient = w.gradientEnabled->isChecked();
        const bool light = w.lightEnabled->isChecked();

        for (QWidget *c : std::initializer_list<QWidget *>{
                 w.preset, w.mainButton, w.coreButton, w.glowEnabled,
                 w.glowButton, w.intensity, w.gradientEnabled,
                 w.lightEnabled}) {
            c->setEnabled(on);
        }

        w.gradientPreset->setEnabled(on && gradient);
        w.gradientSmooth->setEnabled(on && gradient);
        w.stopsHost->setEnabled(on && gradient);
        w.addStop->setEnabled(on && gradient
                              && stopsFor(target).size() < MaxGradientStops);

        w.lightStrength->setEnabled(on && light);
        w.lightRadius->setEnabled(on && light);
        w.headLight->setEnabled(on && light);
    }
}

void CursorTrailOnConfig::updatePreview()
{
    const auto toLook = [](const Look &src) {
        CursorTrailOnPreview::Look look;
        look.main = src.palette.main;
        look.core = src.palette.core;
        look.glow = src.palette.glow;
        look.glowEnabled = src.glowEnabled;
        look.intensity = src.intensity;
        look.gradientEnabled = src.gradientEnabled;
        look.gradientSmooth = src.gradientSmooth;
        look.stops = src.stops;
        look.lightEnabled = src.lightEnabled;
        look.lightStrength = src.lightStrength;
        look.lightRadius = src.lightRadius / 100.0;
        look.headLight = src.headLight;
        return look;
    };

    const Settings settings = currentSettings();

    CursorTrailOnPreview::Appearance appearance;
    appearance.pointer = toLook(settings.pointer);
    appearance.caret = toLook(settings.caret);
    appearance.trailWidth = settings.trailWidth;
    appearance.caretWidth = settings.caretWidth;
    appearance.caretHeight = settings.caretHeight;
    appearance.mouseEnabled = settings.mouseEnabled;
    appearance.caretEnabled = settings.caretEnabled;
    m_preview->setAppearance(appearance);
}

void CursorTrailOnConfig::updateState()
{
    const Settings current = currentSettings();
    setNeedsSave(current != m_savedSettings);
    setRepresentsDefaults(current == defaultSettings());
}

} // namespace KWin

#include "cursortrailon_config.moc"
#include "moc_cursortrailon_config.cpp"
