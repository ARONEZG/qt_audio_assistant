import QtQuick
import AudioAssistant

// The grid of independent chats.
// Public API: `chats`, the grid model. Each cell is a ChatPanel bound to one
// Chat exposed by the model. A positioner with an eager Repeater is used rather
// than a virtualised GridView: the grid holds a handful of chats, and eager
// cells keep every panel (and its live streaming state) alive instead of being
// recycled on scroll. Scrolling is provided by the surrounding Flickable.
Flickable {
    id: root

    required property Chats chats

    contentWidth: width
    contentHeight: grid.height
    clip: true
    Accessible.name: qsTr("Chats")

    Grid {
        id: grid

        width: root.width
        columns: 2
        spacing: Theme.spacingMedium

        Repeater {
            model: root.chats
            delegate: ChatPanel {
                // The delegate's parent is the Grid positioner.
                width: (parent.width - Theme.spacingMedium) / 2
                height: Theme.panelHeight
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: root.chats.count === 0
        text: qsTr("No chats yet. Choose a provider and add one.")
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeBody
        Accessible.role: Accessible.StaticText
    }
}
