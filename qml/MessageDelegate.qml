import QtQuick
import AudioAssistant

// One message row: a bubble aligned by author and coloured from the theme.
// Public API: the model roles `text` and `isUser`, injected by the owning
// ListView. The delegate contains no logic - it renders what the model exposes.
Item {
    id: root

    required property string text
    required property bool isUser

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

        Text {
            id: label

            anchors.centerIn: parent
            width: parent.width - 2 * Theme.spacingMedium
            text: root.text
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
            // A message is a text passage, not an interactive control.
            Accessible.role: Accessible.StaticText
            Accessible.name: root.isUser ? qsTr("You said: %1").arg(root.text) : qsTr("Assistant said: %1").arg(root.text)
        }
    }
}
