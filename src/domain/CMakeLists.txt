# ── Domain library ──────────────────────────────────────────
# The application's QML view models and composition seams (immutable values,
# provider role interface, collections, the chat/capture controllers). Built as a
# static library so the executable and every test target link the same
# implementation, and used as the backing target of the AudioAssistant QML module
# below (its types are the QML view models).
add_library(audio-assistant-domain STATIC
    Answer.cpp
    AudioCapture.cpp
    CaptureConfig.cpp
    CaptureConfigFile.cpp
    CaptureConfigSource.cpp
    CapturePresets.cpp
    CaptureSettings.cpp
    Chat.cpp
    Chats.cpp
    ClipboardImageSource.cpp
    ImageAttachment.cpp
    ImageEncoder.cpp
    Message.cpp
    NewChat.cpp
    PasteImage.cpp
    Provider.cpp
    ProviderConfig.cpp
    ProviderConfigFile.cpp
    ProviderConfigSource.cpp
    ProviderErrorText.cpp
    ProviderOffer.cpp
    ProviderSettings.cpp
    Providers.cpp
    SafeMarkdown.cpp
    SttLanguage.cpp
    TranscriptionProvider.cpp
    VoiceInput.cpp
)

set_target_properties(audio-assistant-domain PROPERTIES
    CXX_EXTENSIONS OFF
)

# Consumers include domain headers as "domain/<Header>.h".
target_include_directories(audio-assistant-domain PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

# qmltyperegistrar generates `#if __has_include(<Header.h>)` for the types it
# registers (it only keeps the header's file name), so the headers must also be
# reachable by their bare name while the module's registration code compiles.
target_include_directories(audio-assistant-domain PRIVATE
    ${PROJECT_SOURCE_DIR}/src/domain
)

# Qt6::Qml is public because the domain types carry the QML registration macros
# (QML_ELEMENT) in their headers; every consumer of the headers needs the Qml
# include path. The types are the view models, so there is no separate adapter.
# Qt6::Gui is public because ClipboardImageSource exposes a decoded QImage in its
# role interface (the platform clipboard read happens on the GUI thread, while
# encoding runs on a worker). Qt6::Concurrent is private: only PasteImage's .cpp
# uses QtConcurrent::run, and the public contract is asynchronous by signal.
# The audio library is private too: AudioCapture is the capture-pipeline facade
# and its header only forward-declares the audio types, so the dependency is an
# implementation detail (it is still carried to the final link, as CMake does for
# a static library's private dependencies).
target_link_libraries(audio-assistant-domain
    PUBLIC Qt6::Core Qt6::Qml Qt6::Gui
    PRIVATE audio-assistant-strict audio-assistant-qt-warnings audio-assistant-audio Qt6::Concurrent
)

# ── QML module ──────────────────────────────────────────────
# The domain library is the backing target of the AudioAssistant QML module: the
# C++ types it exposes (Chat, Chats, Providers, Message) are the view models, so
# a view binds to them directly and no adapter library is needed. The QML files
# live in the top-level qml/ directory and are placed at the module root; a
# resource alias per file keeps them out of a qml/ subdirectory (the default
# would deploy them under /qt/qml/AudioAssistant/qml/).
set(_audio_assistant_qml_dir "${PROJECT_SOURCE_DIR}/qml")

set(_audio_assistant_qml_files
    "${_audio_assistant_qml_dir}/Main.qml"
    "${_audio_assistant_qml_dir}/Theme.qml"
    "${_audio_assistant_qml_dir}/SelectableText.qml"
    "${_audio_assistant_qml_dir}/MessageDelegate.qml"
    "${_audio_assistant_qml_dir}/Composer.qml"
    "${_audio_assistant_qml_dir}/ConversationView.qml"
    "${_audio_assistant_qml_dir}/ChatPanel.qml"
    "${_audio_assistant_qml_dir}/ChatView.qml"
    "${_audio_assistant_qml_dir}/ProviderSettingsPanel.qml"
    "${_audio_assistant_qml_dir}/CaptureSettingsPanel.qml"
    "${_audio_assistant_qml_dir}/SettingsDialog.qml"
)

foreach(_qml_file IN LISTS _audio_assistant_qml_files)
    get_filename_component(_qml_name "${_qml_file}" NAME)
    set_source_files_properties("${_qml_file}"
        PROPERTIES QT_RESOURCE_ALIAS "${_qml_name}"
    )
endforeach()

# Theme.qml is a `pragma Singleton`; qt_add_qml_module needs the file property
# (the pragma alone is not enough) so the generated qmldir marks it a singleton.
set_source_files_properties("${_audio_assistant_qml_dir}/Theme.qml"
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(audio-assistant-domain
    URI "AudioAssistant"
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/AudioAssistant
    QML_FILES ${_audio_assistant_qml_files}
)
