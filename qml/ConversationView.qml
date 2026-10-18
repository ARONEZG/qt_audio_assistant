pragma ComponentBehavior: Bound

import QtQuick
import AudioAssistant

// The transcript of one chat: the finished messages plus the live streaming
// tail while an answer is in flight.
// Public API: `chat`, the Chat this view presents. The messages come from the
// Chat itself (it is the message model); the streaming text and state come from
// its properties.
//
// The live tail is the list's footer, not a sibling of it: a reasoning answer is
// far taller than the panel, so as a sibling Text it grew past the panel and
// painted over its neighbours. As the last row of the clipped, scrolling
// transcript the tail is bounded by the view that owns it.
Item {
    id: root

    required property Chat chat

    ListView {
        id: transcript

        objectName: "transcript"
        anchors.fill: parent
        model: root.chat
        spacing: Theme.spacingTight
        clip: true
        delegate: MessageDelegate {
            width: transcript.width
        }
        Accessible.name: qsTr("Conversation")

        // The in-flight answer as the last row of the transcript. The container
        // exists for the whole chat, but between answers the tail is hidden and
        // the container's height collapses to zero: an invisible footer item
        // still occupies the height it reports.
        footer: Item {
            id: tailContainer

            width: transcript.width
            // ListView.spacing separates delegates only, so this is the single
            // gap before the tail (the Text itself is offset by it).
            height: streamingTail.visible ? streamingTail.implicitHeight + Theme.spacingTight : 0

            Text {
                id: streamingTail

                objectName: "streamingTail"
                y: Theme.spacingTight
                width: parent.width
                visible: root.chat.streaming
                text: root.chat.streamingMarkup
                // The live reply is assistant content: rendered as Markdown
                // through the same C++ sanitisation boundary as a finished
                // message.
                textFormat: Text.MarkdownText
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                wrapMode: Text.Wrap
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("Assistant is replying")
            }
        }

        // Whether the newest content is kept on screen. Every scroll that is not
        // our own follow (wheel, drag, flick) hands the view to the user, and
        // scrolling back to the end resumes following.
        property bool followTail: true

        onContentYChanged: followTail = atYEnd

        // A new message - and the answer it completes into - is always brought
        // into view; the tail only grows while it is followed, so a user reading
        // the history is not pulled down by every chunk.
        onCountChanged: positionViewAtEnd()

        onContentHeightChanged: {
            if (followTail) {
                positionViewAtEnd();
            }
        }

        onHeightChanged: {
            if (followTail) {
                positionViewAtEnd();
            }
        }
    }
}
