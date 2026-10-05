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
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPolygonF>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <initializer_list>

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

    return QStringLiteral(
        "QPushButton {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid rgba(127,127,127,0.7);"
        "  border-radius: 6px;"
        "  padding: 6px 12px;"
        "}")
        .arg(color.name(QColor::HexRgb), textColor);
}

} // namespace

class CursorTrailOnPreview final : public QFrame
{
public:
    struct Appearance {
        QColor main = QColor(255, 6, 12);
        QColor core = QColor(255, 185, 187);
        QColor glow = QColor(184, 4, 9);
        bool glowEnabled = true;
        qreal intensity = 1.0;
        int trailWidth = 18;

        QColor caretMain = QColor(255, 6, 12);
        QColor caretCore = QColor(255, 185, 187);
        QColor caretGlow = QColor(184, 4, 9);
        bool caretGlowEnabled = true;
        qreal caretIntensity = 1.0;
        int caretWidth = 8;
        int caretHeight = 40;

        bool mouseEnabled = true;
        bool caretEnabled = true;
    };

    explicit CursorTrailOnPreview(QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setMinimumHeight(118);
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

    void paintMouse(QPainter &painter, const QRectF &area) const
    {
        QPainterPath path;
        path.moveTo(area.left(), area.center().y() + area.height() * 0.22);
        path.cubicTo(area.left() + area.width() * 0.22,
                     area.top() - area.height() * 0.02,
                     area.left() + area.width() * 0.56,
                     area.bottom() + area.height() * 0.08,
                     area.right(),
                     area.center().y() - area.height() * 0.18);

        const qreal body = m_appearance.trailWidth * 0.5;
        const qreal k = m_appearance.intensity;

        const auto drawLayer = [&](const QColor &color, qreal width) {
            QPen pen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(pen);
            painter.drawPath(path);
        };

        if (m_appearance.glowEnabled) {
            drawLayer(withAlpha(m_appearance.glow, 0.12, k), body * 2.05);
        }
        drawLayer(withAlpha(m_appearance.main, 0.42, k), body);
        drawLayer(withAlpha(m_appearance.core, 0.82, k),
                  std::max<qreal>(1.2, body * 0.27));
    }

    // Horizontal caret move: an isosceles triangle whose base is the caret's
    // left edge, so the trail never covers the caret itself.
    void paintCaret(QPainter &painter, const QRectF &area) const
    {
        const qreal cy = area.center().y();
        const QColor textColor = palette().color(QPalette::Text);

        painter.setPen(Qt::NoPen);
        QColor lineColor = textColor;
        lineColor.setAlpha(40);
        painter.setBrush(lineColor);
        const qreal lineHeight = 4.0;
        painter.drawRoundedRect(
            QRectF(area.left(), cy - 30, area.width() * 0.9, lineHeight), 2, 2);
        painter.drawRoundedRect(
            QRectF(area.left(), cy + 26, area.width() * 0.7, lineHeight), 2, 2);

        const qreal caretHeight = std::min<qreal>(area.height() * 0.5, 24.0);
        const qreal headX = area.left() + area.width() * 0.82;
        const qreal tailX = area.left() + area.width() * 0.10;
        const qreal k = m_appearance.caretIntensity;
        const qreal baseHeight = std::min<qreal>(
            std::min<qreal>(caretHeight, m_appearance.caretHeight),
            area.height() - 8.0);

        const auto drawLayer = [&](const QColor &color, qreal factor) {
            const qreal half = 0.5 * baseHeight * factor;

            QPolygonF triangle;
            triangle << QPointF(headX, cy - half)
                     << QPointF(headX, cy + half)
                     << QPointF(tailX, cy);

            QLinearGradient gradient(tailX, 0, headX, 0);
            QColor transparent = color;
            transparent.setAlpha(0);
            gradient.setColorAt(0.0, transparent);
            gradient.setColorAt(1.0, color);

            painter.setPen(Qt::NoPen);
            painter.setBrush(gradient);
            painter.drawPolygon(triangle);
        };

        if (m_appearance.caretGlowEnabled) {
            drawLayer(withAlpha(m_appearance.caretGlow, 0.30, k), 1.6);
        }
        drawLayer(withAlpha(m_appearance.caretMain, 0.75, k), 1.0);
        drawLayer(withAlpha(m_appearance.caretCore, 0.95, k), 0.40);

        // The caret itself, drawn on top of (not under) the trail.
        painter.setBrush(textColor);
        painter.drawRect(QRectF(headX, cy - caretHeight / 2, 2.0, caretHeight));
    }

    Appearance m_appearance;
};

CursorTrailOnConfig::CursorTrailOnConfig(QObject *parent,
                                         const KPluginMetaData &data)
    : KCModule(parent, data)
{
    auto *rootLayout = new QVBoxLayout(widget());

    auto *description = new QLabel(
        QStringLiteral(
            "Customize the trail that follows the mouse pointer and the "
            "text caret. Each has its own colors and timing. Changes are "
            "applied when you click Apply."),
        widget());
    description->setWordWrap(true);
    rootLayout->addWidget(description);

    m_preview = new CursorTrailOnPreview(widget());
    rootLayout->addWidget(m_preview);

    // --- Mouse pointer ---------------------------------------------------
    auto *mouseBox = new QGroupBox(QStringLiteral("Mouse pointer trail"), widget());
    auto *mouseForm = new QFormLayout(mouseBox);

    m_mouseEnabled = new QCheckBox(
        QStringLiteral("Show trail behind the mouse pointer"), mouseBox);
    mouseForm->addRow(m_mouseEnabled);

    mouseForm->addRow(
        QStringLiteral("Trail width:"),
        makeSliderRow(m_trailWidthSlider, m_trailWidth,
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

    buildColorRows(mouseForm, mouseBox, Target::Pointer);
    rootLayout->addWidget(mouseBox);

    // --- Text caret ------------------------------------------------------
    auto *caretBox = new QGroupBox(QStringLiteral("Text caret trail"), widget());
    auto *caretForm = new QFormLayout(caretBox);

    m_caretEnabled = new QCheckBox(
        QStringLiteral("Show trail when the text caret moves"), caretBox);
    m_caretEnabled->setToolTip(QStringLiteral(
        "Works with Wayland applications that report their caret position "
        "to the compositor (GTK and Qt text fields, most Electron/Chromium "
        "apps running on Wayland). XWayland applications and terminals "
        "that do not use the text-input protocol are not tracked."));
    caretForm->addRow(m_caretEnabled);

    auto *widthRow = makeSliderRow(m_caretWidthSlider, m_caretWidth,
                                   MinCaretWidth, MaxCaretWidth);
    widthRow->setToolTip(QStringLiteral(
        "Width of the trail when the caret moves up or down. The trail "
        "starts at the edge of the caret and never covers it."));
    caretForm->addRow(QStringLiteral("Caret trail width:"), widthRow);

    auto *heightRow = makeSliderRow(m_caretHeightSlider, m_caretHeight,
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

    buildColorRows(caretForm, caretBox, Target::Caret);
    rootLayout->addWidget(caretBox);

    // --- General ---------------------------------------------------------
    m_disableInFullscreen = new QCheckBox(
        QStringLiteral("Disable in full-screen apps and games"),
        widget());
    rootLayout->addWidget(m_disableInFullscreen);

    auto *about = new QLabel(widget());
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

    for (QCheckBox *box : {m_mouseEnabled, m_caretEnabled,
                           m_pointerColors.glowEnabled,
                           m_caretColors.glowEnabled,
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
    for (QDoubleSpinBox *spin : {m_pointerColors.intensity,
                                 m_caretColors.intensity}) {
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                &CursorTrailOnConfig::settingsChanged);
    }

    load();
}

CursorTrailOnConfig::~CursorTrailOnConfig() = default;

QWidget *CursorTrailOnConfig::makeSliderRow(QSlider *&slider,
                                            QSpinBox *&spin,
                                            int minimum,
                                            int maximum)
{
    auto *row = new QWidget(widget());
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

// Preset + main/center/glow colors + glow switch + intensity, identical for
// the pointer trail and the caret trail but stored independently.
void CursorTrailOnConfig::buildColorRows(QFormLayout *form,
                                         QWidget *parent,
                                         Target target)
{
    ColorWidgets &w = widgetsFor(target);

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
    form->addRow(QStringLiteral("Main color:"), w.mainButton);

    w.coreButton = new QPushButton(parent);
    form->addRow(QStringLiteral("Center color:"), w.coreButton);

    w.glowEnabled = new QCheckBox(
        QStringLiteral("Outer glow / shadow around the trail"), parent);
    form->addRow(w.glowEnabled);

    w.glowButton = new QPushButton(parent);
    form->addRow(QStringLiteral("Glow color:"), w.glowButton);

    w.intensity = new QDoubleSpinBox(parent);
    w.intensity->setRange(0.15, 2.0);
    w.intensity->setSingleStep(0.05);
    w.intensity->setDecimals(2);
    w.intensity->setSuffix(QStringLiteral("×"));
    form->addRow(QStringLiteral("Intensity (opacity):"), w.intensity);

    connect(w.mainButton, &QPushButton::clicked, this,
            [this, target] { chooseColor(target, ColorRole::Main); });
    connect(w.coreButton, &QPushButton::clicked, this,
            [this, target] { chooseColor(target, ColorRole::Core); });
    connect(w.glowButton, &QPushButton::clicked, this,
            [this, target] { chooseColor(target, ColorRole::Glow); });
    connect(w.preset, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, target](int index) { presetChanged(target, index); });
}

CursorTrailOnConfig::ColorWidgets &CursorTrailOnConfig::widgetsFor(Target target)
{
    return target == Target::Pointer ? m_pointerColors : m_caretColors;
}

CursorTrailOnConfig::Palette &CursorTrailOnConfig::paletteFor(Target target)
{
    return target == Target::Pointer ? m_pointerPalette : m_caretPalette;
}

bool CursorTrailOnConfig::Palette::operator==(const Palette &other) const
{
    return main.rgb() == other.main.rgb()
        && core.rgb() == other.core.rgb()
        && glow.rgb() == other.glow.rgb();
}

bool CursorTrailOnConfig::Settings::operator==(const Settings &other) const
{
    return mouseEnabled == other.mouseEnabled
        && caretEnabled == other.caretEnabled
        && palette == other.palette
        && glowEnabled == other.glowEnabled
        && qFuzzyCompare(intensity + 1.0, other.intensity + 1.0)
        && trailWidth == other.trailWidth
        && trailDuration == other.trailDuration
        && activationSpeed == other.activationSpeed
        && smoothness == other.smoothness
        && caretPalette == other.caretPalette
        && caretGlowEnabled == other.caretGlowEnabled
        && qFuzzyCompare(caretIntensity + 1.0, other.caretIntensity + 1.0)
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

CursorTrailOnConfig::Settings CursorTrailOnConfig::defaultSettings()
{
    Settings settings;
    settings.palette = presetPalette(0);
    settings.caretPalette = presetPalette(0);
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

    // --- pointer trail
    settings.palette = readPalette("Color", "CoreColor", "GlowColor",
                                   settings.palette);
    settings.glowEnabled = group.readEntry("GlowEnabled", settings.glowEnabled);
    settings.intensity = group.readEntry("Intensity", settings.intensity);

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
    settings.caretPalette = group.hasKey("CaretColor")
        ? readPalette("CaretColor", "CaretCoreColor", "CaretGlowColor",
                      settings.caretPalette)
        : settings.palette;
    settings.caretGlowEnabled = group.readEntry(
        "CaretGlowEnabled", settings.glowEnabled);
    settings.caretIntensity = group.readEntry(
        "CaretIntensity", settings.intensity);
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
    Settings settings;
    settings.mouseEnabled = m_mouseEnabled->isChecked();
    settings.caretEnabled = m_caretEnabled->isChecked();

    settings.palette = m_pointerPalette;
    settings.glowEnabled = m_pointerColors.glowEnabled->isChecked();
    settings.intensity = m_pointerColors.intensity->value();
    settings.trailWidth = m_trailWidth->value();
    settings.trailDuration = m_trailDuration->value();
    settings.activationSpeed = m_activationSpeed->value();
    settings.smoothness = m_smoothness->value();

    settings.caretPalette = m_caretPalette;
    settings.caretGlowEnabled = m_caretColors.glowEnabled->isChecked();
    settings.caretIntensity = m_caretColors.intensity->value();
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

    m_pointerPalette = settings.palette;
    m_caretPalette = settings.caretPalette;

    m_mouseEnabled->setChecked(settings.mouseEnabled);
    m_caretEnabled->setChecked(settings.caretEnabled);

    m_pointerColors.preset->setCurrentIndex(presetForPalette(m_pointerPalette));
    m_pointerColors.glowEnabled->setChecked(settings.glowEnabled);
    m_pointerColors.intensity->setValue(settings.intensity);
    m_trailWidth->setValue(settings.trailWidth);
    m_trailDuration->setValue(settings.trailDuration);
    m_activationSpeed->setValue(settings.activationSpeed);
    m_smoothness->setValue(settings.smoothness);

    m_caretColors.preset->setCurrentIndex(presetForPalette(m_caretPalette));
    m_caretColors.glowEnabled->setChecked(settings.caretGlowEnabled);
    m_caretColors.intensity->setValue(settings.caretIntensity);
    m_caretWidth->setValue(settings.caretWidth);
    m_caretHeight->setValue(settings.caretHeight);
    m_caretMinSpeed->setValue(settings.caretMinSpeed);
    m_caretDuration->setValue(settings.caretDuration);
    m_caretPollInterval->setValue(settings.caretPollInterval);

    m_disableInFullscreen->setChecked(settings.disableInFullscreen);

    updateColorButtons(Target::Pointer);
    updateColorButtons(Target::Caret);
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

    group.writeEntry("Color", settings.palette.main);
    group.writeEntry("CoreColor", settings.palette.core);
    group.writeEntry("GlowColor", settings.palette.glow);
    group.writeEntry("GlowEnabled", settings.glowEnabled);
    group.writeEntry("Intensity", settings.intensity);
    group.writeEntry("TrailWidth", qreal(settings.trailWidth));
    group.writeEntry("TrailDuration", settings.trailDuration);
    group.writeEntry("ActivationSpeed", settings.activationSpeed);
    group.writeEntry("Smoothness", settings.smoothness);

    group.writeEntry("CaretColor", settings.caretPalette.main);
    group.writeEntry("CaretCoreColor", settings.caretPalette.core);
    group.writeEntry("CaretGlowColor", settings.caretPalette.glow);
    group.writeEntry("CaretGlowEnabled", settings.caretGlowEnabled);
    group.writeEntry("CaretIntensity", settings.caretIntensity);
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

    const QColor selected = QColorDialog::getColor(*color, widget(), title);
    if (!selected.isValid()) {
        return;
    }

    QColor opaque = selected;
    opaque.setAlpha(255); // opacity is controlled by Intensity
    *color = opaque;

    {
        const QSignalBlocker blocker(widgetsFor(target).preset);
        widgetsFor(target).preset->setCurrentIndex(presetForPalette(palette));
    }

    updateColorButtons(target);
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
    ColorWidgets &w = widgetsFor(target);
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
             m_smoothness, m_trailDuration,
             m_pointerColors.preset, m_pointerColors.mainButton,
             m_pointerColors.coreButton, m_pointerColors.glowEnabled,
             m_pointerColors.intensity}) {
        w->setEnabled(mouse);
    }
    m_pointerColors.glowButton->setEnabled(
        mouse && m_pointerColors.glowEnabled->isChecked());

    for (QWidget *w : std::initializer_list<QWidget *>{
             m_caretWidthSlider, m_caretWidth, m_caretHeightSlider,
             m_caretHeight, m_caretMinSpeed, m_caretDuration,
             m_caretPollInterval,
             m_caretColors.preset, m_caretColors.mainButton,
             m_caretColors.coreButton, m_caretColors.glowEnabled,
             m_caretColors.intensity}) {
        w->setEnabled(caret);
    }
    m_caretColors.glowButton->setEnabled(
        caret && m_caretColors.glowEnabled->isChecked());
}

void CursorTrailOnConfig::updatePreview()
{
    CursorTrailOnPreview::Appearance appearance;
    appearance.main = m_pointerPalette.main;
    appearance.core = m_pointerPalette.core;
    appearance.glow = m_pointerPalette.glow;
    appearance.glowEnabled = m_pointerColors.glowEnabled->isChecked();
    appearance.intensity = m_pointerColors.intensity->value();
    appearance.trailWidth = m_trailWidth->value();

    appearance.caretMain = m_caretPalette.main;
    appearance.caretCore = m_caretPalette.core;
    appearance.caretGlow = m_caretPalette.glow;
    appearance.caretGlowEnabled = m_caretColors.glowEnabled->isChecked();
    appearance.caretIntensity = m_caretColors.intensity->value();
    appearance.caretWidth = m_caretWidth->value();
    appearance.caretHeight = m_caretHeight->value();

    appearance.mouseEnabled = m_mouseEnabled->isChecked();
    appearance.caretEnabled = m_caretEnabled->isChecked();
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
