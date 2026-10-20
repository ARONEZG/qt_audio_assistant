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

        // The in-flight answer, shown as the last row of the transcript. A
        // hidden footer still reserves its height in the view, so the container
        // collapses to nothing between answers instead of leaving a gap.
        footer: Item {
            id: tailContainer

            width: transcript.width
            height: streamingTail.visible ? streamingTail.implicitHeight + Theme.spacingTight : 0

            SelectableText {
                id: streamingTail

                objectName: "streamingTail"
                y: Theme.spacingTight
                width: parent.width
                visible: root.chat.streaming
                text: root.chat.streamingMarkup
                // The live reply is assistant content: rendered as Markdown
                // through the same C++ sanitisation boundary as a finished
                // message, and selectable while it streams.
                textFormat: TextEdit.MarkdownText
                color: Theme.textSecondary
                Accessible.name: qsTr("Assistant is replying")
            }
        }

        // Whether the newest content is kept on screen. A view scrolls itself
        // only while it shows the end; any user scroll (wheel, drag, flick)
        // away from the end hands the view to the reader, and returning to the
        // end resumes following. Without this guard the list fights the user:
        // it jumps to the bottom on every streamed chunk and on every delegate
        // it materialises while contentHeight is being refined.
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
