pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import AudioAssistant

// Read-only text that behaves like a native text passage: it can be selected
// with the mouse or the keyboard and copied with Ctrl+C, and it offers the
// usual Copy / Select All context menu on right click.
// Public API: it is a TextEdit, so callers set `text`, `textFormat`, `color`
// and connect `onLinkActivated`; everything else is selection presentation.
// The selection is persistent so opening the context menu (which takes focus)
// does not discard it before Copy runs.
TextEdit {
    id: root

    readOnly: true
    selectByMouse: true
    selectByKeyboard: true
    persistentSelection: true
    cursorVisible: false
    wrapMode: TextEdit.Wrap
    textFormat: TextEdit.PlainText
    color: Theme.textPrimary
    selectionColor: Theme.accent
    selectedTextColor: Theme.textPrimary
    font.pixelSize: Theme.fontSizeBody

    Accessible.role: Accessible.StaticText

    // TextEdit consumes mouse presses for its own selection; a pointer handler
    // observes the right button without taking the left-button press away from
    // it. The menu opens at the click position, relative to this item.
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: eventPoint => contextMenu.popup(eventPoint.position.x, eventPoint.position.y)
    }

    Menu {
        id: contextMenu

        objectName: "textContextMenu"

        MenuItem {
            objectName: "copyMenuItem"
            text: qsTr("Copy")
            enabled: root.selectedText.length > 0
            onTriggered: root.copy()
        }

        MenuItem {
            objectName: "selectAllMenuItem"
            text: qsTr("Select All")
            onTriggered: root.selectAll()
        }
    }
}
