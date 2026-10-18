pragma ComponentBehavior: Bound

import QtQuick
import AudioAssistant

// The single open chat.
// Public API: `chats`, the collection whose single row is presented. The Chat is
// bound to a ChatPanel; the delegate's `chat` property is populated from the
// model's `chat` role. An empty collection shows a hint instead of a blank
// panel. The eager Repeater keeps the panel (and its live streaming state)
// alive rather than recycling it.
Item {
    id: root

    required property Chats chats
    required property AudioCapture audioCapture

    Accessible.name: qsTr("Chat")

    Repeater {
        model: root.chats
        // The delegate's parent is the Repeater's parent (this Item), so it sizes
        // itself to the view without reaching for the outer id; the capture
        // facade is passed down explicitly.
        delegate: ChatPanel {
            width: parent.width
            height: parent.height
            audioCapture: root.audioCapture
        }
    }

    Text {
        anchors.centerIn: parent
        visible: root.chats.count === 0
        text: qsTr("No chat is open.")
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeBody
        Accessible.role: Accessible.StaticText
    }
}
