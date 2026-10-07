/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * cursorTrailOn — based on WindTrail by Wesley (ProxyX).
 * Maintained by xcatzix.
 */

#pragma once

#include <KCModule>

#include <QColor>
#include <QList>
#include <QString>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QPushButton;
class QSlider;
class QSpinBox;
class QVBoxLayout;
class QWidget;

namespace KWin
{

class CursorTrailOnPreview;

// One color of a trail gradient; percent 0 = at the cursor, 100 = end of trail.
struct TrailGradientStop {
    QColor color;
    int percent = 0;

    bool operator==(const TrailGradientStop &other) const
    {
        return color.rgb() == other.color.rgb() && percent == other.percent;
    }
};

class CursorTrailOnConfig final : public KCModule
{
    Q_OBJECT

public:
    explicit CursorTrailOnConfig(QObject *parent,
                                 const KPluginMetaData &data);
    ~CursorTrailOnConfig() override;

    void load() override;
    void save() override;
    void defaults() override;

private Q_SLOTS:
    void settingsChanged();

private:
    // Pointer trail and caret trail each have their own complete look.
    enum class Target {
        Pointer,
        Caret,
    };

    enum class ColorRole {
        Main,
        Core,
        Glow,
    };

    struct Palette {
        QColor main;
        QColor core;
        QColor glow;

        bool operator==(const Palette &other) const;
    };

    // Colors, gradient and light effect of one kind of trail.
    struct Look {
        Palette palette;
        bool glowEnabled = true;
        qreal intensity = 1.0;

        bool gradientEnabled = false;
        bool gradientSmooth = true;
        QList<TrailGradientStop> stops;

        bool lightEnabled = false;
        qreal lightStrength = 1.0;
        int lightRadius = 100; // percent
        bool headLight = false;

        bool operator==(const Look &other) const;
    };

    struct LookWidgets {
        QComboBox *preset = nullptr;
        QPushButton *mainButton = nullptr;
        QPushButton *coreButton = nullptr;
        QPushButton *glowButton = nullptr;
        QCheckBox *glowEnabled = nullptr;
        QDoubleSpinBox *intensity = nullptr;

        QCheckBox *gradientEnabled = nullptr;
        QComboBox *gradientPreset = nullptr;
        QCheckBox *gradientSmooth = nullptr;
        QWidget *stopsHost = nullptr;
        QVBoxLayout *stopsLayout = nullptr;
        QPushButton *addStop = nullptr;

        QCheckBox *lightEnabled = nullptr;
        QDoubleSpinBox *lightStrength = nullptr;
        QSpinBox *lightRadius = nullptr;
        QCheckBox *headLight = nullptr;
    };

    struct Settings {
        bool mouseEnabled = true;
        bool caretEnabled = true;

        // --- pointer trail
        Look pointer;
        int trailWidth = 18;
        int trailDuration = 330;
        int activationSpeed = 320;
        int smoothness = 2;

        // --- caret trail
        Look caret;
        int caretWidth = 8;
        int caretHeight = 40;
        int caretMinSpeed = 0;
        int caretDuration = 330;
        int caretPollInterval = 16;

        bool disableInFullscreen = true;

        bool operator==(const Settings &other) const;
        bool operator!=(const Settings &other) const;
    };

    static Palette presetPalette(int index);
    static Palette derivedPalette(const QColor &main);
    static int presetForPalette(const Palette &palette);
    static QList<TrailGradientStop> gradientPresetStops(int index);
    static int gradientPresetFor(const QList<TrailGradientStop> &stops);
    static Look defaultLook();
    static Settings defaultSettings();

    Settings readSettings() const;
    Settings currentSettings() const;
    void applySettings(const Settings &settings);

    void buildLookRows(QFormLayout *form, QWidget *parent, Target target);
    LookWidgets &widgetsFor(Target target);
    Palette &paletteFor(Target target);
    QList<TrailGradientStop> &stopsFor(Target target);
    void chooseColor(Target target, ColorRole role);
    void chooseStopColor(Target target, int index);
    void presetChanged(Target target, int index);
    void gradientPresetChanged(Target target, int index);
    void rebuildStopRows(Target target);
    void updateColorButtons(Target target);
    void updatePreview();
    void updateEnabledStates();
    void updateState();

    QWidget *makeSliderRow(QWidget *parent,
                           QSlider *&slider,
                           QSpinBox *&spin,
                           int minimum,
                           int maximum);

    QCheckBox *m_mouseEnabled = nullptr;
    QCheckBox *m_caretEnabled = nullptr;

    LookWidgets m_pointerWidgets;
    LookWidgets m_caretWidgets;

    QSlider *m_trailWidthSlider = nullptr;
    QSpinBox *m_trailWidth = nullptr;
    QSpinBox *m_trailDuration = nullptr;
    QSpinBox *m_activationSpeed = nullptr;
    QSpinBox *m_smoothness = nullptr;

    QSlider *m_caretWidthSlider = nullptr;
    QSpinBox *m_caretWidth = nullptr;
    QSlider *m_caretHeightSlider = nullptr;
    QSpinBox *m_caretHeight = nullptr;
    QSpinBox *m_caretMinSpeed = nullptr;
    QSpinBox *m_caretDuration = nullptr;
    QSpinBox *m_caretPollInterval = nullptr;

    QCheckBox *m_disableInFullscreen = nullptr;
    CursorTrailOnPreview *m_preview = nullptr;

    Palette m_pointerPalette;
    Palette m_caretPalette;
    QList<TrailGradientStop> m_pointerStops;
    QList<TrailGradientStop> m_caretStops;
    Settings m_savedSettings;
    bool m_loading = false;
};

} // namespace KWin
