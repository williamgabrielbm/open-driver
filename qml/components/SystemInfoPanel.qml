import QtQuick
import QtQuick.Layouts
import "../Theme.js" as Theme

Panel {
    id: root
    required property var info
    width: 300
    height: 244

    Rectangle {
        width: 4
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 18
        radius: 2
        color: Theme.accent
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 28
        anchors.rightMargin: 18
        anchors.topMargin: 18
        anchors.bottomMargin: 16
        spacing: 8
        Text {
            text: "INFORMAÇÕES DO PC"
            color: Theme.text
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
        Repeater {
            model: [
                "windows", "processor", "graphics", "memory", "architecture"
            ]
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 9
                Rectangle { width: 6; height: 6; radius: 3; color: Theme.accentLight }
                Text {
                    Layout.fillWidth: true
                    text: {
                        if (modelData === "windows") return (root.info.windows || "Windows") + "  •  Build " + (root.info.build || "")
                        if (modelData === "processor") return root.info.processor || "Processador"
                        if (modelData === "graphics") return root.info.graphics || "Placa de vídeo"
                        if (modelData === "memory") return root.info.memory || "Memória"
                        return "Sistema " + (root.info.architecture || "")
                    }
                    color: Theme.secondary
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
        }
        Item { Layout.fillHeight: true }
        Text {
            text: "Informações coletadas do Windows"
            color: Theme.accentLight
            font.pixelSize: 11
        }
    }
}
