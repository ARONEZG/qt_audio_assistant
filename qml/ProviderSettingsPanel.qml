import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AudioAssistant

// Provider configuration form: base URL, model and API key are persisted by the
// save command. The key is a masked, write-only field: it is never read back
// from ProviderSettings into the view.
// Public API: `settings`, injected by the composition root. The form keeps the
// fields as a local draft (ProviderSettings exposes read-only properties and a
// single save command), so editing never mutates the stored configuration.
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
                id: baseUrlField

                objectName: "providerBaseUrlField"
                Layout.fillWidth: true
                placeholderText: qsTr("https://host/v1")
                text: root.settings.baseUrl
                onTextEdited: root.settings.clearError()
                Accessible.name: qsTr("Provider base URL")
            }

            Label {
                text: qsTr("Model")
                color: Theme.textSecondary
            }

            TextField {
                id: modelField

                objectName: "providerModelField"
                Layout.fillWidth: true
                placeholderText: qsTr("model name")
                text: root.settings.model
                onTextEdited: root.settings.clearError()
                Accessible.name: qsTr("Model")
            }

            Label {
                text: qsTr("API key")
                color: Theme.textSecondary
            }

            TextField {
                id: apiKeyField

                objectName: "providerApiKeyField"
                Layout.fillWidth: true
                // The key is masked and write-only: never read back from
                // ProviderSettings into the view. It is persisted by the save
                // command together with the endpoint and model. An empty field
                // keeps the key that may already be saved or come from the
                // environment.
                echoMode: TextInput.Password
                placeholderText: root.settings.isKeyConfigured ? qsTr("configured — type to replace") : qsTr("saved with settings")
                onTextEdited: root.settings.clearError()
                Accessible.name: qsTr("API key")
                Accessible.description: qsTr("Saved with the provider settings; masked and never read back")
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
                onClicked: {
                    if (root.settings.save(baseUrlField.text, modelField.text, apiKeyField.text)) {
                        // The command normalised the draft; reflect the stored
                        // values and drop the write-only key from the field.
                        baseUrlField.text = root.settings.baseUrl;
                        modelField.text = root.settings.model;
                        apiKeyField.text = "";
                    }
                }
            }
        }
    }
}
