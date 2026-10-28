import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../Theme.js" as Theme
import "../components"

Item {
    id: root
    property bool operational: ["scanning", "validating", "downloading", "installing", "cancelling"].indexOf(appController.state) >= 0
    property bool resultsVisible: ["results", "completed", "restartRequired", "error"].indexOf(appController.state) >= 0

    opacity: 0
    Component.onCompleted: opacity = 1
    Behavior on opacity { NumberAnimation { duration: appController.animationsEnabled ? 220 : 0 } }

    Item {
        anchors.fill: parent
        visible: !root.resultsVisible

        Text {
            anchors.top: parent.top
            anchors.topMargin: 32
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.operational ? appController.stageText : "Verifique todos os componentes do computador"
            color: Theme.text
            font.family: "Segoe UI Variable Display"
            font.pixelSize: root.operational ? 19 : 17
            font.weight: Font.Medium
        }
        Text {
            anchors.top: parent.top
            anchors.topMargin: 66
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 160
            text: root.operational ? appController.detailText : "Hardware, fontes oficiais e compatibilidade serão conferidos antes de qualquer atualização."
            color: Theme.secondary
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }

        ScanRing {
            id: ring
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.horizontalCenterOffset: root.operational ? 0 : -105
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: 8
            running: root.operational
            cancelling: appController.state === "cancelling"
            determinate: appController.progressDeterminate
            progress: determinate ? appController.progressCurrent / appController.progressTotal : 0
            onActivated: {
                if (root.operational) appController.cancelOperation()
                else appController.startScan()
            }
            Behavior on anchors.horizontalCenterOffset {
                NumberAnimation { duration: appController.animationsEnabled ? 260 : 0; easing.type: Easing.OutCubic }
            }
        }

        SystemInfoPanel {
            anchors.right: parent.right
            anchors.rightMargin: 30
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: 8
            info: appController.systemInfo
            visible: !root.operational && parent.width >= 820
            opacity: visible ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: appController.animationsEnabled ? 220 : 0 } }
        }

        Text {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 28
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.operational
                ? "A lista aparece somente depois da validação completa dos candidatos."
                : "Nenhum pacote será baixado sem sua autorização."
            color: Theme.subtle
            font.pixelSize: 11
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 26
        visible: root.resultsVisible
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 70
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Text {
                    text: appController.summaryText
                    color: appController.state === "error" ? Theme.danger : Theme.text
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                }
                Text {
                    text: appController.devicesAnalyzed + " componentes analisados" +
                          (appController.errorText ? "  •  " + appController.errorText : "")
                    color: Theme.secondary
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
            PremiumButton {
                visible: appController.state === "restartRequired"
                text: "Reiniciar agora"
                onClicked: appController.restartNow()
            }
            PremiumButton {
                visible: appController.state === "error" || appController.drivers.count === 0
                text: "Verificar novamente"
                onClicked: appController.startScan()
            }
            PremiumButton {
                visible: appController.drivers.count > 0 && appController.state === "results"
                text: "Atualizar selecionados (" + appController.drivers.selectedCount + ")"
                enabled: appController.drivers.selectedCount > 0
                onClicked: appController.requestUpdateSelected()
            }
        }

        Panel {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10
                TextField {
                    id: search
                    Layout.fillWidth: true
                    Layout.maximumWidth: 360
                    placeholderText: "Pesquisar driver"
                    color: Theme.text
                    placeholderTextColor: Theme.subtle
                    leftPadding: 14
                    background: Rectangle { color: "#23252A"; radius: 8; border.color: search.activeFocus ? Theme.accent : Theme.border }
                }
                ComboBox {
                    id: filter
                    model: ["Todos", "Selecionados"]
                    Layout.preferredWidth: 160
                    contentItem: Text { text: filter.displayText; color: Theme.text; verticalAlignment: Text.AlignVCenter; leftPadding: 12 }
                    background: Rectangle { color: "#23252A"; radius: 8; border.color: Theme.border }
                }
                Item { Layout.fillWidth: true }
                CheckBox {
                    text: "Selecionar todos"
                    checked: appController.drivers.count > 0 && appController.drivers.selectedCount === appController.drivers.count
                    onClicked: appController.drivers.selectAll(checked)
                    contentItem: Text { text: parent.text; color: Theme.secondary; verticalAlignment: Text.AlignVCenter; leftPadding: parent.indicator.width + 8 }
                }
            }
        }

        ListView {
            id: results
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 9
            model: appController.drivers
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: DriverRow {
                required property int index
                required property string driverName
                required property string deviceClass
                required property string installedVersion
                required property string availableVersion
                required property string availableDate
                required property string sourceName
                required property bool selected
                required property string details
                width: results.width - (results.ScrollBar.vertical.visible ? 12 : 0)
                visible: driverName.toLowerCase().includes(search.text.toLowerCase()) &&
                         (filter.currentIndex === 0 || selected)
                height: visible ? implicitHeight : 0
                rowIndex: index
                onSelectionChanged: appController.drivers.setSelected(index, checked)
                onUpdateRequested: appController.requestUpdateOne(index)
                ListView.onAdd: SequentialAnimation {
                    PropertyAction { target: parent; property: "opacity"; value: 0 }
                    PauseAnimation { duration: Math.min(index * 28, 220) }
                    NumberAnimation { target: parent; property: "opacity"; to: 1; duration: appController.animationsEnabled ? 180 : 0 }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: appController.drivers.count === 0
                text: appController.state === "error"
                    ? "A verificação não publicou uma lista parcial."
                    : "Tudo certo por aqui. Nenhuma atualização compatível foi confirmada."
                color: Theme.secondary
                font.pixelSize: 14
            }
        }
    }
}
