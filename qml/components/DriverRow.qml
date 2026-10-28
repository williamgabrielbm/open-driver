import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../Theme.js" as Theme

Rectangle {
    id: root
    required property int rowIndex
    required property string driverName
    required property string deviceClass
    required property string installedVersion
    required property string availableVersion
    required property string availableDate
    required property string sourceName
    required property bool selected
    required property string details
    property bool expanded: false
    signal selectionChanged(bool checked)
    signal updateRequested()
    width: ListView.view ? ListView.view.width : 900
    implicitHeight: expanded && appController.showAdvancedDetails ? 126 : 76
    height: implicitHeight
    radius: 10
    color: hover.containsMouse ? "#343841" : Theme.elevated
    border.color: selected ? "#426FA8" : Theme.border
    border.width: 1
    Behavior on height { NumberAnimation { duration: appController.animationsEnabled ? 220 : 0; easing.type: Easing.OutCubic } }
    Behavior on color { ColorAnimation { duration: appController.animationsEnabled ? 150 : 0 } }

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        height: 48
        spacing: 12
        CheckBox {
            checked: root.selected
            onToggled: root.selectionChanged(checked)
            indicator: Rectangle {
                implicitWidth: 18; implicitHeight: 18; radius: 4
                color: parent.checked ? Theme.accent : "transparent"
                border.color: parent.checked ? Theme.accent : Theme.secondary
                Text { anchors.centerIn: parent; text: "✓"; visible: parent.parent.checked; color: "white"; font.pixelSize: 13 }
            }
        }
        Rectangle {
            width: 42; height: 42; radius: 10
            color: "#243B59"
            Text {
                anchors.centerIn: parent
                text: root.deviceClass.toLowerCase().includes("display") ? "▰" :
                      root.deviceClass.toLowerCase().includes("net") ? "⌁" : "▣"
                color: Theme.accentLight
                font.pixelSize: 20
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 220
            spacing: 3
            Text {
                Layout.fillWidth: true
                text: root.driverName
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: root.sourceName
                color: Theme.secondary
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }
        Column {
            Layout.preferredWidth: 132
            Text { text: "INSTALADO"; color: Theme.subtle; font.pixelSize: 9; font.weight: Font.DemiBold }
            Text { text: root.installedVersion || "—"; color: Theme.secondary; font.pixelSize: 12; elide: Text.ElideRight; width: 128 }
        }
        Column {
            Layout.preferredWidth: 142
            Text { text: "DISPONÍVEL"; color: Theme.subtle; font.pixelSize: 9; font.weight: Font.DemiBold }
            Text { text: root.availableVersion || root.availableDate; color: Theme.accentLight; font.pixelSize: 12; elide: Text.ElideRight; width: 138 }
        }
        PremiumButton {
            compact: true
            text: "Atualizar"
            onClicked: root.updateRequested()
        }
        ToolButton {
            text: root.expanded ? "⌃" : "⋯"
            hoverEnabled: true
            onClicked: root.expanded = !root.expanded
            contentItem: Text { text: parent.text; color: Theme.secondary; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 20 }
            background: Rectangle { color: parent.hovered ? "#414650" : "transparent"; radius: 7 }
        }
    }

    Text {
        visible: root.expanded && appController.showAdvancedDetails
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 86
        anchors.rightMargin: 18
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        text: root.details
        color: Theme.secondary
        font.pixelSize: 10
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }
    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }
}
