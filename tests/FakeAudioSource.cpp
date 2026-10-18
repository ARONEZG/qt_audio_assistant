#include "FakeAudioSource.h"

// Out-of-line definitions give the double a key function, so its vtable is
// emitted in this translation unit instead of every user (-Wweak-vtables).
FakeAudioSource::FakeAudioSource() = default;

FakeAudioSource::~FakeAudioSource() = default;
