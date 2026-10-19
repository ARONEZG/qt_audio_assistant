#include "domain/SttLanguage.h"

QString sttLanguageFromEnvironment(const QProcessEnvironment& environment) {
    // QProcessEnvironment::value returns an empty string both when the variable
    // is absent and when it is explicitly empty. contains() disambiguates: an
    // absent variable means "use the default", while an explicitly empty value
    // means "leave autodetection to the endpoint".
    if (!environment.contains(QStringLiteral("POLZA_STT_LANGUAGE"))) {
        return QStringLiteral("ru");
    }
    return environment.value(QStringLiteral("POLZA_STT_LANGUAGE"));
}
