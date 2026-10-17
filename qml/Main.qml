import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    title: "Audio Assistant"
    visible: true
    minimumWidth: 480
    minimumHeight: 320
    width: 640
    height: 480

    color: "#1e1e1e"

    ColumnLayout {
        anchors.fill: parent

        Label {
            text: "Audio Assistant"
            font.pixelSize: 24
            horizontalAlignment: Text.AlignHCenter
            color: "#ffffff"
        }

        Label {
            text: "Каркас приложения собран. Функции пока не реализованы."
            font.pixelSize: 14
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: "#aaaaaa"
        }

        Button {
            text: "Close"
            Layout.alignment: Qt.AlignHCenter
            onClicked: Qt.quit()
        }
    }
}
