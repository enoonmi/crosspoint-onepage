#pragma once
// Host stub: the real lib/Memory/Memory.h provides makeUniqueNoThrow.
// The IO layer avoids it for trivial byte buffers and uses std::unique_ptr
// directly, so nothing further is needed here.
#include "Arduino.h"
