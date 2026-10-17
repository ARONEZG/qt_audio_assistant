pragma Singleton
import QtQuick

// The single source of design tokens for the chat UI. Every colour, spacing and
// font size used by the components comes from here; changing the theme means
// changing this file (typography can be scaled centrally through the font size
// tokens). It holds no logic and no state.
QtObject {
    // Spacing and shape.
    readonly property int spacingTight: 4
    readonly property int spacingMedium: 8
    readonly property int spacingLoose: 16
    readonly property int radius: 8
    readonly property int panelMinimumWidth: 280
    readonly property int panelMinimumHeight: 200
    readonly property int panelHeight: 300

    // Typography.
    readonly property int fontSizeSmall: 12
    readonly property int fontSizeBody: 14
    readonly property int fontSizeTitle: 18

    // Palette.
    readonly property color background: "#1e1e1e"
    readonly property color surface: "#2a2a2a"
    readonly property color surfaceElevated: "#333333"
    readonly property color border: "#444444"
    readonly property color textPrimary: "#ffffff"
    readonly property color textSecondary: "#aaaaaa"
    readonly property color accent: "#3d7eff"
    readonly property color userBubble: "#2f5d8f"
    readonly property color assistantBubble: "#333333"
}
