import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Theme.js" as Theme
import "components"
import "pages"

ApplicationWindow {
    id: window
    visible: true
    width: 1180
    height: 760
    minimumWidth: 980
    minimumHeight: 640
    title: "Open Driver"
    color: Theme.background

    property int currentPage: 0

    RowLayout {
        anchors.fill: parent
        spacing: 0
        Sidebar {
            Layout.fillHeight: true
            Layout.preferredWidth: 164
            currentIndex: window.currentPage
            onPageRequested: function(index) { window.currentPage = index }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.background
            clip: true
            Loader {
                id: pageLoader
                anchors.fill: parent
                sourceComponent: [scanPage, optimizePage, toolsPage, historyPage, settingsPage][window.currentPage]
                onLoaded: if (item) item.forceActiveFocus()
            }
        }
    }

    Component { id: scanPage; ScanPage {} }
    Component { id: optimizePage; OptimizePage {} }
    Component { id: toolsPage; ToolsPage {} }
    Component { id: historyPage; HistoryPage {} }
    Component { id: settingsPage; SettingsPage {} }

    Dialog {
        id: downloadDialog
        modal: true
        anchors.centerIn: parent
        width: 480
        title: "Autorizar download"
        property int packageCount: 0
        property string estimatedSize: ""
        standardButtons: Dialog.NoButton
        background: Rectangle { color: Theme.elevated; radius: 14; border.color: Theme.border }
        header: Text {
            text: downloadDialog.title; color: Theme.text; font.pixelSize: 19; font.weight: Font.DemiBold
            padding: 22
        }
        contentItem: ColumnLayout {
            spacing: 18
            Text {
                Layout.fillWidth: true
                text: "Foram encontrados " + downloadDialog.packageCount + " pacote(s) candidato(s), " + downloadDialog.estimatedSize + ".\n\nO download serve para conferir assinatura, integridade, INF e compatibilidade antes de exibir a lista."
                color: Theme.secondary; font.pixelSize: 13; wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                PremiumButton { compact: true; primary: false; text: "Agora não"; onClicked: { downloadDialog.close(); appController.authorizeCandidateDownloads(false) } }
                PremiumButton { compact: true; text: "Baixar e validar"; onClicked: { downloadDialog.close(); appController.authorizeCandidateDownloads(true) } }
            }
        }
    }

    Dialog {
        id: installDialog
        modal: true
        anchors.centerIn: parent
        width: 460
        title: "Confirmar instalação"
        property int driverCount: 0
        standardButtons: Dialog.NoButton
        background: Rectangle { color: Theme.elevated; radius: 14; border.color: Theme.border }
        header: Text { text: installDialog.title; color: Theme.text; font.pixelSize: 19; font.weight: Font.DemiBold; padding: 22 }
        contentItem: ColumnLayout {
            spacing: 18
            Text {
                Layout.fillWidth: true
                text: "Instalar " + installDialog.driverCount + " driver(s) selecionado(s)? O Open Driver revalidará o dispositivo, criará backup e verificará o resultado antes de concluir."
                color: Theme.secondary; font.pixelSize: 13; wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                PremiumButton { compact: true; primary: false; text: "Cancelar"; onClicked: { installDialog.close(); appController.confirmInstallation(false) } }
                PremiumButton { compact: true; text: "Instalar"; onClicked: { installDialog.close(); appController.confirmInstallation(true) } }
            }
        }
    }

    Rectangle {
        id: toast
        property string message: ""
        property bool error: false
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: visible ? 24 : -60
        width: Math.min(parent.width - 80, Math.max(320, toastText.implicitWidth + 44))
        height: 48; radius: 10
        color: error ? "#563039" : "#244337"
        border.color: error ? Theme.danger : Theme.success
        visible: opacity > 0
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: appController.animationsEnabled ? 180 : 0 } }
        Behavior on anchors.bottomMargin { NumberAnimation { duration: appController.animationsEnabled ? 220 : 0; easing.type: Easing.OutCubic } }
        Text { id: toastText; anchors.centerIn: parent; text: toast.message; color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight; width: parent.width - 28 }
        Timer { id: toastTimer; interval: 3600; onTriggered: toast.opacity = 0 }
    }

    Connections {
        target: appController
        function onDownloadAuthorizationRequested(packageCount, estimatedSize) {
            window.currentPage = 0
            downloadDialog.packageCount = packageCount
            downloadDialog.estimatedSize = estimatedSize
            downloadDialog.open()
        }
        function onInstallationConfirmationRequested(driverCount) {
            installDialog.driverCount = driverCount
            installDialog.open()
        }
        function onToastRequested(message, error) {
            toast.message = message
            toast.error = error
            toast.opacity = 1
            toastTimer.restart()
        }
    }
}
