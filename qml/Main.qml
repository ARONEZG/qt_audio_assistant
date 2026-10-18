import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AudioAssistant

// The application window. Its four typed properties are the composition root's
// dependencies, injected by main() before the component is loaded: the open
// chats, the offered providers, the thin invokable that opens a chat for a
// provider and the provider settings. The view only binds them together.
//
// The layout is deliberately a single chat with a top bar: the title on the
// left and the settings gear on the right, outside the chat. Configuration
// lives in the settings dialog, never inside the conversation.
ApplicationWindow {
    id: window

    required property Chats chats
    required property Providers providers
    required property NewChat newChat
    required property ProviderSettings providerSettings
    required property AudioCapture audioCapture

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

        // Top bar: the app name and, on the far right, the settings gear. The
        // gear sits outside the chat so configuration is a separate concern.
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingMedium
            spacing: Theme.spacingMedium

            Text {
                Layout.fillWidth: true
                text: qsTr("Audio Assistant")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeTitle
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
                Accessible.name: qsTr("Audio Assistant")
            }

            ToolButton {
                objectName: "settingsButton"
                text: "\u2699"
                Accessible.name: qsTr("Settings")
                onClicked: settingsDialog.open()
            }
        }

        ChatView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.spacingMedium
            chats: window.chats
            audioCapture: window.audioCapture
        }
    }

    SettingsDialog {
        id: settingsDialog

        providers: window.providers
        settings: window.providerSettings

        // Switching the provider or saving the endpoint/model/key applies the
        // new provider to the open chat, keeping its conversation: the chat
        // continues with the new backend instead of starting over.
        onProviderApplied: providerId => window.newChat.open(providerId)
    }
}
