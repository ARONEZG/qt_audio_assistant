import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AudioAssistant

// The application window. Its three typed properties are the composition root's
// dependencies, injected by main() before the component is loaded: the chat
// grid, the offered providers and the thin invokable that turns the "add chat"
// intent into chats.add(providers.fresh(id)). The view only binds them together.
ApplicationWindow {
    id: window

    required property Chats chats
    required property Providers providers
    required property NewChat newChat
    required property ProviderSettings providerSettings

    title: qsTr("Audio Assistant")
    visible: true
    width: 1000
    height: 720
    minimumWidth: 640
    minimumHeight: 480
    color: Theme.background

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ProviderSelector {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingMedium
            providers: window.providers
            onChatRequested: providerId => window.newChat.open(providerId)
        }

        ProviderSettingsPanel {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingMedium
            Layout.rightMargin: Theme.spacingMedium
            settings: window.providerSettings
        }

        ChatGrid {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.spacingMedium
            chats: window.chats
        }
    }
}
