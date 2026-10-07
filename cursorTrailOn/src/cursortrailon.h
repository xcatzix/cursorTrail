/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * cursorTrailOn — based on WindTrail by Wesley (ProxyX).
 */

#pragma once

#include "effect/effect.h"

#include <QColor>
#include <QElapsedTimer>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QVector2D>

#include <array>
#include <deque>
#include <functional>
#include <vector>

class QTimer;

namespace KWin
{

class CursorTrailOnEffect final : public Effect
{
    Q_OBJECT

public:
    CursorTrailOnEffect();
    ~CursorTrailOnEffect() override;

    void reconfigure(ReconfigureFlags flags) override;
    void prePaintScreen(ScreenPrePaintData &data) override;
    void paintScreen(const RenderTarget &renderTarget,
                     const RenderViewport &viewport,
                     int mask,
                     const Region &deviceRegion,
                     LogicalOutput *screen) override;
    void postPaintScreen() override;

    bool isActive() const override;

private Q_SLOTS:
    void onMouseChanged(const QPointF &pos,
                        const QPointF &oldPos,
                        Qt::MouseButtons buttons,
                        Qt::MouseButtons oldButtons,
                        Qt::KeyboardModifiers modifiers,
                        Qt::KeyboardModifiers oldModifiers);
    void pollTextCaret();

private:
    // Geometry is grouped by (fade bucket x color bin) so that each group can
    // be drawn with a single uniform color.
    static constexpr int AlphaBuckets = 8;
    static constexpr int GradientBins = 24;
    using Buckets = std::vector<QList<QVector2D>>;

    struct ColorStop {
        qreal position = 0.0; // 0 = head (at the cursor), 1 = tail
        QColor color;
    };

    // Everything that defines how one kind of trail (pointer or caret) looks.
    struct TrailStyle {
        QColor main = QColor(255, 6, 12);
        QColor core = QColor(255, 185, 187);
        QColor glow = QColor(184, 4, 9);
        bool glowEnabled = true;
        qreal intensity = 1.0;

        bool gradientEnabled = false;
        bool gradientSmooth = true;
        std::vector<ColorStop> stops;

        bool lightEnabled = false;  // additive bloom around the trail
        qreal lightStrength = 1.0;
        qreal lightRadius = 1.0;    // multiplier of the bloom size
        bool headLight = false;     // soft light spot at the cursor
    };

    enum class Paint {
        Main,
        Core,
        Glow,
        Light,
    };

    struct Sample {
        QPointF position;
        qint64 timestampMs = 0;
        qreal speed = 0.0;
    };

    struct CaretSample {
        QRectF rect; // global logical coordinates
        qint64 timestampMs = 0;
        qreal speed = 0.0; // px/s of the move that led to this sample
    };

    struct CaretInfo {
        QRectF localRect;   // surface-local rectangle reported by the client
        QPointF origin;     // top-left of the client's buffer in global space
        const void *window = nullptr;
    };

    bool isSuppressed() const;
    void clearTrail();
    void clearCaretTrail();
    void pruneExpired(qint64 nowMs);
    void pruneCaretExpired(qint64 nowMs);
    std::vector<Sample> smoothedSamples() const;

    bool queryTextCaret(CaretInfo &info) const;

    void drawRibbonPass(const std::vector<Sample> &samples,
                        const RenderTarget &renderTarget,
                        const RenderViewport &viewport,
                        qreal widthScale,
                        Paint paint,
                        qreal baseAlpha,
                        qint64 nowMs);

    void drawCaretPass(const RenderTarget &renderTarget,
                       const RenderViewport &viewport,
                       qreal sizeFactor,
                       Paint paint,
                       qreal baseAlpha,
                       qint64 nowMs);

    void drawHeadLight(const TrailStyle &style,
                       const RenderTarget &renderTarget,
                       const RenderViewport &viewport,
                       const QPointF &center,
                       qreal radius,
                       qreal fade);

    void renderBuckets(Buckets &buckets,
                       int bins,
                       const TrailStyle &style,
                       Paint paint,
                       const RenderTarget &renderTarget,
                       const RenderViewport &viewport,
                       qreal baseAlpha,
                       qreal fade,
                       bool additive);

    static QColor gradientAt(const TrailStyle &style, qreal t);
    static QColor paintColor(const TrailStyle &style, Paint paint, qreal t);
    static std::vector<ColorStop> parseStops(const QString &text);

    static QPointF interpolatePoint(const QPointF &a, const QPointF &b, qreal t);
    static qreal interpolateValue(qreal a, qreal b, qreal t);
    static QColor mixColors(const QColor &a, const QColor &b, qreal amount);

    qreal lifetimeForSpeed(qreal speed) const;
    qreal fullWidthSpeed() const;
    qreal stopFadeDuration() const;

    // --- mouse pointer trail -------------------------------------------
    std::deque<Sample> m_samples;
    QElapsedTimer m_clock;

    QPointF m_lastPosition;
    qint64 m_lastEventMs = 0;
    qreal m_filteredSpeed = 0.0;
    bool m_haveLastPosition = false;

    // --- text caret trail ------------------------------------------------
    std::deque<CaretSample> m_caretSamples;
    QTimer *m_caretTimer = nullptr;
    bool m_haveCaret = false;
    CaretInfo m_lastCaret;
    qint64 m_lastCaretChangeMs = 0;

    // --- settings: mouse pointer trail -------------------------------------
    TrailStyle m_pointerStyle;

    bool m_mouseEnabled = true;
    bool m_caretEnabled = true;

    qreal m_thickness = 1.0;      // derived from the "Trail width" setting
    qreal m_activationSpeed = 320.0;
    qint64 m_trailDurationMs = 330;
    int m_smoothingPasses = 2;
    bool m_disableInFullscreen = true;

    // --- settings: text caret trail (independent of the mouse trail) --------
    TrailStyle m_caretStyle;
    qreal m_caretWidth = 8.0;       // px, base width of the vertical-move trail
    qreal m_caretMaxHeight = 40.0;  // px, maximum base height of the trail
    qreal m_caretMinSpeed = 0.0;    // px/s, slower caret moves draw no trail
    qint64 m_caretDurationMs = 330;
    int m_caretPollIntervalMs = 16; // how often the caret position is queried

    static constexpr int MaximumSamples = 84;
    static constexpr int MaximumCaretSamples = 64;
    static constexpr qreal MinimumDistance = 1.25;
    static constexpr qreal MaximumJumpDistance = 420.0;
    static constexpr qreal MouseWidthAtScaleOne = 18.0; // px, "Trail width" default
};

} // namespace KWin
