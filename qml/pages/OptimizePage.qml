import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../Theme.js" as Theme
import "../components"

Item {
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 30
        spacing: 18
        Text { text: "Otimizar"; color: Theme.text; font.pixelSize: 28; font.weight: Font.DemiBold }
        Text {
            Layout.fillWidth: true
            text: "Diagnóstico objetivo do Windows, sem promessas genéricas nem alterações automáticas arriscadas."
            color: Theme.secondary; font.pixelSize: 13; wrapMode: Text.Wrap
        }
        Panel {
            Layout.fillWidth: true
            Layout.preferredHeight: 178
            RowLayout {
                anchors.fill: parent; anchors.margins: 24; spacing: 20
                Rectangle {
                    width: 72; height: 72; radius: 36; color: "#243B59"
                    Text { anchors.centerIn: parent; text: "⌁"; color: Theme.accentLight; font.pixelSize: 31 }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 8
                    Text { text: "Integridade dos dispositivos"; color: Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                    Text {
                        Layout.fillWidth: true
                        text: appController.integritySummary
                        color: Theme.secondary; font.pixelSize: 12; wrapMode: Text.Wrap
                    }
                    ProgressBar {
                        visible: appController.integrityBusy
                        indeterminate: true
                        Layout.fillWidth: true
                        background: Rectangle { color: "#202329"; radius: 3 }
                        contentItem: Item {
                            Rectangle { width: parent.width * 0.35; height: parent.height; radius: 3; color: Theme.accent }
                        }
                    }
                }
                PremiumButton {
                    text: appController.integrityBusy ? "Analisando..." : "Analisar agora"
                    enabled: !appController.integrityBusy
                    onClicked: appController.startIntegrityCheck()
                }
            }
        }
        Panel {
            Layout.fillWidth: true
            Layout.preferredHeight: 118
            RowLayout {
                anchors.fill: parent; anchors.margins: 22
                ColumnLayout {
                    Layout.fillWidth: true
                    Text { text: "Otimizações automáticas"; color: Theme.text; font.pixelSize: 16; font.weight: Font.Medium }
                    Text {
                        Layout.fillWidth: true
                        text: "Ainda não há um módulo verificável para alterar serviços, registro ou energia. O Open Driver não apresenta esses ajustes como disponíveis."
                        color: Theme.secondary; font.pixelSize: 12; wrapMode: Text.Wrap
                    }
                }
                Rectangle {
                    width: 112; height: 34; radius: 17; color: "#353941"
                    Text { anchors.centerIn: parent; text: "INDISPONÍVEL"; color: Theme.subtle; font.pixelSize: 10; font.weight: Font.Bold }
                }
            }
        }
        Item { Layout.fillHeight: true }
    }
}
