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
    // Shift+arrows select once the passage has focus, but the passage is not a
    // Tab stop: a long transcript must not make Tab walk through every message.
    // A click focuses it, after which Ctrl+C copies the selection.
    selectByKeyboard: true
    activeFocusOnTab: false
    persistentSelection: true
    wrapMode: TextEdit.Wrap
    textFormat: TextEdit.PlainText
    color: Theme.textPrimary
    selectionColor: Theme.accent
    selectedTextColor: Theme.textPrimary
    font.pixelSize: Theme.fontSizeBody

    // A read-only TextEdit paints no caret. Keyboard selection is still usable:
    // Shift+arrows highlight the passage, and Ctrl+C copies that highlight.
    // The accessibility role is left at the TextEdit default (editable text,
    // reported as read-only), because the passage is interactive now.

    // Right-click opens the menu. Control+click is the one-button-mouse
    // convention on macOS; accepting it everywhere costs nothing and keeps the
    // gesture working whichever form the platform delivers it in.
    TapHandler {
        acceptedButtons: Qt.RightButton
        onPressedChanged: {
            if (pressed && Application.styleHints.contextMenuTrigger === Qt.ContextMenuTrigger.Press) {
                contextMenu.popup(point.position.x, point.position.y);
            }
        }
        onTapped: eventPoint => {
            if (Application.styleHints.contextMenuTrigger === Qt.ContextMenuTrigger.Release) {
                contextMenu.popup(eventPoint.position.x, eventPoint.position.y);
            }
        }
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        acceptedModifiers: Qt.ControlModifier
        onPressedChanged: {
            if (pressed && Application.styleHints.contextMenuTrigger === Qt.ContextMenuTrigger.Press) {
                contextMenu.popup(point.position.x, point.position.y);
            }
        }
        onTapped: eventPoint => {
            if (Application.styleHints.contextMenuTrigger === Qt.ContextMenuTrigger.Release) {
                contextMenu.popup(eventPoint.position.x, eventPoint.position.y);
            }
        }
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
