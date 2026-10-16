# Готовые решения для AI-чата на Qt

Дата первичного обзора: 2026-10-16. Это shortlist для предварительного анализа, а не список
одобренных зависимостей. Перед использованием повторно проверить репозиторий, лицензию, актуальный
commit, security issues и реальную сборку. Ничего из списка не подключать автоматически.

## Рекомендуемый порядок

1. Использовать Qt Quick Controls и `QAbstractListModel` для собственной domain-модели сообщений.
2. Вынести LLM в provider-neutral интерфейс с поддержкой streaming, cancellation и typed errors.
3. Первую сетевую реализацию строить на `Qt6::Network`, не привязывая domain к OpenAI DTO.
4. Подключать генератор или библиотеку только если spike докажет заметную экономию сопровождения.
5. Локальную модель рассматривать отдельным provider, а не встраивать её в UI/chat model.

Внутренние коллекции во всех заимствованных или адаптированных решениях должны быть переведены на
STL. Наличие `QList`, `QVector`, `QMap`, `QHash`, `QStringList`, `QVariantList` и других
Qt-контейнеров в стороннем примере не является основанием переносить их в проект. Qt-контейнер
допустим лишь как краткоживущий тип внутри неизбежного boundary adapter к конкретному Qt API.

QML-слой должен принадлежать проекту и оставаться напрямую редактируемым. Из сторонних приложений
следует брать паттерны или небольшие лицензионно совместимые компоненты, но не всю структуру экранов.
После адаптации компонент хранится в проекте, получает собственный стабильный API, тесты и design
tokens; источник и лицензия фиксируются. Внешний QML framework не должен становиться обязательным
посредником для изменения chat UI.

## Кандидаты первого приоритета

### Qt Quick Controls — Chat Tutorial

- URL: <https://doc.qt.io/qt-6/qtquickcontrols-chattutorial-example.html>
- Статус: **ADAPT**, основной UI/model reference.
- Что даёт: `ListView`, delegates, модель сообщений, адаптивная раскладка chat bubbles.
- Плюсы: официальный пример Qt; примеры Qt доступны по BSD-3-Clause; переносимые Qt primitives.
- Ограничения: это обычный чат, без LLM streaming, cancellation, Markdown и provider layer.
- Решение: брать концепции и минимальные фрагменты с attribution; не переносить SQL и лишние части.

### Qt OpenAPI — ChatGPT RESTful API Client

- URL: <https://doc.qt.io/qt-6/qtopenapi-chatgptclient-example.html>
- Статус: **REFERENCE-ONLY сейчас**, возможный **ADAPT** после обновления Qt.
- Что даёт: генерацию Qt Network клиента из OpenAPI, вызов Responses API, связь C++ с QML.
- Плюсы: официальный современный пример Qt; сокращает ручное написание DTO/HTTP boilerplate.
- Ограничения: Qt OpenAPI в Qt 6.12 имеет статус Technology Preview и не входит в текущий Qt 6.4.2;
  пример реализует только часть API; generated code всё равно требует аудита и тестов.
- Решение: не повышать минимальную версию Qt и не добавлять Java/OpenAPI generator без отдельного
  ADR и spike. Архитектурные идеи допустимы, singleton из примера в проект не переносить.

### Qt Edge AI — qt-ai-app

- URL: <https://git.qt.io/edge-ai/qt-ai-app>
- Статус: **REFERENCE-ONLY**.
- Что даёт: QML ChatView/ChatMessageModel и цепочку speech-to-text → LLM → другие modality.
- Плюсы: особенно релевантно аудио-ассистенту; показывает границы UI и AI-компонентов.
- Ограничения: сам проект называет API proof-of-concept/early phase; требует Ollama, Whisper и Piper;
  минимальная версия указана ориентировочно как Qt 6.7; production-стабильность не обещана.
- Решение: изучать потоки данных и UX, но не зависеть от экспериментального API.

## Сторонние кандидаты для spike

### EasyUI / EasyChat

- URL: <https://github.com/wanghonghust/EasyUI>
- Лицензия по репозиторию: MIT — перепроверить конкретные bundled third-party компоненты.
- Статус: **SPIKE / ADAPT SELECTIVELY**.
- Что даёт: multi-session OpenAI-compatible chat, SSE streaming, function calling, историю,
  GFM Markdown, syntax highlighting и QML-компоненты.
- Плюсы: наиболее полный найденный Qt 6/QML reference, потенциально экономит много UI-времени.
- Риски: заявлен Qt 6.8+, что выше текущего Qt 6.4.2; есть bundled/prebuilt компоненты; нужно
  проверить Linux/macOS, Android, ABI, provenance библиотек Markdown и QWindowKit, тесты и CI.
- Решение: сначала изолированный spike на всех desktop-платформах. Предпочитать перенос идей или
  малых компонентов полному включению framework.

### qopenai (QML plugin)

- URL: <https://github.com/omrdk/qopenai>
- Лицензия по репозиторию: MIT.
- Статус: **EVALUATE**, не default.
- Что даёт: QML API для chat completions, transcription, translation, images и embeddings.
- Плюсы: Qt 6.5; автор заявляет проверку Windows, macOS и iOS.
- Риски: маленький проект и низкая активность; нет подтверждения Linux/Android; API мог устареть;
  прямая provider-зависимость в QML противоречит архитектуре проекта.
- Решение: допустим только за C++ adapter/provider interface после тестов streaming/errors/cancel.

### QOpenAI

- URL: <https://github.com/s12mmm3/QOpenAI>
- Документация: <https://s12mmm3.github.io/QOpenAI/>
- Лицензия по документации: MIT.
- Статус: **EVALUATE**.
- Что даёт: OpenAI-compatible API поверх Qt без libcurl, заявлены Qt 5.12+ и Qt 6.
- Плюсы: потенциально подходит разным OpenAI-compatible endpoints.
- Риски: примеры демонстрируют API, которое может блокировать event loop; стиль CMake использует
  глобальный `include_directories`; необходимо проверить streaming, cancellation, timeout, TLS,
  proxy, Android и соответствие строгой диагностике.
- Решение: только spike с mock HTTP server и собственным асинхронным adapter; не копировать CMake.

### QtOpenAI

- URL: <https://github.com/ahgq7/QtOpenAI>
- Лицензия по репозиторию: MIT.
- Статус: **REFERENCE-ONLY / HIGH RISK**.
- Что даёт: асинхронные Qt signals/slots для chat, embeddings и image generation.
- Риски: молодой малый проект с небольшим числом commits; неизвестны streaming, CI и полноценная
  кроссплатформенная проверка; API-примеры используют raw pointers.
- Решение: не принимать без code audit, sanitizers и сравнительного spike с прямым Qt Network.

### AIChatGUI

- URL: <https://github.com/DmytroGio/AIChatGUI>
- Лицензия по репозиторию: MIT.
- Статус: **REFERENCE-ONLY**.
- Что даёт: Qt 6/QML UI, message model, token streaming, Markdown и прямой llama.cpp connector.
- Риски: фактически Windows/NVIDIA/CUDA-oriented; prebuilt llama libraries; нет подтверждённой
  Linux/macOS/Android переносимости; тесная связь UI, session manager и inference.
- Решение: изучать message model/streaming UX, но не переносить платформенную интеграцию.

### llama.cpp

- URL: <https://github.com/ggml-org/llama.cpp>
- Статус: **POTENTIAL PROVIDER**, только отдельным этапом.
- Что даёт: зрелый локальный inference и OpenAI-compatible `llama-server`.
- Плюсы: можно сначала интегрировать через общий HTTP provider и не связывать приложение с ABI
  inference-библиотеки; поддерживает разные CPU/GPU backends.
- Риски: крупная быстро меняющаяся зависимость, модели занимают много места; platform/backend matrix,
  лицензии моделей, память, startup time, packaging и Android требуют отдельного решения.
- Решение: для MVP предпочесть внешний `llama-server` через тот же provider-neutral HTTP контракт.
  Встраивание библиотеки — отдельный ADR после измерений.

## Отклонённые или низкоприоритетные варианты

- `D7EAD/liboai`: MIT, но репозиторий прямо сообщает, что больше активно не поддерживается;
  **REJECT** для новой зависимости.
- `MartinDelille/chat-qt`: GPL-3.0, старый qmake/Qt chat client, не LLM; **REFERENCE-ONLY**, обычно
  несовместим с желанием сохранить permissive/proprietary варианты лицензирования.
- `QtGraphicalEffects`: deprecated и не является частью Qt 6; для новых эффектов использовать
  штатные Qt 6 решения (`MultiEffect` в подходящей версии), а не тащить compatibility module.

## Архитектурные элементы, которые стоит переиспользовать как идеи

- `QAbstractListModel` сообщения с устойчивым ID, role, state и immutable content snapshot;
- отдельный append-only поток chunks, который агрегируется application layer, а не QML;
- состояния запроса: queued, streaming, completed, cancelled, failed;
- явная отмена и timeout, backpressure/throttling обновлений UI;
- provider-neutral capability model вместо проверки имени провайдера;
- retry только для доказанно безопасных/idempotent операций;
- секреты через platform-appropriate secure storage, никогда не в QML/settings plaintext;
- Markdown renderer считать security boundary: запрещать произвольный HTML/JS и небезопасные URL;
- сетевые тесты выполнять против локального mock server: chunks, разрыв SSE, malformed JSON,
  HTTP/TLS ошибки, rate limits, cancellation и timeout.

## Gate перед окончательным выбором

Для каждого кандидата создать короткий ADR и минимальный spike. Кандидат принимается только если:

- собирается Clang с правилами проекта без отключения `-Werror` для собственного adapter-кода;
- проходит ASan/UBSan и тесты ошибок;
- не протекает типами провайдера в domain/UI;
- подтверждён на Linux, Windows и macOS либо изолирован как platform-specific;
- не блокирует Android архитектурно;
- лицензия и все transitives документированы;
- измеренная стоимость адаптации ниже собственной минимальной реализации.
