import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AudioAssistant

// The settings dialog opened from the top bar's gear button. It collects the
// two configuration concerns in one place, outside the chat: which provider to
// talk to, and that provider's endpoint/model/key.
//
// Public API: `providers` and `settings` (injected by the composition root) and
// the `providerApplied(providerId)` signal. The dialog owns no chat state: it reports
// the one intent - "apply this provider's configuration" - which the
// composition root turns into a provider swap on the open chat (keeping its
// conversation). It is emitted when the provider selection changes and when the
// settings form is saved.
Dialog {
    id: root

    required property Providers providers
    required property ProviderSettings settings
    required property CaptureSettings captureSettings

    // The provider whose (possibly just saved) configuration must be applied.
    signal providerApplied(string providerId)

    // The current selection. It mirrors the picker once the dialog content is
    // created and stays empty until then, so a save() that happens before the
    // dialog was ever opened cannot reach into the not-yet-created picker.
    property string selectedProviderId: ""

    objectName: "settingsDialog"
    title: qsTr("Settings")
    modal: true
    width: 460
    // Tall enough for both configuration panels; the content scrolls when the
    // window (or a future panel) makes it taller than this.
    height: 640
    // Dialog is a popup, not an item; its anchors group is the supported way to
    // centre it over the window.
    anchors.centerIn: parent
    standardButtons: Dialog.Close

    // A saved endpoint/model/key must take effect immediately, so applying it
    // reuses the same intent as a provider switch.
    Connections {
        target: root.settings
        function onSaved() {
            if (root.selectedProviderId.length > 0) {
                root.providerApplied(root.selectedProviderId);
            }
        }
    }

    ScrollView {
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: Theme.spacingMedium

            Label {
                text: qsTr("Provider")
                color: Theme.textSecondary
            }

            ComboBox {
                id: picker

                objectName: "providerPicker"
                Layout.fillWidth: true
                model: root.providers
                textRole: "displayName"
                valueRole: "id"
                Accessible.name: qsTr("Provider")
                Component.onCompleted: root.selectedProviderId = picker.currentValue
                onActivated: index => {
                    root.selectedProviderId = picker.valueAt(index);
                    root.providerApplied(root.selectedProviderId);
                }
            }

            ProviderSettingsPanel {
                Layout.fillWidth: true
                settings: root.settings
            }

            CaptureSettingsPanel {
                Layout.fillWidth: true
                settings: root.captureSettings
            }
        }
    }
}
