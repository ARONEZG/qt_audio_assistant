import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AudioAssistant

// The text input row for one chat.
// Public API: `busy` disables input while a reply is in flight; `chat` is the
// conversation the composer posts to, and `imagePaster` is the command that
// turns a clipboard image into a message. The `submitted(text)` signal is the
// send intent. The component owns no logic: it clears the field and forwards
// the text, which the chat turns into a message.
RowLayout {
    id: root

    required property Chat chat
    required property PasteImage imagePaster
    property bool busy: false

    signal submitted(string text)

    // Caption captured when an image paste starts. The field is cleared only
    // when the command confirms the message was actually appended, so a failed
    // or rejected paste never loses the typed text.
    property string pendingImageCaption: ""

    spacing: Theme.spacingMedium

    TextField {
        id: input

        objectName: "messageInput"
        Layout.fillWidth: true
        enabled: !root.busy
        placeholderText: qsTr("Type a message (paste an image from the clipboard)")
        font.pixelSize: Theme.fontSizeBody
        Accessible.name: qsTr("Message")
        Accessible.description: qsTr("Paste an image from the clipboard to send it as a message")
        onAccepted: root.submit()

        // Ctrl+V (and the platform's other Paste keys) first asks the C++
        // command whether the clipboard holds an image. It consumes the gesture
        // when it does, so the image is sent instead of pasted as text; with no
        // image the event falls through to the field's normal text paste.
        //
        // Encoding runs on a worker thread, so the actual append is confirmed
        // later by imagePasted(). The caption is cleared in that handler, not
        // here: a rejected or failed paste leaves the typed text intact.
        Keys.onPressed: event => {
            if (event.matches(StandardKey.Paste) && root.imagePaster.hasImage()) {
                root.pendingImageCaption = input.text;
                if (!root.imagePaster.paste(root.chat, input.text)) {
                    root.pendingImageCaption = "";
                }
                event.accepted = true;
            }
        }
    }

    Connections {
        target: root.imagePaster

        function onImagePasted() {
            if (input.text === root.pendingImageCaption) {
                input.clear();
            }
            root.pendingImageCaption = "";
        }

        function onPasteFailed() {
            root.pendingImageCaption = "";
        }
    }

    Button {
        id: sendButton

        objectName: "sendButton"
        text: qsTr("Send")
        enabled: !root.busy && input.text.length > 0
        Accessible.name: qsTr("Send message")
        onClicked: root.submit()
    }

    // Trivial intent forwarding: emit the typed text and clear the field. The
    // domain ignores an empty message (Chat::send), so no guard is needed here.
    function submit() {
        root.submitted(input.text);
        input.clear();
    }
}
