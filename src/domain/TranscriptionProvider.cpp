#include "domain/TranscriptionProvider.h"

// Out-of-line destructor: it is the class's key function, so the vtable is
// emitted once, in the domain library, instead of as a weak symbol in every
// translation unit that includes the header (mirrors Provider/Answer and keeps
// the strict -Wweak-vtables profile clean). AUTOMOC picks the header up through
// this include, so the moc-generated signal bodies land here too.
TranscriptionProvider::~TranscriptionProvider() = default;
