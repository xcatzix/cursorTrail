/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * cursorTrailOn — based on WindTrail by Wesley (ProxyX).
 * Maintained by xcatzix.
 */

#pragma once

#include <KCModule>

#include <QColor>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QPushButton;
class QSlider;
class QSpinBox;
class QWidget;

namespace KWin
{

class CursorTrailOnPreview;

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
    // Pointer trail and caret trail each have their own complete color set.
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

    struct ColorWidgets {
        QComboBox *preset = nullptr;
        QPushButton *mainButton = nullptr;
        QPushButton *coreButton = nullptr;
        QPushButton *glowButton = nullptr;
        QCheckBox *glowEnabled = nullptr;
        QDoubleSpinBox *intensity = nullptr;
    };

    struct Settings {
        bool mouseEnabled = true;
        bool caretEnabled = true;

        // --- pointer trail
        Palette palette;
        bool glowEnabled = true;
        qreal intensity = 1.0;
        int trailWidth = 18;
        int trailDuration = 330;
        int activationSpeed = 320;
        int smoothness = 2;

        // --- caret trail
        Palette caretPalette;
        bool caretGlowEnabled = true;
        qreal caretIntensity = 1.0;
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
    static Settings defaultSettings();
    static int presetForPalette(const Palette &palette);

    Settings readSettings() const;
    Settings currentSettings() const;
    void applySettings(const Settings &settings);

    void buildColorRows(QFormLayout *form, QWidget *parent, Target target);
    ColorWidgets &widgetsFor(Target target);
    Palette &paletteFor(Target target);
    void chooseColor(Target target, ColorRole role);
    void presetChanged(Target target, int index);
    void updateColorButtons(Target target);
    void updatePreview();
    void updateEnabledStates();
    void updateState();

    QWidget *makeSliderRow(QSlider *&slider,
                           QSpinBox *&spin,
                           int minimum,
                           int maximum);

    QCheckBox *m_mouseEnabled = nullptr;
    QCheckBox *m_caretEnabled = nullptr;

    ColorWidgets m_pointerColors;
    ColorWidgets m_caretColors;

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
    Settings m_savedSettings;
    bool m_loading = false;
};

} // namespace KWin
