import QtQuick
import AudioAssistant

// One message row: a bubble aligned by author and coloured from the theme.
// Public API: the model roles `text`, `markup` and `isUser`, injected by the
// owning ListView. The delegate contains no logic - it only maps the author to a
// text format and renders what the model exposes: the assistant's already
// sanitised Markdown, or the user's own plain text.
Item {
    id: root

    required property string text
    required property string markup
    required property bool isUser

    objectName: "messageDelegate"
    implicitHeight: bubble.height

    Rectangle {
        id: bubble

        // User messages hug the right edge, assistant messages the left.
        anchors.right: root.isUser ? root.right : undefined
        anchors.left: root.isUser ? undefined : root.left

        width: Math.min(label.implicitWidth + 2 * Theme.spacingMedium, root.width * 0.8)
        height: label.implicitHeight + 2 * Theme.spacingMedium
        radius: Theme.radius
        color: root.isUser ? Theme.userBubble : Theme.assistantBubble

        // Selectable so a reply can be copied out of the chat. The assistant may
        // answer with Markdown (sanitised in C++); the user's own message is
        // shown verbatim as plain text.
        SelectableText {
            id: label

            objectName: "messageText"
            anchors.centerIn: parent
            width: parent.width - 2 * Theme.spacingMedium
            text: root.markup
            textFormat: root.isUser ? TextEdit.PlainText : TextEdit.MarkdownText
            // Sanitisation in C++ leaves only http/https links, so activating one
            // can never hand the system a javascript:/data: URL.
            onLinkActivated: link => Qt.openUrlExternally(link)
            // A message is a text passage, not an interactive control; the
            // accessible name uses the raw text, not the Markdown escapes.
            Accessible.name: root.isUser ? qsTr("You said: %1").arg(root.text) : qsTr("Assistant said: %1").arg(root.text)
        }
    }
}
