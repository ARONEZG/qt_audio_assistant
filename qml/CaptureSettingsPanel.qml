import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AudioAssistant

// Capture and transcription configuration form.
// Public API: `settings`, injected by the composition root. The form keeps the
// fields as a local draft (CaptureSettings exposes read-only properties and a
// single save command), so editing and choosing a preset never mutate the
// stored configuration. All validation and persistence live in C++.
GroupBox {
    id: root

    required property CaptureSettings settings

    title: qsTr("Capture & transcription")
    Accessible.name: qsTr("Capture and transcription settings")

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingMedium

        ComboBox {
            id: capturePresetPicker

            objectName: "capturePresetPicker"
            Layout.fillWidth: true
            model: root.settings.presets
            textRole: "displayName"
            valueRole: "id"
            Accessible.name: qsTr("Capture preset")
            Accessible.description: qsTr("Fills the fields below with the preset values; save to apply")
            // A preset only fills the draft; the user still saves explicitly.
            onActivated: index => {
                captureDurationSpinBox.value = root.settings.presets.captureDurationSecondsAt(index);
                vadSwitch.checked = root.settings.presets.vadEnabledAt(index);
                minSpeechSpinBox.value = Math.round(root.settings.presets.minSpeechSecondsAt(index) * 100);
                sttModelComboBox.editText = root.settings.presets.sttModelAt(index);
                const language = root.settings.presets.sttLanguageAt(index);
                const languageIndex = sttLanguageComboBox.indexOfValue(language);
                sttLanguageComboBox.currentIndex = languageIndex >= 0 ? languageIndex : 0;
                sttPromptField.text = root.settings.presets.sttPromptAt(index);
                root.settings.clearError();
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacingMedium
            rowSpacing: Theme.spacingTight

            Label {
                text: qsTr("Capture window")
                color: Theme.textSecondary
            }

            SpinBox {
                id: captureDurationSpinBox

                objectName: "captureDurationSpinBox"
                Layout.fillWidth: true
                from: 5
                to: 120
                value: root.settings.captureDurationSeconds
                editable: true
                // SpinBox has no suffix property, so the unit is part of the
                // rendered text; valueFromText tolerates it when typed.
                textFromValue: value => Number(value).toFixed(0) + qsTr(" s")
                valueFromText: text => parseInt(text)
                onValueModified: root.settings.clearError()
                Accessible.name: qsTr("Capture duration in seconds")
            }

            Label {
                text: qsTr("Voice activity detection")
                color: Theme.textSecondary
            }

            Switch {
                id: vadSwitch

                objectName: "vadSwitch"
                checked: root.settings.vadEnabled
                onToggled: root.settings.clearError()
                Accessible.name: qsTr("Voice activity detection")
                Accessible.description: qsTr("Send only the last detected utterance")
            }

            Label {
                text: qsTr("Minimum speech")
                color: Theme.textSecondary
            }

            SpinBox {
                id: minSpeechSpinBox

                objectName: "minSpeechSpinBox"
                Layout.fillWidth: true
                // Qt's SpinBox is integer-only. The guard is stored in
                // hundredths of a second (25..300, step 25 = 0.25 s) and shown
                // as seconds, so the arrows still move by exactly 0.25 s.
                from: 25
                to: 300
                stepSize: 25
                value: Math.round(root.settings.minSpeechSeconds * 100)
                editable: true
                textFromValue: value => (value / 100).toFixed(2)
                valueFromText: text => Math.round(Number(text) * 100)
                onValueModified: root.settings.clearError()
                Accessible.name: qsTr("Minimum speech duration in seconds")
            }

            Label {
                text: qsTr("Model")
                color: Theme.textSecondary
            }

            ComboBox {
                id: sttModelComboBox

                objectName: "sttModelComboBox"
                Layout.fillWidth: true
                editable: true
                model: ["openai/whisper-1", "openai/whisper-large-v3", "openai/whisper-large-v3-turbo", "openai/whisper-medium"]
                Component.onCompleted: editText = root.settings.sttModel
                onActivated: root.settings.clearError()
                onEditTextChanged: root.settings.clearError()
                Accessible.name: qsTr("Speech-to-text model")
            }

            Label {
                text: qsTr("Language")
                color: Theme.textSecondary
            }

            ComboBox {
                id: sttLanguageComboBox

                objectName: "sttLanguageComboBox"
                Layout.fillWidth: true
                textRole: "name"
                valueRole: "code"
                model: sttLanguageModel
                Component.onCompleted: currentIndex = indexOfValue(root.settings.sttLanguage)
                onActivated: root.settings.clearError()
                Accessible.name: qsTr("Speech-to-text language")
            }

            Label {
                text: qsTr("Context")
                color: Theme.textSecondary
            }

            TextField {
                id: sttPromptField

                objectName: "sttPromptField"
                Layout.fillWidth: true
                placeholderText: qsTr("Vocabulary or spelling hint")
                text: root.settings.sttPrompt
                onTextEdited: root.settings.clearError()
                Accessible.name: qsTr("Speech-to-text context")
                Accessible.description: qsTr("Optional single-line vocabulary or spelling hint")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium

            Label {
                objectName: "captureSettingsError"
                Layout.fillWidth: true
                visible: text.length > 0
                text: root.settings.lastError
                color: Theme.error
                wrapMode: Text.WordWrap
                Accessible.name: qsTr("Capture settings error")
            }

            Button {
                objectName: "saveCaptureSettingsButton"
                text: qsTr("Save")
                Accessible.name: qsTr("Save capture settings")
                onClicked: {
                    // An unknown language (for example from a hand-edited file)
                    // is not in the list; keep it instead of silently resetting.
                    const language = sttLanguageComboBox.currentIndex >= 0 ? sttLanguageComboBox.currentValue : root.settings.sttLanguage;
                    if (root.settings.save(captureDurationSpinBox.value, sttModelComboBox.editText, language, sttPromptField.text, vadSwitch.checked, minSpeechSpinBox.value / 100)) {
                        // The command normalised the draft; reflect the stored
                        // values back into the fields.
                        captureDurationSpinBox.value = root.settings.captureDurationSeconds;
                        vadSwitch.checked = root.settings.vadEnabled;
                        minSpeechSpinBox.value = Math.round(root.settings.minSpeechSeconds * 100);
                        sttModelComboBox.editText = root.settings.sttModel;
                        sttLanguageComboBox.currentIndex = sttLanguageComboBox.indexOfValue(root.settings.sttLanguage);
                        sttPromptField.text = root.settings.sttPrompt;
                    }
                }
            }
        }
    }

    // Languages offered by the picker. "Auto" is an empty language: the
    // endpoint then autodetects it.
    ListModel {
        id: sttLanguageModel

        ListElement {
            name: "Auto"
            code: ""
        }
        ListElement {
            name: "ru"
            code: "ru"
        }
        ListElement {
            name: "en"
            code: "en"
        }
        ListElement {
            name: "uk"
            code: "uk"
        }
        ListElement {
            name: "de"
            code: "de"
        }
        ListElement {
            name: "fr"
            code: "fr"
        }
        ListElement {
            name: "es"
            code: "es"
        }
        ListElement {
            name: "it"
            code: "it"
        }
        ListElement {
            name: "pt"
            code: "pt"
        }
        ListElement {
            name: "tr"
            code: "tr"
        }
        ListElement {
            name: "pl"
            code: "pl"
        }
        ListElement {
            name: "nl"
            code: "nl"
        }
        ListElement {
            name: "ja"
            code: "ja"
        }
        ListElement {
            name: "ko"
            code: "ko"
        }
        ListElement {
            name: "zh"
            code: "zh"
        }
    }
}
