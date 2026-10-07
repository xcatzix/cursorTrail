/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * cursorTrailOn — based on WindTrail by Wesley (ProxyX).
 */

#include "cursortrailon.h"

#include "core/colorspace.h"
#include "core/rendertarget.h"
#include "core/renderviewport.h"
#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "opengl/glshader.h"
#include "opengl/glshadermanager.h"
#include "opengl/glvertexbuffer.h"

// Text caret support relies on KWin's Wayland text-input interfaces. The
// build system defines CURSORTRAILON_HAVE_TI_V{1,2,3} for the ones whose
// headers are installed by kwin-devel.
#if defined(CURSORTRAILON_HAVE_TI_V1) || defined(CURSORTRAILON_HAVE_TI_V2) \
    || defined(CURSORTRAILON_HAVE_TI_V3)
#define CURSORTRAILON_HAVE_TEXT_INPUT 1
#include "wayland/seat.h"
#include "wayland/surface.h"
#include "wayland_server.h"
#ifdef CURSORTRAILON_HAVE_TI_V1
#include "wayland/textinput_v1.h"
#endif
#ifdef CURSORTRAILON_HAVE_TI_V2
#include "wayland/textinput_v2.h"
#endif
#ifdef CURSORTRAILON_HAVE_TI_V3
#include "wayland/textinput_v3.h"
#endif
#endif

#include <KConfigGroup>

#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector2D>

#include <epoxy/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace KWin
{

namespace
{

qreal clamp01(qreal value)
{
    return std::clamp(value, qreal(0.0), qreal(1.0));
}

// KWin 6.7 returns its own Rect/RectF types from several APIs where older
// versions returned QRect; all of them expose x()/y()/width()/height().
template<typename Rect>
QRectF toQRectF(const Rect &rect)
{
    return QRectF(qreal(rect.x()), qreal(rect.y()),
                  qreal(rect.width()), qreal(rect.height()));
}

qreal smoothStep(qreal value)
{
    const qreal x = clamp01(value);
    return x * x * (3.0 - 2.0 * x);
}

void appendQuad(QList<QVector2D> &vertices,
                const QPointF &a,
                const QPointF &b,
                qreal widthA,
                qreal widthB,
                qreal scale)
{
    const QPointF direction = b - a;
    const qreal length = std::hypot(direction.x(), direction.y());

    if (length < 0.001) {
        return;
    }

    const QPointF normal(-direction.y() / length, direction.x() / length);
    const QPointF offsetA = normal * (widthA * 0.5);
    const QPointF offsetB = normal * (widthB * 0.5);

    const QPointF aLeft = a + offsetA;
    const QPointF aRight = a - offsetA;
    const QPointF bLeft = b + offsetB;
    const QPointF bRight = b - offsetB;

    vertices.push_back(QVector2D(aLeft.x() * scale, aLeft.y() * scale));
    vertices.push_back(QVector2D(aRight.x() * scale, aRight.y() * scale));
    vertices.push_back(QVector2D(bRight.x() * scale, bRight.y() * scale));

    vertices.push_back(QVector2D(aLeft.x() * scale, aLeft.y() * scale));
    vertices.push_back(QVector2D(bRight.x() * scale, bRight.y() * scale));
    vertices.push_back(QVector2D(bLeft.x() * scale, bLeft.y() * scale));
}

void appendTriangle(QList<QVector2D> &vertices,
                    const QPointF &a,
                    const QPointF &b,
                    const QPointF &c,
                    qreal scale)
{
    vertices.push_back(QVector2D(a.x() * scale, a.y() * scale));
    vertices.push_back(QVector2D(b.x() * scale, b.y() * scale));
    vertices.push_back(QVector2D(c.x() * scale, c.y() * scale));
}

void appendDisc(QList<QVector2D> &vertices,
                const QPointF &center,
                qreal radius,
                qreal scale)
{
    constexpr int Segments = 28;
    const qreal step = 2.0 * M_PI / Segments;
    for (int i = 0; i < Segments; ++i) {
        const qreal a0 = i * step;
        const qreal a1 = (i + 1) * step;
        appendTriangle(vertices,
                       center,
                       center + QPointF(std::cos(a0), std::sin(a0)) * radius,
                       center + QPointF(std::cos(a1), std::sin(a1)) * radius,
                       scale);
    }
}

} // namespace

CursorTrailOnEffect::CursorTrailOnEffect()
{
    m_clock.start();

    connect(effects,
            &EffectsHandler::mouseChanged,
            this,
            &CursorTrailOnEffect::onMouseChanged);

    m_caretTimer = new QTimer(this);
    m_caretTimer->setInterval(m_caretPollIntervalMs);
    connect(m_caretTimer,
            &QTimer::timeout,
            this,
            &CursorTrailOnEffect::pollTextCaret);

    reconfigure(ReconfigureAll);
}

CursorTrailOnEffect::~CursorTrailOnEffect() = default;

void CursorTrailOnEffect::reconfigure(ReconfigureFlags)
{
    const KConfigGroup group(effects->config(),
                             QStringLiteral("Effect-cursortrailon"));

    // Opacity is controlled by the Intensity setting, not the color picker.
    const auto readColor = [&group](const char *key, const QColor &fallback) {
        QColor color = group.readEntry(key, fallback);
        if (!color.isValid()) {
            color = fallback;
        }
        color.setAlpha(255);
        return color;
    };

    TrailStyle &ps = m_pointerStyle;
    ps.main = readColor("Color", QColor(255, 6, 12));
    // Older configurations had no separate center/glow colors: derive them
    // from the main color exactly as before.
    ps.core = readColor(
        "CoreColor", mixColors(ps.main, QColor(255, 255, 255), 0.72));
    ps.glow = readColor(
        "GlowColor", mixColors(ps.main, QColor(0, 0, 0), 0.28));
    ps.glowEnabled = group.readEntry("GlowEnabled", true);
    ps.intensity = std::clamp(group.readEntry("Intensity", 1.0), 0.15, 2.0);

    // Gradient (mix of several colors) and light effect.
    const auto readExtras = [&group](TrailStyle &style,
                                     const QString &prefix,
                                     const TrailStyle &fallback) {
        style.gradientEnabled = group.readEntry(
            prefix + QStringLiteral("GradientEnabled"), fallback.gradientEnabled);
        style.gradientSmooth = group.readEntry(
            prefix + QStringLiteral("GradientSmooth"), fallback.gradientSmooth);
        style.stops = parseStops(group.readEntry(
            prefix + QStringLiteral("GradientStops"), QString()));
        if (style.stops.empty()) {
            style.stops = fallback.stops;
        }
        if (style.stops.empty()) {
            style.stops = {
                {0.0, QColor(255, 61, 0)},
                {0.5, QColor(255, 0, 153)},
                {1.0, QColor(106, 0, 255)},
            };
        }

        style.lightEnabled = group.readEntry(
            prefix + QStringLiteral("LightEnabled"), fallback.lightEnabled);
        style.lightStrength = std::clamp(group.readEntry(
            prefix + QStringLiteral("LightStrength"), fallback.lightStrength),
            0.1, 3.0);
        style.lightRadius = std::clamp(group.readEntry(
            prefix + QStringLiteral("LightRadius"), fallback.lightRadius * 100.0)
            / 100.0, 0.5, 4.0);
        style.headLight = group.readEntry(
            prefix + QStringLiteral("HeadLight"), fallback.headLight);
    };
    readExtras(ps, QString(), TrailStyle());

    m_mouseEnabled = group.readEntry("MouseTrailEnabled", true);
#ifdef CURSORTRAILON_HAVE_TEXT_INPUT
    m_caretEnabled = group.readEntry("TextCaretEnabled", true);
#else
    m_caretEnabled = false;
#endif

    // "TrailWidth" is the maximum width of the main ribbon in pixels. The
    // legacy "Thickness" multiplier is still honoured when it is the only
    // width setting present.
    qreal trailWidth = MouseWidthAtScaleOne;
    if (group.hasKey("TrailWidth")) {
        trailWidth = group.readEntry("TrailWidth", MouseWidthAtScaleOne);
    } else if (group.hasKey("Thickness")) {
        trailWidth = group.readEntry("Thickness", 1.0) * MouseWidthAtScaleOne;
    }
    trailWidth = std::clamp<qreal>(trailWidth, 2.0, 80.0);
    m_thickness = trailWidth / MouseWidthAtScaleOne;

    m_activationSpeed = std::clamp(
        qreal(group.readEntry("ActivationSpeed", 320)),
        qreal(100.0),
        qreal(1600.0));
    m_trailDurationMs = std::clamp<qint64>(
        group.readEntry("TrailDuration", 330),
        120,
        650);
    m_smoothingPasses = std::clamp(
        group.readEntry("Smoothness", 2),
        0,
        4);
    m_disableInFullscreen = group.readEntry("DisableInFullscreen", true);

    // --- text caret trail: its own colors, duration and geometry. Missing
    // keys (settings saved by 1.1.x) fall back to the pointer-trail values.
    TrailStyle &cs = m_caretStyle;
    cs.main = readColor("CaretColor", ps.main);
    cs.core = readColor(
        "CaretCoreColor",
        group.hasKey("CaretColor")
            ? mixColors(cs.main, QColor(255, 255, 255), 0.72)
            : ps.core);
    cs.glow = readColor(
        "CaretGlowColor",
        group.hasKey("CaretColor")
            ? mixColors(cs.main, QColor(0, 0, 0), 0.28)
            : ps.glow);
    cs.glowEnabled = group.readEntry("CaretGlowEnabled", ps.glowEnabled);
    cs.intensity = std::clamp(
        group.readEntry("CaretIntensity", ps.intensity), 0.15, 2.0);
    readExtras(cs, QStringLiteral("Caret"), ps);
    m_caretWidth = std::clamp<qreal>(
        group.readEntry("CaretTrailWidth", 8.0), 1.0, 60.0);
    m_caretMaxHeight = std::clamp<qreal>(
        group.readEntry("CaretTrailHeight", 40.0), 4.0, 120.0);
    m_caretMinSpeed = std::clamp<qreal>(
        group.readEntry("CaretMinSpeed", 0.0), 0.0, 3000.0);
    m_caretDurationMs = std::clamp<qint64>(
        group.readEntry("CaretTrailDuration", qint64(m_trailDurationMs)),
        80,
        1000);
    m_caretPollIntervalMs = std::clamp(
        group.readEntry("CaretPollInterval", 16), 4, 200);
    m_caretTimer->setInterval(m_caretPollIntervalMs);

    if (m_caretEnabled) {
        if (!m_caretTimer->isActive()) {
            m_caretTimer->start();
        }
    } else {
        m_caretTimer->stop();
    }

    clearTrail();
    clearCaretTrail();
    effects->addRepaintFull();
}

void CursorTrailOnEffect::prePaintScreen(ScreenPrePaintData &data)
{
    if (isSuppressed()) {
        clearTrail();
        clearCaretTrail();
    } else {
        const qint64 nowMs = m_clock.elapsed();
        pruneExpired(nowMs);
        pruneCaretExpired(nowMs);
    }

    effects->prePaintScreen(data);
}

void CursorTrailOnEffect::paintScreen(const RenderTarget &renderTarget,
                                      const RenderViewport &viewport,
                                      int mask,
                                      const Region &deviceRegion,
                                      LogicalOutput *screen)
{
    effects->paintScreen(renderTarget, viewport, mask, deviceRegion, screen);

    if (!effects->isOpenGLCompositing() || isSuppressed()) {
        return;
    }

    const bool drawMouse = m_mouseEnabled && m_samples.size() >= 2;
    const bool drawCaret = m_caretEnabled && m_caretSamples.size() >= 2;

    if (!drawMouse && !drawCaret) {
        return;
    }

    const qint64 nowMs = m_clock.elapsed();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (drawMouse) {
        const std::vector<Sample> samples = smoothedSamples();
        const TrailStyle &st = m_pointerStyle;

        if (samples.size() >= 2) {
            // Light effect first: additive bloom under the actual trail.
            if (st.lightEnabled) {
                const qreal r = st.lightRadius;
                const qreal k = st.lightStrength * st.intensity;
                drawRibbonPass(samples, renderTarget, viewport,
                               6.0 * r * m_thickness, Paint::Light,
                               0.020 * k, nowMs);
                drawRibbonPass(samples, renderTarget, viewport,
                               3.8 * r * m_thickness, Paint::Light,
                               0.040 * k, nowMs);
                drawRibbonPass(samples, renderTarget, viewport,
                               2.2 * r * m_thickness, Paint::Light,
                               0.075 * k, nowMs);
            }

            if (st.glowEnabled) {
                drawRibbonPass(samples,
                               renderTarget,
                               viewport,
                               2.05 * m_thickness,
                               Paint::Glow,
                               0.11 * st.intensity,
                               nowMs);
            }

            drawRibbonPass(samples,
                           renderTarget,
                           viewport,
                           1.00 * m_thickness,
                           Paint::Main,
                           0.38 * st.intensity,
                           nowMs);

            drawRibbonPass(samples,
                           renderTarget,
                           viewport,
                           0.27 * m_thickness,
                           Paint::Core,
                           0.78 * st.intensity,
                           nowMs);

            if (st.lightEnabled && st.headLight) {
                const qreal newestAge = qreal(std::max<qint64>(
                    0, nowMs - samples.back().timestampMs));
                const qreal fade = clamp01(
                    1.0 - newestAge / lifetimeForSpeed(samples.back().speed));
                drawHeadLight(st, renderTarget, viewport,
                              samples.back().position,
                              MouseWidthAtScaleOne * m_thickness
                                  * 0.9 * st.lightRadius,
                              fade * st.lightStrength);
            }
        }
    }

    if (drawCaret) {
        const TrailStyle &st = m_caretStyle;

        if (st.lightEnabled) {
            const qreal r = st.lightRadius;
            const qreal k = st.lightStrength * st.intensity;
            drawCaretPass(renderTarget, viewport, 4.2 * r,
                          Paint::Light, 0.020 * k, nowMs);
            drawCaretPass(renderTarget, viewport, 2.8 * r,
                          Paint::Light, 0.040 * k, nowMs);
            drawCaretPass(renderTarget, viewport, 1.9 * r,
                          Paint::Light, 0.075 * k, nowMs);
        }

        if (st.glowEnabled) {
            drawCaretPass(renderTarget, viewport, 1.6,
                          Paint::Glow, 0.14 * st.intensity, nowMs);
        }

        drawCaretPass(renderTarget, viewport, 1.0,
                      Paint::Main, 0.50 * st.intensity, nowMs);

        drawCaretPass(renderTarget, viewport, 0.40,
                      Paint::Core, 0.85 * st.intensity, nowMs);

        if (st.lightEnabled && st.headLight) {
            const CaretSample &last = m_caretSamples.back();
            const qreal life = smoothStep(clamp01(
                1.0 - qreal(std::max<qint64>(0, nowMs - last.timestampMs))
                    / std::max<qreal>(1.0, qreal(m_caretDurationMs))));
            drawHeadLight(st, renderTarget, viewport,
                          last.rect.center(),
                          std::max<qreal>(
                              std::min(std::max<qreal>(last.rect.height(), 4.0),
                                       m_caretMaxHeight) * 0.9,
                              m_caretWidth)
                              * st.lightRadius,
                          life * st.lightStrength);
        }
    }

    glDisable(GL_BLEND);
}

void CursorTrailOnEffect::postPaintScreen()
{
    effects->postPaintScreen();

    if ((!m_samples.empty() || !m_caretSamples.empty()) && !isSuppressed()) {
        effects->addRepaintFull();
    }
}

bool CursorTrailOnEffect::isActive() const
{
    return !isSuppressed()
        && ((m_mouseEnabled && m_samples.size() >= 2)
            || (m_caretEnabled && m_caretSamples.size() >= 2));
}

void CursorTrailOnEffect::onMouseChanged(const QPointF &pos,
                                         const QPointF &oldPos,
                                         Qt::MouseButtons,
                                         Qt::MouseButtons,
                                         Qt::KeyboardModifiers,
                                         Qt::KeyboardModifiers)
{
    if (!m_mouseEnabled) {
        return;
    }

    if (isSuppressed()) {
        clearTrail();
        return;
    }

    const qint64 nowMs = m_clock.elapsed();

    if (!m_haveLastPosition) {
        m_lastPosition = oldPos;
        m_lastEventMs = nowMs;
        m_haveLastPosition = true;
    }

    const QPointF delta = pos - m_lastPosition;
    const qreal distance = std::hypot(delta.x(), delta.y());
    const qint64 elapsedMs = std::max<qint64>(1, nowMs - m_lastEventMs);
    const qreal rawSpeed = distance * 1000.0 / qreal(elapsedMs);

    if (distance > MaximumJumpDistance) {
        clearTrail();
        m_haveLastPosition = true;
        m_lastPosition = pos;
        m_lastEventMs = nowMs;
        return;
    }

    if (m_filteredSpeed <= 0.0) {
        m_filteredSpeed = rawSpeed;
    } else {
        constexpr qreal NewSampleWeight = 0.46;
        m_filteredSpeed = m_filteredSpeed * (1.0 - NewSampleWeight)
            + rawSpeed * NewSampleWeight;
    }

    if (distance >= MinimumDistance) {
        if (m_samples.empty() && m_filteredSpeed >= m_activationSpeed) {
            m_samples.push_back(Sample{
                .position = m_lastPosition,
                .timestampMs = std::max<qint64>(0, nowMs - elapsedMs),
                .speed = m_filteredSpeed,
            });
        }

        if (!m_samples.empty() || m_filteredSpeed >= m_activationSpeed) {
            m_samples.push_back(Sample{
                .position = pos,
                .timestampMs = nowMs,
                .speed = m_filteredSpeed,
            });
        }
    }

    while (m_samples.size() > MaximumSamples) {
        m_samples.pop_front();
    }

    pruneExpired(nowMs);

    m_lastPosition = pos;
    m_lastEventMs = nowMs;

    if (!m_samples.empty()) {
        effects->addRepaintFull();
    }
}

bool CursorTrailOnEffect::queryTextCaret(CaretInfo &info) const
{
#ifdef CURSORTRAILON_HAVE_TEXT_INPUT
    WaylandServer *server = waylandServer();
    if (!server) {
        return false;
    }

    SeatInterface *seat = server->seat();
    if (!seat) {
        return false;
    }

    SurfaceInterface *surface = seat->focusedTextInputSurface();
    if (!surface) {
        return false;
    }

    QRectF local;
    bool found = false;

    // A client normally uses one protocol version at a time; if several are
    // enabled for the focused surface the newest one wins, as in KWin itself.
#ifdef CURSORTRAILON_HAVE_TI_V1
    if (auto *input = seat->textInputV1();
        input && input->isEnabled() && input->surface() == surface) {
        local = toQRectF(input->cursorRectangle());
        found = true;
    }
#endif
#ifdef CURSORTRAILON_HAVE_TI_V2
    if (auto *input = seat->textInputV2();
        input && input->isEnabled() && input->surface() == surface) {
        local = toQRectF(input->cursorRectangle());
        found = true;
    }
#endif
#ifdef CURSORTRAILON_HAVE_TI_V3
    if (auto *input = seat->textInputV3();
        input && input->isEnabled() && input->surface() == surface) {
        local = toQRectF(input->cursorRectangle());
        found = true;
    }
#endif

    // Clients that never report a rectangle leave it empty.
    if (!found || local.height() <= 0) {
        return false;
    }

    EffectWindow *window = effects->findWindow(surface);
    if (!window) {
        return false;
    }

    const auto bufferGeometry = window->bufferGeometry();
    info.localRect = local;
    info.origin = QPointF(qreal(bufferGeometry.x()), qreal(bufferGeometry.y()));
    info.window = window;
    return true;
#else
    Q_UNUSED(info)
    return false;
#endif
}

void CursorTrailOnEffect::pollTextCaret()
{
    if (!m_caretEnabled || isSuppressed()) {
        clearCaretTrail();
        return;
    }

    const qint64 nowMs = m_clock.elapsed();

    CaretInfo info;
    if (!queryTextCaret(info)) {
        // No focused text field (or the client does not report a caret).
        m_haveCaret = false;
        return;
    }

    // Focus moved to another window/field, or the window itself moved:
    // re-anchor without drawing a trail across unrelated positions.
    if (!m_haveCaret
        || info.window != m_lastCaret.window
        || info.origin != m_lastCaret.origin) {
        clearCaretTrail();
        m_lastCaret = info;
        m_lastCaretChangeMs = nowMs;
        m_haveCaret = true;
        return;
    }

    if (info.localRect == m_lastCaret.localRect) {
        return;
    }

    const QRectF previous = m_lastCaret.localRect.translated(m_lastCaret.origin);
    const QRectF current = info.localRect.translated(info.origin);
    m_lastCaret = info;

    // Speed of this move: distance over the time since the previous change.
    const qint64 elapsedMs = std::max<qint64>(
        qint64(m_caretPollIntervalMs), nowMs - m_lastCaretChangeMs);
    m_lastCaretChangeMs = nowMs;

    const QPointF delta = current.center() - previous.center();
    const qreal distance = std::hypot(delta.x(), delta.y());
    if (distance < 0.5) {
        return;
    }

    const qreal speed = distance * 1000.0 / qreal(elapsedMs);
    if (speed < m_caretMinSpeed) {
        return;
    }

    if (m_caretSamples.empty()) {
        m_caretSamples.push_back(CaretSample{
            .rect = previous,
            .timestampMs = std::max<qint64>(0, nowMs - m_caretPollIntervalMs),
            .speed = speed,
        });
    }

    m_caretSamples.push_back(CaretSample{
        .rect = current,
        .timestampMs = nowMs,
        .speed = speed,
    });

    while (m_caretSamples.size() > MaximumCaretSamples) {
        m_caretSamples.pop_front();
    }

    effects->addRepaintFull();
}

bool CursorTrailOnEffect::isSuppressed() const
{
    if (effects->isScreenLocked()) {
        return true;
    }

    if (m_disableInFullscreen) {
        EffectWindow *window = effects->activeWindow();
        if (window && window->isFullScreen()) {
            return true;
        }
    }

    return false;
}

void CursorTrailOnEffect::clearTrail()
{
    m_samples.clear();
    m_filteredSpeed = 0.0;
    m_haveLastPosition = false;
}

void CursorTrailOnEffect::clearCaretTrail()
{
    m_caretSamples.clear();
    m_haveCaret = false;
}

void CursorTrailOnEffect::pruneExpired(qint64 nowMs)
{
    while (!m_samples.empty()
           && nowMs - m_samples.front().timestampMs > m_trailDurationMs) {
        m_samples.pop_front();
    }

    if (m_samples.size() == 1
        && nowMs - m_samples.front().timestampMs > m_trailDurationMs / 2) {
        m_samples.clear();
    }
}

void CursorTrailOnEffect::pruneCaretExpired(qint64 nowMs)
{
    while (!m_caretSamples.empty()
           && nowMs - m_caretSamples.front().timestampMs > m_caretDurationMs) {
        m_caretSamples.pop_front();
    }

    if (m_caretSamples.size() == 1) {
        m_caretSamples.clear();
    }
}

std::vector<CursorTrailOnEffect::Sample> CursorTrailOnEffect::smoothedSamples() const
{
    std::vector<Sample> current(m_samples.begin(), m_samples.end());

    if (current.size() < 3 || m_smoothingPasses <= 0) {
        return current;
    }

    const auto chaikinPass = [](const std::vector<Sample> &input) {
        std::vector<Sample> output;
        output.reserve(input.size() * 2);
        output.push_back(input.front());

        for (std::size_t index = 0; index + 1 < input.size(); ++index) {
            const Sample &a = input[index];
            const Sample &b = input[index + 1];

            output.push_back(Sample{
                .position = interpolatePoint(a.position, b.position, 0.25),
                .timestampMs = qRound64(interpolateValue(
                    qreal(a.timestampMs), qreal(b.timestampMs), 0.25)),
                .speed = interpolateValue(a.speed, b.speed, 0.25),
            });

            output.push_back(Sample{
                .position = interpolatePoint(a.position, b.position, 0.75),
                .timestampMs = qRound64(interpolateValue(
                    qreal(a.timestampMs), qreal(b.timestampMs), 0.75)),
                .speed = interpolateValue(a.speed, b.speed, 0.75),
            });
        }

        output.push_back(input.back());
        return output;
    };

    for (int pass = 0; pass < m_smoothingPasses; ++pass) {
        current = chaikinPass(current);
    }

    return current;
}

void CursorTrailOnEffect::drawRibbonPass(const std::vector<Sample> &samples,
                                         const RenderTarget &renderTarget,
                                         const RenderViewport &viewport,
                                         qreal widthScale,
                                         Paint paint,
                                         qreal baseAlpha,
                                         qint64 nowMs)
{
    if (samples.size() < 2) {
        return;
    }

    const TrailStyle &style = m_pointerStyle;

    const qint64 newestAgeMs = std::max<qint64>(
        0, nowMs - samples.back().timestampMs);
    const qreal newestLifetime = lifetimeForSpeed(samples.back().speed);
    const qreal fadeDuration = stopFadeDuration();
    const qreal fadeStartMs = std::max<qreal>(
        0.0, newestLifetime - fadeDuration);

    qreal stopFade = 1.0;
    if (qreal(newestAgeMs) > fadeStartMs) {
        stopFade = clamp01(
            (newestLifetime - qreal(newestAgeMs)) / fadeDuration);
    }

    if (stopFade <= 0.001) {
        return;
    }

    const int bins = style.gradientEnabled ? GradientBins : 1;
    Buckets buckets(std::size_t(AlphaBuckets * bins));
    const qreal scale = viewport.scale();
    const qreal widthSpeed = fullWidthSpeed();
    const std::size_t segments = samples.size() - 1;

    for (std::size_t index = 0; index < segments; ++index) {
        const Sample &a = samples[index];
        const Sample &b = samples[index + 1];

        const qreal ageA = qreal(std::max<qint64>(
            0, nowMs - a.timestampMs));
        const qreal ageB = qreal(std::max<qint64>(
            0, nowMs - b.timestampMs));

        const qreal lifeA = clamp01(
            1.0 - ageA / lifetimeForSpeed(a.speed));
        const qreal lifeB = clamp01(
            1.0 - ageB / lifetimeForSpeed(b.speed));

        const qreal speedA = smoothStep(
            (a.speed - m_activationSpeed)
            / (widthSpeed - m_activationSpeed));
        const qreal speedB = smoothStep(
            (b.speed - m_activationSpeed)
            / (widthSpeed - m_activationSpeed));

        const qreal tailA = std::pow(smoothStep(lifeA), 1.55);
        const qreal tailB = std::pow(smoothStep(lifeB), 1.55);

        const qreal responsiveSpeedA = std::sqrt(speedA);
        const qreal responsiveSpeedB = std::sqrt(speedB);

        const qreal widthA = widthScale
            * (0.18 + 17.5 * responsiveSpeedA * tailA);
        const qreal widthB = widthScale
            * (0.18 + 17.5 * responsiveSpeedB * tailB);

        const qreal segmentLife = (tailA + tailB) * 0.5;
        if (segmentLife <= 0.002) {
            continue;
        }

        const int alphaBucket = std::clamp(
            int(std::floor(segmentLife * qreal(AlphaBuckets))),
            0,
            AlphaBuckets - 1);

        // Position along the trail: 0 at the newest sample, 1 at the oldest.
        const qreal t = 1.0 - (qreal(index) + 0.5) / qreal(segments);
        const int bin = bins == 1
            ? 0
            : std::clamp(int(t * bins), 0, bins - 1);

        appendQuad(buckets[std::size_t(alphaBucket * bins + bin)],
                   a.position,
                   b.position,
                   widthA,
                   widthB,
                   scale);
    }

    renderBuckets(buckets, bins, style, paint, renderTarget, viewport,
                  baseAlpha, stopFade, paint == Paint::Light);
}

// Each caret move is drawn as an isosceles triangle whose base sits on the
// edge of the caret that faces the trail and whose apex points back to where
// the caret came from. The base never extends over the caret itself, so a wide
// trail cannot cover the text cursor:
//   - horizontal move: the base is the caret's left (moving right) or right
//     (moving left) edge, as tall as the caret, capped by "Maximum height";
//   - vertical move: the base is the caret's top (moving down) or bottom
//     (moving up) edge, "Caret trail width" wide.
// With a gradient the triangle is cut into thin slices along its axis so every
// slice can carry its own color.
void CursorTrailOnEffect::drawCaretPass(const RenderTarget &renderTarget,
                                        const RenderViewport &viewport,
                                        qreal sizeFactor,
                                        Paint paint,
                                        qreal baseAlpha,
                                        qint64 nowMs)
{
    if (m_caretSamples.size() < 2) {
        return;
    }

    const TrailStyle &style = m_caretStyle;
    constexpr int Slices = 12;

    const int bins = style.gradientEnabled ? GradientBins : 1;
    Buckets buckets(std::size_t(AlphaBuckets * bins));
    const qreal scale = viewport.scale();
    const qreal duration = std::max<qreal>(1.0, qreal(m_caretDurationMs));
    const std::size_t segments = m_caretSamples.size() - 1;

    for (std::size_t index = 0; index < segments; ++index) {
        const CaretSample &a = m_caretSamples[index];
        const CaretSample &b = m_caretSamples[index + 1];

        const qreal life = clamp01(
            1.0 - qreal(std::max<qint64>(0, nowMs - b.timestampMs)) / duration);
        const qreal tail = smoothStep(life);
        if (tail <= 0.002) {
            continue;
        }

        const QRectF &prev = a.rect;
        const QRectF &cur = b.rect;
        const QPointF delta = cur.center() - prev.center();
        const qreal caretHeight = std::max<qreal>(cur.height(), 4.0);

        QPointF base1;
        QPointF base2;
        QPointF apex;

        if (std::abs(delta.x()) >= std::abs(delta.y())) {
            // Horizontal: base on the caret's near vertical edge.
            const bool movedRight = delta.x() > 0.0;
            const qreal edgeX = movedRight ? cur.left() : cur.right();
            const qreal farX = movedRight ? prev.left() : prev.right();
            const qreal length = std::abs(edgeX - farX) * tail;
            const qreal apexX = movedRight ? edgeX - length : edgeX + length;

            const qreal halfBase = 0.5 * std::min(caretHeight, m_caretMaxHeight)
                * sizeFactor * (0.45 + 0.55 * tail);
            const qreal headY = cur.center().y();
            const qreal apexY = headY + (prev.center().y() - headY) * tail;

            base1 = QPointF(edgeX, headY - halfBase);
            base2 = QPointF(edgeX, headY + halfBase);
            apex = QPointF(apexX, apexY);
        } else {
            // Vertical: base on the caret's near horizontal edge.
            const bool movedDown = delta.y() > 0.0;
            const qreal edgeY = movedDown ? cur.top() : cur.bottom();
            const qreal farY = movedDown ? prev.top() : prev.bottom();
            const qreal length = std::abs(edgeY - farY) * tail;
            const qreal apexY = movedDown ? edgeY - length : edgeY + length;

            const qreal halfBase = 0.5 * std::max(m_caretWidth, cur.width())
                * sizeFactor * (0.45 + 0.55 * tail);
            const qreal headX = cur.center().x();
            const qreal apexX = headX + (prev.center().x() - headX) * tail;

            base1 = QPointF(headX - halfBase, edgeY);
            base2 = QPointF(headX + halfBase, edgeY);
            apex = QPointF(apexX, apexY);
        }

        const int alphaBucket = std::clamp(
            int(std::floor(tail * qreal(AlphaBuckets))),
            0,
            AlphaBuckets - 1);

        if (bins == 1) {
            appendTriangle(buckets[std::size_t(alphaBucket)],
                           base1, base2, apex, scale);
            continue;
        }

        // Newest segment is closest to the head of the gradient.
        const qreal segmentOffset = qreal(segments - 1 - index);
        for (int slice = 0; slice < Slices; ++slice) {
            const qreal f0 = qreal(slice) / Slices;     // 0 = base
            const qreal f1 = qreal(slice + 1) / Slices; // 1 = apex

            const QPointF l0 = interpolatePoint(base1, apex, f0);
            const QPointF r0 = interpolatePoint(base2, apex, f0);
            const QPointF l1 = interpolatePoint(base1, apex, f1);
            const QPointF r1 = interpolatePoint(base2, apex, f1);

            const qreal t = (segmentOffset + (f0 + f1) * 0.5)
                / qreal(segments);
            const int bin = std::clamp(int(t * bins), 0, bins - 1);

            QList<QVector2D> &list = buckets[std::size_t(alphaBucket * bins + bin)];
            appendTriangle(list, l0, r0, r1, scale);
            appendTriangle(list, l0, r1, l1, scale);
        }
    }

    renderBuckets(buckets, bins, style, paint, renderTarget, viewport,
                  baseAlpha, 1.0, paint == Paint::Light);
}

// A soft round light at the head of the trail: a few stacked, additively
// blended discs approximate a radial falloff.
void CursorTrailOnEffect::drawHeadLight(const TrailStyle &style,
                                        const RenderTarget &renderTarget,
                                        const RenderViewport &viewport,
                                        const QPointF &center,
                                        qreal radius,
                                        qreal fade)
{
    if (fade <= 0.002 || radius < 1.0) {
        return;
    }

    ShaderBinder binder(ShaderTrait::UniformColor
                        | ShaderTrait::TransformColorspace);
    binder.shader()->setUniform(
        GLShader::Mat4Uniform::ModelViewProjectionMatrix,
        viewport.projectionMatrix());
    binder.shader()->setColorspaceUniforms(
        ColorDescription::sRGB,
        renderTarget.colorDescription(),
        RenderingIntent::Perceptual);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    GLVertexBuffer *vbo = GLVertexBuffer::streamingBuffer();
    const qreal scale = viewport.scale();
    constexpr int Rings = 6;

    QColor color = paintColor(style, Paint::Light, 0.0);

    for (int ring = 0; ring < Rings; ++ring) {
        const qreal f = qreal(ring + 1) / Rings; // 1/6 .. 1
        QList<QVector2D> vertices;
        appendDisc(vertices, center, radius * f, scale);

        color.setAlphaF(clamp01(0.035 * style.intensity * fade));
        binder.shader()->setUniform(GLShader::ColorUniform::Color, color);

        vbo->reset();
        vbo->setVertices(vertices);
        vbo->render(GL_TRIANGLES);
    }

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void CursorTrailOnEffect::renderBuckets(Buckets &buckets,
                                        int bins,
                                        const TrailStyle &style,
                                        Paint paint,
                                        const RenderTarget &renderTarget,
                                        const RenderViewport &viewport,
                                        qreal baseAlpha,
                                        qreal fade,
                                        bool additive)
{
    ShaderBinder binder(ShaderTrait::UniformColor
                        | ShaderTrait::TransformColorspace);
    binder.shader()->setUniform(
        GLShader::Mat4Uniform::ModelViewProjectionMatrix,
        viewport.projectionMatrix());
    binder.shader()->setColorspaceUniforms(
        ColorDescription::sRGB,
        renderTarget.colorDescription(),
        RenderingIntent::Perceptual);

    if (additive) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    }

    GLVertexBuffer *vbo = GLVertexBuffer::streamingBuffer();

    for (int alphaBucket = 0; alphaBucket < AlphaBuckets; ++alphaBucket) {
        const qreal bucketOpacity = std::pow(
            qreal(alphaBucket + 1) / qreal(AlphaBuckets),
            1.75);

        for (int bin = 0; bin < bins; ++bin) {
            QList<QVector2D> &vertices
                = buckets[std::size_t(alphaBucket * bins + bin)];

            if (vertices.isEmpty()) {
                continue;
            }

            const qreal t = bins == 1 ? 0.0 : (qreal(bin) + 0.5) / qreal(bins);
            QColor color = paintColor(style, paint, t);
            color.setAlphaF(clamp01(baseAlpha * bucketOpacity * fade));

            binder.shader()->setUniform(
                GLShader::ColorUniform::Color,
                color);

            vbo->reset();
            vbo->setVertices(vertices);
            vbo->render(GL_TRIANGLES);
        }
    }

    if (additive) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
}

// Color of the gradient at position t (0 = head, 1 = tail).
QColor CursorTrailOnEffect::gradientAt(const TrailStyle &style, qreal t)
{
    const std::vector<ColorStop> &stops = style.stops;
    if (stops.empty()) {
        return style.main;
    }
    if (stops.size() == 1) {
        return stops.front().color;
    }

    t = clamp01(t);

    if (t <= stops.front().position) {
        return stops.front().color;
    }
    if (t >= stops.back().position) {
        return stops.back().color;
    }

    for (std::size_t i = 0; i + 1 < stops.size(); ++i) {
        const ColorStop &a = stops[i];
        const ColorStop &b = stops[i + 1];
        if (t >= a.position && t <= b.position) {
            if (!style.gradientSmooth) {
                return a.color; // hard stops: color changes at each position
            }
            const qreal span = b.position - a.position;
            const qreal f = span > 1e-6 ? (t - a.position) / span : 0.0;
            return mixColors(a.color, b.color, f);
        }
    }
    return stops.back().color;
}

QColor CursorTrailOnEffect::paintColor(const TrailStyle &style,
                                       Paint paint,
                                       qreal t)
{
    QColor color;

    switch (paint) {
    case Paint::Main:
        color = style.gradientEnabled ? gradientAt(style, t) : style.main;
        break;
    case Paint::Core:
        // The bright center keeps its own color so the trail has a
        // "hot" core whatever the gradient does around it.
        color = style.core;
        break;
    case Paint::Glow:
        color = style.gradientEnabled
            ? mixColors(gradientAt(style, t), QColor(0, 0, 0), 0.28)
            : style.glow;
        break;
    case Paint::Light:
    default:
        color = mixColors(
            style.gradientEnabled ? gradientAt(style, t) : style.main,
            QColor(255, 255, 255),
            0.35);
        break;
    }

    color.setAlpha(255);
    return color;
}

// Parses "#rrggbb@0.0;#rrggbb@0.5;..." into position-sorted color stops.
std::vector<CursorTrailOnEffect::ColorStop>
CursorTrailOnEffect::parseStops(const QString &text)
{
    std::vector<ColorStop> stops;

    const QStringList parts = text.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QStringList pair = part.trimmed().split(QLatin1Char('@'));
        const QColor color(pair.value(0).trimmed());
        if (!color.isValid()) {
            continue;
        }

        bool ok = false;
        qreal position = pair.value(1).toDouble(&ok);
        if (!ok) {
            position = 0.0;
        }

        ColorStop stop;
        stop.position = clamp01(position);
        stop.color = color;
        stop.color.setAlpha(255);
        stops.push_back(stop);
    }

    std::stable_sort(stops.begin(), stops.end(),
                     [](const ColorStop &a, const ColorStop &b) {
                         return a.position < b.position;
                     });
    return stops;
}

QPointF CursorTrailOnEffect::interpolatePoint(const QPointF &a,
                                              const QPointF &b,
                                              qreal t)
{
    return a * (1.0 - t) + b * t;
}

qreal CursorTrailOnEffect::interpolateValue(qreal a, qreal b, qreal t)
{
    return a * (1.0 - t) + b * t;
}

QColor CursorTrailOnEffect::mixColors(const QColor &a,
                                      const QColor &b,
                                      qreal amount)
{
    const qreal t = clamp01(amount);

    return QColor::fromRgbF(
        interpolateValue(a.redF(), b.redF(), t),
        interpolateValue(a.greenF(), b.greenF(), t),
        interpolateValue(a.blueF(), b.blueF(), t),
        interpolateValue(a.alphaF(), b.alphaF(), t));
}

qreal CursorTrailOnEffect::lifetimeForSpeed(qreal speed) const
{
    const qreal normalized = smoothStep(
        (speed - m_activationSpeed)
        / (fullWidthSpeed() - m_activationSpeed));

    const qreal minimumLifetime = std::max<qreal>(
        90.0, qreal(m_trailDurationMs) * 0.53);

    return interpolateValue(
        minimumLifetime,
        qreal(m_trailDurationMs),
        normalized);
}

qreal CursorTrailOnEffect::fullWidthSpeed() const
{
    return m_activationSpeed + 1530.0;
}

qreal CursorTrailOnEffect::stopFadeDuration() const
{
    return std::clamp(
        qreal(m_trailDurationMs) * 0.44,
        qreal(65.0),
        qreal(220.0));
}

} // namespace KWin

#include "moc_cursortrailon.cpp"
