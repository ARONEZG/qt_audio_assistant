import QtQuick
import QtQuick.Layouts
import AudioAssistant

// The transcript of one chat: the finished messages plus the live streaming
// tail while an answer is in flight.
// Public API: `chat`, the Chat this view presents. The messages come from the
// Chat itself (it is the message model); the streaming text and state come from
// its properties.
Item {
    id: root

    required property Chat chat

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingTight

        ListView {
            id: transcript

            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.chat
            spacing: Theme.spacingTight
            clip: true
            delegate: MessageDelegate {
                width: ListView.view.width
            }
            Accessible.name: qsTr("Conversation")

            // Keep the newest message visible as the history grows.
            onCountChanged: transcript.positionViewAtEnd()
        }

        Text {
            id: streamingTail

            objectName: "streamingTail"
            Layout.fillWidth: true
            visible: root.chat.streaming
            text: root.chat.streamingMarkup
            // The live reply is assistant content: rendered as Markdown through
            // the same C++ sanitisation boundary as a finished message.
            textFormat: Text.MarkdownText
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("Assistant is replying")
        }
    }
}
