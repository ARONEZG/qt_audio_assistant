import QtQuick
import AudioAssistant

// One message row: a bubble aligned by author and coloured from the theme.
// Public API: the model roles `text`, `markup`, `isUser`, `hasImage` and
// `imageSource`, injected by the owning ListView. The delegate contains no
// logic - it only maps the author to a text format and renders what the model
// exposes: the assistant's already sanitised Markdown, or the user's own plain
// text, plus an optional image preview for a message pasted from the clipboard.
Item {
    id: root

    required property string text
    required property string markup
    required property bool isUser
    required property bool hasImage
    required property string imageSource

    readonly property bool hasText: text.length > 0
    // The bubble never grows past this, so a preview also fits a narrow window.
    readonly property real maximumBubbleWidth: root.width * 0.8

    objectName: "messageDelegate"
    implicitHeight: bubble.height

    Rectangle {
        id: bubble

        // User messages hug the right edge, assistant messages the left.
        anchors.right: root.isUser ? root.right : undefined
        anchors.left: root.isUser ? undefined : root.left

        // Width follows the widest visible part (caption or image), bounded by
        // the panel. The image itself is bounded independently of the bubble, so
        // this binding has no cycle through the image width.
        width: Math.min(Math.max(root.hasText ? label.implicitWidth : 0, photo.visible ? photo.width : 0) + 2 * Theme.spacingMedium, root.maximumBubbleWidth)
        height: content.implicitHeight + 2 * Theme.spacingMedium
        radius: Theme.radius
        color: root.isUser ? Theme.userBubble : Theme.assistantBubble

        Column {
            id: content

            anchors.centerIn: parent
            width: bubble.width - 2 * Theme.spacingMedium
            spacing: root.hasText && root.hasImage ? Theme.spacingTight : 0

            Image {
                id: photo

                objectName: "messageImage"
                visible: root.hasImage
                source: root.hasImage ? root.imageSource : ""
                // A data URL is decoded off the GUI thread; the image appears a
                // moment after the message row, which is acceptable for a preview.
                asynchronous: true
                smooth: true
                fillMode: Image.PreserveAspectFit

                // Keep the natural aspect ratio while capping both dimensions:
                // the width limit comes from the bubble, the height limit keeps a
                // portrait photo from filling the whole transcript.
                readonly property real naturalRatio: implicitHeight > 0 ? implicitWidth / implicitHeight : 1
                readonly property real maximumWidth: Math.max(1, root.maximumBubbleWidth - 2 * Theme.spacingMedium)
                width: Math.min(implicitWidth, maximumWidth, Theme.messageImageMaxHeight * naturalRatio)
                height: naturalRatio > 0 ? width / naturalRatio : 0

                Accessible.role: Accessible.Graphic
                Accessible.name: qsTr("Image pasted from the clipboard")
            }

            // Selectable so a reply can be copied out of the chat. The assistant
            // may answer with Markdown (sanitised in C++); the user's own message
            // is shown verbatim as plain text.
            SelectableText {
                id: label

                objectName: "messageText"
                width: content.width
                visible: root.hasText
                text: root.markup
                textFormat: root.isUser ? TextEdit.PlainText : TextEdit.MarkdownText
                // Sanitisation in C++ leaves only http/https links, so activating
                // one can never hand the system a javascript:/data: URL.
                onLinkActivated: link => Qt.openUrlExternally(link)
                // A message is a text passage, not an interactive control; the
                // accessible name uses the raw text, not the Markdown escapes.
                Accessible.name: root.isUser ? qsTr("You said: %1").arg(root.text) : qsTr("Assistant said: %1").arg(root.text)
            }
        }
    }
}
