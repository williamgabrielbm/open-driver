import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../Theme.js" as Theme
import "../components"

Item {
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 30; spacing: 16
        Text { text: "Configurações"; color: Theme.text; font.pixelSize: 28; font.weight: Font.DemiBold }
        Text { text: "Preferências aplicadas de verdade pela interface e pela seleção de resultados."; color: Theme.secondary; font.pixelSize: 13 }
        Panel {
            Layout.fillWidth: true; Layout.preferredHeight: 232
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 20; spacing: 0
                Repeater {
                    model: [
                        { title: "Animações da interface", subtitle: appController.reducedMotion ? "O Windows solicitou redução de movimento." : "Transições, hover e movimento do anel.", key: "animations" },
                        { title: "Selecionar atualizações recomendadas", subtitle: "Marca automaticamente os pacotes aprovados pela validação final.", key: "autoSelect" },
                        { title: "Detalhes técnicos nas linhas", subtitle: "Mostra fornecedor, INF e identidade de hardware ao expandir.", key: "details" }
                    ]
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true; Layout.preferredHeight: 64
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 3
                            Text { text: modelData.title; color: Theme.text; font.pixelSize: 14; font.weight: Font.Medium }
                            Text { Layout.fillWidth: true; text: modelData.subtitle; color: Theme.secondary; font.pixelSize: 11; elide: Text.ElideRight }
                        }
                        Switch {
                            id: toggle
                            checked: modelData.key === "animations" ? appController.animationsEnabled :
                                     modelData.key === "autoSelect" ? appController.autoSelectUpdates : appController.showAdvancedDetails
                            enabled: modelData.key !== "animations" || !appController.reducedMotion
                            indicator: Rectangle {
                                implicitWidth: 48; implicitHeight: 26; radius: 13
                                color: toggle.checked ? Theme.accent : "#41454D"
                                border.color: toggle.checked ? Theme.accentLight : Theme.border
                                opacity: toggle.enabled ? 1 : 0.5
                                Behavior on color { ColorAnimation { duration: appController.animationsEnabled ? 150 : 0 } }
                                Rectangle {
                                    width: 20; height: 20; radius: 10; y: 3
                                    x: toggle.checked ? parent.width - width - 3 : 3
                                    color: "white"
                                    Behavior on x { NumberAnimation { duration: appController.animationsEnabled ? 170 : 0; easing.type: Easing.OutCubic } }
                                }
                            }
                            onToggled: {
                                if (modelData.key === "animations") appController.animationsEnabled = checked
                                else if (modelData.key === "autoSelect") appController.autoSelectUpdates = checked
                                else appController.showAdvancedDetails = checked
                            }
                        }
                    }
                }
            }
        }
        Text { text: "Dados locais"; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
        Text { Layout.fillWidth: true; text: appController.dataFolder; color: Theme.secondary; font.pixelSize: 11; elide: Text.ElideMiddle }
        PremiumButton { compact: true; primary: false; text: "Abrir pasta"; onClicked: appController.openDataFolder() }
        Item { Layout.fillHeight: true }
    }
}
