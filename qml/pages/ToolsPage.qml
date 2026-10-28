import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../Theme.js" as Theme
import "../components"

Item {
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 30; spacing: 18
        Text { text: "Ferramentas"; color: Theme.text; font.pixelSize: 28; font.weight: Font.DemiBold }
        Text { text: "Ações reais para diagnóstico e suporte do Open Driver."; color: Theme.secondary; font.pixelSize: 13 }
        GridLayout {
            Layout.fillWidth: true
            columns: width > 760 ? 2 : 1
            rowSpacing: 14; columnSpacing: 14
            Panel {
                Layout.fillWidth: true; Layout.preferredHeight: 180
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 22; spacing: 9
                    Text { text: "▣"; color: Theme.accentLight; font.pixelSize: 28 }
                    Text { text: "Exportar diagnóstico"; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
                    Text { Layout.fillWidth: true; text: "Salva o inventário real dos dispositivos em JSON para análise técnica."; color: Theme.secondary; font.pixelSize: 12; wrapMode: Text.Wrap }
                    Item { Layout.fillHeight: true }
                    PremiumButton { compact: true; text: "Exportar"; onClicked: appController.exportDiagnostics() }
                }
            }
            Panel {
                Layout.fillWidth: true; Layout.preferredHeight: 180
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 22; spacing: 9
                    Text { text: "◷"; color: Theme.accentLight; font.pixelSize: 28 }
                    Text { text: "Arquivos técnicos"; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
                    Text { Layout.fillWidth: true; text: "Abre a pasta que contém planos, diários, diagnósticos e backups de rollback."; color: Theme.secondary; font.pixelSize: 12; wrapMode: Text.Wrap }
                    Item { Layout.fillHeight: true }
                    PremiumButton { compact: true; text: "Abrir pasta"; primary: false; onClicked: appController.openDataFolder() }
                }
            }
        }
        Item { Layout.fillHeight: true }
    }
}
