import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AudioAssistant

// Provider picker plus the "add chat" intent.
// Public API: `providers` (the offered providers) and the
// `chatRequested(providerId)` signal, which the composition root turns into a
// new chat. The component only reports the selected id - it never creates a
// chat or reads a provider's internals.
RowLayout {
    id: root

    required property Providers providers

    signal chatRequested(string providerId)

    spacing: Theme.spacingMedium

    ComboBox {
        id: picker

        objectName: "providerPicker"
        Layout.fillWidth: true
        model: root.providers
        textRole: "displayName"
        valueRole: "id"
        Accessible.name: qsTr("Provider")
    }

    Button {
        text: qsTr("Add chat")
        objectName: "addChatButton"
        enabled: picker.count > 0
        Accessible.name: qsTr("Add a chat with the selected provider")
        onClicked: root.chatRequested(picker.currentValue)
    }
}
