import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../Theme.js" as Theme
import "../components"

Item {
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 30; spacing: 16
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            Text {
                anchors.left: parent.left; anchors.top: parent.top
                text: "Histórico"; color: Theme.text; font.pixelSize: 28; font.weight: Font.DemiBold
            }
            Text {
                anchors.left: parent.left; anchors.bottom: parent.bottom
                text: "Eventos reais gravados pelos executores transacionais."; color: Theme.secondary; font.pixelSize: 13
            }
            PremiumButton {
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                compact: true; primary: false; text: "Atualizar"; onClicked: appController.refreshHistory()
            }
        }
        Panel {
            Layout.fillWidth: true; Layout.fillHeight: true
            ListView {
                id: historyList
                anchors.fill: parent; anchors.margins: 12
                spacing: 7; clip: true
                model: appController.history
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    required property var modelData
                    width: historyList.width - 8; height: 74; radius: 9
                    color: hover.containsMouse ? "#363A43" : Theme.elevated
                    border.color: Theme.border
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 14; spacing: 16
                        Rectangle {
                            width: 38; height: 38; radius: 19
                            color: modelData.result === "completed" || modelData.result === "passed" ? "#23452F" : "#43382A"
                            Text { anchors.centerIn: parent; text: modelData.result === "completed" || modelData.result === "passed" ? "✓" : "•"; color: Theme.text; font.pixelSize: 17 }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 3
                            Text { Layout.fillWidth: true; text: modelData.action + "  •  " + modelData.device; color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium; elide: Text.ElideRight }
                            Text { Layout.fillWidth: true; text: modelData.detail || modelData.result; color: Theme.secondary; font.pixelSize: 11; elide: Text.ElideRight }
                        }
                        Text { text: modelData.date; color: Theme.subtle; font.pixelSize: 11 }
                    }
                    MouseArea { id: hover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                }
                Text {
                    anchors.centerIn: parent; visible: appController.history.length === 0
                    text: "Nenhuma operação registrada ainda."; color: Theme.secondary; font.pixelSize: 13
                }
            }
        }
    }
}
