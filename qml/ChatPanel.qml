import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AudioAssistant

// The panel of the single open chat: a header with the provider name, the
// background-audio switch and the stream controls, then the transcript and the
// composer, all bound to a single Chat.
// Public API: `chat`, injected by the view from the model's role, and
// `audioCapture`, the capture-pipeline facade passed down by the view. The panel
// only binds their state and forwards the user's intentions (send, cancel,
// toggle capture, transcribe) - it holds no state of its own.
Rectangle {
    id: root

    required property Chat chat
    required property AudioCapture audioCapture

    objectName: "chatPanel"
    implicitWidth: Theme.panelMinimumWidth
    implicitHeight: Theme.panelMinimumHeight
    color: Theme.surface
    radius: Theme.radius
    border.color: Theme.border

    // A transcription is text the user did not type: it is appended to the draft
    // so it can be reviewed or edited before sending. The facade never emits
    // synchronously (the TranscriptionProvider contract), so this runs later.
    Connections {
        target: root.audioCapture
        function onTranscribed(text) {
            composer.insertText(text);
        }
    }

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

            // The capture switch. It sets the chat's flag; the composition root
            // maps that flag to starting/stopping the capture pipeline (and
            // reverts the flag if the microphone is unavailable).
            //
            // Two-way sync: the Binding element keeps `checked` following the
            // chat flag (and survives the control's own write on a click, unlike
            // an inline `checked:` binding), while onCheckedChanged pushes the
            // control's state back to the chat. onCheckedChanged is used rather
            // than onToggled because toggled() is emitted only by a real user
            // click, not by a programmatic toggle().
            CheckBox {
                id: captureToggle

                objectName: "backgroundCaptureToggle"
                text: qsTr("Background audio capture")
                Accessible.name: qsTr("Background audio capture")
                onCheckedChanged: root.chat.backgroundCaptureEnabled = captureToggle.checked

                Binding {
                    target: captureToggle
                    property: "checked"
                    value: root.chat.backgroundCaptureEnabled
                }
            }

            // Sends the last N seconds of captured audio for recognition; the
            // recognized text lands in the composer through onTranscribed above.
            // Only meaningful while capture is on, so it follows the switch.
            Button {
                objectName: "transcribeButton"
                text: qsTr("Transcribe")
                enabled: root.chat.backgroundCaptureEnabled
                Accessible.name: qsTr("Transcribe the buffered audio")
                onClicked: root.audioCapture.transcribeLast()
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

        // A failed transcription is reported the same way, from the facade's own
        // error state (kept separate from a failed chat reply).
        Text {
            objectName: "captureError"
            Layout.fillWidth: true
            visible: root.audioCapture.hasError
            text: root.audioCapture.errorText
            color: Theme.error
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.Wrap
            Accessible.role: Accessible.StaticText
            Accessible.name: root.audioCapture.errorText
        }

        Composer {
            id: composer

            Layout.fillWidth: true
            busy: root.chat.streaming
            onSubmitted: text => root.chat.send(text)
        }
    }
}
