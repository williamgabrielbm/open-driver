import QtQuick
import QtQuick.Controls
import "../Theme.js" as Theme

Button {
    id: root
    property bool primary: true
    property bool compact: false
    implicitWidth: compact ? 112 : 184
    implicitHeight: compact ? 38 : 46
    hoverEnabled: true
    font.family: "Segoe UI Variable Text"
    font.pixelSize: compact ? 13 : 14
    font.weight: Font.DemiBold

    contentItem: Text {
        text: root.text
        color: root.enabled ? (root.primary ? "white" : Theme.text) : Theme.subtle
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font: root.font
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 9
        color: !root.enabled ? "#34373D"
             : root.down ? (root.primary ? Theme.accentDark : "#3B4049")
             : root.hovered ? (root.primary ? Theme.accentLight : "#373B43")
             : root.primary ? Theme.accent : Theme.elevated
        border.color: root.primary ? "transparent" : Theme.border
        border.width: 1
        scale: root.down ? 0.975 : 1.0
        Behavior on color { ColorAnimation { duration: appController.animationsEnabled ? 150 : 0 } }
        Behavior on scale { NumberAnimation { duration: appController.animationsEnabled ? 100 : 0; easing.type: Easing.OutCubic } }
    }
}
