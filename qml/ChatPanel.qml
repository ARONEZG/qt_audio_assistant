import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AudioAssistant

// The panel of the single open chat: a header with the provider name, the
// background-audio switch and the stream controls, then the transcript and the
// composer, all bound to a single Chat.
// Public API: `chat`, injected by the view from the model's role, `audioCapture`
// (the capture-pipeline facade for state/errors) and `voiceInput`, the command
// that recognizes the last utterance and posts it. The panel only binds their
// state and forwards the user's intentions (send, cancel, toggle capture,
// transcribe) - it holds no state of its own.
Rectangle {
    id: root

    required property Chat chat
    required property AudioCapture audioCapture
    required property VoiceInput voiceInput
    required property PasteImage pasteImage

    objectName: "chatPanel"
    implicitWidth: Theme.panelMinimumWidth
    implicitHeight: Theme.panelMinimumHeight
    color: Theme.surface
    radius: Theme.radius
    border.color: Theme.border

    // Ctrl+R is the keyboard form of the Transcribe button. Both go through the
    // same C++ command, which recognizes the last utterance and posts it as a
    // message. Disabled in the same states as the button: capture off, a reply
    // in flight, or a transcription already running.
    Shortcut {
        objectName: "voiceInputShortcut"
        sequence: "Ctrl+R"
        enabled: root.chat.backgroundCaptureEnabled && !root.chat.streaming && !root.voiceInput.busy
        onActivated: root.voiceInput.transcribeAndSend()
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

            // Recognizes the last captured utterance and sends it as a message
            // (the same command as Ctrl+R). Only meaningful while capture is on,
            // so it follows the switch; a reply in flight or a running
            // transcription disables it, matching the shortcut.
            Button {
                objectName: "transcribeButton"
                text: qsTr("Transcribe")
                enabled: root.chat.backgroundCaptureEnabled && !root.chat.streaming && !root.voiceInput.busy
                Accessible.name: qsTr("Transcribe and send the buffered audio")
                Accessible.description: qsTr("Recognizes the last captured utterance and sends it to the chat. Shortcut: Ctrl+R")
                onClicked: root.voiceInput.transcribeAndSend()
            }

            BusyIndicator {
                running: root.chat.streaming || root.voiceInput.busy
                visible: running
                Accessible.name: root.voiceInput.busy ? qsTr("Transcribing the captured audio") : qsTr("Waiting for a reply")
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
            chat: root.chat
            imagePaster: root.pasteImage
            onSubmitted: text => root.chat.send(text)
        }
    }
}
