import QtQuick
import "../Theme.js" as Theme

Item {
    id: root
    property bool running: false
    property bool cancelling: false
    property bool determinate: false
    property real progress: 0
    property string detail: ""
    signal activated()
    width: 270
    height: 270
    scale: pointer.pressed ? 0.975 : pointer.containsMouse ? 1.02 : 1.0
    Behavior on scale { NumberAnimation { duration: appController.animationsEnabled ? 160 : 0; easing.type: Easing.OutCubic } }

    Rectangle {
        anchors.centerIn: parent
        width: 246; height: 246; radius: 123
        color: "#17191D"
        border.color: "#252931"
        border.width: 14
    }

    Canvas {
        id: baseCanvas
        anchors.fill: parent
        antialiasing: true
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var cx = width / 2, cy = height / 2
            ctx.lineCap = "round"
            ctx.strokeStyle = "#3B4049"
            ctx.lineWidth = 16
            ctx.beginPath(); ctx.arc(cx, cy, 101, 0, Math.PI * 2); ctx.stroke()
            for (var i = 0; i < 64; ++i) {
                var angle = i * Math.PI * 2 / 64 - Math.PI / 2
                var inner = i % 8 === 0 ? 73 : 77
                ctx.strokeStyle = i % 8 === 0 ? "#6D7685" : "#444A55"
                ctx.lineWidth = i % 8 === 0 ? 2 : 1
                ctx.beginPath()
                ctx.moveTo(cx + Math.cos(angle) * inner, cy + Math.sin(angle) * inner)
                ctx.lineTo(cx + Math.cos(angle) * 86, cy + Math.sin(angle) * 86)
                ctx.stroke()
            }
        }
    }

    Item {
        id: spinner
        anchors.fill: parent
        rotation: 0
        RotationAnimator on rotation {
            from: 0; to: 360
            duration: 1450
            loops: Animation.Infinite
            running: root.running && !root.determinate && appController.animationsEnabled
        }
        Canvas {
            anchors.fill: parent
            antialiasing: true
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                var cx = width / 2, cy = height / 2
                var fraction = root.running ? (root.determinate ? Math.max(0.015, root.progress) : 0.24) : 1.0
                ctx.shadowColor = Theme.accentLight
                ctx.shadowBlur = pointer.containsMouse ? 18 : 11
                ctx.strokeStyle = Theme.accentLight
                ctx.lineWidth = 16
                ctx.lineCap = "round"
                ctx.beginPath()
                ctx.arc(cx, cy, 101, -Math.PI / 2, -Math.PI / 2 + fraction * Math.PI * 2)
                ctx.stroke()
            }
            Connections {
                target: root
                function onProgressChanged() { parent.requestPaint() }
                function onRunningChanged() { parent.requestPaint() }
                function onDeterminateChanged() { parent.requestPaint() }
            }
        }
    }

    Column {
        anchors.centerIn: parent
        width: 175
        spacing: 8
        Text {
            width: parent.width
            text: root.cancelling ? "INTERROMPENDO" : root.running ? "PARAR" : "VERIFICAR"
            color: Theme.text
            horizontalAlignment: Text.AlignHCenter
            font.family: "Segoe UI Variable Display"
            font.pixelSize: root.cancelling ? 17 : 25
            font.weight: Font.Light
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            visible: root.running
            text: root.determinate ? Math.round(root.progress * 100) + "%" : "Em andamento"
            color: Theme.accentLight
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
    }

    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        enabled: !root.cancelling
        onClicked: root.activated()
    }
}
