import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AudioAssistant

// Provider configuration form: endpoint and model are persisted, the API key
// stays in memory for the session and is masked while typed.
// Public API: `settings`, injected by the composition root. The component holds
// no configuration and performs no I/O; it binds the fields to ProviderSettings
// and forwards the "save" intent.
GroupBox {
    id: root

    required property ProviderSettings settings

    title: qsTr("Provider settings")
    Accessible.name: qsTr("Provider settings")

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingMedium

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacingMedium
            rowSpacing: Theme.spacingTight

            Label {
                text: qsTr("Base URL")
                color: Theme.textSecondary
            }

            TextField {
                objectName: "providerBaseUrlField"
                Layout.fillWidth: true
                placeholderText: qsTr("https://host/v1")
                text: root.settings.baseUrl
                onTextEdited: root.settings.baseUrl = text
                Accessible.name: qsTr("Provider base URL")
            }

            Label {
                text: qsTr("Model")
                color: Theme.textSecondary
            }

            TextField {
                objectName: "providerModelField"
                Layout.fillWidth: true
                placeholderText: qsTr("model name")
                text: root.settings.model
                onTextEdited: root.settings.model = text
                Accessible.name: qsTr("Model")
            }

            Label {
                text: qsTr("API key")
                color: Theme.textSecondary
            }

            TextField {
                objectName: "providerApiKeyField"
                Layout.fillWidth: true
                // The key is a session secret: it is masked in the UI and never
                // written to the configuration file.
                echoMode: TextInput.Password
                placeholderText: qsTr("session only")
                text: root.settings.apiKey
                onTextEdited: root.settings.apiKey = text
                Accessible.name: qsTr("API key for this session")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium

            Label {
                objectName: "providerSettingsError"
                Layout.fillWidth: true
                visible: text.length > 0
                text: root.settings.lastError
                color: Theme.error
                wrapMode: Text.WordWrap
                Accessible.name: qsTr("Provider settings error")
            }

            Button {
                objectName: "saveProviderSettingsButton"
                text: qsTr("Save")
                Accessible.name: qsTr("Save provider settings")
                onClicked: root.settings.save()
            }
        }
    }
}
