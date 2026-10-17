import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AudioAssistant

// The text input row for one chat.
// Public API: `busy` disables input while a reply is in flight; the
// `submitted(text)` signal is the send intent. The component owns no logic: it
// clears the field and forwards the text, which the chat turns into a message.
RowLayout {
    id: root

    property bool busy: false

    signal submitted(string text)

    spacing: Theme.spacingMedium

    TextField {
        id: input

        objectName: "messageInput"
        Layout.fillWidth: true
        enabled: !root.busy
        placeholderText: qsTr("Type a message")
        font.pixelSize: Theme.fontSizeBody
        Accessible.name: qsTr("Message")
        onAccepted: root.submit()
    }

    Button {
        id: sendButton

        objectName: "sendButton"
        text: qsTr("Send")
        enabled: !root.busy && input.text.length > 0
        Accessible.name: qsTr("Send message")
        onClicked: root.submit()
    }

    // Trivial intent forwarding: emit the typed text and clear the field. An
    // empty submission is a no-op so the chat never records a blank message by
    // accident (the button is disabled for it; Enter goes through here).
    function submit() {
        if (input.text.length === 0) {
            return;
        }
        root.submitted(input.text);
        input.clear();
    }
}
