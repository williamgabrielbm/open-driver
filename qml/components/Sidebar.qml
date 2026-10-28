import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../Theme.js" as Theme

Rectangle {
    id: root
    required property int currentIndex
    signal pageRequested(int index)
    width: 164
    color: Theme.sidebar

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 88
            Column {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: -2
                Text {
                    text: "OPEN"
                    color: Theme.text
                    font.family: "Segoe UI Variable Display"
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.2
                }
                Text {
                    text: "DRIVER"
                    color: Theme.accentLight
                    font.family: "Segoe UI Variable Text"
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.6
                }
            }
        }

        Repeater {
            model: [
                { label: "Verificar", glyph: "↻" },
                { label: "Otimizar", glyph: "⌁" },
                { label: "Ferramentas", glyph: "▣" },
                { label: "Histórico", glyph: "◷" },
                { label: "Configurações", glyph: "⚙" }
            ]
            delegate: ItemDelegate {
                id: item
                required property var modelData
                required property int index
                Layout.fillWidth: true
                Layout.preferredHeight: 70
                hoverEnabled: true
                onClicked: root.pageRequested(index)
                contentItem: Column {
                    anchors.centerIn: parent
                    spacing: 4
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: item.modelData.glyph
                        color: item.index === root.currentIndex ? Theme.accentLight :
                               item.hovered ? Theme.text : Theme.secondary
                        font.family: "Segoe UI Symbol"
                        font.pixelSize: 22
                        Behavior on color { ColorAnimation { duration: appController.animationsEnabled ? 160 : 0 } }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: item.modelData.label
                        color: item.index === root.currentIndex ? Theme.text : Theme.secondary
                        font.family: "Segoe UI Variable Text"
                        font.pixelSize: 12
                    }
                }
                background: Rectangle {
                    radius: 9
                    color: item.index === root.currentIndex ? "#263B57" :
                           item.hovered ? "#1D2025" : "transparent"
                    Behavior on color { ColorAnimation { duration: appController.animationsEnabled ? 180 : 0 } }
                    Rectangle {
                        width: 3
                        height: parent.height - 18
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        radius: 2
                        color: Theme.accent
                        opacity: item.index === root.currentIndex ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: appController.animationsEnabled ? 180 : 0 } }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }
        Text {
            Layout.fillWidth: true
            Layout.preferredHeight: 45
            text: "Open Driver 1.0"
            color: Theme.subtle
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 11
        }
    }
}
