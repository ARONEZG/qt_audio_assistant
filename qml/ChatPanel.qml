import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AudioAssistant

// The panel of the single open chat: a header with the provider name, the
// background-audio switch and the stream controls, then the transcript and the
// composer, all bound to a single Chat.
// Public API: `chat`, injected by the view from the model's role. All the panel
// does is bind the chat's state and forward the user's intentions (send, cancel,
// toggle capture) to it - it holds no state of its own.
Rectangle {
    id: root

    required property Chat chat

    objectName: "chatPanel"
    implicitWidth: Theme.panelMinimumWidth
    implicitHeight: Theme.panelMinimumHeight
    color: Theme.surface
    radius: Theme.radius
    border.color: Theme.border

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMedium
        spacing: Theme.spacingMedium

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium

            Text {
                Layout.fillWidth: true
                text: root.chat.providerName
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeTitle
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
                Accessible.name: root.chat.providerName
            }

            // Placeholder switch for background audio capture. It only mirrors
            // the chat's flag; the flag itself is inert until the capture
            // pipeline is wired in, so toggling it currently captures nothing.
            CheckBox {
                objectName: "backgroundCaptureToggle"
                text: qsTr("Background audio capture")
                checked: root.chat.backgroundCaptureEnabled
                onCheckedChanged: root.chat.backgroundCaptureEnabled = checked
                Accessible.name: qsTr("Background audio capture")
            }

            BusyIndicator {
                running: root.chat.streaming
                visible: root.chat.streaming
                Accessible.name: qsTr("Waiting for a reply")
            }

            Button {
                text: qsTr("Cancel")
                objectName: "cancelButton"
                visible: root.chat.streaming
                Accessible.name: qsTr("Cancel the reply")
                onClicked: root.chat.cancel()
            }
        }

        ConversationView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            chat: root.chat
        }

        // A failed request is reported next to the composer: the chat exposes
        // the already-formatted reason and the view only mirrors it.
        Text {
            objectName: "chatError"
            Layout.fillWidth: true
            visible: root.chat.hasError
            text: root.chat.errorText
            color: Theme.error
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.Wrap
            Accessible.role: Accessible.StaticText
            Accessible.name: root.chat.errorText
        }

        Composer {
            Layout.fillWidth: true
            busy: root.chat.streaming
            onSubmitted: text => root.chat.send(text)
        }
    }
}
